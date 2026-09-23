// SPDX-FileCopyrightText: Copyright 2026 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QCoreApplication>
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
        if (op == QStringLiteral("inspect_seed")) {
            bool ok = false;
            const int delay = qEnvironmentVariableIntValue("BB_AP_TEST_DELAY_INSPECT_MS", &ok);
            if (ok && delay > 0) QThread::msleep(static_cast<unsigned long>(delay));
        }
        QJsonObject result;
        if (op == QStringLiteral("capabilities")) {
            result = {{"protocol", "bb-ap-integration-v1"},
                      {"activation_route", "copy"},
                      {"operations", QJsonArray{"capabilities", "inspect_seed", "prepare_play",
                                                   "verify_and_arm", "connect_and_start_client",
                                                   "session_status", "stop_client", "cancel_operation"}}};
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
                                            {"ok", true},
                                            {"result", result}})
                      .toJson(QJsonDocument::Compact)
               << Qt::endl;
    }
    return 0;
}
