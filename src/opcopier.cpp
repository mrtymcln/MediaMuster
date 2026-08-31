#include "opcopier.h"

#include "testpause.h"
#include "formatutil.h"
#include "opverify.h"
#include "parkedfile.h"
#include "third_party/xxhash.h"

#include <QFile>
#include <QFileInfo>

// MARK: - Tunables

// 4 MB read/write chunk for the buffered path: big enough to keep the
// syscall count down on a multi-GB MXF, small enough not to sit on a
// large resident allocation for the life of the copier.
static constexpr qint64 kCopyBufferSize = 4 * 1024 * 1024;

namespace
{
	// RAII wrapper for xxHash3 streaming state. Hashes the source during
	// the read pass, re-hashes the destination after the copy; mismatch
	// means the copy is not trusted and the destination is discarded.
	struct XxhStream
	{
		XXH3_state_t *s = XXH3_createState();
		XxhStream()
		{
			if (s)
				XXH3_64bits_reset(s);
		}
		~XxhStream()
		{
			if (s)
				XXH3_freeState(s);
		}

		// XXH3 state isn't copyable; moving would double-free.
		XxhStream(const XxhStream &) = delete;
		XxhStream &operator=(const XxhStream &) = delete;
		XxhStream(XxhStream &&) = delete;
		XxhStream &operator=(XxhStream &&) = delete;

		void update(const void *data, size_t n) { XXH3_64bits_update(s, data, n); }
		quint64 digest() const { return XXH3_64bits_digest(s); }
		bool ok() const { return s != nullptr; }
	};

	QString hexDigest(quint64 digest)
	{
		// Fixed width so journal lines are grep-able and two digests are
		// always comparable as strings.
		return QStringLiteral("%1").arg(digest, 16, 16, QLatin1Char('0'));
	}
} // namespace

// MARK: - Buffer

bool OpCopier::ensureBuffer()
{
	if (m_buffer.size() == kCopyBufferSize)
		return true;
	m_buffer.resize(kCopyBufferSize);
	return m_buffer.size() == kCopyBufferSize;
}

// MARK: - The copy

OpCopier::Result OpCopier::copy(const QString &src, const QString &dst, ParkedFile &park,
								const std::atomic<bool> &cancel,
								NativeFile::Durability durability,
								const std::function<void(qint64, qint64)> &progress,
								const std::function<void()> &verifyStarting)
{
	Result res;

	// The gate on every native path (Marty's call): both ends must be
	// proven-local. The destination doesn't exist yet, so its PARENT
	// answers for it. Anything not proven — network/Nexis, FAT sticks,
	// unknown filesystems — takes the buffered loop below, where every
	// byte is checksummed on the way through.
	const QString dstParent = QFileInfo(dst).absolutePath();
	const bool bothProvenLocal =
		NativeFile::isProvenLocalVolume(src) && NativeFile::isProvenLocalVolume(dstParent);

	// Path 1 — APFS clone. clonefile itself refuses cross-volume and
	// non-APFS, so a false here just falls through; no bytes moved.
	if (bothProvenLocal && NativeFile::clone(src, dst))
	{
		// The clone CREATED the destination, so it is ours: rollback (here
		// or later in the runner) may delete it. Without this note,
		// ParkedFile::restore() refuses to touch the destination — the
		// ownership rule that keeps racers' files alive.
		park.noteDestinationWritten();
		// The one floor a clone needs: the destination must report the
		// source's full size. (Blocks are shared, so there is nothing to
		// checksum — no bytes were rewritten — but a truncated clone
		// would still be a lie worth catching.)
		const qint64 srcSize = QFileInfo(src).size();
		const qint64 dstSize = QFileInfo(dst).size();
		if (dstSize != srcSize)
		{
			QFile::remove(dst);
			park.restore();
			res.error = QStringLiteral("The cloned file came out the wrong size (%1 on disk, "
									   "expected %2), so it wasn't trusted. Nothing has been "
									   "deleted.")
							.arg(Format::bytes(dstSize), Format::bytes(srcSize));
			return res;
		}
		res.outcome = Outcome::Succeeded;
		res.usedClone = true;
		if (progress)
			progress(srcSize, srcSize);
		return res;
	}

#if defined(Q_OS_WIN)
	// Path 2 — Windows native copy. Seam MEDIAMUSTER_DISABLE_COPYFILEEX
	// forces the portable loop so its branches stay reachable in tests.
	if (bothProvenLocal && !qEnvironmentVariableIsSet("MEDIAMUSTER_DISABLE_COPYFILEEX"))
	{
		QString winErr;
		const NativeFile::WinCopyOutcome outcome =
			NativeFile::copyWin(src, dst, progress, cancel, &winErr);
		if (outcome == NativeFile::WinCopyOutcome::Cancelled)
		{
			// CopyFileExW deleted its own partial; put any parked
			// original back and stop quietly (stop-and-keep).
			park.restore();
			res.outcome = Outcome::Cancelled;
			return res;
		}
		if (outcome == NativeFile::WinCopyOutcome::RefusedExists)
		{
			// Another process created a file at this path between the
			// runner's conflict check and the copy starting (FAIL_IF_EXISTS
			// caught it). That file is NOT ours — it must survive
			// (adversarial review finding 7: this used to be a hard delete
			// of somebody else's freshly-written media). restore() without
			// a written-note leaves it alone; a parked original then can't
			// go home and the runner flags the stranded park.
			park.restore();
			res.error = QStringLiteral(
				"Another program created a file at the destination while this copy was being "
				"prepared. That file has been left untouched; this copy was skipped — rescan "
				"and try again.");
			return res;
		}
		if (outcome == NativeFile::WinCopyOutcome::Failed)
		{
			// FAIL_IF_EXISTS proved the slot was empty when the copy
			// started, so any file there now is CopyFileExW's own partial —
			// ours to remove, or a truncated file wearing a real media name
			// reads as media to Avid and as "already exists" to a later
			// Skip-policy run.
			park.noteDestinationWritten();
			if (QFile::exists(dst))
				QFile::remove(dst);
			park.restore();
			res.error = QStringLiteral("Couldn't copy: %1").arg(winErr);
			return res;
		}
		if (outcome == NativeFile::WinCopyOutcome::Succeeded)
		{
			park.noteDestinationWritten();
			res = verifyNativeCopy(src, dst, park, cancel, verifyStarting);
			if (res.outcome != Outcome::Succeeded)
				return res;
			res.usedNativeCopy = true;

			// CopyFileExW gives no durability promise; apply the barrier
			// the caller asked for before success is reported.
			QFile dstFile(dst);
			if (!dstFile.open(QIODevice::ReadWrite))
			{
				QFile::remove(dst);
				park.restore();
				res = Result{};
				res.error = QStringLiteral("The destination disk didn't confirm the copy was "
										   "written, so it wasn't trusted. Nothing has been "
										   "deleted — check the drive and try again.");
				return res;
			}
			const NativeFile::SyncResult sync = NativeFile::syncFile(dstFile, durability);
			dstFile.close();
			if (sync == NativeFile::SyncResult::Failed)
			{
				QFile::remove(dst);
				park.restore();
				res = Result{};
				res.error = QStringLiteral("The destination disk didn't confirm the copy was "
										   "written, so it wasn't trusted. Nothing has been "
										   "deleted — check the drive and try again.");
				return res;
			}
			res.durabilityDegraded = (sync == NativeFile::SyncResult::OkDegraded);
			return res;
		}
		// Unavailable can't happen on Windows; fall through regardless.
	}
#endif

	// Path 3 — our own loop.
	bool syncDegraded = false;
	res = copyBuffered(src, dst, park, cancel, durability, progress, verifyStarting,
					   &syncDegraded);
	res.durabilityDegraded = syncDegraded;
	return res;
}

// MARK: - Native-copy verification

OpCopier::Result OpCopier::verifyNativeCopy(const QString &src, const QString &dst,
											ParkedFile &park, const std::atomic<bool> &cancel,
											const std::function<void()> &verifyStarting)
{
	Result res;

	// Floors first — these run even with the verify toggle off.
	const qint64 srcSize = QFileInfo(src).size();
	const qint64 dstSize = QFileInfo(dst).size();
	if (dstSize != srcSize)
	{
		QFile::remove(dst);
		park.restore();
		res.error = QStringLiteral("The destination file came out the wrong size (%1 on disk, "
								   "expected %2), so it wasn't trusted. Nothing has been "
								   "deleted.")
						.arg(Format::bytes(dstSize), Format::bytes(srcSize));
		return res;
	}

	if (OpVerify::enabled())
	{
		if (verifyStarting)
			verifyStarting();
		// The OS engine gave us no hash along the way, so both files are
		// re-read — the price of the native path. Source first: if the
		// SOURCE can't be read back, the copy's provenance is unknowable.
		const HashOutcome srcHash = hashFile(src, cancel);
		if (srcHash.status == HashOutcome::Status::Cancelled)
		{
			park.restore();
			res.outcome = Outcome::Cancelled;
			return res;
		}
		if (srcHash.status == HashOutcome::Status::ReadFailed)
		{
			QFile::remove(dst);
			park.restore();
			res.error = QStringLiteral("Couldn't read the source back to check the copy, so it "
									   "wasn't trusted. Nothing has been deleted — check the "
									   "drive and try again.");
			return res;
		}

		const HashOutcome dstHash = hashFile(dst, cancel);
		if (dstHash.status == HashOutcome::Status::Cancelled)
		{
			park.restore();
			res.outcome = Outcome::Cancelled;
			return res;
		}
		if (dstHash.status == HashOutcome::Status::ReadFailed)
		{
			QFile::remove(dst);
			park.restore();
			res.error = QStringLiteral("Couldn't read the copy back to check it, so it wasn't "
									   "trusted. The destination has been left unchanged — "
									   "check the drive and try again.");
			return res;
		}

		if (srcHash.digest != dstHash.digest)
		{
			QFile::remove(dst);
			park.restore();
			res.error = QStringLiteral("Copy completed but failed verification. The destination "
									   "has been left unchanged.");
			return res;
		}
		res.hashHex = hexDigest(dstHash.digest);
	}

	res.outcome = Outcome::Succeeded;
	return res;
}

// MARK: - Buffered loop

OpCopier::Result OpCopier::copyBuffered(const QString &src, const QString &dst, ParkedFile &park,
										const std::atomic<bool> &cancel,
										NativeFile::Durability durability,
										const std::function<void(qint64, qint64)> &progress,
										const std::function<void()> &verifyStarting,
										bool *syncDegraded)
{
	Result res;

	if (!ensureBuffer())
	{
		park.restore();
		res.error = QStringLiteral("Couldn't allocate the copy buffer.");
		return res;
	}

	QFile srcFile(src);
	if (!srcFile.open(QIODevice::ReadOnly))
	{
		park.restore();
		res.error = QStringLiteral("Couldn't read source: %1").arg(srcFile.errorString());
		return res;
	}

	// NewOnly closes the TOCTOU window after parking the old destination
	// away; fail loud if another process raced in to create `dst`. The
	// racer's file is NOT ours: no written-note has been given, so
	// restore() leaves it in place (adversarial review finding 3 — this
	// used to unlink another Avid client's freshly-recorded media).
	QFile dstFile(dst);
	if (!dstFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
	{
		const bool raced = QFile::exists(dst);
		park.restore();
		res.error =
			raced ? QStringLiteral(
						"Another program created a file at the destination while this copy was "
						"being prepared. That file has been left untouched; this copy was "
						"skipped — rescan and try again.")
				  : QStringLiteral("Couldn't create destination: %1").arg(dstFile.errorString());
		return res;
	}

	// From here the destination is OURS — created by the NewOnly open —
	// so failure paths may delete the partial to discard it.
	park.noteDestinationWritten();

	const qint64 totalSize = srcFile.size();
	qint64 copied = 0;

	// Hash during the existing read pass; no extra source-side disk
	// traffic when verification is on.
	const bool verify = OpVerify::enabled();
	std::optional<XxhStream> srcHash;
	if (verify)
	{
		srcHash.emplace();
		if (!srcHash->ok())
		{
			dstFile.close();
			QFile::remove(dst);
			park.restore();
			res.error = QStringLiteral("Couldn't initialise verification.");
			return res;
		}
	}

	while (!srcFile.atEnd() && !cancel.load(std::memory_order_acquire))
	{
		const qint64 bytesRead = srcFile.read(m_buffer.data(), kCopyBufferSize);

		// -1 (error) vs 0 (EOF): treating them the same hides a
		// truncated copy, since srcHash only sees bytes we read.
		if (bytesRead < 0)
		{
			dstFile.close();
			park.restore();
			res.error =
				QStringLiteral("Read failed mid-copy: %1").arg(srcFile.errorString());
			return res;
		}
		if (bytesRead == 0)
			break;

		const qint64 bytesWritten = dstFile.write(m_buffer.data(), bytesRead);
		if (bytesWritten != bytesRead)
		{
			dstFile.close();
			park.restore();
			res.error =
				QStringLiteral("Write failed mid-copy: %1").arg(dstFile.errorString());
			return res;
		}
		copied += bytesWritten;

		if (verify)
			srcHash->update(m_buffer.constData(), bytesWritten);

		++OpCopier::loopTicks();
		TestPause::sleepMs(TestPause::kPerCopyChunkMs);

		if (progress)
			progress(copied, totalSize);
	}

	srcFile.close();

	// Floor 1 — runs with or without verification: hand Qt's write
	// buffer to the OS and confirm both that handoff and the close. A
	// full disk or a failing share often only admits trouble here.
	const bool dstFlushOk = dstFile.flush();

	// The durability barrier, at whatever level the caller asked for.
	// Copy asks for Disk ("copied" must not mean "in the page cache" —
	// the user may hand-delete the source after seeing success); Move
	// asks for Platter, because its next step deletes the only other
	// copy. A cancelled run skips it — the file is about to be
	// discarded anyway.
	if (!cancel.load(std::memory_order_acquire))
	{
		const NativeFile::SyncResult sync = NativeFile::syncFile(dstFile, durability);
		if (sync == NativeFile::SyncResult::Failed)
		{
			dstFile.close();
			park.restore();
			res.error = QStringLiteral("The destination disk didn't confirm the copy was "
									   "written, so it wasn't trusted. Nothing has been "
									   "deleted — check the drive and try again.");
			return res;
		}
		if (syncDegraded)
			*syncDegraded = (sync == NativeFile::SyncResult::OkDegraded);
	}
	dstFile.close();

	if (cancel.load(std::memory_order_acquire))
	{
		park.restore();
		res.outcome = Outcome::Cancelled;
		return res;
	}

	if (!dstFlushOk || dstFile.error() != QFileDevice::NoError)
	{
		park.restore();
		res.error = QStringLiteral("The destination reported a write error while finishing. "
								   "Nothing has been deleted — check free space and the "
								   "drive, then try again.");
		return res;
	}

	// Floor 2 — detect a source-size change mid-copy. A networked writer
	// shrinking/growing/moving the file breaks snapshot coherence;
	// verify can't catch it (srcHash only saw bytes we read).
	const qint64 srcSizeAfter = QFileInfo(src).size();
	if (copied != totalSize || srcSizeAfter != totalSize)
	{
		park.restore();
		res.error = QStringLiteral("Source file changed during the copy (started at %1, read "
								   "%2, now %3). Try again when the file is stable.")
						.arg(Format::bytes(totalSize), Format::bytes(copied),
							 Format::bytes(srcSizeAfter));
		return res;
	}

	// Floor 3 — ask the filesystem what size the destination actually
	// is. Byte-counting only proves what WE wrote; a second writer, a
	// truncating share, or a short flush shows up nowhere else when
	// verification is off.
	if (const qint64 dstSizeOnDisk = QFileInfo(dst).size(); dstSizeOnDisk != totalSize)
	{
		park.restore();
		res.error = QStringLiteral("The destination file came out the wrong size (%1 on disk, "
								   "expected %2), so it wasn't trusted. Nothing has been "
								   "deleted.")
						.arg(Format::bytes(dstSizeOnDisk), Format::bytes(totalSize));
		return res;
	}

	if (verify)
	{
		if (verifyStarting)
			verifyStarting();
		const quint64 expected = srcHash->digest();
		const HashOutcome actual = hashFile(dst, cancel);

		if (actual.status == HashOutcome::Status::Cancelled)
		{
			park.restore();
			res.outcome = Outcome::Cancelled;
			return res;
		}

		// Couldn't read the copy back at all. Discard the same as a
		// mismatch — unverified bytes don't get to claim success — but
		// say so honestly: an unreadable destination is a failing drive
		// or a dropped mount, not corrupted data, and the two need
		// different actions from the user.
		if (actual.status == HashOutcome::Status::ReadFailed)
		{
			park.restore();
			res.error = QStringLiteral("Couldn't read the copy back to check it, so it wasn't "
									   "trusted. The destination has been left unchanged — "
									   "check the drive and try again.");
			return res;
		}

		if (actual.digest != expected)
		{
			park.restore();
			res.error = QStringLiteral("Copy completed but failed verification. The "
									   "destination has been left unchanged.");
			return res;
		}
		res.hashHex = hexDigest(actual.digest);
	}

	res.outcome = Outcome::Succeeded;
	return res;
}

// MARK: - Read-back hash

OpCopier::HashOutcome OpCopier::hashFile(const QString &path, const std::atomic<bool> &cancel)
{
	if (!ensureBuffer())
		return {HashOutcome::Status::ReadFailed, 0};

	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {HashOutcome::Status::ReadFailed, 0};

	XxhStream h;
	if (!h.ok())
		return {HashOutcome::Status::ReadFailed, 0};

	while (!f.atEnd() && !cancel.load(std::memory_order_acquire))
	{
		const qint64 n = f.read(m_buffer.data(), kCopyBufferSize);

		// -1 (error) vs 0 (EOF), the same distinction the copy loop
		// makes: a read that fails partway would otherwise digest the
		// bytes we did get and hand back a confident, wrong answer.
		if (n < 0)
			return {HashOutcome::Status::ReadFailed, 0};
		if (n == 0)
			break;
		h.update(m_buffer.constData(), n);
	}

	if (cancel.load(std::memory_order_acquire))
		return {HashOutcome::Status::Cancelled, 0};
	return {HashOutcome::Status::Succeeded, h.digest()};
}
