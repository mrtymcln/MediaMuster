#pragma once

#include <QString>

#include <atomic>
#include <functional>

class QFile;

// MARK: - NativeFile
//
// The engine's only doorway to platform-native file calls. Everything
// unportable lives behind this namespace so the rest of the engine reads
// as one story, and so the safety rules attached to each call are written
// exactly once, next to the call they protect.
//
// Why these four things are native and nothing else is:
//
//   clone()        — APFS clonefile(2): a same-volume copy that finishes
//                    instantly because the new file shares the source's
//                    already-on-disk blocks. There is nothing to verify
//                    because no bytes were rewritten.
//   copyWin()      — CopyFileExW: Windows' own copy routine, with a real
//                    progress callback and cancel. Only used when BOTH
//                    ends are proven-local (see isProvenLocalVolume).
//   syncFile()     — the durability barrier. "The OS accepted the bytes"
//                    and "the bytes are on the platter" are different
//                    promises, and Move's source-delete needs the second.
//   syncDirectory()— making a newly created file's DIRECTORY ENTRY
//                    durable, not just its bytes. Includes the Windows
//                    implementation the v1 journal knowingly lacked.
//
// Plus the one policy question the whole engine keys on:
//
//   isProvenLocalVolume() — the ALLOWLIST that decides which volumes get
//   native fast paths and strong identity. Anything not proven safe —
//   Nexis, SMB, FAT sticks, filesystems we've never heard of — fails
//   this test and gets the engine's own checksummed loop and the weaker
//   identity tier. Unknown must always fail safe.

namespace NativeFile
{
	// MARK: - Volume trust

	/// True only for filesystems proven to keep stable file IDs and to
	/// honour our durability barriers: APFS and HFS+ on the Mac, NTFS and
	/// ReFS on Windows — and only when the volume is genuinely local.
	///
	/// This is an allowlist on purpose (Marty's audit call). A blocklist
	/// of known network types would wrongly trust anything it had never
	/// heard of — and we don't even know what name a Nexis reports. With
	/// an allowlist, an unknown filesystem can never be wrongly trusted.
	///
	/// The Windows subtlety that makes the "genuinely local" checks
	/// necessary: a mapped SMB drive or UNC path reports the SERVER'S
	/// filesystem name — often "NTFS" — so the name alone would pass the
	/// allowlist. UNC prefixes and DRIVE_REMOTE are rejected first.
	bool isProvenLocalVolume(const QString &path);

	// MARK: - Durability barriers

	/// How hard to push a finished file toward the disk.
	///
	///   Disk    — flush() + fsync/_commit: the OS has the bytes and has
	///             queued them to the drive. Survives an app crash and a
	///             drive-acknowledged power loss. Used for Copy
	///             destinations: the source still exists, so the worst
	///             case is a re-copy.
	///   Platter — additionally asks the DRIVE to empty its own write
	///             cache (F_FULLFSYNC on macOS). Used at the one instant
	///             the engine is about to delete the only other copy of
	///             a file: a cross-volume Move's source removal.
	enum class Durability
	{
		Disk,
		Platter
	};

	/// Ok         — the requested barrier held.
	/// OkDegraded — Platter was requested but the filesystem can't do it
	///              (network mounts return ENOTSUP), so the Disk barrier
	///              was applied instead. The caller must SAY so — journal
	///              note + log line — never silently claim more than the
	///              volume delivered.
	/// Failed     — not even the Disk barrier held. The file cannot be
	///              trusted as written.
	enum class SyncResult
	{
		Ok,
		OkDegraded,
		Failed
	};

	SyncResult syncFile(QFile &f, Durability level);

	/// Persist a directory's entries, so a file created (or renamed) in it
	/// survives a power cut that follows. On Unix: open the directory and
	/// fsync it. On Windows: CreateFileW with FILE_FLAG_BACKUP_SEMANTICS
	/// (the only way to get a directory handle) + FlushFileBuffers — the
	/// upgrade the v1 journal documented but never built. Best effort:
	/// false means the window stays open, not that data was lost.
	bool syncDirectory(const QString &dirPath);

	// MARK: - APFS clone

	/// Same-volume APFS clone. On success the destination is a new file
	/// whose data blocks ARE the source's blocks — already durable, no
	/// bytes rewritten, nothing to checksum. Returns false on any other
	/// platform, across volumes, on non-APFS, or when the destination
	/// exists (clonefile refuses to overwrite, which is exactly the
	/// behaviour the park-aside dance relies on).
	///
	/// Test seam: MEDIAMUSTER_DISABLE_CLONEFILE forces false so the
	/// buffered path's cancel/failure branches are reachable in tests.
	/// Never set in production.
	bool clone(const QString &src, const QString &dst);

	// MARK: - Windows native copy

	enum class WinCopyOutcome
	{
		Succeeded,
		Failed,
		Cancelled,
		/// The destination existed when the copy started (FAIL_IF_EXISTS
		/// refused it). Because the runner resolves conflicts and parks
		/// the old destination BEFORE copying, a file here means another
		/// process raced into the slot — a file the engine never wrote
		/// and MUST NOT delete (adversarial review 2026-08-30, finding
		/// 7). Distinct from Failed so the caller can tell "clean up my
		/// partial" from "someone else's file is sitting there".
		RefusedExists,
		Unavailable ///< Not on Windows; caller falls through to the loop.
	};

	/// CopyFileExW with COPY_FILE_FAIL_IF_EXISTS — refusing an existing
	/// destination keeps the same no-overwrite guarantee the buffered
	/// path gets from NewOnly, closing the TOCTOU window after parking.
	///
	/// `progress` is called from the copy engine's callback with
	/// (bytesCopied, bytesTotal); `cancel` is polled there too, and a
	/// set flag makes CopyFileExW abort AND delete its own partial
	/// destination (documented PROGRESS_CANCEL behaviour — the partial
	/// never survives to masquerade as media).
	///
	/// COPY_FILE_RESTARTABLE is deliberately NOT used: it stores restart
	/// state inside the destination file — a corrupt-tail trap for
	/// anything that reads the folder mid-run — and slows large copies.
	/// Recovery's answer to an interrupted copy is "remove the partial,
	/// re-offer the file", which is simpler and already tested.
	WinCopyOutcome copyWin(const QString &src, const QString &dst,
						   const std::function<void(qint64 copied, qint64 total)> &progress,
						   const std::atomic<bool> &cancel, QString *errorOut = nullptr);
} // namespace NativeFile
