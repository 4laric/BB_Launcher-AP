// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ApPage.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QVBoxLayout>

#include "ApFork.h"
#include "modules/Common.h"

ApPage::ApPage(ApCoordinator* coordinator, QWidget* parent)
    : QDialog(parent), m_coordinator(coordinator) {
    setWindowTitle(tr("Archipelago"));
    setMinimumSize(620, 460);

    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("<b>Bloodborne &mdash; Archipelago</b>"), this);
    layout->addWidget(title);

    m_setupLabel = new QLabel(tr("Game and emulator setup: checking&hellip;"), this);
    m_setupLabel->setWordWrap(true);
    layout->addWidget(m_setupLabel);

    auto* form = new QFormLayout();
    m_seedEdit = new QLineEdit(this);
    m_seedEdit->setObjectName(QStringLiteral("apSeedPath"));
    m_seedEdit->setPlaceholderText(tr("AP seed file (.zip or .bbseed.json)"));
    m_seedEdit->setClearButtonEnabled(true);
    auto* browse = new QPushButton(tr("Browse&hellip;"), this);
    connect(browse, &QPushButton::clicked, this, &ApPage::BrowseSeed);
    auto* seedRow = new QHBoxLayout();
    seedRow->addWidget(m_seedEdit, 1);
    seedRow->addWidget(browse);
    form->addRow(tr("&Seed:"), seedRow);
    connect(m_seedEdit, &QLineEdit::editingFinished, this, &ApPage::SeedChanged);
    m_operationControls << m_seedEdit << browse;

    m_playerLabel = new QLabel(tr("&Player:"), this);
    m_playerCombo = new QComboBox(this);
    m_playerCombo->setObjectName(QStringLiteral("apPlayerChoice"));
    m_playerCombo->setEditable(false);
    m_operationControls << m_playerCombo;
    m_playerLabel->setBuddy(m_playerCombo);
    form->addRow(m_playerLabel, m_playerCombo);

    m_serverLabel = new QLabel(tr("S&erver:"), this);
    m_serverEdit = new QLineEdit(this);
    m_operationControls << m_serverEdit;
    m_serverEdit->setPlaceholderText(tr("archipelago.gg:port"));
    m_serverLabel->setBuddy(m_serverEdit);
    form->addRow(m_serverLabel, m_serverEdit);

    m_passwordLabel = new QLabel(tr("P&assword:"), this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setPlaceholderText(tr("Optional; only needed for protected rooms"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_operationControls << m_passwordEdit;
    m_passwordLabel->setBuddy(m_passwordEdit);
    form->addRow(m_passwordLabel, m_passwordEdit);
    layout->addLayout(form);

    m_enemizerGroup = new QGroupBox(tr("Enemies"), this);
    m_enemizerGroup->setObjectName(QStringLiteral("apEnemyOptions"));
    auto* enemyLayout = new QVBoxLayout(m_enemizerGroup);
    m_randomizeEnemies = new QCheckBox(tr("Randomize enemies"), m_enemizerGroup);
    m_randomizeEnemies->setObjectName(QStringLiteral("apRandomizeEnemies"));
    m_randomizeEnemies->setChecked(true);
    enemyLayout->addWidget(m_randomizeEnemies);

    m_expandedCoverage = new QGroupBox(tr("Expanded coverage (experimental)"), m_enemizerGroup);
    m_expandedCoverage->setObjectName(QStringLiteral("apExpandedEnemyCoverage"));
    m_expandedCoverage->setCheckable(true);
    m_expandedCoverage->setChecked(true);
    auto* coverageLayout = new QVBoxLayout(m_expandedCoverage);
    auto* coverageNote = new QLabel(
        tr("Includes additional scripted enemies. Gameplay has not been tested for every encounter."),
        m_expandedCoverage);
    coverageNote->setWordWrap(true);
    coverageNote->setToolTip(coverageNote->text());
    coverageLayout->addWidget(coverageNote);
    m_releaseContracts = new QCheckBox(tr("Scripted enemies with supported behavior"),
                                        m_expandedCoverage);
    m_releaseContracts->setObjectName(QStringLiteral("apEnemyScriptedBehavior"));
    m_releaseContracts->setChecked(true);
    m_releaseSpawns = new QCheckBox(tr("Enemies created by ambushes"), m_expandedCoverage);
    m_releaseSpawns->setObjectName(QStringLiteral("apEnemyAmbushes"));
    m_releaseSpawns->setChecked(true);
    m_releaseChara = new QCheckBox(tr("Hunter-type enemies with scripted equipment"),
                                    m_expandedCoverage);
    m_releaseChara->setObjectName(QStringLiteral("apEnemyHunters"));
    m_releaseChara->setChecked(true);
    coverageLayout->addWidget(m_releaseContracts);
    coverageLayout->addWidget(m_releaseSpawns);
    coverageLayout->addWidget(m_releaseChara);
    enemyLayout->addWidget(m_expandedCoverage);

    m_advancedEnemyOptions = new QToolButton(m_enemizerGroup);
    m_advancedEnemyOptions->setObjectName(QStringLiteral("apAdvancedEnemyOptions"));
    m_advancedEnemyOptions->setText(tr("Advanced enemy options"));
    m_advancedEnemyOptions->setCheckable(true);
    m_advancedEnemyOptions->setChecked(false);
    m_advancedEnemyOptions->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedEnemyOptions->setArrowType(Qt::RightArrow);
    enemyLayout->addWidget(m_advancedEnemyOptions, 0, Qt::AlignLeft);

    m_advancedEnemyPanel = new QWidget(m_enemizerGroup);
    m_advancedEnemyPanel->setObjectName(QStringLiteral("apAdvancedEnemyPanel"));
    auto* advancedLayout = new QVBoxLayout(m_advancedEnemyPanel);
    advancedLayout->setContentsMargins(12, 0, 0, 0);
    auto* enemySeedRow = new QHBoxLayout();
    enemySeedRow->addWidget(new QLabel(tr("Enemy seed:"), m_advancedEnemyPanel));
    m_enemySeedEdit = new QLineEdit(m_advancedEnemyPanel);
    m_enemySeedEdit->setObjectName(QStringLiteral("apEnemySeed"));
    m_enemySeedEdit->setPlaceholderText(tr("Optional; blank uses the AP seed"));
    enemySeedRow->addWidget(m_enemySeedEdit, 1);
    advancedLayout->addLayout(enemySeedRow);
    m_allowTierMixing = new QCheckBox(tr("Allow replacements from any difficulty tier"),
                                       m_advancedEnemyPanel);
    m_allowTierMixing->setObjectName(QStringLiteral("apEnemyTierMixing"));
    m_allowTierMixing->setChecked(true);
    m_preserveLocomotion = new QCheckBox(tr("Preserve movement style"), m_advancedEnemyPanel);
    m_preserveLocomotion->setObjectName(QStringLiteral("apEnemyPreserveLocomotion"));
    m_preserveLocomotion->setChecked(true);
    m_normalizeScaling = new QCheckBox(tr("Adjust enemy stats for their new role"),
                                        m_advancedEnemyPanel);
    m_normalizeScaling->setObjectName(QStringLiteral("apEnemyNormalizeStats"));
    advancedLayout->addWidget(m_allowTierMixing);
    advancedLayout->addWidget(m_preserveLocomotion);
    advancedLayout->addWidget(m_normalizeScaling);
    m_advancedEnemyPanel->setVisible(false);
    enemyLayout->addWidget(m_advancedEnemyPanel);
    connect(m_randomizeEnemies, &QCheckBox::toggled, this,
            [this](bool enabled) { RefreshEnemizerControls(); });
    connect(m_expandedCoverage, &QGroupBox::toggled, this,
            [this](bool) { RefreshEnemizerControls(); });
    connect(m_advancedEnemyOptions, &QToolButton::toggled, this, [this](bool expanded) {
        m_advancedEnemyOptions->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_advancedEnemyPanel->setVisible(expanded);
    });
    m_operationControls << m_enemizerGroup;
    layout->addWidget(m_enemizerGroup);

    auto* actions = new QHBoxLayout();
    m_playButton = new QPushButton(tr("&Play"), this);
    m_playButton->setObjectName(QStringLiteral("apPlay"));
    m_playButton->setDefault(true);
    m_operationControls << m_playButton;
    connect(m_playButton, &QPushButton::clicked, this, &ApPage::PlayClicked);
    m_cancelButton = new QPushButton(tr("&Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("apCancel"));
    connect(m_cancelButton, &QPushButton::clicked, this, &ApPage::CancelClicked);
    m_switchSeedButton = new QPushButton(tr("Change &seed"), this);
    m_switchSeedButton->setObjectName(QStringLiteral("apSwitchSeed"));
    m_operationControls << m_switchSeedButton;
    connect(m_switchSeedButton, &QPushButton::clicked, this, &ApPage::SwitchSeedClicked);
    actions->addWidget(m_playButton);
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_switchSeedButton);
    layout->addLayout(actions);

    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    layout->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Choose a seed file, then press Play."), this);
    m_statusLabel->setObjectName(QStringLiteral("apStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_statusLabel, 1);

    auto* nav = new QHBoxLayout();
    auto* modsButton = new QPushButton(tr("&Mods"), this);
    m_operationControls << modsButton;
    connect(modsButton, &QPushButton::clicked, this, &ApPage::requestMods);
    auto* emuButton = new QPushButton(tr("&Emulator settings"), this);
    m_operationControls << emuButton;
    connect(emuButton, &QPushButton::clicked, this, &ApPage::requestEmulatorSettings);
    m_regularButton = new QPushButton(tr("&Regular play"), this);
    m_regularButton->setObjectName(QStringLiteral("apRegularPlay"));
    m_operationControls << m_regularButton;
    connect(m_regularButton, &QPushButton::clicked, this, &ApPage::RegularPlayClicked);
    auto* helpButton = new QPushButton(tr("&Help"), this);
    m_operationControls << helpButton;
    connect(helpButton, &QPushButton::clicked, this, &ApPage::HelpClicked);
    nav->addWidget(modsButton);
    nav->addWidget(emuButton);
    nav->addWidget(m_regularButton);
    nav->addWidget(helpButton);
    layout->addLayout(nav);

    connect(m_coordinator, &ApCoordinator::StageChanged, this,
            [this](const QString& stage) { SetStatus(stage, false); });
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(5000);
    connect(m_pollTimer, &QTimer::timeout, this, &ApPage::PollStatus);

    SetBusy(false);
    m_playerLabel->setVisible(false);
    m_playerCombo->setVisible(false);
    m_serverLabel->setVisible(false);
    m_serverEdit->setVisible(false);
    RefreshEnemizerControls();
    RefreshForSession();
}

void ApPage::SetBusy(bool busy, const QString& stage) {
    m_busy = busy;
    for (QWidget* control : m_operationControls) {
        control->setEnabled(!busy);
    }
    m_cancelButton->setVisible(busy);
    m_cancelButton->setEnabled(busy);
    m_progress->setRange(0, busy ? 0 : 1);
    m_progress->setValue(busy ? 0 : 1);
    if (busy && !stage.isEmpty()) {
        SetStatus(stage, false);
    }
    RefreshEnemizerControls();
}

void ApPage::RefreshEnemizerControls() {
    if (m_enemizerGroup == nullptr || m_randomizeEnemies == nullptr) {
        return;
    }
    const bool sessionLocked = m_coordinator != nullptr &&
                               (m_coordinator->HasSession() || m_coordinator->GameStarted());
    m_enemizerGroup->setEnabled(!m_busy && !sessionLocked);
    const bool optionsEnabled = !m_busy && !sessionLocked && m_randomizeEnemies->isChecked();
    m_expandedCoverage->setEnabled(optionsEnabled);
    m_advancedEnemyOptions->setEnabled(optionsEnabled);
    m_advancedEnemyPanel->setEnabled(optionsEnabled && m_advancedEnemyOptions->isChecked());
}

void ApPage::SetStatus(const QString& text, bool isError) {
    m_statusLabel->setText(text);
    // Text alongside color: never color alone.
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("QLabel { color: #c0392b; }")
                                         : QString());
    m_statusLabel->setToolTip(text);
}

void ApPage::ShowError(const QString& detail) {
    SetStatus(detail, true);
}

bool ApPage::EnsureConfigured(QString* error) {
    QString gameRoot;
    Common::PathToQString(gameRoot, Common::installPath);
    const QString backendDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/ap_backend");
    QString stateRoot = ApBackend::DefaultStateRoot();
    return m_coordinator->Configure(gameRoot, backendDir, stateRoot, error);
}

void ApPage::BrowseSeed() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose an Archipelago seed"),
        m_seedEdit->text(), tr("AP seed files (*.zip *.bbseed.json)"));
    if (!path.isEmpty()) {
        m_seedEdit->setText(path);
        SeedChanged();
    }
}

void ApPage::SeedChanged() {
    if (m_inspecting) {
        return;
    }
    const QString seedPath = m_seedEdit->text().trimmed();
    if (seedPath.isEmpty()) {
        m_playerCombo->clear();
        m_playerLabel->setVisible(false);
        m_playerCombo->setVisible(false);
        m_serverEdit->clear();
        m_serverLabel->setVisible(false);
        m_serverEdit->setVisible(false);
        SetStatus(tr("Choose a seed file, then press Play."), false);
        return;
    }
    m_inspecting = true;
    m_cancelled = false;
    SetBusy(true, tr("Checking seed"));
    QString error;
    if (!EnsureConfigured(&error)) {
        ShowError(error);
        m_inspecting = false;
        SetBusy(false);
        return;
    }
    ApResponse seen;
    if (!m_coordinator->InspectSeed(seedPath, {}, &seen, &error)) {
        if (m_cancelled) {
            SetStatus(tr("Seed check cancelled. No game or mod changes were made."), false);
        } else {
            ShowError(error);
        }
        m_inspecting = false;
        SetBusy(false);
        return;
    }
    QStringList slotNames;
    for (const QJsonValue& slot : seen.result.value(QStringLiteral("slots")).toArray()) {
        if (slot.isString()) {
            slotNames.push_back(slot.toString());
        }
    }
    const QString selected = seen.result.value(QStringLiteral("selected")).toString();
    m_playerCombo->clear();
    m_playerCombo->addItems(slotNames);
    m_playerCombo->setCurrentText(selected);
    const bool needsChoice = seen.result.value(QStringLiteral("needs_choice")).toBool() ||
                             slotNames.size() > 1;
    m_playerLabel->setVisible(needsChoice);
    m_playerCombo->setVisible(needsChoice);
    const QString server = seen.result.value(QStringLiteral("server")).toString();
    m_serverEdit->setText(server);
    const bool needsServer = server.isEmpty();
    m_serverLabel->setVisible(needsServer);
    m_serverEdit->setVisible(needsServer);
    if (!needsServer) {
        SetStatus(tr("Seed ready. Press Play."), false);
    } else {
        SetStatus(tr("Seed ready. Enter the server address, then press Play."), false);
    }
    m_inspecting = false;
    SetBusy(false);
}

void ApPage::PlayClicked() {
    if (m_busy) {
        return;
    }
    if (m_coordinator->GameStarted()) {
        QString error;
        if (!m_coordinator->ReturnToGame(&error)) {
            ShowError(error);
        }
        return;
    }
    QString error;
    if (!EnsureConfigured(&error)) {
        ShowError(error);
        return;
    }
    const QString seedPath = m_seedEdit->text().trimmed();
    if (seedPath.isEmpty() || !QFileInfo::exists(seedPath)) {
        ShowError(tr("Choose an Archipelago seed file first."));
        return;
    }
    ApPlayRequest request;
    Common::PathToQString(request.gameRoot, Common::installPath);
    request.seedPath = seedPath;
    if (m_playerCombo->isVisible()) {
        request.playerName = m_playerCombo->currentText();
        if (request.playerName.isEmpty()) {
            ShowError(tr("Choose a player for this seed first."));
            return;
        }
    }
    request.server = m_serverEdit->text().trimmed();
    request.password = m_passwordEdit->text();
    request.enemizer.enabled = m_randomizeEnemies->isChecked();
    if (request.enemizer.enabled) {
        request.enemizer.seed = m_enemySeedEdit->text().trimmed();
        request.enemizer.allowTierMixing = m_allowTierMixing->isChecked();
        request.enemizer.preserveLocomotion = m_preserveLocomotion->isChecked();
        request.enemizer.normalizeScaling = m_normalizeScaling->isChecked();
        if (m_expandedCoverage->isChecked()) {
            request.enemizer.releaseContracts = m_releaseContracts->isChecked();
            request.enemizer.releaseSpawns = m_releaseSpawns->isChecked();
            request.enemizer.releaseChara = m_releaseChara->isChecked();
        }
    }

    m_cancelled = false;
    m_pollTimer->stop();
    SetBusy(true, tr("Preparing your seed"));
    // Backend calls can take several minutes. While they wait, the backend
    // owns a controlled Qt event pump; all mutating controls are disabled.

    ApCoordinator::Prepared prepared;
    bool ok = false;
    QString failure;
    // Prepare.
    if (!m_coordinator->Prepare(request, &prepared, &failure)) {
        if (m_cancelled) {
            SetStatus(tr("Cancelled after the current safe step. No game was started."), false);
        } else {
            ShowError(failure);
        }
        SetBusy(false);
        return;
    }
    if (m_cancelled) {
        SetStatus(tr("Cancelled. Your previous setup is untouched."), false);
        SetBusy(false);
        return;
    }
    // Activate through the shared mod service (copy mode).
    SetBusy(true, tr("Setting up your game"));
    bool wasConflict = false;
    if (!m_coordinator->Activate(prepared, false, &wasConflict, &failure)) {
        if (m_cancelled) {
            SetStatus(tr("Cancelled after the current safe step. Use Regular play to restore the previous setup if needed."), false);
            SetBusy(false);
            return;
        }
        if (wasConflict) {
            // Offer Disable conflicting mod and play, with the exact
            // reversible plan, and nothing else.
            auto answer = QMessageBox::question(
                this, tr("Mod conflict"),
                failure + tr("\n\nDisable the conflicting mod and play? "
                             "It will be left deactivated afterwards; your other mods stay."));
            if (answer == QMessageBox::Yes) {
                if (!m_coordinator->Activate(prepared, true, &wasConflict, &failure)) {
                    ShowError(failure);
                    SetBusy(false);
                    return;
                }
            } else {
                SetBusy(false);
                return;
            }
        } else {
            ShowError(failure);
            SetBusy(false);
            return;
        }
    }
    if (m_cancelled) {
        SetStatus(tr("Cancelled. Use Regular play to restore the previous setup."), false);
        SetBusy(false);
        return;
    }
    // Arm, start, connect.
    if (!m_coordinator->Arm(prepared, &failure)) {
        if (m_cancelled) SetStatus(tr("Cancelled after the current safe step. The game was not started."), false);
        else ShowError(failure);
        SetBusy(false);
        return;
    }
    if (!m_coordinator->StartGame(&failure)) {
        if (m_cancelled) SetStatus(tr("Cancelled after the current safe step."), false);
        else ShowError(failure);
        SetBusy(false);
        return;
    }
    if (!m_coordinator->Connect(&failure)) {
        if (m_cancelled) {
            SetStatus(tr("Cancelled after the current safe step. The game may be open; close it before switching setups."), false);
        } else if (m_coordinator->lastErrorCode() == QStringLiteral("password-required")) {
            ShowError(tr("This server needs a password. Enter it above, then press Play."));
        } else {
            ShowError(failure);
        }
        SetBusy(false);
        return;
    }
    ok = true;
    SetBusy(false);
    if (ok) {
        QString status = tr("Game and Archipelago client started. Check the game for connection status.");
        if (!prepared.enemySummary.isEmpty()) {
            status += QStringLiteral("\n") + prepared.enemySummary;
        }
        SetStatus(status, false);
        m_pollTimer->start();
    }
    RefreshForSession();
    if (!m_coordinator->sessionId().isEmpty()) {
        m_pollTimer->start();
    }
}

void ApPage::CancelClicked() {
    m_cancelled = true;
    m_coordinator->RequestCancel();
    SetStatus(tr("Cancellation requested. The current backend step will finish safely, then Play will stop."), false);
}

void ApPage::SwitchSeedClicked() {
    if (m_busy) {
        return;
    }
    if (m_coordinator->HasSession() || m_coordinator->GameStarted()) {
        auto answer = QMessageBox::question(
            this, tr("Switch seed"),
            tr("A game is running. Quit the game and switch seed?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    QString error;
    if (!m_coordinator->SwitchToSeed(&error)) {
        ShowError(error);
        return;
    }
    m_pollTimer->stop();
    m_seedEdit->clear();
    m_playerCombo->clear();
    SetStatus(tr("Choose a seed file, then press Play."), false);
    RefreshForSession();
}

void ApPage::RegularPlayClicked() {
    if (m_busy) {
        return;
    }
    if (m_coordinator->HasSession() || m_coordinator->GameStarted()) {
        const auto answer = QMessageBox::question(
            this, tr("Switch to regular play"),
            tr("Stop the Archipelago client and game, then remove its active package?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    QString error;
    if (!EnsureConfigured(&error)) {
        ShowError(error);
        return;
    }
    if (!m_coordinator->SwitchToRegularPlay(&error)) {
        ShowError(error);
        return;
    }
    m_pollTimer->stop();
    SetStatus(tr("Archipelago package removed. Your other mods were kept."), false);
    RefreshForSession();
}

void ApPage::HelpClicked() {
    QStringList lines;
    lines.push_back(tr("Backend: %1").arg(ApBackend::FindBackend()));
    lines.push_back(tr("State: %1").arg(ApBackend::DefaultStateRoot()));
    lines.push_back(tr("Upstream baseline: %1").arg(ApFork::UpstreamBaseline()));
    if (m_coordinator->backend() != nullptr && m_coordinator->backend()->IsRunning()) {
        const QJsonObject caps = m_coordinator->backend()->capabilities();
        lines.push_back(tr("Protocol: %1").arg(
            caps.value(QStringLiteral("protocol")).toString()));
    }
    QMessageBox::information(this, tr("Diagnostics"), lines.join(QStringLiteral("\n")));
}

void ApPage::closeEvent(QCloseEvent* event) {
    if (m_busy) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void ApPage::PollStatus() {
    QString state;
    QString error;
    if (!m_coordinator->RefreshStatus(&state, &error)) {
        return;
    }
    if (state == QStringLiteral("playing")) {
        SetStatus(tr("Game and Archipelago client are running. Check the game for connection status."), false);
    } else if (state == QStringLiteral("recoverable")) {
        SetStatus(tr("The Archipelago client or game is no longer running. "
                     "Your setup is kept; use Regular play to restore your previous setup."), false);
    }
    RefreshForSession();
}

void ApPage::RefreshForSession() {
    RefreshEnemizerControls();
    if (m_coordinator->GameStarted()) {
        m_playButton->setText(tr("Return to &game"));
    } else if (m_coordinator->HasSession()) {
        m_playButton->setText(tr("&Play"));
    } else {
        m_playButton->setText(tr("&Play"));
    }
}
