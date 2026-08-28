#pragma once

#include "AppUpdater.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QSettings;
class QTimer;

class UpdateChecker final : public QObject, public AppUpdater
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    void start() override;
    void checkNow(bool userInitiated) override;
    void installPendingOnQuit() override;

private:
    enum class CheckKind {
        Automatic,
        Manual,
    };

    void requestLatestRelease();
    void handleLatestReply(QNetworkReply *reply);
    void handleDownloadReply(QNetworkReply *reply);
    void applyRelease();
    void notifyReleaseLinkOnce();
    void stageAndPromptInstall(const QString &downloadUrl, const QString &assetName);
    void promptRestart(const QString &stagedPath);
    bool spawnApplyHelper(const QString &stagedPath);
    void startHelperAndQuit(const QString &stagedPath);
    bool destinationWritable() const;
    QString destinationPath() const;
    QString cacheUpdatesDir() const;
    QString matchingAssetName(const QString &version) const;
    QString hostArch() const;
    bool runningAsAppImage() const;

    QNetworkAccessManager *network_ = nullptr;
    QTimer *intervalTimer_ = nullptr;
    QSettings *settingsStore_ = nullptr;
    CheckKind checkKind_ = CheckKind::Automatic;
    QByteArray latestBody_;
    QString pendingTag_;
    QString pendingVersion_;
    QString pendingHtmlUrl_;
    QString pendingAssetName_;
    QString pendingDownloadPath_;
};
