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
#include <QPalette>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QUuid>
#include <QVBoxLayout>

#include "ApFork.h"
#include "modules/Common.h"

namespace {
void StyleDialog(QMessageBox* box) {
    QPalette palette = box->palette();
    if (palette.color(QPalette::Window).lightness() > 128) return;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#20242b")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#f0f2f5")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#363c46")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#f0f2f5")));
    box->setPalette(palette);
    box->setStyleSheet(QStringLiteral(
        "QMessageBox { background: #20242b; color: #f0f2f5; } "
        "QLabel { color: #f0f2f5; } "
        "QPushButton { color: #f0f2f5; background: #363c46; border: 1px solid #66707e; "
        "border-radius: 4px; padding: 5px 12px; min-width: 64px; } "
        "QPushButton:focus { border: 2px solid #79b8ff; }"));
}

QMessageBox::StandardButton AskDark(QWidget* parent, const QString& title,
                                    const QString& text) {
    QMessageBox box(QMessageBox::Question, title, text,
                    QMessageBox::Yes | QMessageBox::No, parent);
    box.setDefaultButton(QMessageBox::No);
    StyleDialog(&box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

bool AskRerandomize(QWidget* parent) {
    QMessageBox box(QMessageBox::Question,
                    QCoreApplication::translate("ApPage", "Prepared mod already exists"),
                    QCoreApplication::translate(
                        "ApPage", "A prepared mod already exists for this Archipelago seed and enemy layout.\n\nCreate a fresh enemy and boss layout for the same world and player? The existing mod will remain untouched."),
                    QMessageBox::NoButton, parent);
    auto* rerandomize = box.addButton(
        QCoreApplication::translate("ApPage", "Rerandomize enemies"),
        QMessageBox::AcceptRole);
    rerandomize->setObjectName(QStringLiteral("rerandomizeEnemiesButton"));
    auto* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    StyleDialog(&box);
    box.exec();
    return box.clickedButton() == rerandomize;
}
} // namespace

ApPage::ApPage(ApCoordinator* coordinator, QWidget* parent)
    : QDialog(parent), m_coordinator(coordinator) {
    setWindowTitle(tr("Bloodborne randomizer"));
    setMinimumSize(620, 460);

    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("<b>Bloodborne randomizer</b>"), this);
    layout->addWidget(title);

    m_setupLabel = new QLabel(tr("Game and emulator setup will be checked when you prepare."), this);
    m_setupLabel->setWordWrap(true);
    layout->addWidget(m_setupLabel);

    auto* form = new QFormLayout();
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName(QStringLiteral("playMode"));
    m_modeCombo->addItem(tr("Archipelago"));
    m_modeCombo->addItem(tr("Standalone randomizer"));
    form->addRow(tr("&Mode:"), m_modeCombo);
    m_operationControls << m_modeCombo;
    m_seedEdit = new QLineEdit(this);
    m_seedEdit->setObjectName(QStringLiteral("apSeedPath"));
    m_seedEdit->setPlaceholderText(tr("AP seed file (.zip or .bbseed.json)"));
    m_seedEdit->setClearButtonEnabled(true);
    auto* browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &ApPage::BrowseSeed);
    m_apSeedRow = new QWidget(this);
    auto* seedRow = new QHBoxLayout(m_apSeedRow);
    seedRow->setContentsMargins(0, 0, 0, 0);
    seedRow->addWidget(m_seedEdit, 1);
    seedRow->addWidget(browse);
    m_seedLabel = new QLabel(tr("&AP seed:"), this);
    m_seedLabel->setBuddy(m_seedEdit);
    form->addRow(m_seedLabel, m_apSeedRow);
    connect(m_seedEdit, &QLineEdit::editingFinished, this, &ApPage::SeedChanged);
    m_operationControls << m_seedEdit << browse;

    m_standaloneSeedEdit = new QLineEdit(this);
    m_standaloneSeedEdit->setObjectName(QStringLiteral("standaloneSeed"));
    m_standaloneSeedEdit->setPlaceholderText(tr("Any text; use the same text to reproduce a run"));
    m_standaloneSeedEdit->setClearButtonEnabled(true);
    m_standaloneSeedEdit->setMaxLength(256);
    m_standaloneSeedLabel = new QLabel(tr("&Randomizer seed:"), this);
    m_standaloneSeedLabel->setBuddy(m_standaloneSeedEdit);
    form->addRow(m_standaloneSeedLabel, m_standaloneSeedEdit);
    m_operationControls << m_standaloneSeedEdit;
    m_includeDlc = new QCheckBox(tr("Include DLC"), this);
    m_includeDlc->setObjectName(QStringLiteral("includeDlc"));
    m_includeDlc->setChecked(true);
    form->addRow(QString(), m_includeDlc);
    m_operationControls << m_includeDlc;

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
    m_enemyMode = new QComboBox(m_enemizerGroup);
    m_enemyMode->setObjectName(QStringLiteral("enemyMode"));
    m_enemyMode->addItem(tr("Reviewed randomization"));
    m_enemyMode->addItem(tr("Expanded coverage (experimental)"));
    m_enemyMode->addItem(tr("Vanilla enemies"));
    enemyLayout->addWidget(m_enemyMode);
    m_standaloneBossNote = new QLabel(tr("Standalone mode keeps bosses unchanged."),
                                      m_enemizerGroup);
    m_standaloneBossNote->setObjectName(QStringLiteral("standaloneBossNote"));
    enemyLayout->addWidget(m_standaloneBossNote);

    m_enemySeedRow = new QWidget(m_enemizerGroup);
    auto* enemySeedRow = new QHBoxLayout(m_enemySeedRow);
    enemySeedRow->setContentsMargins(0, 0, 0, 0);
    enemySeedRow->addWidget(new QLabel(tr("Enemy seed:"), m_enemySeedRow));
    m_enemySeedEdit = new QLineEdit(m_enemySeedRow);
    m_enemySeedEdit->setObjectName(QStringLiteral("apEnemySeed"));
    m_enemySeedEdit->setPlaceholderText(tr("Optional; blank uses the AP seed"));
    enemySeedRow->addWidget(m_enemySeedEdit, 1);
    enemyLayout->insertWidget(1, m_enemySeedRow);
    m_bossPoolRow = new QWidget(m_enemizerGroup);
    auto* bossPoolLayout = new QHBoxLayout(m_bossPoolRow);
    bossPoolLayout->setContentsMargins(0, 0, 0, 0);
    auto* bossPoolLabel = new QLabel(tr("Boss pool:"), m_bossPoolRow);
    m_bossPool = new QComboBox(m_bossPoolRow);
    m_bossPool->setObjectName(QStringLiteral("apBossPool"));
    m_bossPool->addItem(tr("Reviewed"), QStringLiteral("reviewed"));
    m_bossPool->addItem(tr("Only the good bosses (experimental)"),
                        QStringLiteral("good"));
    bossPoolLabel->setBuddy(m_bossPool);
    bossPoolLayout->addWidget(bossPoolLabel);
    bossPoolLayout->addWidget(m_bossPool, 1);
    enemyLayout->insertWidget(2, m_bossPoolRow);
    m_normalizeScaling = new QCheckBox(tr("Scaling"), m_enemizerGroup);
    m_normalizeScaling->setObjectName(QStringLiteral("apEnemyNormalizeStats"));
    m_normalizeScaling->setChecked(true);
    enemyLayout->addWidget(m_normalizeScaling);
    connect(m_enemyMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { RefreshMode(); InvalidatePrepared(); });
    m_operationControls << m_enemizerGroup;
    layout->addWidget(m_enemizerGroup);

    auto* actions = new QHBoxLayout();
    m_randomizeButton = new QPushButton(tr("&Randomize"), this);
    m_randomizeButton->setObjectName(QStringLiteral("apRandomize"));
    m_operationControls << m_randomizeButton;
    connect(m_randomizeButton, &QPushButton::clicked, this, &ApPage::RandomizeClicked);
    m_playButton = new QPushButton(tr("&Launch"), this);
    m_playButton->setObjectName(QStringLiteral("apPlay"));
    m_randomizeButton->setDefault(true);
    m_operationControls << m_playButton;
    connect(m_playButton, &QPushButton::clicked, this, &ApPage::PlayClicked);
    m_cancelButton = new QPushButton(tr("&Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("apCancel"));
    connect(m_cancelButton, &QPushButton::clicked, this, &ApPage::CancelClicked);
    m_switchSeedButton = new QPushButton(tr("Change &seed"), this);
    m_switchSeedButton->setObjectName(QStringLiteral("apSwitchSeed"));
    m_operationControls << m_switchSeedButton;
    connect(m_switchSeedButton, &QPushButton::clicked, this, &ApPage::SwitchSeedClicked);
    actions->addWidget(m_randomizeButton);
    actions->addWidget(m_playButton);
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_switchSeedButton);
    layout->addLayout(actions);

    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(false);
    layout->addWidget(m_progress);

    m_statusLabel = new QLabel(tr("Enter a seed, then press Randomize or Launch."), this);
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

    // The launcher's dark palette does not define PlaceholderText on every
    // platform, which otherwise leaves black hints on dark input fields.
    for (QLineEdit* edit : findChildren<QLineEdit*>()) {
        QPalette palette = edit->palette();
        palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#a9b0b8")));
        edit->setPalette(palette);
    }

    connect(m_coordinator, &ApCoordinator::StageChanged, this,
            [this](const QString& stage) { SetStatus(stage, false); });
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int mode) {
                RefreshMode();
                InvalidatePrepared();
                if (mode == 0 && m_playerCombo->count() == 0 &&
                    QFileInfo::exists(m_seedEdit->text().trimmed())) SeedChanged();
            });
    connect(m_standaloneSeedEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { InvalidatePrepared(); });
    connect(m_includeDlc, &QCheckBox::toggled, this,
            [this](bool) { InvalidatePrepared(); });
    connect(m_seedEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { InvalidatePrepared(); });
    connect(m_playerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { InvalidatePrepared(); });
    connect(m_serverEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { InvalidatePrepared(); });
    connect(m_passwordEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { InvalidatePrepared(); });
    connect(m_enemySeedEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { InvalidatePrepared(); });
    connect(m_bossPool, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { InvalidatePrepared(); });
    connect(m_normalizeScaling, &QCheckBox::toggled, this,
            [this](bool) { InvalidatePrepared(); });
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(5000);
    connect(m_pollTimer, &QTimer::timeout, this, &ApPage::PollStatus);

    SetBusy(false);
    m_playerLabel->setVisible(false);
    m_playerCombo->setVisible(false);
    m_serverLabel->setVisible(false);
    m_serverEdit->setVisible(false);
    RefreshEnemizerControls();
    LoadSettings();
    RefreshMode();
    RefreshForSession();
    if (m_modeCombo->currentIndex() == 0 &&
        QFileInfo::exists(m_seedEdit->text().trimmed())) SeedChanged();
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
    m_progress->setVisible(busy);
    if (busy && !stage.isEmpty()) {
        SetStatus(stage, false);
    }
    RefreshEnemizerControls();
    RefreshForSession();
}

void ApPage::RefreshEnemizerControls() {
    if (m_enemizerGroup == nullptr || m_enemyMode == nullptr) {
        return;
    }
    const bool sessionLocked = m_coordinator != nullptr && m_coordinator->GameStarted();
    m_enemizerGroup->setEnabled(!m_busy && !sessionLocked);
    const bool optionsEnabled = !m_busy && !sessionLocked && m_enemyMode->currentIndex() != 2;
    m_normalizeScaling->setEnabled(optionsEnabled);
    m_bossPool->setEnabled(optionsEnabled && m_modeCombo->currentIndex() == 0);
}

void ApPage::InvalidatePrepared() {
    if (m_busy || m_coordinator->GameStarted()) return;
    m_hasPrepared = false;
    SaveSettings();
    if (m_playButton != nullptr) RefreshForSession();
}

void ApPage::RefreshMode() {
    const bool standalone = m_modeCombo->currentIndex() == 1;
    if (m_lastModeIndex != m_modeCombo->currentIndex()) {
        if (m_lastModeIndex == 0) {
            m_apEnemyMode = m_enemyMode->currentIndex();
            m_apScaling = m_normalizeScaling->isChecked();
        } else {
            m_standaloneEnemyMode = m_enemyMode->currentIndex();
            m_standaloneScaling = m_normalizeScaling->isChecked();
        }
        m_lastModeIndex = m_modeCombo->currentIndex();
        const QSignalBlocker blockScaling(m_normalizeScaling);
        m_enemyMode->setCurrentIndex(standalone ? m_standaloneEnemyMode : m_apEnemyMode);
        m_normalizeScaling->setChecked(standalone ? m_standaloneScaling : m_apScaling);
    }
    m_seedLabel->setVisible(!standalone);
    m_apSeedRow->setVisible(!standalone);
    m_standaloneSeedEdit->setVisible(standalone);
    m_standaloneSeedLabel->setVisible(standalone);
    m_includeDlc->setVisible(standalone);
    m_standaloneBossNote->setVisible(standalone && m_enemyMode->currentIndex() != 2);
    m_playerLabel->setVisible(!standalone && m_playerCombo->count() > 1);
    m_playerCombo->setVisible(!standalone && m_playerCombo->count() > 1);
    m_serverLabel->setVisible(!standalone && m_serverEdit->text().isEmpty() &&
                              !m_seedEdit->text().isEmpty());
    m_serverEdit->setVisible(m_serverLabel->isVisible());
    m_passwordLabel->setVisible(!standalone);
    m_passwordEdit->setVisible(!standalone);
    m_enemySeedRow->setVisible(!standalone && m_enemyMode->currentIndex() != 2);
    m_bossPoolRow->setVisible(!standalone && m_enemyMode->currentIndex() != 2);
    RefreshEnemizerControls();
}

void ApPage::LoadSettings() {
    const QString path = ApBackend::DefaultStateRoot() + QStringLiteral("/ui-settings.json");
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
        const QSignalBlocker blockMode(m_modeCombo);
        const QSignalBlocker blockApSeed(m_seedEdit);
        const QSignalBlocker blockStandaloneSeed(m_standaloneSeedEdit);
        const QSignalBlocker blockEnemy(m_enemyMode);
        const QSignalBlocker blockBossPool(m_bossPool);
        const QSignalBlocker blockDlc(m_includeDlc);
        const QSignalBlocker blockScaling(m_normalizeScaling);
        m_apEnemyMode = qBound(0, saved.value(QStringLiteral("ap_enemy_mode")).toInt(), 2);
        m_standaloneEnemyMode = qBound(0,
            saved.value(QStringLiteral("standalone_enemy_mode")).toInt(), 2);
        m_apScaling = saved.value(QStringLiteral("ap_normalize_scaling")).toBool(true);
        m_standaloneScaling = saved.value(QStringLiteral("standalone_normalize_scaling")).toBool(true);
        m_lastModeIndex = saved.value(QStringLiteral("mode")).toInt() == 1 ? 1 : 0;
        m_modeCombo->setCurrentIndex(m_lastModeIndex);
        m_enemyMode->setCurrentIndex(m_lastModeIndex == 1 ? m_standaloneEnemyMode : m_apEnemyMode);
        const int bossPoolIndex = m_bossPool->findData(
            saved.value(QStringLiteral("ap_boss_pool")).toString());
        m_bossPool->setCurrentIndex(bossPoolIndex < 0 ? 0 : bossPoolIndex);
        m_normalizeScaling->setChecked(m_lastModeIndex == 1 ? m_standaloneScaling : m_apScaling);
        m_savedApSeedPath = saved.value(QStringLiteral("ap_seed_path")).toString();
        m_seedEdit->setText(m_savedApSeedPath);
        m_standaloneSeedEdit->setText(saved.value(QStringLiteral("standalone_seed")).toString());
        m_includeDlc->setChecked(saved.value(QStringLiteral("include_dlc")).toBool(true));
        m_savedPlayerName = saved.value(QStringLiteral("ap_player")).toString();
        m_savedServer = saved.value(QStringLiteral("ap_server")).toString();
        m_serverEdit->setText(m_savedServer);
        m_enemySeedEdit->setText(saved.value(QStringLiteral("enemy_seed")).toString());
    }
    m_settingsLoaded = true;
}

void ApPage::SaveSettings() const {
    if (!m_settingsLoaded) return;
    const QString root = ApBackend::DefaultStateRoot();
    if (!QDir().mkpath(root)) return;
    QSaveFile file(root + QStringLiteral("/ui-settings.json"));
    if (!file.open(QIODevice::WriteOnly)) return;
    const int apEnemy = m_modeCombo->currentIndex() == 0
        ? m_enemyMode->currentIndex() : m_apEnemyMode;
    const int standaloneEnemy = m_modeCombo->currentIndex() == 1
        ? m_enemyMode->currentIndex() : m_standaloneEnemyMode;
    const bool apScaling = m_modeCombo->currentIndex() == 0
        ? m_normalizeScaling->isChecked() : m_apScaling;
    const bool standaloneScaling = m_modeCombo->currentIndex() == 1
        ? m_normalizeScaling->isChecked() : m_standaloneScaling;
    const QJsonObject saved{
        {QStringLiteral("mode"), m_modeCombo->currentIndex()},
        {QStringLiteral("ap_seed_path"), m_seedEdit->text().trimmed()},
        {QStringLiteral("standalone_seed"), m_standaloneSeedEdit->text().trimmed()},
        {QStringLiteral("include_dlc"), m_includeDlc->isChecked()},
        {QStringLiteral("ap_enemy_mode"), apEnemy},
        {QStringLiteral("ap_boss_pool"), m_bossPool->currentData().toString()},
        {QStringLiteral("standalone_enemy_mode"), standaloneEnemy},
        {QStringLiteral("ap_normalize_scaling"), apScaling},
        {QStringLiteral("standalone_normalize_scaling"), standaloneScaling},
        {QStringLiteral("ap_player"), m_playerCombo->currentText().isEmpty()
                                          ? m_savedPlayerName : m_playerCombo->currentText()},
        {QStringLiteral("ap_server"), m_serverEdit->text().trimmed()},
        {QStringLiteral("enemy_seed"), m_enemySeedEdit->text().trimmed()},
    };
    file.write(QJsonDocument(saved).toJson(QJsonDocument::Indented));
    file.commit();
}

void ApPage::SetStatus(const QString& text, bool isError) {
    m_statusLabel->setText(text);
    // Text alongside color: never color alone.
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("QLabel { color: #ff7b72; }")
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
    const bool configured = m_coordinator->Configure(gameRoot, backendDir, stateRoot, error);
    m_setupLabel->setText(configured
        ? tr("Game and emulator setup ready.")
        : tr("Game setup needs attention. See the status below."));
    return configured;
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
    if (m_inspecting || m_modeCombo->currentIndex() != 0) {
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
        SetStatus(tr("Choose an AP seed file, then press Randomize."), false);
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
    m_playerCombo->setCurrentText(seedPath == m_savedApSeedPath &&
                                  slotNames.contains(m_savedPlayerName)
                                      ? m_savedPlayerName : selected);
    const bool needsChoice = seen.result.value(QStringLiteral("needs_choice")).toBool() ||
                             slotNames.size() > 1;
    m_playerLabel->setVisible(needsChoice);
    m_playerCombo->setVisible(needsChoice);
    const QString server = seen.result.value(QStringLiteral("server")).toString();
    m_serverEdit->setText(seedPath == m_savedApSeedPath && !m_savedServer.isEmpty()
                              ? m_savedServer : server);
    const bool needsServer = m_serverEdit->text().isEmpty();
    m_serverLabel->setVisible(needsServer);
    m_serverEdit->setVisible(needsServer);
    if (!needsServer) {
        SetStatus(tr("Seed ready. Press Randomize."), false);
    } else {
        SetStatus(tr("Seed ready. Enter the server address, then press Randomize."), false);
    }
    m_inspecting = false;
    SetBusy(false);
    SaveSettings();
}

bool ApPage::BuildRequest(ApPlayRequest* request, QString* error) const {
    const QString seedPath = m_seedEdit->text().trimmed();
    if (seedPath.isEmpty() || !QFileInfo::exists(seedPath)) {
        if (error) *error = tr("Choose an Archipelago seed file first.");
        return false;
    }
    Common::PathToQString(request->gameRoot, Common::installPath);
    request->seedPath = seedPath;
    if (m_playerCombo->isVisible()) {
        request->playerName = m_playerCombo->currentText();
        if (request->playerName.isEmpty()) {
            if (error) *error = tr("Choose a player for this seed first.");
            return false;
        }
    }
    request->server = m_serverEdit->text().trimmed();
    request->password = m_passwordEdit->text();
    request->enemizer.enabled = m_enemyMode->currentIndex() != 2;
    if (request->enemizer.enabled) {
        request->enemizer.seed = m_enemySeedEdit->text().trimmed();
        request->enemizer.allowTierMixing = true;
        request->enemizer.preserveLocomotion = false;
        request->enemizer.normalizeScaling = m_normalizeScaling->isChecked();
        request->enemizer.bossPool = m_bossPool->currentData().toString();
        const bool expanded = m_enemyMode->currentIndex() == 1;
        request->enemizer.releaseContracts = expanded;
        request->enemizer.releaseSpawns = expanded;
        request->enemizer.releaseChara = expanded;
    }
    return true;
}

void ApPage::RandomizeClicked() {
    PrepareCurrent(nullptr);
}

bool ApPage::PrepareCurrent(bool* recoveredCollision, bool reuseExisting) {
    if (recoveredCollision != nullptr) *recoveredCollision = false;
    if (m_busy || m_coordinator->GameStarted()) return false;
    QString failure;
    if (!EnsureConfigured(&failure)) { ShowError(failure); return false; }
    m_cancelled = false;
    m_pollTimer->stop();
    ApPlayRequest apRequest;
    StandalonePlayRequest standaloneRequest;
    const bool standalone = m_modeCombo->currentIndex() == 1;
    if (standalone) {
        standaloneRequest.seed = m_standaloneSeedEdit->text().trimmed();
        if (standaloneRequest.seed.isEmpty()) {
            ShowError(tr("Enter a randomizer seed first."));
            return false;
        }
        Common::PathToQString(standaloneRequest.gameRoot, Common::installPath);
        standaloneRequest.includeDlc = m_includeDlc->isChecked();
        standaloneRequest.randomizeEnemies = m_enemyMode->currentIndex() != 2;
        standaloneRequest.expandedCoverage = m_enemyMode->currentIndex() == 1;
        standaloneRequest.normalizeScaling = m_normalizeScaling->isChecked();
    } else if (!BuildRequest(&apRequest, &failure)) {
        ShowError(failure);
        return false;
    }
    InvalidatePrepared();
    SetBusy(true, tr("Randomizing your game"));
    bool prepared = standalone
        ? m_coordinator->PrepareStandalone(standaloneRequest, &m_prepared, &failure)
        : m_coordinator->Prepare(apRequest, &m_prepared, &failure, reuseExisting);
    SetBusy(false);
    if (!prepared && !m_cancelled && !standalone && apRequest.enemizer.enabled &&
        m_coordinator->lastErrorCode() == QStringLiteral("package-exists")) {
        if (!AskRerandomize(this)) {
            SetStatus(tr("Existing prepared mod left unchanged. Change the enemy seed to try again."), false);
            return false;
        }
        // Only the enemy seed changes. The AP world, player, and every other
        // option stay fixed; the existing immutable package is never replaced.
        const QString freshSeed = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_enemySeedEdit->setText(freshSeed);
        apRequest.enemizer.seed = freshSeed;
        SaveSettings();
        SetBusy(true, tr("Rerandomizing enemies and bosses"));
        prepared = m_coordinator->Prepare(apRequest, &m_prepared, &failure);
        SetBusy(false);
        if (recoveredCollision != nullptr) *recoveredCollision = true;
    }
    if (!prepared || m_cancelled) {
        if (m_cancelled) SetStatus(tr("Cancelled. No game was started."), false);
        else ShowError(failure);
        return false;
    }
    m_hasPrepared = true;
    SetStatus(tr("Randomization ready. Press Launch when you are ready to play."), false);
    SaveSettings();
    RefreshForSession();
    return true;
}

void ApPage::PlayClicked() {
    if (m_busy) return;
    QString failure;
    if (m_coordinator->GameStarted()) {
        if (!m_coordinator->ReturnToGame(&failure)) ShowError(failure);
        return;
    }
    if (!m_hasPrepared) {
        bool recoveredCollision = false;
        if (!PrepareCurrent(&recoveredCollision, true) || recoveredCollision) return;
    }
    if (!EnsureConfigured(&failure)) { ShowError(failure); return; }
    m_cancelled = false;
    SetBusy(true, tr("Setting up your game"));
    bool wasConflict = false;
    if (!m_coordinator->Activate(m_prepared, false, &wasConflict, &failure)) {
        if (m_cancelled) {
            SetStatus(tr("Cancelled after the current safe step. Use Regular play to restore the previous setup if needed."), false);
            SetBusy(false);
            return;
        }
        if (wasConflict) {
            // Offer Disable conflicting mod and play, with the exact
            // reversible plan, and nothing else.
            auto answer = AskDark(
                this, tr("Mod conflict"),
                failure + tr("\n\nDisable the conflicting mod and play? "
                             "It will be left deactivated afterwards; your other mods stay."));
            if (answer == QMessageBox::Yes) {
                if (!m_coordinator->Activate(m_prepared, true, &wasConflict, &failure)) {
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
    // AP needs a backend arm and client. Standalone has already verified its
    // receipt before activation and launches without an AP session.
    if (m_prepared.mode == ApCoordinator::Prepared::Mode::Archipelago &&
        !m_coordinator->Arm(m_prepared, &failure)) {
        ShowError(failure);
        SetBusy(false);
        return;
    }
    if (!m_coordinator->StartGame(&failure)) {
        if (m_cancelled) SetStatus(tr("Cancelled after the current safe step."), false);
        else ShowError(failure);
        SetBusy(false);
        return;
    }
    if (m_prepared.mode == ApCoordinator::Prepared::Mode::Archipelago &&
        !m_coordinator->Connect(&failure)) {
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
    SetBusy(false);
    QString status = m_prepared.mode == ApCoordinator::Prepared::Mode::Standalone
        ? tr("Game started with the verified standalone randomizer package.")
        : tr("Game and Archipelago client started. Check the game for connection status.");
    if (!m_prepared.enemySummary.isEmpty()) {
        status += QStringLiteral("\n") + m_prepared.enemySummary;
    }
    SetStatus(status, false);
    m_pollTimer->start();
    RefreshForSession();
}

void ApPage::CancelClicked() {
    m_cancelled = true;
    m_coordinator->RequestCancel();
    SetStatus(tr("Cancellation requested. The current backend step will finish safely."), false);
}

void ApPage::SwitchSeedClicked() {
    if (m_busy) {
        return;
    }
    if (m_coordinator->HasSession() || m_coordinator->GameStarted()) {
        auto answer = AskDark(
            this, tr("Switch seed"),
            tr("Stop the current randomizer session and change seed?"));
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
    m_standaloneSeedEdit->clear();
    m_playerCombo->clear();
    m_hasPrepared = false;
    SetStatus(tr("Choose a mode and seed, then press Randomize."), false);
    RefreshForSession();
}

void ApPage::RegularPlayClicked() {
    if (m_busy) {
        return;
    }
    if (m_coordinator->HasSession() || m_coordinator->GameStarted()) {
        const auto answer = AskDark(
            this, tr("Switch to regular play"),
            tr("Stop the current game and client, then remove its active randomizer package?"));
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
    m_hasPrepared = false;
    SetStatus(tr("Randomizer package removed. Your other mods were kept."), false);
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
    QMessageBox box(QMessageBox::Information, tr("Diagnostics"),
                    lines.join(QStringLiteral("\n")), QMessageBox::Ok, this);
    StyleDialog(&box);
    box.exec();
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
        SetStatus(m_prepared.mode == ApCoordinator::Prepared::Mode::Standalone
                      ? tr("Game is running with the standalone randomizer package.")
                      : tr("Game and Archipelago client are running. Check the game for connection status."), false);
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
        m_playButton->setEnabled(!m_busy);
    } else {
        m_playButton->setText(tr("&Launch"));
        const bool hasSeed = m_modeCombo->currentIndex() == 1
            ? !m_standaloneSeedEdit->text().trimmed().isEmpty()
            : !m_seedEdit->text().trimmed().isEmpty();
        m_playButton->setEnabled(!m_busy && (m_hasPrepared || hasSeed));
    }
    m_randomizeButton->setEnabled(!m_busy && !m_coordinator->GameStarted());
    m_randomizeButton->setDefault(!m_hasPrepared && !m_coordinator->GameStarted());
    m_playButton->setDefault(m_hasPrepared || m_coordinator->GameStarted());
}
