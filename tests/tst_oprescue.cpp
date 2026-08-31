#include "oprescue.h"

#include "testutil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// OpRescue — the launch sweep, driven over HAND-WRITTEN schema-2
// journals and real files in temp dirs, the way tst_oprecovery drove the
// v1 engine. The v1 decision tables are re-pinned (finished work stays,
// src-back never clobbered, partials removed and narrated, dirty
// retries, resumable classification), and the v2 rules get their own
// sections: identity blocking wrong-file reversals, volume re-anchoring
// and the impostor refusal, undo-run recovery, and journal retention for
// the undo candidate.

namespace
{
	// Every hand-written journal belongs to a dead owner: pid 999999 on a
	// host that isn't this machine, so the live-owner guard never trips.
	QString beginRec(const QString &kind)
	{
		return QStringLiteral(
				   R"({"schema":2,"rec":"begin","kind":"%1","started":"2026-08-29T10:00:00.000Z","pid":999999,"host":"deadhost"})")
			.arg(kind);
	}

	QString beginUndoRec(const QString &effective, const QString &undoes)
	{
		return QStringLiteral(
				   R"({"schema":2,"rec":"begin","kind":"undo","started":"2026-08-29T10:00:00.000Z","pid":999999,"host":"deadhost","meta":{"undoes":"%1","effective":"%2"}})")
			.arg(undoes, effective);
	}

	QString planRec(const QString &dest, const QStringList &srcs)
	{
		QStringList files;
		for (const QString &s : srcs)
			files << QStringLiteral(R"({"src":"%1","name":"%2"})")
						 .arg(s, QFileInfo(s).fileName());
		return QStringLiteral(R"({"rec":"plan","dest":"%1","preserve":false,"files":[%2]})")
			.arg(dest, files.join(QLatin1Char(',')));
	}

	QString planRecWithVolumes(const QString &dest, const QStringList &srcs,
							   const QString &volumesJson)
	{
		QStringList files;
		for (const QString &s : srcs)
			files << QStringLiteral(R"({"src":"%1","name":"%2"})")
						 .arg(s, QFileInfo(s).fileName());
		return QStringLiteral(
				   R"({"rec":"plan","dest":"%1","preserve":false,"files":[%2],"volumes":[%3]})")
			.arg(dest, files.join(QLatin1Char(',')), volumesJson);
	}

	QString opRec(int id, const QString &src, const QString &dst, qint64 bytes = 0,
				  const QString &parked = QString(), const QString &srcIdJson = QString(),
				  const QString &dstIdJson = QString())
	{
		QString o = QStringLiteral(R"({"rec":"op","id":%1,"src":"%2","dst":"%3")")
						.arg(id)
						.arg(src, dst);
		if (bytes > 0)
			o += QStringLiteral(R"(,"bytes":%1)").arg(bytes);
		if (!parked.isEmpty())
			o += QStringLiteral(R"(,"parked":"%1")").arg(parked);
		if (!srcIdJson.isEmpty())
			o += QStringLiteral(R"(,"srcId":%1)").arg(srcIdJson);
		if (!dstIdJson.isEmpty())
			o += QStringLiteral(R"(,"dstId":%1)").arg(dstIdJson);
		return o + QLatin1Char('}');
	}

	QString doneRec(int id, const QString &finalPath = QString())
	{
		if (finalPath.isEmpty())
			return QStringLiteral(R"({"rec":"done","id":%1})").arg(id);
		return QStringLiteral(R"({"rec":"done","id":%1,"final":"%2"})").arg(id).arg(finalPath);
	}

	QString failRec(int id) { return QStringLiteral(R"({"rec":"fail","id":%1,"err":"x"})").arg(id); }
	QString failDirtyRec(int id)
	{
		return QStringLiteral(R"({"rec":"fail","id":%1,"err":"x","dirty":true})").arg(id);
	}
	QString skipRec(int id) { return QStringLiteral(R"({"rec":"skip","id":%1})").arg(id); }
	QString endRec(bool cancelled = false)
	{
		return cancelled
				   ? QStringLiteral(R"({"rec":"end","ok":1,"fail":0,"skip":0,"cancelled":true})")
				   : QStringLiteral(R"({"rec":"end","ok":1,"fail":0,"skip":0})");
	}
	QString recoveredRec()
	{
		return QStringLiteral(R"({"rec":"recovered","reversed":0,"failed":0})");
	}

	/// The filesystem-free identity fragment: size (+ optional UMID) at
	/// SizeTime strength, which is exactly what identity checks can
	/// vouch for in a hand-built scenario.
	QString idJson(qint64 size, const QString &umid = QString())
	{
		if (umid.isEmpty())
			return QStringLiteral(R"({"size":%1,"str":1})").arg(size);
		return QStringLiteral(R"({"size":%1,"umid":"%2","str":1})").arg(size).arg(umid);
	}

	QString volJson(const QString &uuid, const QString &label, const QString &root)
	{
		return QStringLiteral(R"({"uuid":"%1","label":"%2","fs":"apfs","root":"%3","str":2})")
			.arg(uuid, label, root);
	}

	VolumeIdentity makeVol(const QString &uuid, const QString &label, const QString &root)
	{
		VolumeIdentity v;
		v.uuid = uuid;
		v.label = label;
		v.fsType = QStringLiteral("apfs");
		v.rootPath = root;
		v.strength = VolumeIdentity::Strength::Full;
		return v;
	}
} // namespace

class TestOpRescue : public QObject
{
	Q_OBJECT
private slots:
	// MARK: - Sweep hygiene
	void interrupted_move_without_plan_is_rolled_back_wholesale();
	void clean_journal_is_kept_as_undo_candidate();
	void superseded_finished_journals_are_pruned();
	void aged_undo_candidate_is_pruned();
	void recovered_journal_with_nothing_left_is_pruned();
	void failed_op_journal_makes_no_noise();
	void live_owner_is_left_alone();
	void unknown_kind_is_left_untouched_but_stamped();
	void rerun_is_idempotent();

	// MARK: - Move reversal table
	void completed_but_src_back_is_not_clobbered();
	void inflight_move_with_full_dst_is_reversed();
	void inflight_move_with_partial_dst_is_flagged();
	void move_replace_restores_parked_original();
	void interrupted_move_with_plan_keeps_finished_work();

	// MARK: - Copy reversal table
	void interrupted_copy_replace_restores_parked_original();
	void copy_committed_before_done_line_keeps_the_finished_copy();
	void interrupted_copy_into_empty_slot_removes_the_partial();
	void interrupted_copy_full_size_into_empty_slot_is_kept();
	void copy_replace_with_park_gone_never_touches_dst();

	// MARK: - Delete reversal table
	void delete_is_restored_from_trash();
	void delete_flagged_when_trash_emptied();
	void interrupted_delete_with_plan_leaves_deleted_files_deleted();

	// MARK: - Dirty retries
	void dirty_fail_in_finished_copy_run_restores_parked_original();
	void finished_dirty_run_leaves_completed_ops_alone();

	// MARK: - Resumable classification
	void interrupted_run_with_plan_is_listed_resumable();
	void resumable_journal_survives_second_launch_without_rerolling();
	void cancelled_run_is_not_resumable_but_is_undo_candidate();
	void pending_agrees_with_run();
	void unreachable_source_volume_is_never_read_as_finished();

	// MARK: - Removal guards (adversarial review 2026-08-30)
	void dirty_move_leaves_restored_original_alone();
	void partial_cleanup_refuses_stranger_mxf();

	// MARK: - v2: identity blocks wrong-file reversals
	void identity_mismatch_blocks_move_reversal();
	void identity_mismatch_blocks_trash_restore();

	// MARK: - v2: volume resolution
	void resolvePath_follows_moved_volume_and_refuses_impostor();
	void run_reanchors_paths_when_volume_moved();
	void impostor_volume_defers_recovery_untouched();
	void missing_volume_defers_recovery_untouched();

	// MARK: - v2: undo runs and notes
	void interrupted_undo_of_move_recovers_with_move_semantics();
	void journal_notes_resurface_in_summary();

private:
	QString writeJournal(const QString &dir, const QStringList &lines, int seq = 1);
};

QString TestOpRescue::writeJournal(const QString &dir, const QStringList &lines, int seq)
{
	// Sequence number in the timestamp position keeps multi-journal
	// scenarios in a deterministic (chronological) scan order.
	return ::writeJournal(
		dir, QStringLiteral("journal-20260829T%1-test.jsonl").arg(seq, 9, 10, QLatin1Char('0')), lines);
}

// MARK: - Sweep hygiene

void TestOpRescue::interrupted_move_without_plan_is_rolled_back_wholesale()
{
	// No plan line = nothing can be offered for resume, so the only
	// help left is putting the files back.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "MOVED");

	writeJournal(tmp.path(), {beginRec("move"), opRec(0, src, dst, 5), doneRec(0)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QCOMPARE(readFile(src), QByteArray("MOVED"));
	QVERIFY(!QFile::exists(dst));
}

void TestOpRescue::clean_journal_is_kept_as_undo_candidate()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeJournal(
		tmp.path(),
		{beginRec("move"), opRec(0, "/v/a.mxf", "/w/a.mxf"), doneRec(0), endRec()});

	const auto sum = OpRescue::run(tmp.path());
	QVERIFY(!sum.anything());
	// The v2 retention rule: the newest finished, clean journal IS the
	// undo candidate — the sweep must not eat it.
	QVERIFY2(QFile::exists(path), "the undo candidate must survive the sweep");
	QVERIFY(OpJournal::latestUndoable(tmp.path()).has_value());
}

void TestOpRescue::superseded_finished_journals_are_pruned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString older = writeJournal(
		tmp.path(),
		{beginRec("move"), opRec(0, "/v/a.mxf", "/w/a.mxf"), doneRec(0), endRec()}, 1);
	const QString newest = writeJournal(
		tmp.path(),
		{beginRec("copy"), opRec(0, "/v/b.mxf", "/w/b.mxf"), doneRec(0), endRec()}, 2);

	OpRescue::run(tmp.path());
	QVERIFY2(!QFile::exists(older), "only the NEWEST finished journal is the undo candidate");
	QVERIFY(QFile::exists(newest));
}

void TestOpRescue::aged_undo_candidate_is_pruned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Started in 2020: far past the 7-day undo window.
	const QString stale = writeJournal(
		tmp.path(),
		{QStringLiteral(
			 R"({"schema":2,"rec":"begin","kind":"move","started":"2020-01-01T00:00:00.000Z","pid":999999,"host":"deadhost"})"),
		 opRec(0, "/v/a.mxf", "/w/a.mxf"), doneRec(0), endRec()});

	OpRescue::run(tmp.path());
	QVERIFY2(!QFile::exists(stale), "a finished journal past the undo window must be aged out");
}

void TestOpRescue::recovered_journal_with_nothing_left_is_pruned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = writeJournal(
		tmp.path(), {beginRec("move"), opRec(0, "/v/a.mxf", "/w/a.mxf"), recoveredRec()});

	const auto sum = OpRescue::run(tmp.path());
	QVERIFY(!sum.anything());
	QVERIFY(!QFile::exists(path));
}

void TestOpRescue::failed_op_journal_makes_no_noise()
{
	// A plain fail is the run's own statement that disk is as if the op
	// never ran; recovery must not "put back" anything.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	writeFile(src, "STILL HERE");

	writeJournal(tmp.path(),
				{beginRec("move"), opRec(0, src, tmp.path() + "/dst/a.mxf", 10), failRec(0)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 0);
	QCOMPARE(sum.opsFlagged, 0);
	QCOMPARE(readFile(src), QByteArray("STILL HERE"));
}

void TestOpRescue::live_owner_is_left_alone()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "MID-RUN");

	// This very process is the owner: alive by definition.
	const QString line =
		QStringLiteral(
			R"({"schema":2,"rec":"begin","kind":"move","started":"2026-08-29T10:00:00.000Z","pid":%1,"host":"%2"})")
			.arg(QCoreApplication::applicationPid())
			.arg(QSysInfo::machineHostName());
	const QString path =
		writeJournal(tmp.path(), {line, opRec(0, tmp.path() + "/src/a.mxf", dst, 7)});

	const auto sum = OpRescue::run(tmp.path());
	QVERIFY(!sum.anything());
	QVERIFY(QFile::exists(dst));
	// And not stamped: the live run owns it.
	QVERIFY(!readFile(path).contains("\"rec\":\"recovered\""));
}

void TestOpRescue::unknown_kind_is_left_untouched_but_stamped()
{
	// v2 flip, deliberate: v1 read an unknown kind as Move (a fallback)
	// and rolled it back with Move semantics. The v2 rule is NEVER
	// GUESS — an unknown kind reverses nothing, gets stamped so it
	// doesn't haunt every launch, and the files stay exactly put.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "MYSTERY");

	const QString path = writeJournal(
		tmp.path(),
		{beginRec("teleport"), opRec(0, tmp.path() + "/src/a.mxf", dst, 7), doneRec(0)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 0);
	QVERIFY(QFile::exists(dst));
	QVERIFY(readFile(path).contains("\"rec\":\"recovered\""));
}

void TestOpRescue::rerun_is_idempotent()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "MOVED");

	writeJournal(tmp.path(), {beginRec("move"), opRec(0, src, dst, 5), doneRec(0)});

	const auto first = OpRescue::run(tmp.path());
	QCOMPARE(first.opsReversed, 1);
	// A second sweep finds the recovered stamp and nothing to do; a
	// third party could crash us mid-rollback, so this must hold.
	const auto second = OpRescue::run(tmp.path());
	QCOMPARE(second.opsReversed, 0);
	QCOMPARE(second.opsFlagged, 0);
	QCOMPARE(readFile(src), QByteArray("MOVED"));
}

// MARK: - Move reversal table

void TestOpRescue::completed_but_src_back_is_not_clobbered()
{
	// src has reappeared (the user restored it, or an earlier sweep put
	// it back). Reversal must never overwrite a live src.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(src, "THE REAL ONE");
	writeFile(dst, "THE MOVED ONE");

	writeJournal(tmp.path(), {beginRec("move"), opRec(0, src, dst, 13), doneRec(0)});

	OpRescue::run(tmp.path());
	QCOMPARE(readFile(src), QByteArray("THE REAL ONE"));
	QCOMPARE(readFile(dst), QByteArray("THE MOVED ONE"));
}

void TestOpRescue::inflight_move_with_full_dst_is_reversed()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "FULL!");

	// No done line — cut off after the copy, before the record. bytes
	// says 5 and dst is 5: whole, safe to bring home.
	writeJournal(tmp.path(), {beginRec("move"), opRec(0, src, dst, 5)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QCOMPARE(readFile(src), QByteArray("FULL!"));
}

void TestOpRescue::inflight_move_with_partial_dst_is_flagged()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "PAR"); // 3 of 10 bytes: a fragment

	writeJournal(tmp.path(), {beginRec("move"), opRec(0, src, dst, 10)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsFlagged, 1);
	// The fragment must not be renamed home as if it were the file.
	QVERIFY(!QFile::exists(src));
	QVERIFY(QFile::exists(dst));
}

void TestOpRescue::move_replace_restores_parked_original()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	const QString parked = dst + ".__movereplace_test1";
	writeFile(dst, "MOVED IN");
	writeFile(parked, "THE REPLACED ORIGINAL");

	writeJournal(tmp.path(), {beginRec("move"), opRec(0, src, dst, 8, parked)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QCOMPARE(readFile(src), QByteArray("MOVED IN"));
	QCOMPARE(readFile(dst), QByteArray("THE REPLACED ORIGINAL"));
	QVERIFY(!QFile::exists(parked));
}

void TestOpRescue::interrupted_move_with_plan_keeps_finished_work()
{
	// Finished work stays: the completed file is untouched, only the
	// in-flight one is tidied, and the remainder is offered.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString srcA = tmp.path() + "/src/a.mxf";
	const QString dstA = tmp.path() + "/dst/a.mxf";
	const QString srcB = tmp.path() + "/src/b.mxf";
	const QString srcC = tmp.path() + "/src/c.mxf";
	writeFile(dstA, "A DONE");
	writeFile(tmp.path() + "/dst/b.mxf", "B PART"); // 6 of 10: in-flight partial
	writeFile(srcB, QByteArray(10, 'B'));
	writeFile(srcC, "C NEVER STARTED");

	writeJournal(tmp.path(), {beginRec("move"), planRec(tmp.path() + "/dst", {srcA, srcB, srcC}),
							 opRec(0, srcA, dstA, 6), doneRec(0),
							 opRec(1, srcB, tmp.path() + "/dst/b.mxf", 10)});

	const auto sum = OpRescue::run(tmp.path());
	// A's move stands.
	QCOMPARE(readFile(dstA), QByteArray("A DONE"));
	QVERIFY(!QFile::exists(srcA));
	// B's partial was removed, its source untouched.
	QVERIFY(!QFile::exists(tmp.path() + "/dst/b.mxf"));
	QCOMPARE(readFile(srcB), QByteArray(10, 'B'));
	// B and C are offered; A is counted finished.
	QCOMPARE(sum.resumable.size(), 1);
	QCOMPARE(sum.resumable[0].finished, 1);
	QCOMPARE(sum.resumable[0].remaining.size(), 2);
}

// MARK: - Copy reversal table

void TestOpRescue::interrupted_copy_replace_restores_parked_original()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	const QString parked = dst + ".__copyreplace_test1";
	writeFile(src, "SOURCE");
	writeFile(dst, "HALFCOPY");
	writeFile(parked, "THE ORIGINAL");

	writeJournal(tmp.path(), {beginRec("copy"), opRec(0, src, dst, 6, parked)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QCOMPARE(readFile(dst), QByteArray("THE ORIGINAL"));
	QCOMPARE(readFile(src), QByteArray("SOURCE"));
	QVERIFY(!QFile::exists(parked));
}

void TestOpRescue::copy_committed_before_done_line_keeps_the_finished_copy()
{
	// Empty-slot copy, dst whole, crash before the done line: a
	// finished copy we keep.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(src, "WHOLE");
	writeFile(dst, "WHOLE");

	writeJournal(tmp.path(), {beginRec("copy"), opRec(0, src, dst, 5)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 0);
	QCOMPARE(readFile(dst), QByteArray("WHOLE"));
}

void TestOpRescue::interrupted_copy_into_empty_slot_removes_the_partial()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(src, QByteArray(10, 'S'));
	writeFile(dst, "PAR"); // 3 of 10

	writeJournal(tmp.path(), {beginRec("copy"), opRec(0, src, dst, 10)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QVERIFY2(!QFile::exists(dst),
			 "a truncated file wearing a real media name reads as media to Avid");
	QCOMPARE(readFile(src), QByteArray(10, 'S'));
	// And it was narrated — the one file the sweep takes off the disk.
	QVERIFY(sum.message().contains(QStringLiteral("partial copy")));
}

void TestOpRescue::interrupted_copy_full_size_into_empty_slot_is_kept()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(src, QByteArray(10, 'S'));
	writeFile(dst, QByteArray(10, 'S'));

	writeJournal(tmp.path(), {beginRec("copy"), opRec(0, src, dst, 10)});

	OpRescue::run(tmp.path());
	QVERIFY(QFile::exists(dst));
}

void TestOpRescue::copy_replace_with_park_gone_never_touches_dst()
{
	// A recorded park path that is GONE means the op already concluded
	// one way or the other (committed, or restored). Both keep dst;
	// nothing about its size may be read into it.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(src, QByteArray(100, 'S'));
	writeFile(dst, "SMALL"); // way short of 100 — still untouchable

	writeJournal(tmp.path(),
				{beginRec("copy"), opRec(0, src, dst, 100, dst + ".__copyreplace_gone1")});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 0);
	QCOMPARE(readFile(dst), QByteArray("SMALL"));
}

// MARK: - Delete reversal table

void TestOpRescue::delete_is_restored_from_trash()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/media/a.mxf";
	const QString trash = tmp.path() + "/_MediaMuster_Trash/media/a.mxf";
	writeFile(trash, "DELETED");

	writeJournal(tmp.path(), {beginRec("delete"), opRec(0, src, QString(), 7), doneRec(0, trash)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QCOMPARE(readFile(src), QByteArray("DELETED"));
	QVERIFY(!QFile::exists(trash));
}

void TestOpRescue::delete_flagged_when_trash_emptied()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/media/a.mxf";
	const QString trash = tmp.path() + "/_MediaMuster_Trash/media/a.mxf"; // never written

	writeJournal(tmp.path(), {beginRec("delete"), opRec(0, src, QString(), 7), doneRec(0, trash)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsFlagged, 1);
	QVERIFY(sum.message().contains(QStringLiteral("no longer in the Trash")));
}

void TestOpRescue::interrupted_delete_with_plan_leaves_deleted_files_deleted()
{
	// Finished work stays for Delete too: files already in the trash
	// stay there; the remainder is offered, not silently restored.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString srcA = tmp.path() + "/media/a.mxf";
	const QString trashA = tmp.path() + "/_MediaMuster_Trash/media/a.mxf";
	const QString srcB = tmp.path() + "/media/b.mxf";
	writeFile(trashA, "A DELETED");
	writeFile(srcB, "B WAITING");

	writeJournal(tmp.path(), {beginRec("delete"), planRec(QString(), {srcA, srcB}),
							 opRec(0, srcA, QString(), 9), doneRec(0, trashA)});

	const auto sum = OpRescue::run(tmp.path());
	QVERIFY(!QFile::exists(srcA));
	QCOMPARE(readFile(trashA), QByteArray("A DELETED"));
	QCOMPARE(sum.resumable.size(), 1);
	QCOMPARE(sum.resumable[0].remaining.size(), 1);
	QCOMPARE(sum.resumable[0].remaining[0].src, srcB);
	QVERIFY(sum.resumable[0].usedMediaMusterTrash);
}

// MARK: - Dirty retries

void TestOpRescue::dirty_fail_in_finished_copy_run_restores_parked_original()
{
	// The run FINISHED (end line) but one op's rollback stalled with
	// the original still parked. The sweep's whole job here is that
	// one retry.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	const QString parked = dst + ".__copyreplace_dirty";
	writeFile(src, "SOURCE");
	writeFile(parked, "STRANDED ORIGINAL");

	writeJournal(tmp.path(), {beginRec("copy"), opRec(0, src, dst, 6, parked), failDirtyRec(0),
							 endRec()});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QCOMPARE(readFile(dst), QByteArray("STRANDED ORIGINAL"));
	QVERIFY(!QFile::exists(parked));
}

void TestOpRescue::finished_dirty_run_leaves_completed_ops_alone()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString srcA = tmp.path() + "/src/a.mxf";
	const QString dstA = tmp.path() + "/dst/a.mxf";
	writeFile(dstA, "A MOVED"); // completed op: must stand
	const QString srcB = tmp.path() + "/src/b.mxf";
	const QString dstB = tmp.path() + "/dst/b.mxf";
	const QString parkedB = dstB + ".__movereplace_dirty";
	// B's move FAILED (that's what a dirty fail is), so its source is
	// still in place — only the parked original never made it home.
	writeFile(srcB, "B SRC");
	writeFile(parkedB, "B ORIGINAL");

	writeJournal(tmp.path(), {beginRec("move"), opRec(0, srcA, dstA, 7), doneRec(0),
							 opRec(1, srcB, dstB, 5, parkedB), failDirtyRec(1), endRec()});

	const auto sum = OpRescue::run(tmp.path());
	// Only the dirty op was touched: A's move stands, B's original is
	// home.
	QCOMPARE(readFile(dstA), QByteArray("A MOVED"));
	QVERIFY(!QFile::exists(srcA));
	QCOMPARE(readFile(dstB), QByteArray("B ORIGINAL"));
	QCOMPARE(sum.opsReversed, 1);
}

// MARK: - Resumable classification

void TestOpRescue::interrupted_run_with_plan_is_listed_resumable()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString srcA = tmp.path() + "/src/a.mxf";
	const QString srcB = tmp.path() + "/src/b.mxf";
	writeFile(srcA, "AAAA");
	writeFile(srcB, "BBBB");

	const QString dest = tmp.path() + "/dst";
	writeJournal(tmp.path(), {beginRec("copy"), planRec(dest, {srcA, srcB})});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.resumable.size(), 1);
	QCOMPARE(sum.resumable[0].kind, OpKind::Copy);
	QCOMPARE(sum.resumable[0].dest, dest);
	QCOMPARE(sum.resumable[0].total, 2);
	QCOMPARE(sum.resumable[0].remaining.size(), 2);
}

void TestOpRescue::resumable_journal_survives_second_launch_without_rerolling()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	writeFile(src, "WAITING");

	const QString path =
		writeJournal(tmp.path(), {beginRec("copy"), planRec(tmp.path() + "/dst", {src})});

	QCOMPARE(OpRescue::run(tmp.path()).resumable.size(), 1);
	QVERIFY(QFile::exists(path));
	// Second launch: same offer, no re-rolling, journal still there
	// until the user resumes or discards.
	QCOMPARE(OpRescue::run(tmp.path()).resumable.size(), 1);
	QVERIFY(QFile::exists(path));
}

void TestOpRescue::cancelled_run_is_not_resumable_but_is_undo_candidate()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	writeFile(src, "WAS CANCELLED");

	writeJournal(tmp.path(), {beginRec("move"), planRec(tmp.path() + "/dst", {src}),
							 opRec(0, src, tmp.path() + "/dst/a.mxf", 13), doneRec(0),
							 endRec(/*cancelled=*/true)});

	const auto sum = OpRescue::run(tmp.path());
	// Concluded on the user's watch: not offered again…
	QVERIFY(sum.resumable.isEmpty());
	// …but stop-and-keep means its landed work is UNDOABLE.
	QVERIFY(OpJournal::latestUndoable(tmp.path()).has_value());
}

void TestOpRescue::pending_agrees_with_run()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	writeFile(src, "PENDING");

	writeJournal(tmp.path(), {beginRec("copy"), planRec(tmp.path() + "/dst", {src})});

	// Before the sweep: pending() must NOT offer an unswept journal.
	QVERIFY(OpRescue::pending(tmp.path()).isEmpty());
	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.resumable.size(), 1);
	// After: they agree.
	const auto pend = OpRescue::pending(tmp.path());
	QCOMPARE(pend.size(), 1);
	QCOMPARE(pend[0].remaining.size(), sum.resumable[0].remaining.size());
}

void TestOpRescue::unreachable_source_volume_is_never_read_as_finished()
{
	// The source lives under a mount root that isn't there. Its absence
	// proves nothing — the op must read as "don't know", stay
	// unfinished, and be offered once the drive is back.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	qputenv("MEDIAMUSTER_MOUNT_ROOT", (tmp.path() + "/mounts/").toUtf8());
	const QString src = tmp.path() + "/mounts/EDIT/media/a.mxf"; // volume absent

	writeJournal(tmp.path(), {beginRec("delete"), planRec(QString(), {src}),
							 opRec(0, src, QString(), 5)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.resumable.size(), 1);
	QCOMPARE(sum.resumable[0].finished, 0);
	qunsetenv("MEDIAMUSTER_MOUNT_ROOT");
}

// MARK: - v2: identity blocks wrong-file reversals

void TestOpRescue::identity_mismatch_blocks_move_reversal()
{
	// The journal recorded a 5-byte a.mxf; the file now at dst is 5
	// bytes of SOMETHING ELSE at the byte level we can't see — but here
	// the size itself differs from the identity record. Never rename a
	// stranger into the user's folder.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "IMPOSTOR CONTENT"); // 16 bytes

	// bytes matches what's on disk (so the whole-file gate passes), but
	// the IDENTITY the run captured says the real file was 5 bytes.
	writeJournal(tmp.path(),
				{beginRec("move"), opRec(0, src, dst, 16, QString(), idJson(5)), doneRec(0)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsFlagged, 1);
	QVERIFY(!QFile::exists(src));
	QCOMPARE(readFile(dst), QByteArray("IMPOSTOR CONTENT"));
	QVERIFY(sum.message().contains(QStringLiteral("doesn't match")));
}

void TestOpRescue::identity_mismatch_blocks_trash_restore()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/media/a.mxf";
	const QString trash = tmp.path() + "/_MediaMuster_Trash/media/a.mxf";
	writeFile(trash, "NOT THE DELETED FILE"); // 20 bytes; identity says 7

	writeJournal(tmp.path(), {beginRec("delete"),
							 opRec(0, src, QString(), 7, QString(), idJson(7)),
							 doneRec(0, trash)});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsFlagged, 1);
	QVERIFY(!QFile::exists(src));
	QCOMPARE(readFile(trash), QByteArray("NOT THE DELETED FILE"));
}

// MARK: - v2: volume resolution

void TestOpRescue::resolvePath_follows_moved_volume_and_refuses_impostor()
{
	const QVector<VolumeIdentity> recorded = {
		makeVol(QStringLiteral("AAAA-1111"), QStringLiteral("EDIT 1"),
				QStringLiteral("/Volumes/EDIT 1"))};

	// Same drive, same address.
	{
		const auto r = OpRescue::resolvePath(QStringLiteral("/Volumes/EDIT 1/media/a.mxf"),
											 recorded, recorded);
		QCOMPARE(r.state, OpRescue::ResolvedPath::State::Proceed);
	}
	// Drive back under a new name: follow it.
	{
		const QVector<VolumeIdentity> mounted = {
			makeVol(QStringLiteral("AAAA-1111"), QStringLiteral("EDIT 1"),
					QStringLiteral("/Volumes/EDIT 1 1"))};
		const auto r = OpRescue::resolvePath(QStringLiteral("/Volumes/EDIT 1/media/a.mxf"),
											 recorded, mounted);
		QCOMPARE(r.state, OpRescue::ResolvedPath::State::Reanchored);
		QCOMPARE(r.path, QStringLiteral("/Volumes/EDIT 1 1/media/a.mxf"));
	}
	// A DIFFERENT drive at the recorded address, real one nowhere: wait.
	{
		const QVector<VolumeIdentity> mounted = {
			makeVol(QStringLiteral("BBBB-2222"), QStringLiteral("SOMEONE ELSES"),
					QStringLiteral("/Volumes/EDIT 1"))};
		const auto r = OpRescue::resolvePath(QStringLiteral("/Volumes/EDIT 1/media/a.mxf"),
											 recorded, mounted);
		QCOMPARE(r.state, OpRescue::ResolvedPath::State::Wait);
		QVERIFY(r.note.contains(QStringLiteral("different volume")));
	}
	// Nothing mounted anywhere: wait.
	{
		const auto r = OpRescue::resolvePath(QStringLiteral("/Volumes/EDIT 1/media/a.mxf"),
											 recorded, {});
		QCOMPARE(r.state, OpRescue::ResolvedPath::State::Wait);
	}
	// A path on a volume with no recorded fingerprint: proceed as-is.
	{
		const auto r =
			OpRescue::resolvePath(QStringLiteral("/elsewhere/b.mxf"), recorded, {});
		QCOMPARE(r.state, OpRescue::ResolvedPath::State::Proceed);
	}
}

void TestOpRescue::run_reanchors_paths_when_volume_moved()
{
	// The drive the run used is back under a different root. Recovery
	// must find the moved file THERE and put it back THERE.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString oldRoot = tmp.path() + "/oldroot";
	const QString newRoot = tmp.path() + "/newroot";
	writeFile(newRoot + "/dst/a.mxf", "MOVED");

	writeJournal(tmp.path(),
				{beginRec("move"),
				 planRecWithVolumes(oldRoot + "/dst", {oldRoot + "/src/a.mxf"},
									volJson(QStringLiteral("AAAA-1111"),
											QStringLiteral("EDIT"), oldRoot)),
				 opRec(0, oldRoot + "/src/a.mxf", oldRoot + "/dst/a.mxf", 5)});

	const QVector<VolumeIdentity> mounted = {
		makeVol(QStringLiteral("AAAA-1111"), QStringLiteral("EDIT"), newRoot)};
	const auto sum = OpRescue::run(tmp.path(), mounted);

	QCOMPARE(sum.opsReversed, 1);
	QCOMPARE(readFile(newRoot + "/src/a.mxf"), QByteArray("MOVED"));
	QVERIFY(sum.message().contains(QStringLiteral("followed it there")));
}

void TestOpRescue::impostor_volume_defers_recovery_untouched()
{
	// A different drive sits at the recorded address. NOTHING on it may
	// be touched, and the journal stays unstamped for a later launch.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = tmp.path() + "/root";
	writeFile(root + "/dst/a.mxf", "SOMEBODY ELSES FILE");

	const QString path = writeJournal(
		tmp.path(),
		{beginRec("move"),
		 planRecWithVolumes(root + "/dst", {root + "/src/a.mxf"},
							volJson(QStringLiteral("AAAA-1111"), QStringLiteral("EDIT"), root)),
		 opRec(0, root + "/src/a.mxf", root + "/dst/a.mxf", 19)});

	const QVector<VolumeIdentity> mounted = {
		makeVol(QStringLiteral("BBBB-2222"), QStringLiteral("OTHER"), root)};
	const auto sum = OpRescue::run(tmp.path(), mounted);

	QCOMPARE(sum.opsReversed, 0);
	QCOMPARE(readFile(root + "/dst/a.mxf"), QByteArray("SOMEBODY ELSES FILE"));
	QVERIFY(!readFile(path).contains("\"rec\":\"recovered\""));
	QVERIFY(sum.message().contains(QStringLiteral("different volume")));
}

void TestOpRescue::missing_volume_defers_recovery_untouched()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = tmp.path() + "/root";
	writeFile(root + "/dst/a.mxf", "WAITING FOR ITS DRIVE");

	const QString path = writeJournal(
		tmp.path(),
		{beginRec("move"),
		 planRecWithVolumes(root + "/dst", {root + "/src/a.mxf"},
							volJson(QStringLiteral("AAAA-1111"), QStringLiteral("EDIT"), root)),
		 opRec(0, root + "/src/a.mxf", root + "/dst/a.mxf", 21)});

	// Mounted table knows nothing about this volume at all.
	const QVector<VolumeIdentity> mounted = {
		makeVol(QStringLiteral("CCCC-3333"), QStringLiteral("SYSTEM"), QStringLiteral("/"))};
	const auto sum = OpRescue::run(tmp.path(), mounted);

	QCOMPARE(sum.opsReversed, 0);
	QVERIFY(QFile::exists(root + "/dst/a.mxf"));
	QVERIFY(!readFile(path).contains("\"rec\":\"recovered\""));
	QVERIFY(sum.message().contains(QStringLiteral("isn't connected")));
}

// MARK: - v2: undo runs and notes

void TestOpRescue::interrupted_undo_of_move_recovers_with_move_semantics()
{
	// An undo-of-move died halfway. Its ops are already inverse-
	// oriented (src = where the file was moved TO, dst = home). Op 0
	// finished (file is home) — finished work stays. Op 1 was cut off
	// with its file fully back at dst(=home): reversed, i.e. the
	// half-done undo of that file is completed by recovery.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString homeA = tmp.path() + "/home/a.mxf";
	const QString awayA = tmp.path() + "/away/a.mxf";
	const QString homeB = tmp.path() + "/home/b.mxf";
	const QString awayB = tmp.path() + "/away/b.mxf";
	writeFile(homeA, "A HOME"); // op 0 concluded
	writeFile(homeB, "B HOME"); // op 1 in flight, dst whole

	writeJournal(tmp.path(),
				{beginUndoRec(QStringLiteral("move"), QStringLiteral("journal-orig.jsonl")),
				 planRec(QString(), {awayA, awayB}), opRec(0, awayA, homeA, 6), doneRec(0),
				 opRec(1, awayB, homeB, 6)});

	const auto sum = OpRescue::run(tmp.path());
	// A stays undone (home).
	QCOMPARE(readFile(homeA), QByteArray("A HOME"));
	QVERIFY(!QFile::exists(awayA));
	// B: the in-flight op's whole dst is brought back to src (away) —
	// the interrupted undo step is unwound, leaving a consistent state.
	QCOMPARE(readFile(awayB), QByteArray("B HOME"));
	QVERIFY(!QFile::exists(homeB));
	QCOMPARE(sum.opsReversed, 1);
	// The narration names it an undo.
	QVERIFY(sum.message().contains(QStringLiteral("undo")));
}

void TestOpRescue::journal_notes_resurface_in_summary()
{
	// Notes were written to be read: a durability degrade recorded
	// mid-run must reach the user's recovery report.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/a.mxf";
	const QString dst = tmp.path() + "/dst/a.mxf";
	writeFile(dst, "MOVED");

	// No apostrophes in this raw string: moc's simplified lexer reads a
	// lone ' as the start of a char literal and falls over at EOF.
	writeJournal(tmp.path(),
				{beginRec("move"), opRec(0, src, dst, 5),
				 QStringLiteral(
					 R"({"rec":"note","text":"a.mxf: the destination volume could not confirm a full flush to disk."})")});

	const auto sum = OpRescue::run(tmp.path());
	QCOMPARE(sum.opsReversed, 1);
	QVERIFY(sum.message().contains(QStringLiteral("full flush")));
}

// MARK: - Removal guards (adversarial review 2026-08-30)

// Finding 2: a rollback that stalled (dirty flag written) and then
// completed leaves the REPLACED ORIGINAL back at the destination. It is a
// different size from the source, so the sweep's size test reads it as a
// "partial" — and used to hard-delete the user's original as tidying.
// The identity of the parked file is in the journal for exactly this:
// match ⇒ the rollback is already done, keep the file.
void TestOpRescue::dirty_move_leaves_restored_original_alone()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dst = tmp.path() + "/dst/clip.mxf";
	writeFile(src, QByteArray(1000, 'S'));
	writeFile(dst, QByteArray(600, 'O')); // the restored replaced-original

	writeJournal(tmp.path(),
				{beginRec("move"),
				 opRec(0, src, dst, 1000, dst + ".__movereplace_gone", idJson(1000),
					   idJson(600)),
				 failDirtyRec(0), endRec()});

	const OpRescue::Summary sum = OpRescue::run(tmp.path());

	// The whole point: the restored original is NOT removed as a
	// "partial", nothing is reversed or flagged (there is nothing left to
	// do), and the sweep stays quiet — silence when nothing changed is
	// the sweep's narration policy.
	QVERIFY2(QFile::exists(dst), "the restored original was deleted as a 'partial'");
	QCOMPARE(readFile(dst), QByteArray(600, 'O'));
	QCOMPARE(readFile(src), QByteArray(1000, 'S'));
	QCOMPARE(sum.opsReversed, 0);
	QCOMPARE(sum.opsFlagged, 0);
}

// The finding-3 racer at recovery time: a short file at an interrupted
// copy's destination whose Avid UMID names a DIFFERENT clip is another
// program's media that landed there after the crash — flagged and left,
// never removed. (The engine's own partial is a prefix of the source, so
// a parseable partial carries the source's own UMID.)
void TestOpRescue::partial_cleanup_refuses_stranger_mxf()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString fixtureA =
		QStringLiteral(FIXTURES_DIR "/avid_headers/A01.E683C413_F82F4F82F4625A.mxf");
	const QString fixtureB =
		QStringLiteral(FIXTURES_DIR "/avid_headers/A01.E683CD73_FF4BEFF4BE934A.mxf");

	const QString src = tmp.path() + "/src/ours.mxf";
	const QString dst = tmp.path() + "/dst/ours.mxf";
	QDir().mkpath(tmp.path() + "/src");
	QDir().mkpath(tmp.path() + "/dst");
	QVERIFY(QFile::copy(fixtureA, src));

	// The stranger: a DIFFERENT clip's media, truncated so the size test
	// reads it as a partial (a whole same-size file takes the
	// finished-work-stays path instead and is never at risk).
	{
		QFile in(fixtureB);
		QVERIFY(in.open(QIODevice::ReadOnly));
		writeFile(dst, in.read(200000));
	}

	const FileIdentity srcNow = FileIdentity::capture(src);
	const FileIdentity dstNow = FileIdentity::capture(dst);
	QVERIFY(!srcNow.contentUmid.isEmpty());
	QVERIFY(!dstNow.contentUmid.isEmpty());
	QVERIFY2(srcNow.contentUmid != dstNow.contentUmid,
			 "fixtures must be different clips for this test to mean anything");

	writeJournal(tmp.path(), {beginRec("copy"),
							 opRec(0, src, dst, srcNow.size, QString(),
								   idJson(srcNow.size, srcNow.contentUmid))});

	const OpRescue::Summary sum = OpRescue::run(tmp.path());

	QVERIFY2(QFile::exists(dst), "another program's media was deleted as our 'partial'");
	QVERIFY(sum.opsFlagged > 0);
	QVERIFY(sum.message().contains(QStringLiteral("doesn't match this run's records")));
}

QTEST_APPLESS_MAIN(TestOpRescue)
#include "tst_oprescue.moc"
