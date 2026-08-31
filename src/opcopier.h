#pragma once

#include "nativefile.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>

class ParkedFile;

// MARK: - OpCopier
//
// The one way bytes move in the engine. Used by Copy, by a cross-volume
// Move's copy leg, and by Undo's copy-back — so the safety rules are
// written once and hold everywhere.
//
// Three paths, chosen automatically, fastest-safe first:
//
//   1. APFS clone (macOS, same proven-local volume). Instant: the new
//      file shares the source's already-durable blocks. Nothing to
//      verify — no bytes were rewritten — beyond a size re-stat floor.
//   2. Windows native copy (CopyFileExW, BOTH ends proven-local). The
//      OS engine moves the bytes; we then re-read BOTH files and compare
//      XXH3 checksums — the price of the native path is that extra
//      source read, because the OS gave us no hash along the way.
//   3. Our own buffered loop (everywhere else — and ALWAYS when either
//      end is a network/Nexis volume, Marty's call: unknown storage
//      gets predictable, checksummed I/O). Hashes the source inline
//      during the write pass, then re-reads the destination and
//      compares.
//
// Unconditional floors that run even with the verify toggle off: the
// byte count must match the size we started with, the source must still
// be that size afterwards (a networked writer moving the file mid-copy
// breaks snapshot coherence), the destination's flush must succeed, and
// a fresh stat of the destination must report the full size.
//
// Durability: the caller says how hard to push the finished file toward
// the disk. Copy passes Disk (the OS has it; source still exists, so
// the worst case is a re-copy). Move passes Platter — the next thing a
// move does is DELETE the source, and that instant must never depend on
// a drive's write cache surviving a power cut. A volume that can't give
// the Platter barrier (network mounts) degrades to Disk and the result
// says so; the caller records it honestly.
//
// Park contract: the caller has already parked any existing destination
// aside (ParkedFile) and journaled the park path. On Failed or
// Cancelled this class restores the park — the partial destination is
// discarded and the original (if any) goes back, so no failure path can
// leave a fragment wearing a real media name. On Succeeded the park is
// LEFT ARMED: the caller re-verifies the source's identity first and
// only then disposes of the parked original (to the trash, never a hard
// delete) — or restores it if that last check fails.
class OpCopier
{
public:
	enum class Outcome
	{
		Succeeded,
		Failed,
		Cancelled
	};

	struct Result
	{
		Outcome outcome = Outcome::Failed;
		QString error;	 ///< User-facing sentence, set when Failed.
		QString hashHex; ///< XXH3-64 of the verified bytes, hex; empty on
						 ///< the clone path or with verification off.
		bool usedClone = false;
		bool usedNativeCopy = false;
		/// Platter was requested but the volume only delivered Disk
		/// (network mounts). The caller must say so — journal note + log —
		/// never silently claim more than the volume gave.
		bool durabilityDegraded = false;
	};

	/// Copy src → dst. `park` must already be armed over dst (see the
	/// park contract above). `progress` is called with (bytesCopied,
	/// bytesTotal) as the copy advances — unthrottled; the caller owns
	/// pacing. `verifyStarting` fires once when the read-back pass
	/// begins, so the UI can switch its detail row to "Verifying …".
	/// Cancel is checked between chunks; Cancelled fires no error and
	/// the caller counts the file as neither success nor failure
	/// (stop-and-keep).
	Result copy(const QString &src, const QString &dst, ParkedFile &park,
				const std::atomic<bool> &cancel, NativeFile::Durability durability,
				const std::function<void(qint64 copied, qint64 total)> &progress,
				const std::function<void()> &verifyStarting = {});

	/// Outcome of reading a file back for a verify pass. ReadFailed and
	/// a digest mismatch are different problems — a drive that can't be
	/// read and a copy that came out wrong need different words to the
	/// user — so they never collapse into one "no value" case.
	struct HashOutcome
	{
		enum class Status
		{
			Ok,
			ReadFailed,
			Cancelled
		};
		Status status = Status::ReadFailed;
		quint64 digest = 0;
	};

	/// XXH3-64 of a whole file. Public because Undo re-checks a landed
	/// copy against the journal's recorded hash before acting on it.
	HashOutcome hashFile(const QString &path, const std::atomic<bool> &cancel);

private:
	Result copyBuffered(const QString &src, const QString &dst, ParkedFile &park,
						const std::atomic<bool> &cancel, NativeFile::Durability durability,
						const std::function<void(qint64, qint64)> &progress,
						const std::function<void()> &verifyStarting, bool *syncDegraded);
	Result verifyNativeCopy(const QString &src, const QString &dst, ParkedFile &park,
							const std::atomic<bool> &cancel,
							const std::function<void()> &verifyStarting);
	bool ensureBuffer();

	/// 4 MB scratch, allocated on first use and reused across files.
	/// Per-copier, not shared: each future worker gets its own copier,
	/// which is what keeps this class worker-pool-ready.
	QByteArray m_buffer;
};
