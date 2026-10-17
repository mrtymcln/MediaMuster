#include "diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	QDateTime testNow()
	{
		return QDateTime::fromString(QStringLiteral("2026-10-17T12:00:00.000"), Qt::ISODateWithMs);
	}

	bool writeBytes(const QString &path, const QByteArray &bytes, const QDateTime &mtime = {})
	{
		QFile file(path);
		return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
			   file.write(bytes) == bytes.size() && file.flush() &&
			   (!mtime.isValid() || file.setFileTime(mtime, QFileDevice::FileModificationTime));
	}

	QByteArray readBytes(const QString &path)
	{
		QFile file(path);
		return file.open(QIODevice::ReadOnly | QIODevice::Text) ? file.readAll() : QByteArray{};
	}

	QByteArray logLine(const QDateTime &time, const QByteArray &message)
	{
		return time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")).toUtf8() +
			   " info scanner: " + message + '\n';
	}
} // namespace

class TestDiagnostics : public QObject
{
	Q_OBJECT
private slots:
	void formats_messages_and_multiline_warnings();
	void cleanup_keeps_recent_content_and_protects_invalid_targets();
	void installed_logger_records_startup_console_and_qt_messages();
	void crash_reports_keep_their_age_and_leave_neighbours_untouched();
};

void TestDiagnostics::formats_messages_and_multiline_warnings()
{
	const QMessageLogContext context("scanner.cpp", 42, "scan", "scanner");
	QCOMPARE(Diagnostics::formatMessage(QtWarningMsg, context, QStringLiteral("first\r\nsecond"), testNow()),
			 QStringLiteral("2026-10-17 12:00:00.000 warning scanner: first  (scanner.cpp:42)\n"
							"2026-10-17 12:00:00.000 warning scanner: second  (scanner.cpp:42)\n"));
	QCOMPARE(Diagnostics::formatMessage(QtInfoMsg, context, QStringLiteral("ready"), testNow()),
			 QStringLiteral("2026-10-17 12:00:00.000 info scanner: ready\n"));
	QCOMPARE(Diagnostics::formatMessage(QtInfoMsg, QMessageLogContext{}, QStringLiteral("Qt message"), testNow()),
			 QStringLiteral("2026-10-17 12:00:00.000 info Qt message\n"));
}

void TestDiagnostics::cleanup_keeps_recent_content_and_protects_invalid_targets()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString log = tmp.filePath(QStringLiteral("mediamuster.log"));
	const QByteArray divider = QByteArray(60, '=') + '\n';
	const QByteArray boundary = logLine(testNow().addDays(-30), "exact boundary retained");
	const QByteArray recent = logLine(testNow(), "recent retained");
	const QByteArray unknown("undated note retained\n");
	QVERIFY(writeBytes(log, divider + logLine(testNow().addDays(-31), "expired") + divider + boundary + recent + unknown));
	QCOMPARE(Diagnostics::cleanUp(tmp.path(), testNow()), QString{});
	QCOMPARE(readBytes(log), boundary + recent + unknown);

	const QString blocked = tmp.filePath(QStringLiteral("blocked"));
	const QString sentinel = blocked + QStringLiteral("/mediamuster.log/keep.txt");
	QVERIFY(writeBytes(sentinel, QByteArray("directory is not a log")));
	QVERIFY(!Diagnostics::cleanUp(blocked, testNow()).isEmpty());
	QCOMPARE(readBytes(sentinel), QByteArray("directory is not a log"));
}

void TestDiagnostics::installed_logger_records_startup_console_and_qt_messages()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString log = tmp.filePath(QStringLiteral("mediamuster.log"));
	const QDateTime now = QDateTime::currentDateTime();
	const QByteArray recent = logLine(now.addDays(-1), "earlier session retained");
	const QByteArray divider = QByteArray(60, '=') + '\n';
	QVERIFY(writeBytes(log, divider + logLine(now.addDays(-31), "expired session") + divider + recent));
	QProcess child;
	child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--logger-smoke"), tmp.path()});
	QVERIFY2(child.waitForStarted(5000), qPrintable(child.errorString()));
	const bool finished = child.waitForFinished(10000);
	if (!finished)
	{
		child.kill();
		child.waitForFinished(3000);
	}
	QVERIFY2(finished, "The installed logger did not finish writing.");
	QCOMPARE(child.exitStatus(), QProcess::NormalExit);
	QCOMPARE(child.exitCode(), 0);
	auto lines = readBytes(log).split('\n');
	QVERIFY(lines.takeLast().isEmpty());
	QCOMPARE(lines.size(), 11);
	QCOMPARE(lines.at(0) + '\n', recent);
	QCOMPARE(lines.at(1) + '\n', divider);
	QCOMPARE(lines.at(7) + '\n', divider);
	const QByteArray prefix = lines.at(2).left(23) + " info app: ";
	QCOMPARE(lines.at(2), prefix + "MediaMuster diagnostic test test (Qt " + qVersion() + ')');
	QVERIFY(lines.at(3).startsWith(prefix));
	const QByteArray architecture = QByteArray(" (") + QSysInfo::currentCpuArchitecture().toUtf8() + ')';
	QVERIFY(lines.at(3).endsWith(architecture));
	QVERIFY(lines.at(3).size() > prefix.size() + architecture.size());
	QCOMPARE(lines.at(4), prefix + QSysInfo::machineHostName().toUtf8());
	QCOMPARE(lines.at(5), prefix + QLocale::system().name().toUtf8());
	QCOMPARE(lines.at(6), prefix + log.toUtf8());
	QVERIFY(lines.at(8).endsWith(" info scanner: Console message"));
	QVERIFY(lines.at(9).endsWith(" info scanner: continued"));
	QVERIFY(lines.at(10).endsWith(" info Qt message"));
	for (const QByteArray &line : lines)
	{
		if (line + '\n' == divider)
			continue;
		QVERIFY(QDateTime::fromString(QString::fromLatin1(line.left(23)),
									  QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
					.isValid());
	}
}

void TestDiagnostics::crash_reports_keep_their_age_and_leave_neighbours_untouched()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString reports = tmp.filePath(QStringLiteral("DiagnosticReports"));
	const QString logs = tmp.filePath(QStringLiteral("logs"));
	const QString recentName = QStringLiteral("MediaMuster-recent.ips");
	const QString boundaryName = QStringLiteral("MediaMuster-boundary.crash");
	const QDateTime recentTime = testNow().addDays(-29);
	QVERIFY(writeBytes(reports + '/' + recentName, QByteArray("recent report"), recentTime));
	QVERIFY(writeBytes(reports + '/' + boundaryName, QByteArray("boundary report"), testNow().addDays(-30)));
	QVERIFY(writeBytes(reports + QStringLiteral("/MediaMuster-expired.ips"), QByteArray("old"), testNow().addDays(-31)));
	QVERIFY(writeBytes(reports + QStringLiteral("/OtherApp.ips"), QByteArray("other"), testNow()));
	const QString expiredCopy = logs + QStringLiteral("/MediaMuster-expired.ips");
	const QString journal = logs + QStringLiteral("/journal/MediaMuster-recovery.ips");
	const QString neighbour = logs + QStringLiteral("/OtherApp.ips");
	QVERIFY(writeBytes(expiredCopy, QByteArray("old copy"), testNow().addDays(-31)));
	QVERIFY(writeBytes(journal, QByteArray("recovery"), testNow().addDays(-31)));
	QVERIFY(writeBytes(neighbour, QByteArray("unrelated"), testNow().addDays(-31)));

	QCOMPARE(Diagnostics::collectCrashReports(reports, logs, testNow()).size(), 2);
	QCOMPARE(readBytes(logs + '/' + recentName), QByteArray("recent report"));
	QCOMPARE(readBytes(logs + '/' + boundaryName), QByteArray("boundary report"));
	QCOMPARE(QFileInfo(logs + '/' + recentName).lastModified(), recentTime);
	QVERIFY(Diagnostics::collectCrashReports(reports, logs, testNow()).isEmpty());
	QCOMPARE(Diagnostics::cleanUp(logs, testNow()), QString{});
	QVERIFY(!QFile::exists(expiredCopy));
	QVERIFY(QFile::exists(logs + '/' + boundaryName));
	QCOMPARE(Diagnostics::cleanUp(logs, testNow().addDays(2)), QString{});
	QVERIFY(!QFile::exists(logs + '/' + recentName));
	QVERIFY(!QFile::exists(logs + '/' + boundaryName));
	QCOMPARE(readBytes(reports + '/' + recentName), QByteArray("recent report"));
	QCOMPARE(QFileInfo(reports + '/' + recentName).lastModified(), recentTime);
	QCOMPARE(readBytes(journal), QByteArray("recovery"));
	QCOMPARE(readBytes(neighbour), QByteArray("unrelated"));
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const QStringList arguments = app.arguments();
	if (arguments.size() == 3 && arguments[1] == QStringLiteral("--logger-smoke"))
	{
		QCoreApplication::setApplicationName(QStringLiteral("MediaMuster diagnostic test"));
		QCoreApplication::setApplicationVersion(QStringLiteral("test"));
		Diagnostics::install(arguments[2]);
		Diagnostics::appendConsoleLine(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Console message\ncontinued"));
		qInfo("Qt message");
		return 0;
	}
	TestDiagnostics test;
	return QTest::qExec(&test, argc, argv);
}
#include "tst_diagnostics.moc"
