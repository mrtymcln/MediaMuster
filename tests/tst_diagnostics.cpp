#include "diagnostics.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	void writeReport(const QString &path, const QDateTime &mtime = {})
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		{
			QFile f(path);
			f.open(QIODevice::WriteOnly);
			f.write("crash");
			f.close();
		}
		if (mtime.isValid())
		{
			QFile f(path);
			if (f.open(QIODevice::ReadWrite))
			{
				f.setFileTime(mtime, QFileDevice::FileModificationTime);
				f.close();
			}
		}
	}
} // namespace

class TestDiagnostics : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();
	void warning_line_has_level_category_and_location();
	void debug_line_omits_location();
	void default_category_omits_prefix();
	void collects_recent_mediamuster_reports();
	void ignores_other_apps_and_old_reports();
	void dedups_on_second_run();
	void missing_reports_dir_is_noop();
};

void TestDiagnostics::initTestCase()
{
	// Exercise the Qt formatter and source suffix without a clock-dependent prefix.
	qSetMessagePattern(QStringLiteral("%{type} %{if-category}%{category}: %{endif}%{message}"));
}

void TestDiagnostics::warning_line_has_level_category_and_location()
{
	QMessageLogContext ctx("scanner.cpp", 42, "doScan", "mediamuster.scanner");
	const QString line = Diagnostics::formatMessage(QtWarningMsg, ctx, QStringLiteral("disk full"));

	QCOMPARE(line, QStringLiteral("warning mediamuster.scanner: disk full  (scanner.cpp:42)\n"));
}

void TestDiagnostics::debug_line_omits_location()
{
	QMessageLogContext ctx("x.cpp", 7, "f", "mediamuster.app");
	const QString line = Diagnostics::formatMessage(QtDebugMsg, ctx, QStringLiteral("hello"));

	QCOMPARE(line, QStringLiteral("debug mediamuster.app: hello\n"));
}

void TestDiagnostics::default_category_omits_prefix()
{
	QMessageLogContext ctx;
	const QString line = Diagnostics::formatMessage(QtInfoMsg, ctx, QStringLiteral("plain qInfo"));

	QCOMPARE(line, QStringLiteral("info plain qInfo\n"));
}

void TestDiagnostics::collects_recent_mediamuster_reports()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString reports = tmp.path() + QStringLiteral("/DiagnosticReports");
	const QString logs = tmp.path() + QStringLiteral("/logs");

	writeReport(reports + QStringLiteral("/MediaMuster-2026-05-31-141233.ips"));

	const QStringList got = Diagnostics::collectCrashReports(reports, logs);
	QCOMPARE(got.size(), 1);
	QVERIFY(QFile::exists(logs + QStringLiteral("/MediaMuster-2026-05-31-141233.ips")));
}

void TestDiagnostics::ignores_other_apps_and_old_reports()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString reports = tmp.path() + QStringLiteral("/DiagnosticReports");
	const QString logs = tmp.path() + QStringLiteral("/logs");

	writeReport(reports + QStringLiteral("/SomeOtherApp-2026-05-31.ips")); // not ours
	writeReport(reports + QStringLiteral("/MediaMuster-old.ips"),
				QDateTime::currentDateTime().addDays(-90)); // too old

	const QStringList got = Diagnostics::collectCrashReports(reports, logs);
	QVERIFY(got.isEmpty());
	QVERIFY(QDir(logs).entryList(QDir::Files).isEmpty());
}

void TestDiagnostics::dedups_on_second_run()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString reports = tmp.path() + QStringLiteral("/DiagnosticReports");
	const QString logs = tmp.path() + QStringLiteral("/logs");
	writeReport(reports + QStringLiteral("/MediaMuster-2026-05-31-141233.ips"));

	QCOMPARE(Diagnostics::collectCrashReports(reports, logs).size(), 1);
	QVERIFY(Diagnostics::collectCrashReports(reports, logs).isEmpty()); // already have it
}

void TestDiagnostics::missing_reports_dir_is_noop()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(Diagnostics::collectCrashReports(QString(), tmp.path()).isEmpty());
	QVERIFY(Diagnostics::collectCrashReports(tmp.path() + QStringLiteral("/nope"),
											 tmp.path() + QStringLiteral("/logs"))
				.isEmpty());
}

QTEST_APPLESS_MAIN(TestDiagnostics)
#include "tst_diagnostics.moc"
