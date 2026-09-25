// SPDX-FileCopyrightText: Copyright 2026 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QStandardPaths>
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
    bool OwnsProcess(const EmulatorProcessIdentity& identity) const override {
        return running && identity.valid && identity.pid == 42 &&
               identity.creationTime == 77;
    }

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
        m_capturedPrepares = m_scratch->path() + QStringLiteral("/prepare-requests.jsonl");
        qputenv("BB_AP_TEST_CAPTURE_PREPARES", m_capturedPrepares.toLocal8Bit());
        m_capturedVerify = m_scratch->path() + QStringLiteral("/verify-request.json");
        qputenv("BB_AP_TEST_CAPTURE_VERIFY", m_capturedVerify.toLocal8Bit());
        m_opLog = m_scratch->path() + QStringLiteral("/backend-ops.txt");
        qputenv("BB_AP_TEST_OP_LOG", m_opLog.toLocal8Bit());

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
        qunsetenv("BB_AP_TEST_CAPTURE_PREPARES");
        qunsetenv("BB_AP_TEST_COLLIDE_ENEMY_SEED");
        qunsetenv("BB_AP_TEST_MIGRATION_FAIL");
        qunsetenv("BB_AP_TEST_LEGACY_MARKER");
        qunsetenv("BB_AP_TEST_REQUIRE_DEACTIVATED");
        qunsetenv("BB_AP_TEST_CAPTURE_VERIFY");
        qunsetenv("BB_AP_TEST_OP_LOG");
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
        auto* randomize = page.findChild<QPushButton*>(QStringLiteral("apRandomize"));
        auto* changeSeed = page.findChild<QPushButton*>(QStringLiteral("apSwitchSeed"));
        auto* enemyMode = page.findChild<QComboBox*>(QStringLiteral("enemyMode"));
        auto* bossPool = page.findChild<QComboBox*>(QStringLiteral("apBossPool"));
        auto* enemySeed = page.findChild<QLineEdit*>(QStringLiteral("apEnemySeed"));
        auto* scaling = page.findChild<QCheckBox*>(QStringLiteral("apEnemyNormalizeStats"));
        QVERIFY(seedEdit && player && play && randomize && changeSeed);
        QVERIFY(enemyMode && bossPool && enemySeed && scaling);
        QVERIFY(!page.findChild<QCheckBox*>(QStringLiteral("apEnemyTierMixing")));
        QVERIFY(!page.findChild<QCheckBox*>(QStringLiteral("apEnemyPreserveLocomotion")));
        QVERIFY(!page.findChild<QCheckBox*>(QStringLiteral("apShuffleBosses")));
        QVERIFY(!page.findChild<QWidget*>(QStringLiteral("apAdvancedEnemyOptions")));
        QCOMPARE(enemyMode->currentIndex(), 0);
        QCOMPARE(bossPool->currentData().toString(), QStringLiteral("reviewed"));
        QVERIFY(bossPool->isVisible() && bossPool->isEnabled());
        QVERIFY(!page.findChild<QLabel*>(QStringLiteral("standaloneBossNote"))->isVisible());
        QVERIFY(scaling->isChecked());
        QVERIFY(!play->isEnabled());
        enemyMode->setCurrentIndex(2);
        QVERIFY(!scaling->isEnabled());
        QVERIFY(!bossPool->isVisible());
        enemyMode->setCurrentIndex(1);
        QVERIFY(bossPool->isVisible() && bossPool->isEnabled());
        bossPool->setCurrentIndex(1);
        QCOMPARE(bossPool->currentData().toString(), QStringLiteral("good"));
        QVERIFY(scaling->isVisible() && scaling->isEnabled());
        QTest::mouseClick(scaling, Qt::LeftButton, Qt::NoModifier,
                          QPoint(10, scaling->height() / 2));
        QVERIFY(!scaling->isChecked());
        QTest::mouseClick(scaling, Qt::LeftButton, Qt::NoModifier,
                          QPoint(10, scaling->height() / 2));
        QVERIFY(scaling->isChecked());
        enemySeed->setText(QStringLiteral("fixture-enemy-seed"));
        seedEdit->setText(seedPath);
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        QVERIFY(player->isVisible());
        QCOMPARE(player->count(), 2);
        player->setCurrentText(QStringLiteral("Bob"));
        QTest::mouseClick(randomize, Qt::LeftButton);
        QVERIFY(m_coordinator->HasSession());
        QVERIFY(!m_coordinator->GameStarted());
        QCOMPARE(m_emulator->startCalls, 0);
        QVERIFY(play->isEnabled());
        bossPool->setCurrentIndex(0); // Changing the pool invalidates the prepared layout.
        QTest::mouseClick(play, Qt::LeftButton);

        QVERIFY(m_coordinator->IsPlaying());
        QCOMPARE(m_coordinator->displayTitle(), QStringLiteral("Fixture seed — Bob"));
        QCOMPARE(m_emulator->startCalls, 1);
        QVERIFY(!enemyMode->isEnabled());
        QFile captured(m_capturedPrepare);
        QVERIFY(captured.open(QIODevice::ReadOnly));
        const QJsonObject capturedRequest =
            QJsonDocument::fromJson(captured.readAll()).object();
        QCOMPARE(capturedRequest.value(QStringLiteral("params")).toObject()
                     .value(QStringLiteral("reuse_existing")).toBool(), false);
        const QJsonObject enemy =
            capturedRequest.value(QStringLiteral("params")).toObject()
                .value(QStringLiteral("enemizer")).toObject();
        QCOMPARE(enemy.value(QStringLiteral("enabled")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("seed")).toString(),
                 QStringLiteral("fixture-enemy-seed"));
        QCOMPARE(enemy.value(QStringLiteral("allow_tier_mixing")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("preserve_locomotion")).toBool(), false);
        QCOMPARE(enemy.value(QStringLiteral("normalize_scaling")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("boss_canary")).toBool(), false);
        QCOMPARE(enemy.value(QStringLiteral("release_contracts")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("release_spawns")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("release_chara")).toBool(), true);
        QCOMPARE(enemy.value(QStringLiteral("boss_pool")).toString(), QStringLiteral("reviewed"));
        QFile prepareAttempts(m_capturedPrepares);
        QVERIFY(prepareAttempts.open(QIODevice::ReadOnly));
        const QList<QByteArray> attempts = prepareAttempts.readAll().trimmed().split('\n');
        QCOMPARE(attempts.size(), 2);
        const auto preparedBossPool = [](const QByteArray& attempt) {
            return QJsonDocument::fromJson(attempt).object()
                .value(QStringLiteral("params")).toObject()
                .value(QStringLiteral("enemizer")).toObject()
                .value(QStringLiteral("boss_pool")).toString();
        };
        QCOMPARE(preparedBossPool(attempts[0]), QStringLiteral("good"));
        QCOMPARE(preparedBossPool(attempts[1]), QStringLiteral("reviewed"));
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
        QVERIFY(enemyMode->isEnabled());
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
        auto* randomize = page.findChild<QPushButton*>(QStringLiteral("apRandomize"));
        seedEdit->setText(seedPath);
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        player->setCurrentText(QStringLiteral("Alice"));
        QTest::mouseClick(randomize, Qt::LeftButton);
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

    void armedIpcStartsOnlyTheOwnedGameOnce() {
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
        QVERIFY(!m_emulator->Check(QStringLiteral("start-game"), &error));
        QVERIFY2(m_coordinator->StartGame(&error), qPrintable(error));
        QVERIFY(!m_emulator->Check(QStringLiteral("start"), &error));
        QVERIFY(!m_emulator->Check(QStringLiteral("restart"), &error));
        m_emulator->running = false;
        QVERIFY(!m_emulator->Check(QStringLiteral("start-game"), &error));
        m_emulator->running = true;
        QVERIFY2(m_emulator->Check(QStringLiteral("start-game"), &error), qPrintable(error));
        QVERIFY(!m_emulator->Check(QStringLiteral("start-game"), &error));
        QCOMPARE(m_emulator->startCalls, 1);
    }

    void standaloneIpcStartsOnlyTheOwnedGameOnce() {
        const StandalonePlayRequest request{m_gameRoot, QStringLiteral("ipc-seed"), true, true};
        ApCoordinator::Prepared prepared;
        QString error;
        QVERIFY2(m_coordinator->Configure(m_gameRoot,
                    QCoreApplication::applicationDirPath() + QStringLiteral("/ap_backend"),
                    ApBackend::DefaultStateRoot(), &error), qPrintable(error));
        QVERIFY2(m_coordinator->PrepareStandalone(request, &prepared, &error),
                 qPrintable(error));
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY2(m_coordinator->StartGame(&error), qPrintable(error));
        QVERIFY(m_coordinator->IsPlaying());
        QVERIFY(!m_emulator->Check(QStringLiteral("start"), &error));
        QVERIFY(!m_emulator->Check(QStringLiteral("restart"), &error));
        QVERIFY2(m_emulator->Check(QStringLiteral("start-game"), &error), qPrintable(error));
        QVERIFY(!m_emulator->Check(QStringLiteral("start-game"), &error));
        QCOMPARE(m_emulator->startCalls, 1);
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

    void preparedPackageCollisionRerandomizesWithoutLaunching() {
        const QString seedPath = m_scratch->path() + QStringLiteral("/collision.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        qputenv("BB_AP_TEST_COLLIDE_ENEMY_SEED", "collision-seed");

        ApPage page(m_coordinator.get());
        page.show();
        auto* apSeed = page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"));
        auto* enemySeed = page.findChild<QLineEdit*>(QStringLiteral("apEnemySeed"));
        auto* launch = page.findChild<QPushButton*>(QStringLiteral("apPlay"));
        QVERIFY(apSeed && enemySeed && launch);
        apSeed->setText(seedPath);
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        page.findChild<QComboBox*>(QStringLiteral("apPlayerChoice"))
            ->setCurrentText(QStringLiteral("Bob"));
        enemySeed->setText(QStringLiteral("collision-seed"));

        QTimer answer;
        answer.setInterval(10);
        connect(&answer, &QTimer::timeout, &page, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            auto* button = box->findChild<QPushButton*>(QStringLiteral("rerandomizeEnemiesButton"));
            if (!button) return;
            answer.stop();
            QTest::mouseClick(button, Qt::LeftButton);
        });
        answer.start();
        QTest::mouseClick(launch, Qt::LeftButton);
        QVERIFY(!answer.isActive());
        QVERIFY(m_coordinator->HasSession());
        QVERIFY(!m_coordinator->GameStarted());
        QCOMPARE(m_emulator->startCalls, 0);

        QFile attempts(m_capturedPrepares);
        QVERIFY(attempts.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = attempts.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QJsonObject first = QJsonDocument::fromJson(lines[0]).object()
                                .value(QStringLiteral("params")).toObject();
        QJsonObject second = QJsonDocument::fromJson(lines[1]).object()
                                 .value(QStringLiteral("params")).toObject();
        QJsonObject firstEnemy = first.value(QStringLiteral("enemizer")).toObject();
        QJsonObject secondEnemy = second.value(QStringLiteral("enemizer")).toObject();
        QCOMPARE(firstEnemy.value(QStringLiteral("seed")).toString(), QStringLiteral("collision-seed"));
        const QString freshSeed = secondEnemy.value(QStringLiteral("seed")).toString();
        QVERIFY(!freshSeed.isEmpty() && freshSeed != QStringLiteral("collision-seed"));
        QCOMPARE(enemySeed->text(), freshSeed);
        firstEnemy.remove(QStringLiteral("seed"));
        secondEnemy.remove(QStringLiteral("seed"));
        QVERIFY(firstEnemy == secondEnemy);
        QCOMPARE(first.value(QStringLiteral("reuse_existing")).toBool(), true);
        QCOMPARE(second.value(QStringLiteral("reuse_existing")).toBool(), false);
        first.insert(QStringLiteral("enemizer"), firstEnemy);
        second.insert(QStringLiteral("enemizer"), secondEnemy);
        first.remove(QStringLiteral("reuse_existing"));
        second.remove(QStringLiteral("reuse_existing"));
        QVERIFY(first == second); // Same AP world, player, server, and options.
        QFile settings(ApBackend::DefaultStateRoot() + QStringLiteral("/ui-settings.json"));
        QVERIFY(settings.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(settings.readAll()).object()
                     .value(QStringLiteral("enemy_seed")).toString(), freshSeed);
        QFile ops(m_opLog);
        QVERIFY(ops.open(QIODevice::ReadOnly));
        const QByteArray log = ops.readAll();
        QVERIFY(!log.contains("verify_and_arm\n"));
        QVERIFY(!log.contains("connect_and_start_client\n"));
    }

    void preparedPackageCollisionCancelLeavesSeedUntouched() {
        const QString seedPath = m_scratch->path() + QStringLiteral("/collision.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        qputenv("BB_AP_TEST_COLLIDE_ENEMY_SEED", "collision-seed");

        ApPage page(m_coordinator.get());
        page.show();
        page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"))->setText(seedPath);
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        auto* enemySeed = page.findChild<QLineEdit*>(QStringLiteral("apEnemySeed"));
        enemySeed->setText(QStringLiteral("collision-seed"));
        QTimer answer;
        answer.setInterval(10);
        connect(&answer, &QTimer::timeout, &page, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            answer.stop();
            QTest::mouseClick(box->button(QMessageBox::Cancel), Qt::LeftButton);
        });
        answer.start();
        QTest::mouseClick(page.findChild<QPushButton*>(QStringLiteral("apRandomize")),
                          Qt::LeftButton);
        QVERIFY(!answer.isActive());
        QCOMPARE(enemySeed->text(), QStringLiteral("collision-seed"));
        QCOMPARE(m_coordinator->lastErrorCode(), QStringLiteral("package-exists"));
        QVERIFY(!m_coordinator->HasSession());
        QCOMPARE(m_emulator->startCalls, 0);
        QFile attempts(m_capturedPrepares);
        QVERIFY(attempts.open(QIODevice::ReadOnly));
        QCOMPARE(attempts.readAll().trimmed().split('\n').size(), 1);
    }

    void legacyMigrationFailureStopsBeforeActivation() {
        qputenv("BB_AP_TEST_MIGRATION_FAIL", "1");
        const QString seedPath = m_scratch->path() + QStringLiteral("/legacy.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        QString error;
        QVERIFY2(m_coordinator->Configure(m_gameRoot,
                    QCoreApplication::applicationDirPath() + QStringLiteral("/ap_backend"),
                    ApBackend::DefaultStateRoot(), &error), qPrintable(error));
        ApPlayRequest request{m_gameRoot, seedPath, QStringLiteral("Alice"), {}, {}};
        ApCoordinator::Prepared prepared;
        QVERIFY2(m_coordinator->Prepare(request, &prepared, &error), qPrintable(error));
        QVERIFY(!m_coordinator->Activate(prepared, false, nullptr, &error));
        QCOMPARE(m_coordinator->lastErrorCode(), QStringLiteral("legacy-owner-conflict"));
        QVERIFY(error.contains(QStringLiteral("previous Archipelago setup")));
        QVERIFY(!QDir(QDir::currentPath() +
                      QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QCOMPARE(m_emulator->startCalls, 0);
        QFile ops(m_opLog);
        QVERIFY(ops.open(QIODevice::ReadOnly));
        const QByteArray log = ops.readAll();
        QVERIFY(log.contains("migrate_legacy_overlay\n"));
        QVERIFY(!log.contains("verify_and_arm\n"));
    }

    void legacyMigrationDeactivatesSameManagedPackageFirst() {
        const QString active = QDir::currentPath() +
            QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture");
        const QString marker = m_scratch->path() +
            QStringLiteral("/CUSA03173-mods/.bb-ap-owner.json");
        qputenv("BB_AP_TEST_LEGACY_MARKER", marker.toLocal8Bit());
        qputenv("BB_AP_TEST_REQUIRE_DEACTIVATED", active.toLocal8Bit());
        const QString seedPath = m_scratch->path() + QStringLiteral("/legacy.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        QString error;
        QVERIFY2(m_coordinator->Configure(m_gameRoot,
                    QCoreApplication::applicationDirPath() + QStringLiteral("/ap_backend"),
                    ApBackend::DefaultStateRoot(), &error), qPrintable(error));
        ApPlayRequest request{m_gameRoot, seedPath, QStringLiteral("Alice"), {}, {}};
        ApCoordinator::Prepared prepared;
        QVERIFY2(m_coordinator->Prepare(request, &prepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY(QDir(active).exists());
        QVERIFY(QDir().mkpath(QFileInfo(marker).absolutePath()));
        QFile owner(marker);
        QVERIFY(owner.open(QIODevice::WriteOnly));
        owner.write("fixture-legacy-owner");
        owner.close();
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY(!QFile::exists(marker));
        QVERIFY(QDir(active).exists());
        QCOMPARE(m_emulator->startCalls, 0);
    }

    void failedNewActivationAfterLegacyMigrationExplainsRecovery() {
        const QString marker = m_scratch->path() +
            QStringLiteral("/CUSA03173-mods/.bb-ap-owner.json");
        qputenv("BB_AP_TEST_LEGACY_MARKER", marker.toLocal8Bit());
        const QString seedPath = m_scratch->path() + QStringLiteral("/legacy.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        QString error;
        QVERIFY2(m_coordinator->Configure(m_gameRoot,
                    QCoreApplication::applicationDirPath() + QStringLiteral("/ap_backend"),
                    ApBackend::DefaultStateRoot(), &error), qPrintable(error));
        ApPlayRequest request{m_gameRoot, seedPath, QStringLiteral("Alice"), {}, {}};
        ApCoordinator::Prepared prepared;
        QVERIFY2(m_coordinator->Prepare(request, &prepared, &error), qPrintable(error));
        QVERIFY(QDir().mkpath(QFileInfo(marker).absolutePath()));
        QFile owner(marker);
        QVERIFY(owner.open(QIODevice::WriteOnly));
        owner.write("fixture-legacy-owner");
        owner.close();
        prepared.packageName = QStringLiteral("Archipelago-Missing");
        QVERIFY(!m_coordinator->Activate(prepared, false, nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("backup was kept")));
        QVERIFY(error.contains(QStringLiteral("new mod was not activated")));
        QVERIFY(!QFile::exists(marker));
        QCOMPARE(m_emulator->startCalls, 0);
    }

    void standalonePrepareVerifyLaunchWithoutClient() {
        ApPage page(m_coordinator.get());
        page.show();
        auto* mode = page.findChild<QComboBox*>(QStringLiteral("playMode"));
        auto* seed = page.findChild<QLineEdit*>(QStringLiteral("standaloneSeed"));
        auto* dlc = page.findChild<QCheckBox*>(QStringLiteral("includeDlc"));
        auto* enemy = page.findChild<QComboBox*>(QStringLiteral("enemyMode"));
        auto* bossPool = page.findChild<QComboBox*>(QStringLiteral("apBossPool"));
        auto* scaling = page.findChild<QCheckBox*>(QStringLiteral("apEnemyNormalizeStats"));
        auto* randomize = page.findChild<QPushButton*>(QStringLiteral("apRandomize"));
        auto* launch = page.findChild<QPushButton*>(QStringLiteral("apPlay"));
        QVERIFY(mode && seed && dlc && enemy && bossPool && scaling && randomize && launch);
        mode->setCurrentIndex(1);
        QVERIFY(!bossPool->isVisible());
        QVERIFY(!page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"))->isVisible());
        QVERIFY(seed->isVisible());
        QVERIFY(enemy->model()->index(1, 0).flags().testFlag(Qt::ItemIsEnabled));
        enemy->setCurrentIndex(1);
        QVERIFY(page.findChild<QLabel*>(QStringLiteral("standaloneBossNote"))->isVisible());
        QVERIFY(scaling->isVisible() && scaling->isEnabled());
        QVERIFY(scaling->isChecked());
        QTest::mouseClick(scaling, Qt::LeftButton, Qt::NoModifier,
                          QPoint(10, scaling->height() / 2));
        QVERIFY(!scaling->isChecked());
        seed->setText(QStringLiteral("standalone-test-seed"));
        dlc->setChecked(false);
        QTest::mouseClick(randomize, Qt::LeftButton);
        QVERIFY(m_coordinator->HasSession());
        QVERIFY(!m_coordinator->GameStarted());
        QCOMPARE(m_emulator->startCalls, 0);
        QVERIFY(launch->isEnabled());
        QFile captured(m_capturedPrepare);
        QVERIFY(captured.open(QIODevice::ReadOnly));
        const QJsonObject request = QJsonDocument::fromJson(captured.readAll()).object();
        QCOMPARE(request.value(QStringLiteral("op")).toString(), QStringLiteral("prepare_standalone"));
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("seed")).toString(), QStringLiteral("standalone-test-seed"));
        QCOMPARE(params.value(QStringLiteral("include_dlc")).toBool(), false);
        QCOMPARE(params.value(QStringLiteral("randomize_enemies")).toBool(), true);
        QCOMPARE(params.value(QStringLiteral("expanded_coverage")).toBool(), true);
        QCOMPARE(params.value(QStringLiteral("normalize_scaling")).toBool(), false);
        QVERIFY(!params.contains(QStringLiteral("server")));
        QVERIFY(!params.contains(QStringLiteral("player_name")));
        QVERIFY(!params.contains(QStringLiteral("boss_pool")));
        QTest::mouseClick(launch, Qt::LeftButton);
        QVERIFY(m_coordinator->GameStarted());
        QCOMPARE(m_emulator->startCalls, 1);
        QFile capturedVerify(m_capturedVerify);
        QVERIFY(capturedVerify.open(QIODevice::ReadOnly));
        const QJsonObject verifyParams = QJsonDocument::fromJson(capturedVerify.readAll())
                                             .object().value(QStringLiteral("params")).toObject();
        QCOMPARE(verifyParams.value(QStringLiteral("expanded_coverage")).toBool(), true);
        QCOMPARE(verifyParams.value(QStringLiteral("normalize_scaling")).toBool(), false);
        QFile ops(m_opLog);
        QVERIFY(ops.open(QIODevice::ReadOnly));
        const QByteArray log = ops.readAll();
        QVERIFY(log.contains("verify_standalone\n"));
        QVERIFY(!log.contains("connect_and_start_client"));
        QVERIFY(!log.contains("verify_and_arm"));
        QVERIFY(QFile::exists(m_gameRoot +
                              QStringLiteral("-mods/dvdroot_ps4/map/standalone.bin")));
    }

    void standaloneReceiptChangeBlocksActivation() {
        ApPage page(m_coordinator.get());
        page.show();
        page.findChild<QComboBox*>(QStringLiteral("playMode"))->setCurrentIndex(1);
        page.findChild<QLineEdit*>(QStringLiteral("standaloneSeed"))
            ->setText(QStringLiteral("tamper-test"));
        QTest::mouseClick(page.findChild<QPushButton*>(QStringLiteral("apRandomize")), Qt::LeftButton);
        const QString receipt = QDir::currentPath() +
            QStringLiteral("/BBLauncher/Mods/Bloodborne-Standalone-Fixture/receipt.json");
        QFile changed(receipt);
        QVERIFY(changed.open(QIODevice::WriteOnly | QIODevice::Truncate));
        changed.write("tampered");
        changed.close();
        QTest::mouseClick(page.findChild<QPushButton*>(QStringLiteral("apPlay")), Qt::LeftButton);
        QCOMPARE(m_emulator->startCalls, 0);
        QVERIFY(!m_coordinator->GameStarted());
        QVERIFY(!QFile::exists(m_gameRoot +
                              QStringLiteral("-mods/dvdroot_ps4/map/standalone.bin")));
    }

    void modeTransitionsRemoveOnlyManagedPackage() {
        const QString other = QDir::currentPath() +
            QStringLiteral("/BBLauncher/Mods/Unrelated/dvdroot_ps4/map");
        QVERIFY(QDir().mkpath(other));
        QFile otherPayload(other + QStringLiteral("/unrelated.bin"));
        QVERIFY(otherPayload.open(QIODevice::WriteOnly));
        otherPayload.write("keep-me");
        otherPayload.close();
        const QString backendDir = QCoreApplication::applicationDirPath() +
                                   QStringLiteral("/ap_backend");
        QString error;
        QVERIFY2(m_coordinator->Configure(m_gameRoot, backendDir,
                  ApBackend::DefaultStateRoot(), &error), qPrintable(error));
        modservice::Plan otherPlan;
        QVERIFY(m_coordinator->modService()->PlanActivate("Unrelated", otherPlan).ok);
        QVERIFY(m_coordinator->modService()->Commit(otherPlan).ok);
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        ApPlayRequest ap{m_gameRoot, seedPath, QStringLiteral("Alice"), {}, {}};
        ApCoordinator::Prepared apPrepared;
        QVERIFY2(m_coordinator->Prepare(ap, &apPrepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Activate(apPrepared, false, nullptr, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Arm(apPrepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->StartGame(&error), qPrintable(error));
        QVERIFY2(m_coordinator->Connect(&error), qPrintable(error));
        m_emulator->running = false;
        QVERIFY2(m_coordinator->GameClosed(&error), qPrintable(error));
        StandalonePlayRequest standalone{m_gameRoot, QStringLiteral("transition-seed"), true, true};
        ApCoordinator::Prepared standalonePrepared;
        QVERIFY2(m_coordinator->PrepareStandalone(standalone, &standalonePrepared, &error),
                 qPrintable(error));
        QVERIFY2(m_coordinator->Activate(standalonePrepared, false, nullptr, &error),
                 qPrintable(error));
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QVERIFY(QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Bloodborne-Standalone-Fixture")).exists());
        QVERIFY2(m_coordinator->StartGame(&error), qPrintable(error));
        m_emulator->running = false;
        QVERIFY2(m_coordinator->GameClosed(&error), qPrintable(error));
        QVERIFY2(m_coordinator->Prepare(ap, &apPrepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Activate(apPrepared, false, nullptr, &error), qPrintable(error));
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Bloodborne-Standalone-Fixture")).exists());
        QVERIFY(QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QVERIFY2(m_coordinator->SwitchToRegularPlay(&error), qPrintable(error));
        QVERIFY(QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Unrelated")).exists());
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
    }

    void restartRestoresExactOwnedPackageForBothModes() {
        const QString backendDir = QCoreApplication::applicationDirPath() +
                                   QStringLiteral("/ap_backend");
        const QString stateRoot = ApBackend::DefaultStateRoot();
        QString error;
        auto configure = [&] {
            return m_coordinator->Configure(m_gameRoot, backendDir, stateRoot, &error);
        };
        QVERIFY2(configure(), qPrintable(error));
        const QString unrelatedPath = QDir::currentPath() +
            QStringLiteral("/BBLauncher/Mods/Unrelated/dvdroot_ps4/map");
        QVERIFY(QDir().mkpath(unrelatedPath));
        QFile unrelated(unrelatedPath + QStringLiteral("/keep.bin"));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        unrelated.write("keep");
        unrelated.close();
        modservice::Plan other;
        QVERIFY(m_coordinator->modService()->PlanActivate("Unrelated", other).ok);
        QVERIFY(m_coordinator->modService()->Commit(other).ok);
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        ApPlayRequest ap{m_gameRoot, seedPath, QStringLiteral("Alice"), {}, {}};
        ApCoordinator::Prepared prepared;
        QVERIFY2(m_coordinator->Prepare(ap, &prepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        const QString ownership = stateRoot +
                                  QStringLiteral("/integrated/cxx-owned-active.json");
        QVERIFY(QFile::exists(ownership));

        m_coordinator.reset();
        m_coordinator = std::make_unique<ApCoordinator>(m_emulator.get());
        QVERIFY2(m_coordinator->Configure(m_gameRoot, backendDir, stateRoot,
                                          &error, {}, false), qPrintable(error));
        QVERIFY(m_coordinator->backend() == nullptr);
        QVERIFY(!m_coordinator->Preflight(QStringLiteral("start-game")).isEmpty());
        qputenv("BB_AP_TEST_REQUIRE_DEACTIVATED",
                (QDir::currentPath() + QStringLiteral(
                    "/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).toLocal8Bit());
        StandalonePlayRequest standalone{m_gameRoot, QStringLiteral("new-mode"), true, true};
        QVERIFY2(m_coordinator->PrepareStandalone(standalone, &prepared, &error),
                 qPrintable(error));
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
        QVERIFY(QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Bloodborne-Standalone-Fixture")).exists());

        m_coordinator.reset();
        m_coordinator = std::make_unique<ApCoordinator>(m_emulator.get());
        QVERIFY2(configure(), qPrintable(error));
        qputenv("BB_AP_TEST_REQUIRE_DEACTIVATED",
                (QDir::currentPath() + QStringLiteral(
                    "/BBLauncher/Mods-Active (DO NOT DELETE)/Bloodborne-Standalone-Fixture")).toLocal8Bit());
        QVERIFY2(m_coordinator->Prepare(ap, &prepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Bloodborne-Standalone-Fixture")).exists());

        m_coordinator.reset();
        m_coordinator = std::make_unique<ApCoordinator>(m_emulator.get());
        QVERIFY2(configure(), qPrintable(error));
        QVERIFY2(m_coordinator->SwitchToRegularPlay(&error), qPrintable(error));
        QVERIFY(!QFile::exists(ownership));
        QVERIFY(QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Unrelated")).exists());
        QVERIFY(!QDir(QDir::currentPath() + QStringLiteral(
            "/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture")).exists());
    }

    void restartPreparesAndLaunchesTheSameSeedWithOnlyItsManagedPackageRemoved() {
        const QString backendDir = QCoreApplication::applicationDirPath() +
                                   QStringLiteral("/ap_backend");
        const QString stateRoot = ApBackend::DefaultStateRoot();
        QString error;
        QVERIFY2(m_coordinator->Configure(m_gameRoot, backendDir, stateRoot, &error),
                 qPrintable(error));
        const QString unrelatedPath = QDir::currentPath() +
            QStringLiteral("/BBLauncher/Mods/Unrelated/dvdroot_ps4/map");
        QVERIFY(QDir().mkpath(unrelatedPath));
        QFile unrelated(unrelatedPath + QStringLiteral("/keep.bin"));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        unrelated.write("keep");
        unrelated.close();
        modservice::Plan other;
        QVERIFY(m_coordinator->modService()->PlanActivate("Unrelated", other).ok);
        QVERIFY(m_coordinator->modService()->Commit(other).ok);
        const QString unrelatedActive = QDir::currentPath() +
            QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Unrelated");
        const QString managedActive = QDir::currentPath() +
            QStringLiteral("/BBLauncher/Mods-Active (DO NOT DELETE)/Archipelago-Fixture");
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        ApPlayRequest request{m_gameRoot, seedPath, QStringLiteral("Alice"), {}, {}};
        ApCoordinator::Prepared prepared;
        QVERIFY2(m_coordinator->Prepare(request, &prepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY(QDir(managedActive).exists());

        m_coordinator.reset();
        m_coordinator = std::make_unique<ApCoordinator>(m_emulator.get());
        QVERIFY2(m_coordinator->Configure(m_gameRoot, backendDir, stateRoot,
                                          &error, {}, false), qPrintable(error));
        m_emulator->running = true;
        QVERIFY(!m_coordinator->Prepare(request, &prepared, &error, true));
        QVERIFY(QDir(managedActive).exists());
        QVERIFY(QDir(unrelatedActive).exists());
        m_emulator->running = false;
        qputenv("BB_AP_TEST_REQUIRE_DEACTIVATED", managedActive.toLocal8Bit());
        QVERIFY2(m_coordinator->Prepare(request, &prepared, &error, true), qPrintable(error));
        QVERIFY(!QDir(managedActive).exists());
        QVERIFY(QDir(unrelatedActive).exists());
        QFile captured(m_capturedPrepare);
        QVERIFY(captured.open(QIODevice::ReadOnly));
        const QJsonObject params = QJsonDocument::fromJson(captured.readAll()).object()
                                       .value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("reuse_existing")).toBool(), true);
        QVERIFY2(m_coordinator->Activate(prepared, false, nullptr, &error), qPrintable(error));
        QVERIFY2(m_coordinator->Arm(prepared, &error), qPrintable(error));
        QVERIFY2(m_coordinator->StartGame(&error), qPrintable(error));
        QVERIFY2(m_coordinator->Connect(&error), qPrintable(error));
        QCOMPARE(m_emulator->startCalls, 1);
        QVERIFY(QDir(managedActive).exists());
        QVERIFY(QDir(unrelatedActive).exists());
    }

    void launchRequestsExistingPackageReuse() {
        const QString seedPath = m_scratch->path() + QStringLiteral("/fixture.bbseed.json");
        QFile seed(seedPath);
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("fixture");
        seed.close();
        ApPage page(m_coordinator.get());
        page.show();
        page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"))->setText(seedPath);
        QVERIFY(QMetaObject::invokeMethod(&page, "SeedChanged"));
        page.findChild<QComboBox*>(QStringLiteral("apPlayerChoice"))
            ->setCurrentText(QStringLiteral("Alice"));
        QTest::mouseClick(page.findChild<QPushButton*>(QStringLiteral("apPlay")), Qt::LeftButton);
        QCOMPARE(m_emulator->startCalls, 1);
        QFile captured(m_capturedPrepare);
        QVERIFY(captured.open(QIODevice::ReadOnly));
        const QJsonObject params = QJsonDocument::fromJson(captured.readAll()).object()
                                       .value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("reuse_existing")).toBool(), true);
    }

    void failedStartupRecoveryBlocksRegularGameStart() {
        const QString stateRoot = ApBackend::DefaultStateRoot();
        const QString journalDir = stateRoot +
            QStringLiteral("/integrated/cxx-journal");
        QVERIFY(QDir().mkpath(journalDir));
        QFile journal(journalDir + QStringLiteral("/modservice.jsonl"));
        QVERIFY(journal.open(QIODevice::WriteOnly));
        journal.write("{\"kind\":\"plan\"\n");
        journal.close();
        QString error;
        QVERIFY(!m_coordinator->Configure(
            m_gameRoot, QCoreApplication::applicationDirPath() +
                QStringLiteral("/ap_backend"), stateRoot, &error, {}, false));
        QVERIFY(error.contains(QStringLiteral("malformed")));
        QString refused;
        QVERIFY(!m_emulator->Check(QStringLiteral("start-game"), &refused));
        QVERIFY(refused.contains(QStringLiteral("recovery")));
        QVERIFY(m_coordinator->backend() == nullptr);
    }

    void savedChoicesRestoreWithoutAuthorityOrPassword() {
        const QString settingsPath = ApBackend::DefaultStateRoot() +
                                     QStringLiteral("/ui-settings.json");
        QVERIFY(QDir().mkpath(QFileInfo(settingsPath).absolutePath()));
        QFile legacy(settingsPath);
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(R"({"normalize_scaling":false,"preserve_locomotion":true,"shuffle_bosses":false})");
        legacy.close();
        {
            ApPage page(m_coordinator.get());
            auto* mode = page.findChild<QComboBox*>(QStringLiteral("playMode"));
            auto* enemy = page.findChild<QComboBox*>(QStringLiteral("enemyMode"));
            auto* bossPool = page.findChild<QComboBox*>(QStringLiteral("apBossPool"));
            auto* scaling = page.findChild<QCheckBox*>(QStringLiteral("apEnemyNormalizeStats"));
            QVERIFY(scaling->isChecked()); // Old default-off setting is not carried forward.
            QCOMPARE(bossPool->currentData().toString(), QStringLiteral("reviewed"));
            auto* apSeed = page.findChild<QLineEdit*>(QStringLiteral("apSeedPath"));
            apSeed->setText(QStringLiteral("C:/fixture/ap-seed.zip"));
            bossPool->setCurrentIndex(1);
            scaling->setChecked(false);
            enemy->setCurrentIndex(2);
            mode->setCurrentIndex(1);
            enemy->setCurrentIndex(1);
            QVERIFY(scaling->isChecked());
            page.findChild<QLineEdit*>(QStringLiteral("standaloneSeed"))
                ->setText(QStringLiteral("remembered-seed"));
            page.findChild<QCheckBox*>(QStringLiteral("includeDlc"))->setChecked(false);
            for (QLineEdit* edit : page.findChildren<QLineEdit*>()) {
                if (edit->echoMode() == QLineEdit::Password) {
                    edit->setText(QStringLiteral("do-not-save-this"));
                }
            }
        }
        QFile settings(settingsPath);
        QVERIFY(settings.open(QIODevice::ReadOnly));
        const QByteArray saved = settings.readAll();
        QVERIFY(!saved.contains("do-not-save-this"));
        QVERIFY(!saved.contains("preserve_locomotion"));
        QVERIFY(!saved.contains("shuffle_bosses"));
        QVERIFY(!saved.contains("\"normalize_scaling\""));
        QCOMPARE(QJsonDocument::fromJson(saved).object()
                     .value(QStringLiteral("ap_boss_pool")).toString(), QStringLiteral("good"));
        ApPage restored(m_coordinator.get());
        auto* mode = restored.findChild<QComboBox*>(QStringLiteral("playMode"));
        auto* enemy = restored.findChild<QComboBox*>(QStringLiteral("enemyMode"));
        auto* bossPool = restored.findChild<QComboBox*>(QStringLiteral("apBossPool"));
        auto* scaling = restored.findChild<QCheckBox*>(QStringLiteral("apEnemyNormalizeStats"));
        auto* launch = restored.findChild<QPushButton*>(QStringLiteral("apPlay"));
        QCOMPARE(mode->currentIndex(), 1);
        QCOMPARE(enemy->currentIndex(), 1);
        QVERIFY(scaling->isChecked());
        QCOMPARE(restored.findChild<QLineEdit*>(QStringLiteral("standaloneSeed"))->text(),
                 QStringLiteral("remembered-seed"));
        QVERIFY(!restored.findChild<QCheckBox*>(QStringLiteral("includeDlc"))->isChecked());
        QVERIFY(launch->isEnabled()); // Can rebuild, but has no prepared authority.
        mode->setCurrentIndex(0);
        QCOMPARE(enemy->currentIndex(), 2);
        QCOMPARE(bossPool->currentData().toString(), QStringLiteral("good"));
        QVERIFY(!bossPool->isVisible());
        QVERIFY(!scaling->isChecked());
        QCOMPARE(restored.findChild<QLineEdit*>(QStringLiteral("apSeedPath"))->text(),
                 QStringLiteral("C:/fixture/ap-seed.zip"));
    }

    void launchBuildsWhenInputsChanged() {
        ApPage page(m_coordinator.get());
        page.show();
        page.findChild<QComboBox*>(QStringLiteral("playMode"))->setCurrentIndex(1);
        auto* seed = page.findChild<QLineEdit*>(QStringLiteral("standaloneSeed"));
        seed->setText(QStringLiteral("first-seed"));
        auto* randomize = page.findChild<QPushButton*>(QStringLiteral("apRandomize"));
        auto* launch = page.findChild<QPushButton*>(QStringLiteral("apPlay"));
        QTest::mouseClick(randomize, Qt::LeftButton);
        QCOMPARE(m_emulator->startCalls, 0);
        seed->setText(QStringLiteral("second-seed"));
        QVERIFY(launch->isEnabled());
        QTest::mouseClick(launch, Qt::LeftButton);
        QCOMPARE(m_emulator->startCalls, 1);
        QFile captured(m_capturedPrepare);
        QVERIFY(captured.open(QIODevice::ReadOnly));
        const QJsonObject params = QJsonDocument::fromJson(captured.readAll()).object()
                                       .value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("seed")).toString(),
                 QStringLiteral("second-seed"));
        QFile ops(m_opLog);
        QVERIFY(ops.open(QIODevice::ReadOnly));
        const QByteArray log = ops.readAll();
        QVERIFY(log.indexOf("prepare_standalone") < log.lastIndexOf("prepare_standalone"));
        QVERIFY(log.lastIndexOf("prepare_standalone") < log.lastIndexOf("verify_standalone"));
    }

    void renderForms() {
        const QString output = QCoreApplication::applicationDirPath();
        const int fontId = QFontDatabase::addApplicationFont(
            QStringLiteral("C:/Windows/Fonts/segoeui.ttf"));
        if (fontId >= 0) QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
        // Match Config::SetTheme("Dark") without reading the user's config.
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(50, 50, 50));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(20, 20, 20));
        palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::black);
        QApplication::setPalette(palette);
        ApPage page(m_coordinator.get());
        page.resize(820, 620);
        page.show();
        QApplication::processEvents();
        QVERIFY(page.grab().save(output + QStringLiteral("/ap-default.png")));
        page.resize(page.minimumSize());
        QApplication::processEvents();
        QVERIFY(page.grab().save(output + QStringLiteral("/ap-minimum.png")));
        page.findChild<QComboBox*>(QStringLiteral("playMode"))->setCurrentIndex(1);
        page.resize(820, 620);
        QApplication::processEvents();
        QVERIFY(page.grab().save(output + QStringLiteral("/standalone-default.png")));
        page.resize(page.minimumSize());
        QApplication::processEvents();
        QVERIFY(page.grab().save(output + QStringLiteral("/standalone-minimum.png")));
    }

  private:
    QString m_previousCwd;
    QString m_capturedPrepare;
    QString m_capturedPrepares;
    QString m_capturedVerify;
    QString m_opLog;
    std::unique_ptr<QTemporaryDir> m_scratch;
    QString m_gameRoot;
    std::unique_ptr<FakeEmulatorService> m_emulator;
    std::unique_ptr<ApCoordinator> m_coordinator;
};

QTEST_MAIN(ApUiTest)
#include "apui_test.moc"
