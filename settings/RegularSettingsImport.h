// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>

namespace RegularSettingsImport {

// Only used before Config::LoadSettings creates the fork's first settings file.
// Returns the source settings file when an import was written.
std::filesystem::path ImportFirstRun(const std::filesystem::path& targetSettings,
                                     const std::filesystem::path& applicationDir,
                                     const std::filesystem::path& userHome);

} // namespace RegularSettingsImport
