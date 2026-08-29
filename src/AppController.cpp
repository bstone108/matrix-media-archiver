#include "AppController.h"

#include "AppUpdater.h"
#include "MatrixClientBackend.h"
#include "ProcessMatrixClientBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

namespace {
QString defaultDestinationRootPath()
{
    QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads.isEmpty()) {
        downloads = QDir::homePath() + QStringLiteral("/Downloads");
    }
    return downloads + QStringLiteral("/MatrixMediaArchiver");
}
}

AppController::AppController(QObject *parent)
    : QObject(parent)
    , database_(paths_.databasePath())
    , secretStore_(paths_)
    , backend_(std::make_unique<ProcessMatrixClientBackend>(paths_, this))
    , refreshTimer_(new QTimer(this))
{
    refreshTimer_->setInterval(1000);
    connect(refreshTimer_, &QTimer::timeout, this, &AppController::refresh);

    backend_->setRuntimeChangedCallback([this](const BotRuntimeSnapshot &runtime) {
        runtime_ = runtime;
        updateRefreshTimer();
        scheduleRefresh();
    });
    backend_->setBackendErrorCallback([this](const QString &message) {
        if (message.isEmpty()) {
            return;
        }
        lastErrorMessage_ = message;
        updateRefreshTimer();
        scheduleRefresh();
    });
}

void AppController::initialize()
{
    settings_ = database_.loadSettings(defaultDestinationRootPath());
    password_ = secretStore_.loadPassword();
    refresh();
    updateRefreshTimer();

    if (settings_.desiredPowerState) {
        togglePower(true);
    }

    updater_ = createAppUpdater();
    updater_->start();
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        if (updater_) {
            updater_->installPendingOnQuit();
        }
    });
}

void AppController::refresh()
{
    refreshQueued_ = false;
    rooms_ = database_.fetchRooms();
    jobs_ = database_.fetchJobs();
    logs_ = database_.fetchRecentLogs();
    waitingQueueCount_ = database_.fetchWaitingJobCount();
    emit stateChanged();
}

const AppSettings &AppController::settings() const
{
    return settings_;
}

const QString &AppController::password() const
{
    return password_;
}

const BotRuntimeSnapshot &AppController::runtime() const
{
    return runtime_;
}

const QVector<RoomRecord> &AppController::rooms() const
{
    return rooms_;
}

QVector<RoomRecord> AppController::joinedRooms() const
{
    QVector<RoomRecord> result;
    for (const RoomRecord &room : rooms_) {
        if (room.membership == QStringLiteral("joined") && !room.isSpace) {
            result.append(room);
        }
    }
    return result;
}

QVector<RoomRecord> AppController::joinedSpaces() const
{
    QVector<RoomRecord> result;
    for (const RoomRecord &room : rooms_) {
        if (room.membership == QStringLiteral("joined") && room.isSpace) {
            result.append(room);
        }
    }
    return result;
}

const QVector<DownloadJobRecord> &AppController::jobs() const
{
    return jobs_;
}

const QVector<ActivityLogEntry> &AppController::logs() const
{
    return logs_;
}

QVector<ActivityLogEntry> AppController::visibleLogs() const
{
    QVector<ActivityLogEntry> result;
    for (const ActivityLogEntry &entry : logs_) {
        if (entry.subsystem == QStringLiteral("queue")
            || entry.subsystem == QStringLiteral("commands")
            || entry.level == AppLogLevel::Warning
            || entry.level == AppLogLevel::Error) {
            result.append(entry);
        }
    }
    return result;
}

int AppController::waitingQueueCount() const
{
    return waitingQueueCount_;
}

QStringList AppController::aliasHistory(const QString &roomId) const
{
    if (roomId.isEmpty()) {
        return {};
    }
    return database_.aliasHistory(roomId);
}

QString AppController::connectionStatusText() const
{
    return connectionStateTitle(runtime_.connectionState);
}

QString AppController::lastErrorMessage() const
{
    return lastErrorMessage_;
}

void AppController::togglePower(const bool enabled)
{
    settings_.desiredPowerState = enabled;
    database_.saveSettings(settings_);

    if (!enabled) {
        QString errorMessage;
        if (backend_->stop(runtime_, errorMessage)) {
            logInfo(QStringLiteral("matrix"), QStringLiteral("Power off requested."));
        } else {
            lastErrorMessage_ = errorMessage;
            logError(QStringLiteral("matrix"), errorMessage);
        }
        updateRefreshTimer();
        refresh();
        return;
    }

    QString errorMessage;
    BotRuntimeSnapshot runtime;
    runtime.connectionState = ConnectionState::Starting;
    if (backend_->start(settings_, password_, runtime, errorMessage)) {
        runtime_ = runtime;
        logInfo(QStringLiteral("matrix"), QStringLiteral("Power on requested."));
    } else {
        runtime_ = runtime;
        lastErrorMessage_ = errorMessage;
        logError(QStringLiteral("matrix"), errorMessage);
    }

    updateRefreshTimer();
    refresh();
}

void AppController::saveSettings(const AppSettings &settings, const QString &password)
{
    settings_ = settings;
    password_ = password;
    database_.saveSettings(settings_);
    secretStore_.savePassword(password_);

    QString errorMessage;
    if ((runtime_.connectionState != ConnectionState::Stopped || settings_.desiredPowerState)
        && !backend_->saveSettings(settings_, password_, errorMessage)) {
        lastErrorMessage_ = errorMessage;
        logError(QStringLiteral("settings"), errorMessage);
    } else {
        logInfo(QStringLiteral("settings"), QStringLiteral("Settings saved."));
    }

    updateRefreshTimer();
    refresh();
}

void AppController::resetHistoryScans()
{
    const bool backendActive = runtime_.connectionState != ConnectionState::Stopped || settings_.desiredPowerState;
    bool ok = false;
    if (backendActive) {
        QString errorMessage;
        ok = backend_->resetHistoryScans(errorMessage);
        if (!ok) {
            lastErrorMessage_ = errorMessage;
            logError(QStringLiteral("history"), errorMessage);
        }
    } else {
        ok = database_.resetHistoryScansForFullRescan();
    }

    if (ok) {
        logInfo(
            QStringLiteral("history"),
            QStringLiteral("Reset room history, discoveries, and queued jobs for a full rescan."));
    } else {
        logError(QStringLiteral("history"), QStringLiteral("Failed to reset room history state."));
    }
    refresh();
}

void AppController::retryFailedJob(const qint64 jobId)
{
    if (database_.retryFailedJob(jobId)) {
        logInfo(QStringLiteral("queue"), QStringLiteral("Retried permanently failed job %1.").arg(jobId));
    }
    refresh();
}

void AppController::retryAllFailedJobs()
{
    const int count = database_.retryAllFailedJobs();
    if (count > 0) {
        logInfo(QStringLiteral("queue"), QStringLiteral("Retried %1 permanently failed jobs.").arg(count));
    }
    refresh();
}

void AppController::clearFailedJob(const qint64 jobId)
{
    if (database_.clearFailedJob(jobId)) {
        logInfo(QStringLiteral("queue"), QStringLiteral("Cleared permanently failed job %1.").arg(jobId));
    }
    refresh();
}

void AppController::clearAllFailedJobs()
{
    const int count = database_.clearAllFailedJobs();
    if (count > 0) {
        logInfo(QStringLiteral("queue"), QStringLiteral("Cleared %1 permanently failed jobs.").arg(count));
    }
    refresh();
}

void AppController::joinRoom(const QString &roomIdOrAlias)
{
    const QString trimmed = roomIdOrAlias.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (backend_->joinRoom(trimmed, errorMessage)) {
        logInfo(QStringLiteral("rooms"), QStringLiteral("Joined room %1.").arg(trimmed));
    } else {
        lastErrorMessage_ = errorMessage;
        logError(QStringLiteral("rooms"), QStringLiteral("Join failed for %1: %2").arg(trimmed, errorMessage));
    }
    refresh();
}

void AppController::leaveRoom(const QString &roomId)
{
    QString errorMessage;
    if (backend_->leaveRoom(roomId, errorMessage)) {
        logInfo(QStringLiteral("rooms"), QStringLiteral("Left room %1.").arg(roomId));
    } else {
        lastErrorMessage_ = errorMessage;
        logError(QStringLiteral("rooms"), QStringLiteral("Leave failed for %1: %2").arg(roomId, errorMessage));
    }
    refresh();
}

void AppController::requestVerification()
{
    QString errorMessage;
    if (!backend_->requestVerification(errorMessage)) {
        lastErrorMessage_ = errorMessage;
        logWarning(QStringLiteral("verification"), errorMessage);
        emit stateChanged();
    }
}

void AppController::startSasVerification()
{
    QString errorMessage;
    if (!backend_->startSasVerification(errorMessage)) {
        lastErrorMessage_ = errorMessage;
        logWarning(QStringLiteral("verification"), errorMessage);
        emit stateChanged();
    }
}

void AppController::approveVerification()
{
    QString errorMessage;
    if (!backend_->approveVerification(errorMessage)) {
        lastErrorMessage_ = errorMessage;
        logWarning(QStringLiteral("verification"), errorMessage);
        emit stateChanged();
    }
}

void AppController::declineVerification()
{
    QString errorMessage;
    if (!backend_->declineVerification(errorMessage)) {
        lastErrorMessage_ = errorMessage;
        logWarning(QStringLiteral("verification"), errorMessage);
        emit stateChanged();
    }
}

void AppController::dismissError()
{
    lastErrorMessage_.clear();
}

void AppController::checkForUpdates()
{
    if (updater_) {
        updater_->checkNow(true);
    }
}

void AppController::logInfo(const QString &subsystem, const QString &message)
{
    database_.insertLog(AppLogLevel::Info, subsystem, message);
}

void AppController::logWarning(const QString &subsystem, const QString &message)
{
    database_.insertLog(AppLogLevel::Warning, subsystem, message);
}

void AppController::logError(const QString &subsystem, const QString &message)
{
    database_.insertLog(AppLogLevel::Error, subsystem, message);
}

void AppController::scheduleRefresh()
{
    if (refreshQueued_) {
        return;
    }

    refreshQueued_ = true;
    QTimer::singleShot(0, this, &AppController::refresh);
}

void AppController::updateRefreshTimer()
{
    const bool shouldPoll = settings_.desiredPowerState
        || runtime_.connectionState == ConnectionState::Starting
        || runtime_.connectionState == ConnectionState::Running
        || runtime_.connectionState == ConnectionState::Stopping
        || runtime_.connectionState == ConnectionState::Error;

    if (shouldPoll) {
        refreshTimer_->start();
    } else {
        refreshTimer_->stop();
    }
}
