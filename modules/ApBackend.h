// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ApBackend: QProcess-managed client for the bundled AP backend's
// versioned JSON-lines protocol (bb-ap-integration-v1, see
// bb_launcher/integrated in the AP repository).
//
// Contract notes:
// - Stdout is protocol-only; backend logs use stderr/files. Requests and
//   responses carry protocol version, operation id and sequence.
// - The backend is spawned with an argument array, never a shell.
// - Qt passes opaque play/arm handles; receipt paths never appear in
//   protocol traffic and are never constructed here.
// - Passwords travel in request bodies only, are never logged, and are
//   never persisted by this client.
// - The protocol cannot run arbitrary commands: op is drawn from the
//   fixed set below.

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

struct ApError {
    QString code;
    QString detail;
    bool retryable = false;
    QStringList recovery;
};

struct ApResponse {
    bool ok = false;
    QString id;
    QJsonObject result;
    ApError error;
};

class ApBackend : public QObject {
    Q_OBJECT

  public:
    explicit ApBackend(QObject* parent = nullptr);
    ~ApBackend() override;

    // Spawns the backend and negotiates capabilities. Returns false with
    // a human-readable reason when the backend is missing or refuses the
    // protocol/operation set.
    bool Start(const QString& stateRoot, QString* error);

    void Stop();

    // One request/response round trip. While waiting, the call pumps the
    // event loop in short slices so the UI stays responsive and
    // RequestCancel() takes effect; cancellation is acknowledged at the
    // backend's next safe boundary (recovery replays its journal).
    ApResponse Call(const QString& op, const QJsonObject& params, int timeoutMs = 120000);
    void RequestCancel();

    bool IsRunning() const;
    QString stateRoot() const { return m_stateRoot; }
    QJsonObject capabilities() const { return m_capabilities; }

    // Backend discovery order: frozen bundle beside the executable,
    // BB_AP_BACKEND env override, then a source checkout via
    // BB_AP_SOURCE_ROOT + python (development only).
    static QString FindBackend(QString* method = nullptr);
    static QString DefaultStateRoot();

  private:
    bool EnsureLine(const QString& id, int timeoutMs, QByteArray* line, QString* error);
    qint64 m_seq = 0;
    bool m_cancelRequested = false;

    QProcess* m_process = nullptr;
    QString m_stateRoot;
    QJsonObject m_capabilities;
    QByteArray m_buffer;
};
