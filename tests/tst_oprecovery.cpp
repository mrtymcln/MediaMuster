#include "opjournal.h"
#include "oprecovery.h"
#include "probesweep.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
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

	/// A plan line: the whole to-do list, as OpJournal::writePlan writes it.
	/// `files` is a list of {src, name, folder, bytes, policy}; empty
	/// policy/folder are omitted like the writer does.
	struct PlanFile
	{
		QString src;
		QString name;
		QString folder;
		qint64 bytes = 0;
		QString policy;
	};
	QJsonObject planRec(const QString &dest, bool preserve, const QVector<PlanFile> &files)
	{
		QJsonArray arr;
		for (const PlanFile &f : files)
		{
			QJsonObject o{{QStringLiteral("src"), f.src}, {QStringLiteral("name"), f.name}};
			if (!f.folder.isEmpty())
				o.insert(QStringLiteral("folder"), f.folder);
			if (f.bytes > 0)
				o.insert(QStringLiteral("bytes"), f.bytes);
			if (!f.policy.isEmpty())
				o.insert(QStringLiteral("policy"), f.policy);
			arr.append(o);
		}
		return {{QStringLiteral("rec"), QStringLiteral("plan")},
				{QStringLiteral("dest"), dest},
				{QStringLiteral("preserve"), preserve},
				{QStringLiteral("files"), arr}};
	}

	QJsonObject skipRec(int id)
	{
		return {{QStringLiteral("rec"), QStringLiteral("skip")}, {QStringLiteral("id"), id}};
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
	// No plan line (the write failed before it, or an older writer):
	// nothing can be offered, so the sweep keeps its wholesale rollback.
	void interrupted_move_without_plan_is_rolled_back_wholesale();
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

	// Copy into an EMPTY slot (nothing parked). Copy never touches its
	// source, so removing what landed at dst can never lose data — the only
	// question is whether dst is a whole file or a fragment, and the
	// journalled size plus the live source answer it. A provably-short dst
	// goes (a truncated file under the real name is a trap: Avid reads it
	// as media, a Skip-policy re-run reads it as "already exists"); a
	// full-size dst is a finished copy that died before its done line and
	// stays.
	void interrupted_copy_into_empty_slot_removes_the_partial();
	void interrupted_copy_full_size_into_empty_slot_is_kept();
	void interrupted_copy_partial_kept_when_it_matches_a_shrunk_source();
	void interrupted_copy_partial_removed_even_when_source_is_gone();
	void interrupted_copy_partial_kept_when_nothing_can_size_it();
	void interrupted_copy_partial_removed_using_live_source_when_journal_has_no_size();
	void interrupted_copy_partial_flagged_when_it_cannot_be_removed();
	// The guard that matters most: a Copy-REPLACE op whose park path is
	// recorded but gone has already concluded (committed or restored) —
	// dst may be the user's original again, of any size — and must never
	// be judged by the empty-slot size rule.
	void copy_replace_with_park_gone_never_touches_dst();

	// Resume: an interrupted run that wrote a plan is listed with what is
	// left to do, kept on disk across launches until the user resumes or
	// discards it, and never rolled back twice.
	void interrupted_run_with_plan_is_listed_resumable();
	void resumable_journal_survives_a_second_launch_without_rerolling();
	void finished_run_with_plan_is_not_resumable_and_is_pruned();
	void cancelled_run_with_plan_is_not_resumable();
	void interrupted_run_without_plan_is_recovered_and_pruned_as_before();
	void pending_agrees_with_run();
	// Finished work stays; only the in-flight file is tidied. What counts
	// as finished is the op's done/skip line OR the disk's own evidence
	// when the crash beat that line (a whole file in an empty Copy slot, a
	// Move whose source is gone and destination whole, a Delete whose
	// source is gone) — so a resume never redoes a file that landed.
	void copy_whole_landed_file_counts_as_finished_for_resume();
	void interrupted_move_with_plan_keeps_finished_work();
	void interrupted_delete_with_plan_leaves_deleted_files_deleted();
	void finished_move_replace_with_orphan_park_is_named_not_undone();
	void pending_ignores_journals_not_yet_rolled_back_or_still_owned();
	void unknown_kind_or_schema_is_never_offered_for_resume();
	// The evidence rules, attacked: a source that GREW after the scan must
	// not let a fragment pass as whole; a source volume that isn't mounted
	// must read as "don't know", never as "we removed it"; a journalled
	// failure outranks any disk evidence; an unreadable journal falls back
	// to the wholesale rollback it can still do.
	void partial_copy_of_a_grown_source_is_not_mistaken_for_whole();
	void unreachable_source_volume_is_never_read_as_finished();
	void failed_op_is_never_counted_finished();
	void unknown_kind_journal_is_rolled_back_wholesale();
	// The in-flight Move partial is removed AND reported, like Copy's.
	void interrupted_move_partial_is_removed_and_reported();

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

void TestOpRecovery::interrupted_move_without_plan_is_rolled_back_wholesale()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	// The forward move completed: file sits at dst, src is gone. With no
	// plan line there is nothing to resume, so putting it back is the only
	// help left — the pre-plan contract, kept deliberately.
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

void TestOpRecovery::interrupted_copy_into_empty_slot_removes_the_partial()
{
	// No parked original (the slot was free); the copy died with 3 of the 7
	// journalled bytes written. dst is provably short, so it goes — and the
	// user is told, with the source named so they can copy it again.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	writeFile(src, "NEWDATA");
	writeFile(dst, "NEW"); // partial: 3 of the 7 bytes claimed below

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(!QFile::exists(dst), "a provably-short partial must be removed");
	QVERIFY2(QFile::exists(src), "reversing a copy must never touch its source");
	QCOMPARE(readFile(src), QByteArray("NEWDATA"));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
	const QString allNotes = s.notes.join(QLatin1Char('\n'));
	QVERIFY2(allNotes.contains(dst), qPrintable(allNotes));
	QVERIFY2(allNotes.contains(src), qPrintable(allNotes));
}

void TestOpRecovery::interrupted_copy_full_size_into_empty_slot_is_kept()
{
	// dst reached the journalled size: the copy finished and the app died
	// before the done line. Nothing to roll back, nothing to say.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	writeFile(src, "NEWDATA");
	writeFile(dst, "NEWDATA");

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(dst), QByteArray("NEWDATA"));
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 0);
	QVERIFY(s.notes.isEmpty());
}

void TestOpRecovery::interrupted_copy_partial_kept_when_it_matches_a_shrunk_source()
{
	// The journal says 7 bytes (scan time) but the source has since shrunk to
	// 3 and dst is 3: that is a whole copy of the file as it is now, not a
	// fragment. The live source outranks the stale journalled size.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	writeFile(src, "NEW");
	writeFile(dst, "NEW");

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(QFile::exists(dst), "a dst that matches the live source is whole");
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::interrupted_copy_partial_removed_even_when_source_is_gone()
{
	// Source unmounted since (a share, say). dst is short of the journalled
	// size and there is no live source to say otherwise: still provably a
	// fragment of a source that lives elsewhere, so it goes.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/gone/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	writeFile(dst, "NEW"); // 3 of 7

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(!QFile::exists(dst));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::interrupted_copy_partial_kept_when_nothing_can_size_it()
{
	// No journalled size (older writer) AND no source to compare against:
	// unknowable, so it stays — the pre-existing behaviour, now the narrow
	// exception rather than the rule.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/gone/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");

	writeFile(dst, "NEW");

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(QFile::exists(dst), "with nothing to size it against, dst is left alone");
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::interrupted_copy_partial_removed_using_live_source_when_journal_has_no_size()
{
	// Older writer (no bytes) but the source is there: 3 < 7 -> partial.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	writeFile(src, "NEWDATA");
	writeFile(dst, "NEW");

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QVERIFY(!QFile::exists(dst));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
}

void TestOpRecovery::interrupted_copy_partial_flagged_when_it_cannot_be_removed()
{
#ifdef Q_OS_WIN
	QSKIP("Directory write-protection doesn't block deletes the same way on Windows.");
#endif
	// The partial sits in a folder we can't write: it must be flagged (with
	// the path named) and NOT counted as cleaned up.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dstDir = tmp.path() + QStringLiteral("/media/B");
	const QString dst = dstDir + QStringLiteral("/clip.mxf");
	writeFile(src, "NEWDATA");
	writeFile(dst, "NEW");
	writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7)});

	QVERIFY(QFile::setPermissions(dstDir, QFile::ReadOwner | QFile::ExeOwner));
	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QVERIFY(QFile::setPermissions(dstDir,
								  QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

	QVERIFY2(QFile::exists(dst), "the partial could not be removed, so it must still be there");
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 1);
	QVERIFY2(s.notes.join(QLatin1Char('\n')).contains(dst), qPrintable(s.notes.join("|")));
}

void TestOpRecovery::copy_replace_with_park_gone_never_touches_dst()
{
	// Copy-Replace: the destination held an original (parked path
	// recorded). The copy failed and ParkedFile restored the original to
	// dst; the app died before the fail line landed. dst is now the user's
	// ORIGINAL (3 bytes) — smaller than the journalled 7 and different from
	// the 7-byte source. The empty-slot size rule must NOT run here: the
	// park path being recorded says the slot was never empty.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/media/B/clip.mxf");
	const QString parked = dst + QStringLiteral(".__copyreplace_abcd1234"); // gone
	writeFile(src, "NEWDATA");
	writeFile(dst, "OLD"); // the restored original

	writeJournal(oplog,
				 {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7, parked)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(QFile::exists(dst), "a restored original must never be deleted");
	QCOMPARE(readFile(dst), QByteArray("OLD"));
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 0);
	QVERIFY(s.notes.isEmpty());

	// Same op as a dirty fail (rollback stalled once, destructor retry
	// then succeeded — parked gone, dst = original): still hands off.
	QTemporaryDir tmp2;
	QVERIFY(tmp2.isValid());
	const QString oplog2 = tmp2.path() + QStringLiteral("/oplog");
	const QString src2 = tmp2.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst2 = tmp2.path() + QStringLiteral("/media/B/clip.mxf");
	writeFile(src2, "NEWDATA");
	writeFile(dst2, "OLD");
	writeJournal(oplog2, {beginRec("copy", deadHost(), 999999),
						  opRec(0, src2, dst2, 7, dst2 + ".__copyreplace_x"), failDirtyRec(0),
						  endRec()});
	const OpRecovery::Summary s2 = OpRecovery::run(oplog2);
	QCOMPARE(readFile(dst2), QByteArray("OLD"));
	QCOMPARE(s2.opsReversed, 0);
}

// MARK: - Resume

void TestOpRecovery::interrupted_run_with_plan_is_listed_resumable()
{
	// Plan of three; op 0 done, op 1 skipped, op 2 died mid-copy (partial
	// removed by rollback), file 3 never started. Remaining = the partial
	// (retry) + the never-started one; finished = 2. The journal stays on
	// disk so a later launch (or the menu) can still offer it.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString destRoot = tmp.path() + QStringLiteral("/dest");
	QVector<QString> src, dst;
	for (int i = 0; i < 4; ++i)
	{
		src << tmp.path() + QStringLiteral("/media/A/clip%1.mxf").arg(i);
		dst << destRoot + QStringLiteral("/clip%1.mxf").arg(i);
		writeFile(src[i], "NEWDATA");
	}
	writeFile(dst[0], "NEWDATA"); // done
	writeFile(dst[2], "NEW");	  // partial, 3 of 7

	const QVector<PlanFile> plan{{src[0], "clip0.mxf", "1", 7, ""},
								 {src[1], "clip1.mxf", "1", 7, "skip"},
								 {src[2], "clip2.mxf", "1", 7, "replace"},
								 {src[3], "clip3.mxf", "1", 7, ""}};
	const QString path =
		writeJournal(oplog, {beginRec("copy", deadHost(), 999999,
									  {{QStringLiteral("dest"), destRoot}}),
							 planRec(destRoot, false, plan), opRec(0, src[0], dst[0], 7),
							 doneRec(0), opRec(1, src[1], dst[1], 7), skipRec(1),
							 opRec(2, src[2], dst[2], 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(!QFile::exists(dst[2]), "the partial is cleaned up before the offer");
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.resumable.size(), 1);
	const OpRecovery::Resumable &r = s.resumable.first();
	QCOMPARE(r.journalPath, path);
	QCOMPARE(r.kind, OpJournal::Kind::Copy);
	QCOMPARE(r.dest, destRoot);
	QCOMPARE(r.preserve, false);
	QCOMPARE(r.total, 4);
	QCOMPARE(r.finished, 2);
	QCOMPARE(r.remaining.size(), 2);
	QCOMPARE(r.remaining[0].src, src[2]);
	QCOMPARE(r.remaining[0].policy, QStringLiteral("replace"));
	QCOMPARE(r.remaining[1].src, src[3]);
	QVERIFY2(QFile::exists(path), "a resumable journal must survive recovery");
	QVERIFY(s.anything());
}

void TestOpRecovery::resumable_journal_survives_a_second_launch_without_rerolling()
{
	// Launch 1 rolls back and stamps recovered; the user says "not now".
	// Launch 2 must offer it again, must not roll anything back a second
	// time, and must not delete it.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	const QString src2 = tmp.path() + QStringLiteral("/media/A/clip2.mxf");
	writeFile(src, "NEWDATA");
	writeFile(src2, "NEWDATA");
	writeFile(dst, "NEW"); // partial

	const QString path =
		writeJournal(oplog, {beginRec("copy", deadHost(), 999999),
							 planRec(tmp.path() + "/dest", false,
									 {{src, "clip.mxf", "", 7, ""}, {src2, "clip2.mxf", "", 7, ""}}),
							 opRec(0, src, dst, 7)});

	const OpRecovery::Summary first = OpRecovery::run(oplog);
	QCOMPARE(first.opsReversed, 1);
	QCOMPARE(first.resumable.size(), 1);
	QVERIFY(QFile::exists(path));

	// Simulate: the user re-copies clip.mxf by hand in between (dst is now
	// whole). Second launch must not touch it and must not roll anything
	// back — and, because a whole file at the op's destination counts as
	// finished, only clip2 is still offered.
	writeFile(dst, "NEWDATA");
	const OpRecovery::Summary second = OpRecovery::run(oplog);
	QCOMPARE(second.opsReversed, 0);
	QCOMPARE(second.opsFlagged, 0);
	QCOMPARE(second.journalsRecovered, 0);
	QCOMPARE(second.resumable.size(), 1);
	QCOMPARE(second.resumable.first().finished, 1);
	QCOMPARE(second.resumable.first().remaining.size(), 1);
	QCOMPARE(second.resumable.first().remaining.first().src, src2);
	QCOMPARE(readFile(dst), QByteArray("NEWDATA"));
	QVERIFY(QFile::exists(path));
}

void TestOpRecovery::finished_run_with_plan_is_not_resumable_and_is_pruned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	writeFile(src, "NEWDATA");
	writeFile(dst, "NEWDATA");

	// Two planned, one done, one failed — but the run concluded (end line):
	// the user watched it end. Not an interruption, nothing to resume.
	const QString path = writeJournal(
		oplog, {beginRec("copy", deadHost(), 999999),
				planRec(tmp.path() + "/dest", false,
						{{src, "clip.mxf", "", 7, ""}, {src + "2", "clip2.mxf", "", 7, ""}}),
				opRec(0, src, dst, 7), doneRec(0), opRec(1, src + "2", dst + "2", 7), failRec(1),
				endRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QVERIFY(s.resumable.isEmpty());
	QVERIFY(!QFile::exists(path));
	QVERIFY(OpRecovery::pending(oplog).isEmpty());
}

void TestOpRecovery::cancelled_run_with_plan_is_not_resumable()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	writeFile(src, "NEWDATA");

	QJsonObject cancelled = endRec();
	cancelled.insert(QStringLiteral("cancelled"), true);
	const QString path =
		writeJournal(oplog, {beginRec("copy", deadHost(), 999999),
							 planRec(tmp.path() + "/dest", false, {{src, "clip.mxf", "", 7, ""}}),
							 cancelled});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QVERIFY(s.resumable.isEmpty());
	QVERIFY(!QFile::exists(path));
}

void TestOpRecovery::interrupted_run_without_plan_is_recovered_and_pruned_as_before()
{
	// A journal from a build before the plan line: rolled back, stamped,
	// and (on the next launch) deleted — exactly the old contract. Never
	// offered for resume, because it can't say what it meant to do.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	writeFile(src, "NEWDATA");
	writeFile(dst, "NEW");

	const QString path =
		writeJournal(oplog, {beginRec("copy", deadHost(), 999999), opRec(0, src, dst, 7)});

	const OpRecovery::Summary first = OpRecovery::run(oplog);
	QCOMPARE(first.opsReversed, 1);
	QVERIFY(first.resumable.isEmpty());
	QVERIFY2(QFile::exists(path), "stamped recovered, deleted on the next sweep");

	const OpRecovery::Summary second = OpRecovery::run(oplog);
	QVERIFY(second.resumable.isEmpty());
	QVERIFY(!QFile::exists(path));
}

void TestOpRecovery::pending_agrees_with_run()
{
	// pending() is the read-only twin of run()'s classifier: same journals,
	// same remaining lists, and no side effects (the partial is NOT removed
	// by pending; only run() rolls back).
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	writeFile(src, "NEWDATA");
	writeFile(dst, "NEW");

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999),
						 planRec(tmp.path() + "/dest", true, {{src, "clip.mxf", "1", 7, "skip"}}),
						 opRec(0, src, dst, 7)});

	// Before the launch sweep the journal is unstamped: pending() must not
	// offer it (its rollback hasn't run) and must not touch disk.
	QVERIFY(OpRecovery::pending(oplog).isEmpty());
	QVERIFY2(QFile::exists(dst), "pending() must not touch disk");

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QCOMPARE(s.resumable.size(), 1);
	QVERIFY(!QFile::exists(dst)); // the sweep removed the partial

	const QVector<OpRecovery::Resumable> after = OpRecovery::pending(oplog);
	QCOMPARE(after.size(), 1);
	QCOMPARE(after.first().journalPath, s.resumable.first().journalPath);
	QCOMPARE(after.first().preserve, true);
	QCOMPARE(after.first().remaining.size(), s.resumable.first().remaining.size());
	QCOMPARE(after.first().remaining.first().folder, QStringLiteral("1"));
	QCOMPARE(after.first().remaining.first().policy, QStringLiteral("skip"));
}

void TestOpRecovery::copy_whole_landed_file_counts_as_finished_for_resume()
{
	// KeepBoth redirected file 0 to clip0.Copy.01.mxf; it landed whole but
	// the app died before its done line. Rollback keeps it; the resume
	// classifier must count it finished, or a resume would land a .Copy.02.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString destRoot = tmp.path() + QStringLiteral("/dest");
	const QString src0 = tmp.path() + QStringLiteral("/media/A/clip0.mxf");
	const QString src1 = tmp.path() + QStringLiteral("/media/A/clip1.mxf");
	const QString dst0 = destRoot + QStringLiteral("/clip0.Copy.01.mxf");
	writeFile(src0, "NEWDATA");
	writeFile(src1, "NEWDATA");
	writeFile(destRoot + "/clip0.mxf", "OLD"); // the pre-existing file KeepBoth kept
	writeFile(dst0, "NEWDATA");					// whole, no done line

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999),
						 planRec(destRoot, false,
								 {{src0, "clip0.mxf", "", 7, "keepboth"}, {src1, "clip1.mxf", "", 7, ""}}),
						 opRec(0, src0, dst0, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QCOMPARE(readFile(dst0), QByteArray("NEWDATA")); // kept
	QCOMPARE(s.resumable.size(), 1);
	QCOMPARE(s.resumable.first().finished, 1);
	QCOMPARE(s.resumable.first().remaining.size(), 1);
	QCOMPARE(s.resumable.first().remaining.first().src, src1);
}

void TestOpRecovery::partial_copy_of_a_grown_source_is_not_mistaken_for_whole()
{
	// The scan recorded 3 bytes; by the time the copy ran the source was 13
	// (Avid was still writing into it). 5 bytes landed. Reading the stale
	// journalled size first would call that "whole" and leave a truncated
	// clip under the real name — the live source is the authority.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	writeFile(src, "NEWDATANEWDA"); // 12 bytes now
	writeFile(dst, "NEWDA");		// 5 bytes landed

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999),
						 planRec(tmp.path() + "/dest", false, {{src, "clip.mxf", "", 3, ""}}),
						 opRec(0, src, dst, 3)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(!QFile::exists(dst), "a fragment of a grown source is still a fragment");
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.resumable.size(), 1);
	QCOMPARE(s.resumable.first().finished, 0);
	QCOMPARE(s.resumable.first().remaining.size(), 1);
}

void TestOpRecovery::unreachable_source_volume_is_never_read_as_finished()
{
	// The classic post-crash reboot: MediaMuster launches from login items
	// before the media volume is mounted, so every source path is "missing"
	// for the wrong reason. Absence must not be read as "moved" or
	// "deleted", or the run would report itself finished and the journal
	// would be deleted with nothing left to resume. And recovery must not
	// put anything back into the mount point — creating that folder is what
	// makes the real volume mount as "EDIT 1" later.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mounts = tmp.path() + QStringLiteral("/Volumes");
	QVERIFY(QDir().mkpath(mounts));
	qputenv("MEDIAMUSTER_MOUNT_ROOT", mounts.toUtf8());

	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString destRoot = tmp.path() + QStringLiteral("/dest");
	const QString src = mounts + QStringLiteral("/EDIT/media/clip.mxf"); // EDIT not mounted
	const QString dst = destRoot + QStringLiteral("/clip.mxf");
	writeFile(dst, "NEWDATA");

	const QString movePath =
		writeJournal(oplog, {beginRec("move", deadHost(), 999999),
							 planRec(destRoot, false, {{src, "clip.mxf", "", 7, ""}}),
							 opRec(0, src, dst, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QCOMPARE(s.resumable.size(), 1);
	QCOMPARE(s.resumable.first().finished, 0);
	QCOMPARE(s.resumable.first().remaining.size(), 1);
	QVERIFY2(QFile::exists(movePath), "the journal must survive for when the volume comes back");
	QVERIFY2(QFile::exists(dst), "and nothing at the destination may be touched meanwhile");
	QVERIFY2(!QFileInfo::exists(mounts + QStringLiteral("/EDIT")),
			 "recovery must never create a folder where a volume should mount");

	// A Delete on the same unmounted volume: also "don't know", not "done",
	// and nothing is restored into the mount point.
	const QString oplog2 = tmp.path() + QStringLiteral("/oplog2");
	const QString gone = mounts + QStringLiteral("/EDIT/media/gone.mxf");
	// No done line: whether this file was ever trashed is exactly what the
	// missing volume stops us knowing. (A delete WITH a done line is
	// finished on the journal's word alone, volume or no volume.)
	writeJournal(oplog2, {beginRec("delete", deadHost(), 999999),
						  planRec(QString(), false, {{gone, "gone.mxf", "", 5, ""}}),
						  opRec(0, gone, QString(), 5)});
	const OpRecovery::Summary s2 = OpRecovery::run(oplog2);
	QCOMPARE(s2.resumable.size(), 1);
	QCOMPARE(s2.resumable.first().finished, 0);
	QVERIFY2(!QFileInfo::exists(mounts + QStringLiteral("/EDIT")), "no mount point conjured");

	qunsetenv("MEDIAMUSTER_MOUNT_ROOT");

	// With the volume mounted but the folder genuinely deleted, the old
	// behaviour stands: recovery recreates the folder and puts it back.
	QTemporaryDir tmp3;
	QVERIFY(tmp3.isValid());
	const QString oplog3 = tmp3.path() + QStringLiteral("/oplog");
	const QString src3 = tmp3.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst3 = tmp3.path() + QStringLiteral("/dest/clip.mxf");
	writeFile(dst3, "NEWDATA");
	writeJournal(oplog3, {beginRec("move", deadHost(), 999999), opRec(0, src3, dst3, 7)});
	OpRecovery::run(oplog3);
	QCOMPARE(readFile(src3), QByteArray("NEWDATA"));
}

void TestOpRecovery::failed_op_is_never_counted_finished()
{
	// The engine wrote a fail line: it refused to trust what is at dst
	// (a verify that failed, say, whose cleanup couldn't remove the file).
	// The journal's own verdict outranks the disk looking plausible.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString destRoot = tmp.path() + QStringLiteral("/dest");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = destRoot + QStringLiteral("/clip.mxf");
	writeFile(src, "NEWDATA");
	writeFile(dst, "NEWDATA"); // full size, but not trusted

	writeJournal(oplog, {beginRec("copy", deadHost(), 999999),
						 planRec(destRoot, false, {{src, "clip.mxf", "", 7, ""}}),
						 opRec(0, src, dst, 7), failRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QCOMPARE(s.resumable.size(), 1);
	QCOMPARE(s.resumable.first().finished, 0);
	QCOMPARE(s.resumable.first().remaining.size(), 1);
	QVERIFY2(QFile::exists(dst), "a plain fail means hands off, not delete");
}

void TestOpRecovery::unknown_kind_journal_is_rolled_back_wholesale()
{
	// A journal from a later build: this one can't be resumed (it would
	// have to guess what the kind means), so it must get the rollback it
	// can still do rather than being left half-done and then deleted.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = tmp.path() + QStringLiteral("/dest/clip.mxf");
	QVERIFY(QDir().mkpath(QFileInfo(src).absolutePath()));
	writeFile(dst, "NEWDATA"); // the completed op's file

	writeJournal(oplog, {beginRec("teleport", deadHost(), 999999),
						 planRec(tmp.path() + "/dest", false, {{src, "clip.mxf", "", 7, ""}}),
						 opRec(0, src, dst, 7), doneRec(0)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QVERIFY(s.resumable.isEmpty());
	QVERIFY2(QFile::exists(src), "an unreadable journal falls back to putting things back");
	QCOMPARE(s.opsReversed, 1);
}

void TestOpRecovery::interrupted_move_partial_is_removed_and_reported()
{
	// The Move twin of interrupted_copy_into_empty_slot_removes_the_partial:
	// removing a file from the user's disk is never silent.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString destRoot = tmp.path() + QStringLiteral("/dest");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = destRoot + QStringLiteral("/clip.mxf");
	writeFile(src, "NEWDATA");
	writeFile(dst, "NEW"); // in-flight cross-volume copy

	writeJournal(oplog, {beginRec("move", deadHost(), 999999),
						 planRec(destRoot, false, {{src, "clip.mxf", "", 7, ""}}),
						 opRec(0, src, dst, 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY(!QFile::exists(dst));
	QCOMPARE(readFile(src), QByteArray("NEWDATA"));
	QCOMPARE(s.opsReversed, 1);
	QCOMPARE(s.opsFlagged, 0);
	QCOMPARE(s.journalsRecovered, 1);
	const QString allNotes = s.notes.join(QLatin1Char('\n'));
	QVERIFY2(allNotes.contains(dst), qPrintable(allNotes));
	QVERIFY2(allNotes.contains(src), qPrintable(allNotes));
}

void TestOpRecovery::interrupted_move_with_plan_keeps_finished_work()
{
	// Move of 4 with a plan. op 0 finished (done line). op 1 finished but
	// the crash beat its done line (src gone, dst whole) — the disk says so.
	// op 2 was in flight (src still there, dst a short partial). file 3 was
	// never started. The sweep must remove ONLY op 2's partial, leave the
	// two finished files at the destination, and offer 2 and 3.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString destRoot = tmp.path() + QStringLiteral("/dest");
	QVector<QString> src, dst;
	for (int i = 0; i < 4; ++i)
	{
		src << tmp.path() + QStringLiteral("/media/A/clip%1.mxf").arg(i);
		dst << destRoot + QStringLiteral("/clip%1.mxf").arg(i);
	}
	QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/media/A"))); // volume mounted
	writeFile(dst[0], "NEWDATA"); // moved, done line landed
	writeFile(dst[1], "NEWDATA"); // moved, done line did NOT land
	writeFile(src[2], "NEWDATA");
	writeFile(dst[2], "NEW"); // in flight: partial cross-volume copy
	writeFile(src[3], "NEWDATA");

	const QVector<PlanFile> plan{{src[0], "clip0.mxf", "", 7, ""},
								 {src[1], "clip1.mxf", "", 7, ""},
								 {src[2], "clip2.mxf", "", 7, ""},
								 {src[3], "clip3.mxf", "", 7, ""}};
	writeJournal(oplog, {beginRec("move", deadHost(), 999999), planRec(destRoot, false, plan),
						 opRec(0, src[0], dst[0], 7), doneRec(0), opRec(1, src[1], dst[1], 7),
						 opRec(2, src[2], dst[2], 7)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(QFile::exists(dst[0]), "a finished move must be left where it landed");
	QVERIFY2(!QFile::exists(src[0]), "and its source must not be resurrected");
	QVERIFY2(QFile::exists(dst[1]), "a move whose done line was lost is still finished work");
	QVERIFY2(!QFile::exists(src[1]), "so its source stays gone too");
	QVERIFY2(!QFile::exists(dst[2]), "the in-flight partial is the one thing tidied up");
	QVERIFY2(QFile::exists(src[2]), "its source was never touched");
	QCOMPARE(s.opsReversed, 1); // and it is reported, never removed silently
	QCOMPARE(s.opsFlagged, 0);
	QVERIFY2(s.notes.join(QLatin1Char('\n')).contains(dst[2]), qPrintable(s.notes.join("|")));

	QCOMPARE(s.resumable.size(), 1);
	const OpRecovery::Resumable &r = s.resumable.first();
	QCOMPARE(r.kind, OpJournal::Kind::Move);
	QCOMPARE(r.total, 4);
	QCOMPARE(r.finished, 2);
	QCOMPARE(r.remaining.size(), 2);
	QCOMPARE(r.remaining[0].src, src[2]);
	QCOMPARE(r.remaining[1].src, src[3]);
}

void TestOpRecovery::interrupted_delete_with_plan_leaves_deleted_files_deleted()
{
	// Delete of 3 with a plan: one with a done line, one whose source is
	// gone but whose done line was lost, one never started. Deleting is
	// what the user asked for, so nothing comes back from the Trash; only
	// the untouched file is offered.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src0 = tmp.path() + QStringLiteral("/media/clip0.mxf");
	const QString src1 = tmp.path() + QStringLiteral("/media/clip1.mxf");
	const QString src2 = tmp.path() + QStringLiteral("/media/clip2.mxf");
	const QString trash0 = tmp.path() + QStringLiteral("/trash/clip0.mxf");
	QVERIFY(QDir().mkpath(tmp.path() + QStringLiteral("/media"))); // volume mounted
	writeFile(trash0, "DELME"); // deleted, done line landed
	writeFile(src2, "KEEP");	// never started

	writeJournal(oplog, {beginRec("delete", deadHost(), 999999),
						 planRec(QString(), false,
								 {{src0, "clip0.mxf", "", 5, ""},
								  {src1, "clip1.mxf", "", 5, ""},
								  {src2, "clip2.mxf", "", 4, ""}}),
						 opRec(0, src0, QString(), 5), doneRec(0, trash0),
						 opRec(1, src1, QString(), 5)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QVERIFY2(!QFile::exists(src0), "a finished delete must stay deleted");
	QVERIFY2(QFile::exists(trash0), "and its file must stay in the Trash");
	QVERIFY2(!QFile::exists(src1), "a delete whose done line was lost is still finished work");
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 0);

	QCOMPARE(s.resumable.size(), 1);
	const OpRecovery::Resumable &r = s.resumable.first();
	QCOMPARE(r.kind, OpJournal::Kind::Delete);
	QVERIFY(r.dest.isEmpty());
	QCOMPARE(r.finished, 2);
	QCOMPARE(r.remaining.size(), 1);
	QCOMPARE(r.remaining.first().src, src2);
}

void TestOpRecovery::finished_move_replace_with_orphan_park_is_named_not_undone()
{
	// The move landed (src gone, dst whole) but the crash beat the commit
	// that deletes the parked original. Deleting the user's previous file
	// on an inference is not recovery's call: the work stands and the
	// leftover is named so they can deal with it.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString destRoot = tmp.path() + QStringLiteral("/dest");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	const QString dst = destRoot + QStringLiteral("/clip.mxf");
	const QString parked = dst + QStringLiteral(".__movereplace_abcd1234");
	// The source's folder is still there (the volume is mounted) — only the
	// file has gone, which is what proves the move happened.
	QVERIFY(QDir().mkpath(QFileInfo(src).absolutePath()));
	writeFile(dst, "NEWDATA"); // the moved file
	writeFile(parked, "OLD");  // the original it replaced, never committed

	writeJournal(oplog, {beginRec("move", deadHost(), 999999),
						 planRec(destRoot, false, {{src, "clip.mxf", "", 7, ""}}),
						 opRec(0, src, dst, 7, parked)});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	QCOMPARE(readFile(dst), QByteArray("NEWDATA")); // the move stands
	QVERIFY2(QFile::exists(parked), "the user's previous file must not be deleted on a guess");
	QCOMPARE(s.opsReversed, 0);
	QCOMPARE(s.opsFlagged, 1);
	const QString allNotes = s.notes.join(QLatin1Char('\n'));
	QVERIFY2(allNotes.contains(QStringLiteral("clip.mxf.__movereplace_abcd1234")),
			 qPrintable(allNotes));
	// Finished work, so nothing is offered for resume.
	QVERIFY(s.resumable.isEmpty());
}

void TestOpRecovery::pending_ignores_journals_not_yet_rolled_back_or_still_owned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	writeFile(src, "NEWDATA");

	// A run in flight in THIS process (unstamped, live pid): never offered —
	// not by the read-only listing, not by the sweep (which leaves live
	// owners alone and so never stamps it).
	writeJournal(oplog, {beginRec("copy", QSysInfo::machineHostName(),
								  QCoreApplication::applicationPid()),
						 planRec(tmp.path() + "/dest", false, {{src, "clip.mxf", "", 7, ""}}),
						 opRec(0, src, tmp.path() + "/dest/clip.mxf", 7)});
	// Dead owner but not yet swept (no recovered line): not offered by the
	// read-only listing — run() must roll it back first.
	writeJournal(oplog, {beginRec("copy", deadHost(), 999999),
						 planRec(tmp.path() + "/dest", false, {{src, "clip.mxf", "", 7, ""}})});

	QVERIFY(OpRecovery::pending(oplog).isEmpty());

	// After the sweep the dead one is stamped and offered; the live one still is not.
	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QCOMPARE(s.resumable.size(), 1);
	QCOMPARE(OpRecovery::pending(oplog).size(), 1);
	QCOMPARE(OpRecovery::pending(oplog).first().journalPath, s.resumable.first().journalPath);
}

void TestOpRecovery::unknown_kind_or_schema_is_never_offered_for_resume()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oplog = tmp.path() + QStringLiteral("/oplog");
	const QString src = tmp.path() + QStringLiteral("/media/A/clip.mxf");
	writeFile(src, "NEWDATA");

	// A kind this build doesn't know (a future build's journal).
	writeJournal(oplog, {beginRec("teleport", deadHost(), 999999),
						 planRec(tmp.path() + "/dest", false, {{src, "clip.mxf", "", 7, ""}})});
	// A newer schema.
	QJsonObject begin2 = beginRec("copy", deadHost(), 999999);
	begin2.insert(QStringLiteral("schema"), 2);
	writeJournal(oplog, {begin2, planRec(tmp.path() + "/dest", false, {{src, "clip.mxf", "", 7, ""}})});

	const OpRecovery::Summary s = OpRecovery::run(oplog);
	QVERIFY(s.resumable.isEmpty());
	QVERIFY(OpRecovery::pending(oplog).isEmpty());
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
	// dst is a FULL copy of src, so it is the same size as its source —
	// that is what makes it a copy recovery must not destroy.
	writeFile(src, "GOODCOPY");
	writeFile(dst, "GOODCOPY");
	writeFile(parked, "ORIGINAL");

	writeJournal(oplog, {beginRec("move", deadHost(), 999999), opRec(0, src, dst, 8, parked),
						 failDirtyRec(0), endRec()});

	const OpRecovery::Summary s = OpRecovery::run(oplog);

	// Nothing destroyed: all three files still exist.
	QCOMPARE(readFile(src), QByteArray("GOODCOPY"));
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