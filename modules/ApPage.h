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
#include <QToolButton>

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
    void closeEvent(QCloseEvent* event) override;

    ApCoordinator* m_coordinator;
    QLabel* m_setupLabel = nullptr;
    QLineEdit* m_seedEdit = nullptr;
    QLabel* m_playerLabel = nullptr;
    QComboBox* m_playerCombo = nullptr;
    QLabel* m_serverLabel = nullptr;
    QLineEdit* m_serverEdit = nullptr;
    QLabel* m_passwordLabel = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QGroupBox* m_enemizerGroup = nullptr;
    QCheckBox* m_randomizeEnemies = nullptr;
    QLineEdit* m_enemySeedEdit = nullptr;
    QGroupBox* m_expandedCoverage = nullptr;
    QCheckBox* m_releaseContracts = nullptr;
    QCheckBox* m_releaseSpawns = nullptr;
    QCheckBox* m_releaseChara = nullptr;
    QToolButton* m_advancedEnemyOptions = nullptr;
    QWidget* m_advancedEnemyPanel = nullptr;
    QCheckBox* m_allowTierMixing = nullptr;
    QCheckBox* m_preserveLocomotion = nullptr;
    QCheckBox* m_normalizeScaling = nullptr;
    QPushButton* m_playButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_switchSeedButton = nullptr;
    QPushButton* m_regularButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QTimer* m_pollTimer = nullptr;
    bool m_cancelled = false;
    bool m_inspecting = false;
    bool m_busy = false;
    QList<QWidget*> m_operationControls;
};
