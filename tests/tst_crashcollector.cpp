#include "crashcollector.h"

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

class TestCrashCollector : public QObject
{
	Q_OBJECT
private slots:
	void collects_recent_mediamuster_reports();
	void ignores_other_apps_and_old_reports();
	void dedups_on_second_run();
	void missing_reports_dir_is_noop();
};

void TestCrashCollector::collects_recent_mediamuster_reports()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString reports = tmp.path() + QStringLiteral("/DiagnosticReports");
	const QString logs = tmp.path() + QStringLiteral("/logs");

	writeReport(reports + QStringLiteral("/MediaMuster-2026-05-31-141233.ips"));

	const QStringList got = CrashCollector::collect(reports, logs);
	QCOMPARE(got.size(), 1);
	QVERIFY(QFile::exists(logs + QStringLiteral("/MediaMuster-2026-05-31-141233.ips")));
}

void TestCrashCollector::ignores_other_apps_and_old_reports()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString reports = tmp.path() + QStringLiteral("/DiagnosticReports");
	const QString logs = tmp.path() + QStringLiteral("/logs");

	writeReport(reports + QStringLiteral("/SomeOtherApp-2026-05-31.ips")); // not ours
	writeReport(reports + QStringLiteral("/MediaMuster-old.ips"),
				QDateTime::currentDateTime().addDays(-90)); // too old

	const QStringList got = CrashCollector::collect(reports, logs);
	QVERIFY(got.isEmpty());
	QVERIFY(QDir(logs).entryList(QDir::Files).isEmpty());
}

void TestCrashCollector::dedups_on_second_run()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString reports = tmp.path() + QStringLiteral("/DiagnosticReports");
	const QString logs = tmp.path() + QStringLiteral("/logs");
	writeReport(reports + QStringLiteral("/MediaMuster-2026-05-31-141233.ips"));

	QCOMPARE(CrashCollector::collect(reports, logs).size(), 1);
	QVERIFY(CrashCollector::collect(reports, logs).isEmpty()); // already have it
}

void TestCrashCollector::missing_reports_dir_is_noop()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(CrashCollector::collect(QString(), tmp.path()).isEmpty());
	QVERIFY(CrashCollector::collect(tmp.path() + QStringLiteral("/nope"),
									tmp.path() + QStringLiteral("/logs"))
				.isEmpty());
}

QTEST_APPLESS_MAIN(TestCrashCollector)
#include "tst_crashcollector.moc"