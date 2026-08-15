#include "debugslowdown.h"
#include "mediafile.h"
#include "mediamanager.h"
#include "mediamanagerverify.h"
#include "opjournal.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestMediaManagerPaths : public QObject
{
	Q_OBJECT
private slots:
	/// Point the write-ahead journal at a temp dir for the whole class. Copy,
	/// Move and Delete all journal now, and without this they'd write into the
	/// real AppData oplog — where a test that dies mid-run would leave a
	/// journal for the next *real* app launch to dutifully roll back.
	void initTestCase();
	void cleanupTestCase();

	void buildDestPath_preserve_true_uses_avid_structure();
	void buildDestPath_preserve_false_flattens();
	void generateRenamePath_first_slot_when_no_copies();
	void generateRenamePath_skips_existing_slots();
	void generateRenamePath_returns_nullopt_when_exhausted();
	void generateRenamePath_handles_extensionless_path();

	// Regression: two selected files that flatten onto the same destination
	// name must both survive — the later one renamed, never overwriting the
	// first (which on Move would lose it outright).
	void move_flatten_sameName_keepsBothFiles();

	// Copy: non-destructive; the source always survives.
	void copy_writes_file_and_leaves_source();
	void copy_skip_policy_leaves_existing_destination();
	void copy_replace_policy_overwrites_destination();
	void copy_replace_cleans_up_parked_copy();
	// A genuine mid-copy failure must roll the destination back to the parked
	// original and count as a failure; a user cancel must roll back too but count
	// as neither success nor failure.
	void copy_replace_midCopyFailure_restores_original();
	void copy_replace_cancel_restores_and_is_not_counted_failed();
	// The write-ahead log's whole purpose: the parked original's path must be
	// on disk *before* the copy that could strand it, not after.
	void copy_replace_journals_parked_path_while_still_in_flight();
	void copy_keepboth_policy_keeps_both();
	void copy_flatten_sameName_keepsBothFiles();

	// Move: destructive; same-volume rename removes the source.
	void move_happy_sameVolume_removesSource();
	void move_replace_policy_overwrites_and_cleans_up_parked();

	// Case-variant flatten (see review/findings-data-loss-2026-07.md, finding 1):
	// two selected files whose names differ only by case collide on a
	// case-insensitive destination. Either legal outcome is fine — kept both
	// via .Copy.NN where canonicalFilePath returns on-disk case, or the later
	// file skipped where it doesn't. Silent loss / silent overwrite is the
	// failure. CI runs these on real NTFS, which is the point: the collision
	// guard's behaviour there is otherwise unverified.
	void move_flatten_caseVariantNames_nothingLost();
	void copy_flatten_caseVariantNames_noSilentOverwrite();

	// A conflict the dialog never showed (the destination appeared after the
	// preview's sweep) must never be silently replaced (finding 2):
	// resolveConflict skips an unlisted conflict rather than defaulting it to
	// Replace.
	void move_conflictNotInPolicyMap_isSkippedNotReplaced();
	void copy_conflictNotInPolicyMap_isSkippedNotReplaced();

	// Finding 3: a restore that fails must stay visible. ParkedFile reports
	// stranded (and retries); the engine journals a dirty fail that survives
	// finish()+prune() so next-launch recovery can finish the rollback.
	void parkedFile_failed_restore_reports_stranded_and_retries();
	void copy_replace_strandedRestore_keepsDirtyJournal();

	// Move's cross-volume copy+delete leg, reached via the
	// MEDIAMUSTER_FORCE_MOVE_COPY seam — every test dir is same-volume, so
	// without it the rename path always wins and this leg (including the
	// finding-4 fsync-before-source-delete) would ship untested.
	void move_copyLeg_movesFileAndRemovesSource();
	void move_copyLeg_replace_overwritesAndCleansUpParked();

	// Finding 5: with the verification toggle OFF, nothing used to look at
	// the destination file at all before Move deleted the source. These pin
	// the two floors that now run regardless of the toggle: the destination
	// must be the size we wrote, and an ordinary verify-off copy must still
	// just work (no false alarms).
	void move_copyLeg_verifyOff_tamperedDestination_keepsSource();
	void copy_verifyOff_stillSucceeds();

	// Delete: routed to the OS trash (best-effort — see note in body).
	void delete_removes_file_from_original_location();

	// Finding 7: the _MediaMuster_Trash fallback must never hard-delete a
	// prior safety catch to make room — the second arrival gets a .Copy.NN
	// sibling instead. Uses the OS-trash and trash-root seams: without
	// them this branch aims at the real volume root and is untestable.
	void delete_fallbackTrash_neverOverwritesPriorCatch();

	// Finding 12: the per-volume OS-trash probe must clean up after itself
	// on volumes whose trash reports addresses (this dev machine's does;
	// the pathless case is platform behaviour a unit test can't force).
	void delete_osTrashProbe_leavesNoResidue();

	// Runs after every test (QTest hook). Clears the copy-path test seam and the
	// slowdown so a test that returns early on a failed QVERIFY can't leak them
	// into the next test.
	void cleanup();

private:
	// Aborts the test on failure.
	void touchFile(const QString &path);
	void writeFile(const QString &path, const QByteArray &contents);
	QByteArray readFile(const QString &path);

	/// Outlives every test function, so the env var stays pointing at a real
	/// directory for the whole run.
	QTemporaryDir m_oplogDir;
};

void TestMediaManagerPaths::initTestCase()
{
	QVERIFY(m_oplogDir.isValid());
	qputenv("MEDIAMUSTER_OPLOG_DIR", m_oplogDir.path().toUtf8());
}

void TestMediaManagerPaths::cleanupTestCase()
{
	qunsetenv("MEDIAMUSTER_OPLOG_DIR");
}

void TestMediaManagerPaths::touchFile(const QString &path)
{
	QFile f(path);
	QVERIFY2(f.open(QIODevice::WriteOnly),
			 qPrintable(QStringLiteral("failed to create %1: %2").arg(path, f.errorString())));
}

void TestMediaManagerPaths::writeFile(const QString &path, const QByteArray &contents)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	QVERIFY2(f.open(QIODevice::WriteOnly),
			 qPrintable(QStringLiteral("failed to create %1: %2").arg(path, f.errorString())));
	QCOMPARE(f.write(contents), qint64(contents.size()));
}

QByteArray TestMediaManagerPaths::readFile(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return f.readAll();
}

void TestMediaManagerPaths::buildDestPath_preserve_true_uses_avid_structure()
{
	MediaFile mf;
	mf.fileName = "clip.mxf";
	mf.mxfFolder = "5";
	QCOMPARE(MediaManager::buildDestPath(mf, "/dest", true),
			 QStringLiteral("/dest/Avid MediaFiles/MXF/5/clip.mxf"));
}

void TestMediaManagerPaths::buildDestPath_preserve_false_flattens()
{
	MediaFile mf;
	mf.fileName = "clip.mxf";
	mf.mxfFolder = "5";
	QCOMPARE(MediaManager::buildDestPath(mf, "/dest", false), QStringLiteral("/dest/clip.mxf"));
}

void TestMediaManagerPaths::generateRenamePath_first_slot_when_no_copies()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString target = tmp.path() + "/clip.mxf";
	touchFile(target);

	const auto out = MediaManager::generateRenamePath(target);
	QVERIFY(out.has_value());
	QCOMPARE(*out, tmp.path() + "/clip.Copy.01.mxf");
}

void TestMediaManagerPaths::generateRenamePath_skips_existing_slots()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString target = tmp.path() + "/clip.mxf";
	touchFile(target);
	touchFile(tmp.path() + "/clip.Copy.01.mxf");
	touchFile(tmp.path() + "/clip.Copy.02.mxf");

	const auto out = MediaManager::generateRenamePath(target);
	QVERIFY(out.has_value());
	QCOMPARE(*out, tmp.path() + "/clip.Copy.03.mxf");
}

void TestMediaManagerPaths::generateRenamePath_returns_nullopt_when_exhausted()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString target = tmp.path() + "/clip.mxf";
	touchFile(target);

	for (int n = 1; n <= 999; ++n)
	{
		const QString p =
			tmp.path() + QStringLiteral("/clip.Copy.%1.mxf").arg(n, 2, 10, QLatin1Char('0'));
		touchFile(p);
	}
	QCOMPARE(MediaManager::generateRenamePath(target), std::nullopt);
}

void TestMediaManagerPaths::generateRenamePath_handles_extensionless_path()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString target = tmp.path() + "/noextension";
	touchFile(target);

	const auto out = MediaManager::generateRenamePath(target);
	QVERIFY(out.has_value());
	QCOMPARE(*out, tmp.path() + "/noextension.Copy.01");
}

void TestMediaManagerPaths::move_flatten_sameName_keepsBothFiles()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Two sources in different folders, same filename — the flatten case.
	QVERIFY(QDir(tmp.path()).mkpath("a"));
	QVERIFY(QDir(tmp.path()).mkpath("b"));
	const QString srcA = tmp.path() + "/a/clip.mxf";
	const QString srcB = tmp.path() + "/b/clip.mxf";
	const QByteArray contentA = "this is file A";
	const QByteArray contentB = "this is a different file B";
	writeFile(srcA, contentA);
	writeFile(srcB, contentB);

	auto makeFile = [](const QString &path, const QByteArray &data)
	{
		MediaFile mf;
		mf.filePath = path;
		mf.fileName = "clip.mxf";
		mf.sizeBytes = data.size();
		return mf;
	};
	const QVector<MediaFile> files{makeFile(srcA, contentA), makeFile(srcB, contentB)};

	const QString dest = tmp.path() + "/dest";

	MediaManager mgr;
	QSignalSpy finishedSpy(&mgr, &MediaManager::operationFinished);
	mgr.executeMove(files, dest, /*preserveStructure=*/false, {});
	QVERIFY2(finishedSpy.wait(10000), "move did not finish in time");

	// Both succeeded; nothing failed.
	QCOMPARE(finishedSpy.count(), 1);
	QCOMPARE(finishedSpy.first().at(0).toInt(), 2); // succeeded
	QCOMPARE(finishedSpy.first().at(1).toInt(), 0); // failed

	// Processed in selection order: A claims the bare name, B is renamed.
	const QString destA = dest + "/clip.mxf";
	const QString destB = dest + "/clip.Copy.01.mxf";
	QVERIFY2(QFile::exists(destA), "first file missing at destination");
	QVERIFY2(QFile::exists(destB), "second file was not kept (would have been overwritten)");
	QCOMPARE(readFile(destA), contentA);
	QCOMPARE(readFile(destB), contentB);

	// Move is destructive: both sources should be gone.
	QVERIFY(!QFile::exists(srcA));
	QVERIFY(!QFile::exists(srcB));

}

void TestMediaManagerPaths::copy_writes_file_and_leaves_source()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	writeFile(src, "PAYLOAD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 7;

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, tmp.path() + "/dest", /*preserveStructure=*/false, {});
	QVERIFY2(finished.wait(10000), "copy did not finish in time");

	QCOMPARE(finished.first().at(0).toInt(), 1); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 0); // failed
	QCOMPARE(readFile(tmp.path() + "/dest/clip.mxf"), QByteArray("PAYLOAD"));
	QVERIFY2(QFile::exists(src), "copy must leave the source in place");
}

void TestMediaManagerPaths::copy_skip_policy_leaves_existing_destination()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;
	const QHash<QString, MediaManager::ConflictPolicy> pol{{src, MediaManager::ConflictPolicy::Skip}};

	MediaManager mgr;
	QSignalSpy itemDone(&mgr, &MediaManager::operationItemDone);
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, dest, false, pol);
	QVERIFY(finished.wait(10000));

	// Existing destination untouched.
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));
	// Reported skipped: success=true, skipped=true; counted as neither ok nor fail.
	QCOMPARE(itemDone.count(), 1);
	QCOMPARE(itemDone.first().at(2).toBool(), true); // success
	QCOMPARE(itemDone.first().at(4).toBool(), true); // skipped
	QCOMPARE(finished.first().at(0).toInt(), 0);
	QCOMPARE(finished.first().at(1).toInt(), 0);
}

void TestMediaManagerPaths::copy_replace_policy_overwrites_destination()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, dest, false, pol);
	QVERIFY(finished.wait(10000));

	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("NEW"));
	QCOMPARE(finished.first().at(0).toInt(), 1);
	QCOMPARE(finished.first().at(1).toInt(), 0);
}

void TestMediaManagerPaths::copy_replace_cleans_up_parked_copy()
{
	// A Replace copy parks the existing destination aside, writes the new data,
	// then commits by deleting the parked original. Guards that commit path:
	// the destination must be replaced AND no ".__copyreplace_" temp may be left
	// behind. (Mirrors move_replace_policy_overwrites_and_cleans_up_parked.)
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, dest, false, pol);
	QVERIFY(finished.wait(10000));

	QCOMPARE(finished.first().at(0).toInt(), 1);			   // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 0);			   // failed
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("NEW")); // destination replaced
	QVERIFY2(QFile::exists(src), "copy must leave the source in place");
	// The parked original must be committed (removed), never left behind.
	const QStringList leftovers = QDir(dest).entryList({"*__copyreplace*"}, QDir::Files);
	QVERIFY2(leftovers.isEmpty(), "parked original should be removed after a successful copy");
}

void TestMediaManagerPaths::cleanup()
{
	DebugSlowdown::setEnabled(false);
	qunsetenv("MEDIAMUSTER_DISABLE_CLONEFILE");
	qunsetenv("MEDIAMUSTER_FORCE_MOVE_COPY");
	qunsetenv("MEDIAMUSTER_DISABLE_OS_TRASH");
	qunsetenv("MEDIAMUSTER_TRASH_ROOT");
	// Verification is a process-wide toggle; a test that turns it off must
	// not leak that state into the next test.
	MediaManagerVerify::setEnabled(true);
}

void TestMediaManagerPaths::copy_replace_midCopyFailure_restores_original()
{
	// A genuine failure mid-copy (here the source changes size, which the copy
	// detects and rejects) must roll the destination back to the parked original,
	// count as a failure, and leave the source in place. Force the buffered copy
	// path and slow it so the concurrent grow lands after the size is captured but
	// before the post-copy check.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	DebugSlowdown::setEnabled(true);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, QByteArray(8 * 1024 * 1024, 'N')); // >= 2 buffer iterations
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 8 * 1024 * 1024;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	mgr.executeCopy({mf}, dest, false, pol);

	// Grow the source after the copy has captured its starting size but before it
	// stats it again at the end, so the size-change guard trips. Wait for the
	// loop's own tick — size captured, first buffer written, a full slowed
	// iteration still ahead — not a wall-clock guess (which lost on Windows CI
	// when the worker started late and the grow landed before the capture).
	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 10000);
	{
		QFile grow(src);
		QVERIFY(grow.open(QIODevice::Append));
		QCOMPARE(grow.write(QByteArray(4 * 1024 * 1024, 'X')), qint64(4 * 1024 * 1024));
		grow.close();
	}

	QVERIFY2(finished.wait(15000), "copy did not finish in time");

	QCOMPARE(finished.first().at(0).toInt(), 0); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 1); // failed (not silently dropped)
	// Rolled back to the original, not left as a half-written copy or nothing.
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));
	QVERIFY2(QDir(dest).entryList({"*__copyreplace*"}, QDir::Files).isEmpty(),
			 "the parked temp must not be left behind");
	QVERIFY2(QFile::exists(src), "a failed copy must leave the source in place");
}

void TestMediaManagerPaths::copy_replace_cancel_restores_and_is_not_counted_failed()
{
	// A user cancel mid-copy must roll the destination back to the parked original
	// AND count as neither a success nor a failure (the C2 accounting fix). Force
	// the buffered path and slow it so the cancel is observed mid-copy rather than
	// after an instant clonefile.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	DebugSlowdown::setEnabled(true);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, QByteArray(8 * 1024 * 1024, 'N'));
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 8 * 1024 * 1024;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	mgr.executeCopy({mf}, dest, false, pol);

	// Cancel only *after* the copy is underway, so it lands inside
	// copyFileWithProgress's byte loop rather than at doCopy's loop guard (which
	// would skip the copy entirely and pass the test for the wrong reason).
	// Waiting for the loop's own tick makes that deterministic on any runner.
	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 10000);
	mgr.cancel();
	QVERIFY2(finished.wait(15000), "copy did not finish in time");

	// The core C2 fix: a clean cancel is neither a success nor a failure.
	QCOMPARE(finished.first().at(0).toInt(), 0); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 0); // failed
	// And the parked original is restored, not lost.
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));
	QVERIFY2(QDir(dest).entryList({"*__copyreplace*"}, QDir::Files).isEmpty(),
			 "the parked temp must not be left behind");
	QVERIFY2(QFile::exists(src), "cancel must leave the source untouched");
}

void TestMediaManagerPaths::copy_replace_journals_parked_path_while_still_in_flight()
{
	// A write-ahead log that's only correct after the fact is no log at all:
	// the parked original's path has to be readable from disk during the copy,
	// because that is exactly the window a crash strands it in. Force the
	// buffered path and slow it down, then read the journal mid-flight.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	DebugSlowdown::setEnabled(true);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, QByteArray(8 * 1024 * 1024, 'N'));
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 8 * 1024 * 1024;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	mgr.executeCopy({mf}, dest, false, pol);

	// Wait for the byte loop's own tick — by then the original is parked and
	// journalled, and the copy is verifiably still in flight.
	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 10000);
	const QVector<OpJournal::Record> live = OpJournal::scan(m_oplogDir.path());
	QCOMPARE(live.size(), 1);
	QCOMPARE(live.first().kind, OpJournal::Kind::Copy);
	QCOMPARE(live.first().ops.size(), 1);

	const OpJournal::Entry &op = live.first().ops.first();
	QVERIFY2(!op.completed, "the op must still be open while the copy is in flight");
	QVERIFY2(!op.parked.isEmpty(), "the parked original's path must be journalled BEFORE the copy");
	QVERIFY2(op.parked.contains(QStringLiteral("__copyreplace_")), qPrintable(op.parked));
	QVERIFY2(QFile::exists(op.parked), "the journalled path must be where the original actually is");
	QCOMPARE(op.dst, dest + "/clip.mxf");

	QVERIFY2(finished.wait(15000), "copy did not finish in time");
	QCOMPARE(finished.first().at(0).toInt(), 1); // succeeded
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray(8 * 1024 * 1024, 'N'));

	// A concluded run has nothing to recover, so the journal is pruned.
	QVERIFY2(OpJournal::scan(m_oplogDir.path()).isEmpty(),
			 "a finished copy must not leave its journal behind");
}

void TestMediaManagerPaths::copy_keepboth_policy_keeps_both()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::KeepBoth}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, dest, false, pol);
	QVERIFY(finished.wait(10000));

	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));		   // original kept
	QCOMPARE(readFile(dest + "/clip.Copy.01.mxf"), QByteArray("NEW")); // new sits alongside
	QCOMPARE(finished.first().at(0).toInt(), 1);
}

void TestMediaManagerPaths::copy_flatten_sameName_keepsBothFiles()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(QDir(tmp.path()).mkpath("a"));
	QVERIFY(QDir(tmp.path()).mkpath("b"));
	const QString srcA = tmp.path() + "/a/clip.mxf";
	const QString srcB = tmp.path() + "/b/clip.mxf";
	writeFile(srcA, "AAA");
	writeFile(srcB, "BBB");

	auto mk = [](const QString &path, int n)
	{
		MediaFile mf;
		mf.filePath = path;
		mf.fileName = "clip.mxf";
		mf.sizeBytes = n;
		return mf;
	};
	const QString dest = tmp.path() + "/dest";

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mk(srcA, 3), mk(srcB, 3)}, dest, false, {});
	QVERIFY(finished.wait(10000));

	QCOMPARE(finished.first().at(0).toInt(), 2); // both copied
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("AAA"));
	QCOMPARE(readFile(dest + "/clip.Copy.01.mxf"), QByteArray("BBB"));
	QVERIFY(QFile::exists(srcA)); // copy leaves sources
	QVERIFY(QFile::exists(srcB));
}

void TestMediaManagerPaths::move_happy_sameVolume_removesSource()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	writeFile(src, "PAYLOAD");
	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 7;

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeMove({mf}, tmp.path() + "/dest", false, {});
	QVERIFY(finished.wait(10000));

	QCOMPARE(finished.first().at(0).toInt(), 1);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QCOMPARE(readFile(tmp.path() + "/dest/clip.mxf"), QByteArray("PAYLOAD"));
	QVERIFY2(!QFile::exists(src), "move must remove the source");

}

void TestMediaManagerPaths::move_replace_policy_overwrites_and_cleans_up_parked()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeMove({mf}, dest, false, pol);
	QVERIFY(finished.wait(10000));

	QCOMPARE(finished.first().at(0).toInt(), 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("NEW")); // replaced
	QVERIFY(!QFile::exists(src));							   // source gone
	// The parked original must be cleaned up on success, never left behind.
	const QStringList leftovers = QDir(dest).entryList({"*__movereplace*"}, QDir::Files);
	QVERIFY2(leftovers.isEmpty(), "parked original should be removed after a successful move");

}

void TestMediaManagerPaths::move_flatten_caseVariantNames_nothingLost()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString srcA = tmp.path() + "/a/A01.MXF";
	const QString srcB = tmp.path() + "/b/a01.mxf";
	const QByteArray contentA = "upper-case file A";
	const QByteArray contentB = "lower-case file B";
	writeFile(srcA, contentA);
	writeFile(srcB, contentB);

	auto makeFile = [](const QString &path)
	{
		MediaFile mf;
		mf.filePath = path;
		mf.fileName = QFileInfo(path).fileName();
		mf.sizeBytes = QFileInfo(path).size();
		return mf;
	};

	const QString dest = tmp.path() + "/dest";
	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeMove({makeFile(srcA), makeFile(srcB)}, dest, false, {});
	QVERIFY2(finished.wait(10000), "move did not finish in time");

	// The invariant that matters: neither file's bytes ceased to exist. Exact
	// names are platform-dependent (kept-both as .Copy.NN where
	// canonicalFilePath case-corrects, skipped where it doesn't) — both are
	// legal outcomes; silent loss is not.
	QByteArrayList everywhere;
	for (const QString &dir : {dest, tmp.path() + "/a", tmp.path() + "/b"})
	{
		const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files);
		for (const QFileInfo &fi : entries)
			everywhere << readFile(fi.filePath());
	}
	QVERIFY2(everywhere.contains(contentA), "file A's bytes vanished");
	QVERIFY2(everywhere.contains(contentB), "file B's bytes vanished");
	QCOMPARE(finished.first().at(1).toInt(), 0); // nothing may report as failed
}

void TestMediaManagerPaths::copy_flatten_caseVariantNames_noSilentOverwrite()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString srcA = tmp.path() + "/a/A01.MXF";
	const QString srcB = tmp.path() + "/b/a01.mxf";
	const QByteArray contentA = "upper-case file A";
	const QByteArray contentB = "lower-case file B";
	writeFile(srcA, contentA);
	writeFile(srcB, contentB);

	auto makeFile = [](const QString &path)
	{
		MediaFile mf;
		mf.filePath = path;
		mf.fileName = QFileInfo(path).fileName();
		mf.sizeBytes = QFileInfo(path).size();
		return mf;
	};

	const QString dest = tmp.path() + "/dest";
	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({makeFile(srcA), makeFile(srcB)}, dest, false, {});
	QVERIFY2(finished.wait(10000), "copy did not finish in time");

	// Copy never loses source bytes; the failure mode here is a *silent
	// overwrite* — reporting two successes while only one file landed. Every
	// claimed success must be a real, distinct file at the destination.
	const int succeeded = finished.first().at(0).toInt();
	QCOMPARE(finished.first().at(1).toInt(), 0); // no failures
	QByteArrayList destContents;
	const QFileInfoList entries = QDir(dest).entryInfoList(QDir::Files);
	for (const QFileInfo &fi : entries)
		destContents << readFile(fi.filePath());
	QCOMPARE(destContents.size(), succeeded);
	QVERIFY2(destContents.contains(contentA), "first file's copy was clobbered");
	if (succeeded == 2)
		QVERIFY2(destContents.contains(contentB), "two successes reported but file B's copy is missing");
}

void TestMediaManagerPaths::move_conflictNotInPolicyMap_isSkippedNotReplaced()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "FOREIGN"); // appeared after the dialog's sweep

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;

	MediaManager mgr;
	QSignalSpy itemDone(&mgr, &MediaManager::operationItemDone);
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeMove({mf}, dest, false, /*conflictPolicies=*/{}); // empty map = user never saw this conflict
	QVERIFY(finished.wait(10000));

	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("FOREIGN")); // untouched
	QVERIFY2(QFile::exists(src), "source must survive a skipped move");
	QCOMPARE(itemDone.count(), 1);
	QCOMPARE(itemDone.first().at(4).toBool(), true); // skipped, not failed
	QCOMPARE(finished.first().at(1).toInt(), 0);
}

void TestMediaManagerPaths::copy_conflictNotInPolicyMap_isSkippedNotReplaced()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "FOREIGN"); // appeared after the dialog's sweep

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;

	MediaManager mgr;
	QSignalSpy itemDone(&mgr, &MediaManager::operationItemDone);
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, dest, false, /*conflictPolicies=*/{}); // empty map = user never saw this conflict
	QVERIFY(finished.wait(10000));

	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("FOREIGN")); // untouched
	QVERIFY2(QFile::exists(src), "copy must leave the source in place");
	QCOMPARE(itemDone.count(), 1);
	QCOMPARE(itemDone.first().at(4).toBool(), true); // skipped, not failed
	QCOMPARE(finished.first().at(1).toInt(), 0);
}

void TestMediaManagerPaths::delete_removes_file_from_original_location()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/media/clip.mxf";
	writeFile(src, "DELME");
	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 5;

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeDelete({mf});
	QVERIFY(finished.wait(10000));

	// Delete routes to the OS trash (or the per-volume fallback); either way
	// the file must leave its original path and be reported as succeeded.
	QCOMPARE(finished.first().at(0).toInt(), 1);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QVERIFY2(!QFile::exists(src), "delete must remove the file from its original path");

}

void TestMediaManagerPaths::parkedFile_failed_restore_reports_stranded_and_retries()
{
#ifdef Q_OS_WIN
	QSKIP("Directory write-protection doesn't block renames the same way on Windows.");
#else
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dir = tmp.path() + "/slot";
	const QString dst = dir + "/clip.mxf";
	writeFile(dst, "ORIGINAL");

	ParkedFile park(dst, QLatin1String(".__test_"));
	QVERIFY(park.park());
	QVERIFY(!QFile::exists(dst)); // parked aside

	// Write-protect the directory so the rename home fails, the way a dying
	// volume would refuse it.
	QFile dirFile(dir);
	QVERIFY(dirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));

	QVERIFY(!park.restore());
	QVERIFY(park.isStranded());

	// The volume "comes back": a retry must succeed and clear the flag —
	// this is the destructor's second chance, and recovery's no-op case.
	QVERIFY(dirFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
								   QFileDevice::ExeOwner));
	QVERIFY(park.restore());
	QVERIFY(!park.isStranded());
	QCOMPARE(readFile(dst), QByteArray("ORIGINAL"));
#endif
}

void TestMediaManagerPaths::copy_replace_strandedRestore_keepsDirtyJournal()
{
	// The finding-3 scenario end-to-end: a Replace copy fails AND the
	// rollback can't put the parked original back. The journal must survive
	// finish()+prune() carrying a dirty fail so the next launch finishes the
	// rollback. Sabotage: steal the parked file mid-copy (the restore's
	// rename then fails exactly as on a yanked volume) and grow the source
	// so the size-change guard fails the copy.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	DebugSlowdown::setEnabled(true);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, QByteArray(8 * 1024 * 1024, 'N'));
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 8 * 1024 * 1024;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, dest, false, pol);

	QTest::qWait(120);
	// Steal the parked original out from under the run.
	const QStringList parkedNames = QDir(dest).entryList({"*__copyreplace*"}, QDir::Files);
	QVERIFY2(!parkedNames.isEmpty(), "expected the original to be parked by now");
	QVERIFY(QFile::remove(dest + "/" + parkedNames.first()));
	// And make the copy itself fail.
	{
		QFile grow(src);
		QVERIFY(grow.open(QIODevice::Append));
		QCOMPARE(grow.write(QByteArray(4 * 1024 * 1024, 'X')), qint64(4 * 1024 * 1024));
		grow.close();
	}

	QVERIFY2(finished.wait(15000), "copy did not finish in time");
	QCOMPARE(finished.first().at(0).toInt(), 0); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 1); // failed

	// The dirty record must have pinned the journal past finish()+prune().
	const QStringList journals =
		QDir(m_oplogDir.path()).entryList({"oplog-*.jsonl"}, QDir::Files);
	bool sawDirty = false;
	for (const QString &name : journals)
	{
		if (readFile(m_oplogDir.path() + "/" + name).contains("\"dirty\":true"))
			sawDirty = true;
		// Leave no cross-test state behind either way.
		QFile::remove(m_oplogDir.path() + "/" + name);
	}
	QVERIFY2(sawDirty, "the journal must survive with the rollback-incomplete marker");
}

void TestMediaManagerPaths::move_copyLeg_movesFileAndRemovesSource()
{
	// Force the copy+delete leg (and the buffered path within it, so the
	// finding-4 fsync-before-source-delete code actually runs).
	qputenv("MEDIAMUSTER_FORCE_MOVE_COPY", "1");
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	writeFile(src, "PAYLOAD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 7;

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeMove({mf}, tmp.path() + "/dest", false, {});
	QVERIFY2(finished.wait(10000), "move did not finish in time");

	QCOMPARE(finished.first().at(0).toInt(), 1); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 0); // failed
	QCOMPARE(readFile(tmp.path() + "/dest/clip.mxf"), QByteArray("PAYLOAD"));
	QVERIFY2(!QFile::exists(src), "the copy leg must remove the source after the synced copy");
}

void TestMediaManagerPaths::move_copyLeg_replace_overwritesAndCleansUpParked()
{
	// The copy leg's replace dance runs TWO parks — the outer one holding
	// the original until the source is truly gone, and the inner one that
	// only discards a partial write. This pins their choreography: original
	// replaced, source removed, no park temp left behind.
	qputenv("MEDIAMUSTER_FORCE_MOVE_COPY", "1");
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 3;
	const QHash<QString, MediaManager::ConflictPolicy> pol{
		{src, MediaManager::ConflictPolicy::Replace}};

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeMove({mf}, dest, false, pol);
	QVERIFY2(finished.wait(10000), "move did not finish in time");

	QCOMPARE(finished.first().at(0).toInt(), 1);			   // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 0);			   // failed
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("NEW")); // replaced
	QVERIFY(!QFile::exists(src));							   // source gone
	const QStringList leftovers = QDir(dest).entryList({"*__movereplace*"}, QDir::Files);
	QVERIFY2(leftovers.isEmpty(), "no park temp may survive a successful replace move");
}

void TestMediaManagerPaths::move_copyLeg_verifyOff_tamperedDestination_keepsSource()
{
	// The verify-off nightmare: the user turned verification off (Debug
	// menu, visible in beta), something interferes with the destination
	// mid-copy, and Move must still refuse to delete the source. Before the
	// finding-5 floors, the only checks were byte-counting and the SOURCE's
	// size — the destination was never looked at, so this move reported
	// success and destroyed the only real copy.
	MediaManagerVerify::setEnabled(false);
	qputenv("MEDIAMUSTER_FORCE_MOVE_COPY", "1");
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	DebugSlowdown::setEnabled(true);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, QByteArray(8 * 1024 * 1024, 'N'));

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 8 * 1024 * 1024;

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeMove({mf}, dest, false, {});

	// Tamper mid-copy: grow the destination file from outside, the way a
	// second writer or a misbehaving share would. The worker's own byte
	// count still comes out right, so only a real stat of the destination
	// can notice. Wait for the file to appear rather than a fixed delay —
	// doMove's slow-mode pause runs BEFORE the copy starts, so a timer
	// would fire while the destination doesn't exist yet; once it appears,
	// the slowed chunk loop leaves a comfortable window to tamper in.
	const QString dstPath = dest + "/clip.mxf";
	QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dstPath), 10000);
	{
		QFile tamper(dstPath);
		QVERIFY2(tamper.open(QIODevice::ReadWrite), "expected the destination to exist mid-copy");
		QVERIFY(tamper.resize(100 * 1024 * 1024));
		tamper.close();
	}

	QVERIFY2(finished.wait(15000), "move did not finish in time");
	QCOMPARE(finished.first().at(0).toInt(), 0); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 1); // failed
	QVERIFY2(QFile::exists(src),
			 "the source must survive when the destination can't be trusted");
}

void TestMediaManagerPaths::copy_verifyOff_stillSucceeds()
{
	// The floors must not cry wolf: a perfectly ordinary verify-off copy
	// still succeeds and lands byte-identical.
	MediaManagerVerify::setEnabled(false);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	writeFile(src, "PAYLOAD");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 7;

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeCopy({mf}, tmp.path() + "/dest", false, {});
	QVERIFY2(finished.wait(10000), "copy did not finish in time");

	QCOMPARE(finished.first().at(0).toInt(), 1);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QCOMPARE(readFile(tmp.path() + "/dest/clip.mxf"), QByteArray("PAYLOAD"));
	QVERIFY(QFile::exists(src));
}

void TestMediaManagerPaths::delete_fallbackTrash_neverOverwritesPriorCatch()
{
	// Delete a file, put a new file at the same path (restore, re-copy —
	// routine on a shared volume), delete again. The fallback trash used to
	// hard-delete the FIRST safety copy to clear the slot — the only place
	// in the app that permanently destroyed data without a confirmation.
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	qputenv("MEDIAMUSTER_TRASH_ROOT", tmp.path().toUtf8());

	const QString src = tmp.path() + "/media/clip.mxf";
	auto deleteOnce = [&](const QByteArray &content)
	{
		writeFile(src, content);
		MediaFile mf;
		mf.filePath = src;
		mf.fileName = "clip.mxf";
		mf.sizeBytes = content.size();
		MediaManager mgr;
		QSignalSpy finished(&mgr, &MediaManager::operationFinished);
		mgr.executeDelete({mf});
		QVERIFY2(finished.wait(10000), "delete did not finish in time");
		QCOMPARE(finished.first().at(0).toInt(), 1);
		QCOMPARE(finished.first().at(1).toInt(), 0);
	};

	deleteOnce("FIRST");
	const QString firstCatch = tmp.path() + "/_MediaMuster_Trash/media/clip.mxf";
	QCOMPARE(readFile(firstCatch), QByteArray("FIRST"));

	deleteOnce("SECOND");

	// The first catch is sacred; the second arrival takes a sibling name.
	QCOMPARE(readFile(firstCatch), QByteArray("FIRST"));
	QCOMPARE(readFile(tmp.path() + "/_MediaMuster_Trash/media/clip.Copy.01.mxf"),
			 QByteArray("SECOND"));
}

void TestMediaManagerPaths::delete_osTrashProbe_leavesNoResidue()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/media/clip.mxf";
	writeFile(src, "DELME");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 5;

	MediaManager mgr;
	QSignalSpy finished(&mgr, &MediaManager::operationFinished);
	mgr.executeDelete({mf});
	QVERIFY2(finished.wait(10000), "delete did not finish in time");

	QCOMPARE(finished.first().at(0).toInt(), 1);
	QVERIFY(!QFile::exists(src));
	// The probe scratch must be gone from the source folder either way,
	// and (on an address-reporting volume) tidied out of the trash too.
	const QStringList residue = QDir(tmp.path() + "/media")
									.entryList({QStringLiteral(".mm_trashprobe_*")},
											   QDir::Files | QDir::Hidden);
	QVERIFY2(residue.isEmpty(), "trash probe scratch must not linger in the source folder");
}

QTEST_GUILESS_MAIN(TestMediaManagerPaths)
#include "tst_mediamanager_paths.moc"