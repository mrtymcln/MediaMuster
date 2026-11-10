#pragma once

#include <QLayout>
#include <QLayoutItem>
#include <QMimeData>
#include <QString>
#include <QUrl>
#include <QWidget>

// MARK: - Drag and drop

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

// MARK: - Layout cleanup

namespace LayoutUtil
{
	// Remove layout items recursively and defer child-widget deletion to the event loop.
	inline void clearLayout(QLayout *layout)
	{
		if (!layout)
			return;
		while (QLayoutItem *item = layout->takeAt(0))
		{
			if (QWidget *w = item->widget())
				w->deleteLater();
			else if (QLayout *child = item->layout())
				clearLayout(child);
			delete item;
		}
	}
} // namespace LayoutUtil
