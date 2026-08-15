#include "applog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QMutex>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>

namespace
{
	constexpr int kRetentionDays = 30;

	// Heap-allocated and never freed on purpose: the handler can fire during
	// static teardown (a stray qWarning from another TU), and it can't be
	// touching an already-destroyed QFile/QMutex when it does.
	QFile *const g_file = new QFile;
	QMutex *const g_mutex = new QMutex;
	QtMessageHandler g_prevHandler = nullptr;
	bool g_installed = false;
	QString g_path;

	QChar levelLetter(QtMsgType t)
	{
		switch (t)
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
		if (!g_file->isOpen())
			return;
		g_file->write(bytes);
		g_file->flush();
	}

	void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
	{
		const QByteArray line = AppLog::formatMessage(type, ctx, msg).toUtf8();
		{
			QMutexLocker lock(g_mutex);
			writeRaw(line);
		}
		if (g_prevHandler)
			g_prevHandler(type, ctx, msg);
	}
} // namespace

QString AppLog::formatMessage(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
	QString line = QStringLiteral("%1 %2 [%3] %4")
					   .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
					   .arg(levelLetter(type))
					   .arg(QString::fromUtf8(ctx.category ? ctx.category : "default"), msg);

	// Only bother with warning messages and above. Ignore info messages.
	if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && ctx.file)
		line += QStringLiteral("  (%1:%2)").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);

	line += QLatin1Char('\n');
	return line;
}

void AppLog::install()
{
	if (g_installed) // a second install would chain our handler to itself and loop
		return;
	g_installed = true;

	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (dir.isEmpty())
		dir = QDir::homePath() + QStringLiteral("/.mediamuster");
	QDir().mkpath(dir);

	g_path = dir + QStringLiteral("/mediamuster.log");

	// Older builds kept the log in a `logs/` subfolder. Move it up (keeping
	// history) and delete the folder, so the log now sits directly in the
	// app-data folder.
	const QString oldLogDir = dir + QStringLiteral("/logs");
	if (QDir(oldLogDir).exists())
	{
		const QString oldLog = oldLogDir + QStringLiteral("/mediamuster.log");
		if (QFile::exists(oldLog) && !QFile::exists(g_path))
			QFile::rename(oldLog, g_path);
		QDir(oldLogDir).removeRecursively();
	}

	// Wipe the file once it's older than the retention window. Keyed
	// on birth time, so removing it starts a fresh cycle.
	const QFileInfo fi(g_path);
	if (fi.exists())
	{
		const QDateTime born = fi.birthTime();
		if (born.isValid() && born.daysTo(QDateTime::currentDateTime()) >= kRetentionDays)
			QFile::remove(g_path);
	}

	g_file->setFileName(g_path);
	g_file->open(QIODevice::Append | QIODevice::Text);

	// Always on, always full detail, so there's no toggle to forget.
	QLoggingCategory::setFilterRules(QStringLiteral("mediamuster.*.debug=true"));

	// A header, so log messages in cold still says what ran it.
	const QString rule(56, QLatin1Char('='));
	QStringList h;
	h << QString() << rule;
	h << QStringLiteral("%1 %2  (Qt %3)")
			 .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion(),
				  QString::fromLatin1(qVersion()));
	h << QStringLiteral("started   %1")
			 .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
	h << QStringLiteral("os        %1").arg(QSysInfo::prettyProductName());
	h << QStringLiteral("kernel    %1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
	h << QStringLiteral("arch      %1").arg(QSysInfo::currentCpuArchitecture());
	h << QStringLiteral("host      %1").arg(QSysInfo::machineHostName());
	h << QStringLiteral("locale    %1").arg(QLocale::system().name());
	h << QStringLiteral("log       %1").arg(g_path);
	h << QStringLiteral("detail    full (mediamuster.*=debug) — wipes after %1 days")
			 .arg(kRetentionDays);
	h << rule << QString();
	const QByteArray header = (h.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
	{
		QMutexLocker lock(g_mutex);
		writeRaw(header);
	}

	g_prevHandler = qInstallMessageHandler(messageHandler);
}

QString AppLog::logPath()
{
	return g_path;
}

void AppLog::appendConsoleLine(QtMsgType level, const QString &module, const QString &message)
{
	// Reuse the handler's own letter map so the console tee and the raw
	// qC* lines share one severity column in the file.
	const QString line = QStringLiteral("%1 %2 [console/%3] %4\n")
							 .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
							 .arg(levelLetter(level))
							 .arg(module, message);
	QMutexLocker lock(g_mutex);
	writeRaw(line.toUtf8());
}