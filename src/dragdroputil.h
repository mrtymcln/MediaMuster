#pragma once

#include <QMimeData>
#include <QString>
#include <QUrl>

// MARK: - DragDropUtil

/// Shared local-URL check; callers supply their own acceptance predicate.
namespace DragDropUtil
{
	/// Test local file paths against the caller's acceptance predicate.
	template <typename Predicate>
	inline bool hasAnyLocalUrl(const QMimeData *mime, Predicate pred)
	{
		if (!mime || !mime->hasUrls())
			return false;
		for (const QUrl &url : mime->urls())
		{
			if (url.isLocalFile() && pred(url.toLocalFile()))
				return true;
		}
		return false;
	}
} // namespace DragDropUtil
