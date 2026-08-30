#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

// MARK: - FileIdentity
//
// The answer to "is the file at this path still the file we mean?" —
// asked immediately before every destructive step, so the engine never
// operates on a guess. The dialog can sit open for minutes while a
// shared Nexis changes underneath it; a crash can strand a journal for
// days before recovery reads it; an undo can run long after the drive
// was unplugged and replugged. Identity is what keeps all of those
// honest: capture it, write it to the ledger, and re-verify it before
// acting. Mismatch means refuse and explain, never proceed.
//
// Identity has two halves, deliberately independent:
//
//   Filesystem half — size, modification time, and (where the volume is
//   trusted, see strength below) the disk's own file ID (inode /
//   NTFS file index) plus its volume ID. Answers "same file OBJECT?".
//
//   Content half — the Avid UMID parsed out of the MXF header itself
//   (`contentUmid`). Answers "same MEDIA?". It survives renames,
//   remounts, drive-letter changes and mtime truncation, which makes it
//   the strongest check exactly where the filesystem half is weakest:
//   network volumes.
//
// Strength records how much the filesystem half is worth, and it is
// written into the ledger so recovery and undo can narrate honestly
// when a check was weak:
//
//   Full     — proven-local volume (see NativeFile::isProvenLocalVolume)
//              and the file ID was captured. IDs decide; mtime is
//              informational only (editing a file in place keeps its ID —
//              same object, and the content half catches media swaps).
//   SizeTime — everything else: network/Nexis, FAT sticks, unknown
//              filesystems. SMB synthesizes file IDs that are not stable
//              across remounts, and a false "this is a different file!"
//              storm during recovery would be its own failure mode — so
//              on those volumes we honestly compare only size + mtime
//              (+ the content half, which IS reliable there).
//   None     — the file couldn't be examined at all.

struct FileIdentity
{
	enum class Strength : int
	{
		None = 0,
		SizeTime = 1,
		Full = 2
	};

	qint64 size = -1;	 ///< -1 = never captured.
	qint64 mtimeNs = 0;	 ///< Native-epoch nanoseconds. Only ever compared
						 ///< against another capture from the same
						 ///< filesystem, so the platform epoch difference
						 ///< (Unix vs Windows 1601) never matters.
	quint64 fileId = 0;	 ///< st_ino / NTFS file index. Full strength only.
	quint64 volumeId = 0; ///< st_dev / volume serial. Full strength only.
	QString contentUmid; ///< Avid UMID from the MXF header; empty = not an
						 ///< MXF, or its header couldn't be parsed.
	Strength strength = Strength::None;

	// MARK: - Capture

	/// Read the file's identity from disk. `readContent=false` skips the
	/// MXF header parse (a few hundred KB of reads) for callers that only
	/// need the filesystem half — verify() uses it when the expected
	/// identity carries no UMID anyway.
	static FileIdentity capture(const QString &path, bool readContent = true);

	// MARK: - Verify

	/// Match      — every field the expected identity's strength vouches
	///              for agrees. Proceed.
	/// Changed    — something disagrees: the file is not (or no longer)
	///              the one recorded. REFUSE the operation and explain.
	/// Missing    — nothing at the path.
	/// Unreadable — something is there but couldn't be examined, or a
	///              Full-strength expectation couldn't be re-checked at
	///              full strength. Unverifiable is not verified: refuse,
	///              with different words (a failing drive or dropped
	///              mount needs a different action than a swapped file).
	enum class Verdict : int
	{
		Match,
		Changed,
		Missing,
		Unreadable
	};

	/// Re-capture the identity at `path` and compare it against
	/// `expected`, applying the strength rules above. The re-parse of the
	/// MXF header only happens when `expected` carries a UMID. Pass
	/// `actualOut` to get the fresh capture back for message-building.
	static Verdict verify(const QString &path, const FileIdentity &expected,
						  FileIdentity *actualOut = nullptr);

	/// The relocated flavour, for recovery and undo: "is the file at
	/// this path the same MEDIA the ledger recorded?" — asked of a file
	/// that has legitimately MOVED since capture (a moved copy about to
	/// be renamed home, a trash catch about to be restored). File IDs
	/// change across volumes and mtimes change on copy, so only the
	/// fields that survive a move are compared: size, and the Avid UMID
	/// when one was recorded. Never weaker than doing nothing — and for
	/// MXF media the UMID makes it decisive.
	static Verdict verifyRelocated(const QString &path, const FileIdentity &expected,
								   FileIdentity *actualOut = nullptr);

	/// One plain-English fragment naming the FIRST thing that differs,
	/// for the runner's refusal messages: "its size changed from 1.2 GB
	/// to 890 MB", "the disk reports it is a different file than the one
	/// selected", "the Avid media ID inside the file is different".
	static QString explainDifference(const FileIdentity &expected, const FileIdentity &actual);

	// MARK: - Ledger round-trip

	/// Compact JSON for a ledger line. File and volume IDs are stored as
	/// hex STRINGS, not JSON numbers: they are unsigned 64-bit values
	/// that can exceed what a JSON number round-trips exactly. Fields the
	/// strength doesn't vouch for are omitted.
	QJsonObject toJson() const;
	static FileIdentity fromJson(const QJsonObject &o);
};

// MARK: - VolumeIdentity
//
// The same idea one level up: "is the volume mounted at this path still
// the volume we mean?" A drive that comes back under a different name
// (macOS's "EDIT 1" → "EDIT 1 1") or letter (E: → F:) makes every
// journaled absolute path a lie — and worse, a DIFFERENT drive mounted
// at the old address would silently receive the recovery actions meant
// for the original. Every operation records the identity of each volume
// it touches; recovery and undo then resolve journaled paths through
// those records: matching volume → proceed, nothing mounted → wait,
// different volume at the address → never touch it, search the mounted
// volumes for the real one and re-anchor the path there.
//
// Capture is read-only — the OS already gives volumes an identity
// (APFS/HFS+ volume UUID on the Mac; volume serial + permanent
// \\?\Volume{GUID}\ path on Windows). Nothing is ever written onto the
// user's drives. Network volumes typically expose no OS identity and get
// Weak strength: label + filesystem type + capacity, recorded as such.

struct VolumeIdentity
{
	enum class Strength : int
	{
		None = 0,
		Weak = 1, ///< No OS id; label+type+capacity is the honest best.
		Full = 2  ///< OS-issued UUID / GUID+serial captured.
	};

	QString uuid;	///< macOS volume UUID, or Windows \\?\Volume{GUID}\ path.
	quint32 serial = 0; ///< Windows volume serial number; 0 elsewhere.
	QString label;	///< The volume's display name.
	QString fsType; ///< As QStorageInfo reports it ("apfs", "NTFS"…).
	qint64 capacityBytes = 0;
	QString rootPath; ///< Where it was mounted at capture time.
	Strength strength = Strength::None;

	/// Identity of the volume holding `anyPathOnVolume`.
	static VolumeIdentity capture(const QString &anyPathOnVolume);

	/// Are these the same physical volume? OS ids decide when both sides
	/// have one; otherwise the Weak triple (label + type + capacity) is
	/// compared — honest best, and the ledger's strength field lets the
	/// caller say so.
	bool matches(const VolumeIdentity &other) const;

	QJsonObject toJson() const;
	static VolumeIdentity fromJson(const QJsonObject &o);
};
