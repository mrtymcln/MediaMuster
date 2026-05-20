#include "mainwindow.h"
#include "version.h"
#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	app.setApplicationName(APP_NAME);
	app.setApplicationVersion(APP_VERSION);
	app.setOrganizationName("Martin McLean");
	app.setOrganizationDomain("com.McLean.MediaMuster");

	// Dock icon comes from the bundled .icns/.ico; this only sets
	// the dialog-box icon.
	app.setWindowIcon(QIcon(":/res/mediamuster.png"));

#ifdef Q_OS_MAC
	app.setStyle(QStyleFactory::create("macos"));
#elif defined(Q_OS_WIN)
	app.setStyle(QStyleFactory::create("windows"));
#endif

	// MARK: - Run

	MainWindow window;
	window.show();
	return app.exec();
}