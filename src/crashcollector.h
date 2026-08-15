#pragma once

#include <QString>
#include <QStringList>

// MARK: - CrashCollector
//
// macOS drops a crash report in ~/Library/Logs/DiagnosticReports, so
// we collect the fresh ones into our own logs folder.
//
// macOS-only by decision: Windows writes no crash files unless WER is
// opted into via the registry, so systemReportsDir() returns empty
// there and collect() finds nothing. The AppLog diagnostic log is the
// Windows crash story.
namespace CrashCollector
{
	QString systemReportsDir();

	QStringList collect(const QString &reportsDir, const QString &logsDir, int maxAgeDays = 30);
} // namespace CrashCollector