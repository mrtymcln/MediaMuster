#include "opjournal.h"
#include "oprecovery.h"
#include "probesweep.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace
{
	// A host string that can never match this machine, so recovery always
	// treats the journal's owner as dead and walks it back.
	QString deadHost()
	{
		return QSysInfo::machineHostName() + QStringLiteral("-DEAD");
	}

	QString jline(const QJsonObject &o)
	{
		return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
	}

	QJsonObject beginRec(const QString &kind, const QString &host, qint64 pid,
						 const QJsonObject &meta = QJsonObject())
	{
		return {{QStringLiteral("schema"), 1},
				{QStringLiteral("rec"), QStringLiteral("begin")},
				{QStringLiteral("kind"), kind},
				{QStringLiteral("started"), QStringLiteral("2026-01-01T00:00:00.000Z")},
				{QStringLiteral("app"), QStringLiteral("test")},
				{QStringLiteral("pid"), pid},
				{QStringLiteral("host"), host},
				{QStringLiteral("meta"), meta}};
	}

	QJsonObject opRec(int id, const QString &src, const QString &dst, qint64 bytes = 0,
					  const QString &parked = QString())
	{
		QJsonObject o{{QStringLiteral("rec"), QStringLiteral("op")},
					  {QStringLiteral("id"), id},
					  {QStringLiteral("src"), src},
					  {QStringLiteral("dst"), dst}};
		if (bytes > 0)
			o.insert(QStringLiteral("bytes"), bytes);
		if (!parked.isEmpty())
			o.insert(QStringLiteral("parked"), parked);
		return o;
	}

	QJsonObject doneRec(int id, const QString &final = QString())
	{
		QJsonObject o{{QStringLiteral("rec"), QStringLiteral("done")}, {QStringLiteral("id"), id}};
		if (!final.isEmpty())
			o.insert(QStringLiteral("final"), final);
		return o;
	}

	QJsonObject failRec(int id)
	{
		return {{QStringLiteral("rec"), QStringLiteral("fail")},
				{QStringLiteral("id"), id},
				{QStringLiteral("err"), QStringLiteral("boom")}};
	}

	/// A fail whose rollback also failed: the parked original never made it
	/// back, so recovery must treat this op as unfinished business even in a
	/// journal that carries an end line.
	QJsonObject failDirtyRec(int id)
	{
		return {{QStringLiteral("rec"), QStringLiteral("fail")},
				{QStringLiteral("id"), id},
				{QStringLiteral("err"), QStringLiteral("restore failed")},
				{QStringLiteral("dirty"), true}};
	}

	QJsonObject endRec()
	{
		return {{QStringLiteral("rec"), QStringLiteral("end")},
				{QStringLiteral("ok"), 0},
				{QStringLiteral("fail"), 0},
				{QStringLiteral("skip"), 0},
				{QStringLiteral("ended"), QStringLiteral("2026-01-01T00:00:01.000Z")}};
	}

	QJsonObject recoveredRec()
	{
		return {{QStringLiteral("rec"), QStringLiteral("recovered")},
				{QStringLiteral("reversed"), 0},
				{QStringLiteral("failed"), 0},
				{QStringLiteral("ts"), QStringLiteral("2026-01-01T00:00:02.000Z")}};
	}

	QString writeJournal(const QString &oplogDir, const QVector<QJsonObject> &recs)
	{
		QDir().mkpath(oplogDir);
		const QString path = oplogDir + QStringLiteral("/oplog-") +
							 QUuid::createUuid().toString(QUuid::WithoutBraces) +
							 QStringLiteral(".jsonl");
		QFile f(path);
		if (f.open(QIODevice::WriteOnly))
		{
			for (const QJsonObject &o : recs)
			{
				f.write(jline(o).toUtf8());
				f.write("\n", 1);
			}
			f.close();
		}
		return path;
	}

	void writeFile(const QString &path, const QByteArray &content)
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile f(path);
		if (f.open(QIODevice::WriteOnly))
		{
			f.write(content);
			f.close();
		}
	}

	QByteArray readFile(const QString &path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	}

	int journalCount(const QString &oplogDir)
	{
		return QDir(oplogDir).entryList({QStringLiteral("oplog-*.jsonl")}, QDir::Files).size();
	}
} // namespace

class TestOpRecovery : public QObject
{
	Q_OBJECT
private slots:
	void interrupted_move_is_rolled_back();
	void clean_journal_is_pruned();
	void recovered_journal_is_pruned();
	void failed_op_journal_makes_no_noise();
	void live_owner_is_left_alone();
	void delete_is_restored_from_trash();
	void delete_flagged_when_trash_emptied();
	void completed_but_src_back_is_not_clobbered();
	void inflight_move_with_full_dst_is_reversed();
	void inflight_move_with_partial_dst_is_flagged();
	void move_replace_restores_parked_original();
	void move_replace_flagged_when_park_restore_fails();
	void interrupted_move_replace_full_copy_flags_stranded_park();
	void rerun_is_idempotent();

	// Copy. Its source always survives, so reverseMoveLike's "src gone == the
	// forward op finished" rule says nothing here; `parked` still being on disk
	// is the signal instead. These pin the three states that rule produces.
	void interrupted_copy_replace_restores_parked_original();
	void copy_committed_before_done_line_keeps_the_finished_copy();
	void interrupted_copy_into_empty_slot_leaves_the_partial_alone();

	// Undo: user-initiated reversal of a *completed* op (not a crash). Owner
	// is deliberately this live process — undo, unlike run(), doesn't skip a
	// journal just because its owner is still alive.
	void undo_completed_move_puts_file_back();
	void undo_completed_delete_restores_from_trash();
	void undo_twice_is_a_noop();

	// Dirty fails (finding 3): a fail record carrying dirty:true means the
	// forward op failed AND its rollback failed — the replaced original is
	// still sitting at the parked path. Unlike a plain fail ("never touched
	// disk"), recovery must retry these even though the run finished, without
	// rolling back the run's completed ops.
	void dirty_fail_in_finished_copy_run_restores_parked_original();
	void finished_dirty_run_leaves_completed_ops_alone();
	void plain_fail_in_finished_run_is_pruned_untouched();
	void dirty_fail_flags_when_destination_occupied();

	// Rebalance pre-flight probes (finding 10, belt and braces): a probe is
	// journalled like any move, so a crash between its two renames is an
	// unfinished op recovery renames home; the root sweep additionally
	// catches probes no journal ever heard about (older builds, degraded
	// journals), located via the begin line's mxfRoot.
	void interrupted_rebalance_probe_op_is_renamed_home();
	void interrupted_rebalance_root_sweep_catches_unjournalled_probe();
	void probe_suffix_roundtrips_through_the_sweep();
};

void TestOpRecovery::interrupted_move_is_rolled_back()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	// The forward move completed: file sits at dst, src is gone.
	writeFile(dst, "DATA");

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 4), doneRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(QFile::exists(src));
	QCOMPARE(readFile(src), QByteArray("DATA"));
	QVERIFY(!QFile::exists(dst));
	QCOMPARE(s.journalsRecovered, 1);
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
	QVERIFY(s.anything());

	// Still on disk; deleted on next launch.
	const auto recs = OpJournal::scan(oplog);
	QCOMPARE(recs.size(), 1);
	QVERIFY(recs.first().recovered);
}

void TestOpRecovery::clean_journal_is_pruned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");

	writeJournal(
		oplog, {beginRec("move", deadHost(), 999999), opRec(0, "/x", "/y"), doneRec(0), endRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(journalCount(oplog), 0); // finished, so deleted
	QVERIFY(!s.anything());
}

void TestOpRecovery::recovered_journal_is_pruned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, "/x", "/y"), doneRec(0),
						 recoveredRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(journalCount(oplog), 0); // already rolled back, so deleted
	QVERIFY(!s.anything());
}

void TestOpRecovery::failed_op_journal_makes_no_noise()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/clip.mxf");

	// Forward op failed, so src was never touched.
	writeFile(src, "INTACT");

	writeJournal(oplog,
				 {beginRec("move", deadHost(), 999999), opRec(0, src, "/dead/end"), failRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(src), QByteArray("INTACT"));
	QVERIFY(!s.anything()); // nothing to undo, nothing to report
}

void TestOpRecovery::live_owner_is_left_alone()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	writeFile(dst, "DATA");

	writeJournal(oplog,
				 {beginRec("move", QSysInfo::machineHostName(), QCoreApplication::applicationPid()),
				  opRec(0, src, dst, 4), doneRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(!QFile::exists(src));
	QVERIFY(QFile::exists(dst));
	QCOMPARE(journalCount(oplog), 1);
	QVERIFY(!s.anything());
	const auto recs = OpJournal::scan(oplog);
	QVERIFY(!recs.first().recovered);
}

void TestOpRecovery::delete_is_restored_from_trash()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/clip.mxf");
	const QString trash = tmp.path() + QStringLiteral("/trash/clip.mxf");
	writeFile(trash, "DELME");

	writeJournal(oplog, {beginRec("delete", deadHost(), 999999), opRec(0, src, QString()),
						 doneRec(0, trash)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(QFile::exists(src));
	QCOMPARE(readFile(src), QByteArray("DELME"));
	QVERIFY(!QFile::exists(trash));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::delete_flagged_when_trash_emptied()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/clip.mxf");
	const QString trash = tmp.path() + QStringLiteral("/trash/gone.mxf"); // never created

	writeJournal(oplog, {beginRec("delete", deadHost(), 999999), opRec(0, src, QString()),
						 doneRec(0, trash)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(!QFile::exists(src));
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 1);
	QVERIFY(s.anything());
	QVERIFY(s.hadTrouble());
}

void TestOpRecovery::completed_but_src_back_is_not_clobbered()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	// The move completed, but src is back (user recreated it). Recovery
	// must not overwrite the live src.
	writeFile(src, "ORIGINAL");
	writeFile(dst, "MOVED");

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 5), doneRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(src), QByteArray("ORIGINAL")); // untouched
	QVERIFY(!s.anything());							 // nothing safely undoable
}

void TestOpRecovery::inflight_move_with_full_dst_is_reversed()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	// Crash between the copy finishing and the done line: src gone, dst is
	// a full-size copy. No 'done' record in the journal.
	writeFile(dst, "FULLDATA"); // 8 bytes

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 8)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(QFile::exists(src));
	QCOMPARE(readFile(src), QByteArray("FULLDATA"));
	QVERIFY(!QFile::exists(dst));
	QCOMPARE(s.opsReversed, 1);
}

void TestOpRecovery::inflight_move_with_partial_dst_is_flagged()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	// Crash mid-copy: src gone, dst is a short partial.
	// We expected 8 bytes but only 3 landed.
	writeFile(dst, "PAR"); // 3 bytes

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 8)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(!QFile::exists(src)); // nothing trustworthy to restore
	QVERIFY(QFile::exists(dst));  // partial left for the user to judge
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 1);
}

void TestOpRecovery::move_replace_restores_parked_original()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	const QString parked = dst + QStringLiteral(".__movereplace_abcd1234");

	// Replace-move completed: the moved-in file is at dst, the replaced
	// original is parked, src is gone.
	writeFile(dst, "NEWCONTENT");
	writeFile(parked, "OLDCONTENT");

	writeJournal(
		oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 10, parked), doneRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(src), QByteArray("NEWCONTENT")); // moved file back to src
	QCOMPARE(readFile(dst), QByteArray("OLDCONTENT")); // replaced original restored
	QVERIFY(!QFile::exists(parked));
	QCOMPARE(s.opsReversed, 1);
}

void TestOpRecovery::move_replace_flagged_when_park_restore_fails()
{
#ifdef Q_OS_WIN
	QSKIP("Directory write-protection doesn't block renames the same way on Windows.");
#endif
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	// Parked in its own directory (a hand-written journal can put it
	// anywhere) so write-protecting that directory fails ONLY the
	// parked→dst rename, never the dst→src eviction before it.
	const QString parkDir = tmp.path() + QStringLiteral("/park");
	const QString parked = parkDir + QStringLiteral("/clip.mxf.__movereplace_abcd1234");

	writeFile(dst, "NEWCONTENT");
	writeFile(parked, "OLDCONTENT");
	QVERIFY(QFile::setPermissions(parkDir, QFile::ReadOwner | QFile::ExeOwner));

	writeJournal(
		oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 10, parked), doneRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	// Restore permissions before asserting so QTemporaryDir can clean up
	// whatever the outcome.
	QVERIFY(QFile::setPermissions(parkDir,
								  QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

	// The moved file went back home, but the parked original could not —
	// this used to count as Reversed with no note, and the next launch
	// deleted the journal, the only record of the park path.
	QCOMPARE(readFile(src), QByteArray("NEWCONTENT"));
	QVERIFY(QFile::exists(parked));
	QCOMPARE(s.opsFlagged, 1);
	QCOMPARE(s.opsReversed, 0);
	QVERIFY2(!s.notes.isEmpty(), "a stranded original must produce a note");
	QVERIFY2(s.notes.join(QLatin1Char('\n'))
				 .contains(QStringLiteral("clip.mxf.__movereplace_abcd1234")),
			 "the note must name the park path — it is the user's only pointer to the file");
}

void TestOpRecovery::interrupted_move_replace_full_copy_flags_stranded_park()
{
	// Cross-volume Move-Replace, power lost during the verify pass: src
	// still present, dst full-size (the partial-dst guard must not remove
	// it), the replaced original parked. Recovery can restore nothing —
	// but it must TELL, because the journal holding the park path is
	// deleted after this walk. This used to return silent NothingToDo.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	const QString parked = dst + QStringLiteral(".__movereplace_abcd1234");

	writeFile(src, "NEWCONTENT");
	writeFile(dst, "NEWCONTENT"); // full size: matches op.bytes below
	writeFile(parked, "OLDCONTENT");

	// In-flight op line (no done, no fail): a clean crash, not a dirty fail.
	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 10, parked)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	// Nothing destroyed, nothing moved — and the user is told where the
	// original lives, full path included.
	QCOMPARE(readFile(src), QByteArray("NEWCONTENT"));
	QCOMPARE(readFile(dst), QByteArray("NEWCONTENT"));
	QCOMPARE(readFile(parked), QByteArray("OLDCONTENT"));
	QCOMPARE(s.opsFlagged, 1);
	QCOMPARE(s.opsReversed, 0);
	const QString allNotes = s.notes.join(QLatin1Char('\n'));
	QVERIFY(allNotes.contains(QStringLiteral("clip.mxf.__movereplace_abcd1234")));
	QVERIFY2(allNotes.contains(dst),
			 "the note must carry the full destination path — it is the only "
			 "record that survives the journal's deletion");
}

void TestOpRecovery::rerun_is_idempotent()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	writeFile(dst, "DATA");

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 4), doneRec(0)});

	const OpRecovery::Summary first = OpRecovery::run(oplog);
	QCOMPARE(first.opsReversed, 1);
	QCOMPARE(readFile(src), QByteArray("DATA"));

	// Second pass: the journal is now stamped recovered, so it's deleted and
	// the already-restored file is left exactly where it is.
	const OpRecovery::Summary second = OpRecovery::run(oplog);
	QVERIFY(!second.anything());
	QCOMPARE(journalCount(oplog), 0);
	QCOMPARE(readFile(src), QByteArray("DATA"));
	QVERIFY(!QFile::exists(dst));
}

// MARK: - Copy

void TestOpRecovery::interrupted_copy_replace_restores_parked_original()
{
	// The window that made Copy worth journalling: the destination has been
	// fully written but not yet verified, so the parked original is still on
	// disk and the op has no done line. Roll back to the original — a
	// full-size dst here is either mid-verify or a copy whose verify just
	// failed, and recovery can't tell those apart.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	const QString parked = dst + QStringLiteral(".__copyreplace_abcd1234");

	writeFile(src, "NEW");	  // copy leaves its source alone
	writeFile(dst, "NEW");	  // full-size, unverified
	writeFile(parked, "OLD"); // the user's original, still held

	writeJournal(oplog,
				 {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 3, parked)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(dst), QByteArray("OLD")); // original put back
	QVERIFY2(!QFile::exists(parked), "the parked temp must not be left orphaned");
	QVERIFY2(QFile::exists(src), "reversing a copy must never touch its source");
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::copy_committed_before_done_line_keeps_the_finished_copy()
{
	// Crash between commit (parked deleted) and the done line. The op reads as
	// incomplete, but dst is a finished, verified copy and there is nothing
	// left to restore — rolling back here would destroy the very thing the
	// user asked for. No parked file on disk means hands off.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	const QString parked = dst + QStringLiteral(".__copyreplace_abcd1234");

	writeFile(src, "NEW");
	writeFile(dst, "NEW"); // committed; parked already gone

	writeJournal(oplog,
				 {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 3, parked)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(dst), QByteArray("NEW")); // the good copy survives
	QVERIFY(QFile::exists(src));
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::interrupted_copy_into_empty_slot_leaves_the_partial_alone()
{
	// No parked original, because the destination slot was free. A partial
	// left here has no safety net to restore from, and op.bytes is the
	// scan-time size — too stale to tell a partial from a complete copy of a
	// source that changed since. So it stays: it's at the path and under the
	// name the user chose, which is more use to them than a guess.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	writeFile(src, "NEWDATA");
	writeFile(dst, "NEW"); // partial: 3 of the 7 bytes claimed below

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(QFile::exists(dst), "a partial with no parked original must be left for the user");
	QCOMPARE(readFile(dst), QByteArray("NEW"));
	QVERIFY(QFile::exists(src));
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::undo_completed_move_puts_file_back()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	// A completed same-volume move: the file sits at dst, src is gone. Owner is
	// this live process — run() would leave it alone, undo() reverses it.
	writeFile(dst, "DATA");
	const QString jpath = writeJournal(
		oplog, {beginRec("move", QSysInfo::machineHostName(), QCoreApplication::applicationPid()),
				opRec(0, src, dst, 4), doneRec(0), endRec()});

	const OpRecovery::Summary s = OpRecovery::undo(jpath);

	QVERIFY(QFile::exists(src));
	QCOMPARE(readFile(src), QByteArray("DATA"));
	QVERIFY(!QFile::exists(dst));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
	QVERIFY(s.anything());
}

void TestOpRecovery::undo_completed_delete_restores_from_trash()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/clip.mxf");
	const QString trash = tmp.path() + QStringLiteral("/trash/clip.mxf");
	writeFile(trash, "DELME");

	const QString jpath = writeJournal(
		oplog, {beginRec("delete", QSysInfo::machineHostName(), QCoreApplication::applicationPid()),
				opRec(0, src, QString()), doneRec(0, trash), endRec()});

	const OpRecovery::Summary s = OpRecovery::undo(jpath);

	QVERIFY(QFile::exists(src));
	QCOMPARE(readFile(src), QByteArray("DELME"));
	QVERIFY(!QFile::exists(trash));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::undo_twice_is_a_noop()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	writeFile(dst, "DATA");

	const QString jpath = writeJournal(
		oplog, {beginRec("move", QSysInfo::machineHostName(), QCoreApplication::applicationPid()),
				opRec(0, src, dst, 4), doneRec(0), endRec()});

	const OpRecovery::Summary first = OpRecovery::undo(jpath);
	QCOMPARE(first.opsReversed, 1);
	QCOMPARE(readFile(src), QByteArray("DATA"));

	// The file is already back where it started; a second undo finds src
	// present, treats every op as a no-op, and reports nothing.
	const OpRecovery::Summary second = OpRecovery::undo(jpath);
	QVERIFY(!second.anything());
	QCOMPARE(second.opsReversed, 0);
	QCOMPARE(readFile(src), QByteArray("DATA")); // untouched
	QVERIFY(!QFile::exists(dst));
}

void TestOpRecovery::dirty_fail_in_finished_copy_run_restores_parked_original()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	const QString parked = dst + QStringLiteral(".__copyreplace_test");

	// Copy-replace failed mid-run AND its restore failed: the source is
	// untouched, a partial write squats at dst, and the replaced original is
	// stranded at the park path. The run then concluded (end line).
	writeFile(src, "SOURCE");
	writeFile(dst, "PARTIAL");
	writeFile(parked, "ORIGINAL");

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 8, parked),
						 failDirtyRec(0), endRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	// The original is back in its slot; the partial is gone; the source was
	// never in danger.
	QCOMPARE(readFile(dst), QByteArray("ORIGINAL"));
	QVERIFY(!QFile::exists(parked));
	QCOMPARE(readFile(src), QByteArray("SOURCE"));
	QCOMPARE(s.journalsRecovered, 1);
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);

	// Spent, not deleted: marked recovered now, swept on the next launch.
	const auto recs = OpJournal::scan(oplog);
	QCOMPARE(recs.size(), 1);
	QVERIFY(recs.first().recovered);
}

void TestOpRecovery::finished_dirty_run_leaves_completed_ops_alone()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");

	// Op 0: a move that completed — its file must STAY moved; this run
	// finished and only the dirty op is unfinished business.
	const QString srcA = tmp.path() + QStringLiteral("/media/A/one.mxf");
	const QString dstA = tmp.path() + QStringLiteral("/dest/one.mxf");
	writeFile(dstA, "MOVED");

	// Op 1: a move-replace whose rollback failed; original still parked.
	const QString srcB = tmp.path() + QStringLiteral("/media/A/two.mxf");
	const QString dstB = tmp.path() + QStringLiteral("/dest/two.mxf");
	const QString parkedB = dstB + QStringLiteral(".__movereplace_test");
	writeFile(srcB, "B-SRC");
	writeFile(parkedB, "B-ORIGINAL");

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, srcA, dstA, 5), doneRec(0),
						 opRec(1, srcB, dstB, 5, parkedB), failDirtyRec(1), endRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	// The completed move is untouched — a finished run is never rolled back.
	QCOMPARE(readFile(dstA), QByteArray("MOVED"));
	QVERIFY(!QFile::exists(srcA));

	// The dirty op's original is back in its destination slot; the failed
	// move's source is untouched.
	QCOMPARE(readFile(dstB), QByteArray("B-ORIGINAL"));
	QVERIFY(!QFile::exists(parkedB));
	QCOMPARE(readFile(srcB), QByteArray("B-SRC"));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::plain_fail_in_finished_run_is_pruned_untouched()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	const QString parked = dst + QStringLiteral(".__copyreplace_test");

	// A plain (non-dirty) fail means the rollback already put everything
	// back; a file at the parked path here belongs to someone else's story
	// and must not be touched.
	writeFile(src, "SOURCE");
	writeFile(dst, "RESTORED");
	writeFile(parked, "UNRELATED");

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 8, parked),
						 failRec(0), endRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(journalCount(oplog), 0); // finished and clean: deleted
	QCOMPARE(readFile(dst), QByteArray("RESTORED"));
	QCOMPARE(readFile(parked), QByteArray("UNRELATED"));
	QVERIFY(!s.anything());
}

void TestOpRecovery::dirty_fail_flags_when_destination_occupied()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	const QString parked = dst + QStringLiteral(".__movereplace_test");

	// Move-replace: the cross-volume copy landed in full (dst matches the
	// journalled size), the source delete failed, and the rollback couldn't
	// clear dst to put the original back. Recovery can't fix this either —
	// dst is occupied by a full-size copy it must not destroy — so the only
	// honest move is a flag that names where the original is parked.
	writeFile(src, "SRC");
	writeFile(dst, "GOODCOPY"); // 8 bytes, matches op bytes below
	writeFile(parked, "ORIGINAL");

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 8, parked),
						 failDirtyRec(0), endRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	// Nothing destroyed: all three files still exist.
	QCOMPARE(readFile(src), QByteArray("SRC"));
	QCOMPARE(readFile(dst), QByteArray("GOODCOPY"));
	QCOMPARE(readFile(parked), QByteArray("ORIGINAL"));
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 1);
	QVERIFY(s.hadTrouble());
}

void TestOpRecovery::interrupted_rebalance_probe_op_is_renamed_home()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString root = tmp.path() + QStringLiteral("/MXF");
	const QString original = root + QStringLiteral("/1/a.mxf");
	const QString probe =
		original + QStringLiteral(".__rebalprobe_00000000-0000-0000-0000-000000000000");

	// Crash between the probe's two renames: the clip sits under the probe
	// name, and the journal holds the probe op with no outcome line.
	writeFile(probe, "CLIPDATA");

	writeJournal(oplog, {beginRec("rebalance", deadHost(), 999999,
								  QJsonObject{{QStringLiteral("mxfRoot"), root}}),
						 opRec(0, original, probe)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(original), QByteArray("CLIPDATA"));
	QVERIFY(!QFile::exists(probe));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::interrupted_rebalance_root_sweep_catches_unjournalled_probe()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString root = tmp.path() + QStringLiteral("/MXF");
	const QString original = root + QStringLiteral("/2/b.mxf");
	const QString probe =
		original + QStringLiteral(".__rebalprobe_11111111-1111-1111-1111-111111111111");

	// The stranded probe has NO op line (older build, or the journal
	// degraded before it landed) — only the begin line's mxfRoot can find
	// it. The one journalled op is a plain fail the walk ignores.
	writeFile(probe, "BDATA");

	writeJournal(oplog, {beginRec("rebalance", deadHost(), 999999,
								  QJsonObject{{QStringLiteral("mxfRoot"), root}}),
						 opRec(0, QStringLiteral("/x"), QStringLiteral("/y")), failRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(original), QByteArray("BDATA"));
	QVERIFY(!QFile::exists(probe));
	QCOMPARE(s.opsReversed, 1); // via the sweep, not the op walk
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::probe_suffix_roundtrips_through_the_sweep()
{
	// Writer/reader protocol check: a probe named by makeProbeSuffix()
	// must be exactly what recoverStranded recognises. The raw-literal
	// probe names in the two tests above stay deliberately hardcoded —
	// they pin on-disk compatibility with probes stranded by older builds.
	QVERIFY(ProbeSweep::makeProbeSuffix().startsWith(QStringLiteral(".__rebalprobe_")));

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString original = tmp.path() + QStringLiteral("/1/clip.mxf");
	const QString probe = original + ProbeSweep::makeProbeSuffix();
	writeFile(probe, "MEDIA");

	const ProbeSweep::Result swept = ProbeSweep::recoverStranded(tmp.path());
	QCOMPARE(swept.restored.size(), 1);
	QCOMPARE(swept.stuck.size(), 0);
	QCOMPARE(readFile(original), QByteArray("MEDIA"));
}

QTEST_APPLESS_MAIN(TestOpRecovery)
#include "tst_oprecovery.moc"