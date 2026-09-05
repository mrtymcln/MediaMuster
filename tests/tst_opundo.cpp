#include "opjournal.h"
#include "oprunner.h"
#include "opundo.h"

#include "testutil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <memory>

// OpUndo — Edit ▸ Undo, driven synchronously: a REAL forward run (through
// OpRunner) writes a real journal, then OpUndo reverses it and the disk is
// checked. That round-trip is the contract: whatever the engine does, one
// press of Undo puts back.
//
// Trash env seams route every disposal into a sandbox _MediaMuster_Trash
// so tests never touch the real OS trash (and can inspect the catches).

namespace
{
	struct SinkItem
	{
		QString name;
		QString path;
		QString error;
		bool ok = false;
		bool skipped = false;
	};

	struct TestSink : OpSink
	{
		QVector<SinkItem> items;
		QStringList logs;
		void progress(const QString &, int, int, double) override {}
		void itemDone(const QString &name, const QString &path, bool ok, const QString &error,
					  bool skipped) override
		{
			items.append({name, path, error, ok, skipped});
		}
		void log(QtMsgType, const QString &message) override { logs.append(message); }
		void trashUsed(const QString &, int) override {}

		QString allErrors() const
		{
			QStringList e;
			for (const SinkItem &it : items)
				if (!it.error.isEmpty())
					e << it.error;
			return e.join(QStringLiteral(" | "));
		}
	};
} // namespace

class TestOpUndo : public QObject
{
	Q_OBJECT

private slots:
	void init();
	void cleanup();

	void undo_copy_trashes_the_copy_and_stamps_undone();
	void undo_copy_replace_restores_the_replaced_original();
	void undo_move_renames_back_same_volume();
	void undo_move_copyleg_copies_back_and_trashes_far_copy();
	void undo_delete_restores_from_trash();
	void undo_rename_renames_back_and_resets_avid_dbs();
	void undo_skips_already_undone_items();
	void undo_refuses_identity_drift_and_keeps_candidacy();
	void undo_writes_its_own_journal();

private:
	// One FRESH sandbox per test (init() recreates it): media tree,
	// journal dir, and a trash root the env seams point every disposal at.
	std::unique_ptr<QTemporaryDir> m_tmp;
	QString m_journal;
	QString m_trashRoot;

	QString path(const char *rel) const { return m_tmp->path() + QLatin1Char('/') + rel; }

	/// Run one forward request through the real runner, into m_journal.
	OpRunner::Totals runForward(const OpRequest &req);
	/// The newest undoable journal in m_journal — must exist.
	QString undoablePath();
	/// Run OpUndo on the newest undoable journal, returning its totals.
	OpRunner::Totals runUndo(TestSink &sink);

	OpItem item(const QString &src, const QString &policy = QString());
};

void TestOpUndo::init()
{
	m_tmp = std::make_unique<QTemporaryDir>();
	QVERIFY(m_tmp->isValid());
	m_journal = path("journal");
	// The trash root must be an ANCESTOR of the files being trashed (the
	// router mirrors each file's path relative to it, exactly as the
	// real volume root is an ancestor of everything on the volume) — so
	// the sandbox root itself plays the volume root.
	m_trashRoot = m_tmp->path();
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");
	qputenv("MEDIAMUSTER_TRASH_ROOT", m_trashRoot.toUtf8());
}

void TestOpUndo::cleanup()
{
	qunsetenv("MEDIAMUSTER_DISABLE_OS_TRASH");
	qunsetenv("MEDIAMUSTER_TRASH_ROOT");
	qunsetenv("MEDIAMUSTER_FORCE_MOVE_COPY");
}

OpItem TestOpUndo::item(const QString &src, const QString &policy)
{
	OpItem it;
	it.src = src;
	it.name = QFileInfo(src).fileName();
	it.bytes = QFileInfo(src).size();
	it.policy = policy;
	return it;
}

OpRunner::Totals TestOpUndo::runForward(const OpRequest &req)
{
	TestSink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	return runner.run(req, m_journal);
}

QString TestOpUndo::undoablePath()
{
	const auto rec = OpJournal::latestUndoable(m_journal);
	return rec ? rec->path : QString();
}

OpRunner::Totals TestOpUndo::runUndo(TestSink &sink)
{
	const QString journal = undoablePath();
	if (journal.isEmpty())
		return {};
	std::atomic<bool> cancel{false};
	OpUndo undo(sink, cancel);
	return undo.run(journal, m_journal);
}

// MARK: - Copy

void TestOpUndo::undo_copy_trashes_the_copy_and_stamps_undone()
{
	const QString src = path("src/clip.mxf");
	writeFile(src, "CLIP BYTES");
	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = path("dest");
	req.items = {item(src)};
	QCOMPARE(runForward(req).succeeded, 1);
	QVERIFY(QFile::exists(path("dest/clip.mxf")));

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QVERIFY2(t.succeeded == 1, qPrintable(sink.allErrors()));
	QCOMPARE(t.failed, 0);

	// The copy is gone from the destination — but NOT hard-deleted: it
	// sits in the sandboxed trash.
	QVERIFY(!QFile::exists(path("dest/clip.mxf")));
	QCOMPARE(readFile(src), QByteArray("CLIP BYTES"));
	QVERIFY(!QDir(m_trashRoot + "/_MediaMuster_Trash").entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

	// Single-level undo: the candidate is spent.
	QVERIFY(undoablePath().isEmpty());
}

void TestOpUndo::undo_copy_replace_restores_the_replaced_original()
{
	const QString src = path("src/clip.mxf");
	const QString dst = path("dest/clip.mxf");
	writeFile(src, "NEW BYTES!");
	writeFile(dst, "OLD ORIGINAL");

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = path("dest");
	req.items = {item(src, conflictPolicyName(ConflictPolicy::Replace))};
	QCOMPARE(runForward(req).succeeded, 1);
	QCOMPARE(readFile(dst), QByteArray("NEW BYTES!"));

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QCOMPARE(t.failed, 0);
	QCOMPARE(t.succeeded, 1);

	// The full Replace round-trip: the copy is in the trash and the file
	// it replaced is back in its slot, byte for byte.
	QCOMPARE(readFile(dst), QByteArray("OLD ORIGINAL"));
	QCOMPARE(readFile(src), QByteArray("NEW BYTES!"));
}

// MARK: - Move

void TestOpUndo::undo_move_renames_back_same_volume()
{
	const QString src = path("src/clip.mxf");
	writeFile(src, "MOVED BYTES");
	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = path("dest");
	req.items = {item(src)};
	QCOMPARE(runForward(req).succeeded, 1);
	QVERIFY(!QFile::exists(src));

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QCOMPARE(t.failed, 0);
	QCOMPARE(t.succeeded, 1);

	QCOMPARE(readFile(src), QByteArray("MOVED BYTES"));
	QVERIFY(!QFile::exists(path("dest/clip.mxf")));
	QVERIFY(undoablePath().isEmpty());
}

void TestOpUndo::undo_move_copyleg_copies_back_and_trashes_far_copy()
{
	// FORCE_MOVE_COPY makes both the forward move and the undo take the
	// cross-volume leg (every QTemporaryDir is one volume, so the rename
	// path would otherwise always win) — this is the leg where undo must
	// copy back, verify, and trash the far copy.
	qputenv("MEDIAMUSTER_FORCE_MOVE_COPY", "1");

	const QString src = path("src/clip.mxf");
	writeFile(src, "FAR BYTES");
	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = path("dest");
	req.items = {item(src)};
	QCOMPARE(runForward(req).succeeded, 1);
	QVERIFY(!QFile::exists(src));

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QCOMPARE(t.failed, 0);
	QCOMPARE(t.succeeded, 1);

	// Home again, verified; the far copy went to the trash, not unlink.
	QCOMPARE(readFile(src), QByteArray("FAR BYTES"));
	QVERIFY(!QFile::exists(path("dest/clip.mxf")));
	QVERIFY(!QDir(m_trashRoot + "/_MediaMuster_Trash").entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

// MARK: - Delete

void TestOpUndo::undo_delete_restores_from_trash()
{
	const QString src = path("media/clip.mxf");
	writeFile(src, "DELETED BYTES");
	OpRequest req;
	req.kind = OpKind::Delete;
	req.items = {item(src)};
	QCOMPARE(runForward(req).succeeded, 1);
	QVERIFY(!QFile::exists(src));

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QCOMPARE(t.failed, 0);
	QCOMPARE(t.succeeded, 1);

	QCOMPARE(readFile(src), QByteArray("DELETED BYTES"));
	QVERIFY(undoablePath().isEmpty());
}

// MARK: - Rename

void TestOpUndo::undo_rename_renames_back_and_resets_avid_dbs()
{
	const QString src = path("mxf/1/clip.mxf");
	const QString dst = path("mxf/2/clip.mxf");
	writeFile(src, "RENAMED BYTES");
	QDir().mkpath(path("mxf/2"));

	OpRequest req;
	req.kind = OpKind::Rename;
	OpItem it = item(src);
	it.renameDst = dst;
	req.items = {it};
	QCOMPARE(runForward(req).succeeded, 1);
	QVERIFY(!QFile::exists(src));

	// Fresh Avid databases appear (Avid rebuilt them since the forward
	// run); the undo must delete them again — stale databases after a
	// rename-back would read as "No reference", the cull-risk state.
	writeFile(path("mxf/1/msmMMOB.mdb"), "stale");
	writeFile(path("mxf/2/msmFMID.pmr"), "stale");

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QCOMPARE(t.failed, 0);
	QCOMPARE(t.succeeded, 1);

	QCOMPARE(readFile(src), QByteArray("RENAMED BYTES"));
	QVERIFY(!QFile::exists(dst));
	QVERIFY(!QFile::exists(path("mxf/1/msmMMOB.mdb")));
	QVERIFY(!QFile::exists(path("mxf/2/msmFMID.pmr")));
}

// MARK: - Convergence and refusal

void TestOpUndo::undo_skips_already_undone_items()
{
	const QString srcA = path("src/a.mxf");
	const QString srcB = path("src/b.mxf");
	writeFile(srcA, "AAAA");
	writeFile(srcB, "BBBB");
	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = path("dest");
	req.items = {item(srcA), item(srcB)};
	QCOMPARE(runForward(req).succeeded, 2);

	// Simulate a half-finished earlier undo: A's copy is already gone.
	QVERIFY(QFile::remove(path("dest/a.mxf")));

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QCOMPARE(t.failed, 0);
	QCOMPARE(t.succeeded, 1); // B
	QCOMPARE(t.skipped, 1);	  // A: already undone, quietly

	QVERIFY(!QFile::exists(path("dest/b.mxf")));
	// Clean finish (no failures) — the candidate is spent.
	QVERIFY(undoablePath().isEmpty());
}

void TestOpUndo::undo_refuses_identity_drift_and_keeps_candidacy()
{
	const QString src = path("src/clip.mxf");
	writeFile(src, "REAL BYTES");
	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = path("dest");
	req.items = {item(src)};
	QCOMPARE(runForward(req).succeeded, 1);

	// Someone replaced the landed copy with a different file (same name,
	// different size). Undo must NOT trash it.
	writeFile(path("dest/clip.mxf"), "SOMETHING ELSE ENTIRELY");

	TestSink sink;
	const OpRunner::Totals t = runUndo(sink);
	QCOMPARE(t.succeeded, 0);
	QCOMPARE(t.failed, 1);
	QVERIFY2(sink.allErrors().contains(QStringLiteral("left alone")),
			 qPrintable(sink.allErrors()));

	QCOMPARE(readFile(path("dest/clip.mxf")), QByteArray("SOMETHING ELSE ENTIRELY"));
	// A failed undo does NOT stamp the original: after the user sorts
	// the folder out, Undo can be pressed again.
	QCOMPARE(undoablePath(), undoablePath()); // still present
	QVERIFY(!undoablePath().isEmpty());
}

void TestOpUndo::undo_writes_its_own_journal()
{
	const QString src = path("src/clip.mxf");
	writeFile(src, "BYTES");
	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = path("dest");
	req.items = {item(src)};
	QCOMPARE(runForward(req).succeeded, 1);

	TestSink sink;
	QCOMPARE(runUndo(sink).succeeded, 1);

	// The undo run left its own finished journal behind (kind undo,
	// naming what it reversed), and the original carries the undone
	// stamp — so a crash mid-undo would have been recoverable, and a
	// double-undo is structurally impossible.
	bool sawUndoJournal = false;
	bool sawUndoneOriginal = false;
	for (const OpJournal::Record &rec : OpJournal::scan(m_journal))
	{
		if (rec.kind == OpKind::Undo && rec.complete && rec.originalKind == OpKind::Copy)
			sawUndoJournal = true;
		if (rec.kind == OpKind::Copy && rec.undone)
			sawUndoneOriginal = true;
	}
	QVERIFY(sawUndoJournal);
	QVERIFY(sawUndoneOriginal);
}

QTEST_APPLESS_MAIN(TestOpUndo)
#include "tst_opundo.moc"
