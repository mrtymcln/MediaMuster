#pragma once

#include <QHash>
#include <QString>
#include <QVector>

// Data shapes for the rebalancer: folder identity, planned move,
// and a complete plan. Separate header so the dialog can include
// the types without Rebalancer's signal/slot logic.

// MARK: - FolderName

/// An Avid MXF subfolder's name, kept in its two parts so the rebalancer
/// can do arithmetic on it — "how full is 42?", "what is the next free
/// number in MartysiMac's range?". You cannot add one to the text
/// "MartysiMac.42"; you can to the number beside the prefix. display()
/// puts the two halves back together whenever a real path is needed.
///
/// Not unique across the machine — two volumes can each hold a folder
/// "3" — which is safe because a rebalance runs over one volume at a
/// time (Marty's ruling 2026-08-31). That is also why it is a Name and
/// not an Id: nothing issues it, the app reads it off the disk.
///
/// Standalone Avid setups name folders just `1`, `2`, `3`, ...; prefix
/// empty, n is the trailing integer.
///
/// In a Nexis environment Avid prepends a hostname so each station gets
/// its own range: `MartysiMac.1`, `Edit14.88`. Prefix is the hostname.
///
/// Anything that doesn't round-trip through `display()` (e.g.
/// `Quarantined Files`, `.5`, `01`) gets rejected by
/// `Rebalancer::parseFolderName` and falls back to `std::nullopt`.
struct FolderName
{
	QString prefix;
	int n = 0;

	QString display() const
	{
		return prefix.isEmpty() ? QString::number(n) : QStringLiteral("%1.%2").arg(prefix).arg(n);
	}

	bool operator==(const FolderName &o) const { return prefix == o.prefix && n == o.n; }

	bool operator!=(const FolderName &o) const { return !(*this == o); }

	/// Prefix first, then n; gives the preview a stable order:
	/// `MartysiMac.*` together in numeric order, then `Edit14.*`, then
	/// unprefixed local folders.
	bool operator<(const FolderName &o) const
	{
		if (prefix != o.prefix)
			return prefix < o.prefix;
		return n < o.n;
	}
};

/// qHashMulti mixes both fields through the hash state; XOR-ing
/// two qHash results would collide for every n on matching prefixes.
inline size_t qHash(const FolderName &id, size_t seed = 0) noexcept
{
	return qHashMulti(seed, id.prefix, id.n);
}

// MARK: - RenameOp

/// One file-move planned by the rebalancer. Relatives are
/// grouped so they always land in the same destination folder.
struct RenameOp
{
	QString srcPath;
	FolderName dest;
	QString masterMobId; ///< Empty for files with no relatives group.
	qint64 sizeBytes = 0;
};

// MARK: - FolderState

/// Per-folder snapshot for the rebalance dialog: current on-disk count
/// and bytes, plus the projected delta if the plan runs.
///
/// `inScope=false` marks folders with non-conforming names
/// (e.g. `Quarantined Files`); shown read-only; never touched.
struct FolderState
{
	QString name; ///< Matches FolderName::display() when in scope.
	FolderName id;  ///< Valid only when `inScope` is true.
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

/// Output of Rebalancer::computePlan. Either rendered in the preview
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