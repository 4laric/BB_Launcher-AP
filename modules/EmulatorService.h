// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later
//
// EmulatorService: the single funnel for starting the emulator, extracted
// from BBLauncher so regular play, Archipelago play, Restart, IPC and
// no-GUI/shortcut startup share one launch path with one preflight gate.
//
// The service wraps the existing IpcClient spawn (no protocol change)
// and additionally returns the actual spawned process identity:
// executable path, executable SHA-256, PID and OS process creation time.
// A PID alone is never identity: reattachment and the AP backend compare
// the full tuple. An optional preflight handler (installed by the AP
// coordinator while a session is armed/active) can refuse any startup
// action with a human-readable reason; with no handler every action is
// allowed, preserving legacy behavior exactly.

#pragma once

#include <functional>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QStringList>

class IpcClient;

struct EmulatorProcessIdentity {
    QString executable;
    QString executableSha256;
    qint64 pid = -1;
    quint64 creationTime = 0;
    bool hasCreationTime = false;
    bool valid = false;
};

class EmulatorService : public QObject {
    Q_OBJECT

  public:
    // Refuse a startup action by returning a non-empty reason; return
    // empty to allow. Actions: "start", "restart", "start-game".
    using PreflightHandler = std::function<QString(const QString& action)>;

    explicit EmulatorService(IpcClient* ipc, QObject* parent = nullptr);

    void setPreflightHandler(PreflightHandler handler) { m_preflight = std::move(handler); }
    void clearPreflightHandler() { m_preflight = nullptr; }

    bool Start(const QFileInfo& exe, const QStringList& args, const QString& workDir,
               EmulatorProcessIdentity* identity, QString* error);
    bool Restart(const QFileInfo& exe, const QStringList& args, const QString& workDir,
                 EmulatorProcessIdentity* identity, QString* error);
    // Runs the preflight handler without starting anything, so read-only
    // actions (e.g. sending START to an already-running game) share the
    // same gate as every launch path.
    bool Check(const QString& action, QString* error);

    static QString Sha256OfFile(const QString& path);
    // Best-available OS process birth identity; false when unavailable.
    static bool CreationTimeOf(qint64 pid, quint64* creationTime);

  private:
    IpcClient* m_ipc;
    PreflightHandler m_preflight = nullptr;
};
