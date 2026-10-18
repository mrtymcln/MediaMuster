#pragma once

#include <QLayout>
#include <QLayoutItem>
#include <QWidget>

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
