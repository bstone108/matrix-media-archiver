#include "AppController.h"
#include "MainWindow.h"

#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>

extern "C" {
extern int __argc;
extern char **__argv;
}
#endif

namespace {

int runApplication(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("BrandonStone"));
    application.setApplicationName(QStringLiteral("MatrixMediaArchiverQt"));
    application.setApplicationVersion(QStringLiteral(MATRIX_MEDIA_ARCHIVER_VERSION_STRING));

    AppController controller;
    controller.initialize();

    MainWindow window(&controller);
    window.show();

    return application.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    return runApplication(argc, argv);
}

#ifdef Q_OS_WIN
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return runApplication(__argc, __argv);
}
#endif
