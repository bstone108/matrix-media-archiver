#pragma once

#include <QDateTime>
#include <QSettings>
#include <QString>

class UpdateSettings
{
public:
    static constexpr auto kLastCheckUtc = "updates/lastCheckUtc";
    static constexpr auto kLastNotifiedTag = "updates/lastNotifiedTag";
    static constexpr auto kStagedUpdatePath = "updates/stagedUpdatePath";
    static constexpr auto kStagedUpdateVersion = "updates/stagedUpdateVersion";
    static constexpr auto kPendingInstallOnQuit = "updates/pendingInstallOnQuit";

    explicit UpdateSettings(QSettings *settings);

    QDateTime lastCheckUtc() const;
    void setLastCheckUtc(const QDateTime &when);

    QString lastNotifiedTag() const;
    bool shouldNotifyTag(const QString &tag) const;
    void markNotifiedTag(const QString &tag);

    QString stagedUpdatePath() const;
    QString stagedUpdateVersion() const;
    bool pendingInstallOnQuit() const;
    void setStagedUpdate(const QString &path, const QString &version, bool pendingInstallOnQuit);
    void clearStagedUpdate();

private:
    QSettings *settings_;
};
