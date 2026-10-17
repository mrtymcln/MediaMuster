#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMutex>
#include <QOperatingSystemVersion>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QtLogging>

Q_LOGGING_CATEGORY(lcApp, "app")
Q_LOGGING_CATEGORY(lcAvb, "avb")
Q_LOGGING_CATEGORY(lcMdb, "mdb")
Q_LOGGING_CATEGORY(lcMetadata, "metadata")
Q_LOGGING_CATEGORY(lcMxf, "mxf")
Q_LOGGING_CATEGORY(lcOmf, "omf")
Q_LOGGING_CATEGORY(lcPmr, "pmr")
Q_LOGGING_CATEGORY(lcScanner, "scanner")
Q_LOGGING_CATEGORY(lcVolumes, "volumes")

namespace
{
	constexpr char kSessionDivider[] = "============================================================";
	constexpr int kTimestampLength = 23;

	QString systemName()
	{
#ifdef Q_OS_MAC
		const auto version = QOperatingSystemVersion::current();
		return QStringLiteral("%1 %2.%3.%4")
			.arg(version.name())
			.arg(version.majorVersion())
			.arg(version.minorVersion())
			.arg(version.microVersion());
#else
		return QSysInfo::prettyProductName();
#endif
	}

	QStringList crashReportNames()
	{
		return {QStringLiteral("MediaMuster*.ips"), QStringLiteral("MediaMuster*.crash")};
	}

	QString trimLog(const QString &path, const QDateTime &cutoff)
	{
		const QFileInfo info(path);
		if (info.isSymLink() || (info.exists() && !info.isFile()))
			return QStringLiteral("Diagnostic log is not a regular file: %1").arg(path);
		if (!info.exists())
			return {};
		QFile input(path);
		if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
			return QStringLiteral("Cannot read diagnostic log: %1").arg(input.errorString());
		QSaveFile output(path);
		if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
			return QStringLiteral("Cannot prepare diagnostic log cleanup: %1").arg(output.errorString());
		QTextStream reader(&input);
		QTextStream writer(&output);
		while (!reader.atEnd())
		{
			const QString line = reader.readLine();
			if (line == QLatin1String(kSessionDivider))
				continue;
			const auto date = QDateTime::fromString(line.left(kTimestampLength), QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
			if (!date.isValid() || date >= cutoff)
				writer << line << '\n';
		}
		writer.flush();
		if (reader.status() != QTextStream::Ok || input.error() != QFileDevice::NoError)
			return QStringLiteral("Cannot finish reading diagnostic log: %1").arg(input.errorString());
		input.close();
		if (!output.commit())
			return QStringLiteral("Cannot save diagnostic log cleanup: %1").arg(output.errorString());
		return {};
	}

	// Keep these alive until process exit: Qt may log during static destruction.
	QFile *const g_file = new QFile;
	QMutex *const g_mutex = new QMutex;
	QtMessageHandler g_previousHandler = nullptr;
	bool g_installed = false;

	void writeRaw(const QByteArray &bytes)
	{
		QMutexLocker lock(g_mutex);
		if (!g_file->isOpen())
			return;
		g_file->write(bytes);
		g_file->flush();
	}

	void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
	{
		const auto now = QDateTime::currentDateTime();
		writeRaw(Diagnostics::formatMessage(type, ctx, msg, now).toUtf8());
		if (g_previousHandler)
			g_previousHandler(type, ctx, msg);
	}
} // namespace

QString Diagnostics::formatMessage(QtMsgType type, const QMessageLogContext &ctx, const QString &msg, const QDateTime &now)
{
	const char *severity = "unknown";
	switch (type)
	{
	case QtDebugMsg:
		severity = "debug";
		break;
	case QtInfoMsg:
		severity = "info";
		break;
	case QtWarningMsg:
		severity = "warning";
		break;
	case QtCriticalMsg:
		severity = "critical";
		break;
	case QtFatalMsg:
		severity = "fatal";
		break;
	}
	QString prefix = now.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) +
					 QLatin1Char(' ') + QLatin1String(severity) + QLatin1Char(' ');
	if (ctx.category && *ctx.category && qstrcmp(ctx.category, "default") != 0)
		prefix += QString::fromUtf8(ctx.category) + QStringLiteral(": ");
	QString text = msg;
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	QString formatted;
	for (const auto &part : text.split(QLatin1Char('\n')))
	{
		// The saved timestamp must not depend on QT_MESSAGE_PATTERN overrides.
		formatted += prefix + part;
		// Include the source location on each warning/error line.
		if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && ctx.file)
			formatted += QStringLiteral("  (%1:%2)").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);
		formatted += QLatin1Char('\n');
	}
	return formatted;
}

QString Diagnostics::cleanUp(const QString &directory, const QDateTime &now)
{
	if (directory.isEmpty() || !now.isValid())
		return QStringLiteral("Cannot determine the diagnostic cleanup location or date.");
	const QDir dir(directory);
	if (!dir.exists() && !QDir().mkpath(directory))
		return QStringLiteral("Cannot create diagnostic directory: %1").arg(directory);
	const auto cutoff = now.addDays(-30);
	QStringList issues;
	const auto logIssue = trimLog(dir.filePath(QStringLiteral("mediamuster.log")), cutoff);
	if (!logIssue.isEmpty())
		issues.append(logIssue);
	for (const auto &report : dir.entryInfoList(crashReportNames(), QDir::Files | QDir::NoSymLinks))
		if (report.lastModified().isValid() && report.lastModified() < cutoff && !QFile::remove(report.absoluteFilePath()))
			issues.append(QStringLiteral("Cannot remove expired crash report: %1").arg(report.absoluteFilePath()));
	return issues.join(QLatin1Char('\n'));
}

void Diagnostics::install(const QString &directory, const QDateTime &now)
{
	if (g_installed) // a second install would chain our handler to itself and loop
		return;
	g_installed = true;

	QString dir = directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : directory;
	if (dir.isEmpty())
		dir = QDir::homePath() + QStringLiteral("/.mediamuster");
	const QString issue = cleanUp(dir, now);

	const QString path = dir + QStringLiteral("/mediamuster.log");
	g_file->setFileName(path);
	const QFileInfo info(path);
	if (!info.isSymLink() && (!info.exists() || info.isFile()))
		g_file->open(QIODevice::Append | QIODevice::Text);

	// Record the App and System details for this session.
	const QString details = QStringLiteral(
								"%1 %2 (Qt %3)\n"
								"%4 (%5)\n"
								"%6\n"
								"%7\n"
								"%8")
								.arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion(),
									 QString::fromLatin1(qVersion()), systemName(), QSysInfo::currentCpuArchitecture(),
									 QSysInfo::machineHostName(), QLocale::system().name(), path);
	const QMessageLogContext context(nullptr, 0, nullptr, "app");
	const QString header = QStringLiteral("%1\n%2%1\n")
							   .arg(QString::fromLatin1(kSessionDivider), formatMessage(QtInfoMsg, context, details, now));
	writeRaw(header.toUtf8());
	if (!issue.isEmpty())
		writeRaw(formatMessage(QtWarningMsg, context, issue, now).toUtf8());

	g_previousHandler = qInstallMessageHandler(messageHandler);
}

QString Diagnostics::logPath()
{
	QMutexLocker lock(g_mutex);
	return g_file->fileName();
}

void Diagnostics::appendConsoleLine(QtMsgType level, const QString &module, const QString &message, const QDateTime &now)
{
	const QByteArray category = module.toUtf8();
	const QMessageLogContext context(nullptr, 0, nullptr, category.constData());
	writeRaw(formatMessage(level, context, message, now).toUtf8());
}

QString Diagnostics::systemCrashReportsDir()
{
#ifdef Q_OS_MAC
	return QDir::homePath() + QStringLiteral("/Library/Logs/DiagnosticReports");
#else
	return QString();
#endif
}

QStringList Diagnostics::collectCrashReports(const QString &reportsDir, const QString &logsDir, const QDateTime &now)
{
	QStringList collected;

	// An empty path would make QDir search the current working directory.
	if (reportsDir.isEmpty() || logsDir.isEmpty() || !now.isValid())
		return collected;
	QDir src(reportsDir);
	if (!src.exists())
		return collected;

	QDir().mkpath(logsDir);
	// Only collect crash reports from the last 30 days.
	const QDateTime cutoff = now.addDays(-30);
	for (const QString &name : src.entryList(crashReportNames(), QDir::Files | QDir::NoSymLinks, QDir::Name))
	{
		const QFileInfo fi(src.filePath(name));
		if (fi.lastModified() < cutoff)
			continue;
		const QString dest = logsDir + QLatin1Char('/') + name;
		if (QFile::copy(fi.filePath(), dest))
		{
			// Copy time must not restart an older report's 30-day lifetime.
			if (QFileInfo(dest).lastModified() != fi.lastModified())
			{
				QFile copy(dest);
				if (!copy.open(QIODevice::ReadOnly) || !copy.setFileTime(fi.lastModified(), QFileDevice::FileModificationTime))
					qCWarning(lcApp) << "Cannot preserve crash-report age:" << dest << copy.errorString();
			}
			collected << dest;
		}
	}
	return collected;
}
