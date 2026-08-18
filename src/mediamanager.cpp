#include "mediamanager.h"
#include "avidlayout.h"
#include "debugslowdown.h"
#include "formatutil.h"
#include "mediamanagerverify.h"
#include "opjournal.h"
#include "pathkey.h"
#include "progressthrottle.h"
#include "third_party/xxhash.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUuid>
#include <optional>
#ifdef Q_OS_MAC
#include <sys/clonefile.h>
#endif
#if defined(Q_OS_WIN)
#include <io.h>
#else
#include <unistd.h>
#endif

// MARK: - Tunables

// 4 MB read/write chunk for the buffered copy path: big enough to keep the
// syscall count down on a multi-GB MXF, small enough not to sit on a large
// resident allocation for the life of the MediaManager.
static constexpr qint64 kCopyBufferSize = 4 * 1024 * 1024;

// MARK: - Internal helpers

namespace
{
	// RAII wrapper for xxHash3 streaming state. Hashes the source
	// during the read pass, re-hashes the destination after copy.
	// Mismatch means copy fails, destination removed.
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

	// Suffixes for ParkedFile. Distinct per operation so a stray temp names
	// the job that left it behind.
	// Defined in AvidLayout beside the probe marker: the scanner has to
	// recognise these same names as temp-renamed media so a stranded
	// park never becomes invisible in the table.
	inline constexpr QLatin1String kCopyReplaceTag = AvidLayout::kCopyReplaceTag;
	inline constexpr QLatin1String kMoveReplaceTag = AvidLayout::kMoveReplaceTag;

	// APFS fast path: same volume clonefile makes a new inode sharing
	// the source's blocks; effectively free. Caller skips the byte
	// loop and verify pass on success.
	bool tryCloneFile(const QString &src, const QString &dst)
	{
#ifdef Q_OS_MAC
		// Test seam: force the buffered read/write path so its cancel and
		// failure-restore branches are reachable. clonefile is atomic and
		// same-volume, so it otherwise finishes before either can trigger.
		// Never set in production.
		if (qEnvironmentVariableIsSet("MEDIAMUSTER_DISABLE_CLONEFILE"))
			return false;
		return clonefile(QFile::encodeName(src).constData(), QFile::encodeName(dst).constData(), 0) ==
			   0;
#else
		Q_UNUSED(src);
		Q_UNUSED(dst);
		return false;
#endif
	}

	// Push a finished destination file down to the drive. flush() moves
	// Qt's buffer to the OS; fsync makes the OS move it to the disk. Both
	// must succeed before a Move may claim the copy is durable — see the
	// syncDestination block in copyFileWithProgress for the reasoning.
	// Plain fsync, not F_FULLFSYNC: the same measured tradeoff the journal
	// makes (see syncFile in opjournal.cpp).
	bool syncToDisk(QFile &f)
	{
		if (!f.flush())
			return false;
		const int fd = f.handle();
		if (fd == -1)
			return false;
#if defined(Q_OS_WIN)
		return ::_commit(fd) == 0;
#else
		return ::fsync(fd) == 0;
#endif
	}

	// Probes whether the OS trash on `sampleFilePath`'s volume reports the
	// trashed location. That location is what undo and crash recovery use
	// to put a file back, so a trash that won't name one is a trash the
	// delete path must avoid (the journal would hold a blank address and
	// every restore would dead-end). A scratch file pays for the answer so
	// no user file has to; on a pathless volume the scratch stays in the OS
	// trash — its address is exactly what we don't have — a few bytes of
	// tmp litter in a bin the user empties anyway.
	bool osTrashReportsPath(const QString &sampleFilePath)
	{
		const QString probePath = QFileInfo(sampleFilePath).absolutePath() +
								  QStringLiteral("/.mm_trashprobe_") +
								  QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) +
								  QStringLiteral(".tmp");
		{
			QFile probe(probePath);
			if (!probe.open(QIODevice::WriteOnly))
				return true; // can't probe here; keep the old behaviour
			probe.write("probe", 5);
		}
		QString where;
		if (!QFile::moveToTrash(probePath, &where))
		{
			QFile::remove(probePath);
			return true; // OS trash refuses outright; the per-file fallback covers that
		}
		if (where.isEmpty())
			return false;
		QFile::remove(where); // tidy our scratch out of the trash
		return true;
	}

	// Skipped count is suppressed when 0 so Delete (no skip path)
	// doesn't trail a `, 0 skipped`.
	QString formatOperationSummary(const QString &verb, int succeeded, int failed, int skipped = 0)
	{
		QString s =
			QStringLiteral("%1 complete: %2 succeeded, %3 failed").arg(verb).arg(succeeded).arg(failed);
		if (skipped > 0)
			s += QStringLiteral(", %1 skipped").arg(skipped);
		return s;
	}
} // namespace

// MARK: - Construction

MediaManager::MediaManager(QObject *parent)
	: QObject(parent)
{
	m_copyBuffer.resize(kCopyBufferSize);
}

// MARK: - Path helpers

QString MediaManager::buildDestPath(const MediaFile &mf, const QString &destRoot, bool preserve)
{
	if (preserve)
		return AvidLayout::mxfRootUnder(destRoot) + QLatin1Char('/') + mf.mxfFolder +
			   QLatin1Char('/') + mf.fileName;
	return destRoot + QLatin1Char('/') + mf.fileName;
}

std::optional<QString> MediaManager::generateRenamePath(const QString &destPath)
{
	const QFileInfo fi(destPath);
	const QString dir = fi.absolutePath();
	const QString base = fi.completeBaseName();
	const QString ext = fi.suffix();

	for (int n = 1; n <= 999; ++n)
	{
		const QString suffix = QStringLiteral(".Copy.%1").arg(n, 2, 10, QLatin1Char('0'));
		const QString candidate =
			ext.isEmpty() ? dir + QLatin1Char('/') + base + suffix
						  : dir + QLatin1Char('/') + base + suffix + QLatin1Char('.') + ext;
		if (!QFile::exists(candidate))
			return candidate;
	}
	return std::nullopt;
}

// MARK: - Journal plan

QString MediaManager::conflictPolicyName(ConflictPolicy policy)
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

std::optional<MediaManager::ConflictPolicy> MediaManager::conflictPolicyFromName(const QString &name)
{
	if (name == QStringLiteral("keepboth"))
		return ConflictPolicy::KeepBoth;
	if (name == QStringLiteral("skip"))
		return ConflictPolicy::Skip;
	if (name == QStringLiteral("replace"))
		return ConflictPolicy::Replace;
	return std::nullopt;
}

QVector<OpJournal::PlanItem> MediaManager::planItems(const QVector<MediaFile> &files,
													 const QHash<QString, ConflictPolicy> &policies)
{
	QVector<OpJournal::PlanItem> out;
	out.reserve(files.size());
	for (const MediaFile &mf : files)
	{
		OpJournal::PlanItem it;
		it.src = mf.filePath;
		it.name = mf.fileName;
		it.folder = mf.mxfFolder;
		it.bytes = mf.sizeBytes;
		if (const auto p = policies.constFind(mf.filePath); p != policies.constEnd())
			it.policy = conflictPolicyName(p.value());
		out.append(it);
	}
	return out;
}

QVector<MediaFile> MediaManager::filesFromPlan(const QVector<OpJournal::PlanItem> &items)
{
	QVector<MediaFile> out;
	out.reserve(items.size());
	for (const OpJournal::PlanItem &it : items)
	{
		MediaFile mf;
		mf.filePath = it.src;
		mf.fileName = it.name.isEmpty() ? QFileInfo(it.src).fileName() : it.name;
		mf.mxfFolder = it.folder;
		mf.sizeBytes = it.bytes;
		out.append(mf);
	}
	return out;
}

QHash<QString, MediaManager::ConflictPolicy>
MediaManager::policiesFromPlan(const QVector<OpJournal::PlanItem> &items)
{
	QHash<QString, ConflictPolicy> out;
	for (const OpJournal::PlanItem &it : items)
		if (const auto p = conflictPolicyFromName(it.policy))
			out.insert(it.src, *p);
	return out;
}

// MARK: - Conflict resolution

MediaManager::ConflictAction
MediaManager::resolveConflict(const MediaFile &mf, QString &dstPath,
							  const QHash<QString, ConflictPolicy> &policies,
							  const QSet<QString> &claimed)
{
	if (!QFile::exists(dstPath))
		return ConflictAction::Proceed;

	const auto policyIt = policies.constFind(mf.filePath);
	if (policyIt == policies.constEnd())
	{
		// No policy entry means the dialog never showed this conflict. Two
		// ways in:
		//
		//   1. A file earlier in THIS run created the destination (flatten
		//      duplicates). Expected; proceed and let claimDestination
		//      redirect this one to a .Copy.NN sibling.
		//
		//   2. A foreign file appeared after the dialog's conflict sweep — a
		//      race on a shared volume, or a case/normalisation alias of a
		//      selected name that string keys can't see. Replacing would
		//      destroy a file the user was never asked about; skip instead.
		if (claimed.contains(PathKey::normalise(dstPath)))
			return ConflictAction::Proceed;

		emit operationItemDone(mf.fileName, mf.filePath, true,
							   QStringLiteral("Skipped: a file appeared at this destination "
											  "after the preview. Run the operation again to "
											  "choose Replace or Keep Both."),
							   true);
		return ConflictAction::Skip;
	}
	const ConflictPolicy policy = policyIt.value();

	if (policy == ConflictPolicy::Skip)
	{
		emit operationItemDone(mf.fileName, mf.filePath, true, QStringLiteral("Skipped (already exists)"),
							   true);
		return ConflictAction::Skip;
	}

	if (policy == ConflictPolicy::KeepBoth)
	{
		const auto renamed = generateRenamePath(dstPath);
		if (!renamed)
		{
			emit operationItemDone(
				mf.fileName, mf.filePath, false,
				QStringLiteral("There are already 999 copies! Did somebody mean to delete some of these?"));
			return ConflictAction::Fail;
		}
		dstPath = *renamed;
		emit operationLog(QtInfoMsg, QStringLiteral("Renaming to %1").arg(QFileInfo(*renamed).fileName()));
	}

	// Replace falls through: both doCopy and doMove park the live destination
	// aside (ParkedFile) before writing, so the caller clears the slot itself
	// and the original is still recoverable if the operation fails.
	return ConflictAction::Proceed;
}

bool MediaManager::claimDestination(const MediaFile &mf, QString &dstPath, QSet<QString> &claimed)
{
	const QString key = PathKey::normalise(dstPath);
	if (!claimed.contains(key))
	{
		claimed.insert(key);
		return true;
	}

	// A file earlier in this run already took this exact path. Never
	// clobber it; carve out a unique sibling instead. generateRenamePath
	// probes the disk, where that earlier file already sits, so it skips
	// straight past the taken slot.
	const auto renamed = generateRenamePath(dstPath);
	if (!renamed)
	{
		emit operationItemDone(mf.fileName, mf.filePath, false,
							   QStringLiteral("Another selected file already maps to this "
											  "destination, and all .Copy.NN names are taken."));
		return false;
	}
	emit operationLog(QtInfoMsg,
					  QStringLiteral("Renaming to %1 (another selected file already targets %2)")
						  .arg(QFileInfo(*renamed).fileName(), QFileInfo(dstPath).fileName()));
	dstPath = *renamed;
	claimed.insert(PathKey::normalise(dstPath));
	return true;
}

// MARK: - Buffered copy + verify

MediaManager::CopyOutcome MediaManager::copyFileWithProgress(const QString &src, const QString &dst,
															 const QString &name, int current,
															 int total, ParkedFile &park,
															 bool syncDestination)
{
	QFile srcFile(src);
	if (!srcFile.open(QIODevice::ReadOnly))
	{
		emit operationItemDone(name, src, false,
							   QStringLiteral("Couldn't read source: %1").arg(srcFile.errorString()));
		park.restore();
		return CopyOutcome::Failed;
	}

	// `park` is already armed over `dst` by the caller: an existing destination
	// has been renamed aside rather than deleted, so a failed or cancelled copy
	// can put the original back. The user chose Replace over a real file and
	// must never be left with neither. Parking it away also leaves the slot
	// empty for clonefile, which refuses to overwrite.

	// Cloned files share blocks with the source, so bytes are already
	// correct: no read pass and no verify pass. `syncDestination` is also
	// moot here: the data blocks are the source's own, already on disk, and
	// the clone's metadata rides the APFS journal — the same consistency
	// guarantee a same-volume rename relies on.
	if (tryCloneFile(src, dst))
	{
		park.commit();
		emit operationProgress(name, current, total, 100.0);
		emit operationLog(QtInfoMsg, QStringLiteral("Cloned %1").arg(name));
		return CopyOutcome::Succeeded;
	}

	// NewOnly closes the TOCTOU window after parking the old destination
	// away; fail loud if another process raced in to create `dst`.
	QFile dstFile(dst);
	if (!dstFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
	{
		emit operationItemDone(name, src, false,
							   QStringLiteral("Couldn't create destination: %1")
								   .arg(dstFile.errorString()));
		park.restore();
		return CopyOutcome::Failed;
	}

	const qint64 totalSize = srcFile.size();
	qint64 copied = 0;

	// Hash during the existing read pass; no extra source-side
	// disk traffic when verify is enabled.
	const bool verify = MediaManagerVerify::enabled();
	std::optional<XxhStream> srcHash;
	if (verify)
	{
		srcHash.emplace();
		if (!srcHash->ok())
		{
			emit operationItemDone(name, src, false, QStringLiteral("Couldn't initialise verification."));
			dstFile.close();
			park.restore();
			return CopyOutcome::Failed;
		}
	}

	// ProgressThrottle (~30 Hz) plus a 32 MB byte threshold so large
	// single-file copies still tick visibly.
	constexpr qint64 kProgressIntervalBytes = 32 * 1024 * 1024;
	ProgressThrottle throttle;
	qint64 lastEmitBytes = 0;

	while (!srcFile.atEnd() && !m_job.isCancelled())
	{
		const qint64 bytesRead = srcFile.read(m_copyBuffer.data(), kCopyBufferSize);

		// -1 (error) vs 0 (EOF): treating them the same hides a
		// truncated copy since srcHash only sees bytes we read.
		if (bytesRead < 0)
		{
			emit operationItemDone(name, src, false,
								   QStringLiteral("Read failed mid-copy: %1").arg(srcFile.errorString()));
			dstFile.close();
			park.restore();
			return CopyOutcome::Failed;
		}
		if (bytesRead == 0)
			break;

		const qint64 bytesWritten = dstFile.write(m_copyBuffer.data(), bytesRead);
		if (bytesWritten != bytesRead)
		{
			emit operationItemDone(name, src, false,
								   QStringLiteral("Write failed mid-copy: %1").arg(dstFile.errorString()));
			dstFile.close();
			park.restore();
			return CopyOutcome::Failed;
		}
		copied += bytesWritten;

		if (verify)
			srcHash->update(m_copyBuffer.constData(), bytesWritten);

		++DebugSlowdown::copyLoopTicks();
		DebugSlowdown::pauseForMs(5);

		if (srcFile.atEnd() || throttle.shouldEmit() ||
			(copied - lastEmitBytes) >= kProgressIntervalBytes)
		{
			const double pct = totalSize > 0 ? (100.0 * copied / totalSize) : 100.0;
			emit operationProgress(name, current, total, pct);
			lastEmitBytes = copied;
		}
	}

	srcFile.close();

	// Floor 1 — runs with or without verification: hand Qt's write buffer
	// to the OS and confirm both that handoff and the close. A full disk or
	// a failing share often only admits trouble at this point, and with the
	// verify toggle off nothing else would notice.
	const bool dstFlushOk = dstFile.flush();

	// Move deletes the source the moment this function reports success, so
	// success must mean the bytes reached the destination's platter — not
	// the OS page cache, which a power cut or yanked cable simply erases.
	// The verify pass below can't cover this: it reads back through that
	// same cache. Copy skips the sync (its source still exists, so the
	// worst case is a re-copy, not a loss).
	if (syncDestination && !m_job.isCancelled() && !syncToDisk(dstFile))
	{
		emit operationItemDone(name, src, false,
							   QStringLiteral("The destination disk didn't confirm the copy was "
											  "written, so it wasn't trusted. Nothing has been "
											  "deleted — check the drive and try again."));
		dstFile.close();
		park.restore();
		return CopyOutcome::Failed;
	}
	dstFile.close();

	if (m_job.isCancelled())
	{
		park.restore();
		return CopyOutcome::Cancelled;
	}

	if (!dstFlushOk || dstFile.error() != QFileDevice::NoError)
	{
		emit operationItemDone(name, src, false,
							   QStringLiteral("The destination reported a write error while "
											  "finishing. Nothing has been deleted — check free "
											  "space and the drive, then try again."));
		park.restore();
		return CopyOutcome::Failed;
	}

	// Detect a source-size change mid-copy. A networked writer
	// shrinking/growing/moving the file breaks snapshot coherence;
	// verify can't catch it (srcHash only saw bytes we read).
	const qint64 srcSizeAfter = QFileInfo(src).size();
	if (copied != totalSize || srcSizeAfter != totalSize)
	{
		emit operationItemDone(
			name, src, false,
			QStringLiteral("Source file changed during the copy "
						   "(started at %1, read %2, now %3). "
						   "Try again when the file is stable.")
				.arg(Format::bytes(totalSize), Format::bytes(copied), Format::bytes(srcSizeAfter)));
		park.restore();
		return CopyOutcome::Failed;
	}

	// Floor 2 — also independent of the verify toggle: ask the filesystem
	// what size the destination actually is. Byte-counting above only
	// proves what WE wrote; a second writer, a truncating share, or a
	// short flush shows up nowhere else when verification is off.
	if (const qint64 dstSizeOnDisk = QFileInfo(dst).size(); dstSizeOnDisk != totalSize)
	{
		emit operationItemDone(
			name, src, false,
			QStringLiteral("The destination file came out the wrong size (%1 on disk, expected "
						   "%2), so it wasn't trusted. Nothing has been deleted.")
				.arg(Format::bytes(dstSizeOnDisk), Format::bytes(totalSize)));
		park.restore();
		return CopyOutcome::Failed;
	}

	if (verify)
	{
		// Progress signal, not a log line: the sheet's detail row is
		// driven by operationProgress, and a console line is not a UI
		// event. The bar keeps its position (same current/total).
		emit operationProgress(QStringLiteral("Verifying %1").arg(name), current, total, 100.0);
		const quint64 expected = srcHash->digest();
		const HashResult actual = hashFile(dst);

		if (actual.status == HashResult::Status::Cancelled)
		{
			park.restore();
			return CopyOutcome::Cancelled;
		}

		// Couldn't read the copy back at all. Roll back the same as a
		// mismatch — unverified bytes don't get to claim success — but say so
		// honestly: an unreadable destination is a failing drive or a dropped
		// mount, not corrupted data, and the two need different actions.
		if (actual.status == HashResult::Status::ReadFailed)
		{
			emit operationItemDone(name, src, false,
								   QStringLiteral("Couldn't read the copy back to check it, so it "
												  "wasn't trusted. The destination has been left "
												  "unchanged — check the drive and try again."));
			park.restore();
			return CopyOutcome::Failed;
		}

		if (actual.digest != expected)
		{
			emit operationItemDone(name, src, false,
								   QStringLiteral("Copy completed but failed verification. The destination "
												  "has been left unchanged."));
			park.restore();
			return CopyOutcome::Failed;
		}
	}
	park.commit();
	return CopyOutcome::Succeeded;
}

void MediaManager::warnJournalDegradedOnce(const OpJournal &journal, bool &warned)
{
	if (warned || !journal.degraded())
		return;
	warned = true;
	emit operationLog(QtCriticalMsg, OpJournal::degradedText());
}

void MediaManager::flagStrandedPark(JournalOp &jop, const ParkedFile &park, const MediaFile &mf)
{
	jop.failedDirty(QStringLiteral("restore failed; original still parked"));
	emit operationLog(
		QtCriticalMsg,
		QStringLiteral("Couldn't put the original '%1' back — it's still in the destination "
					   "folder, named '%2'. MediaMuster will finish restoring it automatically "
					   "on the next launch.")
			.arg(mf.fileName, QFileInfo(park.path()).fileName()));
}

MediaManager::HashResult MediaManager::hashFile(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {HashResult::Status::ReadFailed, 0};

	XxhStream h;
	if (!h.ok())
		return {HashResult::Status::ReadFailed, 0};

	while (!f.atEnd() && !m_job.isCancelled())
	{
		const qint64 n = f.read(m_copyBuffer.data(), kCopyBufferSize);

		// -1 (error) vs 0 (EOF), the same distinction the copy loop makes: a
		// read that fails partway would otherwise digest the bytes we did get
		// and hand back a confident, wrong answer, which the caller can only
		// read as "your data is corrupt".
		if (n < 0)
			return {HashResult::Status::ReadFailed, 0};
		if (n == 0)
			break;
		h.update(m_copyBuffer.constData(), n);
	}

	if (m_job.isCancelled())
		return {HashResult::Status::Cancelled, 0};
	return {HashResult::Status::Ok, h.digest()};
}

// MARK: - Copy job

void MediaManager::executeCopy(QVector<MediaFile> files, const QString &destRoot,
							   bool preserveStructure,
							   const QHash<QString, ConflictPolicy> &conflictPolicies)
{
	m_job.start([this, files = std::move(files), destRoot, preserveStructure, conflictPolicies]
				{ doCopy(files, destRoot, preserveStructure, conflictPolicies); });
}

void MediaManager::doCopy(const QVector<MediaFile> &files, const QString &dest, bool preserve,
						  const QHash<QString, ConflictPolicy> &policies)
{
	int succeeded = 0, failed = 0, skipped = 0;
	const int total = files.size();

	emit operationLog(QtInfoMsg, QStringLiteral("Copying %1 files to %2").arg(total).arg(dest));

	// Write-ahead journal. Copy leaves its source alone, so this looks
	// unnecessary — but replacing a live destination parks the original aside
	// and only deletes it once the new file has verified. A crash inside that
	// window leaves the user with neither file and a temp nothing knows about.
	// The parked path goes in the journal before any rename, which is what
	// lets recovery put the original back.
	auto journal = std::make_unique<OpJournal>(
		OpJournal::Kind::Copy,
		QJsonObject{{QStringLiteral("dest"), dest}, {QStringLiteral("preserve"), preserve}});
	if (!journal->isOpen())
		emit operationLog(QtWarningMsg, OpJournal::openFailedText(OpJournal::Kind::Copy));
	// The whole to-do list, so an interrupted run can be offered for resume.
	journal->writePlan(dest, preserve, planItems(files, policies));

	// Destinations already taken this run, so two same-named selections
	// can't collide on one path. See claimDestination.
	QSet<QString> claimedDests;

	bool journalDegradedWarned = false;

	// NOTE: doMove's loop mirrors this per-file sequence (conflict →
	// claim → mkpath → park → journal). A fix here almost always needs
	// the same fix there — change both together.
	for (int i = 0; i < total && !m_job.isCancelled(); ++i)
	{
		const MediaFile &mf = files[i];
		QString dstPath = buildDestPath(mf, dest, preserve);

		emit operationProgress(mf.fileName, i + 1, total, 0);
		warnJournalDegradedOnce(*journal, journalDegradedWarned);

		if (const auto action = resolveConflict(mf, dstPath, policies, claimedDests);
			action != ConflictAction::Proceed)
		{
			if (action == ConflictAction::Skip)
			{
				++skipped;
				// Journal the skip so a resumed run knows this file was
				// concluded, not left undone (skip lines are what
				// OpRecovery::resumableFrom counts as finished).
				journal->markSkipped(journal->planOp(mf.filePath, dstPath));
			}
			else
				++failed;
			continue;
		}

		if (!claimDestination(mf, dstPath, claimedDests))
		{
			++failed;
			continue;
		}

		// dstPath is final now (KeepBoth already redirected it). Build the park
		// before the JournalOp so its path is on disk in the journal before the
		// rename it describes, then create the destination's folder — parking
		// can't move a file into a directory that doesn't exist yet.
		QDir().mkpath(QFileInfo(dstPath).absolutePath());
		ParkedFile park(dstPath, kCopyReplaceTag);
		JournalOp jop(journal.get(), mf.filePath, dstPath, mf.sizeBytes, park.path());

		if (!park.park())
		{
			emit operationItemDone(
				mf.fileName, mf.filePath, false,
				QStringLiteral("Couldn't move the existing destination aside. Nothing changed."));
			jop.failed(QStringLiteral("park failed"));
			++failed;
			continue;
		}

		const CopyOutcome outcome = copyFileWithProgress(mf.filePath, dstPath, mf.fileName, i + 1,
														 total, park, /*syncDestination=*/false);
		if (outcome == CopyOutcome::Cancelled)
		{
			// Stop the run; the in-flight file is neither succeeded nor failed.
			// The cancel path already restored the park — unless that restore
			// failed, which must reach the journal before finish() runs below.
			if (park.isStranded())
				flagStrandedPark(jop, park, mf);
			break;
		}
		if (outcome == CopyOutcome::Succeeded)
		{
			jop.done();
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
		}
		else if (park.isStranded())
		{
			flagStrandedPark(jop, park, mf);
			++failed;
		}
		else
		{
			jop.failed(QStringLiteral("copy failed"));
			++failed;
		}

		DebugSlowdown::pauseForMs(40);
	}

	// Cancel means stop and keep: close the journal clean so recovery won't
	// undo the copies that already landed, then delete it; a concluded run has
	// nothing to recover.
	journal->finish(succeeded, failed, skipped, m_job.isCancelled());
	journal->prune();

	emit operationLog(failed > 0 ? QtWarningMsg : QtInfoMsg,
					  formatOperationSummary("Copy", succeeded, failed, skipped));
	emit operationFinished(succeeded, failed);
}

// MARK: - Move job

void MediaManager::executeMove(QVector<MediaFile> files, const QString &destRoot,
							   bool preserveStructure,
							   const QHash<QString, ConflictPolicy> &conflictPolicies)
{
	m_job.start([this, files = std::move(files), destRoot, preserveStructure, conflictPolicies]
				{ doMove(files, destRoot, preserveStructure, conflictPolicies); });
}

void MediaManager::doMove(const QVector<MediaFile> &files, const QString &dest, bool preserve,
						  const QHash<QString, ConflictPolicy> &policies)
{
	int succeeded = 0, failed = 0, skipped = 0;
	const int total = files.size();

	emit operationLog(QtInfoMsg, QStringLiteral("Moving %1 files to %2").arg(total).arg(dest));

	// Write-ahead journal: Move is destructive (the source goes away), so a
	// crash mid-run can be rolled back on next launch.
	auto journal = std::make_unique<OpJournal>(
		OpJournal::Kind::Move,
		QJsonObject{{QStringLiteral("dest"), dest}, {QStringLiteral("preserve"), preserve}});
	if (!journal->isOpen())
		emit operationLog(QtWarningMsg, OpJournal::openFailedText(OpJournal::Kind::Move));
	// The whole to-do list, so an interrupted run can be offered for resume.
	journal->writePlan(dest, preserve, planItems(files, policies));

	// Destinations already taken this run, so two same-named selections
	// can't collide on one path. Critical for Move: a silent overwrite
	// here loses the first file outright (its source is already gone).
	QSet<QString> claimedDests;

	bool journalDegradedWarned = false;

	// NOTE: doCopy's loop mirrors this per-file sequence (conflict →
	// claim → mkpath → park → journal). A fix here almost always needs
	// the same fix there — change both together.
	for (int i = 0; i < total && !m_job.isCancelled(); ++i)
	{
		const MediaFile &mf = files[i];
		QString dstPath = buildDestPath(mf, dest, preserve);

		emit operationProgress(mf.fileName, i + 1, total, 0);
		warnJournalDegradedOnce(*journal, journalDegradedWarned);
		DebugSlowdown::pauseForMs(40);

		if (const auto action = resolveConflict(mf, dstPath, policies, claimedDests);
			action != ConflictAction::Proceed)
		{
			if (action == ConflictAction::Skip)
			{
				++skipped;
				// Journal the skip so a resumed run knows this file was
				// concluded, not left undone (skip lines are what
				// OpRecovery::resumableFrom counts as finished).
				journal->markSkipped(journal->planOp(mf.filePath, dstPath));
			}
			else
				++failed;
			continue;
		}

		// Disambiguate before the park-aside check below, so a redirected
		// dstPath isn't mistaken for a live destination to move aside.
		if (!claimDestination(mf, dstPath, claimedDests))
		{
			++failed;
			continue;
		}

		// dstPath is final now (KeepBoth already redirected it). Build the park
		// before the JournalOp so its path is in the journal before the rename
		// it describes; recovery needs it to put a replaced file back.
		QDir().mkpath(QFileInfo(dstPath).absolutePath());
		ParkedFile park(dstPath, kMoveReplaceTag);
		JournalOp jop(journal.get(), mf.filePath, dstPath, mf.sizeBytes, park.path());

		if (!park.park())
		{
			emit operationItemDone(
				mf.fileName, mf.filePath, false,
				QStringLiteral("Couldn't move the existing destination aside. Nothing changed."));
			jop.failed(QStringLiteral("park failed"));
			++failed;
			continue;
		}

		// Test seam: skip the rename so the cross-volume copy+delete leg is
		// reachable on a single-volume test machine (every QTemporaryDir sits
		// on the same filesystem, so the rename below would otherwise always
		// win and leave that leg untested). Never set in production.
		const bool forceCopyLeg = qEnvironmentVariableIsSet("MEDIAMUSTER_FORCE_MOVE_COPY");

		// Same-volume rename: pure inode swap. QFile::rename returns
		// false on a filesystem-boundary crossing, so fall back to
		// copy-then-delete.
		if (!forceCopyLeg && QFile::rename(mf.filePath, dstPath))
		{
			park.commit();
			jop.done();
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
			continue;
		}

		// Cross-volume: copy then delete. `park` above already emptied the
		// destination slot and still holds the original, and it must keep
		// holding it until the source is gone — so the copy gets its own park
		// over the (now empty) slot, whose only job is discarding a partial
		// write. Committing the outer one here would destroy the file we still
		// need to put back if the source delete fails.
		ParkedFile partial(dstPath, kMoveReplaceTag);
		partial.park(); // slot is empty, so this only arms the discard
		const CopyOutcome outcome = copyFileWithProgress(mf.filePath, dstPath, mf.fileName, i + 1,
														 total, partial, /*syncDestination=*/true);
		if (outcome == CopyOutcome::Cancelled)
		{
			// User stopped mid-copy: copyFileWithProgress already discarded its
			// partial write. Put the parked destination back and leave the source
			// untouched. Not a failure — the loop guard ends the run, ~JournalOp
			// settles this op as failed, and the journal is finished as cancelled
			// below and pruned, so its record never reaches recovery. A failed
			// restore is the exception: the dirty fail blocks the prune so
			// recovery can put the stranded original back next launch.
			park.restore();
			if (park.isStranded())
				flagStrandedPark(jop, park, mf);
			break;
		}
		if (outcome == CopyOutcome::Succeeded)
		{
			if (QFile::remove(mf.filePath))
			{
				park.commit();
				jop.done();
				emit operationItemDone(mf.fileName, mf.filePath, true, {});
				++succeeded;
			}
			else
			{
				park.restore();
				if (park.isStranded())
					flagStrandedPark(jop, park, mf);
				else
					jop.failed(QStringLiteral("source remove failed"));
				emit operationItemDone(mf.fileName, mf.filePath, false,
									   QStringLiteral("Copy succeeded but original couldn't be removed. Move "
													  "rolled back; source intact."));
				++failed;
			}
		}
		else
		{
			park.restore();
			if (park.isStranded())
				flagStrandedPark(jop, park, mf);
			else
				jop.failed(QStringLiteral("copy failed"));
			++failed;
		}
	}

	// Cancel means stop and keep: close the journal clean so recovery won't
	// undo what already moved, then delete it; a concluded run has nothing
	// to recover.
	journal->finish(succeeded, failed, skipped, m_job.isCancelled());
	journal->prune();

	emit operationLog(failed > 0 ? QtWarningMsg : QtInfoMsg,
					  formatOperationSummary("Move", succeeded, failed, skipped));
	emit operationFinished(succeeded, failed);
}

// MARK: - Delete job

void MediaManager::executeDelete(QVector<MediaFile> files)
{
	m_job.start([this, files = std::move(files)]
				{ doDelete(files); });
}

void MediaManager::doDelete(const QVector<MediaFile> &files)
{
	int succeeded = 0, failed = 0, trashedCount = 0;
	const int total = files.size();
	QString trashFolder;

	emit operationLog(QtInfoMsg, QStringLiteral("Deleting %1 files").arg(total));

	// Write-ahead journal: log where each file lands (the trash path)
	// before moving on, so a crash mid-run can put it back next launch.
	auto journal = std::make_unique<OpJournal>(OpJournal::Kind::Delete, QJsonObject{});
	if (!journal->isOpen())
		emit operationLog(QtWarningMsg, OpJournal::openFailedText(OpJournal::Kind::Delete));
	// The whole to-do list, so an interrupted run can be offered for resume.
	journal->writePlan(QString(), false, planItems(files, {}));

	// Test seams: force the per-volume fallback (the OS trash succeeds on
	// any local dev volume, so the branch is unreachable otherwise) and
	// re-root it inside the test sandbox (the real root is the volume root,
	// which tests must never write to). Never set in production.
	const bool osTrashDisabled = qEnvironmentVariableIsSet("MEDIAMUSTER_DISABLE_OS_TRASH");
	const QString trashRootOverride = qEnvironmentVariable("MEDIAMUSTER_TRASH_ROOT");

	bool journalDegradedWarned = false;

	// Volumes whose OS trash reports where files land, probed once per
	// volume per run (see osTrashReportsPath). A pathless volume's deletes
	// are routed to the MediaMuster Trash instead, whose addresses we
	// control — so undo and crash recovery keep working there.
	QHash<QString, bool> trashReportsPathByVolume;
	bool pathlessTrashWarned = false;

	for (int i = 0; i < total && !m_job.isCancelled(); ++i)
	{
		const MediaFile &mf = files[i];
		emit operationProgress(mf.fileName, i + 1, total, 0);
		warnJournalDegradedOnce(*journal, journalDegradedWarned);

		JournalOp jop(journal.get(), mf.filePath, QString(), mf.sizeBytes);

		// OS trash first — but only where it reports the trashed location.
		// The out-param is what recovery and undo restore from; a volume
		// whose trash leaves it blank gets its deletes rerouted to the
		// MediaMuster Trash below, where every address is ours.
		bool useOsTrash = !osTrashDisabled;
		// Resolved while the file still exists — after moveToTrash the path
		// is gone and QStorageInfo can't answer.
		QString volKey;
		if (useOsTrash)
		{
			volKey = QStorageInfo(mf.filePath).rootPath();
			auto verdict = trashReportsPathByVolume.find(volKey);
			if (verdict == trashReportsPathByVolume.end())
				verdict = trashReportsPathByVolume.insert(volKey, osTrashReportsPath(mf.filePath));
			if (!verdict.value())
			{
				useOsTrash = false;
				if (!pathlessTrashWarned)
				{
					pathlessTrashWarned = true;
					emit operationLog(QtWarningMsg,
									  QStringLiteral("The system trash on '%1' doesn't report "
													 "where files land, which would make these "
													 "deletes impossible to undo. Using the "
													 "MediaMuster Trash instead.")
										  .arg(volKey));
				}
			}
		}

		QString trashedPath;
		if (useOsTrash && QFile::moveToTrash(mf.filePath, &trashedPath))
		{
			// Belt for the probe's braces: if a file still lands addressless,
			// remember it for this volume and say so — later files reroute.
			if (trashedPath.isEmpty())
			{
				trashReportsPathByVolume[volKey] = false;
				if (!pathlessTrashWarned)
				{
					pathlessTrashWarned = true;
					emit operationLog(QtWarningMsg,
									  QStringLiteral("The system trash accepted '%1' without "
													 "reporting where it landed — that delete "
													 "can't be undone from here. Remaining files "
													 "go to the MediaMuster Trash instead.")
										  .arg(mf.fileName));
				}
			}
			jop.done(trashedPath);
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
			continue;
		}

		// Per-volume fallback: `_MediaMuster_Trash` folder at the
		// volume root, mirroring the source's path layout.
		QString volRoot = trashRootOverride;
		if (volRoot.isEmpty())
			volRoot = QStorageInfo(mf.filePath).rootPath();

		if (volRoot.isEmpty())
		{
			emit operationItemDone(
				mf.fileName, mf.filePath, false,
				QStringLiteral("Couldn't find which volume this lives on, so it's been left alone."));
			jop.failed(QStringLiteral("no volume root"));
			++failed;
			continue;
		}

		// QDir::filePath joins without doubling a separator whether or not the
		// volume root already ends in one ("C:/" and "/Volumes/EDIT" alike).
		const QDir volDir(volRoot);
		const QString relPath = volDir.relativeFilePath(mf.filePath);
		const QString binRoot = volDir.filePath(AvidLayout::kMediaMusterTrashDir);
		const QString binDest = binRoot + QLatin1Char('/') + relPath;

		if (!QDir().mkpath(QFileInfo(binDest).absolutePath()))
		{
			emit operationItemDone(mf.fileName, mf.filePath, false,
								   QStringLiteral("Couldn't create the MediaMuster Trash. File left alone — "
												  "check your write permissions."));
			jop.failed(QStringLiteral("mkpath failed"));
			++failed;
			continue;
		}

		// A prior catch at this path is an earlier delete's safety copy;
		// never destroy it to make room (the exists+remove that used to sit
		// here was the only unconfirmed hard delete in the app). Divert the
		// new arrival to a .Copy.NN sibling — the same scheme the conflict
		// handling uses — and journal wherever it actually lands.
		QString finalDest = binDest;
		if (QFile::exists(finalDest))
		{
			const auto renamed = generateRenamePath(finalDest);
			if (!renamed)
			{
				emit operationItemDone(mf.fileName, mf.filePath, false,
									   QStringLiteral("The MediaMuster Trash already holds 999 "
													  "copies of this file. File left in place — "
													  "empty the trash and try again."));
				jop.failed(QStringLiteral("trash rename slots exhausted"));
				++failed;
				continue;
			}
			finalDest = *renamed;
		}

		if (QFile::rename(mf.filePath, finalDest))
		{
			jop.done(finalDest);
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
			++trashedCount;
			trashFolder = binRoot;
		}
		else
		{
			emit operationItemDone(mf.fileName, mf.filePath, false,
								   QStringLiteral("Couldn't move to MediaMuster Trash. File "
												  "left in place — it may be open elsewhere."));
			jop.failed(QStringLiteral("rename to trash failed"));
			++failed;
		}

		DebugSlowdown::pauseForMs(40);
	}

	journal->finish(succeeded, failed, /*skipped=*/0, m_job.isCancelled());
	journal->prune();

	emit operationLog(failed > 0 ? QtWarningMsg : QtInfoMsg, formatOperationSummary("Delete", succeeded, failed));

	if (trashedCount > 0)
	{
		emit operationLog(QtInfoMsg, QStringLiteral("%1 file(s) moved to MediaMuster Trash at %2")
										 .arg(trashedCount)
										 .arg(trashFolder));
		emit mediaMusterTrashUsed(trashFolder, trashedCount);
	}

	emit operationFinished(succeeded, failed);
}