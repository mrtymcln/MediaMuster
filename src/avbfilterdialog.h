#pragma once

#include "avbbinloader.h"
#include "avbfilter.h"

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVector>
#include <QtGlobal>

#include <atomic>
#include <memory>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QMimeData;
class QPushButton;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

// MARK: - AvbFilterDialog

/// Loads bins asynchronously and builds an Intersect / Subtract / Add chain.
/// Loaded bins keep compact file references and warnings, not parsed graphs.
/// Each step snapshots the ticked references; later ticks or bin removal leave it intact.
/// Readable partial bins remain available with persistent warnings.
class AvbFilterDialog : public QDialog
{
	Q_OBJECT
public:
	// MARK: - Operations

	using Operation = AvbFilter::Operation;
	using ChainStep = AvbFilter::Step;

	explicit AvbFilterDialog(QWidget *parent = nullptr);
	~AvbFilterDialog() override;

	// MARK: - Public API

	/// Parse .avb candidates off the GUI thread, including header validation.
	/// Duplicate canonical paths are ignored.
	void addBinFromFile(const QString &avbFilePath);

	/// Clear the chain while keeping loaded bins.
	void clearFilterSteps();

signals:

	// MARK: - Filter signal

	/// The complete ordered expression and insertion-ordered, deduped names
	/// used by the main-window chip strip. An empty expression is inactive.
	void filterChainChanged(const AvbFilter &filter, const QStringList &binNames);

	/// Completed attempt, including immediate extension rejection and failed reads.
	/// Cancelled/removed loading rows do not emit a completion.
	void binLoaded(const AvbBin &bin);

	/// Rejected local file or unsuccessful retained load, for console reporting.
	/// Drag rejections emit on entry; cancelled/removed loads remain silent.
	void loadError(const QString &filePath, const QString &reason);

	/// Readable partial bins remain available, with their uncertainty visible.
	void loadWarning(const QString &filePath, const QString &reason);

protected:
	// MARK: - Drag-drop

	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private slots:
	void onAddBinsClicked();
	void onRemoveSelectedBinsClicked();
	void onIntersectClicked();
	void onSubtractClicked();
	void onAddClicked();
	void onRemoveStep(int index);

	/// Apply one Intersect after the loading batch settles, if the chain is empty.
	void tryAutoIntersect();
	void finishLoadingBatch();

private:
	Q_DISABLE_COPY_MOVE(AvbFilterDialog)

	void setupUi();

	/// Blue ring + tint on the bin list while a valid .avb drag hovers,
	/// matching the Volumes list (VolumeListWidget::setDropHighlight).
	void setDropHighlight(bool on);
	bool hasAcceptedDragPath(const QMimeData *mime) const;

	void appendBinItem(int idx);
	void updateBinItem(int idx);
	void startBinLoad(quint64 id, const QString &path);
	void completeBinLoad(quint64 id, const AvbBin &bin);
	void removeBinRow(int row);
	void reportLoadFailure(const AvbBin &bin);
	bool hasLoadingBins() const;
	void rebuildChainList();
	void publishFilter();
	void applyOperation(Operation op);

	/// Refresh the summary, operation buttons and placeholder when bins or ticks change.
	void refreshBinSelectionUi();

	// MARK: - Tick helpers

	/// Immediate-use snapshot of ticked, fully loaded bins in list order.
	QVector<const AvbBin *> checkedLoadedBins() const;

	struct BinLoadEntry
	{
		AvbBin bin;
		quint64 binRowId = 0;
		bool loading = true;
	};

	QVector<BinLoadEntry> m_bins;
	QVector<ChainStep> m_chain;
	QHash<quint64, std::shared_ptr<std::atomic_bool>> m_pendingLoads;
	QSet<QString> m_dragAcceptedPaths;
	QSet<quint64> m_newlyLoadedIds;
	QThreadPool m_loadPool;
	quint64 m_nextBinId = 1;
	bool m_autoIntersectPending = false;

	// Non-owning observers; the widget/layout parent tree owns the controls.
	QListWidget *m_binList = nullptr;
	QLabel *m_binListSummary = nullptr;
	QListWidget *m_chainList = nullptr;
	QLabel *m_chainSummary = nullptr;
	QPushButton *m_intersectButton = nullptr;
	QPushButton *m_subtractButton = nullptr;
	QPushButton *m_addButton = nullptr;
};
