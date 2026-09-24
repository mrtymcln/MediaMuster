#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMutex>
#include <QStandardPaths>
#include <QSysInfo>

Q_LOGGING_CATEGORY(lcAvb, "mediamuster.avb", QtWarningMsg)
Q_LOGGING_CATEGORY(lcMdb, "mediamuster.mdb", QtWarningMsg)
Q_LOGGING_CATEGORY(lcMetadata, "mediamuster.metadata", QtWarningMsg)
Q_LOGGING_CATEGORY(lcMxf, "mediamuster.mxf", QtWarningMsg)
Q_LOGGING_CATEGORY(lcOmf, "mediamuster.omf", QtWarningMsg)
Q_LOGGING_CATEGORY(lcPmr, "mediamuster.pmr", QtWarningMsg)
Q_LOGGING_CATEGORY(lcScanner, "mediamuster.scanner", QtWarningMsg)
Q_LOGGING_CATEGORY(lcVolume, "mediamuster.volume", QtWarningMsg)
Q_LOGGING_CATEGORY(lcWorker, "mediamuster.worker", QtWarningMsg)

namespace
{
	constexpr int kRetentionDays = 30;

	// Keep these alive until process exit: Qt may log during static destruction.
	QFile *const g_file = new QFile;
	QMutex *const g_mutex = new QMutex;
	QtMessageHandler g_previousHandler = nullptr;
	bool g_installed = false;
	QString g_path;

	QChar levelLetter(QtMsgType level)
	{
		switch (level)
		{
		case QtDebugMsg:
			return QLatin1Char('D');
		case QtInfoMsg:
			return QLatin1Char('I');
		case QtWarningMsg:
			return QLatin1Char('W');
		case QtCriticalMsg:
			return QLatin1Char('E');
		case QtFatalMsg:
			return QLatin1Char('F');
		}
		return QLatin1Char('?');
	}

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
		writeRaw(Diagnostics::formatMessage(type, ctx, msg).toUtf8());
		if (g_previousHandler)
			g_previousHandler(type, ctx, msg);
	}
} // namespace

QString Diagnostics::formatMessage(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
	QString line = QStringLiteral("%1 %2 [%3] %4")
					   .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
					   .arg(levelLetter(type))
					   .arg(QString::fromUtf8(ctx.category ? ctx.category : "default"), msg);

	// Include the source location for warnings, errors and fatal messages.
	if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && ctx.file)
		line += QStringLiteral("  (%1:%2)").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);

	line += QLatin1Char('\n');
	return line;
}

void Diagnostics::install()
{
	if (g_installed) // a second install would chain our handler to itself and loop
		return;
	g_installed = true;

	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (dir.isEmpty())
		dir = QDir::homePath() + QStringLiteral("/.mediamuster");
	QDir().mkpath(dir);

	g_path = dir + QStringLiteral("/mediamuster.log");

	// At startup, clear the log if it was created at least 30 days ago.
	const QFileInfo fi(g_path);
	if (fi.exists())
	{
		const QDateTime born = fi.birthTime();
		if (born.isValid() && born.daysTo(QDateTime::currentDateTime()) >= kRetentionDays)
			QFile::remove(g_path);
	}

	g_file->setFileName(g_path);
	g_file->open(QIODevice::Append | QIODevice::Text);

	// Enable every level of MediaMuster diagnostic messages.
	QLoggingCategory::setFilterRules(QStringLiteral("mediamuster.*=true"));

	// Record the app version and system details for this session.
	const QString separator(56, QLatin1Char('='));
	QStringList headerLines;
	headerLines << QString() << separator;
	headerLines << QStringLiteral("%1 %2  (Qt %3)")
					   .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion(),
							QString::fromLatin1(qVersion()));
	headerLines << QStringLiteral("started   %1")
					   .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
	headerLines << QStringLiteral("os        %1").arg(QSysInfo::prettyProductName());
	headerLines << QStringLiteral("kernel    %1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
	headerLines << QStringLiteral("arch      %1").arg(QSysInfo::currentCpuArchitecture());
	headerLines << QStringLiteral("host      %1").arg(QSysInfo::machineHostName());
	headerLines << QStringLiteral("locale    %1").arg(QLocale::system().name());
	headerLines << QStringLiteral("log       %1").arg(g_path);
	headerLines << QStringLiteral("detail    all levels (mediamuster.*=true)");
	headerLines << QStringLiteral("retention cleared at startup when at least %1 days old")
					   .arg(kRetentionDays);
	headerLines << separator << QString();
	writeRaw((headerLines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());

	g_previousHandler = qInstallMessageHandler(messageHandler);
}

QString Diagnostics::logPath()
{
	return g_path;
}

void Diagnostics::appendConsoleLine(QtMsgType level, const QString &module, const QString &message)
{
	const QByteArray category = QByteArrayLiteral("console/") + module.toUtf8();
	const QMessageLogContext context(nullptr, 0, nullptr, category.constData());
	writeRaw(formatMessage(level, context, message).toUtf8());
}

QString Diagnostics::systemCrashReportsDir()
{
#ifdef Q_OS_MAC
	return QDir::homePath() + QStringLiteral("/Library/Logs/DiagnosticReports");
#else
	return QString();
#endif
}

QStringList Diagnostics::collectCrashReports(const QString &reportsDir, const QString &logsDir,
											 int maxAgeDays)
{
	QStringList collected;

	// An empty path would make QDir search the current working directory.
	if (reportsDir.isEmpty())
		return collected;
	QDir src(reportsDir);
	if (!src.exists())
		return collected;

	QDir().mkpath(logsDir);
	const QDateTime cutoff = QDateTime::currentDateTime().addDays(-maxAgeDays);
	const QStringList globs{QStringLiteral("MediaMuster*.ips"),	   // macOS 12 and later
							QStringLiteral("MediaMuster*.crash")}; // macOS 11
	for (const QString &name : src.entryList(globs, QDir::Files, QDir::Name))
	{
		const QFileInfo fi(src.filePath(name));
		if (fi.lastModified() < cutoff)
			continue;
		const QString dest = logsDir + QLatin1Char('/') + name;
		// QFile::copy leaves existing reports untouched.
		if (QFile::copy(fi.filePath(), dest))
			collected << dest;
	}
	return collected;
}
