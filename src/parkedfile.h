#pragma once

#include <QFile>
#include <QLatin1String>
#include <QString>
#include <QUuid>

#include <utility>

// MARK: - ParkedFile

/// RAII for the park-aside dance Copy and Move both need before they replace
/// a live destination. `QFile::rename` won't overwrite, so an existing
/// destination has to be moved out of the way first — and if the operation
/// then fails, the user must never be left with neither file.
///
/// Deliberately split in two steps. The constructor only *computes* the park
/// path; nothing touches disk until `park()`. That gap is the point: it lets
/// the caller write the path to the write-ahead journal before any rename
/// happens, and recovery keys off the parked file still existing (see
/// `reverseCopy` / `reverseMoveLike` in oprescue.cpp).
///
/// THE OWNERSHIP RULE (adversarial review, 2026-08-30, findings 1/3/7):
/// `restore()` may only delete the destination when the ENGINE wrote it.
/// The caller proves that by calling `noteDestinationWritten()` at the
/// moment its own bytes start landing (buffered open succeeded, clone
/// succeeded, CopyFileEx created the file). Without that note, a file
/// sitting at the destination is somebody else's — another Avid client
/// racing the same folder, the user in Finder — and restore() must not
/// unlink it. In that state a parked original cannot go home either
/// (the slot is occupied), so restore() reports stranded and the caller
/// flags a dirty journal line; the occupying file survives untouched.
///
/// Destruction restores, so a stray early return can't strand the original.
/// `commit()` disarms that once the new file has landed — and note its
/// second life in engine v2: when a replaced original must be TRASHED
/// rather than deleted, the caller moves the parked file to the trash
/// itself and then calls `commit()`, whose remove of the now-absent park
/// is a harmless no-op and whose disarm is the point (see
/// trashParkedOriginal in oprunner.cpp).
///
/// `disarm()` stands down WITHOUT touching the disk. It exists for the
/// one branch with no safe mechanical exit: a same-volume Move whose
/// rename landed but whose rollback rename-home failed — the destination
/// now holds the user's MOVED FILE (their only copy), so neither
/// restore() (which would try to displace it) nor commit() (which would
/// delete the parked original) may run. The caller writes a dirty journal
/// line naming both files and disarms; recovery reads the truth at next
/// launch. It is also called after ANY dirty journal line is written: the
/// journal must be the LAST word on disk state, so the destructor must
/// not keep retrying renames after it (a retry that succeeded after the
/// line was final is how recovery came to delete a restored original as
/// a "partial" — review finding 2).
///
/// Usage:
/// ```
///     ParkedFile park(dstPath, kCopyReplaceTag);
///     JournalOpGuard lop(journal, src, dstPath, bytes, park.path());  // WAL first
///     if (!park.park())
///         return;                       // nothing touched; bail
///     ... open dstPath for writing → park.noteDestinationWritten() ...
///     park.commit();                    // or let it restore on the way out
/// ```
class ParkedFile
{
public:
	/// `tag` separates the park path from the real one (e.g. ".__copyreplace_").
	/// A uuid tail keeps two runs over the same destination from colliding.
	ParkedFile(QString dstPath, QLatin1String tag)
		: m_dst(std::move(dstPath))
	{
		// An empty slot needs no parking; path() stays empty and park()
		// becomes a no-op, so callers take one code path either way.
		if (QFile::exists(m_dst))
			m_parked = m_dst + tag + QUuid::createUuid().toString(QUuid::WithoutBraces);
	}

	~ParkedFile() { restore(); }

	ParkedFile(const ParkedFile &) = delete;
	ParkedFile &operator=(const ParkedFile &) = delete;
	ParkedFile(ParkedFile &&) = delete;
	ParkedFile &operator=(ParkedFile &&) = delete;

	// MARK: - Query

	/// Where the original is being held. Empty when the destination slot was
	/// already free. Journal this before calling `park()`.
	const QString &path() const noexcept { return m_parked; }

	/// The destination this object guards. Callers name it in the message
	/// they show when `destinationLeftBehind()` reports a stuck partial.
	const QString &destinationPath() const noexcept { return m_dst; }

	/// True when the last `restore()` attempt could not put the parked
	/// original back (rename-home failed, or the slot is occupied by a file
	/// the engine didn't write). The caller must record a dirty fail in the
	/// journal so recovery finishes the job at next launch — and then call
	/// `disarm()`, so nothing changes on disk after the journal's last word.
	bool isStranded() const noexcept { return m_restoreFailed; }

	/// Which KIND of stranding the last `restore()` hit: true when the
	/// engine's OWN unfinished write could not be removed from the
	/// destination slot, so a partial file is still sitting there under a
	/// real media name. Distinct from the parked-original case because the
	/// two need different words to the user — and because with no parked
	/// original there is no temp name to point at.
	bool destinationLeftBehind() const noexcept { return m_dstLeftBehind; }

	// MARK: - Lifecycle

	/// Move any existing destination aside and arm the restore. False means
	/// the rename failed and nothing changed; the caller must bail without
	/// touching the destination.
	bool park()
	{
		if (!m_parked.isEmpty())
		{
			if (!QFile::rename(m_dst, m_parked))
				return false;
			m_parkedOnDisk = true;
		}
		m_armed = true;
		return true;
	}

	/// The engine's own bytes are landing at the destination (file created /
	/// opened for write by US). From this moment restore() may delete the
	/// destination to discard the partial. Never call it for a file that
	/// was already there or that another process created — see the
	/// ownership rule above.
	void noteDestinationWritten() noexcept { m_dstWritten = true; }

	/// The new file landed: drop the original and disarm the restore.
	void commit()
	{
		if (m_parkedOnDisk)
		{
			QFile::remove(m_parked);
			m_parkedOnDisk = false;
		}
		m_armed = false;
	}

	/// Stand down without touching the disk — no removal, no rename, no
	/// destructor action later. For the branches where the journal has
	/// recorded a state that must remain exactly as written (a dirty fail),
	/// or where the destination holds a file that must survive (the user's
	/// moved file after a failed rollback). The parked original, if any,
	/// stays at its park path — visible in the media table via the temp-
	/// suffix rule, and recovery's to finish.
	void disarm() noexcept
	{
		m_armed = false;
		m_restoreFailed = false;
		m_dstLeftBehind = false;
	}

	/// Discard the engine's own partial write (only if noteDestinationWritten
	/// was called) and put the original back. Idempotent; a no-op before
	/// `park()` or after `commit()`/`disarm()`. Returns false when the slot
	/// could not be left as it was found — because our own partial write
	/// would not delete (`destinationLeftBehind()`), because the rename home
	/// failed, or because the slot is occupied by a file the engine never
	/// wrote (another process raced in; that file is not ours to delete).
	/// The object stays armed and `isStranded()` reports true so the caller
	/// can write the dirty journal line — and must then `disarm()`.
	bool restore()
	{
		if (!m_armed)
			return true;
		// Discarding OUR partial has to be checked, not assumed. A removal
		// can fail — a file another process still holds open is the everyday
		// Windows case — and swallowing that left a truncated file sitting at
		// the real media name while this returned "clean rollback": the
		// caller then journalled an ordinary failure, recovery read it as
		// "nothing happened", and the fragment stayed on disk for good,
		// reading as media to Avid and as "already exists" to the next run.
		if (m_dstWritten && QFile::exists(m_dst) && !QFile::remove(m_dst))
		{
			m_restoreFailed = true;
			m_dstLeftBehind = true;
			return false;
		}
		if (m_parkedOnDisk)
		{
			// A slot still occupied here means a file the engine didn't
			// write sits at the destination. It survives; the original
			// stays parked; the caller narrates. Trying the rename anyway
			// would fail on every platform that refuses to overwrite —
			// checking first makes the WHY explicit.
			if (!m_dstWritten && QFile::exists(m_dst))
			{
				m_restoreFailed = true;
				return false;
			}
			if (!QFile::rename(m_parked, m_dst))
			{
				m_restoreFailed = true;
				return false;
			}
			m_parkedOnDisk = false;
		}
		m_armed = false;
		m_restoreFailed = false;
		m_dstLeftBehind = false;
		return true;
	}

private:
	QString m_dst;
	QString m_parked;
	bool m_parkedOnDisk = false;
	bool m_armed = false;
	bool m_dstWritten = false;
	bool m_restoreFailed = false;
	bool m_dstLeftBehind = false;
};
