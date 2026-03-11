#include "SecretStore.h"

#include "AppPaths.h"

#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
QJsonObject readJsonObject(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists() && info.isSymLink()) {
        return {};
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object();
}

void writeJsonObject(const QString &path, const QJsonObject &object)
{
    const QFileInfo info(path);
    if (info.exists() && info.isSymLink() && !QFile::remove(path)) {
        return;
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        return;
    }
#ifdef Q_OS_UNIX
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
}
}

SecretStore::SecretStore(const AppPaths &paths)
    : filePath_(paths.secretStorePath())
{
}

QString SecretStore::loadPassword() const
{
    return readJsonObject(filePath_).value(QStringLiteral("password")).toString();
}

void SecretStore::savePassword(const QString &password) const
{
    QJsonObject object = readJsonObject(filePath_);
    object.insert(QStringLiteral("password"), password);
    writeJsonObject(filePath_, object);
}

StoredSession SecretStore::loadSession() const
{
    const QJsonObject sessionObject = readJsonObject(filePath_).value(QStringLiteral("session")).toObject();
    StoredSession session;
    session.accessToken = sessionObject.value(QStringLiteral("accessToken")).toString();
    session.refreshToken = sessionObject.value(QStringLiteral("refreshToken")).toString();
    session.userId = sessionObject.value(QStringLiteral("userId")).toString();
    session.deviceId = sessionObject.value(QStringLiteral("deviceId")).toString();
    session.homeserverUrl = sessionObject.value(QStringLiteral("homeserverUrl")).toString();
    session.oidcData = sessionObject.value(QStringLiteral("oidcData")).toString();
    session.slidingSyncVersion = sessionObject.value(QStringLiteral("slidingSyncVersion")).toString();
    return session;
}

void SecretStore::saveSession(const StoredSession &session) const
{
    QJsonObject object = readJsonObject(filePath_);
    QJsonObject sessionObject;
    sessionObject.insert(QStringLiteral("accessToken"), session.accessToken);
    sessionObject.insert(QStringLiteral("refreshToken"), session.refreshToken);
    sessionObject.insert(QStringLiteral("userId"), session.userId);
    sessionObject.insert(QStringLiteral("deviceId"), session.deviceId);
    sessionObject.insert(QStringLiteral("homeserverUrl"), session.homeserverUrl);
    sessionObject.insert(QStringLiteral("oidcData"), session.oidcData);
    sessionObject.insert(QStringLiteral("slidingSyncVersion"), session.slidingSyncVersion);
    object.insert(QStringLiteral("session"), sessionObject);
    writeJsonObject(filePath_, object);
}

void SecretStore::clearSession() const
{
    QJsonObject object = readJsonObject(filePath_);
    object.remove(QStringLiteral("session"));
    writeJsonObject(filePath_, object);
}
