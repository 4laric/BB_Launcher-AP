// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ApCoordinator: Archipelago session sequencing on top of ModService,
// EmulatorService and ApBackend. AP-specific; not for upstream.
//
// Flow (one application, choose seed, Play):
//   inspect -> prepare -> activate (ModService, copy) ->
//   verify/arm (backend) -> start (EmulatorService) ->
//   connect client (backend) -> playing
//
// Ownership recap: ModService alone writes the overlay; the backend
// alone owns preparation, receipts, boot proof, runtime config, ledgers
// and client supervision; the native client stays a separate process.
// Final verification and client spawning stay together in the backend.
// The coordinator never parses human-readable logs as an API.

#pragma once

#include <QObject>
#include <QString>

#include <memory>

#include "ApBackend.h"
#include "EmulatorService.h"
#include "ModService.h"

struct ApPlayRequest {
    QString gameRoot;
    QString seedPath;
    QString playerName;
    QString server;
    QString password; // request bodies only; never logged, never in argv
    struct Enemizer {
        bool enabled = false;
        QString seed;
        bool allowTierMixing = true;
        bool preserveLocomotion = false;
        bool normalizeScaling = false;
        bool bossCanary = false;
        QString bossPool;
        bool releaseContracts = false;
        bool releaseSpawns = false;
        bool releaseChara = false;
    } enemizer;
};

struct StandalonePlayRequest {
    QString gameRoot;
    QString seed;
    bool includeDlc = true;
    bool randomizeEnemies = true;
    bool expandedCoverage = false;
    bool normalizeScaling = true;
};

// Managed by the AP coordinator in copy mode. The default paths match the
// regular Mod Manager; explicit roots also let embedders and tests isolate
// an AP session without touching a user's install.
struct ApModRoots {
    QString inactive;
    QString active;
    QString overlay;
    QString backups;
};

class ApCoordinator : public QObject {
    Q_OBJECT

  public:
    explicit ApCoordinator(EmulatorService* emulator, QObject* parent = nullptr);

    // Roots for the AP-owned ModService (copy mode) and backend state.
    // gameRoot: install dir containing CUSA03173; backendDir: directory
    // holding the backend bundle (suppression data, client); stateRoot:
    // launcher state root for plays/arms/journal/supervisor.
    bool Configure(const QString& gameRoot, const QString& backendDir,
                   const QString& stateRoot, QString* error,
                   const ApModRoots& roots = {}, bool startBackend = true);

    bool InspectSeed(const QString& seedPath, const QString& playerName,
                     ApResponse* response, QString* error);
    void RequestCancel();

    // Installs itself as the emulator preflight handler while a session
    // is armed or playing. Every startup route (Play, Restart, IPC,
    // no-GUI/shortcuts) funnels through it.
    QString Preflight(const QString& action);

    ApBackend* backend() const { return m_backend.get(); }
    modservice::ModService* modService() const { return m_mods.get(); }

    bool HasSession() const { return !m_playId.isEmpty(); }
    bool GameStarted() const { return m_gameIdentity.valid; }
    bool IsPlaying() const { return m_playing; }
    QString playId() const { return m_playId; }
    QString armId() const { return m_armId; }
    QString sessionId() const { return m_sessionId; }
    QString displayTitle() const { return m_title; }

    // Headless Play for no-GUI/shortcut startup. Blocks (pumping events)
    // until the session is playing or fails; returns 0 on success.
    int RunHeadless(const ApPlayRequest& request, QString* error);

    // Foreground step used by the AP page and headless flow alike.
    struct Prepared {
        enum class Mode { Archipelago, Standalone } mode = Mode::Archipelago;
        QString playId;
        QString packageName;
        QString packagePath;
        QString receiptPath;
        bool includeDlc = true;
        bool randomizeEnemies = true;
        bool expandedCoverage = false;
        bool normalizeScaling = true;
        QString seed;
        QString slot;
        QString server;
        QString title;
        bool reused = false;
        QString enemySummary;
    };
    bool Prepare(const ApPlayRequest& request, Prepared* prepared, QString* error,
                 bool reuseExisting = false);
    bool PrepareStandalone(const StandalonePlayRequest& request, Prepared* prepared,
                           QString* error);
    // Activates the prepared package through ModService (copy). When a
    // named third-party mod conflicts and allowDisableConflicts is set,
    // conflicting mods are deactivated first via their exact reversible
    // plan; otherwise the conflict is reported with the mod name and
    // wasConflict is set so the UI can offer the single safe recovery.
    bool Activate(const Prepared& prepared, bool allowDisableConflicts, bool* wasConflict,
                  QString* error);
    bool Arm(const Prepared& prepared, QString* error);
    bool StartGame(QString* error);
    bool Connect(QString* error);
    bool ReturnToGame(QString* error);
    bool GameClosed(QString* error);
    QString lastErrorCode() const { return m_lastErrorCode; }
    bool RefreshStatus(QString* stateOut, QString* error);
    bool SwitchToRegularPlay(QString* error);
    bool SwitchToSeed(QString* error);
    void ResetSession();

  signals:
    void StageChanged(const QString& stage);

  private:
    bool EnsureBackend(QString* error);
    bool ReleaseIdleManagedPackage(QString* error);
    bool StopSessionAndDeactivate(QString* error);
    bool SaveManagedPackage(const QString& package, Prepared::Mode mode,
                            QString* error);
    void RestoreManagedPackage();
    void ClearManagedPackage();
    EmulatorService* m_emu = nullptr;
    std::unique_ptr<ApBackend> m_backend;
    std::unique_ptr<modservice::ModService> m_mods;
    QString m_gameRoot;
    QString m_backendDir;
    QString m_stateRoot;
    QString m_playId;
    QString m_armId;
    QString m_sessionId;
    QString m_title;
    QString m_packageName;
    QString m_activePackageName;
    Prepared::Mode m_mode = Prepared::Mode::Archipelago;
    ApModRoots m_modRoots;
    EmulatorProcessIdentity m_gameIdentity;
    QString m_lastErrorCode;
    bool m_playing = false;
    bool m_ownStart = false;
    bool m_ipcStartPending = false;
    bool m_recoveryFailed = false;
    QString m_recoveryError;
};
