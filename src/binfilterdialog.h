#pragma once

#include "avbparser.h"

#include <QDialog>
#include <QSet>
#include <QString>
#include <QVector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// MARK: - BinFilterDialog

/// Filters the main table by one or more Avid bins. User loads bins
/// via drag-drop or the picker, ticks the ones to use, then chains
/// Intersect / Subtract / Add to build the accepted MOB set.
///
/// State:
///   - Loaded bins: parsed AvbBins with tickboxes.
///   - Chain: ordered (operation, snapshot of ticked MOB IDs)
///     steps. Each applyOperation snapshots ticks so subsequent
///     re-ticking doesn't disturb prior steps.
///
/// On every change we recompute and emit filterChainChanged. The
/// MediaFilterProxy consumer should accept a row iff:
///
///     !isActive
///     || acceptedMobs.contains(mf.mobId)
///     || acceptedMobs.contains(mf.compositionMobId)
class BinFilterDialog : public QDialog
{
	Q_OBJECT
public:
	// MARK: - Operations

	enum class Operation
	{
		Intersect, ///< Keep only MOBs that are in the selected bins.
		Subtract,  ///< Hide MOBs that are in the selected bins.
		Add        ///< Pull MOBs from the selected bins back in.
	};

	/// binDisplayNames feeds the chain list label; mobIds is the
	/// actual operand for the set maths (union of every ticked bin's
	/// MOBs at apply time).
	struct ChainStep
	{
		Operation op;
		QVector<QString> binDisplayNames;
		QSet<QString> mobIds;
	};

	explicit BinFilterDialog(QWidget *parent = nullptr);
	~BinFilterDialog() override;

	// MARK: - Public API

	/// Duplicate filePath is silently ignored.
	void addBinFromFile(const QString &avbFilePath);

signals:

	// MARK: - Filter signal

	/// isActive=false means the chain is empty — the proxy should
	/// accept every row. true means acceptedMobs is the resolved
	/// set of MOB ID hex strings the filter accepts.
	void filterChainChanged(bool isActive, const QSet<QString> &acceptedMobs);

protected:
	// MARK: - Drag-drop

	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private slots:
	void onAddBinsClicked();
	void onRemoveSelectedBinsClicked();
	void onIntersectClicked();
	void onSubtractClicked();
	void onAddClicked();
	void onRemoveStep(int index);

private:
	void setupUi();
	void appendBinItem(int idx);
	void rebuildChainList();
	void recomputeAndEmit();
	void applyOperation(Operation op);

	// MARK: - Tick helpers

	QSet<QString> selectedBinsMobs() const;
	QVector<QString> selectedBinsDisplayNames() const;
	int selectedBinsCount() const;

	/// Used as the starting universe when the first chain step is
	/// Subtract: can't subtract from 'accept everything', so prime
	/// with the union and subtract from there. Intersect first or
	/// Add first: start from the first step's own MOB set instead.
	QSet<QString> allLoadedMobs() const;

	QVector<AvbBin> m_bins;
	QVector<ChainStep> m_chain;

	QListWidget *m_binList = nullptr;
	QLabel *m_binListSummary = nullptr;
	QListWidget *m_chainList = nullptr;
	QLabel *m_chainSummary = nullptr;
	QPushButton *m_btnIntersect = nullptr;
	QPushButton *m_btnSubtract = nullptr;
	QPushButton *m_btnAdd = nullptr;
	QPushButton *m_btnDone = nullptr;
};