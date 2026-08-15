#pragma once

#include <QString>
#include <QtGlobal>

// MARK: - AppLog
//
// The diagnostic log at <AppData>/mediamuster.log. Installs a Qt
// message handler so every qDebug/qCWarning/etc lands in a file, not just
// stderr (a double-clicked .app has none). The in-app console tees its
// lines here too, so one file carries both the user-facing story and the
// dev categories. Always on at full detail; wipes itself every 30 days.
namespace AppLog
{
	/// Open the log and install the handler. Call once, early in main(), after
	/// the application/organisation name is set (the path depends on them).
	/// Wipes the log first if it's past the retention window, then writes a
	/// system-info header so a log sent in cold still says what machine and
	/// build produced it.
	void install();

	/// Absolute path to the current log file, for 'Reveal Logs'.
	QString logPath();

	/// Tee point for the in-app console: writes the same line into the file so
	/// it interleaves with the dev categories. `module` is its freeform tag.
	void appendConsoleLine(QtMsgType level, const QString &module, const QString &message);

	/// Formats one handler message into a log line. Exposed for testing.
	QString formatMessage(QtMsgType type, const QMessageLogContext &context, const QString &message);
} // namespace AppLog