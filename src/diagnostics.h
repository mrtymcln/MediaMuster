#pragma once

#include <QLoggingCategory>
#include <QString>
#include <QStringList>

// MARK: - Diagnostics
//
// Writes Qt diagnostic messages and in-app console messages to
// <AppData>/mediamuster.log. Startup enables all MediaMuster log levels
// and clears the log if it was created at least 30 days ago.
namespace Diagnostics
{
	/// Open the log and install the handler. Call once, early in main(), after
	/// the application/organisation name is set (the path depends on them).
	/// Sets Qt's message pattern to include date, time, severity and category.
	/// Writes a header with the app version and system details for each session.
	void install();

	/// Absolute path to the current log file, for 'Reveal Logs'.
	QString logPath();

	/// Also write a console message to the file, labelled with its module.
	void appendConsoleLine(QtMsgType level, const QString &module, const QString &message);

	/// Uses Qt's message pattern, appending available warning/error source locations.
	QString formatMessage(QtMsgType type, const QMessageLogContext &context, const QString &message);

	/// macOS crash-report location. Empty on Windows, where collection is disabled.
	QString systemCrashReportsDir();

	/// Copy crash reports from the last 30 days beside the log.
	/// Existing copies are left untouched.
	QStringList collectCrashReports(const QString &reportsDir, const QString &logsDir);
} // namespace Diagnostics

// Categories default to warnings and errors; install() enables all levels.
// Qt's logging rules can override these settings, for example:
// QT_LOGGING_RULES="mediamuster.scanner.debug=true"
Q_DECLARE_LOGGING_CATEGORY(lcAvb)
Q_DECLARE_LOGGING_CATEGORY(lcMdb)
Q_DECLARE_LOGGING_CATEGORY(lcMetadata)
Q_DECLARE_LOGGING_CATEGORY(lcMxf)
Q_DECLARE_LOGGING_CATEGORY(lcOmf)
Q_DECLARE_LOGGING_CATEGORY(lcPmr)
Q_DECLARE_LOGGING_CATEGORY(lcScanner)
Q_DECLARE_LOGGING_CATEGORY(lcVolume)
Q_DECLARE_LOGGING_CATEGORY(lcWorker)
