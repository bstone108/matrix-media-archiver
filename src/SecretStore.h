#pragma once

#include "Domain.h"

#include <QString>

class AppPaths;

class SecretStore
{
public:
    explicit SecretStore(const AppPaths &paths);

    QString loadPassword() const;
    void savePassword(const QString &password) const;

    StoredSession loadSession() const;
    void saveSession(const StoredSession &session) const;
    void clearSession() const;

private:
    QString filePath_;
};

