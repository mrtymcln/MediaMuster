#include "applog.h"
#include "mainwindow.h"
#include "version.h"
#include <QApplication>
#include <QDate>
#include <QIcon>
#include <QMessageBox>
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

	// Start the diagnostic log first, which needes the above info.
	AppLog::install();

#ifdef Q_OS_MAC
	app.setStyle(QStyleFactory::create("macos"));
#elif defined(Q_OS_WIN)
	app.setStyle(QStyleFactory::create("windows"));
#endif

#ifdef SELF_DESTRUCT
	const QDate expiry = QDate::fromString(SELF_DESTRUCT_DATE, Qt::ISODate);
	if (expiry.isValid() && QDate::currentDate() > expiry)
	{
		QMessageBox::critical(nullptr, "MediaMuster beta programme",
							  "This beta build expired on " SELF_DESTRUCT_DATE ".\n"
							  "Please download the latest version.");
		return 1;
	}
#endif

	MainWindow window;
	window.show();
	return app.exec();
}