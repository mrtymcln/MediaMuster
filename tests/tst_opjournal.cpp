#include "opjournal.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using Kind = OpJournal::Kind;

class TestOpJournal : public QObject
{
	Q_OBJECT
private slots:
	void kind_names_round_trip();
	void opens_into_given_dir();
	void clean_run_round_trips();
	void interrupted_run_has_no_end();
	void delete_final_path_round_trips();
	void move_replace_parked_round_trips();
	void zero_bytes_omitted_from_line();
	void begin_line_stamps_schema();
	void begin_records_owner_pid_host();
	void cancelled_run_is_complete_but_flagged();
	void recovered_marker_round_trips();
	void weird_paths_stay_one_record();
	void torn_final_line_is_tolerated();
	void scan_empty_dir_is_empty();
	void scans_every_journal_in_dir();
	void prune_removes_finished_journal();
	void prune_is_noop_before_finish();

	// Dirty fails (finding 3): rollback-incomplete ops round-trip through
	// the read side and pin the journal on disk past finish()+prune().
	void dirty_fail_round_trips_and_blocks_prune();

	// Finding 8: a degraded journal (a line write failed mid-run) is
	// deleted at finish even when dirty — with lines missing it misleads
	// recovery, which could read a finished run as interrupted and roll
	// back moves that completed.
	void degraded_journal_prunes_even_when_dirty();

	// Finding 11: the UI asks this before dispatching a destructive run,
	// so "no crash protection" is a question, not a console whisper.
	void standardDirWritable_reflects_env_dir();

private:
	/// First physical line of the file at `path`, trimmed.
	static QByteArray firstLine(const QString &path);
};

QByteArray TestOpJournal::firstLine(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return f.readLine().trimmed();
}

void TestOpJournal::kind_names_round_trip()
{
	for (Kind k : {Kind::Move, Kind::Rebalance, Kind::Delete})
	{
		bool ok = false;
		QCOMPARE(OpJournal::kindFromName(OpJournal::kindName(k), &ok), k);
		QVERIFY(ok);
	}

	// Garbage in -> Move out, but the ok flag tells the caller it lied.
	bool ok = true;
	QCOMPARE(OpJournal::kindFromName(QStringLiteral("nonsense"), &ok), Kind::Move);
	QVERIFY(!ok);
}

void TestOpJournal::opens_into_given_dir()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	OpJournal j(Kind::Move, {}, tmp.path());
	QVERIFY(j.isOpen());
	QVERIFY(j.path().startsWith(tmp.path()));
	QVERIFY(j.path().endsWith(QStringLiteral(".jsonl")));
	QVERIFY(QFile::exists(j.path()));
}

void TestOpJournal::clean_run_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QJsonObject meta{{QStringLiteral("dest"), QStringLiteral("/Volumes/Backup")},
						   {QStringLiteral("preserve"), true}};
	{
		OpJournal j(Kind::Rebalance, meta, tmp.path());
		QVERIFY(j.isOpen());

		const int a = j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"), 12345);
		const int b = j.planOp(QStringLiteral("/A/y.mxf"), QStringLiteral("/B/y.mxf"), 678);
		const int c = j.planOp(QStringLiteral("/A/z.mxf"), QStringLiteral("/B/z.mxf"));
		j.markDone(a);
		j.markFailed(b, QStringLiteral("disk full"));
		j.markSkipped(c);
		j.finish(1, 1, 1);
	}

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);

	const OpJournal::Record &r = recs.first();
	QCOMPARE(r.kind, Kind::Rebalance);
	QVERIFY(r.complete);
	QVERIFY(!r.cancelled);
	QVERIFY(!r.recovered);
	QVERIFY(!r.started.isEmpty());
	QCOMPARE(r.meta.value(QStringLiteral("dest")).toString(), QStringLiteral("/Volumes/Backup"));
	QCOMPARE(r.meta.value(QStringLiteral("preserve")).toBool(), true);

	QCOMPARE(r.ops.size(), 3);

	QCOMPARE(r.ops[0].src, QStringLiteral("/A/x.mxf"));
	QCOMPARE(r.ops[0].dst, QStringLiteral("/B/x.mxf"));
	QCOMPARE(r.ops[0].bytes, qint64(12345));
	QVERIFY(r.ops[0].completed);
	QVERIFY(!r.ops[0].failed);
	QVERIFY(!r.ops[0].skipped);

	QVERIFY(!r.ops[1].completed);
	QVERIFY(r.ops[1].failed);

	QVERIFY(!r.ops[2].completed);
	QVERIFY(r.ops[2].skipped);
}

void TestOpJournal::interrupted_run_has_no_end()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Crash mid-run: drop the journal without ever calling finish().
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		const int a = j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"));
		j.planOp(QStringLiteral("/A/y.mxf"), QStringLiteral("/B/y.mxf"));
		j.markDone(a);
		// no finish() means no 'end' line.
	}

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);

	const OpJournal::Record &r = recs.first();
	QVERIFY(!r.complete); // the half-open state recovery looks for
	QCOMPARE(r.ops.size(), 2);
	QVERIFY(r.ops[0].completed);  // already done, so recovery reverses this one
	QVERIFY(!r.ops[1].completed); // never finished, so nothing to undo
}

void TestOpJournal::delete_final_path_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Delete records where the file landed (the trash) so rollback
	// knows what to put back.
	const QString trash = QStringLiteral("/Volumes/Media/_MediaMuster_Trash/clip.mxf");
	{
		OpJournal j(Kind::Delete, {}, tmp.path());
		const int id = j.planOp(QStringLiteral("/Volumes/Media/clip.mxf"), QString());
		j.markDone(id, trash);
		j.finish(1, 0, 0);
	}

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);
	QCOMPARE(recs.first().kind, Kind::Delete);
	QCOMPARE(recs.first().ops.size(), 1);
	QVERIFY(recs.first().ops[0].completed);
	QCOMPARE(recs.first().ops[0].finalPath, trash);
}

void TestOpJournal::move_replace_parked_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Move-Replace shoves the existing destination aside first; the parked
	// path has to survive a round-trip so rollback can put it back.
	const QString parked = QStringLiteral("/B/x.mxf.__movereplace_abcd1234");
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"), 99, parked);
		j.finish(0, 0, 0);
	}

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);
	QCOMPARE(recs.first().ops.size(), 1);
	QCOMPARE(recs.first().ops[0].parked, parked);
}

void TestOpJournal::zero_bytes_omitted_from_line()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QString path;
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf")); // bytes defaults to 0
		path = j.path();
		j.finish(0, 0, 0);
	}

	// The op line is the second physical line; pull it and confirm
	// we didn't waste bytes serialising a zero.
	QFile f(path);
	QVERIFY(f.open(QIODevice::ReadOnly));
	f.readLine(); // begin
	const QByteArray opLine = f.readLine();
	QVERIFY(opLine.contains("\"rec\":\"op\""));
	QVERIFY(!opLine.contains("\"bytes\""));

	// ...and it still reads back as zero.
	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.first().ops[0].bytes, qint64(0));
}

void TestOpJournal::begin_line_stamps_schema()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	OpJournal j(Kind::Move, {}, tmp.path());
	const QByteArray begin = firstLine(j.path());
	// The schema version is the contract with future readers; lock it.
	QVERIFY(begin.contains("\"schema\":1"));
	QVERIFY(begin.contains("\"rec\":\"begin\""));
	QVERIFY(begin.contains("\"kind\":\"move\""));
	QVERIFY(begin.contains("\"pid\":"));
	QVERIFY(begin.contains("\"host\":"));
}

void TestOpJournal::torn_final_line_is_tolerated()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QString path;
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		const int id = j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"), 100);
		j.markDone(id);
		path = j.path();
		// no finish(), so interrupted
	}

	// Simulate a crash mid-write: a half-written, un-terminated JSON line.
	QFile f(path);
	QVERIFY(f.open(QIODevice::Append));
	f.write("{\"rec\":\"op\",\"id\":9,\"src\":\"/A/y");
	f.close();

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);
	QVERIFY(!recs.first().complete);
	QCOMPARE(recs.first().ops.size(), 1); // torn line skipped, good op survives
	QVERIFY(recs.first().ops[0].completed);
}

void TestOpJournal::scan_empty_dir_is_empty()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(OpJournal::scan(tmp.path()).isEmpty());

	// A dir that doesn't exist at all is also fine: no crash, no records.
	QVERIFY(OpJournal::scan(tmp.path() + QStringLiteral("/nope")).isEmpty());
}

void TestOpJournal::scans_every_journal_in_dir()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	{
		OpJournal a(Kind::Move, {}, tmp.path());
		a.finish(0, 0, 0);
	}
	{
		OpJournal b(Kind::Delete, {}, tmp.path());
		b.finish(0, 0, 0);
	}

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 2);

	// Both runs come back; filename timestamps may share a millisecond so
	// don't pin the order, just the membership.
	QSet<Kind> kinds;
	for (const auto &r : recs)
		kinds.insert(r.kind);
	QVERIFY(kinds.contains(Kind::Move));
	QVERIFY(kinds.contains(Kind::Delete));
}

void TestOpJournal::begin_records_owner_pid_host()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		j.finish(0, 0, 0);
	}

	// Recovery uses these to tell an interrupted run from one a still-live
	// instance is actively writing.
	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);
	QCOMPARE(recs.first().pid, QCoreApplication::applicationPid());
	QVERIFY(!recs.first().host.isEmpty());
}

void TestOpJournal::cancelled_run_is_complete_but_flagged()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		const int id = j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"));
		j.markDone(id);
		j.finish(1, 0, 0, /*cancelled=*/true);
	}

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);
	QVERIFY(recs.first().complete);	 // has an end line, recovery leaves it alone
	QVERIFY(recs.first().cancelled); // but we still know it was a cancel
}

void TestOpJournal::recovered_marker_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QString path;
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		const int id = j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"));
		j.markDone(id);
		path = j.path();
		// interrupted: no finish()
	}

	// Before recovery: interrupted, not yet walked back.
	{
		const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
		QCOMPARE(recs.size(), 1);
		QVERIFY(!recs.first().complete);
		QVERIFY(!recs.first().recovered);
	}

	QVERIFY(OpJournal::markRecovered(path, 1, 0));

	// After: still no 'end' line, but now flagged recovered so the next
	// launch won't roll it back a second time.
	{
		const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
		QCOMPARE(recs.size(), 1);
		QVERIFY(!recs.first().complete);
		QVERIFY(recs.first().recovered);
	}
}

void TestOpJournal::weird_paths_stay_one_record()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// A quote, an embedded newline and non-ASCII. Compact JSON must escape
	// all three so one op stays one physical line; otherwise the newline
	// would split the record and scan() would lose half of it.
	const QString weird = QStringLiteral("/A/he said \"hi\"\ncafé.mxf");
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		j.planOp(weird, QStringLiteral("/B/x.mxf"));
		j.finish(0, 0, 0);
	}

	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);
	QCOMPARE(recs.first().ops.size(), 1);
	QCOMPARE(recs.first().ops[0].src, weird);
}

void TestOpJournal::prune_removes_finished_journal()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		const int id = j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"));
		j.markDone(id);
		j.finish(1, 0, 0);
		j.prune();
	}
	// A concluded run is deleted, so there's nothing left to scan.
	QVERIFY(OpJournal::scan(tmp.path()).isEmpty());
}

void TestOpJournal::prune_is_noop_before_finish()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QString path;
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"));
		path = j.path();
		j.prune(); // must NOT delete an open, unfinished journal
	}

	// Still on disk and still looks interrupted; deleting an open journal
	// would have thrown away a run recovery needs.
	const QVector<OpJournal::Record> recs = OpJournal::scan(tmp.path());
	QCOMPARE(recs.size(), 1);
	QVERIFY(!recs.first().complete);
	QVERIFY(QFile::exists(path));
}

void TestOpJournal::dirty_fail_round_trips_and_blocks_prune()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QString path;
	{
		OpJournal j(Kind::Copy, {}, tmp.path());
		QVERIFY(j.isOpen());
		path = j.path();

		const int id = j.planOp(QStringLiteral("/A/clip.mxf"), QStringLiteral("/B/clip.mxf"), 8,
								QStringLiteral("/B/clip.mxf.__copyreplace_x"));
		j.markFailed(id, QStringLiteral("restore failed"), /*rollbackIncomplete=*/true);
		j.finish(0, 1, 0);

		// The journal is the stranded original's only record; prune must
		// refuse even though finish() has run.
		j.prune();
	}
	QVERIFY(QFile::exists(path));

	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QVERIFY(rec->complete); // the run DID finish...
	QVERIFY(rec->dirty);	// ...but carries unfinished rollback business
	QCOMPARE(rec->ops.size(), 1);
	QVERIFY(rec->ops[0].failed);
	QVERIFY(rec->ops[0].rollbackIncomplete);
}

void TestOpJournal::degraded_journal_prunes_even_when_dirty()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QString path;
	{
		OpJournal j(Kind::Move, {}, tmp.path());
		QVERIFY(j.isOpen());
		path = j.path();
		const int id = j.planOp(QStringLiteral("/A/x.mxf"), QStringLiteral("/B/x.mxf"), 8,
								QStringLiteral("/B/x.mxf.__movereplace_t"));
		j.markFailed(id, QStringLiteral("restore failed"), /*rollbackIncomplete=*/true);
		j.debugForceWriteFailure(); // as if the dirty line above never reached disk
		j.finish(0, 1, 0);
		j.prune();
	}

	// Dirty alone would pin the file (previous test); degraded overrides —
	// a journal with missing lines is worse than none.
	QVERIFY(!QFile::exists(path));
}

void TestOpJournal::standardDirWritable_reflects_env_dir()
{
	const QByteArray oldEnv = qgetenv("MEDIAMUSTER_OPLOG_DIR");

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	qputenv("MEDIAMUSTER_OPLOG_DIR", (tmp.path() + QStringLiteral("/oplog")).toUtf8());
	QVERIFY(OpJournal::standardDirWritable());

#ifndef Q_OS_WIN
	// Point the oplog inside a write-protected directory: mkpath fails and
	// the probe must report unwritable.
	const QString locked = tmp.path() + QStringLiteral("/locked");
	QVERIFY(QDir().mkpath(locked));
	QFile lockedDir(locked);
	QVERIFY(lockedDir.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));

	qputenv("MEDIAMUSTER_OPLOG_DIR", (locked + QStringLiteral("/oplog")).toUtf8());
	QVERIFY(!OpJournal::standardDirWritable());

	// Unlock so QTemporaryDir can clean up.
	lockedDir.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
							 QFileDevice::ExeOwner);
#endif

	if (oldEnv.isEmpty())
		qunsetenv("MEDIAMUSTER_OPLOG_DIR");
	else
		qputenv("MEDIAMUSTER_OPLOG_DIR", oldEnv);
}

QTEST_APPLESS_MAIN(TestOpJournal)
#include "tst_opjournal.moc"