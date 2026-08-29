#include "UpdateSettings.h"

#include <QDateTime>

UpdateSettings::UpdateSettings(QSettings *settings)
    : settings_(settings)
{
}

QDateTime UpdateSettings::lastCheckUtc() const
{
    return settings_->value(QLatin1String(kLastCheckUtc)).toDateTime();
}

void UpdateSettings::setLastCheckUtc(const QDateTime &when)
{
    settings_->setValue(QLatin1String(kLastCheckUtc), when.toUTC());
}

QString UpdateSettings::lastNotifiedTag() const
{
    return settings_->value(QLatin1String(kLastNotifiedTag)).toString();
}

bool UpdateSettings::shouldNotifyTag(const QString &tag) const
{
    if (tag.isEmpty()) {
        return false;
    }
    return lastNotifiedTag() != tag;
}

void UpdateSettings::markNotifiedTag(const QString &tag)
{
    settings_->setValue(QLatin1String(kLastNotifiedTag), tag);
}

QString UpdateSettings::stagedUpdatePath() const
{
    return settings_->value(QLatin1String(kStagedUpdatePath)).toString();
}

QString UpdateSettings::stagedUpdateVersion() const
{
    return settings_->value(QLatin1String(kStagedUpdateVersion)).toString();
}

bool UpdateSettings::pendingInstallOnQuit() const
{
    return settings_->value(QLatin1String(kPendingInstallOnQuit), false).toBool();
}

void UpdateSettings::setStagedUpdate(const QString &path, const QString &version, const bool pendingInstallOnQuit)
{
    settings_->setValue(QLatin1String(kStagedUpdatePath), path);
    settings_->setValue(QLatin1String(kStagedUpdateVersion), version);
    settings_->setValue(QLatin1String(kPendingInstallOnQuit), pendingInstallOnQuit);
}

void UpdateSettings::clearStagedUpdate()
{
    settings_->remove(QLatin1String(kStagedUpdatePath));
    settings_->remove(QLatin1String(kStagedUpdateVersion));
    settings_->remove(QLatin1String(kPendingInstallOnQuit));
}
