#include "UpdateChecker.h"

#include "GitHubRelease.h"
#include "UpdateSettings.h"
#include "UpdateVersion.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

namespace {
constexpr auto kLatestReleaseUrl =
    "https://api.github.com/repos/bstone108/matrix-media-archiver/releases/latest";
constexpr qint64 kCheckIntervalMs = 48LL * 60LL * 60LL * 1000LL;
constexpr int kLaunchDelayMs = 4000;

QString envAppImagePath()
{
    return qEnvironmentVariable("APPIMAGE");
}

bool extractZip(const QString &zipPath, const QString &destinationDir)
{
    QDir().mkpath(destinationDir);
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("tar"), {QStringLiteral("-xf"), zipPath, QStringLiteral("-C"), destinationDir});
    if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.start(
            QStringLiteral("powershell"),
            {QStringLiteral("-NoProfile"),
             QStringLiteral("-Command"),
             QStringLiteral("Expand-Archive -LiteralPath \"%1\" -DestinationPath \"%2\" -Force")
                 .arg(zipPath, destinationDir)});
        if (!process.waitForFinished(120000)) {
            return false;
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
    return true;
#else
    QProcess process;
    process.start(QStringLiteral("unzip"), {QStringLiteral("-o"), zipPath, QStringLiteral("-d"), destinationDir});
    if (process.waitForFinished(120000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        return true;
    }
    process.start(QStringLiteral("python3"), {
        QStringLiteral("-c"),
        QStringLiteral(
            "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])"),
        zipPath,
        destinationDir,
    });
    if (!process.waitForFinished(120000)) {
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
#endif
}

QString findFirstFile(const QString &root, const QStringList &nameFilters)
{
    QDir directory(root);
    const QFileInfoList matches = directory.entryInfoList(nameFilters, QDir::Files | QDir::Readable);
    if (!matches.isEmpty()) {
        return matches.first().absoluteFilePath();
    }
    const QFileInfoList children = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &child : children) {
        const QString nested = findFirstFile(child.absoluteFilePath(), nameFilters);
        if (!nested.isEmpty()) {
            return nested;
        }
    }
    return {};
}

bool pathIsWritable(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    if (info.exists()) {
        return info.isWritable();
    }
    return QFileInfo(info.absolutePath()).isWritable();
}

void openReleaseUrl(const QString &htmlUrl)
{
    if (!htmlUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(htmlUrl));
    }
}
} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
    , intervalTimer_(new QTimer(this))
    , settingsStore_(new QSettings(this))
{
    intervalTimer_->setInterval(static_cast<int>(kCheckIntervalMs));
    connect(intervalTimer_, &QTimer::timeout, this, [this]() { checkNow(false); });
}

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::start()
{
    UpdateSettings settings(settingsStore_);
    const QDateTime lastCheck = settings.lastCheckUtc();
    const bool stale = !lastCheck.isValid()
        || lastCheck.msecsTo(QDateTime::currentDateTimeUtc()) >= kCheckIntervalMs;
    if (stale) {
        QTimer::singleShot(kLaunchDelayMs, this, [this]() { checkNow(false); });
    }
    intervalTimer_->start();
}

void UpdateChecker::checkNow(const bool userInitiated)
{
    checkKind_ = userInitiated ? CheckKind::Manual : CheckKind::Automatic;
    requestLatestRelease();
}

void UpdateChecker::installPendingOnQuit()
{
    UpdateSettings settings(settingsStore_);
    if (!settings.pendingInstallOnQuit()) {
        return;
    }
    const QString staged = settings.stagedUpdatePath();
    if (staged.isEmpty() || !QFileInfo::exists(staged)) {
        settings.clearStagedUpdate();
        return;
    }
    if (spawnApplyHelper(staged)) {
        settings.clearStagedUpdate();
    }
}

void UpdateChecker::requestLatestRelease()
{
    QNetworkRequest request{QUrl(QString::fromLatin1(kLatestReleaseUrl))};
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("MatrixMediaArchiverQt/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleLatestReply(reply); });
}

void UpdateChecker::handleLatestReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        if (checkKind_ == CheckKind::Manual) {
            QMessageBox::information(
                nullptr,
                QStringLiteral("Check for updates"),
                QStringLiteral("Unable to check for updates right now."));
        }
        return;
    }

    latestBody_ = reply->readAll();
    UpdateSettings settings(settingsStore_);
    settings.setLastCheckUtc(QDateTime::currentDateTimeUtc());
    applyRelease();
}

void UpdateChecker::applyRelease()
{
    const auto parsed = parseGitHubReleaseJson(latestBody_);
    if (!parsed.has_value() || !isUsableGitHubRelease(*parsed)) {
        return;
    }

    const QString current = QCoreApplication::applicationVersion();
    const QString remoteVersion = releaseVersionString(*parsed);
    pendingTag_ = parsed->tagName;
    pendingVersion_ = remoteVersion;
    pendingHtmlUrl_ = parsed->htmlUrl;

    if (!isNewerDateBuild(remoteVersion, current)) {
        if (checkKind_ == CheckKind::Manual) {
            QMessageBox::information(
                nullptr,
                QStringLiteral("Check for updates"),
                QStringLiteral("You're up to date (%1).").arg(current));
        }
        return;
    }

#ifdef Q_OS_LINUX
    if (!runningAsAppImage()) {
        notifyReleaseLinkOnce();
        return;
    }
#endif

    if (!destinationWritable()) {
        notifyReleaseLinkOnce();
        return;
    }

    pendingAssetName_ = matchingAssetName(remoteVersion);
    const GitHubReleaseAsset *asset = findReleaseAssetByName(*parsed, pendingAssetName_);
    if (asset == nullptr) {
        notifyReleaseLinkOnce();
        return;
    }

    UpdateSettings settings(settingsStore_);
    if (settings.stagedUpdateVersion() == remoteVersion && QFileInfo::exists(settings.stagedUpdatePath())) {
        promptRestart(settings.stagedUpdatePath());
        return;
    }

    stageAndPromptInstall(asset->downloadUrl, asset->name);
}

void UpdateChecker::notifyReleaseLinkOnce()
{
    UpdateSettings settings(settingsStore_);
    if (!settings.shouldNotifyTag(pendingTag_)) {
        return;
    }
    settings.markNotifiedTag(pendingTag_);

    QMessageBox box(QMessageBox::Information, QStringLiteral("Update available"),
                    QStringLiteral("Version %1 is available.").arg(pendingVersion_));
    box.setInformativeText(
        QStringLiteral("Automatic install is not available for this install. Open the GitHub release to download it."));
    auto *openButton = box.addButton(QStringLiteral("Open release page"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("OK"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton *>(openButton)) {
        openReleaseUrl(pendingHtmlUrl_);
    }
}

void UpdateChecker::stageAndPromptInstall(const QString &downloadUrl, const QString &assetName)
{
    const QString dir = cacheUpdatesDir();
    QDir().mkpath(dir);
    pendingDownloadPath_ = dir + QLatin1Char('/') + assetName;
    QNetworkRequest request{QUrl(downloadUrl)};
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("MatrixMediaArchiverQt/%1").arg(QCoreApplication::applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleDownloadReply(reply); });
}

void UpdateChecker::handleDownloadReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        return;
    }

    QFile file(pendingDownloadPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        notifyReleaseLinkOnce();
        return;
    }
    file.write(reply->readAll());
    file.close();

    const QString extractDir = cacheUpdatesDir() + QStringLiteral("/staged-") + pendingVersion_;
    QDir(extractDir).removeRecursively();
    QDir().mkpath(extractDir);
    if (!extractZip(pendingDownloadPath_, extractDir)) {
        notifyReleaseLinkOnce();
        return;
    }

#ifdef Q_OS_WIN
    const QString staged = extractDir;
    if (findFirstFile(staged, {QStringLiteral("MatrixMediaArchiverQt.exe")}).isEmpty()) {
        notifyReleaseLinkOnce();
        return;
    }
#else
    const QString staged = findFirstFile(extractDir, {QStringLiteral("*.AppImage")});
    if (staged.isEmpty()) {
        notifyReleaseLinkOnce();
        return;
    }
    QFile::setPermissions(
        staged,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
            | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
#endif

    UpdateSettings settings(settingsStore_);
    settings.setStagedUpdate(staged, pendingVersion_, false);
    promptRestart(staged);
}

void UpdateChecker::promptRestart(const QString &stagedPath)
{
    QMessageBox box(QMessageBox::Information, QStringLiteral("Update ready"),
                    QStringLiteral("Version %1 is ready to install.").arg(pendingVersion_));
    box.setInformativeText(QStringLiteral("Restart now to apply the update, or later to install it when the app quits."));
    auto *restartButton = box.addButton(QStringLiteral("Restart now"), QMessageBox::AcceptRole);
    auto *laterButton = box.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(restartButton);
    box.exec();

    UpdateSettings settings(settingsStore_);
    if (box.clickedButton() == static_cast<QAbstractButton *>(laterButton)) {
        settings.setStagedUpdate(stagedPath, pendingVersion_, true);
        return;
    }
    startHelperAndQuit(stagedPath);
}

bool UpdateChecker::spawnApplyHelper(const QString &stagedPath)
{
    const QString dest = destinationPath();
    if (dest.isEmpty() || !pathIsWritable(dest)) {
        return false;
    }

    const QString helperDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(helperDir);
#ifdef Q_OS_WIN
    const QString helperPath = helperDir + QStringLiteral("/mma-apply-update.cmd");
    QFile helper(helperPath);
    if (!helper.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    helper.write("@echo off\r\n");
    helper.write("set \"PID=%~1\"\r\n");
    helper.write("set \"STAGED=%~2\"\r\n");
    helper.write("set \"DEST=%~3\"\r\n");
    helper.write(":wait\r\n");
    helper.write("tasklist /FI \"PID eq %PID%\" | find \"%PID%\" >nul\r\n");
    helper.write("if not errorlevel 1 (\r\n");
    helper.write("  timeout /t 1 /nobreak >nul\r\n");
    helper.write("  goto wait\r\n");
    helper.write(")\r\n");
    helper.write("timeout /t 1 /nobreak >nul\r\n");
    helper.write("xcopy /E /Y /I /Q \"%STAGED%\\*\" \"%DEST%\\\" >nul\r\n");
    helper.write("start \"\" \"%DEST%\\MatrixMediaArchiverQt.exe\"\r\n");
    helper.close();

    const qint64 pid = QCoreApplication::applicationPid();
    return QProcess::startDetached(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"), helperPath, QString::number(pid), QDir::toNativeSeparators(stagedPath),
         QDir::toNativeSeparators(dest)});
#else
    const QString helperPath = helperDir + QStringLiteral("/mma-apply-update.sh");
    QFile helper(helperPath);
    if (!helper.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    helper.write("#!/bin/sh\n");
    helper.write("pid=\"$1\"\n");
    helper.write("staged=\"$2\"\n");
    helper.write("dest=\"$3\"\n");
    helper.write("while kill -0 \"$pid\" 2>/dev/null; do sleep 0.2; done\n");
    helper.write("sleep 0.4\n");
    helper.write("tmp=\"$dest.new.$$\"\n");
    helper.write("cp -f \"$staged\" \"$tmp\" && chmod +x \"$tmp\" && mv -f \"$tmp\" \"$dest\"\n");
    helper.write("exec \"$dest\" &\n");
    helper.close();
    QFile::setPermissions(
        helperPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const qint64 pid = QCoreApplication::applicationPid();
    return QProcess::startDetached(QStringLiteral("/bin/sh"), {helperPath, QString::number(pid), stagedPath, dest});
#endif
}

void UpdateChecker::startHelperAndQuit(const QString &stagedPath)
{
    if (!spawnApplyHelper(stagedPath)) {
        notifyReleaseLinkOnce();
        return;
    }
    UpdateSettings settings(settingsStore_);
    settings.clearStagedUpdate();
    QCoreApplication::quit();
}

bool UpdateChecker::destinationWritable() const
{
    return pathIsWritable(destinationPath());
}

QString UpdateChecker::destinationPath() const
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath();
#else
    if (runningAsAppImage()) {
        return envAppImagePath();
    }
    return {};
#endif
}

QString UpdateChecker::cacheUpdatesDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/MatrixMediaArchiverQt");
    }
    return base + QStringLiteral("/updates");
}

QString UpdateChecker::matchingAssetName(const QString &version) const
{
#ifdef Q_OS_WIN
    return windowsZipAssetName(version, hostArch());
#else
    return linuxAppImageZipAssetName(version, hostArch());
#endif
}

QString UpdateChecker::hostArch() const
{
#ifdef Q_OS_WIN
    return normalizeWindowsArch(QSysInfo::currentCpuArchitecture());
#else
    return normalizeLinuxArch(QSysInfo::currentCpuArchitecture());
#endif
}

bool UpdateChecker::runningAsAppImage() const
{
    return !envAppImagePath().isEmpty() && QFileInfo::exists(envAppImagePath());
}

std::unique_ptr<AppUpdater> createAppUpdater(QObject *parent)
{
    return std::unique_ptr<AppUpdater>(new UpdateChecker(parent));
}
