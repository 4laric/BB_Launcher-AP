// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ApCoordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "ApFork.h"
#include "modules/Common.h"
#include "settings/updater/BuildInfo.h"

namespace {
QString ModsRootFor(const QString& gameRoot) {
    // Mirrors ModManager's root computation: the BBLauncher Mods dir.
    return Common::ModPath.is_absolute()
               ? QString::fromStdWString(Common::ModPath.wstring())
               : QString();
}

QString InstallNameFor(const QString& gameRoot) {
    const QFileInfo info(gameRoot);
    return info.fileName().isEmpty() ? QStringLiteral("CUSA03173") : info.fileName();
}
} // namespace

ApCoordinator::ApCoordinator(EmulatorService* emulator, QObject* parent)
    : QObject(parent), m_emu(emulator) {}

bool ApCoordinator::Configure(const QString& gameRoot, const QString& backendDir,
                              const QString& stateRoot, QString* error) {
    m_gameRoot = gameRoot;
    m_backendDir = backendDir;
    m_stateRoot = stateRoot;
    const QString installName = InstallNameFor(gameRoot);
    const QFileInfo gameInfo(gameRoot);
    const QString gameParent = gameInfo.absolutePath();
    // Managed roots mirror the Mod Manager layout exactly so both UIs
    // share one activation owner: inactive Mods, active Mods, overlay,
    // backups. Mode is always copy for AP.
    const QString inactive = ModsRootFor(gameRoot);
    if (inactive.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("BBLauncher Mods directory is not configured");
        }
        return false;
    }
    const QString active =
        QFileInfo(Common::GetBBLFilesPath()).absoluteFilePath() +
        QStringLiteral("/Mods-Active (DO NOT DELETE)");
    const QString overlay = gameParent + QStringLiteral("/") + installName + QStringLiteral("-mods/dvdroot_ps4");
    const QString backups = gameParent + QStringLiteral("/") + installName + QStringLiteral("-modsBACKUP");
    const QString journal = stateRoot + QStringLiteral("/integrated/cxx-journal");
    m_mods = std::make_unique<modservice::ModService>(
        inactive.toStdWString(), active.toStdWString(), overlay.toStdWString(),
        backups.toStdWString(), modservice::OverlayMode::Copy, journal.toStdWString());
    ResetSession();
    if (m_emu != nullptr) {
        m_emu->setPreflightHandler(
            [this](const QString& action) { return Preflight(action); });
    }
    return true;
}

QString ApCoordinator::Preflight(const QString& action) {
    if (m_ownStart) {
        return {};
    }
    if (m_playing) {
        if (action == QStringLiteral("start") || action == QStringLiteral("restart")) {
            return tr("An Archipelago session is already running. "
                      "Use Return to game instead of starting a second copy.");
        }
        return {};
    }
    if (!m_armId.isEmpty()) {
        // Armed but not yet playing: only the coordinator's own start may
        // launch the game, so activation cannot drift under us.
        return tr("An Archipelago session is armed and starting. "
                  "Please wait for the game to launch.");
    }
    return {};
}

bool ApCoordinator::EnsureBackend(QString* error) {
    if (m_backend && m_backend->IsRunning()) {
        return true;
    }
    if (!m_backend) {
        m_backend = std::make_unique<ApBackend>(this);
    }
    return m_backend->Start(m_stateRoot, error);
}

bool ApCoordinator::Prepare(const ApPlayRequest& request, Prepared* prepared,
                            QString* error) {
    emit StageChanged(tr("Preparing your seed"));
    if (!EnsureBackend(error)) {
        return false;
    }
    // Inspect first so single-slot seeds skip the player question and
    // multi-slot seeds fail closed with a named choice.
    QJsonObject inspect{{QStringLiteral("seed_path"), request.seedPath}};
    if (!request.playerName.isEmpty()) {
        inspect.insert(QStringLiteral("player_name"), request.playerName);
    }
    ApResponse seen = m_backend->Call(QStringLiteral("inspect_seed"), inspect, 30000);
    if (!seen.ok) {
        if (error != nullptr) {
            *error = seen.error.detail;
        }
        return false;
    }
    const QString player = !request.playerName.isEmpty()
                               ? request.playerName
                               : seen.result.value(QStringLiteral("selected")).toString();
    QString server = request.server;
    if (server.isEmpty()) {
        server = seen.result.value(QStringLiteral("server")).toString();
    }
    QString client = m_backendDir + QStringLiteral("/ap-client/bb-ap-client");
#ifdef Q_OS_WIN
    if (QFileInfo::exists(client + QStringLiteral(".exe"))) {
        client += QStringLiteral(".exe");
    }
    // Otherwise the extensionless name is passed through and the backend
    // reports the missing bundle file instead of failing obscurely here.
#endif
    QJsonObject params{
        {QStringLiteral("game_root"), request.gameRoot},
        {QStringLiteral("mods_root"), ModsRootFor(request.gameRoot)},
        {QStringLiteral("seed_path"), request.seedPath},
        {QStringLiteral("player_name"), player},
        {QStringLiteral("server"), server},
        {QStringLiteral("shad_executable"), QString::fromStdWString(Common::shadPs4Executable.wstring())},
        {QStringLiteral("ap_client"), client},
        {QStringLiteral("suppression_dir"), m_backendDir + QStringLiteral("/suppression")},
        {QStringLiteral("cache_root"), m_stateRoot + QStringLiteral("/cache")},
    };
    if (!request.password.isEmpty()) {
        params.insert(QStringLiteral("password"), request.password);
    }
#ifdef BB_AP_FORK
    params.insert(QStringLiteral("fork_build"),
                  QJsonObject{{QStringLiteral("build"), QString::fromStdString(Common::VERSION)},
                              {QStringLiteral("commit"), QString::fromStdString(Build::Rev)},
                              {QStringLiteral("executable_sha256"),
                               EmulatorService::Sha256OfFile(
                                   QCoreApplication::applicationFilePath())}});
#endif
    ApResponse response = m_backend->Call(QStringLiteral("prepare_play"), params, 600000);
    if (!response.ok) {
        if (error != nullptr) {
            *error = response.error.detail;
        }
        return false;
    }
    m_playId = response.result.value(QStringLiteral("play_id")).toString();
    const QJsonObject display = response.result.value(QStringLiteral("display")).toObject();
    if (prepared != nullptr) {
        prepared->playId = m_playId;
        prepared->packageName =
            response.result.value(QStringLiteral("package_name")).toString();
        if (prepared->packageName.isEmpty()) {
            // The display title carries seed/slot when the package was
            // reused; fall back to the inactive-list match below.
            prepared->packageName = display.value(QStringLiteral("title")).toString();
        }
        prepared->seed = display.value(QStringLiteral("seed")).toString();
        prepared->slot = display.value(QStringLiteral("slot")).toString();
        prepared->server = display.value(QStringLiteral("server")).toString();
        prepared->title = display.value(QStringLiteral("title")).toString();
        prepared->reused = response.result.value(QStringLiteral("reused")).toBool();
    }
    m_title = display.value(QStringLiteral("title")).toString();
    m_playing = false;
    return true;
}

bool ApCoordinator::Activate(const Prepared& prepared, bool allowDisableConflicts,
                             bool* wasConflict, QString* error) {
    emit StageChanged(tr("Setting up your game"));
    m_lastErrorCode.clear();
    if (wasConflict != nullptr) {
        *wasConflict = false;
    }
    if (!m_mods) {
        if (error != nullptr) {
            *error = tr("Archipelago is not configured for this installation");
        }
        return false;
    }
    // Resolve the prepared package by exact name in the inactive mods;
    // a display title is never authorization, ModService validates.
    QString package = prepared.packageName;
    bool found = false;
    for (const std::string& name : m_mods->InactiveMods()) {
        if (QString::fromStdString(name) == package) {
            found = true;
            break;
        }
    }
    if (!found) {
        if (error != nullptr) {
            *error = tr("The prepared Archipelago package is not in the inactive mods. "
                        "Prepare again.");
        }
        return false;
    }
    modservice::Plan plan;
    modservice::Result planned =
        m_mods->PlanActivate(package.toStdString(), plan);
    if (!planned.ok && std::string(planned.code) == modservice::kConflict) {
        m_lastErrorCode = QString::fromStdString(planned.code);
        if (wasConflict != nullptr) {
            *wasConflict = true;
        }
        if (!allowDisableConflicts) {
            if (error != nullptr) {
                QString names;
                for (const std::string& entry : plan.conflictingMods) {
                    const QString text = QString::fromStdString(entry);
                    names += text.section(QChar(','), 1, 1).trimmed() + QStringLiteral(" ");
                }
                *error = tr("This mod conflicts with Archipelago: %1").arg(names.trimmed());
            }
            return false;
        }
        plan.conflictOverride = true;
        // Exact reversible plan: deactivate conflicting mods first, in
        // reverse ledger order, then proceed with activation.
        QStringList owners;
        for (const std::string& entry : plan.conflictingMods) {
            const QString owner =
                QString::fromStdString(entry).section(QChar(','), 1, 1).trimmed();
            if (!owner.isEmpty() && !owners.contains(owner)) {
                owners.push_back(owner);
            }
        }
        for (auto it = owners.rbegin(); it != owners.rend(); ++it) {
            modservice::Plan down;
            modservice::Result will = m_mods->PlanDeactivate(it->toStdString(), down);
            if (!will.ok) {
                if (error != nullptr) {
                    *error = QString::fromStdString(will.detail);
                }
                return false;
            }
            modservice::Result did = m_mods->Commit(down);
            if (!did.ok) {
                if (error != nullptr) {
                    *error = QString::fromStdString(did.detail);
                }
                return false;
            }
        }
    } else if (!planned.ok) {
        if (error != nullptr) {
            *error = QString::fromStdString(planned.detail);
        }
        return false;
    }
    modservice::Result done = m_mods->Commit(plan);
    if (!done.ok) {
        if (error != nullptr) {
            *error = QString::fromStdString(done.detail);
        }
        return false;
    }
    return true;
}

bool ApCoordinator::Arm(const Prepared& prepared, QString* error) {
    emit StageChanged(tr("Verifying your game setup"));
    const QString modsRoot = ModsRootFor(m_gameRoot);
    ApResponse response = m_backend->Call(
        QStringLiteral("verify_and_arm"),
        QJsonObject{{QStringLiteral("play_id"), prepared.playId},
                    {QStringLiteral("game_root"), m_gameRoot},
                    {QStringLiteral("mods_root"), modsRoot},
                    {QStringLiteral("server"), prepared.server}},
        120000);
    if (!response.ok) {
        if (error != nullptr) {
            *error = response.error.detail;
        }
        return false;
    }
    m_armId = response.result.value(QStringLiteral("arm_id")).toString();
    return true;
}

bool ApCoordinator::StartGame(QString* error) {
    emit StageChanged(tr("Starting Bloodborne"));
    if (m_emu == nullptr) {
        if (error != nullptr) {
            *error = tr("Emulator service is unavailable");
        }
        return false;
    }
    QString exe;
    Common::PathToQString(exe, Common::shadPs4Executable);
    QFileInfo fileInfo(exe);
    if (!fileInfo.exists()) {
        if (error != nullptr) {
            *error = tr("shadPS4 build not found. Install shadPS4 first.");
        }
        return false;
    }
    QString eboot;
    Common::PathToQString(eboot, Common::installPath / "eboot.bin");
    const QStringList args{QStringLiteral("--game"), eboot};
    const QString workDir = fileInfo.absolutePath();
    m_ownStart = true;
    EmulatorProcessIdentity identity;
    const bool started = m_emu->Start(fileInfo, args, workDir, &identity, error);
    m_ownStart = false;
    if (!started) {
        return false;
    }
    if (!identity.valid) {
        if (error != nullptr) {
            *error = tr("Could not establish the emulator process identity");
        }
        return false;
    }
    m_gameIdentity = identity;
    return true;
}

bool ApCoordinator::Connect(QString* error) {
    emit StageChanged(tr("Connecting to Archipelago"));
    const EmulatorProcessIdentity& identity = m_gameIdentity;
    QJsonObject params{{QStringLiteral("arm_id"), m_armId},
                       {QStringLiteral("game_root"), m_gameRoot},
                       {QStringLiteral("mods_root"), ModsRootFor(m_gameRoot)}};
    if (identity.valid) {
        params.insert(QStringLiteral("process"),
                      QJsonObject{{QStringLiteral("executable"), identity.executable},
                                  {QStringLiteral("executable_sha256"),
                                   identity.executableSha256},
                                  {QStringLiteral("pid"), identity.pid},
                                  {QStringLiteral("creation_time"),
                                   QString::number(identity.creationTime)}});
    }
    ApResponse response = m_backend->Call(QStringLiteral("connect_and_start_client"), params,
                                          180000);
    if (!response.ok) {
        m_lastErrorCode = response.error.code;
        if (error != nullptr) {
            *error = response.error.detail;
        }
        return false;
    }
    m_sessionId = response.result.value(QStringLiteral("session_id")).toString();
    m_playing = true;
    emit StageChanged(tr("Ready to play"));
    return true;
}

bool ApCoordinator::RefreshStatus(QString* stateOut, QString* error) {
    if (!m_backend || !m_backend->IsRunning() || m_playId.isEmpty()) {
        return false;
    }
    ApResponse response = m_backend->Call(
        QStringLiteral("session_status"),
        QJsonObject{{QStringLiteral("play_id"), m_playId}}, 15000);
    if (!response.ok) {
        if (error != nullptr) {
            *error = response.error.detail;
        }
        return false;
    }
    const QString state = response.result.value(QStringLiteral("state")).toString();
    if (stateOut != nullptr) {
        *stateOut = state;
    }
    m_playing = (state == QStringLiteral("playing"));
    return true;
}

bool ApCoordinator::SwitchToRegularPlay(QString* error) {
    // Removes the AP package through the same service and retains
    // unrelated mods. Never called "vanilla" while other mods remain.
    if (m_backend && !m_sessionId.isEmpty()) {
        m_backend->Call(QStringLiteral("stop_client"),
                        QJsonObject{{QStringLiteral("session_id"), m_sessionId}}, 15000);
    }
    ResetSession();
    if (m_mods) {
        for (const std::string& name : m_mods->ActiveMods()) {
            const QString mod = QString::fromStdString(name);
            if (mod.startsWith(QStringLiteral("Archipelago-"))) {
                modservice::Plan down;
                modservice::Result will = m_mods->PlanDeactivate(name, down);
                if (!will.ok) {
                    if (error != nullptr) {
                        *error = QString::fromStdString(will.detail);
                    }
                    return false;
                }
                modservice::Result did = m_mods->Commit(down);
                if (!did.ok) {
                    if (error != nullptr) {
                        *error = QString::fromStdString(did.detail);
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

void ApCoordinator::ResetSession() {
    m_playId.clear();
    m_armId.clear();
    m_sessionId.clear();
    m_title.clear();
    m_gameIdentity = EmulatorProcessIdentity{};
    m_playing = false;
}

int ApCoordinator::RunHeadless(const ApPlayRequest& request, QString* error) {
    // Reuse the previous session when it is still alive instead of
    // spawning a duplicate client.
    if (!m_playId.isEmpty() && m_playing) {
        QString state;
        if (RefreshStatus(&state, nullptr) && m_playing) {
            return 0;
        }
    }
    Prepared prepared;
    if (!Prepare(request, &prepared, error)) {
        return 1;
    }
    bool wasConflict = false;
    if (!Activate(prepared, false, &wasConflict, error)) {
        return 1;
    }
    if (!Arm(prepared, error)) {
        return 1;
    }
    if (!StartGame(error)) {
        return 1;
    }
    if (!Connect(error)) {
        return 1;
    }
    return 0;
}
