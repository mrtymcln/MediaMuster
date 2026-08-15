#include "volumelistwidget.h"
#include "dragdroputil.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>

namespace
{
	// Shared by dragEnterEvent and dragMoveEvent so both honour the
	// same drop-accept rules.
	bool dragHasLocalDir(const QMimeData *mime)
	{
		return DragDropUtil::hasAnyLocalUrl(mime, [](const QString &path)
											{ return QFileInfo(path).isDir(); });
	}
} // namespace

VolumeListWidget::VolumeListWidget(QWidget *parent)
	: QListWidget(parent)
{
	setAcceptDrops(true);
	setDragDropMode(QAbstractItemView::DropOnly);
}

void VolumeListWidget::setDropHighlight(bool on)
{
	if (on)
	{
		setStyleSheet("QListWidget { border: 2px solid #4A90E2; "
					  "background-color: rgba(74, 144, 226, 0.08); }");
	}
	else
	{
		setStyleSheet({});
	}
}

void VolumeListWidget::dragEnterEvent(QDragEnterEvent *event)
{
	if (dragHasLocalDir(event->mimeData()))
	{
		event->acceptProposedAction();
		setDropHighlight(true);
	}
	else
	{
		event->ignore();
	}
}

void VolumeListWidget::dragMoveEvent(QDragMoveEvent *event)
{
	if (dragHasLocalDir(event->mimeData()))
		event->acceptProposedAction();
	else
		event->ignore();
}

void VolumeListWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
	setDropHighlight(false);
	QListWidget::dragLeaveEvent(event);
}

void VolumeListWidget::dropEvent(QDropEvent *event)
{
	setDropHighlight(false);
	QStringList paths;
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (!url.isLocalFile())
			continue;
		const QString path = url.toLocalFile();
		const QFileInfo fi(path);
		// Filter to directories that actually exist and are readable.
		if (fi.isDir() && fi.exists() && fi.isReadable())
			paths.append(path);
	}
	if (!paths.isEmpty())
	{
		emit pathsDropped(paths);
		event->acceptProposedAction();
	}
}