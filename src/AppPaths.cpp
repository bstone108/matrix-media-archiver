#include "AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QStandardPaths>

namespace {
QString fallbackAppDataPath()
{
    const QString home = QDir::homePath();
    return home + QStringLiteral("/.local/share/MatrixMediaArchiverQt");
}
}

AppPaths::AppPaths()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = fallbackAppDataPath();
    }

    rootPath_ = base + QStringLiteral("/MatrixMediaArchiverQt");
    appSupportPath_ = rootPath_;
    databasePath_ = rootPath_ + QStringLiteral("/app.sqlite");
    matrixDataPath_ = rootPath_ + QStringLiteral("/matrix-sdk/data");
    matrixCachePath_ = rootPath_ + QStringLiteral("/matrix-sdk/cache");
    tempDownloadsPath_ = rootPath_ + QStringLiteral("/tmp-downloads");
    secretStorePath_ = rootPath_ + QStringLiteral("/secrets.json");

    ensureDirectory(rootPath_);
    ensureDirectory(appSupportPath_);
    ensureDirectory(matrixDataPath_);
    ensureDirectory(matrixCachePath_);
    ensureDirectory(tempDownloadsPath_);
}

QString AppPaths::rootPath() const
{
    return rootPath_;
}

QString AppPaths::appSupportPath() const
{
    return appSupportPath_;
}

QString AppPaths::databasePath() const
{
    return databasePath_;
}

QString AppPaths::matrixDataPath() const
{
    return matrixDataPath_;
}

QString AppPaths::matrixCachePath() const
{
    return matrixCachePath_;
}

QString AppPaths::tempDownloadsPath() const
{
    return tempDownloadsPath_;
}

QString AppPaths::secretStorePath() const
{
    return secretStorePath_;
}

void AppPaths::ensureDirectory(const QString &path) const
{
    QDir directory;
    directory.mkpath(path);
    lockDownPermissions(path);
}

void AppPaths::lockDownPermissions(const QString &path) const
{
#ifdef Q_OS_UNIX
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#else
    Q_UNUSED(path);
#endif
}

