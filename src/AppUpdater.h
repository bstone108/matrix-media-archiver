#pragma once

#include <QObject>
#include <memory>

class AppUpdater
{
public:
    virtual ~AppUpdater() = default;

    virtual void start() = 0;
    virtual void checkNow(bool userInitiated) = 0;
    virtual void installPendingOnQuit() = 0;
};

std::unique_ptr<AppUpdater> createAppUpdater(QObject *parent = nullptr);
