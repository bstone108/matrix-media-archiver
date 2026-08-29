#include "GitHubRelease.h"
#include "UpdateSettings.h"
#include "UpdateVersion.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QtTest/QTest>

class UpdateUtilityTests : public QObject
{
    Q_OBJECT

private slots:
    void dateBuildParsesUnpaddedAndPadded();
    void dateBuildSameDayBuildOrder();
    void dateBuildOlderDateVsNewerDate();
    void dateBuildRejectsInvalid();
    void macosAssetNameSelection();
    void windowsAndLinuxAssetNameSelection();
    void notifyOncePerVersion();
    void ignoresDraftAndPrereleaseFixtures();
};

namespace {
const QByteArray kLatestFixture = R"({
  "tag_name": "v2026.8.28.1",
  "draft": false,
  "prerelease": false,
  "html_url": "https://github.com/bstone108/matrix-media-archiver/releases/tag/v2026.8.28.1",
  "assets": [
    {"name": "MatrixMediaArchiverQt-2026.8.28.1-macos-arm64.zip", "browser_download_url": "https://example.invalid/arm64.zip", "size": 11},
    {"name": "MatrixMediaArchiverQt-2026.8.28.1-macos-x86_64.zip", "browser_download_url": "https://example.invalid/x86_64.zip", "size": 12},
    {"name": "MatrixMediaArchiverQt-2026.8.28.1-macos-arm64.dmg", "browser_download_url": "https://example.invalid/arm64.dmg", "size": 13},
    {"name": "MatrixMediaArchiverQt-2026.8.28.1-windows-x64.zip", "browser_download_url": "https://example.invalid/win-x64.zip", "size": 14},
    {"name": "MatrixMediaArchiverQt-2026.8.28.1-windows-arm64.zip", "browser_download_url": "https://example.invalid/win-arm64.zip", "size": 15},
    {"name": "MatrixMediaArchiverQt-2026.8.28.1-linux-x86_64-appimage.zip", "browser_download_url": "https://example.invalid/linux-x64.zip", "size": 16},
    {"name": "MatrixMediaArchiverQt-2026.8.28.1-linux-aarch64-appimage.zip", "browser_download_url": "https://example.invalid/linux-arm.zip", "size": 17}
  ]
})";

const QByteArray kDraftFixture = R"({
  "tag_name": "v2026.8.28.9",
  "draft": true,
  "prerelease": false,
  "html_url": "https://github.com/bstone108/matrix-media-archiver/releases/tag/v2026.8.28.9",
  "assets": []
})";

const QByteArray kPrereleaseFixture = R"({
  "tag_name": "v2026.8.28.8",
  "draft": false,
  "prerelease": true,
  "html_url": "https://github.com/bstone108/matrix-media-archiver/releases/tag/v2026.8.28.8",
  "assets": []
})";
}

void UpdateUtilityTests::dateBuildParsesUnpaddedAndPadded()
{
    const auto unpadded = DateBuildVersion::parse(QStringLiteral("2026.8.24.1"));
    const auto padded = DateBuildVersion::parse(QStringLiteral("2026.08.24.01"));
    const auto tagged = DateBuildVersion::parse(QStringLiteral("v2026.8.24.1"));
    QVERIFY(unpadded.has_value());
    QVERIFY(padded.has_value());
    QVERIFY(tagged.has_value());
    QCOMPARE(compareDateBuild(*unpadded, *padded), 0);
    QCOMPARE(compareDateBuild(*unpadded, *tagged), 0);
    QCOMPARE(unpadded->toUnpaddedString(), QStringLiteral("2026.8.24.1"));
}

void UpdateUtilityTests::dateBuildSameDayBuildOrder()
{
    QVERIFY(isNewerDateBuild(QStringLiteral("2026.8.24.2"), QStringLiteral("2026.8.24.1")));
    QVERIFY(!isNewerDateBuild(QStringLiteral("2026.8.24.1"), QStringLiteral("2026.8.24.2")));
    QVERIFY(!isNewerDateBuild(QStringLiteral("2026.8.24.1"), QStringLiteral("2026.8.24.1")));
}

void UpdateUtilityTests::dateBuildOlderDateVsNewerDate()
{
    QVERIFY(isNewerDateBuild(QStringLiteral("2026.8.28.1"), QStringLiteral("2026.8.24.1")));
    QVERIFY(isNewerDateBuild(QStringLiteral("2026.9.1.1"), QStringLiteral("2026.8.28.9")));
    QVERIFY(!isNewerDateBuild(QStringLiteral("2026.3.12.4"), QStringLiteral("2026.8.24.1")));
}

void UpdateUtilityTests::dateBuildRejectsInvalid()
{
    QVERIFY(!DateBuildVersion::parse(QStringLiteral("1.2.3")).has_value());
    QVERIFY(!DateBuildVersion::parse(QStringLiteral("2026.0.24.1")).has_value());
    QVERIFY(!DateBuildVersion::parse(QStringLiteral("not-a-version")).has_value());
}

void UpdateUtilityTests::macosAssetNameSelection()
{
    QCOMPARE(
        macosZipAssetName(QStringLiteral("2026.8.28.1"), QStringLiteral("arm64")),
        QStringLiteral("MatrixMediaArchiverQt-2026.8.28.1-macos-arm64.zip"));
    QCOMPARE(
        macosZipAssetName(QStringLiteral("2026.8.28.1"), QStringLiteral("x86_64")),
        QStringLiteral("MatrixMediaArchiverQt-2026.8.28.1-macos-x86_64.zip"));
    QCOMPARE(normalizeMacArch(QStringLiteral("aarch64")), QStringLiteral("arm64"));
    QCOMPARE(normalizeMacArch(QStringLiteral("x86_64")), QStringLiteral("x86_64"));

    const auto release = parseGitHubReleaseJson(kLatestFixture);
    QVERIFY(release.has_value());
    const GitHubReleaseAsset *arm = findReleaseAssetByName(
        *release, macosZipAssetName(QStringLiteral("2026.8.28.1"), QStringLiteral("arm64")));
    const GitHubReleaseAsset *intel = findReleaseAssetByName(
        *release, macosZipAssetName(QStringLiteral("2026.8.28.1"), QStringLiteral("x86_64")));
    QVERIFY(arm != nullptr);
    QVERIFY(intel != nullptr);
    QVERIFY(!arm->downloadUrl.contains(QStringLiteral(".dmg")));
    QVERIFY(!intel->downloadUrl.contains(QStringLiteral(".dmg")));
}

void UpdateUtilityTests::windowsAndLinuxAssetNameSelection()
{
    QCOMPARE(
        windowsZipAssetName(QStringLiteral("2026.8.28.1"), QStringLiteral("x64")),
        QStringLiteral("MatrixMediaArchiverQt-2026.8.28.1-windows-x64.zip"));
    QCOMPARE(
        windowsZipAssetName(QStringLiteral("2026.8.28.1"), QStringLiteral("arm64")),
        QStringLiteral("MatrixMediaArchiverQt-2026.8.28.1-windows-arm64.zip"));
    QCOMPARE(normalizeWindowsArch(QStringLiteral("x86_64")), QStringLiteral("x64"));
    QCOMPARE(normalizeLinuxArch(QStringLiteral("arm64")), QStringLiteral("aarch64"));
    QCOMPARE(
        linuxAppImageZipAssetName(QStringLiteral("2026.8.28.1"), QStringLiteral("x86_64")),
        QStringLiteral("MatrixMediaArchiverQt-2026.8.28.1-linux-x86_64-appimage.zip"));
}

void UpdateUtilityTests::notifyOncePerVersion()
{
    const QString path = QDir::temp().filePath(
        QStringLiteral("mma-update-tests-%1.ini").arg(QCoreApplication::applicationPid()));
    QFile::remove(path);
    QSettings settings(path, QSettings::IniFormat);
    UpdateSettings state(&settings);
    QVERIFY(state.shouldNotifyTag(QStringLiteral("v2026.8.28.1")));
    state.markNotifiedTag(QStringLiteral("v2026.8.28.1"));
    QVERIFY(!state.shouldNotifyTag(QStringLiteral("v2026.8.28.1")));
    QVERIFY(state.shouldNotifyTag(QStringLiteral("v2026.8.28.2")));
    settings.sync();
    QFile::remove(path);
}

void UpdateUtilityTests::ignoresDraftAndPrereleaseFixtures()
{
    const auto latest = parseGitHubReleaseJson(kLatestFixture);
    const auto draft = parseGitHubReleaseJson(kDraftFixture);
    const auto pre = parseGitHubReleaseJson(kPrereleaseFixture);
    QVERIFY(latest.has_value());
    QVERIFY(draft.has_value());
    QVERIFY(pre.has_value());
    QVERIFY(isUsableGitHubRelease(*latest));
    QVERIFY(!isUsableGitHubRelease(*draft));
    QVERIFY(!isUsableGitHubRelease(*pre));
    QCOMPARE(releaseVersionString(*latest), QStringLiteral("2026.8.28.1"));
}

QTEST_MAIN(UpdateUtilityTests)

#include "UpdateUtilityTests.moc"
