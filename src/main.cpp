#include <QApplication>
#include "MainWindow.h"
#include "FfprobeAnalyzer.h"

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Analyze Audio Bitrate"));
    QApplication::setOrganizationName(QStringLiteral("teaching-droid"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    qRegisterMetaType<FileResult>("FileResult");

    MainWindow w;
    w.show();
    return app.exec();
}
