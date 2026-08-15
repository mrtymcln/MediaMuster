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
/// `reverseCopy` / `reverseMoveLike` in oprecovery.cpp).
///
/// Destruction restores, so a stray early return can't strand the original.
/// `commit()` disarms that once the new file has landed. `restore()` is
/// idempotent and does nothing at all unless `park()` actually succeeded —
/// otherwise a failed park would delete the very destination it couldn't move.
///
/// `restore()` can itself fail (volume yanked mid-operation). It then stays
/// armed — the destructor retries, and `isStranded()` tells the caller to
/// journal a dirty fail so next-launch recovery finishes the job. Forgetting
/// a failed restore is how originals used to vanish under park names with no
/// record anywhere (data-loss review, finding 3).
///
/// Usage:
/// ```
///     ParkedFile park(dstPath, kCopyReplaceTag);
///     JournalOp jop(journal, src, dstPath, bytes, park.path());  // WAL first
///     if (!park.park())
///         return;                       // nothing touched; bail
///     ... write dstPath ...
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

	/// True when the last `restore()` attempt could not put the parked
	/// original back. The caller must record a dirty fail in the journal so
	/// recovery retries at next launch; the destructor also retries in case
	/// the failure was transient (a success there clears this, and recovery
	/// then finds nothing to do — every step stays idempotent).
	bool isStranded() const noexcept { return m_restoreFailed; }

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

	/// Discard whatever we wrote to the destination and put the original back.
	/// Idempotent; a no-op before `park()` or after `commit()`. Returns false
	/// when a parked original could NOT be returned to its slot — the object
	/// stays armed so a later call (or the destructor) can retry, and
	/// `isStranded()` reports true so the caller can journal it.
	bool restore()
	{
		if (!m_armed)
			return true;
		QFile::remove(m_dst);
		if (m_parkedOnDisk)
		{
			if (!QFile::rename(m_parked, m_dst))
			{
				m_restoreFailed = true;
				return false;
			}
			m_parkedOnDisk = false;
		}
		m_armed = false;
		m_restoreFailed = false;
		return true;
	}

private:
	QString m_dst;
	QString m_parked;
	bool m_parkedOnDisk = false;
	bool m_armed = false;
	bool m_restoreFailed = false;
};