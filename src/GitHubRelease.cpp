#include "GitHubRelease.h"

#include "UpdateVersion.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

QString macosZipAssetName(const QString &version, const QString &arch)
{
    return QStringLiteral("MatrixMediaArchiverQt-%1-macos-%2.zip").arg(version, arch);
}

QString windowsZipAssetName(const QString &version, const QString &arch)
{
    return QStringLiteral("MatrixMediaArchiverQt-%1-windows-%2.zip").arg(version, arch);
}

QString linuxAppImageZipAssetName(const QString &version, const QString &arch)
{
    return QStringLiteral("MatrixMediaArchiverQt-%1-linux-%2-appimage.zip").arg(version, arch);
}

QString normalizeMacArch(const QString &cpuArch)
{
    const QString arch = cpuArch.toLower();
    if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")) {
        return QStringLiteral("arm64");
    }
    if (arch == QLatin1String("x86_64") || arch == QLatin1String("amd64") || arch == QLatin1String("i386")) {
        return QStringLiteral("x86_64");
    }
    return {};
}

QString normalizeWindowsArch(const QString &cpuArch)
{
    const QString arch = cpuArch.toLower();
    if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")) {
        return QStringLiteral("arm64");
    }
    if (arch == QLatin1String("x86_64") || arch == QLatin1String("amd64") || arch == QLatin1String("x64")) {
        return QStringLiteral("x64");
    }
    return {};
}

QString normalizeLinuxArch(const QString &cpuArch)
{
    const QString arch = cpuArch.toLower();
    if (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")) {
        return QStringLiteral("aarch64");
    }
    if (arch == QLatin1String("x86_64") || arch == QLatin1String("amd64")) {
        return QStringLiteral("x86_64");
    }
    return {};
}

std::optional<GitHubRelease> parseGitHubReleaseJson(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    GitHubRelease release;
    release.tagName = root.value(QStringLiteral("tag_name")).toString();
    release.htmlUrl = root.value(QStringLiteral("html_url")).toString();
    release.draft = root.value(QStringLiteral("draft")).toBool(false);
    release.prerelease = root.value(QStringLiteral("prerelease")).toBool(false);

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject object = value.toObject();
        GitHubReleaseAsset asset;
        asset.name = object.value(QStringLiteral("name")).toString();
        asset.downloadUrl = object.value(QStringLiteral("browser_download_url")).toString();
        asset.size = object.value(QStringLiteral("size")).toInteger();
        if (!asset.name.isEmpty() && !asset.downloadUrl.isEmpty()) {
            release.assets.append(asset);
        }
    }

    if (release.tagName.isEmpty()) {
        return std::nullopt;
    }
    return release;
}

bool isUsableGitHubRelease(const GitHubRelease &release)
{
    if (release.draft || release.prerelease) {
        return false;
    }
    return DateBuildVersion::parse(release.tagName).has_value();
}

QString releaseVersionString(const GitHubRelease &release)
{
    const auto parsed = DateBuildVersion::parse(release.tagName);
    if (!parsed.has_value()) {
        return {};
    }
    return parsed->toUnpaddedString();
}

const GitHubReleaseAsset *findReleaseAssetByName(const GitHubRelease &release, const QString &name)
{
    for (const GitHubReleaseAsset &asset : release.assets) {
        if (asset.name == name) {
            return &asset;
        }
    }
    return nullptr;
}
