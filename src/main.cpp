#include "mainwindow.h"
#include "mediafile.h"
#include "version.h"
#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    qRegisterMetaType<MediaFile>("MediaFile");
    qRegisterMetaType<QVector<MediaFile>>("QVector<MediaFile>");
    qRegisterMetaType<DriveInfo>("DriveInfo");
    qRegisterMetaType<QVector<DriveInfo>>("QVector<DriveInfo>");

    QApplication app(argc, argv);

    app.setApplicationName(APP_NAME);
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("Martin McLean");
    app.setOrganizationDomain("com.McLean.MediaMuster");

    // About Dialog icon. Dock icon comes from the icns/ico.
    app.setWindowIcon(QIcon(":/res/mediamuster.png"));

#ifdef Q_OS_MAC
    app.setStyle(QStyleFactory::create("macos"));
#elif defined(Q_OS_WIN)
    app.setStyle(QStyleFactory::create("windows"));
#endif

    MainWindow window;
    window.show();

    return app.exec();
}