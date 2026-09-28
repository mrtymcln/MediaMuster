#pragma once

#include "avidmedialayout.h"

#include <QString>
#include <QVector>

// Planned moves and folder counts shared by Rebalance's planner and dialog.

// MARK: - RenameOp

/// One planned media relocation. Relatives normally share a destination;
/// groups larger than the folder target must span folders.
struct RenameOp
{
	QString srcPath;
	FolderName dest;
	QString masterMobId; ///< Empty for files with no relatives group.
	qint64 sizeBytes = 0;
	qint64 modifiedMs = -1;
	QString fileMobId; ///< Essence-file identity retained for the engine's pre-move check.
};

// MARK: - FolderState

/// Per-folder snapshot for the rebalance dialog: current on-disk count
/// and bytes, plus the projected delta if the plan runs.
///
/// Folders outside Rebalance's numbered series are shown read-only.
struct FolderState
{
	QString name;  ///< Matches FolderName::display() when in scope.
	FolderName id; ///< Valid only when `inScope` is true.
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
	QString mxfRoot;	 ///< Path to `.../Avid MediaFiles/MXF`.
	QString volumeLabel; ///< Volume display name, for dialog headings.

	QVector<FolderState> folders;
	QVector<RenameOp> ops;
	QVector<FolderName> newFolders;

	int totalFiles() const { return static_cast<int>(ops.size()); }
};
