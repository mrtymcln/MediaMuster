#pragma once

#include "avbparser.h"
#include "binfilter.h"

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

// MARK: - BinFilterDialog

/// Filters the main table by one or more Avid bins. User loads bins
/// via drag-drop or the picker, ticks the ones to use, then chains
/// Intersect / Subtract / Add to filter media-row membership.
///
/// State:
///   - Loaded bins: parsed AvbBins with tickboxes.
///   - Chain: ordered (operation, snapshot of ticked MOB IDs)
///     steps. Each applyOperation snapshots ticks so subsequent
///     re-ticking doesn't disturb prior steps.
///
/// The proxy applies each step to a row's file or master identity, in order.
/// Loading is asynchronous. Failed or unsupported bins are not retained;
/// errors are reported through loadError for the main-window console.
class BinFilterDialog : public QDialog
{
	Q_OBJECT
public:
	// MARK: - Operations

	using Operation = BinFilter::Operation;
	using ChainStep = BinFilter::Step;

	explicit BinFilterDialog(QWidget *parent = nullptr);
	~BinFilterDialog() override;

	// MARK: - Public API

	/// Parse .avb candidates off the GUI thread, including header validation.
	/// Duplicate canonical paths are ignored.
	void addBinFromFile(const QString &avbFilePath);

	/// Drops every chain step, leaving loaded bins untouched. Used
	/// when the user dismisses the bin filter from the main-window
	/// chip strip; keeps dialog state and chip state in lockstep.
	void clearChain();

signals:

	// MARK: - Filter signal

	/// The complete ordered expression and insertion-ordered, deduped names
	/// used by the main-window chip strip. An empty expression is inactive.
	void filterChainChanged(const BinFilter &filter, const QStringList &binNames);

	/// Completed attempt, including immediate extension rejection and failed reads.
	/// Cancelled/removed loading rows do not emit a completion.
	void binLoaded(const AvbBin &bin);

	/// Rejected local file or unsuccessful retained load, for console reporting.
	/// Drag rejections emit on entry; cancelled/removed loads remain silent.
	void loadError(const QString &filePath, const QString &reason);

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
	void maybeAutoIntersect();
	void finishLoadingBatch();

private:
	Q_DISABLE_COPY_MOVE(BinFilterDialog)

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
	void recomputeAndEmit();
	void applyOperation(Operation op);

	/// Re-syncs the bin-list summary, op-button enable state, and
	/// empty-chain placeholder against current bin/tick state. Cheap
	/// to call; invoke whenever the bin list or tickboxes change.
	void refreshBinSelectionUi();

	// MARK: - Tick helpers

	QSet<QString> selectedBinsMobs() const;
	QVector<QString> selectedBinsDisplayNames() const;
	int selectedBinsCount() const;

	struct LoadedBin
	{
		AvbBin bin;
		quint64 id = 0;
		bool loading = true;
	};

	QVector<LoadedBin> m_bins;
	QVector<ChainStep> m_chain;
	QHash<quint64, std::shared_ptr<std::atomic_bool>> m_pendingLoads;
	QSet<QString> m_dragAcceptedPaths;
	QSet<quint64> m_newlyLoadedIds;
	QThreadPool m_parsePool;
	quint64 m_nextBinId = 1;
	bool m_autoIntersectPending = false;
	bool m_metadataUpdatePending = false;

	// Non-owning observers; the widget/layout parent tree owns the controls.
	QListWidget *m_binList = nullptr;
	QLabel *m_binListSummary = nullptr;
	QListWidget *m_chainList = nullptr;
	QLabel *m_chainSummary = nullptr;
	QPushButton *m_btnIntersect = nullptr;
	QPushButton *m_btnSubtract = nullptr;
	QPushButton *m_btnAdd = nullptr;
	QPushButton *m_btnDone = nullptr;
};
