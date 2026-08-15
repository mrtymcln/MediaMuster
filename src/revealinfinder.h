#pragma once

#include <QString>
#include <functional>

/// OS-specific 'reveal this file in the file manager' helpers, plus
/// the parent-folder fallback used when the file is gone or the
/// platform reveal API fails.
namespace RevealInFinder
{

	/// Diagnostic sink, fed straight into MainWindow::addLog.
	using Logger = std::function<void(QtMsgType level, const QString &message)>;

	/// Best-effort reveal, in priority order:
	///	  1. File exists: platform reveal-with-highlight.
	///	  2. File missing but parent reachable: open parent folder.
	///	  3. Parent unreachable too: log error and give up.
	///
	/// `log` runs synchronously on the calling thread.
	void reveal(const QString &path, const Logger &log);

} // namespace RevealInFinder