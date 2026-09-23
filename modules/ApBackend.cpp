// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ApBackend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

#include "ApFork.h"

namespace {
constexpr const char* kProtocol = "bb-ap-integration-v1";

QString FindPython() {
    const char* env = std::getenv("BB_AP_PYTHON");
    if (env != nullptr && *env != '\0') {
        return QString::fromLocal8Bit(env);
    }
    for (const char* name : {"python3", "python"}) {
        const QString found =
            QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!found.isEmpty()) {
            return found;
        }
    }
    return {};
}
} // namespace

ApBackend::ApBackend(QObject* parent) : QObject(parent) {}

ApBackend::~ApBackend() {
    Stop();
}

QString ApBackend::FindBackend(QString* method) {
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString frozen = appDir + QStringLiteral("/ap_backend/bb-ap-backend.exe");
#else
    const QString frozen = appDir + QStringLiteral("/ap_backend/bb-ap-backend");
#endif
    if (QFileInfo::exists(frozen)) {
        if (method != nullptr) {
            *method = QStringLiteral("frozen");
        }
        return frozen;
    }
    const char* override = std::getenv("BB_AP_BACKEND");
    if (override != nullptr && *override != '\0' &&
        QFileInfo::exists(QString::fromLocal8Bit(override))) {
        if (method != nullptr) {
            *method = QStringLiteral("env");
        }
        return QString::fromLocal8Bit(override);
    }
    const char* sourceRoot = std::getenv("BB_AP_SOURCE_ROOT");
    if (sourceRoot != nullptr && *sourceRoot != '\0') {
        const QString python = FindPython();
        if (!python.isEmpty()) {
            if (method != nullptr) {
                *method = QStringLiteral("source");
            }
            return python;
        }
    }
    return {};
}

QString ApBackend::DefaultStateRoot() {
    const char* env = std::getenv("BB_AP_STATE_ROOT");
    if (env != nullptr && *env != '\0') {
        return QString::fromLocal8Bit(env);
    }
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/ap-state");
}

bool ApBackend::Start(const QString& stateRoot, QString* error) {
    Stop();
    QString method;
    const QString backend = FindBackend(&method);
    if (backend.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral(
                "Archipelago backend is not installed. Reinstall %1 or set BB_AP_BACKEND.")
                           .arg(ApFork::AppName());
        }
        return false;
    }
    m_stateRoot = stateRoot;
    QDir().mkpath(m_stateRoot);

    QString program;
    QStringList args;
    if (method == QStringLiteral("source")) {
        program = backend;
        args << QStringLiteral("-m") << QStringLiteral("bb_launcher")
             << QStringLiteral("integrated-backend") << QStringLiteral("--state-root")
             << m_stateRoot;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PYTHONPATH"),
                   QString::fromLocal8Bit(std::getenv("BB_AP_SOURCE_ROOT")));
        m_process = new QProcess(this);
        m_process->setProcessEnvironment(env);
        m_process->setProgram(program);
        m_process->setArguments(args);
    } else {
        m_process = new QProcess(this);
        m_process->setProgram(backend);
        m_process->setArguments({QStringLiteral("--state-root"), m_stateRoot});
    }
    // No shell, argument array only. Stderr carries logs; stdout is
    // protocol-only, so only stdout is parsed.
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();
    if (!m_process->waitForStarted(15000)) {
        if (error != nullptr) {
            *error = QStringLiteral("could not start the Archipelago backend: ") +
                     m_process->errorString();
        }
        Stop();
        return false;
    }
    ApResponse caps = Call(QStringLiteral("capabilities"), {}, 15000);
    if (!caps.ok) {
        if (error != nullptr) {
            *error = QStringLiteral("Archipelago backend refused the handshake: ") +
                     caps.error.detail;
        }
        Stop();
        return false;
    }
    const QString protocol =
        caps.result.value(QStringLiteral("protocol")).toString();
    if (protocol != QString::fromLatin1(kProtocol)) {
        if (error != nullptr) {
            *error = QStringLiteral("Archipelago backend protocol mismatch: ") + protocol;
        }
        Stop();
        return false;
    }
    m_capabilities = caps.result;
    return true;
}

void ApBackend::Stop() {
    if (m_process != nullptr) {
        m_process->disconnect();
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(3000)) {
                m_process->kill();
                m_process->waitForFinished(3000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_buffer.clear();
    m_capabilities = {};
}

bool ApBackend::IsRunning() const {
    return m_process != nullptr && m_process->state() != QProcess::NotRunning;
}

void ApBackend::RequestCancel() {
    m_cancelRequested = true;
}

bool ApBackend::EnsureLine(const QString& id, int timeoutMs, QByteArray* line,
                           QString* error) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        // Pump events so the UI repaints and Cancel stays live during
        // long backend operations such as seed preparation.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
        if (m_cancelRequested) {
            if (error != nullptr) {
                *error = QStringLiteral("cancelled");
            }
            return false;
        }
        int newline = m_buffer.indexOf('\n');
        if (newline >= 0) {
            *line = m_buffer.left(newline).trimmed();
            m_buffer.remove(0, newline + 1);
            if (line->isEmpty()) {
                continue;
            }
            return true;
        }
        if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
            if (error != nullptr) {
                *error = QStringLiteral("Archipelago backend exited unexpectedly");
            }
            return false;
        }
        m_process->waitForReadyRead(100);
        const QByteArray chunk = m_process->readAllStandardOutput();
        if (!chunk.isEmpty()) {
            m_buffer += chunk;
        }
    }
    if (error != nullptr) {
        *error = QStringLiteral("Archipelago backend did not answer in time");
    }
    return false;
}

ApResponse ApBackend::Call(const QString& op, const QJsonObject& params, int timeoutMs) {
    ApResponse response;
    if (!IsRunning()) {
        response.error.code = QStringLiteral("backend-unreachable");
        response.error.detail = QStringLiteral("Archipelago backend is not running");
        return response;
    }
    m_cancelRequested = false;
    response.id = QStringLiteral("q%1").arg(++m_seq);
    QJsonObject request{
        {QStringLiteral("protocol"), QString::fromLatin1(kProtocol)},
        {QStringLiteral("op"), op},
        {QStringLiteral("id"), response.id},
        {QStringLiteral("seq"), static_cast<qint64>(m_seq)},
        {QStringLiteral("params"), params},
    };
    const QByteArray wire =
        QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n";
    m_process->write(wire);
    if (!m_process->waitForBytesWritten(10000)) {
        response.error.code = QStringLiteral("backend-unreachable");
        response.error.detail = QStringLiteral("could not send the request to the backend");
        return response;
    }
    // Drain earlier lines until the matching id arrives; the backend
    // answers in order, so a mismatch means a desync worth failing on.
    for (;;) {
        QByteArray line;
        QString failure;
        if (!EnsureLine(response.id, timeoutMs, &line, &failure)) {
            response.error.code = m_cancelRequested ? QStringLiteral("cancelled")
                                                    : QStringLiteral("timeout");
            response.error.detail = failure;
            return response;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            response.error.code = QStringLiteral("protocol-error");
            response.error.detail = QStringLiteral("backend sent a non-JSON line");
            return response;
        }
        const QJsonObject envelope = doc.object();
        if (envelope.value(QStringLiteral("id")).toString() != response.id) {
            response.error.code = QStringLiteral("protocol-error");
            response.error.detail =
                QStringLiteral("backend response id does not match the request");
            return response;
        }
        response.ok = envelope.value(QStringLiteral("ok")).toBool();
        if (response.ok) {
            response.result = envelope.value(QStringLiteral("result")).toObject();
        } else {
            const QJsonObject err = envelope.value(QStringLiteral("error")).toObject();
            response.error.code = err.value(QStringLiteral("code")).toString();
            response.error.detail = err.value(QStringLiteral("detail")).toString();
            response.error.retryable = err.value(QStringLiteral("retryable")).toBool();
            for (const QJsonValue& item :
                 err.value(QStringLiteral("recovery")).toArray()) {
                response.error.recovery.push_back(item.toString());
            }
        }
        return response;
    }
}
