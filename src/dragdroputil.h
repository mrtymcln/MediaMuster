#pragma once

#include <QMimeData>
#include <QString>
#include <QUrl>

// MARK: - DragDropUtil

/// Shared `dragEnter` / `dragMove` / `drop` helper for the Volumes
/// panel and the Bin Filter dialog. Centralising the accept-check
/// stops `dragEnter` accepting a drop that `drop` later rejects.
namespace DragDropUtil
{
/// True iff any URL in `mime` is a local file path that
/// satisfies `pred`. `pred` is invoked with the local file
/// path (the result of `QUrl::toLocalFile()`).
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