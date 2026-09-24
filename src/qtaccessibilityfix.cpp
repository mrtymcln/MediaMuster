#include "qtaccessibilityfix.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QLatin1String>
#include <QString>
#include <QWidget>

namespace
{
#ifdef Q_OS_MAC
	// Hide child widgets too, so row events cannot resolve to a scroll bar or header.
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
		if (!object || !object->isWidgetType())
			return nullptr;

		QAccessible::Role role;
		if (className == QLatin1String("QListView"))
			role = QAccessible::List;
		else if (className == QLatin1String("QTableView"))
			role = QAccessible::Table;
		else if (className == QLatin1String("QTreeView"))
			role = QAccessible::Tree;
		else
			return nullptr;

		return new ChildlessWidget(static_cast<QWidget *>(object), role);
	}
#endif
} // namespace

void QtAccessibilityFix::install()
{
#ifdef Q_OS_MAC
	QAccessible::installFactory(rowlessItemViewFactory);
	qInfo("macOS accessibility fix enabled");
#endif
}
