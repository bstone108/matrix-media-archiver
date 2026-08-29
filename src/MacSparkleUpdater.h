#pragma once

#include "AppUpdater.h"

#include <QObject>

class MacSparkleUpdater final : public QObject, public AppUpdater
{
public:
    explicit MacSparkleUpdater(QObject *parent = nullptr);
    ~MacSparkleUpdater() override;

    void start() override;
    void checkNow(bool userInitiated) override;
    void installPendingOnQuit() override;

private:
    void *bridge_ = nullptr;
};
