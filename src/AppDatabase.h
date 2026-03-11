#pragma once

#include "Domain.h"

#include <QSqlDatabase>
#include <QStringList>
#include <QVector>

class AppDatabase
{
public:
    explicit AppDatabase(const QString &databasePath);
    ~AppDatabase();

    AppSettings loadSettings(const QString &defaultDestinationRootPath);
    bool saveSettings(const AppSettings &settings);

    QVector<RoomRecord> fetchRooms() const;
    QVector<DownloadJobRecord> fetchJobs() const;
    QVector<ActivityLogEntry> fetchRecentLogs(int limit = 500) const;
    QStringList aliasHistory(const QString &roomId) const;
    int fetchWaitingJobCount() const;

    bool retryFailedJob(qint64 jobId);
    int retryAllFailedJobs();
    bool clearFailedJob(qint64 jobId);
    int clearAllFailedJobs();
    bool resetHistoryScansForFullRescan();

    bool insertLog(AppLogLevel level, const QString &subsystem, const QString &message);

private:
    void initializeSchema();
    bool execute(const QString &sql) const;

    QSqlDatabase database_;
};

