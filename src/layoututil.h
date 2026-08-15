#pragma once

#include <QLayout>
#include <QLayoutItem>
#include <QWidget>

namespace LayoutUtil
{
	// Empty a layout so it can be refilled. Child widgets go through deleteLater()
	// rather than delete, so a queued paint event can't land on a freed widget
	// mid-teardown; the layout items themselves go now. Recurses into nested
	// layouts — without that, a sub-layout's widgets get orphaned (detached but
	// never freed) instead of cleaned up.
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