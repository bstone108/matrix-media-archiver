#pragma once

#include "AppPaths.h"
#include "MatrixClientBackend.h"

#include <QHash>
#include <QObject>
#include <QProcess>

#include <memory>

class QJsonObject;

class ProcessMatrixClientBackend final : public QObject, public MatrixClientBackend
{
public:
    explicit ProcessMatrixClientBackend(const AppPaths &paths, QObject *parent = nullptr);
    ~ProcessMatrixClientBackend() override;

    QString backendName() const override;
    bool isAvailable() const override;
    void setRuntimeChangedCallback(RuntimeChangedCallback callback) override;
    void setBackendErrorCallback(BackendErrorCallback callback) override;

    bool start(const AppSettings &settings, const QString &password, BotRuntimeSnapshot &runtime, QString &errorMessage) override;
    bool stop(BotRuntimeSnapshot &runtime, QString &errorMessage) override;
    bool saveSettings(const AppSettings &settings, const QString &password, QString &errorMessage) override;
    bool resetHistoryScans(QString &errorMessage) override;

    bool joinRoom(const QString &roomIdOrAlias, QString &errorMessage) override;
    bool leaveRoom(const QString &roomId, QString &errorMessage) override;

    bool requestVerification(QString &errorMessage) override;
    bool startSasVerification(QString &errorMessage) override;
    bool approveVerification(QString &errorMessage) override;
    bool declineVerification(QString &errorMessage) override;

private:
    struct CommandResponse {
        bool ok = false;
        QString error;
    };

    QString backendExecutablePath() const;
    QString stderrSummary() const;
    bool ensureProcess(QString &errorMessage);
    bool launchProcess(QString &errorMessage);
    bool sendCommand(const QJsonObject &command, QString &errorMessage, int timeoutMs);
    bool commandRequiringRunningProcess(const QJsonObject &command, QString &errorMessage, int timeoutMs);
    void publishRuntime(const BotRuntimeSnapshot &runtime);
    void handleReadyReadStandardOutput();
    void handleReadyReadStandardError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessErrorOccurred(QProcess::ProcessError error);

    AppPaths paths_;
    std::unique_ptr<QProcess> process_;
    QHash<quint64, CommandResponse> completedResponses_;
    QByteArray stdoutBuffer_;
    QByteArray stderrBuffer_;
    quint64 nextCommandId_ = 1;
    RuntimeChangedCallback runtimeChangedCallback_;
    BackendErrorCallback backendErrorCallback_;
    BotRuntimeSnapshot latestRuntime_;
    QString lastProcessError_;
    bool explicitShutdown_ = false;
};
