// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RegularSettingsImport.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <toml.hpp>

namespace RegularSettingsImport {
namespace {

using Path = std::filesystem::path;

std::string Utf8(const Path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::optional<Path> ExistingPath(const toml::value& launcher, const char* key,
                                 const Path& sourceRoot, bool directory) {
    const auto raw = toml::find_or<std::string>(launcher, key, "");
    if (raw.empty()) return std::nullopt;
    try {
        Path path = std::filesystem::u8path(raw);
        if (path.is_relative()) path = sourceRoot / path;
        std::error_code error;
        path = std::filesystem::weakly_canonical(path, error);
        if (error || (directory ? !std::filesystem::is_directory(path)
                                : !std::filesystem::is_regular_file(path))) {
            return std::nullopt;
        }
        return path;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

struct Candidate {
    Path file;
    toml::value imported;
    int validPaths = 0;
    std::filesystem::file_time_type modified;
};

std::optional<Candidate> ReadCandidate(const Path& file, const Path& userHome) {
    try {
        std::ifstream input(file, std::ios::binary);
        if (!input) return std::nullopt;
        const toml::value source = toml::parse(input, Utf8(file.filename()));
        if (!source.contains("Launcher")) return std::nullopt;
        const auto& launcher = source.at("Launcher");
        const Path sourceRoot = file.parent_path().parent_path();
        const auto game = ExistingPath(launcher, "installPath", sourceRoot, true);
        const auto emulator = ExistingPath(launcher, "shadPath-New", sourceRoot, false);
        const int validPaths = int(game.has_value()) + int(emulator.has_value());
        if (validPaths == 0) return std::nullopt;

        toml::value imported;
        if (game) imported["Launcher"]["installPath"] = Utf8(*game);
        if (emulator) imported["Launcher"]["shadPath-New"] = Utf8(*emulator);

        const auto theme = toml::find_or<std::string>(launcher, "Theme", "Dark");
        if (theme == "Dark" || theme == "Light") imported["Launcher"]["Theme"] = theme;
        imported["Launcher"]["SoundFixEnabled"] =
            toml::find_or<bool>(launcher, "SoundFixEnabled", true);

        const int userFolder = toml::find_or<int>(launcher, "UserFolderLocation", 0);
        if (userFolder == 2) {
            if (const auto custom = ExistingPath(launcher, "CustomUserFolder", sourceRoot, true)) {
                imported["Launcher"]["UserFolderLocation"] = 2;
                imported["Launcher"]["CustomUserFolder"] = Utf8(*custom);
            }
        } else if (userFolder == 1) {
            // LauncherFolder uses <old launcher>/user, then falls back to AppData/shadPS4.
            const Path oldUser = sourceRoot / "user";
            const Path fallbackUser = userHome / "AppData" / "Roaming" / "shadPS4";
            const Path oldLocation = std::filesystem::is_directory(oldUser) ? oldUser : fallbackUser;
            if (std::filesystem::is_directory(oldLocation)) {
                imported["Launcher"]["UserFolderLocation"] = 2;
                imported["Launcher"]["CustomUserFolder"] = Utf8(oldLocation);
            }
        }

        if (source.contains("Backups")) {
            const auto& backups = source.at("Backups");
            imported["Backups"]["BackupSaveEnabled"] =
                toml::find_or<bool>(backups, "BackupSaveEnabled", true);
            const int interval = toml::find_or<int>(backups, "BackupInterval", 10);
            const int number = toml::find_or<int>(backups, "BackupNumber", 2);
            if (interval > 0 && interval <= 1440) imported["Backups"]["BackupInterval"] = interval;
            if (number > 0 && number <= 100) imported["Backups"]["BackupNumber"] = number;
        }
        if (source.contains("Trophy")) {
            const auto& trophy = source.at("Trophy");
            imported["Trophy"]["ShowEarned"] = toml::find_or<bool>(trophy, "ShowEarned", true);
            imported["Trophy"]["ShowUnearned"] = toml::find_or<bool>(trophy, "ShowUnearned", true);
            imported["Trophy"]["ShowHidden"] = toml::find_or<bool>(trophy, "ShowHidden", false);
        }
        return Candidate{file, std::move(imported), validPaths, std::filesystem::last_write_time(file)};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool RegularInstallName(const Path& folder) {
    const auto name = Utf8(folder.filename());
    return (name.starts_with("BB_Launcher") || name.starts_with("BBLauncher")) &&
           name.find("-AP") == std::string::npos;
}

void AddInstallCandidates(std::vector<Path>& files, const Path& parent) {
    std::error_code error;
    if (!std::filesystem::is_directory(parent, error)) return;
    for (std::filesystem::directory_iterator it(parent, error), end; !error && it != end;
         it.increment(error)) {
        if (!it->is_directory(error) || !RegularInstallName(it->path())) continue;
        files.push_back(it->path() / "BBLauncher" / "LauncherSettings.toml");
    }
}

} // namespace

Path ImportFirstRun(const Path& targetSettings, const Path& applicationDir, const Path& userHome) {
    std::error_code error;
    if (std::filesystem::exists(targetSettings, error) || error) return {};

    std::vector<Path> files;
    AddInstallCandidates(files, applicationDir.parent_path());
    AddInstallCandidates(files, userHome / "Downloads");
    files.push_back(userHome / "AppData" / "Roaming" / "BBLauncher" / "LauncherSettings.toml");
    files.push_back(userHome / "AppData" / "Local" / "BBLauncher" / "LauncherSettings.toml");

    std::vector<Candidate> candidates;
    std::set<Path> seen;
    const auto targetAbsolute = std::filesystem::absolute(targetSettings).lexically_normal();
    for (const auto& file : files) {
        const auto absolute = std::filesystem::absolute(file).lexically_normal();
        if (absolute == targetAbsolute || !seen.insert(absolute).second) continue;
        if (auto candidate = ReadCandidate(absolute, userHome)) candidates.push_back(std::move(*candidate));
    }
    if (candidates.empty()) return {};

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.validPaths != b.validPaths) return a.validPaths > b.validPaths;
        if (a.modified != b.modified) return a.modified > b.modified;
        return a.file.native() < b.file.native();
    });

    std::filesystem::create_directories(targetSettings.parent_path(), error);
    if (error || std::filesystem::exists(targetSettings)) return {};
    // Write a new file only; never copy source text or replace fork settings.
    std::ofstream output(targetSettings, std::ios::binary | std::ios::out | std::ios::noreplace);
    if (!output) return {};
    output << candidates.front().imported;
    output.close();
    if (!output) {
        std::filesystem::remove(targetSettings, error);
        return {};
    }
    return candidates.front().file;
}

} // namespace RegularSettingsImport
