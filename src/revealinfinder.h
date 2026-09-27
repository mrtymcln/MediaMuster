#pragma once

#include <QString>
#include <functional>

/// Reveal the selected file in the Finder or Explorer.
namespace RevealInFinder
{

	/// Receives messages for the Console and diagnostic log.
	using Logger = std::function<void(QtMsgType level, const QString &message)>;

	/// macOS logs failure; Windows can try Explorer or the parent folder.
	/// Calls `log` before returning, on the same thread.
	void reveal(const QString &path, const Logger &log);

} // namespace RevealInFinder
