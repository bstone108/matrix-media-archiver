#pragma once

#include <QString>
#include <QVector>
#include <optional>

struct GitHubReleaseAsset
{
    QString name;
    QString downloadUrl;
    qint64 size = 0;
};

struct GitHubRelease
{
    QString tagName;
    QString htmlUrl;
    bool draft = false;
    bool prerelease = false;
    QVector<GitHubReleaseAsset> assets;
};

QString macosZipAssetName(const QString &version, const QString &arch);
QString windowsZipAssetName(const QString &version, const QString &arch);
QString linuxAppImageZipAssetName(const QString &version, const QString &arch);

QString normalizeMacArch(const QString &cpuArch);
QString normalizeWindowsArch(const QString &cpuArch);
QString normalizeLinuxArch(const QString &cpuArch);

std::optional<GitHubRelease> parseGitHubReleaseJson(const QByteArray &json);
bool isUsableGitHubRelease(const GitHubRelease &release);
QString releaseVersionString(const GitHubRelease &release);
const GitHubReleaseAsset *findReleaseAssetByName(const GitHubRelease &release, const QString &name);
