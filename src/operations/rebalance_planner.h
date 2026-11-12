#pragma once

#include "media/avid_media_layout.h"
#include "media/media_file_record.h"
#include "operation_request.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <optional>

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
	QString fileMobId;						  ///< Essence-file identity retained for the engine's pre-move check.
	std::optional<OperationItem> scannedItem; ///< Exact selection receipt retained while the plan is previewed.
};

// MARK: - FolderState

/// Per-folder snapshot for the rebalance dialog: directory file count and
/// eligible scan-row bytes, plus the projected delta if the plan runs.
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

/// Read-only planning and value conversion. File mutations belong to OpRunner.
// MARK: - Rebalance planning

namespace RebalancePlanner
{
	struct FolderCount
	{
		int count = -1; ///< Unavailable; a known count is zero or greater.
		bool exists = false;
	};

	/// Flat MXF essence counts for the requested managed folders. A zero
	/// count with exists=false means absent under an accessible MXF root.
	/// QDirIterator exposes no enumeration error: accessibility is checked
	/// before and after reading, but this is not an atomic filesystem snapshot.
	QHash<NumberedMxfFolder, FolderCount> countFolders(const QString &mxfRootPath, const QSet<NumberedMxfFolder> &folders);

	std::optional<NumberedMxfFolder> srcFolderOf(const QString &sourcePath);
	bool isEligible(const MediaFileRecord &file);
	RebalancePlan computePlan(const QString &mxfRootPath, const QString &volumeLabel,
							  const QVector<MediaFileRecord> &files);
	OperationRequest requestForPlan(const RebalancePlan &plan);
} // namespace RebalancePlanner
