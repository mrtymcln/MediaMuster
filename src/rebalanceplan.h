#pragma once

#include "app_avidmedialayout.h"
#include "oprequest.h"

#include <QString>
#include <QVector>

// Planned moves and folder counts shared by Rebalance's planner and dialog.

// MARK: - RebalanceMove

/// One planned media relocation. Relatives normally share a destination;
/// groups larger than the folder target must span folders.
struct RebalanceMove
{
	QString srcPath;
	NumberedMxfFolder dest;
	QString masterMobId; ///< Empty for files with no relatives group.
	qint64 sizeBytes = 0;
	qint64 modifiedMs = -1;
	QString fileMobId;				   ///< Essence-file identity retained for the engine's pre-move check.
	std::optional<OpItem> scannedItem; ///< Exact selection receipt retained while the plan is previewed.
};

// MARK: - FolderState

/// Per-folder snapshot for the rebalance dialog: current on-disk count
/// and bytes, plus the projected delta if the plan runs.
///
/// Folders outside Rebalance's numbered series are shown read-only.
struct FolderState
{
	QString mediaFolderName; ///< Matches NumberedMxfFolder::display() when in scope.
	NumberedMxfFolder id;	 ///< Valid only when `inScope` is true.
	int count = 0;
	qint64 bytes = 0;
	int filesIn = 0;
	int filesOut = 0;
	qint64 bytesIn = 0;
	qint64 bytesOut = 0;
	bool isNew = false;
	bool inScope = true;
};

// MARK: - RebalancePlan

/// Output of RebalancePlanner::computePlan. Either rendered in the preview
/// dialog or passed to Rebalancer::executeAsync to perform the moves.
struct RebalancePlan
{
	QString mxfRootPath; ///< Path to `.../Avid MediaFiles/MXF`.
	QString volumeLabel; ///< Volume display name, for dialog headings.

	QVector<FolderState> folders;
	QVector<RebalanceMove> ops;
	QVector<NumberedMxfFolder> newFolders;

	int moveCount() const { return static_cast<int>(ops.size()); }
};
