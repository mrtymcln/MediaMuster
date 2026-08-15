#pragma once

#include <QListWidget>
#include <QStringList>

class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

/// Side-panel volume list with drop-target behaviour: accepts any
/// drag carrying at least one local directory URL and emits the
/// filtered paths via `pathsDropped`.
class VolumeListWidget : public QListWidget
{
	Q_OBJECT
public:
	explicit VolumeListWidget(QWidget *parent = nullptr);

signals:
	/// Filtered to existing, readable directories.
	void pathsDropped(const QStringList &paths);

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	void setDropHighlight(bool on);
};