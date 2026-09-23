// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>

#include "modules/EmulatorService.h"
#include "modules/ApCoordinator.h"
#include "modules/QAnsiTextEdit.h"
#include "modules/ipc/ipc_client.h"
#include "settings/emulator_settings.h"
#include "settings/user_settings.h"

namespace Ui {
class BBLauncher;
}

class BBLauncher : public QMainWindow {
    Q_OBJECT

public:
    BBLauncher(bool noGUI, bool noInstanceRunning, QWidget* parent = nullptr);
    ~BBLauncher();

    bool canLaunch = true;
    // Single funnel for every emulator startup route (regular play,
    // Archipelago play, Restart, IPC, no-GUI). The AP coordinator
    // installs a preflight handler while a session is armed/active.
    EmulatorService* emulatorService() const { return m_emu_service.get(); }
    const EmulatorProcessIdentity& lastEmulatorIdentity() const { return m_lastIdentity; }
    ApCoordinator* apCoordinator() const { return m_ap_coordinator.get(); }
    // Headless Archipelago Play for no-GUI/shortcut startup. Runs the
    // same preflight-gated flow as the AP page, then returns 0 when the
    // session is playing (the supervisor owns game + client afterwards).
    int RunApHeadless(const QString& seedPath, const QString& player,
                      const QString& server);

 public slots:

private slots:
    void ShadSelectButton_isPressed();
    void onGameClosed();
    void PrintLog(QString entry);
    void OpenFolders();

private:
    QVBoxLayout* createIconTextButtonLayout(const QString& resourcePath, const QString& buttonText,
                                            QPushButton* button);
    static void StartBackupSave();
    bool CheckBBInstall();
    void UpdatePatchesList();
    void UpdateModList();
    void LogSettings();
    void GetShadExecutable();
    QIcon RecolorIcon(const QIcon& icon, bool isWhite);
    void UpdateIcons();
    void RunGame();
    void RestartEmulator();
    void StartGameWithArgs(QStringList args);
    void StartEmulator(std::filesystem::path path, QStringList args);
    std::vector<MemoryPatcher::PendingPatch> readPatches(std::string gameSerial,
                                                         std::string appVersion);

    Ui::BBLauncher* ui;
    QAnsiTextEdit* logDisplay;
    std::shared_ptr<IpcClient> m_ipc_client = std::make_shared<IpcClient>();
    std::unique_ptr<EmulatorService> m_emu_service;
    std::unique_ptr<ApCoordinator> m_ap_coordinator;
    EmulatorProcessIdentity m_lastIdentity;
    void OpenApPage();
    void OpenModManager();
    void OpenShadSettings();
    std::shared_ptr<EmulatorSettingsImpl> m_emu_settings = std::make_shared<EmulatorSettingsImpl>();
    std::shared_ptr<UserSettingsImpl> m_user_settings = std::make_shared<UserSettingsImpl>();

    std::filesystem::path shadPs4Directory;
    bool noGUIset;
    bool noinstancerunning;
    bool is_paused;

    QPushButton* modManagerButton = new QPushButton(this);
    QPushButton* archipelagoButton = new QPushButton(this);
    QPushButton* modDownloaderButton = new QPushButton(this);
    QPushButton* patchesButton = new QPushButton(this);
    QPushButton* shadSettingsButton = new QPushButton(this);
    QPushButton* launcherSettingsButton = new QPushButton(this);
    QPushButton* saveViewerButton = new QPushButton(this);

    QPushButton* launchButton = new QPushButton(this);
    QPushButton* stopButton = new QPushButton(this);
    QPushButton* restartButton = new QPushButton(this);
    QPushButton* fullscreenButton = new QPushButton(this);

    const std::vector<std::string> BBSerialList = {"CUSA03173", "CUSA00900", "CUSA00208",
                                                   "CUSA00207", "CUSA01363", "CUSA03023",
                                                   "CUSA00299", "CUSA03014"};
};
