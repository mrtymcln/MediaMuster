#include "opjournal.h"

#include "testutil.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// OpJournal — the engine v2 write-ahead journal. Re-pins every behaviour
// the v1 OpJournal tests held (round-trips, torn lines, degraded
// semantics) at schema 2, plus the v2 additions: identities and volume
// fingerprints in the records, retention (a finished journal SURVIVES so
// undo can use it; the NEXT run's constructor prunes it), the undone
// marker, notes, and the latestUndoable qualification rules.

namespace
{
	QStringList readLines(const QString &path)
	{
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
			return {};
		QStringList out;
		while (!f.atEnd())
		{
			const QString line = QString::fromUtf8(f.readLine()).trimmed();
			if (!line.isEmpty())
				out << line;
		}
		return out;
	}

	QString beginLine(const QString &kind, int schema = 2)
	{
		return QStringLiteral(
				   R"({"schema":%1,"rec":"begin","kind":"%2","started":"2026-01-01T00:00:00.000Z","pid":999999,"host":"deadhost"})")
			.arg(schema)
			.arg(kind);
	}

	FileIdentity sampleIdentity()
	{
		FileIdentity id;
		id.size = 4096;
		id.mtimeNs = Q_INT64_C(1'756'000'000'000'000'000);
		id.fileId = Q_UINT64_C(0xABCDEF0123456789);
		id.volumeId = Q_UINT64_C(0x42);
		id.contentUmid = QStringLiteral("060A2B340101010101010F00130000001234");
		id.strength = FileIdentity::Strength::Full;
		return id;
	}
} // namespace

class TestOpJournal : public QObject
{
	Q_OBJECT
private slots:
	// MARK: - Writer round-trips
	void opens_into_given_dir();
	void clean_run_round_trips();
	void plan_round_trips_items_and_volumes();
	void journal_without_plan_reads_has_plan_false();
	void interrupted_run_has_no_end();
	void delete_final_path_round_trips();
	void replace_park_and_parked_final_round_trip();
	void zero_bytes_omitted_from_line();
	void begin_line_stamps_schema_two();
	void begin_records_owner_pid_host();
	void cancelled_run_is_complete_but_flagged();
	void undo_meta_round_trips();
	void note_round_trips();
	void weird_paths_stay_one_record();

	// MARK: - Reader robustness
	void torn_final_line_is_tolerated();
	void scan_empty_dir_is_empty();
	void scans_every_journal_in_dir();
	void legacy_schema_is_invisible_and_untouched();

	// MARK: - Markers
	void recovered_marker_round_trips();
	void undone_marker_round_trips();

	// MARK: - Retention
	void finished_journal_survives_finish();
	void next_run_prunes_superseded_journals();
	void interrupted_and_dirty_journals_survive_pruning();
	void degraded_journal_self_destructs_on_finish_even_when_dirty();

	// MARK: - Undo candidacy
	void latest_undoable_picks_newest_qualifying();
	void latest_undoable_refuses_disqualified();

	// MARK: - Locations
	void standardDirWritable_reflects_env_dir();
};

// MARK: - Writer round-trips

void TestOpJournal::opens_into_given_dir()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	OpJournal journal(OpKind::Copy, {}, tmp.path());
	QVERIFY(journal.isOpen());
	QVERIFY(journal.path().startsWith(tmp.path()));
	QVERIFY(journal.path().contains(QStringLiteral("journal-")));
	QVERIFY(journal.path().endsWith(QStringLiteral(".jsonl")));
}

void TestOpJournal::clean_run_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const FileIdentity srcId = sampleIdentity();

	QString path;
	{
		OpJournal journal(OpKind::Move, {{QStringLiteral("dest"), QStringLiteral("/dst")}},
						tmp.path());
		QVERIFY(journal.isOpen());
		path = journal.path();

		const int a = journal.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"),
									4096, QString(), srcId);
		OpJournal::DoneInfo info;
		info.hash = QStringLiteral("1a2b3c4d5e6f7788");
		info.landedId = srcId;
		journal.markDone(a, info);

		const int b = journal.planOp(QStringLiteral("/src/b.mxf"), QStringLiteral("/dst/b.mxf"),
									8192, QString(), srcId);
		journal.markFailed(b, QStringLiteral("copy failed"));

		journal.finish(1, 1, 0);
	}

	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QCOMPARE(rec->schema, 2);
	QVERIFY(rec->kindKnown);
	QCOMPARE(rec->kind, OpKind::Move);
	QVERIFY(rec->complete);
	QVERIFY(!rec->cancelled);
	QVERIFY(!rec->dirty);
	QCOMPARE(rec->ops.size(), 2);

	// The op line carried the source identity the run verified against…
	QCOMPARE(rec->ops[0].srcId.size, srcId.size);
	QCOMPARE(rec->ops[0].srcId.fileId, srcId.fileId);
	QCOMPARE(rec->ops[0].srcId.contentUmid, srcId.contentUmid);
	QCOMPARE(rec->ops[0].srcId.strength, srcId.strength);
	// …and the done line carried the hash and the landed file's identity.
	QVERIFY(rec->ops[0].completed);
	QCOMPARE(rec->ops[0].hash, QStringLiteral("1a2b3c4d5e6f7788"));
	QCOMPARE(rec->ops[0].landedId.fileId, srcId.fileId);

	QVERIFY(rec->ops[1].failed);
	QVERIFY(!rec->ops[1].completed);
	QCOMPARE(rec->doneCount(), 1);
}

void TestOpJournal::plan_round_trips_items_and_volumes()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	OpItem it;
	it.src = QStringLiteral("/vol/Avid MediaFiles/MXF/1/clip.mxf");
	it.name = QStringLiteral("clip.mxf");
	it.folder = QStringLiteral("1");
	it.bytes = 123456789;
	it.policy = QStringLiteral("replace");
	it.mobId = QStringLiteral("060A2B34...4321");
	it.masterMobId = QStringLiteral("060A2B34...8765");
	it.clipName = QStringLiteral("A001_C002_0805RM");
	it.renameDst = QStringLiteral("/vol/Avid MediaFiles/MXF/2/clip.mxf");
	it.groupKey = QStringLiteral("group-7");

	VolumeIdentity vol;
	vol.uuid = QStringLiteral("8F2E9B7A-1C3D-4E5F-A6B7-C8D9E0F1A2B3");
	vol.label = QStringLiteral("EDIT 1");
	vol.fsType = QStringLiteral("apfs");
	vol.capacityBytes = Q_INT64_C(4'000'000'000'000);
	vol.rootPath = QStringLiteral("/Volumes/EDIT 1");
	vol.strength = VolumeIdentity::Strength::Full;

	QString path;
	{
		OpJournal journal(OpKind::Rename, {}, tmp.path());
		path = journal.path();
		journal.writePlan(QString(), false, {it}, {vol});
		journal.finish(0, 0, 0);
	}

	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QVERIFY(rec->hasPlan);
	QCOMPARE(rec->plan.size(), 1);
	const OpItem &back = rec->plan[0];
	QCOMPARE(back.src, it.src);
	QCOMPARE(back.name, it.name);
	QCOMPARE(back.folder, it.folder);
	QCOMPARE(back.bytes, it.bytes);
	QCOMPARE(back.policy, it.policy);
	QCOMPARE(back.mobId, it.mobId);
	QCOMPARE(back.masterMobId, it.masterMobId);
	QCOMPARE(back.clipName, it.clipName);
	QCOMPARE(back.renameDst, it.renameDst);
	QCOMPARE(back.groupKey, it.groupKey);

	QCOMPARE(rec->volumes.size(), 1);
	QVERIFY(rec->volumes[0].matches(vol));
	QCOMPARE(rec->volumes[0].rootPath, vol.rootPath);
}

void TestOpJournal::journal_without_plan_reads_has_plan_false()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Copy, {}, tmp.path());
		path = journal.path();
		journal.finish(0, 0, 0);
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QVERIFY(!rec->hasPlan);
	QVERIFY(rec->plan.isEmpty());
}

void TestOpJournal::interrupted_run_has_no_end()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Delete, {}, tmp.path());
		path = journal.path();
		journal.planOp(QStringLiteral("/src/a.mxf"), QString(), 0, QString(), {});
		// No finish(): simulates the process dying. The destructor must
		// leave the file exactly as-is — that half-open state is what
		// recovery looks for.
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QVERIFY(!rec->complete);
	QCOMPARE(rec->ops.size(), 1);
}

void TestOpJournal::delete_final_path_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Delete, {}, tmp.path());
		path = journal.path();
		const int id =
			journal.planOp(QStringLiteral("/vol/clip.mxf"), QString(), 1000, QString(), {});
		OpJournal::DoneInfo info;
		info.finalPath = QStringLiteral("/vol/.Trash/clip.mxf");
		journal.markDone(id, info);
		journal.finish(1, 0, 0);
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QCOMPARE(rec->ops[0].finalPath, QStringLiteral("/vol/.Trash/clip.mxf"));
}

void TestOpJournal::replace_park_and_parked_final_round_trip()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	FileIdentity parkedId = sampleIdentity();
	parkedId.fileId = Q_UINT64_C(0x777);

	QString path;
	{
		OpJournal journal(OpKind::Copy, {}, tmp.path());
		path = journal.path();
		const int id = journal.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"),
									 2048, QStringLiteral("/dst/a.mxf.__copyreplace_x1"),
									 sampleIdentity(), parkedId);
		OpJournal::DoneInfo info;
		info.parkedFinal = QStringLiteral("/dst/_MediaMuster_Trash/a.mxf");
		journal.markDone(id, info);
		journal.finish(1, 0, 0);
	}

	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	// The park path was written ahead of the rename it describes…
	QCOMPARE(rec->ops[0].parked, QStringLiteral("/dst/a.mxf.__copyreplace_x1"));
	// …with the REPLACED file's identity, so undo can restore exactly it…
	QCOMPARE(rec->ops[0].parkedOriginalId.fileId, Q_UINT64_C(0x777));
	// …and the done line says where the replaced original was trashed to.
	QCOMPARE(rec->ops[0].parkedFinal, QStringLiteral("/dst/_MediaMuster_Trash/a.mxf"));
}

void TestOpJournal::zero_bytes_omitted_from_line()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Delete, {}, tmp.path());
		path = journal.path();
		journal.planOp(QStringLiteral("/src/a.mxf"), QString(), 0, QString(), {});
		journal.finish(0, 0, 0);
	}
	for (const QString &line : readLines(path))
		if (line.contains(QStringLiteral("\"rec\":\"op\"")))
			QVERIFY(!line.contains(QStringLiteral("bytes")));
}

void TestOpJournal::begin_line_stamps_schema_two()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Copy, {}, tmp.path());
		path = journal.path();
	}
	const QStringList lines = readLines(path);
	QVERIFY(!lines.isEmpty());
	QVERIFY(lines.first().contains(QStringLiteral("\"schema\":2")));
}

void TestOpJournal::begin_records_owner_pid_host()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Copy, {}, tmp.path());
		path = journal.path();
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QCOMPARE(rec->pid, QCoreApplication::applicationPid());
	QVERIFY(!rec->host.isEmpty());
	QVERIFY(!rec->started.isEmpty());
}

void TestOpJournal::cancelled_run_is_complete_but_flagged()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Move, {}, tmp.path());
		path = journal.path();
		journal.finish(3, 0, 0, /*cancelled=*/true);
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	// Stop-and-keep: a cancelled run is CLEAN for recovery (complete),
	// and the flag is forensics.
	QVERIFY(rec->complete);
	QVERIFY(rec->cancelled);
}

void TestOpJournal::undo_meta_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Undo,
						{{QStringLiteral("undoes"), QStringLiteral("journal-x.jsonl")},
						 {QStringLiteral("effective"), QStringLiteral("move")}},
						tmp.path());
		path = journal.path();
		journal.finish(0, 0, 0);
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QCOMPARE(rec->kind, OpKind::Undo);
	QCOMPARE(rec->undoes, QStringLiteral("journal-x.jsonl"));
	QVERIFY(rec->effective.has_value());
	QCOMPARE(*rec->effective, OpKind::Move);
}

void TestOpJournal::note_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Move, {}, tmp.path());
		path = journal.path();
		journal.writeNote(QStringLiteral("The destination volume couldn't confirm a full flush."));
		journal.finish(1, 0, 0);
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QCOMPARE(rec->notes.size(), 1);
	QVERIFY(rec->notes.first().contains(QStringLiteral("full flush")));
}

void TestOpJournal::weird_paths_stay_one_record()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Newlines, quotes, emoji, NFD accents — everything a facility's
	// clip names throw at a JSON writer must stay one line per record.
	const QString weird =
		QStringLiteral("/vol/Cé \"quoted\"\nnewline \U0001F3AC clip.mxf");
	QString path;
	{
		OpJournal journal(OpKind::Delete, {}, tmp.path());
		path = journal.path();
		const int id = journal.planOp(weird, QString(), 12, QString(), {});
		OpJournal::DoneInfo info;
		info.finalPath = weird + QStringLiteral(".trashed");
		journal.markDone(id, info);
		journal.finish(1, 0, 0);
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QCOMPARE(rec->ops.size(), 1);
	QCOMPARE(rec->ops[0].src, weird);
	QCOMPARE(rec->ops[0].finalPath, weird + QStringLiteral(".trashed"));
}

// MARK: - Reader robustness

void TestOpJournal::torn_final_line_is_tolerated()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Move, {}, tmp.path());
		path = journal.path();
		const int id = journal.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"),
									 100, QString(), {});
		journal.markDone(id);
	}
	// Simulate a crash mid-write: append half a JSON object, no newline.
	{
		QFile f(path);
		QVERIFY(f.open(QIODevice::Append));
		f.write("{\"rec\":\"end\",\"ok\":1,");
	}
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QVERIFY(!rec->complete); // the torn end line never counted
	QVERIFY(rec->ops[0].completed);
}

void TestOpJournal::scan_empty_dir_is_empty()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(OpJournal::scan(tmp.path()).isEmpty());
	// A directory that doesn't exist at all is also just empty.
	QVERIFY(OpJournal::scan(tmp.path() + QStringLiteral("/nope")).isEmpty());
}

void TestOpJournal::scans_every_journal_in_dir()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	{
		OpJournal a(OpKind::Copy, {}, tmp.path());
		// Not finished — otherwise the next constructor would prune it.
	}
	{
		OpJournal b(OpKind::Move, {}, tmp.path());
	}
	const auto records = OpJournal::scan(tmp.path());
	QCOMPARE(records.size(), 2);
	// Oldest first: name sort == chronological.
	QCOMPARE(records[0].kind, OpKind::Copy);
	QCOMPARE(records[1].kind, OpKind::Move);
}

void TestOpJournal::legacy_schema_is_invisible_and_untouched()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// An old-beta journal: schema 1, finished clean. The v2 reader must
	// not interpret it, the pruner must not delete it, and undo must not
	// offer it — ignored, left exactly where it is (Marty's call).
	const QString legacy = writeJournal(
		tmp.path(), QStringLiteral("journal-20250101T000000000-old1.jsonl"),
		{beginLine(QStringLiteral("move"), /*schema=*/1),
		 QStringLiteral(R"({"rec":"op","id":0,"src":"/old/a.mxf","dst":"/old/b.mxf"})"),
		 QStringLiteral(R"({"rec":"done","id":0})"),
		 QStringLiteral(R"({"rec":"end","ok":1,"fail":0,"skip":0})")});
	QVERIFY(!legacy.isEmpty());

	QVERIFY(!OpJournal::readOne(legacy).has_value());
	QVERIFY(OpJournal::scan(tmp.path()).isEmpty());
	QVERIFY(!OpJournal::latestUndoable(tmp.path()).has_value());

	OpJournal::pruneSuperseded(tmp.path());
	QVERIFY(QFile::exists(legacy));

	// Even a new run starting in the same directory leaves it alone.
	{
		OpJournal fresh(OpKind::Copy, {}, tmp.path());
	}
	QVERIFY(QFile::exists(legacy));
}

// MARK: - Markers

void TestOpJournal::recovered_marker_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Move, {}, tmp.path());
		path = journal.path();
	}
	QVERIFY(OpJournal::markRecovered(path, 2, 1));
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QVERIFY(rec->recovered);
}

void TestOpJournal::undone_marker_round_trips()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Move, {}, tmp.path());
		path = journal.path();
		const int id = journal.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"),
									 10, QString(), {});
		journal.markDone(id);
		journal.finish(1, 0, 0);
	}
	QVERIFY(OpJournal::markUndone(path, QStringLiteral("journal-undo-run.jsonl")));
	const auto rec = OpJournal::readOne(path);
	QVERIFY(rec.has_value());
	QVERIFY(rec->undone);
	// Spent is spent: an undone journal is no longer an undo candidate.
	QVERIFY(!OpJournal::latestUndoable(tmp.path()).has_value());
}

// MARK: - Retention

void TestOpJournal::finished_journal_survives_finish()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Move, {}, tmp.path());
		path = journal.path();
		const int id = journal.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"),
									 10, QString(), {});
		journal.markDone(id);
		journal.finish(1, 0, 0);
	}
	// The v2 change of heart: a clean finish KEEPS the journal — it is
	// the undo candidate. (v1 pruned it here, which is why undo never
	// had anything to act on.)
	QVERIFY(QFile::exists(path));
	const auto undoable = OpJournal::latestUndoable(tmp.path());
	QVERIFY(undoable.has_value());
	QCOMPARE(undoable->path, path);
}

void TestOpJournal::next_run_prunes_superseded_journals()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString first;
	{
		OpJournal a(OpKind::Move, {}, tmp.path());
		first = a.path();
		const int id =
			a.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"), 10, QString(), {});
		a.markDone(id);
		a.finish(1, 0, 0);
	}
	QVERIFY(QFile::exists(first));

	// The moment a new operation begins, the previous run's undo
	// candidate is superseded — pruned by the new constructor, before
	// the new run writes its first line.
	QString second;
	{
		OpJournal b(OpKind::Copy, {}, tmp.path());
		second = b.path();
		QVERIFY(b.isOpen());
		QVERIFY(!QFile::exists(first));
	}
	QVERIFY(QFile::exists(second));
}

void TestOpJournal::interrupted_and_dirty_journals_survive_pruning()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// An interrupted run (no end line) is recovery's business.
	QString interrupted;
	{
		OpJournal a(OpKind::Move, {}, tmp.path());
		interrupted = a.path();
		a.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"), 10, QString(), {});
	}

	// A finished-but-dirty run holds the only record of a stranded park.
	QString dirty;
	{
		OpJournal b(OpKind::Copy, {}, tmp.path());
		dirty = b.path();
		const int id = b.planOp(QStringLiteral("/src/b.mxf"), QStringLiteral("/dst/b.mxf"), 10,
								QStringLiteral("/dst/b.mxf.__copyreplace_z9"), {});
		b.markFailed(id, QStringLiteral("restore failed"), /*rollbackIncomplete=*/true);
		b.finish(0, 1, 0);
	}

	OpJournal::pruneSuperseded(tmp.path());
	QVERIFY(QFile::exists(interrupted));
	QVERIFY(QFile::exists(dirty));

	// Dirty is also not an undo candidate — recovery owns it.
	QVERIFY(!OpJournal::latestUndoable(tmp.path()).has_value());
}

void TestOpJournal::degraded_journal_self_destructs_on_finish_even_when_dirty()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString path;
	{
		OpJournal journal(OpKind::Move, {}, tmp.path());
		path = journal.path();
		const int id = journal.planOp(QStringLiteral("/src/a.mxf"), QStringLiteral("/dst/a.mxf"),
									 10, QStringLiteral("/dst/a.mxf.__movereplace_q2"), {});
		journal.markFailed(id, QStringLiteral("restore failed"), /*rollbackIncomplete=*/true);
		// A line write failed mid-run: with lines missing, the file no
		// longer tells the truth, and recovery reading it could roll
		// back work that finished. Degraded overrides dirty.
		journal.debugForceWriteFailure();
		QVERIFY(journal.degraded());
		journal.finish(0, 1, 0);
	}
	QVERIFY(!QFile::exists(path));
}

// MARK: - Undo candidacy

void TestOpJournal::latest_undoable_picks_newest_qualifying()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString opLine =
		QStringLiteral(R"({"rec":"op","id":0,"src":"/v/a.mxf","dst":"/w/a.mxf"})");
	const QString doneLine = QStringLiteral(R"({"rec":"done","id":0})");
	const QString endLine = QStringLiteral(R"({"rec":"end","ok":1,"fail":0,"skip":0})");
	const QString endCancelled =
		QStringLiteral(R"({"rec":"end","ok":1,"fail":0,"skip":0,"cancelled":true})");

	writeJournal(tmp.path(), QStringLiteral("journal-20260101T000001000-a1.jsonl"),
				{beginLine(QStringLiteral("move")), opLine, doneLine, endLine});
	// Newest, CANCELLED — still qualifies: stop-and-keep means the file
	// it landed is real work the user may want back.
	const QString newest =
		writeJournal(tmp.path(), QStringLiteral("journal-20260101T000002000-b2.jsonl"),
					{beginLine(QStringLiteral("copy")), opLine, doneLine, endCancelled});

	const auto undoable = OpJournal::latestUndoable(tmp.path());
	QVERIFY(undoable.has_value());
	QCOMPARE(undoable->path, newest);
	QCOMPARE(undoable->kind, OpKind::Copy);
}

void TestOpJournal::latest_undoable_refuses_disqualified()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString opLine =
		QStringLiteral(R"({"rec":"op","id":0,"src":"/v/a.mxf","dst":"/w/a.mxf"})");
	const QString endLine = QStringLiteral(R"({"rec":"end","ok":0,"fail":0,"skip":1})");

	// Interrupted (no end line): recovery's, not undo's.
	writeJournal(tmp.path(), QStringLiteral("journal-20260101T000001000-c1.jsonl"),
				{beginLine(QStringLiteral("move")), opLine,
				 QStringLiteral(R"({"rec":"done","id":0})")});
	// Finished but nothing landed (all skipped): nothing to reverse.
	writeJournal(tmp.path(), QStringLiteral("journal-20260101T000002000-c2.jsonl"),
				{beginLine(QStringLiteral("move")), opLine,
				 QStringLiteral(R"({"rec":"skip","id":0})"), endLine});
	// An undo run itself: no undo-of-undo, by design.
	writeJournal(tmp.path(), QStringLiteral("journal-20260101T000003000-c3.jsonl"),
				{beginLine(QStringLiteral("undo")), opLine,
				 QStringLiteral(R"({"rec":"done","id":0})"),
				 QStringLiteral(R"({"rec":"end","ok":1,"fail":0,"skip":0})")});
	// Swept by recovery already.
	writeJournal(tmp.path(), QStringLiteral("journal-20260101T000004000-c4.jsonl"),
				{beginLine(QStringLiteral("move")), opLine,
				 QStringLiteral(R"({"rec":"done","id":0})"),
				 QStringLiteral(R"({"rec":"end","ok":1,"fail":0,"skip":0})"),
				 QStringLiteral(R"({"rec":"recovered","reversed":1,"failed":0})")});

	QVERIFY(!OpJournal::latestUndoable(tmp.path()).has_value());
}

// MARK: - Locations

void TestOpJournal::standardDirWritable_reflects_env_dir()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	qputenv("MEDIAMUSTER_JOURNAL_DIR", tmp.path().toUtf8());
	QCOMPARE(OpJournal::standardJournalDir(), tmp.path());
	QVERIFY(OpJournal::standardDirWritable());
	qunsetenv("MEDIAMUSTER_JOURNAL_DIR");
}

QTEST_APPLESS_MAIN(TestOpJournal)
#include "tst_opjournal.moc"
