#include "applog.h"

#include <QTest>

class TestAppLog : public QObject
{
	Q_OBJECT
private slots:
	void warning_line_has_level_category_and_location();
	void debug_line_omits_location();
	void unknown_category_falls_back_to_default();
};

void TestAppLog::warning_line_has_level_category_and_location()
{
	QMessageLogContext ctx("scanner.cpp", 42, "doScan", "mediamuster.scanner");
	const QString line = AppLog::formatMessage(QtWarningMsg, ctx, QStringLiteral("disk full"));

	QVERIFY(line.contains(QStringLiteral(" W [mediamuster.scanner] disk full")));
	QVERIFY(line.contains(QStringLiteral("scanner.cpp:42")));
	QVERIFY(line.endsWith(QLatin1Char('\n')));
}

void TestAppLog::debug_line_omits_location()
{
	QMessageLogContext ctx("x.cpp", 7, "f", "mediamuster.app");
	const QString line = AppLog::formatMessage(QtDebugMsg, ctx, QStringLiteral("hello"));

	QVERIFY(line.contains(QStringLiteral(" D [mediamuster.app] hello")));
	QVERIFY(!line.contains(QStringLiteral("x.cpp")));
}

void TestAppLog::unknown_category_falls_back_to_default()
{
	QMessageLogContext ctx;
	const QString line = AppLog::formatMessage(QtInfoMsg, ctx, QStringLiteral("plain qInfo"));

	QVERIFY(line.contains(QStringLiteral(" I [default] plain qInfo")));
}

QTEST_APPLESS_MAIN(TestAppLog)
#include "tst_applog.moc"