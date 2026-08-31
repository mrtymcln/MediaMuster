#include "debugslowdown.h"
#include "mediamanagerverify.h"
#include "opjournal.h"
#include "opmanager.h"

#include "testutil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// OpManager — the engine v2 facade, driven through its real signals on
// its real worker thread. This suite owns the MID-FLIGHT behaviours the
// synchronous runner tests can't reach: reading the journal while a copy
// is still running, mutating a source mid-copy, cancelling mid-copy,
// stranding a park, tampering with a destination. Ports of the v1
// tst_mediamanager_paths tests, with one deliberate flip: a finished
// run's journal now SURVIVES (it is the undo candidate) where v1 pruned
// it.

namespace
{
	/// The production dispatch shape (MainWindow::dispatchOperation): the
	/// selection becomes a request via itemsFromMediaFiles, the engine
	/// runs it. The per-kind convenience wrappers these tests used to call
	/// were removed 2026-08-31 as dead code.
	void dispatch(OpManager &mgr, OpKind kind, const QVector<MediaFile> &files,
				  const QString &destRoot = {}, bool preserve = false,
				  const QHash<QString, ConflictPolicy> &policies = {})
	{
		OpRequest req;
		req.kind = kind;
		req.destRoot = destRoot;
		req.preserve = preserve;
		req.items = OpManager::itemsFromMediaFiles(files, policies);
		mgr.execute(std::move(req));
	}
} // namespace

class TestOpManager : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase();
	void cleanupTestCase();
	void cleanup();

	void items_from_mediafiles_carry_scan_claims();
	void copy_run_writes_its_plan_before_the_first_op();
	void copy_replace_journals_parked_path_while_still_in_flight();
	void copy_replace_midCopyFailure_restores_original();
	void copy_replace_cancel_restores_and_is_not_counted_failed();
	void copy_replace_strandedRestore_keepsDirtyJournal();
	void move_copyLeg_verifyOff_tamperedDestination_keepsSource();
	void delete_osTrashProbe_leavesNoResidue();

private:

	/// Outlives every test function, so the env var stays pointing at a
	/// real directory for the whole run.
	QTemporaryDir m_journalDir;
};

void TestOpManager::initTestCase()
{
	QVERIFY(m_journalDir.isValid());
	qputenv("MEDIAMUSTER_JOURNAL_DIR", m_journalDir.path().toUtf8());
}

void TestOpManager::cleanupTestCase()
{
	qunsetenv("MEDIAMUSTER_JOURNAL_DIR");
}

void TestOpManager::cleanup()
{
	DebugSlowdown::setEnabled(false);
	qunsetenv("MEDIAMUSTER_DISABLE_CLONEFILE");
	qunsetenv("MEDIAMUSTER_DISABLE_COPYFILEEX");
	qunsetenv("MEDIAMUSTER_FORCE_MOVE_COPY");
	qunsetenv("MEDIAMUSTER_DISABLE_OS_TRASH");
	qunsetenv("MEDIAMUSTER_TRASH_ROOT");
	MediaManagerVerify::setEnabled(true);
	// v2 retention keeps finished journals around — wipe them between
	// tests or every scan-based assertion sees its predecessors' runs.
	const QDir journal(m_journalDir.path());
	for (const QString &name : journal.entryList({"journal-*.jsonl"}, QDir::Files))
		QFile::remove(journal.filePath(name));
}

void TestOpManager::items_from_mediafiles_carry_scan_claims()
{
	MediaFile mf;
	mf.filePath = "/vol/Avid MediaFiles/MXF/1/a.mxf";
	mf.fileName = "a.mxf";
	mf.mxfFolder = "1";
	mf.sizeBytes = 100;
	mf.mobId = QStringLiteral("060a2b34...file");
	mf.masterMobId = QStringLiteral("060a2b34...master");
	mf.clipName = QStringLiteral("A001_C002");
	const QHash<QString, ConflictPolicy> pol{{mf.filePath, ConflictPolicy::KeepBoth}};

	const auto items = OpManager::itemsFromMediaFiles({mf}, pol);
	QCOMPARE(items.size(), 1);
	QCOMPARE(items[0].src, mf.filePath);
	QCOMPARE(items[0].name, mf.fileName);
	QCOMPARE(items[0].folder, mf.mxfFolder);
	QCOMPARE(items[0].bytes, mf.sizeBytes);
	QCOMPARE(items[0].policy, QStringLiteral("keepboth"));
	// The scan's Avid identity claims travel with the item — they are
	// what the runner cross-checks and what makes journals readable.
	QCOMPARE(items[0].mobId, mf.mobId);
	QCOMPARE(items[0].masterMobId, mf.masterMobId);
	QCOMPARE(items[0].clipName, mf.clipName);

	// The dialog's preview static keeps MediaManager's shape.
	QCOMPARE(OpManager::buildDestPath(mf, "/dest", true),
			 QStringLiteral("/dest/Avid MediaFiles/MXF/1/a.mxf"));
	QCOMPARE(OpManager::buildDestPath(mf, "/dest", false), QStringLiteral("/dest/a.mxf"));
}

void TestOpManager::copy_run_writes_its_plan_before_the_first_op()
{
	// Force the buffered path and slow it down so the journal can be read
	// while the run is still in flight.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
	DebugSlowdown::setEnabled(true);

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString srcDir = tmp.path() + "/src";
	const QString dest = tmp.path() + "/dest";
	QDir().mkpath(srcDir);
	QDir().mkpath(dest);
	writeFile(srcDir + "/a.mxf", QByteArray(8 * 1024 * 1024, 'A'));
	writeFile(srcDir + "/b.mxf", "BBBBBB");
	writeFile(dest + "/b.mxf", "OLD"); // b already exists -> Skip policy

	MediaFile a;
	a.filePath = srcDir + "/a.mxf";
	a.fileName = "a.mxf";
	a.sizeBytes = 8 * 1024 * 1024;
	MediaFile b;
	b.filePath = srcDir + "/b.mxf";
	b.fileName = "b.mxf";
	b.sizeBytes = 6;
	const QHash<QString, ConflictPolicy> pol{{b.filePath, ConflictPolicy::Skip}};

	OpManager mgr;
	QSignalSpy finished(&mgr, &OpManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	dispatch(mgr, OpKind::Copy, {a, b}, dest, false, pol);
	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 20000);

	// Mid-flight: the plan must already be on disk — written before the
	// first op line, i.e. before any file was touched.
	const QVector<OpJournal::Record> recs = OpJournal::scan(m_journalDir.path());
	QCOMPARE(recs.size(), 1);
	QVERIFY2(recs.first().hasPlan, "the plan line must be written at the start of the run");
	QCOMPARE(recs.first().plan.size(), 2);
	QCOMPARE(recs.first().planDest, dest);
	QCOMPARE(recs.first().plan[1].src, b.filePath);
	QCOMPARE(recs.first().plan[1].policy, QStringLiteral("skip"));

	QVERIFY2(finished.wait(60000), "copy did not finish in time");
	QCOMPARE(finished.count(), 1);
	QCOMPARE(readFile(dest + "/a.mxf"), QByteArray(8 * 1024 * 1024, 'A'));
	QCOMPARE(readFile(dest + "/b.mxf"), QByteArray("OLD"));

	// The v2 flip: a finished run KEEPS its journal — it is the undo
	// candidate now (v1 pruned it here, which is why undo had nothing
	// to work with).
	const auto after = OpJournal::scan(m_journalDir.path());
	QCOMPARE(after.size(), 1);
	QVERIFY(after.first().complete);
	QVERIFY(OpJournal::latestUndoable(m_journalDir.path()).has_value());
}

void TestOpManager::copy_replace_journals_parked_path_while_still_in_flight()
{
	// A write-ahead log that's only correct after the fact is no log at
	// all: the parked original's path has to be readable FROM DISK
	// during the copy, because that is exactly the window a crash
	// strands it in.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
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
	const QHash<QString, ConflictPolicy> pol{{src, ConflictPolicy::Replace}};

	OpManager mgr;
	QSignalSpy finished(&mgr, &OpManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	dispatch(mgr, OpKind::Copy, {mf}, dest, false, pol);

	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 20000);
	const QVector<OpJournal::Record> live = OpJournal::scan(m_journalDir.path());
	QCOMPARE(live.size(), 1);
	QCOMPARE(live.first().kind, OpKind::Copy);
	QCOMPARE(live.first().ops.size(), 1);

	const OpJournal::Entry &op = live.first().ops.first();
	QVERIFY2(!op.completed, "the op must still be open while the copy is in flight");
	QVERIFY2(!op.parked.isEmpty(),
			 "the parked original's path must be journaled BEFORE the copy");
	QVERIFY2(op.parked.contains(QStringLiteral("__copyreplace_")), qPrintable(op.parked));
	QVERIFY2(QFile::exists(op.parked),
			 "the journaled path must be where the original actually is");
	// v2: the replaced file's own identity is on the op line too.
	QCOMPARE(op.parkedOriginalId.size, qint64(3));

	QVERIFY2(finished.wait(60000), "copy did not finish in time");
	QCOMPARE(finished.first().at(0).toInt(), 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray(8 * 1024 * 1024, 'N'));

	// The finished journal records where the replaced original went.
	const auto after = OpJournal::scan(m_journalDir.path());
	QCOMPARE(after.size(), 1);
	QVERIFY(!after.first().ops.first().parkedFinal.isEmpty());
	QCOMPARE(readFile(after.first().ops.first().parkedFinal), QByteArray("OLD"));
}

void TestOpManager::copy_replace_midCopyFailure_restores_original()
{
	// A genuine failure mid-copy (the source changes size, which the
	// engine detects and rejects) must roll the destination back to the
	// parked original, count as a failure, and leave the source alone.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
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
	const QHash<QString, ConflictPolicy> pol{{src, ConflictPolicy::Replace}};

	OpManager mgr;
	QSignalSpy finished(&mgr, &OpManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	dispatch(mgr, OpKind::Copy, {mf}, dest, false, pol);

	// Grow the source after the copy has captured its starting size but
	// before it stats it again at the end. The loop's own tick makes
	// that deterministic on any runner.
	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 20000);
	{
		QFile grow(src);
		QVERIFY(grow.open(QIODevice::Append));
		QCOMPARE(grow.write(QByteArray(4 * 1024 * 1024, 'X')), qint64(4 * 1024 * 1024));
		grow.close();
	}

	QVERIFY2(finished.wait(60000), "copy did not finish in time");
	QCOMPARE(finished.first().at(0).toInt(), 0); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 1); // failed (not silently dropped)
	// Rolled back to the original, not left as a half-written copy.
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));
	QVERIFY2(QDir(dest).entryList({"*__copyreplace*"}, QDir::Files).isEmpty(),
			 "the parked temp must not be left behind");
	QVERIFY2(QFile::exists(src), "a failed copy must leave the source in place");
}

void TestOpManager::copy_replace_cancel_restores_and_is_not_counted_failed()
{
	// A user cancel mid-copy must roll the destination back to the
	// parked original AND count as neither a success nor a failure.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
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
	const QHash<QString, ConflictPolicy> pol{{src, ConflictPolicy::Replace}};

	OpManager mgr;
	QSignalSpy finished(&mgr, &OpManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	dispatch(mgr, OpKind::Copy, {mf}, dest, false, pol);

	// Cancel only *after* the copy is underway, so it lands inside the
	// byte loop rather than at the run loop's guard.
	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 20000);
	mgr.cancel();
	QVERIFY2(finished.wait(60000), "copy did not finish in time");

	// finished fires EXACTLY once, and a clean cancel is neither a
	// success nor a failure.
	QCOMPARE(finished.count(), 1);
	QCOMPARE(finished.first().at(0).toInt(), 0);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));
	QVERIFY2(QDir(dest).entryList({"*__copyreplace*"}, QDir::Files).isEmpty(),
			 "the parked temp must not be left behind");
	QVERIFY2(QFile::exists(src), "cancel must leave the source untouched");
}

void TestOpManager::copy_replace_strandedRestore_keepsDirtyJournal()
{
	// The finding-3 scenario end-to-end: a Replace copy fails AND the
	// rollback can't put the parked original back. The journal must
	// survive carrying a dirty fail so the next launch finishes the
	// rollback. Sabotage: steal the parked file mid-copy (the restore's
	// rename then fails as on a yanked volume) and grow the source so
	// the size-change guard fails the copy.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
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
	const QHash<QString, ConflictPolicy> pol{{src, ConflictPolicy::Replace}};

	OpManager mgr;
	QSignalSpy finished(&mgr, &OpManager::operationFinished);
	const quint64 ticks = DebugSlowdown::copyLoopTicks().load();
	dispatch(mgr, OpKind::Copy, {mf}, dest, false, pol);

	// By the first tick the original is parked; steal it, then break
	// the copy.
	QTRY_VERIFY_WITH_TIMEOUT(DebugSlowdown::copyLoopTicks().load() > ticks, 20000);
	const QStringList parkedNames = QDir(dest).entryList({"*__copyreplace*"}, QDir::Files);
	QVERIFY2(!parkedNames.isEmpty(), "expected the original to be parked by now");
	QVERIFY(QFile::remove(dest + "/" + parkedNames.first()));
	{
		QFile grow(src);
		QVERIFY(grow.open(QIODevice::Append));
		QCOMPARE(grow.write(QByteArray(4 * 1024 * 1024, 'X')), qint64(4 * 1024 * 1024));
		grow.close();
	}

	QVERIFY2(finished.wait(60000), "copy did not finish in time");
	QCOMPARE(finished.first().at(0).toInt(), 0);
	QCOMPARE(finished.first().at(1).toInt(), 1);

	// The dirty record pinned the journal, and a dirty journal is
	// recovery's business — never an undo candidate.
	const QStringList journals =
		QDir(m_journalDir.path()).entryList({"journal-*.jsonl"}, QDir::Files);
	bool sawDirty = false;
	for (const QString &name : journals)
		if (readFile(m_journalDir.path() + "/" + name).contains("\"dirty\":true"))
			sawDirty = true;
	QVERIFY2(sawDirty, "the journal must survive with the rollback-incomplete marker");
	QVERIFY(!OpJournal::latestUndoable(m_journalDir.path()).has_value());
}

void TestOpManager::move_copyLeg_verifyOff_tamperedDestination_keepsSource()
{
	// The verify-off nightmare: verification is off (Debug menu),
	// something interferes with the destination mid-copy, and Move must
	// STILL refuse to delete the source — the unconditional floors are
	// what stand between this and destroying the only real copy.
	MediaManagerVerify::setEnabled(false);
	qputenv("MEDIAMUSTER_FORCE_MOVE_COPY", "1");
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
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

	OpManager mgr;
	QSignalSpy finished(&mgr, &OpManager::operationFinished);
	dispatch(mgr, OpKind::Move, {mf}, dest);

	// Tamper mid-copy: grow the destination file from outside, the way
	// a second writer or a misbehaving share would. Only a real stat of
	// the destination can notice.
	const QString dstPath = dest + "/clip.mxf";
	QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dstPath), 20000);
	{
		QFile tamper(dstPath);
		QVERIFY2(tamper.open(QIODevice::ReadWrite),
				 "expected the destination to exist mid-copy");
		QVERIFY(tamper.resize(100 * 1024 * 1024));
		tamper.close();
	}

	QVERIFY2(finished.wait(60000), "move did not finish in time");
	QCOMPARE(finished.first().at(0).toInt(), 0);
	QCOMPARE(finished.first().at(1).toInt(), 1);
	QVERIFY2(QFile::exists(src),
			 "the source must survive when the destination can't be trusted");
}

void TestOpManager::delete_osTrashProbe_leavesNoResidue()
{
	// The real OS trash path: the per-volume probe must clean up after
	// itself — no `.mm_trashprobe_*` scratch left in the media folder
	// (the deleted file itself lands in the user's real trash, which is
	// the point of the probe passing).
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mediaDir = tmp.path() + "/media";
	const QString src = mediaDir + "/clip.mxf";
	writeFile(src, "PROBED");

	MediaFile mf;
	mf.filePath = src;
	mf.fileName = "clip.mxf";
	mf.sizeBytes = 6;

	OpManager mgr;
	QSignalSpy finished(&mgr, &OpManager::operationFinished);
	dispatch(mgr, OpKind::Delete, {mf});
	QVERIFY2(finished.wait(20000), "delete did not finish in time");

	QCOMPARE(finished.first().at(0).toInt(), 1);
	QVERIFY(!QFile::exists(src));
	const QStringList residue =
		QDir(mediaDir).entryList({QStringLiteral(".mm_trashprobe_*")},
								 QDir::Files | QDir::Hidden);
	QVERIFY2(residue.isEmpty(), "the trash probe must not leave scratch files behind");
}

QTEST_GUILESS_MAIN(TestOpManager)
#include "tst_opmanager.moc"
