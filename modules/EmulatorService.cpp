// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EmulatorService.h"

#include <QCryptographicHash>
#include <QFile>
#include <QProcess>

#include "modules/ipc/ipc_client.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

EmulatorService::EmulatorService(IpcClient* ipc, QObject* parent)
    : QObject(parent), m_ipc(ipc) {}

QString EmulatorService::Sha256OfFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool EmulatorService::CreationTimeOf(qint64 pid, quint64* creationTime) {
#ifdef Q_OS_WIN
    HANDLE handle =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }
    FILETIME created, exited, kernelTime, userTime;
    const bool ok =
        GetProcessTimes(handle, &created, &exited, &kernelTime, &userTime) != FALSE;
    if (ok && creationTime != nullptr) {
        ULARGE_INTEGER value;
        value.LowPart = created.dwLowDateTime;
        value.HighPart = created.dwHighDateTime;
        *creationTime = value.QuadPart;
    }
    CloseHandle(handle);
    return ok;
#else
    (void)pid;
    (void)creationTime;
    return false;
#endif
}

bool EmulatorService::Check(const QString& action, QString* error) {
    if (m_preflight) {
        const QString refusal = m_preflight(action);
        if (!refusal.isEmpty()) {
            if (error != nullptr) {
                *error = refusal;
            }
            return false;
        }
    }
    return true;
}

bool EmulatorService::Start(const QFileInfo& exe, const QStringList& args,
                            const QString& workDir, EmulatorProcessIdentity* identity,
                            QString* error) {
    if (!Check(QStringLiteral("start"), error)) {
        return false;
    }
    m_ipc->startEmulator(exe, args, workDir);
    QProcess* process = m_ipc->emulatorProcess();
    if (process == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("emulator process did not start");
        }
        return false;
    }
    // QProcess::processId() is 0 until the process actually spawns; wait
    // briefly rather than recording a bogus identity.
    if (process->processId() == 0 && !process->waitForStarted(15000)) {
        if (error != nullptr) {
            *error = QStringLiteral("emulator process did not start: ") +
                     process->errorString();
        }
        return false;
    }
    if (identity != nullptr) {
        identity->executable = exe.absoluteFilePath();
        identity->executableSha256 = Sha256OfFile(exe.absoluteFilePath());
        identity->pid = static_cast<qint64>(process->processId());
        quint64 birth = 0;
        identity->hasCreationTime = CreationTimeOf(identity->pid, &birth);
        identity->creationTime = birth;
        identity->valid = identity->pid > 0 && !identity->executableSha256.isEmpty();
        if (!identity->valid && error != nullptr) {
            *error = QStringLiteral("could not establish the emulator process identity");
        }
    }
    return identity == nullptr || identity->valid;
}

bool EmulatorService::Restart(const QFileInfo& exe, const QStringList& args,
                              const QString& workDir, EmulatorProcessIdentity* identity,
                              QString* error) {
    if (!Check(QStringLiteral("restart"), error)) {
        return false;
    }
    return Start(exe, args, workDir, identity, error);
}
