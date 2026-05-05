#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// Parsed identity of an Avid MXF subfolder.
//   prefix: empty for local Avid ("1", "2"); hostname for Nexis ("MartysiMac", "Edit1").
//   n:      trailing positive integer.
struct FolderId
{
	QString prefix;
	int n = 0;

	QString display() const
	{
		return prefix.isEmpty() ? QString::number(n)
								: QString("%1.%2").arg(prefix).arg(n);
	}

	bool operator==(const FolderId &o) const
	{
		return prefix == o.prefix && n == o.n;
	}

	bool operator!=(const FolderId &o) const { return !(*this == o); }

	bool operator<(const FolderId &o) const
	{
		if (prefix != o.prefix)
			return prefix < o.prefix;
		return n < o.n;
	}
};

inline size_t qHash(const FolderId &id, size_t seed = 0) noexcept
{
	return qHash(id.prefix, seed) ^ qHash(id.n, seed);
}

// One file-move planned by the rebalancer.
struct MoveOp
{
	QString srcPath;
	FolderId dest;
	QString compositionMobId; // empty for files with no composition group
	qint64 sizeBytes = 0;
};

// Per-folder snapshot (before-state + projected delta).
// inScope=false for non-conforming names ("Quarantined Files") — read-only in the dialog.
struct FolderState
{
	QString name;  // display name (matches FolderId::display() in scope)
	FolderId id;   // valid only when inScope is true
	int count = 0; // current on-disk file count
	qint64 bytes = 0;
	int filesIn = 0;
	int filesOut = 0;
	qint64 bytesIn = 0;
	qint64 bytesOut = 0;
	bool isNew = false;
	bool inScope = true;
};

// Output of computePlan; render or pass to executeAsync.
struct RebalancePlan
{
	QString mxfRoot;	// /…/Avid MediaFiles/MXF
	QString driveLabel; // For dialog display only

	QVector<FolderState> folders;
	QVector<MoveOp> ops;
	QVector<FolderId> newFolders;
	QStringList warnings;

	int totalFiles() const { return static_cast<int>(ops.size()); }
	qint64 totalBytes() const
	{
		qint64 t = 0;
		for (const auto &o : ops)
			t += o.sizeBytes;
		return t;
	}
};