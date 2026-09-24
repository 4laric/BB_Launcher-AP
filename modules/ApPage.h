// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ApPage: the Archipelago play mode. One application, choose seed, Play.
// Activation, verification, process attachment and connection happen
// behind the Play action via ApCoordinator; receipts, manifests, process
// plans and suppression paths stay internal. Logs and provenance remain
// available under Help / Diagnostics.
//
// Player-facing rules honored here: keyboard navigation with visible
// focus and a default action; text alongside color; usable at
// 820x620; long paths elided with a full-text tooltip. Waiting states
// stay neutral and never ask for consumable use; red states name the
// problem and offer a recovery action. No modal dialogs for normal
// progress or success.

#pragma once

#include <QComboBox>
#include <QDialog>
#include <QCloseEvent>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>

#include "ApCoordinator.h"

class ApPage : public QDialog {
    Q_OBJECT

  public:
    explicit ApPage(ApCoordinator* coordinator, QWidget* parent = nullptr);

  signals:
    void requestMods();
    void requestEmulatorSettings();

  private slots:
    void BrowseSeed();
    void SeedChanged();
    void RandomizeClicked();
    void PlayClicked();
    void CancelClicked();
    void SwitchSeedClicked();
    void RegularPlayClicked();
    void HelpClicked();
    void PollStatus();

  private:
    void SetBusy(bool busy, const QString& stage = {});
    void SetStatus(const QString& text, bool isError);
    void ShowError(const QString& detail);
    bool EnsureConfigured(QString* error);
    void RefreshForSession();
    void RefreshEnemizerControls();
    void RefreshMode();
    void InvalidatePrepared();
    void LoadSettings();
    void SaveSettings() const;
    bool BuildRequest(ApPlayRequest* request, QString* error) const;
    bool PrepareCurrent(bool* recoveredCollision);
    void closeEvent(QCloseEvent* event) override;

    ApCoordinator* m_coordinator;
    QLabel* m_setupLabel = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QLabel* m_seedLabel = nullptr;
    QWidget* m_apSeedRow = nullptr;
    QLineEdit* m_seedEdit = nullptr;
    QLineEdit* m_standaloneSeedEdit = nullptr;
    QLabel* m_standaloneSeedLabel = nullptr;
    QCheckBox* m_includeDlc = nullptr;
    QLabel* m_playerLabel = nullptr;
    QComboBox* m_playerCombo = nullptr;
    QLabel* m_serverLabel = nullptr;
    QLineEdit* m_serverEdit = nullptr;
    QLabel* m_passwordLabel = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QGroupBox* m_enemizerGroup = nullptr;
    QComboBox* m_enemyMode = nullptr;
    QLabel* m_standaloneBossNote = nullptr;
    QLineEdit* m_enemySeedEdit = nullptr;
    QWidget* m_enemySeedRow = nullptr;
    QCheckBox* m_normalizeScaling = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_randomizeButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_switchSeedButton = nullptr;
    QPushButton* m_regularButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QTimer* m_pollTimer = nullptr;
    bool m_cancelled = false;
    bool m_inspecting = false;
    bool m_busy = false;
    bool m_hasPrepared = false;
    bool m_settingsLoaded = false;
    int m_lastModeIndex = 0;
    int m_apEnemyMode = 0;
    int m_standaloneEnemyMode = 0;
    bool m_apScaling = true;
    bool m_standaloneScaling = true;
    QString m_savedPlayerName;
    QString m_savedServer;
    QString m_savedApSeedPath;
    ApCoordinator::Prepared m_prepared;
    QList<QWidget*> m_operationControls;
};
