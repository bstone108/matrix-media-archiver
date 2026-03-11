#pragma once

#include <QString>

class AppPaths
{
public:
    AppPaths();

    QString rootPath() const;
    QString appSupportPath() const;
    QString databasePath() const;
    QString matrixDataPath() const;
    QString matrixCachePath() const;
    QString tempDownloadsPath() const;
    QString secretStorePath() const;

private:
    void ensureDirectory(const QString &path) const;
    void lockDownPermissions(const QString &path) const;

    QString rootPath_;
    QString appSupportPath_;
    QString databasePath_;
    QString matrixDataPath_;
    QString matrixCachePath_;
    QString tempDownloadsPath_;
    QString secretStorePath_;
};

