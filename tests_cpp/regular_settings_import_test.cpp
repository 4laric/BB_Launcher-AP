// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTemporaryDir>

#include <fstream>
#include <filesystem>
#include <chrono>

#include <toml.hpp>

#include "settings/RegularSettingsImport.h"

namespace {
using Path = std::filesystem::path;

Path AsPath(const QString& text) {
    return std::filesystem::u8path(text.toUtf8().toStdString());
}

std::string Utf8(const Path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void WriteToml(const Path& path, const toml::value& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << data;
}

toml::value ReadToml(const Path& path) {
    std::ifstream input(path, std::ios::binary);
    return toml::parse(input, "settings");
}

struct Fixture {
    QTemporaryDir scratch;
    Path home = AsPath(scratch.path()) / "home";
    Path app = AsPath(scratch.path()) / "BBLauncher-AP";
    Path target = app / "BBLauncher" / "LauncherSettings.toml";
    Path game = AsPath(scratch.path()) / "game";
    Path emulator = AsPath(scratch.path()) / "shad" / "shadPS4.exe";

    Fixture() {
        std::filesystem::create_directories(game);
        std::filesystem::create_directories(emulator.parent_path());
        std::ofstream(emulator).put('x');
    }

    Path Source(const std::string& name, toml::value data) {
        const Path file = home / "Downloads" / name / "BBLauncher" / "LauncherSettings.toml";
        WriteToml(file, data);
        return file;
    }

    toml::value Settings() const {
        toml::value data;
        data["Launcher"]["installPath"] = Utf8(game);
        data["Launcher"]["shadPath-New"] = Utf8(emulator);
        data["Launcher"]["Theme"] = "Light";
        data["Launcher"]["SoundFixEnabled"] = false;
        data["Launcher"]["ApiKey"] = "do-not-copy";
        data["Launcher"]["AutoUpdateEnabled"] = true;
        data["Backups"]["BackupSaveEnabled"] = false;
        data["Backups"]["BackupInterval"] = 20;
        data["Backups"]["BackupNumber"] = 3;
        data["Trophy"]["ShowHidden"] = true;
        data["Builds-New"]["Secret"] = "do-not-copy";
        return data;
    }
};
} // namespace

class RegularSettingsImportTest : public QObject {
    Q_OBJECT
private slots:
    void importsAllowlistWithoutTouchingSource() {
        Fixture f;
        const auto source = f.Source("BB_Launcher-regular", f.Settings());
        const auto original = ReadToml(source);
        QVERIFY(RegularSettingsImport::ImportFirstRun(f.target, f.app, f.home) == source);
        const auto imported = ReadToml(f.target);
        QVERIFY(std::filesystem::u8path(toml::find<std::string>(imported, "Launcher", "installPath")) ==
                std::filesystem::weakly_canonical(f.game));
        QVERIFY(std::filesystem::u8path(toml::find<std::string>(imported, "Launcher", "shadPath-New")) ==
                std::filesystem::weakly_canonical(f.emulator));
        QCOMPARE(toml::find<std::string>(imported, "Launcher", "Theme"), std::string("Light"));
        QVERIFY(!toml::find<bool>(imported, "Launcher", "SoundFixEnabled"));
        QCOMPARE(toml::find<int>(imported, "Backups", "BackupInterval"), 20);
        QVERIFY(toml::find<bool>(imported, "Trophy", "ShowHidden"));
        QVERIFY(!imported.at("Launcher").contains("ApiKey"));
        QVERIFY(!imported.at("Launcher").contains("AutoUpdateEnabled"));
        QVERIFY(!imported.contains("Builds-New"));
        QVERIFY(ReadToml(source) == original);
    }

    void existingForkSettingsNeverOverwritten() {
        Fixture f;
        f.Source("BB_Launcher-regular", f.Settings());
        toml::value existing;
        existing["Launcher"]["installPath"] = "fork-choice";
        WriteToml(f.target, existing);
        QVERIFY(RegularSettingsImport::ImportFirstRun(f.target, f.app, f.home).empty());
        QVERIFY(ReadToml(f.target) == existing);
    }

    void resolvesRelativePathsAndPreservesOldLauncherFolder() {
        Fixture f;
        const Path root = f.home / "Downloads" / "BB_Launcher-relative";
        std::filesystem::create_directories(root / "game");
        std::filesystem::create_directories(root / "emu");
        std::ofstream(root / "emu" / "shadPS4.exe").put('x');
        toml::value data = f.Settings();
        data["Launcher"]["installPath"] = "game";
        data["Launcher"]["shadPath-New"] = "emu/shadPS4.exe";
        data["Launcher"]["UserFolderLocation"] = 1;
        std::filesystem::create_directories(root / "user");
        f.Source("BB_Launcher-relative", data);
        QVERIFY(!RegularSettingsImport::ImportFirstRun(f.target, f.app, f.home).empty());
        const auto imported = ReadToml(f.target);
        QVERIFY(std::filesystem::u8path(toml::find<std::string>(imported, "Launcher", "installPath")) ==
                std::filesystem::weakly_canonical(root / "game"));
        QVERIFY(std::filesystem::u8path(toml::find<std::string>(imported, "Launcher", "shadPath-New")) ==
                std::filesystem::weakly_canonical(root / "emu" / "shadPS4.exe"));
        QCOMPARE(toml::find<int>(imported, "Launcher", "UserFolderLocation"), 2);
        QVERIFY(std::filesystem::u8path(toml::find<std::string>(imported, "Launcher", "CustomUserFolder")) ==
                std::filesystem::weakly_canonical(root / "user"));
    }

    void preservesLauncherFolderFallbackToAppData() {
        Fixture f;
        const Path fallback = f.home / "AppData" / "Roaming" / "shadPS4";
        std::filesystem::create_directories(fallback);
        auto data = f.Settings();
        data["Launcher"]["UserFolderLocation"] = 1;
        f.Source("BB_Launcher-old", data);
        QVERIFY(!RegularSettingsImport::ImportFirstRun(f.target, f.app, f.home).empty());
        const auto imported = ReadToml(f.target);
        QCOMPARE(toml::find<int>(imported, "Launcher", "UserFolderLocation"), 2);
        QVERIFY(std::filesystem::u8path(toml::find<std::string>(imported, "Launcher", "CustomUserFolder")) ==
                std::filesystem::weakly_canonical(fallback));
    }

    void prefersCompleteAndThenRecentRegularInstallation() {
        Fixture f;
        auto incomplete = f.Settings();
        incomplete["Launcher"]["shadPath-New"] = "missing.exe";
        const auto weak = f.Source("BB_Launcher-weak", incomplete);
        const auto older = f.Source("BB_Launcher-older", f.Settings());
        const auto newer = f.Source("BB_Launcher-newer", f.Settings());
        f.Source("BBLauncher-AP-newest", f.Settings());
        const auto now = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(weak, now);
        std::filesystem::last_write_time(older, now - std::chrono::hours(2));
        std::filesystem::last_write_time(newer, now - std::chrono::hours(1));
        QVERIFY(RegularSettingsImport::ImportFirstRun(f.target, f.app, f.home) == newer);
    }

    void noValidPathsLeavesFirstRunUntouched() {
        Fixture f;
        toml::value data = f.Settings();
        data["Launcher"]["installPath"] = "missing-game";
        data["Launcher"]["shadPath-New"] = "missing.exe";
        f.Source("BB_Launcher-invalid", data);
        QVERIFY(RegularSettingsImport::ImportFirstRun(f.target, f.app, f.home).empty());
        QVERIFY(!std::filesystem::exists(f.target));
    }
};

QTEST_GUILESS_MAIN(RegularSettingsImportTest)
#include "regular_settings_import_test.moc"
