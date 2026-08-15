#include "crashcollector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

QString CrashCollector::systemReportsDir()
{
#ifdef Q_OS_MAC
	return QDir::homePath() + QStringLiteral("/Library/Logs/DiagnosticReports");
#else
	return QString();
#endif
}

QStringList CrashCollector::collect(const QString &reportsDir, const QString &logsDir,
									int maxAgeDays)
{
	QStringList collected;

	// Empty dir == QDir(".") which exists, so screen that out first.
	if (reportsDir.isEmpty())
		return collected;
	QDir src(reportsDir);
	if (!src.exists())
		return collected;

	QDir().mkpath(logsDir);
	const QDateTime cutoff = QDateTime::currentDateTime().addDays(-maxAgeDays);
	const QStringList globs{QStringLiteral("MediaMuster*.ips"),	   // MacOS 12 and later
							QStringLiteral("MediaMuster*.crash")}; // MacOS 11
	for (const QString &name : src.entryList(globs, QDir::Files, QDir::Name))
	{
		const QFileInfo fi(src.filePath(name));
		if (fi.lastModified() < cutoff)
			continue; // ancient history
		const QString dest = logsDir + QLatin1Char('/') + name;
		if (QFile::exists(dest))
			continue; // already collected and notified
		if (QFile::copy(fi.filePath(), dest))
			collected << dest;
	}
	return collected;
}