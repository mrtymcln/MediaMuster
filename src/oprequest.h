#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>

// MARK: - Op request types
//
// The value types every part of the file-operations engine v2 speaks:
// what the user asked for (OpRequest), one file's line item (OpItem),
// and the on-disk names for the enums. Pure data — no I/O, no Qt beyond
// containers — so the journal, runner, recovery and undo can all include
// this without dragging each other in.
//
// The engine deliberately does NOT pass MediaFile around: an OpItem
// carries only what an operation needs, which is also exactly what the
// journal's plan record can reconstruct for a resumed run. Anything a
// MediaFile knows that isn't here (codec, project, duration…) is scanner
// business the engine must never depend on.

// MARK: - OpKind

/// The five things the engine can be asked to do. Rename is the
/// Rebalance feature's same-volume relocation; Undo is the reversal of a
/// previous run (itself journaled, so a crashed undo is recoverable).
enum class OpKind : int
{
	Copy = 0,
	Move = 1,
	Delete = 2,
	Rename = 3,
	Undo = 4
};

// MARK: - ConflictPolicy

/// What to do when a destination file already exists. Driven from the
/// per-file dropdown in ManageMediaDialog. A file absent from the policy
/// map was never shown as a conflict to the user, so if one turns up on
/// disk anyway it is skipped, never silently replaced (the NTFS
/// case-variant incident is why — see the runner's conflict resolution).
enum class ConflictPolicy : int
{
	KeepBoth = 0,
	Skip = 1,
	Replace = 2
};

// MARK: - On-disk names
//
// The journal stores kinds and policies by NAME, not enum value, so a
// journal written today still means the same thing to a future build
// even if someone reorders the enums. Never rename a string here; add
// new ones instead.

inline QString opKindName(OpKind k)
{
	switch (k)
	{
	case OpKind::Copy:
		return QStringLiteral("copy");
	case OpKind::Move:
		return QStringLiteral("move");
	case OpKind::Delete:
		return QStringLiteral("delete");
	case OpKind::Rename:
		return QStringLiteral("rename");
	case OpKind::Undo:
		return QStringLiteral("undo");
	}
	return QStringLiteral("copy");
}

/// nullopt for a name this build doesn't know. Callers must treat an
/// unknown kind as "do not touch": a wrong guess here is how a recovery
/// pass turns a crash into data loss.
inline std::optional<OpKind> opKindFromName(const QString &s)
{
	if (s == QStringLiteral("copy"))
		return OpKind::Copy;
	if (s == QStringLiteral("move"))
		return OpKind::Move;
	if (s == QStringLiteral("delete"))
		return OpKind::Delete;
	if (s == QStringLiteral("rename"))
		return OpKind::Rename;
	if (s == QStringLiteral("undo"))
		return OpKind::Undo;
	return std::nullopt;
}

inline QString conflictPolicyName(ConflictPolicy policy)
{
	switch (policy)
	{
	case ConflictPolicy::KeepBoth:
		return QStringLiteral("keepboth");
	case ConflictPolicy::Skip:
		return QStringLiteral("skip");
	case ConflictPolicy::Replace:
		return QStringLiteral("replace");
	}
	return {};
}

inline std::optional<ConflictPolicy> conflictPolicyFromName(const QString &name)
{
	if (name == QStringLiteral("keepboth"))
		return ConflictPolicy::KeepBoth;
	if (name == QStringLiteral("skip"))
		return ConflictPolicy::Skip;
	if (name == QStringLiteral("replace"))
		return ConflictPolicy::Replace;
	return std::nullopt;
}

// MARK: - OpItem

/// One file's line item in a request — and, verbatim, one entry in the
/// journal's plan record, which is what makes an interrupted run
/// resumable without a rescan.
///
/// The mob fields are the SCAN'S CLAIMS about the file's Avid identity,
/// recorded so (a) the runner can cross-check the file it finds on disk
/// against what the user actually selected, and (b) journal, undo and
/// recovery messages can name clips ("A001_C002"), not just cryptic MXF
/// filenames. They are claims, not captures: the runner re-reads the
/// real identity from the file itself immediately before touching it
/// (see FileIdentity), because the dialog can sit open for minutes while
/// a shared volume changes underneath it.
struct OpItem
{
	QString src;	///< Absolute source path; the item's identity key.
	QString name;	///< Destination leaf name (usually the source's).
	QString folder; ///< Avid MXF subfolder ("1", "hostname.3"…); preserve mode only.
	qint64 bytes = 0;
	QString policy; ///< Conflict policy by name; empty = none chosen.

	// Scan claims about the media inside the file (empty when unknown).
	QString mobId;		 ///< Avid MOB ID of this essence file.
	QString masterMobId; ///< The master clip's MOB ID.
	QString clipName;	 ///< The human name the editor knows the clip by.

	// Rename (Rebalance) only.
	QString renameDst; ///< Full destination path for this rename.
	QString groupKey;  ///< Relatives-atomic cancel boundary: cancel only
					   ///< lands between groups, never inside one.
};

// MARK: - OpRequest

/// Everything the engine needs to run one operation. Built by the
/// OpManager facade from the UI's selection, by the resume flow from a
/// journal's plan record, or by OpUndo as the inverse of a previous run.
struct OpRequest
{
	OpKind kind = OpKind::Copy;
	QString destRoot; ///< Copy/Move destination root; empty for Delete/Rename/Undo.
	bool preserve = false; ///< Mirror Avid MediaFiles/MXF/<n> under destRoot.
	QVector<OpItem> items;

	/// Undo only: the journal file this run reverses. The undo run writes
	/// its own journal; on clean finish the original gets an 'undone'
	/// marker so it can never be undone twice.
	QString undoesJournalPath;
};
