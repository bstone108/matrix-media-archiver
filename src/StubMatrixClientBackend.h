#pragma once

#include "MatrixClientBackend.h"

class StubMatrixClientBackend final : public MatrixClientBackend
{
public:
    QString backendName() const override;
    bool isAvailable() const override;

    bool start(const AppSettings &settings, const QString &password, BotRuntimeSnapshot &runtime, QString &errorMessage) override;
    void stop(BotRuntimeSnapshot &runtime) override;

    bool joinRoom(const QString &roomIdOrAlias, QString &errorMessage) override;
    bool leaveRoom(const QString &roomId, QString &errorMessage) override;

    bool requestVerification(QString &errorMessage) override;
    bool startSasVerification(QString &errorMessage) override;
    bool approveVerification(QString &errorMessage) override;
    bool declineVerification(QString &errorMessage) override;
};

