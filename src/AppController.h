#pragma once

#include "AppDatabase.h"
#include "AppPaths.h"
#include "Domain.h"
#include "MatrixClientBackend.h"
#include "AppUpdater.h"
#include "SecretStore.h"

#include <QObject>
#include <memory>

class QTimer;

class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);

    void initialize();
    void refresh();

    const AppSettings &settings() const;
    const QString &password() const;
    const BotRuntimeSnapshot &runtime() const;
    const QVector<RoomRecord> &rooms() const;
    QVector<RoomRecord> joinedRooms() const;
    QVector<RoomRecord> joinedSpaces() const;
    const QVector<DownloadJobRecord> &jobs() const;
    const QVector<ActivityLogEntry> &logs() const;
    QVector<ActivityLogEntry> visibleLogs() const;
    int waitingQueueCount() const;
    QStringList aliasHistory(const QString &roomId) const;

    QString connectionStatusText() const;
    QString lastErrorMessage() const;

public slots:
    void togglePower(bool enabled);
    void saveSettings(const AppSettings &settings, const QString &password);
    void resetHistoryScans();
    void retryFailedJob(qint64 jobId);
    void retryAllFailedJobs();
    void clearFailedJob(qint64 jobId);
    void clearAllFailedJobs();
    void joinRoom(const QString &roomIdOrAlias);
    void leaveRoom(const QString &roomId);
    void requestVerification();
    void startSasVerification();
    void approveVerification();
    void declineVerification();
    void dismissError();
    void checkForUpdates();

signals:
    void stateChanged();

private:
    void logInfo(const QString &subsystem, const QString &message);
    void logWarning(const QString &subsystem, const QString &message);
    void logError(const QString &subsystem, const QString &message);
    void scheduleRefresh();
    void updateRefreshTimer();

    AppPaths paths_;
    AppDatabase database_;
    SecretStore secretStore_;
    std::unique_ptr<MatrixClientBackend> backend_;
    std::unique_ptr<AppUpdater> updater_;
    QTimer *refreshTimer_ = nullptr;

    AppSettings settings_;
    QString password_;
    BotRuntimeSnapshot runtime_;
    QVector<RoomRecord> rooms_;
    QVector<DownloadJobRecord> jobs_;
    QVector<ActivityLogEntry> logs_;
    int waitingQueueCount_ = 0;
    QString lastErrorMessage_;
    bool refreshQueued_ = false;
};
