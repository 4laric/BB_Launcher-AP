// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ApCoordinator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <iterator>

#include "ApFork.h"
#include "modules/Common.h"
#include "settings/updater/BuildInfo.h"

namespace {
QString ModsRootFor(const QString& gameRoot) {
    // Mirrors ModManager's root computation: the BBLauncher Mods dir.
    const std::filesystem::path mods = Common::GetBBLFilesPath() / "Mods";
    return mods.is_absolute() ? QString::fromStdWString(mods.wstring()) : QString();
}

QString InstallNameFor(const QString& gameRoot) {
    const QFileInfo info(gameRoot);
    return info.fileName().isEmpty() ? QStringLiteral("CUSA03173") : info.fileName();
}

QString AbsoluteClean(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
} // namespace

ApCoordinator::ApCoordinator(EmulatorService* emulator, QObject* parent)
    : QObject(parent), m_emu(emulator) {}

bool ApCoordinator::Configure(const QString& gameRoot, const QString& backendDir,
                              const QString& stateRoot, QString* error,
                              const ApModRoots& requestedRoots, bool startBackend) {
    ApModRoots roots = requestedRoots;
    const QString installName = InstallNameFor(gameRoot);
    const QFileInfo gameInfo(gameRoot);
    const QString gameParent = gameInfo.absolutePath();
    if (roots.inactive.isEmpty()) roots.inactive = ModsRootFor(gameRoot);
    if (roots.active.isEmpty()) {
        roots.active = QFileInfo(Common::GetBBLFilesPath()).absoluteFilePath() +
                       QStringLiteral("/Mods-Active (DO NOT DELETE)");
    }
    if (roots.overlay.isEmpty()) {
        roots.overlay = gameParent + QStringLiteral("/") + installName +
                        QStringLiteral("-mods/dvdroot_ps4");
    }
    if (roots.backups.isEmpty()) {
        roots.backups = gameParent + QStringLiteral("/") + installName +
                        QStringLiteral("-modsBACKUP");
    }
    const bool sameConfiguration = gameRoot == m_gameRoot && backendDir == m_backendDir &&
                                   stateRoot == m_stateRoot && m_mods != nullptr &&
                                   roots.inactive == m_modRoots.inactive &&
                                   roots.active == m_modRoots.active &&
                                   roots.overlay == m_modRoots.overlay &&
                                   roots.backups == m_modRoots.backups;
    if (!m_gameRoot.isEmpty() && !sameConfiguration &&
        (m_playing || !m_armId.isEmpty() || !m_sessionId.isEmpty() ||
         !m_activePackageName.isEmpty())) {
        if (error != nullptr) {
            *error = tr("Stop the current Archipelago session before changing its configuration.");
        }
        return false;
    }
    // Managed roots mirror the Mod Manager layout exactly so both UIs
    // share one activation owner: inactive Mods, active Mods, overlay,
    // backups. Mode is always copy for AP.
    const QString inactive = roots.inactive;
    if (inactive.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("BBLauncher Mods directory is not configured");
        }
        return false;
    }
    const QString journal = stateRoot + QStringLiteral("/integrated/cxx-journal");
    if (!sameConfiguration) {
        if (m_backend != nullptr && m_backend->IsRunning()) {
            m_backend->Stop();
        }
        m_gameRoot = gameRoot;
        m_backendDir = backendDir;
        m_stateRoot = stateRoot;
        m_mods = std::make_unique<modservice::ModService>(
            roots.inactive.toStdWString(), roots.active.toStdWString(),
            roots.overlay.toStdWString(), roots.backups.toStdWString(),
            modservice::OverlayMode::Copy, journal.toStdWString());
        m_modRoots = roots;
        ResetSession();
    }
    // A failed recovery must be retried on the next Configure call. The
    // configuration may already be cached even though its journal remains
    // unresolved, so recovery cannot be limited to service construction.
    const modservice::Result recovered = m_mods->Recover();
    if (!recovered.ok) {
        m_recoveryFailed = true;
        m_recoveryError = QString::fromStdString(recovered.detail);
        if (error != nullptr) {
            *error = m_recoveryError;
        }
        if (m_emu != nullptr) {
            m_emu->setPreflightHandler(
                [this](const QString& action) { return Preflight(action); });
        }
        return false;
    }
    m_recoveryFailed = false;
    m_recoveryError.clear();
    if (!sameConfiguration) RestoreManagedPackage();
    if (m_emu != nullptr) {
        m_emu->setPreflightHandler(
            [this](const QString& action) { return Preflight(action); });
    }
    return !startBackend || EnsureBackend(error);
}

QString ApCoordinator::Preflight(const QString& action) {
    if (m_recoveryFailed) {
        return tr("Randomizer mod recovery needs attention before starting the game: %1")
            .arg(m_recoveryError);
    }
    if (m_ownStart) {
        if (action == QStringLiteral("start-game")) {
            m_ipcStartPending = false;
        }
        return {};
    }
    if (action == QStringLiteral("start-game") && m_ipcStartPending &&
        m_emu != nullptr &&
        m_emu->OwnsProcess(m_gameIdentity)) {
        // The emulator's IPC handshake starts the game after StartGame has
        // returned. It belongs to our tracked process, not a second launch.
        m_ipcStartPending = false;
        return {};
    }
    const bool starting = action == QStringLiteral("start") ||
                          action == QStringLiteral("restart") ||
                          action == QStringLiteral("start-game");
    if (m_playing) {
        if (starting) {
            return tr("An Archipelago session is already running. "
                      "Use Return to game instead of starting a second copy.");
        }
        return {};
    }
    if (!m_armId.isEmpty() && starting) {
        // Armed but not yet playing: only the coordinator's own start may
        // launch the game, so activation cannot drift under us.
        return tr("An Archipelago session is armed and starting. "
                  "Please wait for the game to launch.");
    }
    if (!m_activePackageName.isEmpty() && starting) {
        return tr("A randomizer package is active. Use Randomizer Launch or Regular play.");
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

bool ApCoordinator::SaveManagedPackage(const QString& package,
                                       Prepared::Mode mode, QString* error) {
    const QString path = m_stateRoot + QStringLiteral("/integrated/cxx-owned-active.json");
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = tr("Could not save the active package identity.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = tr("Could not save the active package identity: %1")
                                 .arg(file.errorString());
        return false;
    }
    const QJsonObject record{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("game_root"), AbsoluteClean(m_gameRoot)},
        {QStringLiteral("inactive_root"), AbsoluteClean(m_modRoots.inactive)},
        {QStringLiteral("active_root"), AbsoluteClean(m_modRoots.active)},
        {QStringLiteral("overlay_root"), AbsoluteClean(m_modRoots.overlay)},
        {QStringLiteral("package_name"), package},
        {QStringLiteral("mode"), mode == Prepared::Mode::Standalone
                                     ? QStringLiteral("standalone") : QStringLiteral("ap")},
    };
    file.write(QJsonDocument(record).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = tr("Could not save the active package identity: %1")
                                 .arg(file.errorString());
        return false;
    }
    return true;
}

void ApCoordinator::RestoreManagedPackage() {
    QFile file(m_stateRoot + QStringLiteral("/integrated/cxx-owned-active.json"));
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject record = QJsonDocument::fromJson(file.readAll()).object();
    if (record.value(QStringLiteral("schema")).toInt() != 1 ||
        record.value(QStringLiteral("game_root")).toString() != AbsoluteClean(m_gameRoot) ||
        record.value(QStringLiteral("inactive_root")).toString() != AbsoluteClean(m_modRoots.inactive) ||
        record.value(QStringLiteral("active_root")).toString() != AbsoluteClean(m_modRoots.active) ||
        record.value(QStringLiteral("overlay_root")).toString() != AbsoluteClean(m_modRoots.overlay)) return;
    const QString package = record.value(QStringLiteral("package_name")).toString();
    const QString mode = record.value(QStringLiteral("mode")).toString();
    if (!((mode == QStringLiteral("ap") &&
           package.startsWith(QStringLiteral("Archipelago-"))) ||
          (mode == QStringLiteral("standalone") &&
           package.startsWith(QStringLiteral("Bloodborne-Standalone-"))))) return;
    for (const std::string& active : m_mods->ActiveMods()) {
        if (QString::fromStdString(active) == package) {
            m_activePackageName = package;
            m_mode = mode == QStringLiteral("standalone")
                         ? Prepared::Mode::Standalone : Prepared::Mode::Archipelago;
            return;
        }
    }
}

void ApCoordinator::ClearManagedPackage() {
    QFile::remove(m_stateRoot + QStringLiteral("/integrated/cxx-owned-active.json"));
    m_activePackageName.clear();
}

void ApCoordinator::RequestCancel() {
    if (m_backend != nullptr) {
        m_backend->RequestCancel();
    }
}

bool ApCoordinator::InspectSeed(const QString& seedPath, const QString& playerName,
                                ApResponse* response, QString* error) {
    if (!EnsureBackend(error)) {
        return false;
    }
    QJsonObject params{{QStringLiteral("seed_path"), seedPath}};
    if (!playerName.isEmpty()) {
        params.insert(QStringLiteral("player_name"), playerName);
    }
    ApResponse seen = m_backend->Call(QStringLiteral("inspect_seed"), params, 30000);
    if (response != nullptr) {
        *response = seen;
    }
    if (!seen.ok) {
        if (error != nullptr) {
            *error = seen.error.detail;
        }
        return false;
    }
    return true;
}

bool ApCoordinator::Prepare(const ApPlayRequest& request, Prepared* prepared,
                            QString* error, bool reuseExisting) {
    emit StageChanged(tr("Preparing your seed"));
    m_lastErrorCode.clear();
    if (prepared != nullptr) *prepared = Prepared{};
    if (!EnsureBackend(error)) {
        return false;
    }
    // Inspect first so single-slot seeds skip the player question and
    // multi-slot seeds fail closed with a named choice.
    ApResponse seen;
    if (!InspectSeed(request.seedPath, request.playerName, &seen, error)) {
        return false;
    }
    if (seen.result.value(QStringLiteral("needs_choice")).toBool() &&
        request.playerName.isEmpty()) {
        if (error != nullptr) {
            *error = tr("Choose a player for this seed, then press Play.");
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
        {QStringLiteral("mods_root"), m_modRoots.inactive},
        {QStringLiteral("seed_path"), request.seedPath},
        {QStringLiteral("player_name"), player},
        {QStringLiteral("server"), server},
        {QStringLiteral("shad_executable"), QString::fromStdWString(Common::shadPs4Executable.wstring())},
        {QStringLiteral("ap_client"), client},
        {QStringLiteral("suppression_dir"), m_backendDir + QStringLiteral("/suppression")},
        {QStringLiteral("cache_root"), m_stateRoot + QStringLiteral("/cache")},
        {QStringLiteral("reuse_existing"), reuseExisting},
        {QStringLiteral("enemizer"),
         QJsonObject{{QStringLiteral("enabled"), request.enemizer.enabled},
                     {QStringLiteral("seed"), request.enemizer.seed.isEmpty()
                                                  ? QJsonValue(QJsonValue::Null)
                                                  : QJsonValue(request.enemizer.seed)},
                     {QStringLiteral("allow_tier_mixing"), request.enemizer.allowTierMixing},
                     {QStringLiteral("preserve_locomotion"), request.enemizer.preserveLocomotion},
                     {QStringLiteral("normalize_scaling"), request.enemizer.normalizeScaling},
                     {QStringLiteral("boss_canary"), request.enemizer.bossCanary},
                     {QStringLiteral("boss_pool"), request.enemizer.bossPool.isEmpty()
                                                       ? QJsonValue(QJsonValue::Null)
                                                       : QJsonValue(request.enemizer.bossPool)},
                     {QStringLiteral("release_contracts"), request.enemizer.releaseContracts},
                     {QStringLiteral("release_spawns"), request.enemizer.releaseSpawns},
                     {QStringLiteral("release_chara"), request.enemizer.releaseChara}}},
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
    if (!ReleaseIdleManagedPackage(error)) {
        return false;
    }
    ApResponse response = m_backend->Call(QStringLiteral("prepare_play"), params, 600000);
    if (!response.ok) {
        m_lastErrorCode = response.error.code;
        if (error != nullptr) {
            *error = response.error.detail;
        }
        return false;
    }
    m_playId = response.result.value(QStringLiteral("play_id")).toString();
    m_mode = Prepared::Mode::Archipelago;
    m_packageName = response.result.value(QStringLiteral("package_name")).toString();
    const QJsonObject display = response.result.value(QStringLiteral("display")).toObject();
    if (prepared != nullptr) {
        prepared->playId = m_playId;
        prepared->packageName =
            response.result.value(QStringLiteral("package_name")).toString();
        prepared->seed = display.value(QStringLiteral("seed")).toString();
        prepared->slot = display.value(QStringLiteral("slot")).toString();
        prepared->server = display.value(QStringLiteral("server")).toString();
        prepared->title = display.value(QStringLiteral("title")).toString();
        prepared->reused = response.result.value(QStringLiteral("reused")).toBool();
        const QJsonObject enemy = response.result.value(QStringLiteral("enemizer")).toObject();
        if (enemy.value(QStringLiteral("enabled")).toBool()) {
            QStringList details;
            const QJsonValue swaps = enemy.value(QStringLiteral("swap_count"));
            const QJsonValue mapFiles = enemy.value(QStringLiteral("map_file_count"));
            const QJsonValue aiFiles = enemy.value(QStringLiteral("ai_file_count"));
            if (swaps.isDouble()) {
                details.push_back(tr("%1 enemy swaps").arg(swaps.toInt()));
            }
            if (mapFiles.isDouble()) {
                details.push_back(tr("%1 map files").arg(mapFiles.toInt()));
            }
            if (aiFiles.isDouble()) {
                details.push_back(tr("%1 AI files").arg(aiFiles.toInt()));
            }
            if (!details.isEmpty()) {
                prepared->enemySummary = tr("Enemy randomization: %1.").arg(details.join(tr(", ")));
            }
        }
    }
    m_title = display.value(QStringLiteral("title")).toString();
    m_playing = false;
    return true;
}

bool ApCoordinator::PrepareStandalone(const StandalonePlayRequest& request,
                                      Prepared* prepared, QString* error) {
    emit StageChanged(tr("Randomizing your game"));
    if (request.seed.trimmed().isEmpty()) {
        if (error != nullptr) *error = tr("Enter a seed first.");
        return false;
    }
    if (request.expandedCoverage && !request.randomizeEnemies) {
        if (error != nullptr) *error = tr("Expanded coverage requires enemy randomization.");
        return false;
    }
    if (!EnsureBackend(error)) return false;
    const QJsonObject params{
        {QStringLiteral("seed"), request.seed.trimmed()},
        {QStringLiteral("include_dlc"), request.includeDlc},
        {QStringLiteral("randomize_enemies"), request.randomizeEnemies},
        {QStringLiteral("expanded_coverage"), request.expandedCoverage},
        {QStringLiteral("normalize_scaling"), request.normalizeScaling},
        {QStringLiteral("game_root"), request.gameRoot},
        {QStringLiteral("mods_root"), m_modRoots.inactive},
        {QStringLiteral("state_root"), m_stateRoot},
    };
    if (!ReleaseIdleManagedPackage(error)) return false;
    const ApResponse response = m_backend->Call(QStringLiteral("prepare_standalone"),
                                                params, 600000);
    if (!response.ok) {
        if (error != nullptr) *error = response.error.detail;
        return false;
    }
    Prepared result;
    result.mode = Prepared::Mode::Standalone;
    result.playId = response.result.value(QStringLiteral("receipt_id")).toString();
    result.packageName = response.result.value(QStringLiteral("package_name")).toString();
    result.packagePath = response.result.value(QStringLiteral("package_path")).toString();
    result.receiptPath = response.result.value(QStringLiteral("receipt_path")).toString();
    result.includeDlc = request.includeDlc;
    result.randomizeEnemies = request.randomizeEnemies;
    result.expandedCoverage = request.expandedCoverage;
    result.normalizeScaling = request.normalizeScaling;
    result.seed = response.result.value(QStringLiteral("seed")).toString();
    result.title = response.result.value(QStringLiteral("display_name")).toString();
    if (result.playId.isEmpty() || result.packageName.isEmpty() ||
        result.packagePath.isEmpty() || result.receiptPath.isEmpty()) {
        if (error != nullptr) *error = tr("The standalone backend returned an incomplete receipt.");
        return false;
    }
    m_mode = Prepared::Mode::Standalone;
    m_playId = result.playId;
    m_packageName = result.packageName;
    m_title = result.title;
    m_playing = false;
    if (prepared != nullptr) *prepared = result;
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
    if (m_emu != nullptr && m_emu->IsEmulatorRunning()) {
        if (error != nullptr) {
            *error = tr("Close the emulator before switching to an Archipelago seed. The active package was left unchanged.");
        }
        return false;
    }
    if (prepared.mode == Prepared::Mode::Standalone) {
        const QString expected = QDir(m_modRoots.inactive).absoluteFilePath(prepared.packageName);
        const QString actual = QFileInfo(prepared.packagePath).absoluteFilePath();
        if (prepared.playId != m_playId || prepared.packageName != m_packageName ||
            QDir::cleanPath(expected) != QDir::cleanPath(actual)) {
            if (error != nullptr) *error = tr("The prepared package does not match this session. Randomize again.");
            return false;
        }
        emit StageChanged(tr("Verifying the randomized package"));
        const ApResponse verified = m_backend->Call(
            QStringLiteral("verify_standalone"),
            QJsonObject{{QStringLiteral("package_path"), prepared.packagePath},
                        {QStringLiteral("receipt_path"), prepared.receiptPath},
                        {QStringLiteral("receipt_id"), prepared.playId},
                        {QStringLiteral("package_name"), prepared.packageName},
                        {QStringLiteral("seed"), prepared.seed},
                        {QStringLiteral("game_root"), m_gameRoot},
                        {QStringLiteral("mods_root"), m_modRoots.inactive},
                        {QStringLiteral("include_dlc"), prepared.includeDlc},
                        {QStringLiteral("randomize_enemies"), prepared.randomizeEnemies},
                        {QStringLiteral("expanded_coverage"), prepared.expandedCoverage},
                        {QStringLiteral("normalize_scaling"), prepared.normalizeScaling}}, 120000);
        if (!verified.ok) {
            if (error != nullptr) *error = verified.error.detail;
            return false;
        }
        if (verified.result.value(QStringLiteral("receipt_id")).toString() != prepared.playId ||
            verified.result.value(QStringLiteral("package_name")).toString() != prepared.packageName) {
            if (error != nullptr) *error = tr("The standalone receipt changed. Randomize again.");
            return false;
        }
    }
    // A previous run can still own the overlay after another inactive seed
    // has been prepared. If an older AP installation owns the overlay,
    // deactivate this coordinator's exact package even when it has the same
    // name as the prepared one, so its backed-up bytes are restored first.
    const QString legacyOwnerMarker = QFileInfo(m_modRoots.overlay).absolutePath() +
                                      QStringLiteral("/.bb-ap-owner.json");
    const bool legacyOwnerPresent = QFileInfo::exists(legacyOwnerMarker);
    if (!m_activePackageName.isEmpty() &&
        (m_activePackageName != prepared.packageName || legacyOwnerPresent)) {
        if (!m_sessionId.isEmpty() && !GameClosed(error)) return false;
        modservice::Plan down;
        const modservice::Result will = m_mods->PlanDeactivate(
            m_activePackageName.toStdString(), down);
        if (!will.ok) {
            if (error) *error = QString::fromStdString(will.detail);
            return false;
        }
        const modservice::Result did = m_mods->Commit(down);
        if (!did.ok) {
            if (error) *error = QString::fromStdString(did.detail);
            return false;
        }
        ClearManagedPackage();
    }
    // The backend verifies and retires an older AP-owned overlay before
    // ModService plans a new activation. Qt never edits its ownership marker.
    if (!m_sessionId.isEmpty() && !GameClosed(error)) return false;
    emit StageChanged(tr("Checking previous game setup"));
    const ApResponse migrated = m_backend->Call(
        QStringLiteral("migrate_legacy_overlay"),
        QJsonObject{{QStringLiteral("game_root"), m_gameRoot}}, 120000);
    if (!migrated.ok) {
        m_lastErrorCode = migrated.error.code;
        if (error != nullptr) *error = migrated.error.detail;
        return false;
    }
    const QString migrationStatus = migrated.result.value(QStringLiteral("status")).toString();
    if (migrationStatus != QStringLiteral("migrated") &&
        migrationStatus != QStringLiteral("already_migrated") &&
        migrationStatus != QStringLiteral("no_legacy")) {
        if (error != nullptr) *error = tr("The previous game setup could not be verified. No new mod was activated.");
        return false;
    }
    const bool retiredLegacy = migrationStatus == QStringLiteral("migrated");
    if (retiredLegacy) {
        emit StageChanged(tr("Previous Archipelago mod removed; preparing the new mod."));
    }
    const auto activationError = [this, error, retiredLegacy](const QString& detail) {
        if (error == nullptr) return;
        *error = retiredLegacy
            ? tr("The previous Archipelago mod was removed and its backup was kept. "
                 "The new mod was not activated: %1").arg(detail)
            : detail;
    };
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
        activationError(tr("The prepared randomizer package is not in the inactive mods. "
                           "Prepare again."));
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
            QString names;
            for (const std::string& entry : plan.conflictingMods) {
                const QString text = QString::fromStdString(entry);
                names += text.section(QChar(','), 1, 1).trimmed() + QStringLiteral(" ");
            }
            activationError(tr("This mod conflicts with the randomizer: %1").arg(names.trimmed()));
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
                activationError(QString::fromStdString(will.detail));
                return false;
            }
            modservice::Result did = m_mods->Commit(down);
            if (!did.ok) {
                activationError(QString::fromStdString(did.detail));
                return false;
            }
        }
        // Deactivation restores the bytes underneath each conflicting mod.
        // The original activation plan fingerprinted the conflicting bytes,
        // so discard it and plan against the now-restored overlay.
        planned = m_mods->PlanActivate(package.toStdString(), plan);
        if (!planned.ok) {
            activationError(QString::fromStdString(planned.detail));
            return false;
        }
        plan.conflictOverride = true;
    } else if (!planned.ok) {
        activationError(QString::fromStdString(planned.detail));
        return false;
    }
    // Record the exact owner before the overlay commit. On an interrupted
    // commit, Configure checks ModService's active ledger before restoring it.
    if (!SaveManagedPackage(package, prepared.mode, error)) {
        if (error != nullptr) activationError(*error);
        return false;
    }
    modservice::Result done = m_mods->Commit(plan);
    if (!done.ok) {
        ClearManagedPackage();
        activationError(QString::fromStdString(done.detail));
        return false;
    }
    m_activePackageName = package;
    return true;
}

bool ApCoordinator::Arm(const Prepared& prepared, QString* error) {
    emit StageChanged(tr("Verifying your game setup"));
    const QString modsRoot = m_modRoots.inactive;
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
    m_ipcStartPending = true;
    m_ownStart = true;
    EmulatorProcessIdentity identity;
    const bool started = m_emu->Start(fileInfo, args, workDir, &identity, error);
    m_ownStart = false;
    if (!started) {
        m_ipcStartPending = false;
        return false;
    }
    if (!identity.valid) {
        m_ipcStartPending = false;
        if (error != nullptr) {
            *error = tr("Could not establish the emulator process identity");
        }
        return false;
    }
    m_gameIdentity = identity;
    if (m_mode == Prepared::Mode::Standalone) {
        m_playing = true;
        emit StageChanged(tr("Ready to play"));
    }
    return true;
}

bool ApCoordinator::Connect(QString* error) {
    emit StageChanged(tr("Connecting to Archipelago"));
    const EmulatorProcessIdentity& identity = m_gameIdentity;
    QJsonObject params{{QStringLiteral("arm_id"), m_armId},
                       {QStringLiteral("game_root"), m_gameRoot},
                       {QStringLiteral("mods_root"), m_modRoots.inactive}};
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

bool ApCoordinator::ReturnToGame(QString* error) {
    if (!m_gameIdentity.valid || m_emu == nullptr) {
        if (error != nullptr) {
            *error = tr("The Archipelago game window is not available yet.");
        }
        return false;
    }
    return m_emu->Focus(m_gameIdentity, error);
}

bool ApCoordinator::GameClosed(QString* error) {
    if (!m_sessionId.isEmpty()) {
        if (m_backend == nullptr || !m_backend->IsRunning()) {
            if (error != nullptr) {
                *error = tr("The Archipelago backend is unavailable; its client could not be stopped.");
            }
            return false;
        }
        ApResponse stopped = m_backend->Call(
            QStringLiteral("stop_client"),
            QJsonObject{{QStringLiteral("session_id"), m_sessionId}}, 30000);
        if (!stopped.ok || !stopped.result.value(QStringLiteral("stopped")).toBool()) {
            if (error != nullptr) {
                *error = stopped.ok
                             ? tr("The Archipelago client could not be verified as stopped; the active package was left in place.")
                             : stopped.error.detail;
            }
            return false;
        }
    }
    m_sessionId.clear();
    m_armId.clear();
    m_gameIdentity = EmulatorProcessIdentity{};
    m_playing = false;
    return true;
}

bool ApCoordinator::RefreshStatus(QString* stateOut, QString* error) {
    if (m_mode == Prepared::Mode::Standalone) {
        if (m_playId.isEmpty() || m_emu == nullptr) return false;
        m_playing = m_emu->IsEmulatorRunning();
        if (stateOut != nullptr) {
            *stateOut = m_playing ? QStringLiteral("playing") : QStringLiteral("recoverable");
        }
        return true;
    }
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
    return StopSessionAndDeactivate(error);
}

bool ApCoordinator::SwitchToSeed(QString* error) {
    return StopSessionAndDeactivate(error);
}

bool ApCoordinator::ReleaseIdleManagedPackage(QString* error) {
    if (m_activePackageName.isEmpty()) return true;
    if (m_emu == nullptr || m_emu->IsEmulatorRunning()) {
        if (error != nullptr) {
            *error = tr("Close the emulator before preparing another randomizer package. "
                        "The active package was left unchanged.");
        }
        return false;
    }
    return StopSessionAndDeactivate(error);
}

bool ApCoordinator::StopSessionAndDeactivate(QString* error) {
    // Stop the AP client and the exact game instance started by this
    // coordinator before allowing ModService to change the live overlay.
    if (!m_sessionId.isEmpty()) {
        if (!m_backend || !m_backend->IsRunning()) {
            if (error != nullptr) {
                *error = tr("The Archipelago backend is unavailable; the game setup was left unchanged.");
            }
            return false;
        }
        ApResponse stopped = m_backend->Call(
            QStringLiteral("stop_client"),
            QJsonObject{{QStringLiteral("session_id"), m_sessionId}}, 30000);
        if (!stopped.ok || !stopped.result.value(QStringLiteral("stopped")).toBool()) {
            if (error != nullptr) {
                *error = stopped.ok
                             ? tr("The Archipelago client could not be verified as stopped.")
                             : stopped.error.detail;
            }
            return false;
        }
    }
    if (!m_gameIdentity.valid && m_emu != nullptr && m_emu->IsEmulatorRunning() &&
        !m_activePackageName.isEmpty()) {
        if (error) *error = tr("Close the emulator before removing the active randomizer package.");
        return false;
    }
    if (m_gameIdentity.valid) {
        if (m_emu == nullptr || !m_gameIdentity.valid) {
            if (error != nullptr) {
                *error = tr("The game instance cannot be verified, so its active overlay was left untouched.");
            }
            return false;
        }
        if (!m_emu->Stop(m_gameIdentity, error)) {
            return false;
        }
    }
    if (m_mods) {
        const auto activeMods = m_mods->ActiveMods();
        for (auto it = activeMods.rbegin(); it != activeMods.rend(); ++it) {
            const QString mod = QString::fromStdString(*it);
            if (mod == m_activePackageName) {
                modservice::Plan down;
                modservice::Result will = m_mods->PlanDeactivate(*it, down);
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
    ClearManagedPackage();
    ResetSession();
    return true;
}

void ApCoordinator::ResetSession() {
    m_ipcStartPending = false;
    m_playId.clear();
    m_armId.clear();
    m_sessionId.clear();
    m_title.clear();
    m_packageName.clear();
    m_activePackageName.clear();
    m_mode = Prepared::Mode::Archipelago;
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
    if (!Prepare(request, &prepared, error, true)) {
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
