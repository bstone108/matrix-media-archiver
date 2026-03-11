#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

enum class AppSection {
    Dashboard,
    Workers,
    Rooms,
    Spaces,
    Queue,
    Help,
    Settings,
    Verification,
};

inline QList<AppSection> allSections()
{
    return {
        AppSection::Dashboard,
        AppSection::Workers,
        AppSection::Rooms,
        AppSection::Spaces,
        AppSection::Queue,
        AppSection::Help,
        AppSection::Settings,
        AppSection::Verification,
    };
}

inline QString sectionTitle(const AppSection section)
{
    switch (section) {
    case AppSection::Dashboard:
        return QStringLiteral("Dashboard");
    case AppSection::Workers:
        return QStringLiteral("Workers");
    case AppSection::Rooms:
        return QStringLiteral("Rooms");
    case AppSection::Spaces:
        return QStringLiteral("Spaces");
    case AppSection::Queue:
        return QStringLiteral("Queue");
    case AppSection::Help:
        return QStringLiteral("Help");
    case AppSection::Settings:
        return QStringLiteral("Settings");
    case AppSection::Verification:
        return QStringLiteral("Verification");
    }
    return QStringLiteral("Unknown");
}

enum class AccountMode {
    DedicatedBot,
    SharedOwnerAccount,
};

inline QString accountModeTitle(const AccountMode mode)
{
    switch (mode) {
    case AccountMode::DedicatedBot:
        return QStringLiteral("dedicatedBot");
    case AccountMode::SharedOwnerAccount:
        return QStringLiteral("sharedOwnerAccount");
    }
    return QStringLiteral("unknown");
}

enum class TimeWindowUnit {
    None,
    Day,
    Week,
    Month,
};

inline QList<TimeWindowUnit> allTimeWindowUnits()
{
    return {TimeWindowUnit::None, TimeWindowUnit::Day, TimeWindowUnit::Week, TimeWindowUnit::Month};
}

inline QString timeWindowUnitTitle(const TimeWindowUnit unit)
{
    switch (unit) {
    case TimeWindowUnit::None:
        return QStringLiteral("Disabled");
    case TimeWindowUnit::Day:
        return QStringLiteral("Days");
    case TimeWindowUnit::Week:
        return QStringLiteral("Weeks");
    case TimeWindowUnit::Month:
        return QStringLiteral("Months");
    }
    return QStringLiteral("Unknown");
}

enum class FailedJobRetentionUnit {
    None,
    Minute,
    Hour,
    Day,
};

inline QList<FailedJobRetentionUnit> allFailedJobRetentionUnits()
{
    return {
        FailedJobRetentionUnit::None,
        FailedJobRetentionUnit::Minute,
        FailedJobRetentionUnit::Hour,
        FailedJobRetentionUnit::Day,
    };
}

inline QString failedJobRetentionUnitTitle(const FailedJobRetentionUnit unit)
{
    switch (unit) {
    case FailedJobRetentionUnit::None:
        return QStringLiteral("Disabled");
    case FailedJobRetentionUnit::Minute:
        return QStringLiteral("Minutes");
    case FailedJobRetentionUnit::Hour:
        return QStringLiteral("Hours");
    case FailedJobRetentionUnit::Day:
        return QStringLiteral("Days");
    }
    return QStringLiteral("Unknown");
}

enum class MediaCategory {
    Images,
    Videos,
    Audio,
    Documents,
    Archives,
    Programs,
    Other,
};

inline QString mediaCategoryTitle(const MediaCategory category)
{
    switch (category) {
    case MediaCategory::Images:
        return QStringLiteral("images");
    case MediaCategory::Videos:
        return QStringLiteral("videos");
    case MediaCategory::Audio:
        return QStringLiteral("audio");
    case MediaCategory::Documents:
        return QStringLiteral("documents");
    case MediaCategory::Archives:
        return QStringLiteral("archives");
    case MediaCategory::Programs:
        return QStringLiteral("programs");
    case MediaCategory::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

enum class DownloadJobState {
    Queued,
    Downloading,
    CoolingDown,
    Completed,
    DuplicateCompleted,
    FailedPermanent,
    UndecryptablePending,
};

inline QString downloadJobStateTitle(const DownloadJobState state)
{
    switch (state) {
    case DownloadJobState::Queued:
        return QStringLiteral("queued");
    case DownloadJobState::Downloading:
        return QStringLiteral("downloading");
    case DownloadJobState::CoolingDown:
        return QStringLiteral("coolingDown");
    case DownloadJobState::Completed:
        return QStringLiteral("completed");
    case DownloadJobState::DuplicateCompleted:
        return QStringLiteral("duplicateCompleted");
    case DownloadJobState::FailedPermanent:
        return QStringLiteral("failedPermanent");
    case DownloadJobState::UndecryptablePending:
        return QStringLiteral("undecryptablePending");
    }
    return QStringLiteral("queued");
}

enum class RoomHistoryMode {
    Idle,
    InitialBackfill,
    ReconnectCatchUp,
    Complete,
};

inline QString roomHistoryModeTitle(const RoomHistoryMode mode)
{
    switch (mode) {
    case RoomHistoryMode::Idle:
        return QStringLiteral("idle");
    case RoomHistoryMode::InitialBackfill:
        return QStringLiteral("initialBackfill");
    case RoomHistoryMode::ReconnectCatchUp:
        return QStringLiteral("reconnectCatchUp");
    case RoomHistoryMode::Complete:
        return QStringLiteral("complete");
    }
    return QStringLiteral("idle");
}

enum class ConnectionState {
    Stopped,
    Starting,
    Running,
    Stopping,
    Error,
};

inline QString connectionStateTitle(const ConnectionState state)
{
    switch (state) {
    case ConnectionState::Stopped:
        return QStringLiteral("Disconnected");
    case ConnectionState::Starting:
        return QStringLiteral("Starting");
    case ConnectionState::Running:
        return QStringLiteral("Connected");
    case ConnectionState::Stopping:
        return QStringLiteral("Stopping");
    case ConnectionState::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Disconnected");
}

enum class AppLogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

inline QString appLogLevelTitle(const AppLogLevel level)
{
    switch (level) {
    case AppLogLevel::Debug:
        return QStringLiteral("debug");
    case AppLogLevel::Info:
        return QStringLiteral("info");
    case AppLogLevel::Warning:
        return QStringLiteral("warning");
    case AppLogLevel::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("info");
}

enum class VerificationStatus {
    Unknown,
    Verified,
    Unverified,
};

inline QString verificationStatusTitle(const VerificationStatus status)
{
    switch (status) {
    case VerificationStatus::Unknown:
        return QStringLiteral("unknown");
    case VerificationStatus::Verified:
        return QStringLiteral("verified");
    case VerificationStatus::Unverified:
        return QStringLiteral("unverified");
    }
    return QStringLiteral("unknown");
}

struct AppSettings {
    QString homeserverUrl;
    QString username;
    QString ownerUserId;
    QString destinationRootPath;
    int messageLimit = 5000;
    int timeWindowValue = 0;
    TimeWindowUnit timeWindowUnit = TimeWindowUnit::None;
    int retryCooldownMinutes = 5;
    int retryLimit = 10;
    int downloadWorkerCount = 1;
    int failedJobRetentionValue = 0;
    FailedJobRetentionUnit failedJobRetentionUnit = FailedJobRetentionUnit::None;
    bool desiredPowerState = false;

    static AppSettings defaults(const QString &destinationRootPath)
    {
        AppSettings settings;
        settings.homeserverUrl = QStringLiteral("https://matrix.org");
        settings.destinationRootPath = destinationRootPath;
        return settings;
    }
};

struct StoredSession {
    QString accessToken;
    QString refreshToken;
    QString userId;
    QString deviceId;
    QString homeserverUrl;
    QString oidcData;
    QString slidingSyncVersion;
};

struct RoomRecord {
    QString roomId;
    QString currentDisplayName;
    QString currentCanonicalAlias;
    QString activeFolderLabel;
    bool isSpace = false;
    QString membership;
    QDateTime updatedAt;
};

struct RoomCheckpoint {
    QString roomId;
    QString lastProcessedEventId;
    QDateTime lastProcessedTimestamp;
    QString oldestBackfilledEventId;
    QDateTime oldestBackfilledTimestamp;
    int historicalMessageCount = 0;
    bool initialBackfillComplete = false;
    RoomHistoryMode lastHistoryMode = RoomHistoryMode::Idle;
    QDateTime lastHistoryRunAt;
};

struct AttachmentDiscovery {
    QString roomId;
    QString eventId;
    QDateTime originServerTimestamp;
    QString mxcUrl;
    QString originalFilename;
    QString mimeType;
    MediaCategory category = MediaCategory::Other;
};

struct SpaceChildDescriptor {
    QString roomId;
    QStringList viaServers;
};

struct RoomHierarchySnapshot {
    QString roomId;
    bool isSpace = false;
    QString displayName;
    QString canonicalAlias;
    QVector<SpaceChildDescriptor> children;
};

struct SpaceAutoJoinRecord {
    QString spaceRoomId;
    QString childRoomId;
    bool autoJoinedByBot = false;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct DownloadJobRecord {
    qint64 id = 0;
    QString roomId;
    QString eventId;
    QString mxcUrl;
    QString originalFilename;
    QString mimeType;
    MediaCategory category = MediaCategory::Other;
    DownloadJobState state = DownloadJobState::Queued;
    int retryCount = 0;
    QDateTime nextEligibleAt;
    QDateTime lastFailureAt;
    QString lastError;
    QString sha256;
    QString savedRelativePath;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct ActivityLogEntry {
    qint64 id = 0;
    QDateTime createdAt;
    AppLogLevel level = AppLogLevel::Info;
    QString subsystem;
    QString message;
};

struct RoomWorkerSnapshot {
    QString roomId;
    bool liveWatcherActive = false;
    RoomHistoryMode historyMode = RoomHistoryMode::Idle;
    QString historyDetail = QStringLiteral("Idle");
};

struct ActiveDownloadSnapshot {
    int workerId = 0;
    qint64 jobId = 0;
    QString roomId;
    QString filename;
    qint64 receivedBytes = 0;
    qint64 totalBytes = -1;
};

struct VerificationEmoji {
    QString symbol;
    QString description;
};

struct VerificationSnapshot {
    VerificationStatus state = VerificationStatus::Unknown;
    QString deviceId;
    QVector<VerificationEmoji> emojis;
    QVector<quint16> decimals;
};

struct BotRuntimeSnapshot {
    ConnectionState connectionState = ConnectionState::Stopped;
    QString currentUserId;
    QString deviceId;
    QString accountMode;
    VerificationSnapshot verification;
    QVector<RoomWorkerSnapshot> workerStates;
    QVector<ActiveDownloadSnapshot> activeDownloads;
};

