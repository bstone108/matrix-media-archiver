#include "MacSparkleUpdater.h"

#import <Sparkle/Sparkle.h>

#include <QSysInfo>
#include <QString>

#include <CoreFoundation/CoreFoundation.h>

@interface MMASparkleBridge : NSObject <SPUUpdaterDelegate>
@property (nonatomic, strong) SPUStandardUpdaterController *controller;
@end

@implementation MMASparkleBridge

- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater
{
    Q_UNUSED(updater);
    const QString arch = QSysInfo::currentCpuArchitecture();
    const bool intel = arch.compare(QStringLiteral("x86_64"), Qt::CaseInsensitive) == 0
        || arch.compare(QStringLiteral("i386"), Qt::CaseInsensitive) == 0;
    const char *file = intel ? "appcast-macos-x86_64.xml" : "appcast-macos-arm64.xml";
    return [NSString stringWithFormat:
        @"https://github.com/bstone108/matrix-media-archiver/releases/latest/download/%s",
        file];
}

@end

MacSparkleUpdater::MacSparkleUpdater(QObject *parent)
    : QObject(parent)
{
}

MacSparkleUpdater::~MacSparkleUpdater()
{
    if (bridge_ != nullptr) {
        CFRelease(bridge_);
        bridge_ = nullptr;
    }
}

void MacSparkleUpdater::start()
{
    if (bridge_ != nullptr) {
        return;
    }
    MMASparkleBridge *bridge = [[MMASparkleBridge alloc] init];
    SPUStandardUpdaterController *controller =
        [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                     updaterDelegate:bridge
                                                  userDriverDelegate:nil];
    bridge.controller = controller;
    bridge_ = (void *)CFBridgingRetain(bridge);
}

void MacSparkleUpdater::checkNow(bool userInitiated)
{
    Q_UNUSED(userInitiated);
    if (bridge_ == nullptr) {
        start();
    }
    MMASparkleBridge *bridge = (__bridge MMASparkleBridge *)bridge_;
    [bridge.controller checkForUpdates:nil];
}

void MacSparkleUpdater::installPendingOnQuit()
{
}

std::unique_ptr<AppUpdater> createAppUpdater(QObject *parent)
{
    return std::unique_ptr<AppUpdater>(new MacSparkleUpdater(parent));
}
