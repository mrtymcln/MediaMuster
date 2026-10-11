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

/// Filters the main table by one or more Avid bins. User loads bins
/// via drag-drop or the picker, ticks the ones to use, then chains
/// Intersect / Subtract / Add to filter media-row membership.
///
/// State:
///   - Loaded bins: parsed AvbBins with tickboxes.
///   - Chain: ordered (operation, snapshot of ticked media-file identities)
///     steps. Each applyOperation snapshots ticks so subsequent
///     re-ticking doesn't disturb prior steps.
///
/// The proxy matches each row's file identity against the MSML references, in order.
/// Loading is asynchronous. Unreadable bins report loadError; readable partial
/// bins remain available with loadWarning and persistent filter warnings.
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

	/// Drops every chain step, leaving loaded bins untouched. Used
	/// when the user dismisses the bin filter from the main-window
	/// chip strip; keeps dialog state and chip state in lockstep.
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

	/// Current successfully parsed bins, for provenance-aware enrichment.
	/// Emitted once when a loading batch settles, or immediately when bins
	/// are removed so their fallback metadata can be retracted.
	void binsChanged(const QVector<AvbBin> &bins);

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

	/// Convenience: when the chain is empty and bins were just added,
	/// apply Intersect across whatever's currently ticked once the complete
	/// loading batch settles. Drop-bursts collapse into one step.
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
	void emitBinsChanged();
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
	bool m_metadataUpdatePending = false;

	// Non-owning observers; the widget/layout parent tree owns the controls.
	QListWidget *m_binList = nullptr;
	QLabel *m_binListSummary = nullptr;
	QListWidget *m_chainList = nullptr;
	QLabel *m_chainSummary = nullptr;
	QPushButton *m_intersectButton = nullptr;
	QPushButton *m_subtractButton = nullptr;
	QPushButton *m_addButton = nullptr;
};
