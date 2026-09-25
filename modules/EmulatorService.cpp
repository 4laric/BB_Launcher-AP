// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EmulatorService.h"

#include <QCryptographicHash>
#include <QFile>
#include <QProcess>

#include "modules/ipc/ipc_client.h"

namespace Config {
extern bool GameRunning;
}

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
struct MainWindowSearch {
    DWORD pid = 0;
    HWND window = nullptr;
};

BOOL CALLBACK FindProcessWindow(HWND window, LPARAM value) {
    auto* search = reinterpret_cast<MainWindowSearch*>(value);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == search->pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HANDLE OpenVerifiedProcess(const EmulatorProcessIdentity& identity, DWORD access,
                           QString* error) {
    if (!identity.valid || identity.pid <= 0 || identity.executable.isEmpty() ||
        identity.executableSha256.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("The game process identity is unavailable; restart it from Archipelago first.");
        }
        return nullptr;
    }
    HANDLE process = OpenProcess(access | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, static_cast<DWORD>(identity.pid));
    if (process == nullptr) {
        // ERROR_INVALID_PARAMETER means the PID no longer exists. That is a
        // completed shutdown; access-denied and all other failures remain
        // actionable because a live process cannot be proven safe to touch.
        if (GetLastError() == ERROR_INVALID_PARAMETER) {
            return INVALID_HANDLE_VALUE;
        }
        if (error != nullptr) {
            *error = QStringLiteral("The Archipelago game process is no longer available.");
        }
        return nullptr;
    }
    wchar_t imagePath[32768]{};
    DWORD imageSize = static_cast<DWORD>(std::size(imagePath));
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool gotPath = QueryFullProcessImageNameW(process, 0, imagePath, &imageSize) != FALSE;
    const bool gotTimes = GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE;
    ULARGE_INTEGER birth{};
    birth.LowPart = created.dwLowDateTime;
    birth.HighPart = created.dwHighDateTime;
    const QString actualPath = gotPath ? QString::fromWCharArray(imagePath, static_cast<int>(imageSize))
                                       : QString();
    const bool matches = gotPath && gotTimes &&
                         QFileInfo(actualPath).canonicalFilePath().compare(
                             QFileInfo(identity.executable).canonicalFilePath(), Qt::CaseInsensitive) == 0 &&
                         (!identity.hasCreationTime || identity.creationTime == birth.QuadPart) &&
                         EmulatorService::Sha256OfFile(actualPath).compare(
                             identity.executableSha256, Qt::CaseInsensitive) == 0;
    if (!matches) {
        CloseHandle(process);
        if (error != nullptr) {
            *error = QStringLiteral("The process no longer matches the game instance started for this session.");
        }
        return nullptr;
    }
    return process;
}
#endif
} // namespace

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

bool EmulatorService::IsEmulatorRunning() const {
    if (Config::GameRunning) {
        return true;
    }
    const QProcess* process = m_ipc == nullptr ? nullptr : m_ipc->emulatorProcess();
    return process != nullptr && process->state() != QProcess::NotRunning;
}

bool EmulatorService::OwnsProcess(const EmulatorProcessIdentity& identity) const {
    const QProcess* process = m_ipc == nullptr ? nullptr : m_ipc->emulatorProcess();
    if (!identity.valid || process == nullptr || process->state() != QProcess::Running ||
        static_cast<qint64>(process->processId()) != identity.pid) {
        return false;
    }
    if (identity.hasCreationTime) {
        quint64 birth = 0;
        return CreationTimeOf(identity.pid, &birth) && birth == identity.creationTime;
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

bool EmulatorService::Focus(const EmulatorProcessIdentity& identity, QString* error) {
#ifdef Q_OS_WIN
    HANDLE process = OpenVerifiedProcess(identity, PROCESS_QUERY_LIMITED_INFORMATION, error);
    if (process == INVALID_HANDLE_VALUE) {
        if (error != nullptr) {
            *error = QStringLiteral("The game has already closed.");
        }
        return false;
    }
    if (process == nullptr) {
        return false;
    }
    CloseHandle(process);
    MainWindowSearch search{static_cast<DWORD>(identity.pid), nullptr};
    EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
    if (search.window == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("The game is running, but its window could not be found.");
        }
        return false;
    }
    if (IsIconic(search.window)) {
        ShowWindow(search.window, SW_RESTORE);
    }
    SetForegroundWindow(search.window);
    return true;
#else
    (void)identity;
    if (error != nullptr) {
        *error = QStringLiteral("Returning to a running game window is unavailable on this platform.");
    }
    return false;
#endif
}

bool EmulatorService::Stop(const EmulatorProcessIdentity& identity, QString* error) {
#ifdef Q_OS_WIN
    HANDLE process = OpenVerifiedProcess(identity, PROCESS_QUERY_LIMITED_INFORMATION, error);
    if (process == INVALID_HANDLE_VALUE) {
        return true;
    }
    if (process == nullptr) {
        return false;
    }
    MainWindowSearch search{static_cast<DWORD>(identity.pid), nullptr};
    EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
    if (search.window != nullptr) {
        PostMessageW(search.window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process, 5000) == WAIT_OBJECT_0) {
            CloseHandle(process);
            return true;
        }
    }
    // A game may be writing a save. Never force-kill it to switch overlays;
    // keep the session and active package intact until the user closes it.
    const bool stopped = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    CloseHandle(process);
    if (!stopped && error != nullptr) {
        *error = QStringLiteral("The game is still open. Close it normally, then try switching again; its active package was left in place.");
    }
    return stopped;
#else
    (void)identity;
    if (error != nullptr) {
        *error = QStringLiteral("Stopping the tracked game process is unavailable on this platform.");
    }
    return false;
#endif
}
