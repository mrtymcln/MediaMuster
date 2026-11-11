#include "support/diagnostics.h"
#include "ui/main_window.h"
#include "version.h"
#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);

	qRegisterMetaType<QtMsgType>();

	app.setApplicationName(APP_NAME);
	app.setApplicationVersion(APP_VERSION);
	app.setOrganizationName("Martin McLean");
	app.setOrganizationDomain("com.McLean.MediaMuster");
	app.setWindowIcon(QIcon(":/res/mediamuster.png"));

	Diagnostics::install();

#ifdef Q_OS_MAC
	app.setStyle(QStyleFactory::create("macos"));
#elif defined(Q_OS_WIN)
	app.setStyle(QStyleFactory::create("windows"));
#endif

	MainWindow window;
	window.show();
	return app.exec();
}
