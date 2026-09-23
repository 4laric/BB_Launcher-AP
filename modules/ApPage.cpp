// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ApPage.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
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
    m_seedEdit->setPlaceholderText(tr("AP seed file (.zip or .bbseed.json)"));
    m_seedEdit->setClearButtonEnabled(true);
    auto* browse = new QPushButton(tr("Browse&hellip;"), this);
    connect(browse, &QPushButton::clicked, this, &ApPage::BrowseSeed);
    auto* seedRow = new QHBoxLayout();
    seedRow->addWidget(m_seedEdit, 1);
    seedRow->addWidget(browse);
    form->addRow(tr("&Seed:"), seedRow);
    connect(m_seedEdit, &QLineEdit::editingFinished, this, &ApPage::SeedChanged);

    m_playerLabel = new QLabel(tr("&Player:"), this);
    m_playerCombo = new QComboBox(this);
    m_playerCombo->setEditable(false);
    m_playerLabel->setBuddy(m_playerCombo);
    form->addRow(m_playerLabel, m_playerCombo);

    m_serverLabel = new QLabel(tr("S&erver:"), this);
    m_serverEdit = new QLineEdit(this);
    m_serverEdit->setPlaceholderText(tr("archipelago.gg:port"));
    m_serverLabel->setBuddy(m_serverEdit);
    form->addRow(m_serverLabel, m_serverEdit);

    m_passwordLabel = new QLabel(tr("P&assword:"), this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordLabel->setBuddy(m_passwordEdit);
    form->addRow(m_passwordLabel, m_passwordEdit);
    layout->addLayout(form);

    auto* actions = new QHBoxLayout();
    m_playButton = new QPushButton(tr("&Play"), this);
    m_playButton->setDefault(true);
    connect(m_playButton, &QPushButton::clicked, this, &ApPage::PlayClicked);
    m_cancelButton = new QPushButton(tr("&Cancel"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, &ApPage::CancelClicked);
    m_switchSeedButton = new QPushButton(tr("Change &seed"), this);
    connect(m_switchSeedButton, &QPushButton::clicked, this, &ApPage::SwitchSeedClicked);
    actions->addWidget(m_playButton);
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_switchSeedButton);
    layout->addLayout(actions);

    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    layout->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Choose a seed file, then press Play."), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_statusLabel, 1);

    auto* nav = new QHBoxLayout();
    auto* modsButton = new QPushButton(tr("&Mods"), this);
    connect(modsButton, &QPushButton::clicked, this, &ApPage::requestMods);
    auto* emuButton = new QPushButton(tr("&Emulator settings"), this);
    connect(emuButton, &QPushButton::clicked, this, &ApPage::requestEmulatorSettings);
    m_regularButton = new QPushButton(tr("&Regular play"), this);
    connect(m_regularButton, &QPushButton::clicked, this, &ApPage::RegularPlayClicked);
    auto* helpButton = new QPushButton(tr("&Help"), this);
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
    m_passwordLabel->setVisible(false);
    m_passwordEdit->setVisible(false);
    RefreshForSession();
}

void ApPage::SetBusy(bool busy, const QString& stage) {
    m_playButton->setEnabled(!busy);
    m_cancelButton->setVisible(busy);
    m_switchSeedButton->setEnabled(!busy);
    m_seedEdit->setEnabled(!busy);
    m_progress->setRange(0, busy ? 0 : 1);
    m_progress->setValue(busy ? 0 : 1);
    if (busy && !stage.isEmpty()) {
        SetStatus(stage, false);
    }
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
    if (m_inspecting || m_seedEdit->text().trimmed().isEmpty()) {
        return;
    }
    m_inspecting = true;
    QString error;
    if (!EnsureConfigured(&error)) {
        ShowError(error);
        m_inspecting = false;
        return;
    }
    if (!m_coordinator->backend()->IsRunning() &&
        !m_coordinator->backend()->Start(ApBackend::DefaultStateRoot(), &error)) {
        ShowError(error);
        m_inspecting = false;
        return;
    }
    ApResponse seen = m_coordinator->backend()->Call(
        QStringLiteral("inspect_seed"),
        QJsonObject{{QStringLiteral("seed_path"), m_seedEdit->text().trimmed()}}, 30000);
    if (!seen.ok) {
        if (seen.error.code == QStringLiteral("ambiguous-player")) {
            // Multi-slot seed: remember the choice by seed; ask once.
            ShowError(tr("This seed has several players. Choose yours, then press Play."));
        } else {
            ShowError(seen.error.detail);
        }
        m_inspecting = false;
        return;
    }
    const QStringList slotNames = seen.result.value(QStringLiteral("slots")).toVariant().toStringList();
    const QString selected = seen.result.value(QStringLiteral("selected")).toString();
    const bool needsChoice = seen.result.value(QStringLiteral("needs_choice")).toBool();
    m_playerCombo->clear();
    m_playerCombo->addItems(slotNames);
    m_playerCombo->setCurrentText(selected);
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
}

void ApPage::PlayClicked() {
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
    }
    request.server = m_serverEdit->text().trimmed();
    request.password = m_passwordEdit->text();

    m_cancelled = false;
    SetBusy(true, tr("Preparing your seed"));
    // The flow runs stepwise on this thread; every backend call pumps
    // events while waiting so Cancel and stage text stay live.

    ApCoordinator::Prepared prepared;
    bool ok = false;
    QString failure;
    // Prepare.
    if (!m_coordinator->Prepare(request, &prepared, &failure)) {
        ShowError(failure);
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
        ShowError(failure);
        SetBusy(false);
        return;
    }
    if (!m_coordinator->StartGame(&failure)) {
        ShowError(failure);
        SetBusy(false);
        return;
    }
    if (!m_coordinator->Connect(&failure)) {
        if (m_coordinator->lastErrorCode() == QStringLiteral("password-required")) {
            m_passwordLabel->setVisible(true);
            m_passwordEdit->setVisible(true);
            ShowError(tr("This server needs a password. Enter it, then press Play."));
        } else {
            ShowError(failure);
        }
        SetBusy(false);
        return;
    }
    ok = true;
    SetBusy(false);
    if (ok) {
        SetStatus(tr("Connected. Ready to play."), false);
        m_pollTimer->start();
    }
    RefreshForSession();
}

void ApPage::CancelClicked() {
    m_cancelled = true;
    m_coordinator->backend()->RequestCancel();
    SetStatus(tr("Cancelling at the next safe step. Your previous setup is kept."), false);
}

void ApPage::SwitchSeedClicked() {
    if (m_coordinator->IsPlaying()) {
        auto answer = QMessageBox::question(
            this, tr("Switch seed"),
            tr("A game is running. Quit the game and switch seed?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    m_pollTimer->stop();
    m_coordinator->ResetSession();
    m_seedEdit->clear();
    m_playerCombo->clear();
    SetStatus(tr("Choose a seed file, then press Play."), false);
    RefreshForSession();
}

void ApPage::RegularPlayClicked() {
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
    if (m_coordinator->backend()->IsRunning()) {
        const QJsonObject caps = m_coordinator->backend()->capabilities();
        lines.push_back(tr("Protocol: %1").arg(
            caps.value(QStringLiteral("protocol")).toString()));
    }
    QMessageBox::information(this, tr("Diagnostics"), lines.join(QStringLiteral("\n")));
}

void ApPage::PollStatus() {
    QString state;
    QString error;
    if (!m_coordinator->RefreshStatus(&state, &error)) {
        return;
    }
    if (state == QStringLiteral("playing")) {
        SetStatus(tr("Connected. Ready to play."), false);
    } else if (state == QStringLiteral("recoverable")) {
        // Neutral wait: reconnect with backoff, keep preparation.
        SetStatus(tr("Reconnecting to Archipelago. Your setup is kept; "
                     "no need to prepare again."), false);
    }
    RefreshForSession();
}

void ApPage::RefreshForSession() {
    if (m_coordinator->IsPlaying()) {
        m_playButton->setText(tr("Return to &game"));
    } else if (m_coordinator->HasSession()) {
        m_playButton->setText(tr("&Play"));
    } else {
        m_playButton->setText(tr("&Play"));
    }
}
