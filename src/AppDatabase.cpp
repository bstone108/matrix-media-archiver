#include "AppDatabase.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

namespace {
QString connectionName()
{
    static const QString value = QStringLiteral("matrix-media-archiver-qt-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    return value;
}

QString timeWindowUnitKey(const TimeWindowUnit unit)
{
    switch (unit) {
    case TimeWindowUnit::None:
        return QStringLiteral("none");
    case TimeWindowUnit::Day:
        return QStringLiteral("day");
    case TimeWindowUnit::Week:
        return QStringLiteral("week");
    case TimeWindowUnit::Month:
        return QStringLiteral("month");
    }
    return QStringLiteral("none");
}

TimeWindowUnit parseTimeWindowUnit(const QString &value)
{
    if (value == QStringLiteral("day")) {
        return TimeWindowUnit::Day;
    }
    if (value == QStringLiteral("week")) {
        return TimeWindowUnit::Week;
    }
    if (value == QStringLiteral("month")) {
        return TimeWindowUnit::Month;
    }
    return TimeWindowUnit::None;
}

QString failedRetentionUnitKey(const FailedJobRetentionUnit unit)
{
    switch (unit) {
    case FailedJobRetentionUnit::None:
        return QStringLiteral("none");
    case FailedJobRetentionUnit::Minute:
        return QStringLiteral("minute");
    case FailedJobRetentionUnit::Hour:
        return QStringLiteral("hour");
    case FailedJobRetentionUnit::Day:
        return QStringLiteral("day");
    }
    return QStringLiteral("none");
}

FailedJobRetentionUnit parseFailedRetentionUnit(const QString &value)
{
    if (value == QStringLiteral("minute")) {
        return FailedJobRetentionUnit::Minute;
    }
    if (value == QStringLiteral("hour")) {
        return FailedJobRetentionUnit::Hour;
    }
    if (value == QStringLiteral("day")) {
        return FailedJobRetentionUnit::Day;
    }
    return FailedJobRetentionUnit::None;
}

MediaCategory parseMediaCategory(const QString &value)
{
    if (value == QStringLiteral("images")) {
        return MediaCategory::Images;
    }
    if (value == QStringLiteral("videos")) {
        return MediaCategory::Videos;
    }
    if (value == QStringLiteral("audio")) {
        return MediaCategory::Audio;
    }
    if (value == QStringLiteral("documents")) {
        return MediaCategory::Documents;
    }
    if (value == QStringLiteral("archives")) {
        return MediaCategory::Archives;
    }
    if (value == QStringLiteral("programs")) {
        return MediaCategory::Programs;
    }
    return MediaCategory::Other;
}

DownloadJobState parseDownloadJobState(const QString &value)
{
    if (value == QStringLiteral("downloading")) {
        return DownloadJobState::Downloading;
    }
    if (value == QStringLiteral("coolingDown")) {
        return DownloadJobState::CoolingDown;
    }
    if (value == QStringLiteral("completed")) {
        return DownloadJobState::Completed;
    }
    if (value == QStringLiteral("duplicateCompleted")) {
        return DownloadJobState::DuplicateCompleted;
    }
    if (value == QStringLiteral("failedPermanent")) {
        return DownloadJobState::FailedPermanent;
    }
    if (value == QStringLiteral("undecryptablePending")) {
        return DownloadJobState::UndecryptablePending;
    }
    return DownloadJobState::Queued;
}

AppLogLevel parseLogLevel(const QString &value)
{
    if (value == QStringLiteral("debug")) {
        return AppLogLevel::Debug;
    }
    if (value == QStringLiteral("warning")) {
        return AppLogLevel::Warning;
    }
    if (value == QStringLiteral("error")) {
        return AppLogLevel::Error;
    }
    return AppLogLevel::Info;
}
}

AppDatabase::AppDatabase(const QString &databasePath)
{
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName());
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    database_.setDatabaseName(databasePath);
    database_.open();
    execute(QStringLiteral("PRAGMA journal_mode = WAL"));
    execute(QStringLiteral("PRAGMA synchronous = NORMAL"));
    execute(QStringLiteral("PRAGMA foreign_keys = ON"));
    initializeSchema();
}

AppDatabase::~AppDatabase()
{
    if (database_.isOpen()) {
        database_.close();
    }
}

AppSettings AppDatabase::loadSettings(const QString &defaultDestinationRootPath)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT * FROM app_settings ORDER BY id DESC LIMIT 1"));
    if (query.exec() && query.next()) {
        AppSettings settings;
        settings.homeserverUrl = query.value(QStringLiteral("homeserver_url")).toString();
        settings.username = query.value(QStringLiteral("username")).toString();
        settings.ownerUserId = query.value(QStringLiteral("owner_user_id")).toString();
        settings.destinationRootPath = query.value(QStringLiteral("destination_root_path")).toString();
        settings.messageLimit = query.value(QStringLiteral("message_limit")).toInt();
        settings.timeWindowValue = query.value(QStringLiteral("time_window_value")).toInt();
        settings.timeWindowUnit = parseTimeWindowUnit(query.value(QStringLiteral("time_window_unit")).toString());
        settings.retryCooldownMinutes = query.value(QStringLiteral("retry_cooldown_minutes")).toInt();
        settings.retryLimit = query.value(QStringLiteral("retry_limit")).toInt();
        settings.downloadWorkerCount = query.value(QStringLiteral("download_worker_count")).toInt();
        settings.failedJobRetentionValue = query.value(QStringLiteral("failed_job_retention_value")).toInt();
        settings.failedJobRetentionUnit = parseFailedRetentionUnit(query.value(QStringLiteral("failed_job_retention_unit")).toString());
        settings.desiredPowerState = query.value(QStringLiteral("desired_power_state")).toBool();
        return settings;
    }

    const AppSettings defaults = AppSettings::defaults(defaultDestinationRootPath);
    saveSettings(defaults);
    return defaults;
}

bool AppDatabase::saveSettings(const AppSettings &settings)
{
    QSqlQuery deleteQuery(database_);
    if (!deleteQuery.exec(QStringLiteral("DELETE FROM app_settings"))) {
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO app_settings ("
        "homeserver_url, username, owner_user_id, destination_root_path, "
        "message_limit, time_window_value, time_window_unit, "
        "retry_cooldown_minutes, retry_limit, download_worker_count, "
        "failed_job_retention_value, failed_job_retention_unit, desired_power_state, updated_at"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(settings.homeserverUrl);
    query.addBindValue(settings.username);
    query.addBindValue(settings.ownerUserId);
    query.addBindValue(settings.destinationRootPath);
    query.addBindValue(settings.messageLimit);
    query.addBindValue(settings.timeWindowValue);
    query.addBindValue(timeWindowUnitKey(settings.timeWindowUnit));
    query.addBindValue(settings.retryCooldownMinutes);
    query.addBindValue(settings.retryLimit);
    query.addBindValue(qBound(1, settings.downloadWorkerCount, 6));
    query.addBindValue(settings.failedJobRetentionValue);
    query.addBindValue(failedRetentionUnitKey(settings.failedJobRetentionUnit));
    query.addBindValue(settings.desiredPowerState ? 1 : 0);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return query.exec();
}

QVector<RoomRecord> AppDatabase::fetchRooms() const
{
    QVector<RoomRecord> rooms;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT room_id, display_name, canonical_alias, active_folder_label, is_space, membership, updated_at "
        "FROM rooms "
        "ORDER BY COALESCE(display_name, canonical_alias, room_id) COLLATE NOCASE ASC"));

    if (!query.exec()) {
        return rooms;
    }

    while (query.next()) {
        RoomRecord room;
        room.roomId = query.value(0).toString();
        room.currentDisplayName = query.value(1).toString();
        room.currentCanonicalAlias = query.value(2).toString();
        room.activeFolderLabel = query.value(3).toString();
        room.isSpace = query.value(4).toBool();
        room.membership = query.value(5).toString();
        room.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
        rooms.append(room);
    }

    return rooms;
}

QVector<DownloadJobRecord> AppDatabase::fetchJobs() const
{
    QVector<DownloadJobRecord> jobs;
    QSqlQuery query(database_);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    query.prepare(QStringLiteral(
        "SELECT id, room_id, event_id, mxc_url, original_filename, mime_type, category, state, retry_count, "
        "next_eligible_at, last_failure_at, last_error, sha256, saved_relative_path, created_at, updated_at "
        "FROM download_jobs "
        "ORDER BY "
        "CASE state "
        "    WHEN 'queued' THEN 0 "
        "    WHEN 'coolingDown' THEN CASE WHEN next_eligible_at IS NULL OR next_eligible_at <= ? THEN 0 ELSE 1 END "
        "    WHEN 'undecryptablePending' THEN CASE WHEN next_eligible_at IS NULL OR next_eligible_at <= ? THEN 0 ELSE 1 END "
        "    WHEN 'failedPermanent' THEN 2 "
        "    ELSE 3 "
        "END, "
        "COALESCE(last_failure_at, created_at) ASC, id ASC"));
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        return jobs;
    }

    while (query.next()) {
        DownloadJobRecord job;
        job.id = query.value(0).toLongLong();
        job.roomId = query.value(1).toString();
        job.eventId = query.value(2).toString();
        job.mxcUrl = query.value(3).toString();
        job.originalFilename = query.value(4).toString();
        job.mimeType = query.value(5).toString();
        job.category = parseMediaCategory(query.value(6).toString());
        job.state = parseDownloadJobState(query.value(7).toString());
        job.retryCount = query.value(8).toInt();
        job.nextEligibleAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
        job.lastFailureAt = QDateTime::fromString(query.value(10).toString(), Qt::ISODateWithMs);
        job.lastError = query.value(11).toString();
        job.sha256 = query.value(12).toString();
        job.savedRelativePath = query.value(13).toString();
        job.createdAt = QDateTime::fromString(query.value(14).toString(), Qt::ISODateWithMs);
        job.updatedAt = QDateTime::fromString(query.value(15).toString(), Qt::ISODateWithMs);
        jobs.append(job);
    }

    return jobs;
}

QVector<ActivityLogEntry> AppDatabase::fetchRecentLogs(const int limit) const
{
    QVector<ActivityLogEntry> logs;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, created_at, level, subsystem, message "
        "FROM activity_log ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        return logs;
    }

    while (query.next()) {
        ActivityLogEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.createdAt = QDateTime::fromString(query.value(1).toString(), Qt::ISODateWithMs);
        entry.level = parseLogLevel(query.value(2).toString());
        entry.subsystem = query.value(3).toString();
        entry.message = query.value(4).toString();
        logs.prepend(entry);
    }

    return logs;
}

QStringList AppDatabase::aliasHistory(const QString &roomId) const
{
    QStringList aliases;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT alias FROM room_alias_history WHERE room_id = ? ORDER BY seen_at DESC"));
    query.addBindValue(roomId);
    if (!query.exec()) {
        return aliases;
    }

    while (query.next()) {
        aliases.append(query.value(0).toString());
    }

    return aliases;
}

int AppDatabase::fetchWaitingJobCount() const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM download_jobs WHERE state IN (?, ?, ?)"));
    query.addBindValue(downloadJobStateTitle(DownloadJobState::Queued));
    query.addBindValue(downloadJobStateTitle(DownloadJobState::CoolingDown));
    query.addBindValue(downloadJobStateTitle(DownloadJobState::UndecryptablePending));
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

bool AppDatabase::retryFailedJob(const qint64 jobId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE download_jobs "
        "SET state = ?, retry_count = 0, next_eligible_at = NULL, last_failure_at = NULL, last_error = NULL, updated_at = ? "
        "WHERE id = ? AND state = ?"));
    query.addBindValue(downloadJobStateTitle(DownloadJobState::Queued));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(jobId);
    query.addBindValue(downloadJobStateTitle(DownloadJobState::FailedPermanent));
    return query.exec() && query.numRowsAffected() > 0;
}

int AppDatabase::retryAllFailedJobs()
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "UPDATE download_jobs "
        "SET state = ?, retry_count = 0, next_eligible_at = NULL, last_failure_at = NULL, last_error = NULL, updated_at = ? "
        "WHERE state = ?"));
    query.addBindValue(downloadJobStateTitle(DownloadJobState::Queued));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(downloadJobStateTitle(DownloadJobState::FailedPermanent));
    if (!query.exec()) {
        return 0;
    }
    return query.numRowsAffected();
}

bool AppDatabase::clearFailedJob(const qint64 jobId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM download_jobs WHERE id = ? AND state = ?"));
    query.addBindValue(jobId);
    query.addBindValue(downloadJobStateTitle(DownloadJobState::FailedPermanent));
    return query.exec() && query.numRowsAffected() > 0;
}

int AppDatabase::clearAllFailedJobs()
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM download_jobs WHERE state = ?"));
    query.addBindValue(downloadJobStateTitle(DownloadJobState::FailedPermanent));
    if (!query.exec()) {
        return 0;
    }
    return query.numRowsAffected();
}

bool AppDatabase::resetHistoryScansForFullRescan()
{
    const bool scanReset = execute(QStringLiteral(
        "UPDATE room_scan_state SET "
        "last_processed_event_id = NULL, "
        "last_processed_ts = NULL, "
        "oldest_backfilled_event_id = NULL, "
        "oldest_backfilled_ts = NULL, "
        "historical_message_count = 0, "
        "initial_backfill_complete = 0, "
        "last_history_mode = 'idle', "
        "last_history_run_at = NULL"));
    const bool discoveriesCleared = execute(QStringLiteral("DELETE FROM discovered_attachments"));
    const bool jobsCleared = execute(QStringLiteral("DELETE FROM download_jobs"));
    return scanReset && discoveriesCleared && jobsCleared;
}

bool AppDatabase::insertLog(const AppLogLevel level, const QString &subsystem, const QString &message)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO activity_log (created_at, level, subsystem, message) VALUES (?, ?, ?, ?)"));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(appLogLevelTitle(level));
    query.addBindValue(subsystem);
    query.addBindValue(message);
    const bool inserted = query.exec();
    execute(QStringLiteral(
        "DELETE FROM activity_log "
        "WHERE created_at < datetime('now', '-30 day')"));
    execute(QStringLiteral(
        "DELETE FROM activity_log "
        "WHERE id NOT IN (SELECT id FROM activity_log ORDER BY id DESC LIMIT 5000)"));
    return inserted;
}

void AppDatabase::initializeSchema()
{
    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS app_settings ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "homeserver_url TEXT NOT NULL,"
        "username TEXT NOT NULL,"
        "owner_user_id TEXT NOT NULL,"
        "destination_root_path TEXT NOT NULL,"
        "message_limit INTEGER NOT NULL,"
        "time_window_value INTEGER NOT NULL,"
        "time_window_unit TEXT NOT NULL,"
        "retry_cooldown_minutes INTEGER NOT NULL,"
        "retry_limit INTEGER NOT NULL,"
        "download_worker_count INTEGER NOT NULL DEFAULT 1,"
        "failed_job_retention_value INTEGER NOT NULL DEFAULT 0,"
        "failed_job_retention_unit TEXT NOT NULL DEFAULT 'none',"
        "desired_power_state INTEGER NOT NULL,"
        "updated_at TEXT NOT NULL"
        ")"));

    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS rooms ("
        "room_id TEXT PRIMARY KEY,"
        "display_name TEXT,"
        "canonical_alias TEXT,"
        "active_folder_label TEXT NOT NULL,"
        "is_space INTEGER NOT NULL DEFAULT 0,"
        "membership TEXT NOT NULL,"
        "updated_at TEXT NOT NULL"
        ")"));

    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS room_alias_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "room_id TEXT NOT NULL,"
        "alias TEXT NOT NULL,"
        "seen_at TEXT NOT NULL,"
        "UNIQUE(room_id, alias)"
        ")"));

    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS room_scan_state ("
        "room_id TEXT PRIMARY KEY,"
        "last_processed_event_id TEXT,"
        "last_processed_ts TEXT,"
        "oldest_backfilled_event_id TEXT,"
        "oldest_backfilled_ts TEXT,"
        "historical_message_count INTEGER NOT NULL DEFAULT 0,"
        "initial_backfill_complete INTEGER NOT NULL DEFAULT 0,"
        "last_history_mode TEXT NOT NULL DEFAULT 'idle',"
        "last_history_run_at TEXT"
        ")"));

    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS discovered_attachments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "room_id TEXT NOT NULL,"
        "event_id TEXT NOT NULL,"
        "origin_ts TEXT NOT NULL,"
        "mxc_url TEXT NOT NULL,"
        "original_filename TEXT,"
        "mime_type TEXT,"
        "category TEXT NOT NULL,"
        "UNIQUE(room_id, event_id)"
        ")"));

    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS download_jobs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "room_id TEXT NOT NULL,"
        "event_id TEXT NOT NULL,"
        "mxc_url TEXT NOT NULL,"
        "original_filename TEXT,"
        "mime_type TEXT,"
        "category TEXT NOT NULL,"
        "state TEXT NOT NULL,"
        "retry_count INTEGER NOT NULL DEFAULT 0,"
        "next_eligible_at TEXT,"
        "last_failure_at TEXT,"
        "last_error TEXT,"
        "sha256 TEXT,"
        "saved_relative_path TEXT,"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL,"
        "UNIQUE(room_id, event_id)"
        ")"));

    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS activity_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_at TEXT NOT NULL,"
        "level TEXT NOT NULL,"
        "subsystem TEXT NOT NULL,"
        "message TEXT NOT NULL"
        ")"));

    execute(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS space_auto_joins ("
        "space_room_id TEXT NOT NULL,"
        "child_room_id TEXT NOT NULL,"
        "auto_joined_by_bot INTEGER NOT NULL DEFAULT 0,"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL,"
        "PRIMARY KEY(space_room_id, child_room_id)"
        ")"));
}

bool AppDatabase::execute(const QString &sql) const
{
    QSqlQuery query(database_);
    return query.exec(sql);
}
