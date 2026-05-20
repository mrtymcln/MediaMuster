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

#ifdef SELF_DESTRUCT
	if (QDate::currentDate() > QDate::fromString(SELF_DESTRUCT_DATE, Qt::ISODate)) {
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