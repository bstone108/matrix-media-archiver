#include "StubMatrixClientBackend.h"

namespace {
QString unavailableMessage()
{
    return QStringLiteral("The Qt port UI is in place, but the Rust-backed Matrix bridge has not been wired yet.");
}
}

QString StubMatrixClientBackend::backendName() const
{
    return QStringLiteral("stub");
}

bool StubMatrixClientBackend::isAvailable() const
{
    return false;
}

bool StubMatrixClientBackend::start(const AppSettings &settings, const QString &password, BotRuntimeSnapshot &runtime, QString &errorMessage)
{
    Q_UNUSED(settings);
    Q_UNUSED(password);
    runtime.connectionState = ConnectionState::Error;
    errorMessage = unavailableMessage();
    return false;
}

void StubMatrixClientBackend::stop(BotRuntimeSnapshot &runtime)
{
    runtime = BotRuntimeSnapshot {};
    runtime.connectionState = ConnectionState::Stopped;
}

bool StubMatrixClientBackend::joinRoom(const QString &roomIdOrAlias, QString &errorMessage)
{
    Q_UNUSED(roomIdOrAlias);
    errorMessage = unavailableMessage();
    return false;
}

bool StubMatrixClientBackend::leaveRoom(const QString &roomId, QString &errorMessage)
{
    Q_UNUSED(roomId);
    errorMessage = unavailableMessage();
    return false;
}

bool StubMatrixClientBackend::requestVerification(QString &errorMessage)
{
    errorMessage = unavailableMessage();
    return false;
}

bool StubMatrixClientBackend::startSasVerification(QString &errorMessage)
{
    errorMessage = unavailableMessage();
    return false;
}

bool StubMatrixClientBackend::approveVerification(QString &errorMessage)
{
    errorMessage = unavailableMessage();
    return false;
}

bool StubMatrixClientBackend::declineVerification(QString &errorMessage)
{
    errorMessage = unavailableMessage();
    return false;
}

