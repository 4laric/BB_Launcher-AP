// SPDX-FileCopyrightText: Copyright 2026 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTextStream input(stdin);
    QTextStream output(stdout);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        const QJsonObject request = QJsonDocument::fromJson(line.toUtf8()).object();
        const QString op = request.value("op").toString();
        const QJsonObject params = request.value("params").toObject();
        const QString opLog = qEnvironmentVariable("BB_AP_TEST_OP_LOG");
        if (!opLog.isEmpty()) {
            QFile log(opLog);
            if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
                log.write(op.toUtf8() + '\n');
            }
        }
        if (op == QStringLiteral("inspect_seed")) {
            bool ok = false;
            const int delay = qEnvironmentVariableIntValue("BB_AP_TEST_DELAY_INSPECT_MS", &ok);
            if (ok && delay > 0) QThread::msleep(static_cast<unsigned long>(delay));
        }
        QJsonObject result;
        bool succeeded = true;
        QJsonObject error;
        if (op == QStringLiteral("capabilities")) {
            result = {{"protocol", "bb-ap-integration-v1"},
                      {"activation_route", "copy"},
                      {"operations", QJsonArray{"capabilities", "inspect_seed", "prepare_play",
                                                   "verify_and_arm", "connect_and_start_client",
                                                   "session_status", "stop_client", "cancel_operation",
                                                   "prepare_standalone", "verify_standalone",
                                                   "migrate_legacy_overlay"}}};
        } else if (op == QStringLiteral("inspect_seed")) {
            const QString player = params.value("player_name").toString();
            result = {{"seed", "Fixture seed"},
                      {"server", "localhost:38281"},
                      {"slots", QJsonArray{"Alice", "Bob"}},
                      {"selected", player},
                      {"needs_choice", player.isEmpty()}};
        } else if (op == QStringLiteral("prepare_play")) {
            const QString attemptsPath = qEnvironmentVariable("BB_AP_TEST_CAPTURE_PREPARES");
            if (!attemptsPath.isEmpty()) {
                QFile attempts(attemptsPath);
                if (attempts.open(QIODevice::WriteOnly | QIODevice::Append)) {
                    attempts.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
                }
            }
            const QString capturePath = qEnvironmentVariable("BB_AP_TEST_CAPTURE_REQUEST");
            if (!capturePath.isEmpty()) {
                QFile capture(capturePath);
                if (capture.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    capture.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
                }
            }
            const QJsonObject enemizer = params.value("enemizer").toObject();
            const QString collidedSeed = qEnvironmentVariable("BB_AP_TEST_COLLIDE_ENEMY_SEED");
            const QString activePath = qEnvironmentVariable("BB_AP_TEST_REQUIRE_DEACTIVATED");
            if (!activePath.isEmpty() && QDir(activePath).exists()) {
                succeeded = false;
                error = {{"code", "active-package"},
                         {"detail", "Managed package is still active during export."}};
            } else if (!collidedSeed.isEmpty() &&
                enemizer.value("seed").toString() == collidedSeed) {
                succeeded = false;
                error = {{"code", "package-exists"},
                         {"detail", "A prepared mod for this seed already exists."}};
            } else {
                result = {{"play_id", "fixture-play"},
                      {"package_name", "Archipelago-Fixture"},
                      {"reused", false},
                      {"enemizer", QJsonObject{{"enabled", enemizer.value("enabled")},
                                                {"seed", enemizer.value("seed")},
                                                {"swap_count", 117},
                                                {"map_file_count", 214},
                                                {"ai_file_count", 28}}},
                      {"display", QJsonObject{{"seed", "Fixture seed"},
                                               {"slot", params.value("player_name")},
                                               {"server", params.value("server")},
                                               {"title", "Fixture seed — " + params.value("player_name").toString()}}}};
            }
        } else if (op == QStringLiteral("verify_and_arm")) {
            result = {{"arm_id", "fixture-arm"}};
        } else if (op == QStringLiteral("migrate_legacy_overlay")) {
            if (qEnvironmentVariableIsSet("BB_AP_TEST_MIGRATION_FAIL")) {
                succeeded = false;
                error = {{"code", "legacy-owner-conflict"},
                         {"detail", "A previous Archipelago setup could not be safely restored."}};
            } else {
                const QString activePath = qEnvironmentVariable("BB_AP_TEST_REQUIRE_DEACTIVATED");
                if (!activePath.isEmpty() && QDir(activePath).exists()) {
                    succeeded = false;
                    error = {{"code", "legacy-owner-conflict"},
                             {"detail", "Current managed package was not deactivated first."}};
                } else {
                    const QString marker = qEnvironmentVariable("BB_AP_TEST_LEGACY_MARKER");
                    const bool present = !marker.isEmpty() && QFile::exists(marker);
                    if (present) QFile::remove(marker); // Simulated backend migration.
                    const QString status = qEnvironmentVariable("BB_AP_TEST_MIGRATION_STATUS");
                    result = {{"status", status.isEmpty()
                        ? (present ? QStringLiteral("migrated") : QStringLiteral("no_legacy"))
                        : status}};
                }
            }
        } else if (op == QStringLiteral("prepare_standalone")) {
            const QString activePath = qEnvironmentVariable("BB_AP_TEST_REQUIRE_DEACTIVATED");
            if (!activePath.isEmpty() && QDir(activePath).exists()) {
                succeeded = false;
                error = {{"code", "active-package"},
                         {"detail", "Managed package is still active during export."}};
            } else {
                const QString packageName = QStringLiteral("Bloodborne-Standalone-Fixture");
                const QString packagePath = QDir(params.value("mods_root").toString())
                                                .filePath(packageName);
                QDir().mkpath(packagePath + QStringLiteral("/dvdroot_ps4/map"));
                QFile payload(packagePath + QStringLiteral("/dvdroot_ps4/map/standalone.bin"));
                if (payload.open(QIODevice::WriteOnly)) payload.write("standalone-map");
                const QString receiptPath = packagePath + QStringLiteral("/receipt.json");
                QFile receipt(receiptPath);
                if (receipt.open(QIODevice::WriteOnly)) {
                    const QByteArray value = QByteArray(params.value("expanded_coverage").toBool()
                        ? "fixture-receipt-expanded" : "fixture-receipt-reviewed")
                        + (params.value("normalize_scaling").toBool() ? "-scaled" : "-unscaled");
                    receipt.write(value);
                }
                const QString capturePath = qEnvironmentVariable("BB_AP_TEST_CAPTURE_REQUEST");
                if (!capturePath.isEmpty()) {
                    QFile capture(capturePath);
                    if (capture.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        capture.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
                    }
                }
                result = {{"package_name", packageName}, {"package_path", packagePath},
                          {"receipt_path", receiptPath}, {"receipt_id", "fixture-standalone"},
                          {"seed", params.value("seed")}, {"display_name", "Standalone fixture"}};
            }
        } else if (op == QStringLiteral("verify_standalone")) {
            const QString capturePath = qEnvironmentVariable("BB_AP_TEST_CAPTURE_VERIFY");
            if (!capturePath.isEmpty()) {
                QFile capture(capturePath);
                if (capture.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    capture.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
                }
            }
            QFile receipt(params.value("receipt_path").toString());
            const QByteArray expected = QByteArray(params.value("expanded_coverage").toBool()
                ? QByteArray("fixture-receipt-expanded")
                : QByteArray("fixture-receipt-reviewed"))
                + (params.value("normalize_scaling").toBool() ? "-scaled" : "-unscaled");
            if (!receipt.open(QIODevice::ReadOnly) ||
                receipt.readAll() != expected) {
                succeeded = false;
                error = {{"code", "receipt-mismatch"},
                         {"detail", "Standalone package receipt changed"}};
            } else {
                result = {{"package_name", params.value("package_name")},
                          {"receipt_id", params.value("receipt_id")}};
            }
        } else if (op == QStringLiteral("connect_and_start_client")) {
            result = {{"session_id", "fixture-session"}};
        } else if (op == QStringLiteral("session_status")) {
            result = {{"state", "playing"}};
        } else if (op == QStringLiteral("stop_client")) {
            result = {{"stopped", true}};
        } else if (op == QStringLiteral("cancel_operation")) {
            result = {{"cancelled", request.value("id")}};
        }
        output << QJsonDocument(QJsonObject{{"protocol", "bb-ap-integration-v1"},
                                            {"id", request.value("id")},
                                            {"seq", request.value("seq")},
                                            {"ok", succeeded},
                                            {"result", result},
                                            {"error", error}})
                      .toJson(QJsonDocument::Compact)
               << Qt::endl;
    }
    return 0;
}
