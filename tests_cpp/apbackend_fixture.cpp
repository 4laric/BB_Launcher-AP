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
                                                   "prepare_standalone", "verify_standalone"}}};
        } else if (op == QStringLiteral("inspect_seed")) {
            const QString player = params.value("player_name").toString();
            result = {{"seed", "Fixture seed"},
                      {"server", "localhost:38281"},
                      {"slots", QJsonArray{"Alice", "Bob"}},
                      {"selected", player},
                      {"needs_choice", player.isEmpty()}};
        } else if (op == QStringLiteral("prepare_play")) {
            const QString capturePath = qEnvironmentVariable("BB_AP_TEST_CAPTURE_REQUEST");
            if (!capturePath.isEmpty()) {
                QFile capture(capturePath);
                if (capture.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    capture.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
                }
            }
            const QJsonObject enemizer = params.value("enemizer").toObject();
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
        } else if (op == QStringLiteral("verify_and_arm")) {
            result = {{"arm_id", "fixture-arm"}};
        } else if (op == QStringLiteral("prepare_standalone")) {
            const QString packageName = QStringLiteral("Bloodborne-Standalone-Fixture");
            const QString packagePath = QDir(params.value("mods_root").toString())
                                            .filePath(packageName);
            QDir().mkpath(packagePath + QStringLiteral("/dvdroot_ps4/map"));
            QFile payload(packagePath + QStringLiteral("/dvdroot_ps4/map/standalone.bin"));
            if (payload.open(QIODevice::WriteOnly)) payload.write("standalone-map");
            const QString receiptPath = packagePath + QStringLiteral("/receipt.json");
            QFile receipt(receiptPath);
            if (receipt.open(QIODevice::WriteOnly)) receipt.write("fixture-receipt");
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
        } else if (op == QStringLiteral("verify_standalone")) {
            QFile receipt(params.value("receipt_path").toString());
            if (!receipt.open(QIODevice::ReadOnly) ||
                receipt.readAll() != QByteArray("fixture-receipt")) {
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
