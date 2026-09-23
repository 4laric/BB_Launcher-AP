// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ApFork: fork identity and update-channel separation for BBLauncher-AP,
// the Archipelago-integrated fork of BBLauncher.
//
// Upstream baseline: rainmakerv3/BB_Launcher at
// ca12c2fc38b8ba485e508bde815e8ea8cb49ac10.
//
// An update must never replace the fork with upstream BBLauncher. Every
// update source below resolves to the fork's own channel, and the asset
// filter additionally requires the fork marker in the asset name, so a
// misconfigured feed still cannot install an upstream build. Ordinary
// (non-BB_AP_FORK) builds keep the exact upstream endpoints.

#pragma once

#include <QString>

namespace ApFork {

inline QString AppName() {
#ifdef BB_AP_FORK
    return QStringLiteral("BBLauncher-AP");
#else
    return QStringLiteral("BBLauncher");
#endif
}

inline QString UpdateApiUrl() {
#ifdef BB_AP_FORK
    // Fork-only channel. Never the upstream releases endpoint.
    return QStringLiteral("https://api.github.com/repos/4laric/BB_Launcher-AP/releases");
#else
    return QStringLiteral("https://api.github.com/repos/rainmakerv3/BB_Launcher/releases");
#endif
}

inline QString CompareUrl(const QString& from, const QString& to) {
#ifdef BB_AP_FORK
    return QStringLiteral("https://api.github.com/repos/4laric/BB_Launcher-AP/compare/%1...%2")
        .arg(from, to);
#else
    return QStringLiteral("https://api.github.com/repos/rainmakerv3/BB_Launcher/compare/%1...%2")
        .arg(from, to);
#endif
}

// Fork builds only accept assets explicitly published for the fork.
inline bool AssetMatches(const QString& assetName, const QString& branch,
                         const QString& platformString) {
#ifdef BB_AP_FORK
    if (!assetName.contains(QStringLiteral("BBLauncher-AP"))) {
        return false;
    }
#else
    (void)platformString;
#endif
    if (branch == QStringLiteral("noUAC")) {
        return assetName.contains(QStringLiteral("noUAC"));
    }
    if (branch == QStringLiteral("downloader")) {
        return assetName.contains(QStringLiteral("Downloader"));
    }
#ifdef BB_AP_FORK
    return assetName.contains(platformString);
#else
    return assetName.contains(platformString);
#endif
}

// Machine-readable backend protocol spoken by the bundled AP backend.
inline QString BackendProtocol() {
    return QStringLiteral("bb-ap-integration-v1");
}

inline QString UpstreamBaseline() {
    return QStringLiteral("ca12c2fc38b8ba485e508bde815e8ea8cb49ac10");
}

} // namespace ApFork
