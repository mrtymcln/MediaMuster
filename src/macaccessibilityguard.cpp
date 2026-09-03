#include "macaccessibilityguard.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QLatin1String>
#include <QString>
#include <QWidget>

namespace
{
#if defined(Q_OS_MAC) && QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
	// The three base classes Qt's own factory keys item views on. A row-less
	// interface for these is the whole guard; see the header for why.
	QAccessible::Role roleFor(const QString &className)
	{
		if (className == QLatin1String("QListView"))
			return QAccessible::List;
		if (className == QLatin1String("QTableView"))
			return QAccessible::Table;
		if (className == QLatin1String("QTreeView"))
			return QAccessible::Tree;
		return QAccessible::NoRole;
	}

	// A plain QAccessibleWidget would still expose the view's child WIDGETS
	// (viewport, scroll bars, header) — and the view's per-row events carry a
	// child index that the bridge resolves through child(): row 0 would
	// come back as the viewport, rows 1-2 as the scroll bars, and VoiceOver
	// would jump there on a click. No children at all makes every row event
	// fail the same way — unresolved, no element, no jump.
	class ChildlessWidget final : public QAccessibleWidget
	{
	public:
		using QAccessibleWidget::QAccessibleWidget;
		int childCount() const override { return 0; }
		QAccessibleInterface *child(int) const override { return nullptr; }
		int indexOfChild(const QAccessibleInterface *) const override { return -1; }
	};

	QAccessibleInterface *rowlessItemViewFactory(const QString &className, QObject *object)
	{
		const QAccessible::Role role = roleFor(className);
		if (role == QAccessible::NoRole || !object || !object->isWidgetType())
			return nullptr;
		return new ChildlessWidget(static_cast<QWidget *>(object), role);
	}
#endif
} // namespace

void MacAccessibilityGuard::install()
{
#if defined(Q_OS_MAC) && QT_VERSION < QT_VERSION_CHECK(6, 6, 2)
	QAccessible::installFactory(rowlessItemViewFactory);
	qInfo() << "macOS accessibility guard on: item views expose no rows (QTBUG-119526, Qt" << qVersion()
			<< "< 6.6.2)";
#endif
}
