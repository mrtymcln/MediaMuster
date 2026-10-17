#pragma once

#include <QLoggingCategory>
#include <QDateTime>
#include <QString>
#include <QStringList>

// MARK: - Diagnostics
//
// Writes Qt diagnostic messages and in-app console messages to
// <AppData>/mediamuster.log.
// Removes lines older than 30 days at startup.
namespace Diagnostics
{
	/// Open the log and install the handler. Call once, early in main(), after
	/// the application/organisation name is set (the path depends on them).
	/// Logs the app version and system details for each session, without a file title.
	/// The five dated detail lines are enclosed by plain decorative dividers.
	/// An explicit directory allows isolated callers to keep all output there.
	void install(const QString &directory = {}, const QDateTime &now = QDateTime::currentDateTime());

	/// Absolute path to the current log file, for Help > Reveal Diagnostics.
	QString logPath();

	/// Also write a console message to the file, labelled with its module.
	void appendConsoleLine(QtMsgType level, const QString &module, const QString &message,
						   const QDateTime &now = QDateTime::currentDateTime());

	/// Timestamp every physical line, including severity/category and source locations.
	QString formatMessage(QtMsgType type, const QMessageLogContext &context, const QString &message,
						  const QDateTime &now = QDateTime::currentDateTime());

	/// At startup, trim dated log lines and expire collected crash copies.
	/// Discard past divider lines; install() writes the current startup block.
	/// Returns a diagnostic on failure; keeps the original log if rewriting fails.
	QString cleanUp(const QString &directory, const QDateTime &now);

	/// macOS crash-report location. Empty on Windows, where collection is disabled.
	QString systemCrashReportsDir();

	/// Copy crash reports from the last 30 days beside the log.
	/// Preserve the report's modification time; existing copies are left untouched.
	QStringList collectCrashReports(const QString &reportsDir, const QString &logsDir,
									const QDateTime &now = QDateTime::currentDateTime());
} // namespace Diagnostics

// Technical messages use the same category names as the Console where they overlap.
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcAvb)
Q_DECLARE_LOGGING_CATEGORY(lcMdb)
Q_DECLARE_LOGGING_CATEGORY(lcMetadata)
Q_DECLARE_LOGGING_CATEGORY(lcMxf)
Q_DECLARE_LOGGING_CATEGORY(lcOmf)
Q_DECLARE_LOGGING_CATEGORY(lcPmr)
Q_DECLARE_LOGGING_CATEGORY(lcScanner)
Q_DECLARE_LOGGING_CATEGORY(lcVolumes)
