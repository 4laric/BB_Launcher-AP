// SPDX-FileCopyrightText: Copyright 2026 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QTimer>

#include <filesystem>
#include <memory>

#include "../modules/ApCoordinator.h"
#include "../modules/ApPage.h"
#include "../modules/ipc/ipc_client.h"

namespace Common {
extern const char VERSION[] = "ap-ui-test";
std::filesystem::path installPath;
std::filesystem::path shadPs4Executable;
std::filesystem::path GetBBLFilesPath() {
    return std::filesystem::current_path() / "BBLauncher";
}
void PathToQString(QString& result, const std::filesystem::path& path) {
    result = QString::fromStdWString(path.wstring());
}
} // namespace Common

namespace Config {
bool GameRunning = false;
} // namespace Config

namespace Build {
extern const char Rev[] = "ap-ui-test";
} // namespace Build

// EmulatorService's real Start path is replaced by FakeEmulatorService in
// these tests. This definition only satisfies the unused base method.
void IpcClient::startEmulator(const QFileInfo&, const QStringList&, const QString&) {}

class FakeEmulatorService final : public EmulatorService {
  public:
    FakeEmulatorService() : EmulatorService(nullptr) {}

    bool Start(const QFileInfo&, const QStringList&, const QString&,
               EmulatorProcessIdentity* identity, QString*) override {
        ++startCalls;
        running = true;
        if (identity != nullptr) {
            identity->executable = QStringLiteral("fake-shad.exe");
            identity->executableSha256 = QStringLiteral("fixture-hash");
            identity->pid = 42;
            identity->creationTime = 77;
            identity->hasCreationTime = true;
            identity->valid = true;
        }
        return true;
    }
    bool Focus(const EmulatorProcessIdentity&, QString*) override {
        ++focusCalls;
        return running;
    }
    bool Stop(const EmulatorProcessIdentity&, QString* error) override {
        ++stopCalls;
        if (!stopSucceeds) {
            if (error != nullptr) *error = QStringLiteral("fixture game refused to close");
            return false;
        }
        running = false;
        return true;
    }
    bool IsEmulatorRunning() const override { return running; }

    bool running = false;
    bool stopSucceeds = true;
    int startCalls = 0;
    int focusCalls = 0;
    int stopCalls = 0;
};

class ApUiTest final : public QObject {
    Q_OBJECT

  private slots:
    void init() {
        m_previousCwd = QDir::currentPath();
        m_scratch = std::make_unique<QTemporaryDir>();
        QVERIFY(m_scratch->isValid());
        QVERIFY(QDir::setCurrent(m_scratch->path()));
        m_gameRoot = m_scratch->path() + QStringLiteral("/CUSA03173");
        QVERIFY(QDir().mkpath(m_gameRoot));
        Common::installPath = std::filesystem::path(m_gameRoot.toStdWString());
        Common::shadPs4Executable = std::filesystem::path(
            (m_scratch->path() + QStringLiteral("/fake-shad.exe")).toStdWString());
        QFile shad(QString::fromStdWString(Common::shadPs4Executable.wstring()));
        QVERIFY(shad.open(QIODevice::WriteOnly));
        shad.write("fixture");
        shad.close();
        Config::GameRunning = false;
        qputenv("BB_AP_STATE_ROOT", (m_scratch->path() + QStringLiteral("/state")).toLocal8Bit());
        m_capturedPrepare = m_scratch->path() + QStringLiteral("/prepare-request.json");
        qputenv("BB_AP_TEST_CAPTURE_REQUEST", m_capturedPrepare.toLocal8Bit());

        const QString package = QDir::currentPath() +
                                QStringLiteral("/BBLauncher/Mods/Archipelago-Fixture/dvdroot_ps4/map");
        QVERIFY(QDir().mkpath(package));
        QFile payload(package + QStringLiteral("/fixture.bin"));
        QVERIFY(payload.open(QIODevice::WriteOnly));
        payload.write("fixture-map");
        payload.close();

        m_emulator = std::make_unique<FakeEmulatorService>();
        m_coordinator = std::make_unique<ApCoordinator>(m_emulator.get());
    }

    void cleanup() {
        m_coordinator.reset();
        m_emulator.reset();
        qunsetenv("BB_AP_TEST_CAPTURE_REQUEST");
        QVERIFY(QDir::setCurrent(m_previousCwd));
        m_scratch.reset();
    }

    void seedChoicePlayReturnAndSwitch() {
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();

        ApPage page(m_coordinator.get());
        page.show();
        auto* seedEdit = page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"));
        auto* player = page.findChild<QComboBox*>(QStringLiteral("apPlayerChoice"));
        auto* play = page.findChild<QPushButton*>(QStringLiteral("apPlay"));
        auto* changeSeed = page.findChild<QPushButton*>(QStringLiteral("apSwitchSeed"));
        auto* randomize = page.findChild<QCheckBox*>(QStringLiteral("apRandomizeEnemies"));
        auto* coverage = page.findChild<QGroupBox*>(QStringLiteral("apExpandedEnemyCoverage"));
        auto* contracts = page.findChild<QCheckBox*>(QStringLiteral("apEnemyScriptedBehavior"));
        auto* spawns = page.findChild<QCheckBox*>(QStringLiteral("apEnemyAmbushes"));
        auto* hunters = page.findChild<QCheckBox*>(QStringLiteral("apEnemyHunters"));
        auto* advanced = page.findChild<QToolButton*>(QStringLiteral("apAdvancedEnemyOptions"));
        auto* enemySeed = page.findChild<QLineEdit*>(QStringLiteral("apEnemySeed"));
        QVERIFY(seedEdit && player && play && changeSeed);
        QVERIFY(randomize && coverage && contracts && spawns && hunters && advanced && enemySeed);
        QVERIFY(randomize->isChecked());
        QVERIFY(coverage->isChecked());
        QVERIFY(contracts->isChecked());
        QVERIFY(spawns->isChecked());
        QVERIFY(hunters->isChecked());
        randomize->setChecked(false);
        QVERIFY(!coverage->isEnabled());
        randomize->setChecked(true);
        QVERIFY(coverage->isEnabled());
        advanced->click();
        enemySeed->setText(QStringLiteral("fixture-enemy-seed"));
        spawns->setChecked(false);
        seedEdit->setText(seedPath);
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        QVERIFY(player->isVisible());
        QCOMPARE(player->count(), 2);
        player->setCurrentText(QStringLiteral("Bob"));
        QTest::mouseClick(play, Qt::LeftButton);

        QVERIFY(m_coordinator->IsPlaying());
        QCOMPARE(m_coordinator->displayTitle(), QStringLiteral("Fixture seed — Bob"));
        QCOMPARE(m_emulator->startCalls, 1);
        QVERIFY(!randomize->isEnabled());
        QFile captured(m_capturedPrepare);
        QVERIFY(captured.open(QIODevice::ReadOnly));
        const QJsonObject capturedRequest =
            QJsonDocument::fromJson(captured.readAll()).object();
        const QJsonObject enemy =
            capturedRequest.value(QStringLiteral("params")).toObject()
                .value(QStringLiteral("enemizer")).toObject();
        QCOMPARE(enemy.value(QStringLiteral("enabled")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("seed")).toString(),
                 QStringLiteral("fixture-enemy-seed"));
        QCOMPARE(enemy.value(QStringLiteral("allow_tier_mixing")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("preserve_locomotion")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("normalize_scaling")).toBool(), false);
        QCOMPARE(enemy.value(QStringLiteral("boss_canary")).toBool(), false);
        QVERIFY(enemy.value(QStringLiteral("boss_pool")).isNull());
        QCOMPARE(enemy.value(QStringLiteral("release_contracts")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("release_spawns")).toBool(), false);
        QCOMPARE(enemy.value(QStringLiteral("release_chara")).toBool(), true);
        QVERIFY(page.findChild<QLabel*>(QStringLiteral("apStatus"))->text()
                    .contains(QStringLiteral("117 enemy swaps")));
        QVERIFY(page.findChild<QLabel*>(QStringLiteral("apStatus"))->text()
                    .contains(QStringLiteral("214 map files")));
        QVERIFY(QDir(QDir::currentPath() + QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QFile overlay(m_scratch->path() + QStringLiteral("/CUSA03173-mods/dvdroot_ps4/map/fixture.bin"));
        QVERIFY(overlay.exists());

        // A repeated Configure (as the Play page performs) must not discard
        // an active play/session handle.
        QString error;
        const QString backendDir = QCoreApplication::applicationDirPath() + QStringLiteral("/ap_backend");
        QVERIFY2(m_coordinator->Configure(m_gameRoot, backendDir, ApBackend::DefaultStateRoot(), &error),
                 qPrintable(error));
        QVERIFY(m_coordinator->IsPlaying());
        QTest::mouseClick(play, Qt::LeftButton);
        QCOMPARE(m_emulator->focusCalls, 1);
        QCOMPARE(m_emulator->startCalls, 1);

        QTimer answerTimer;
        answerTimer.setInterval(10);
        connect(&answerTimer, &QTimer::timeout, &answerTimer, [&answerTimer] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box != nullptr) {
                if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
                    yes->click();
                    answerTimer.stop();
                }
            }
        });
        answerTimer.start();
        QVERIFY(changeSeed->isEnabled());
        QSignalSpy switchClicked(changeSeed, &QPushButton::clicked);
        QTest::mouseClick(changeSeed, Qt::LeftButton);
        QCOMPARE(switchClicked.count(), 1);
        auto* status = page.findChild<QLabel*>(QStringLiteral("apStatus"));
        QVERIFY2(!m_coordinator->HasSession(), status ? qPrintable(status->text()) : "missing status");
        QVERIFY(!m_coordinator->GameStarted());
        QCOMPARE(m_emulator->stopCalls, 1);
        QVERIFY(randomize->isEnabled());
        QVERIFY(!overlay.exists());
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QVERIFY(QFile::exists(QDir::currentPath() +
                              QStringLiteral("/BBLauncher/Mods/Archipelago-Fixture/map/fixture.bin")));
    }

    void activationRefusesAnAlreadyRunningEmulator() {
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        m_emulator->running = true;

        ApPage page(m_coordinator.get());
        page.show();
        auto* seedEdit = page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"));
        auto* player = page.findChild<QComboBox*>(QStringLiteral("apPlayerChoice"));
        auto* play = page.findChild<QPushButton*>(QStringLiteral("apPlay"));
        seedEdit->setText(seedPath);
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        player->setCurrentText(QStringLiteral("Alice"));
        QTest::mouseClick(play, Qt::LeftButton);
        QVERIFY(m_coordinator->HasSession()); // Preparation completed.
        QVERIFY(!m_coordinator->IsPlaying());
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QVERIFY(!QFile::exists(m_scratch->path() + QStringLiteral("/CUSA03173-mods/dvdroot_ps4/map/fixture.bin")));
    }

    void failedGameStopLeavesActiveOverlayUntouched() {
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        ApCoordinator::Prepared prepared;
        ApPlayRequest request{m_gameRoot, seedPath, QStringLiteral("Alice"), {}, {}};
        QString error;
        QVERIFY2(m_coordinator->Configure(m_gameRoot,
                    QCoreApplication::applicationDirPath() + QStringLiteral("/ap_backend"),
                    ApBackend::DefaultStateRoot(), &error), qPrintable(error));
        QVERIFY2(m_coordinator->Prepare(request, &prepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Arm(prepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->StartGame(&error), qPrintable(error));
        QVERIFY2(m_coordinator->Connect(&error), qPrintable(error));
        m_emulator->stopSucceeds = false;
        QVERIFY(!m_coordinator->SwitchToSeed(&error));
        QVERIFY(m_coordinator->HasSession());
        QVERIFY(m_coordinator->GameStarted());
        QVERIFY(QDir(QDir::currentPath() + QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QVERIFY(QFile::exists(m_scratch->path() + QStringLiteral("/CUSA03173-mods/dvdroot_ps4/map/fixture.bin")));
    }

    void cancelButtonRemainsResponsiveAndProtocolStaysSynchronized() {
        qputenv("BB_AP_TEST_DELAY_INSPECT_MS", "350");
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        ApPage page(m_coordinator.get());
        page.show();
        auto* seedEdit = page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"));
        seedEdit->setText(seedPath);
        QTimer::singleShot(100, [&page] {
            if (auto* cancel = page.findChild<QPushButton*>(QStringLiteral("apCancel"))) {
                QTest::mouseClick(cancel, Qt::LeftButton);
            }
        });
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        auto* status = page.findChild<QLabel*>(QStringLiteral("apStatus"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        qunsetenv("BB_AP_TEST_DELAY_INSPECT_MS");
        // A subsequent request succeeds; the canceled response was drained
        // and did not desynchronize the JSON-lines protocol.
        ApResponse response;
        QString error;
        QVERIFY2(m_coordinator->InspectSeed(seedPath, QStringLiteral("Alice"), &response, &error),
                 qPrintable(error));
        QVERIFY(response.ok);
    }

  private:
    QString m_previousCwd;
    QString m_capturedPrepare;
    std::unique_ptr<QTemporaryDir> m_scratch;
    QString m_gameRoot;
    std::unique_ptr<FakeEmulatorService> m_emulator;
    std::unique_ptr<ApCoordinator> m_coordinator;
};

QTEST_MAIN(ApUiTest)
#include "apui_test.moc"
