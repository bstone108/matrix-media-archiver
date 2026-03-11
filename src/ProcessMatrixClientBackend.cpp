#include "ProcessMatrixClientBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTimer>

namespace {
QString backendExecutableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("matrix_media_archiver_backend.exe");
#else
    return QStringLiteral("matrix_media_archiver_backend");
#endif
}

QString timeWindowUnitJsonKey(const TimeWindowUnit unit)
{
    switch (unit) {
    case TimeWindowUnit::Day:
        return QStringLiteral("day");
    case TimeWindowUnit::Week:
        return QStringLiteral("week");
    case TimeWindowUnit::Month:
        return QStringLiteral("month");
    case TimeWindowUnit::None:
        break;
    }
    return QStringLiteral("none");
}

QString failedRetentionUnitJsonKey(const FailedJobRetentionUnit unit)
{
    switch (unit) {
    case FailedJobRetentionUnit::Minute:
        return QStringLiteral("minute");
    case FailedJobRetentionUnit::Hour:
        return QStringLiteral("hour");
    case FailedJobRetentionUnit::Day:
        return QStringLiteral("day");
    case FailedJobRetentionUnit::None:
        break;
    }
    return QStringLiteral("none");
}

ConnectionState parseConnectionState(const QString &value)
{
    if (value == QStringLiteral("starting")) {
        return ConnectionState::Starting;
    }
    if (value == QStringLiteral("running")) {
        return ConnectionState::Running;
    }
    if (value == QStringLiteral("stopping")) {
        return ConnectionState::Stopping;
    }
    if (value == QStringLiteral("error")) {
        return ConnectionState::Error;
    }
    return ConnectionState::Stopped;
}

RoomHistoryMode parseRoomHistoryMode(const QString &value)
{
    if (value == QStringLiteral("initialBackfill")) {
        return RoomHistoryMode::InitialBackfill;
    }
    if (value == QStringLiteral("reconnectCatchUp")) {
        return RoomHistoryMode::ReconnectCatchUp;
    }
    if (value == QStringLiteral("complete")) {
        return RoomHistoryMode::Complete;
    }
    return RoomHistoryMode::Idle;
}

VerificationStatus parseVerificationStatus(const QString &value)
{
    if (value == QStringLiteral("verified")) {
        return VerificationStatus::Verified;
    }
    if (value == QStringLiteral("unverified")) {
        return VerificationStatus::Unverified;
    }
    return VerificationStatus::Unknown;
}

QJsonObject settingsToJson(const AppSettings &settings)
{
    return {
        {QStringLiteral("homeserverUrl"), settings.homeserverUrl},
        {QStringLiteral("username"), settings.username},
        {QStringLiteral("ownerUserId"), settings.ownerUserId},
        {QStringLiteral("destinationRootPath"), settings.destinationRootPath},
        {QStringLiteral("messageLimit"), settings.messageLimit},
        {QStringLiteral("timeWindowValue"), settings.timeWindowValue},
        {QStringLiteral("timeWindowUnit"), timeWindowUnitJsonKey(settings.timeWindowUnit)},
        {QStringLiteral("retryCooldownMinutes"), settings.retryCooldownMinutes},
        {QStringLiteral("retryLimit"), settings.retryLimit},
        {QStringLiteral("downloadWorkerCount"), settings.downloadWorkerCount},
        {QStringLiteral("failedJobRetentionValue"), settings.failedJobRetentionValue},
        {QStringLiteral("failedJobRetentionUnit"), failedRetentionUnitJsonKey(settings.failedJobRetentionUnit)},
        {QStringLiteral("desiredPowerState"), settings.desiredPowerState},
    };
}

BotRuntimeSnapshot parseRuntimeSnapshot(const QJsonObject &object)
{
    BotRuntimeSnapshot snapshot;
    snapshot.connectionState = parseConnectionState(object.value(QStringLiteral("connectionState")).toString());
    snapshot.currentUserId = object.value(QStringLiteral("currentUserId")).toString();
    snapshot.deviceId = object.value(QStringLiteral("deviceId")).toString();
    snapshot.accountMode = object.value(QStringLiteral("accountMode")).toString();

    const QJsonObject verificationObject = object.value(QStringLiteral("verification")).toObject();
    snapshot.verification.state = parseVerificationStatus(verificationObject.value(QStringLiteral("state")).toString());
    snapshot.verification.deviceId = verificationObject.value(QStringLiteral("deviceId")).toString();

    const QJsonArray emojis = verificationObject.value(QStringLiteral("emojis")).toArray();
    for (const QJsonValue &value : emojis) {
        const QJsonObject emojiObject = value.toObject();
        VerificationEmoji emoji;
        emoji.symbol = emojiObject.value(QStringLiteral("symbol")).toString();
        emoji.description = emojiObject.value(QStringLiteral("description")).toString();
        snapshot.verification.emojis.append(emoji);
    }

    const QJsonArray decimals = verificationObject.value(QStringLiteral("decimals")).toArray();
    for (const QJsonValue &value : decimals) {
        snapshot.verification.decimals.append(static_cast<quint16>(value.toInt()));
    }

    const QJsonArray workerStates = object.value(QStringLiteral("workerStates")).toArray();
    for (const QJsonValue &value : workerStates) {
        const QJsonObject workerObject = value.toObject();
        RoomWorkerSnapshot worker;
        worker.roomId = workerObject.value(QStringLiteral("roomId")).toString();
        worker.liveWatcherActive = workerObject.value(QStringLiteral("liveWatcherActive")).toBool();
        worker.historyMode = parseRoomHistoryMode(workerObject.value(QStringLiteral("historyMode")).toString());
        worker.historyDetail = workerObject.value(QStringLiteral("historyDetail")).toString();
        snapshot.workerStates.append(worker);
    }

    const QJsonArray activeDownloads = object.value(QStringLiteral("activeDownloads")).toArray();
    for (const QJsonValue &value : activeDownloads) {
        const QJsonObject downloadObject = value.toObject();
        ActiveDownloadSnapshot download;
        download.workerId = downloadObject.value(QStringLiteral("workerId")).toInt();
        download.jobId = downloadObject.value(QStringLiteral("jobId")).toVariant().toLongLong();
        download.roomId = downloadObject.value(QStringLiteral("roomId")).toString();
        download.filename = downloadObject.value(QStringLiteral("filename")).toString();
        download.receivedBytes = downloadObject.value(QStringLiteral("receivedBytes")).toVariant().toLongLong();
        download.totalBytes = downloadObject.value(QStringLiteral("totalBytes")).toVariant().toLongLong();
        snapshot.activeDownloads.append(download);
    }

    return snapshot;
}

QString processErrorSummary(const QProcess::ProcessError error)
{
    switch (error) {
    case QProcess::FailedToStart:
        return QStringLiteral("The backend process failed to start.");
    case QProcess::Crashed:
        return QStringLiteral("The backend process crashed.");
    case QProcess::Timedout:
        return QStringLiteral("The backend process timed out.");
    case QProcess::WriteError:
        return QStringLiteral("The app could not write to the backend process.");
    case QProcess::ReadError:
        return QStringLiteral("The app could not read from the backend process.");
    case QProcess::UnknownError:
        break;
    }
    return QStringLiteral("The backend process reported an unknown error.");
}
}

ProcessMatrixClientBackend::ProcessMatrixClientBackend(const AppPaths &paths, QObject *parent)
    : QObject(parent)
    , paths_(paths)
{
}

ProcessMatrixClientBackend::~ProcessMatrixClientBackend()
{
    runtimeChangedCallback_ = {};
    backendErrorCallback_ = {};

    if (process_ == nullptr) {
        return;
    }

    QString unusedError;
    explicitShutdown_ = true;
    if (process_->state() == QProcess::Running) {
        const bool sent = sendCommand(QJsonObject {{QStringLiteral("type"), QStringLiteral("shutdown")}}, unusedError, 5000);
        Q_UNUSED(sent);
        process_->waitForFinished(5000);
    }

    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(2000);
    }
}

QString ProcessMatrixClientBackend::backendName() const
{
    return QStringLiteral("rust-sidecar");
}

bool ProcessMatrixClientBackend::isAvailable() const
{
    const QFileInfo info(backendExecutablePath());
    return info.exists() && info.isFile() && info.isExecutable();
}

void ProcessMatrixClientBackend::setRuntimeChangedCallback(RuntimeChangedCallback callback)
{
    runtimeChangedCallback_ = std::move(callback);
}

void ProcessMatrixClientBackend::setBackendErrorCallback(BackendErrorCallback callback)
{
    backendErrorCallback_ = std::move(callback);
}

bool ProcessMatrixClientBackend::start(const AppSettings &settings, const QString &password, BotRuntimeSnapshot &runtime, QString &errorMessage)
{
    if (!ensureProcess(errorMessage)) {
        runtime = latestRuntime_;
        runtime.connectionState = ConnectionState::Error;
        return false;
    }

    latestRuntime_.connectionState = ConnectionState::Starting;
    publishRuntime(latestRuntime_);

    const bool ok = sendCommand(
        QJsonObject {
            {QStringLiteral("type"), QStringLiteral("start")},
            {QStringLiteral("settings"), settingsToJson(settings)},
            {QStringLiteral("password"), password},
        },
        errorMessage,
        180000);
    runtime = latestRuntime_;
    if (!ok && runtime.connectionState == ConnectionState::Stopped) {
        runtime.connectionState = ConnectionState::Error;
        latestRuntime_ = runtime;
    }
    return ok;
}

bool ProcessMatrixClientBackend::stop(BotRuntimeSnapshot &runtime, QString &errorMessage)
{
    if (process_ == nullptr || process_->state() != QProcess::Running) {
        latestRuntime_ = BotRuntimeSnapshot {};
        latestRuntime_.connectionState = ConnectionState::Stopped;
        publishRuntime(latestRuntime_);
        runtime = latestRuntime_;
        return true;
    }

    const bool ok = sendCommand(
        QJsonObject {{QStringLiteral("type"), QStringLiteral("stop")}},
        errorMessage,
        60000);
    if (ok && latestRuntime_.connectionState != ConnectionState::Stopped) {
        BotRuntimeSnapshot stopped = latestRuntime_;
        stopped.connectionState = ConnectionState::Stopped;
        stopped.workerStates.clear();
        stopped.activeDownloads.clear();
        stopped.verification = VerificationSnapshot {};
        publishRuntime(stopped);
    }
    runtime = latestRuntime_;
    return ok;
}

bool ProcessMatrixClientBackend::saveSettings(const AppSettings &settings, const QString &password, QString &errorMessage)
{
    if (process_ == nullptr || process_->state() != QProcess::Running) {
        if (!settings.desiredPowerState) {
            return true;
        }

        BotRuntimeSnapshot runtime = latestRuntime_;
        return start(settings, password, runtime, errorMessage);
    }

    return sendCommand(
        QJsonObject {
            {QStringLiteral("type"), QStringLiteral("saveSettings")},
            {QStringLiteral("settings"), settingsToJson(settings)},
            {QStringLiteral("password"), password},
        },
        errorMessage,
        180000);
}

bool ProcessMatrixClientBackend::resetHistoryScans(QString &errorMessage)
{
    return commandRequiringRunningProcess(
        QJsonObject {{QStringLiteral("type"), QStringLiteral("resetHistoryScans")}},
        errorMessage,
        120000);
}

bool ProcessMatrixClientBackend::joinRoom(const QString &roomIdOrAlias, QString &errorMessage)
{
    return commandRequiringRunningProcess(
        QJsonObject {
            {QStringLiteral("type"), QStringLiteral("joinRoom")},
            {QStringLiteral("roomIdOrAlias"), roomIdOrAlias},
        },
        errorMessage,
        60000);
}

bool ProcessMatrixClientBackend::leaveRoom(const QString &roomId, QString &errorMessage)
{
    return commandRequiringRunningProcess(
        QJsonObject {
            {QStringLiteral("type"), QStringLiteral("leaveRoom")},
            {QStringLiteral("roomId"), roomId},
        },
        errorMessage,
        60000);
}

bool ProcessMatrixClientBackend::requestVerification(QString &errorMessage)
{
    return commandRequiringRunningProcess(
        QJsonObject {{QStringLiteral("type"), QStringLiteral("requestVerification")}},
        errorMessage,
        60000);
}

bool ProcessMatrixClientBackend::startSasVerification(QString &errorMessage)
{
    return commandRequiringRunningProcess(
        QJsonObject {{QStringLiteral("type"), QStringLiteral("startSasVerification")}},
        errorMessage,
        60000);
}

bool ProcessMatrixClientBackend::approveVerification(QString &errorMessage)
{
    return commandRequiringRunningProcess(
        QJsonObject {{QStringLiteral("type"), QStringLiteral("approveVerification")}},
        errorMessage,
        60000);
}

bool ProcessMatrixClientBackend::declineVerification(QString &errorMessage)
{
    return commandRequiringRunningProcess(
        QJsonObject {{QStringLiteral("type"), QStringLiteral("declineVerification")}},
        errorMessage,
        60000);
}

QString ProcessMatrixClientBackend::backendExecutablePath() const
{
    const QString executableName = backendExecutableName();
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        QDir::cleanPath(appDir.filePath(executableName)),
        QDir::cleanPath(appDir.filePath(QStringLiteral("../Resources/%1").arg(executableName))),
        QDir::cleanPath(appDir.filePath(QStringLiteral("../backend/target/debug/%1").arg(executableName))),
        QDir::cleanPath(appDir.filePath(QStringLiteral("../backend/target/release/%1").arg(executableName))),
        QDir::cleanPath(appDir.filePath(QStringLiteral("../../../../backend/target/debug/%1").arg(executableName))),
        QDir::cleanPath(appDir.filePath(QStringLiteral("../../../../backend/target/release/%1").arg(executableName))),
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }

    return candidates.first();
}

QString ProcessMatrixClientBackend::stderrSummary() const
{
    QString summary = QString::fromLocal8Bit(stderrBuffer_).trimmed();
    if (summary.size() > 600) {
        summary = summary.right(600);
    }

    if (!lastProcessError_.isEmpty() && !summary.contains(lastProcessError_)) {
        if (!summary.isEmpty()) {
            summary.append(QStringLiteral("\n"));
        }
        summary.append(lastProcessError_);
    }

    return summary.trimmed();
}

bool ProcessMatrixClientBackend::ensureProcess(QString &errorMessage)
{
    if (process_ != nullptr && process_->state() == QProcess::Running) {
        return true;
    }
    return launchProcess(errorMessage);
}

bool ProcessMatrixClientBackend::launchProcess(QString &errorMessage)
{
    errorMessage.clear();
    const QString executablePath = backendExecutablePath();
    const QFileInfo executableInfo(executablePath);
    if (!executableInfo.exists()) {
        errorMessage = QStringLiteral("Matrix backend executable was not found at %1.").arg(executablePath);
        return false;
    }
    if (!executableInfo.isExecutable()) {
        errorMessage = QStringLiteral("Matrix backend executable is not runnable: %1").arg(executablePath);
        return false;
    }
    if (executableInfo.isSymLink()) {
        errorMessage = QStringLiteral("Matrix backend executable must not be a symbolic link: %1").arg(executablePath);
        return false;
    }

    stdoutBuffer_.clear();
    stderrBuffer_.clear();
    completedResponses_.clear();
    lastProcessError_.clear();
    explicitShutdown_ = false;

    process_ = std::make_unique<QProcess>(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_.get(), &QProcess::readyReadStandardOutput, this, &ProcessMatrixClientBackend::handleReadyReadStandardOutput);
    connect(process_.get(), &QProcess::readyReadStandardError, this, &ProcessMatrixClientBackend::handleReadyReadStandardError);
    connect(process_.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &ProcessMatrixClientBackend::handleProcessFinished);
    connect(process_.get(), &QProcess::errorOccurred, this, &ProcessMatrixClientBackend::handleProcessErrorOccurred);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.remove(QStringLiteral("LD_PRELOAD"));
    environment.remove(QStringLiteral("DYLD_INSERT_LIBRARIES"));
    environment.remove(QStringLiteral("DYLD_FORCE_FLAT_NAMESPACE"));
    process_->setProcessEnvironment(environment);

    process_->start(executableInfo.canonicalFilePath(), {QStringLiteral("--root-path"), paths_.rootPath()});
    if (!process_->waitForStarted(10000)) {
        errorMessage = QStringLiteral("Failed to launch the Matrix backend: %1").arg(process_->errorString());
        const QString stderrText = stderrSummary();
        if (!stderrText.isEmpty()) {
            errorMessage.append(QStringLiteral("\n%1").arg(stderrText));
        }
        process_.reset();
        return false;
    }

    return true;
}

bool ProcessMatrixClientBackend::sendCommand(const QJsonObject &command, QString &errorMessage, const int timeoutMs)
{
    errorMessage.clear();
    if (process_ == nullptr || process_->state() != QProcess::Running) {
        errorMessage = QStringLiteral("Matrix backend is not running.");
        return false;
    }

    const quint64 id = nextCommandId_++;
    const QJsonObject envelope {
        {QStringLiteral("id"), static_cast<qint64>(id)},
        {QStringLiteral("command"), command},
    };

    QByteArray payload = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    payload.append('\n');

    if (process_->write(payload) != payload.size()) {
        errorMessage = QStringLiteral("Failed to send a command to the Matrix backend.");
        return false;
    }
    if (!process_->waitForBytesWritten(5000)) {
        errorMessage = QStringLiteral("Timed out while writing to the Matrix backend.");
        return false;
    }

    QEventLoop loop;
    QTimer responsePollTimer;
    responsePollTimer.setInterval(10);
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;

    const QMetaObject::Connection responseConnection = connect(
        &responsePollTimer,
        &QTimer::timeout,
        &loop,
        [this, id, &loop]() {
            if (completedResponses_.contains(id)
                || process_ == nullptr
                || process_->state() != QProcess::Running) {
                loop.quit();
            }
        });
    const QMetaObject::Connection finishedConnection = connect(
        process_.get(),
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        &loop,
        [&loop]() {
            loop.quit();
        });
    const QMetaObject::Connection timeoutConnection = connect(
        &timeoutTimer,
        &QTimer::timeout,
        &loop,
        [&timedOut, &loop]() {
            timedOut = true;
            loop.quit();
        });

    responsePollTimer.start();
    timeoutTimer.start(timeoutMs);
    while (!completedResponses_.contains(id)
        && !timedOut
        && process_ != nullptr
        && process_->state() == QProcess::Running) {
        loop.exec();
    }

    disconnect(responseConnection);
    disconnect(finishedConnection);
    disconnect(timeoutConnection);
    responsePollTimer.stop();

    if (!completedResponses_.contains(id)) {
        if (timedOut) {
            errorMessage = QStringLiteral("Timed out waiting for the Matrix backend to finish the command.");
            return false;
        }

        errorMessage = QStringLiteral("The Matrix backend exited before replying.");
        const QString stderrText = stderrSummary();
        if (!stderrText.isEmpty()) {
            errorMessage.append(QStringLiteral("\n%1").arg(stderrText));
        }
        return false;
    }

    const CommandResponse response = completedResponses_.take(id);
    if (!response.ok) {
        errorMessage = response.error.isEmpty()
            ? QStringLiteral("The Matrix backend rejected the command.")
            : response.error;
        return false;
    }

    return true;
}

bool ProcessMatrixClientBackend::commandRequiringRunningProcess(const QJsonObject &command, QString &errorMessage, const int timeoutMs)
{
    if (process_ == nullptr || process_->state() != QProcess::Running) {
        errorMessage = QStringLiteral("Matrix backend is not running.");
        return false;
    }
    return sendCommand(command, errorMessage, timeoutMs);
}

void ProcessMatrixClientBackend::publishRuntime(const BotRuntimeSnapshot &runtime)
{
    latestRuntime_ = runtime;
    if (runtimeChangedCallback_) {
        runtimeChangedCallback_(latestRuntime_);
    }
}

void ProcessMatrixClientBackend::handleReadyReadStandardOutput()
{
    if (process_ == nullptr) {
        return;
    }

    stdoutBuffer_.append(process_->readAllStandardOutput());
    while (true) {
        const qsizetype newlineIndex = stdoutBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }

        QByteArray line = stdoutBuffer_.left(newlineIndex).trimmed();
        stdoutBuffer_.remove(0, newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (backendErrorCallback_) {
                backendErrorCallback_(QStringLiteral("Matrix backend emitted invalid JSON: %1").arg(QString::fromUtf8(line)));
            }
            continue;
        }

        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("response")) {
            const quint64 id = object.value(QStringLiteral("id")).toVariant().toULongLong();
            CommandResponse response;
            response.ok = object.value(QStringLiteral("ok")).toBool();
            response.error = object.value(QStringLiteral("error")).toString();
            completedResponses_.insert(id, response);
            continue;
        }

        if (type == QStringLiteral("runtime")) {
            publishRuntime(parseRuntimeSnapshot(object.value(QStringLiteral("snapshot")).toObject()));
            continue;
        }

        if (backendErrorCallback_) {
            backendErrorCallback_(QStringLiteral("Matrix backend emitted an unknown event type: %1").arg(type));
        }
    }
}

void ProcessMatrixClientBackend::handleReadyReadStandardError()
{
    if (process_ != nullptr) {
        stderrBuffer_.append(process_->readAllStandardError());
    }
}

void ProcessMatrixClientBackend::handleProcessFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    const bool expectedShutdown = explicitShutdown_;
    explicitShutdown_ = false;

    BotRuntimeSnapshot snapshot = latestRuntime_;
    snapshot.workerStates.clear();
    snapshot.activeDownloads.clear();
    snapshot.verification = VerificationSnapshot {};

    if (expectedShutdown) {
        snapshot.connectionState = ConnectionState::Stopped;
        snapshot.currentUserId.clear();
        snapshot.deviceId.clear();
        snapshot.accountMode.clear();
        publishRuntime(snapshot);
        return;
    }

    snapshot.connectionState = ConnectionState::Error;
    publishRuntime(snapshot);

    QString message;
    if (exitStatus == QProcess::CrashExit) {
        message = QStringLiteral("The Matrix backend crashed.");
    } else {
        message = QStringLiteral("The Matrix backend exited unexpectedly with code %1.").arg(exitCode);
    }

    const QString stderrText = stderrSummary();
    if (!stderrText.isEmpty()) {
        message.append(QStringLiteral("\n%1").arg(stderrText));
    }

    if (backendErrorCallback_) {
        backendErrorCallback_(message);
    }
}

void ProcessMatrixClientBackend::handleProcessErrorOccurred(const QProcess::ProcessError error)
{
    lastProcessError_ = processErrorSummary(error);
}
