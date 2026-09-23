// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ApBackend integration test: drives the REAL Python AP backend
// (bb_launcher integrated-backend) over the versioned JSON-lines
// protocol. No game, no emulator, no GUI widgets required.
//
// Environment:
//   BB_AP_SOURCE_ROOT  AP repository checkout (backend source)
//   BB_AP_STATE_ROOT   writable scratch dir (defaults to a temp dir)
//   BB_AP_PYTHON       python executable (default: PATH discovery)
//
// Build with -DBB_AP_BUILD_TESTS=ON and run the produced binary.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

#include "../modules/ApBackend.h"

static int g_failures = 0;

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                         \
            std::cout << "FAIL " << __LINE__ << ": " #cond "\n";                                \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (0)

static void WriteJson(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cout << "FAIL: cannot write fixture " << path.toStdString() << "\n";
        ++g_failures;
        return;
    }
    file.write(QJsonDocument(object).toJson());
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const char* sourceRoot = std::getenv("BB_AP_SOURCE_ROOT");
    if (sourceRoot == nullptr || *sourceRoot == '\0') {
        std::cout << "SKIP: BB_AP_SOURCE_ROOT is not set\n";
        return 2;
    }

    QTemporaryDir state;
    if (!state.isValid()) {
        std::cout << "FAIL: no temp dir\n";
        return 1;
    }
    qputenv("BB_AP_STATE_ROOT", state.path().toLocal8Bit());

    ApBackend backend;
    QString error;
    CHECK(backend.Start(state.path(), &error));
    if (!backend.IsRunning()) {
        std::cout << "backend start failed: " << error.toStdString() << "\n";
        return 1;
    }

    // Capabilities handshake terms.
    const QJsonObject caps = backend.capabilities();
    CHECK(caps.value("protocol").toString() == QStringLiteral("bb-ap-integration-v1"));
    CHECK(caps.value("activation_route").toString() == QStringLiteral("copy"));
    bool hasPrepare = false;
    for (const QJsonValue& op : caps.value("operations").toArray()) {
        if (op.toString() == QStringLiteral("prepare_play")) {
            hasPrepare = true;
        }
    }
    CHECK(hasPrepare);

    // No installation: named prerequisite, not a crash.
    {
        ApResponse response = backend.Call(
            QStringLiteral("inspect_install"),
            QJsonObject{{QStringLiteral("candidates"), QJsonArray{QStringLiteral("Z:/nope")}}},
            15000);
        CHECK(!response.ok);
        CHECK(response.error.code == QStringLiteral("missing-prerequisite"));
    }

    // Single-slot seed needs no player question.
    const QString singleSeed = state.path() + "/single.bbseed.json";
    WriteJson(singleSeed, QJsonObject{{QStringLiteral("format"), QStringLiteral("bb-seed-request-v1")},
                                      {QStringLiteral("seed"), QStringLiteral("Evening hunt")},
                                      {QStringLiteral("player"), 1},
                                      {QStringLiteral("player_name"), QStringLiteral("Alaric")},
                                      {QStringLiteral("runtime_build"), QStringLiteral("r")},
                                      {QStringLiteral("world_version"), QStringLiteral("w")},
                                      {QStringLiteral("server"), QStringLiteral("archipelago.gg:9")}});
    {
        ApResponse response =
            backend.Call(QStringLiteral("inspect_seed"),
                         QJsonObject{{QStringLiteral("seed_path"), singleSeed}}, 15000);
        CHECK(response.ok);
        CHECK(!response.result.value("needs_choice").toBool());
        CHECK(response.result.value("selected").toString() == QStringLiteral("Alaric"));
    }

    // Multi-slot seed without a choice fails closed with a named choice.
    const QString multiSeed = state.path() + "/multi.bbseed.json";
    WriteJson(multiSeed, QJsonObject{{QStringLiteral("format"), QStringLiteral("bb-seed-request-v1")},
                                     {QStringLiteral("seed"), QStringLiteral("Hunt")},
                                     {QStringLiteral("player"), 1},
                                     {QStringLiteral("player_name"), QStringLiteral("A")},
                                     {QStringLiteral("runtime_build"), QStringLiteral("r")},
                                     {QStringLiteral("world_version"), QStringLiteral("w")},
                                     {QStringLiteral("slots"), QJsonArray{"A", "B"}}});
    {
        ApResponse response =
            backend.Call(QStringLiteral("inspect_seed"),
                         QJsonObject{{QStringLiteral("seed_path"), multiSeed}}, 15000);
        CHECK(!response.ok);
        CHECK(response.error.code == QStringLiteral("ambiguous-player"));
        ApResponse chosen = backend.Call(
            QStringLiteral("inspect_seed"),
            QJsonObject{{QStringLiteral("seed_path"), multiSeed},
                        {QStringLiteral("player_name"), QStringLiteral("B")}},
            15000);
        CHECK(chosen.ok);
        CHECK(chosen.result.value("selected").toString() == QStringLiteral("B"));
    }

    // Unknown play handles report idle (fail closed, never a session)
    // and never leak receipt paths.
    {
        ApResponse response = backend.Call(
            QStringLiteral("session_status"),
            QJsonObject{{QStringLiteral("play_id"),
                         QStringLiteral("play_00000000000000000000000000000000")}},
            15000);
        CHECK(response.ok);
        CHECK(response.result.value("state").toString() == QStringLiteral("idle"));
        const QString blob = QString::fromUtf8(QJsonDocument(response.result).toJson());
        CHECK(!blob.contains(QStringLiteral("receipts")));
    }

    // Cancellation lands at a safe boundary.
    {
        ApResponse response = backend.Call(
            QStringLiteral("cancel_operation"),
            QJsonObject{{QStringLiteral("target_id"), QStringLiteral("q-test")}}, 15000);
        CHECK(response.ok);
    }

    backend.Stop();
    if (g_failures == 0) {
        std::cout << "apbackend integration tests passed\n";
        return 0;
    }
    std::cout << g_failures << " apbackend test failure(s)\n";
    return 1;
}
