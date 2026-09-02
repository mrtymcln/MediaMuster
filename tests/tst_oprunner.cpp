#include "conventions.h"
#include "opverify.h"
#include "mobid.h"
#include "mxfparser.h"
#include "opcopier.h"
#include "oprunner.h"
#include "parkedfile.h"

#include "testutil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// OpRunner — the engine v2 state machines, driven SYNCHRONOUSLY through
// a test sink (no threads, no signals: the runner is plain C++ on
// purpose, and this suite is the payoff). Re-pins the v1 engine's
// behaviour contract — conflict rules, park-aside Replace, keep-both,
// claim collisions, trash tiers — and pins the v2 additions: identity
// gates, replaced-originals-to-trash, journal retention, and the Rename
// machine. Mid-flight behaviours (cancel, mutation during a copy) live
// in tst_opmanager, which drives the threaded facade.

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
		QString trashFolder;
		int trashCount = 0;

		void progress(const QString &, int, int, double) override {}
		void itemDone(const QString &name, const QString &path, bool ok, const QString &error,
					  bool skipped) override
		{
			items.append({name, path, error, ok, skipped});
		}
		void log(QtMsgType, const QString &message) override { logs << message; }
		void trashUsed(const QString &folder, int count) override
		{
			trashFolder = folder;
			trashCount = count;
		}
	};

	const std::atomic<bool> kNoCancel{false};

	OpItem makeItem(const QString &src, qint64 bytes, const QString &policy = QString())
	{
		OpItem it;
		it.src = src;
		it.name = QFileInfo(src).fileName();
		it.bytes = bytes;
		it.policy = policy;
		return it;
	}
} // namespace

class TestOpRunner : public QObject
{
	Q_OBJECT
private slots:
	void cleanup();

	// MARK: - Path helpers
	void buildDestPath_preserve_true_uses_avid_structure();
	void buildDestPath_preserve_false_flattens();
	void buildDestPath_omf_row_preserves_to_omfi_root();
	void generateRenamePath_first_slot_when_no_copies();
	void generateRenamePath_skips_existing_slots();
	void generateRenamePath_returns_nullopt_when_exhausted();
	void generateRenamePath_handles_extensionless_path();

	// MARK: - Copy
	void copy_writes_file_and_leaves_source();
	void copy_skip_policy_leaves_existing_destination();
	void copy_replace_sends_replaced_original_to_trash();
	void copy_keepboth_policy_keeps_both();
	void copy_flatten_sameName_keepsBothFiles();
	void copy_flatten_sameName_keepsBothFiles_throughASymlinkedDestination();
	void copy_flatten_caseVariantNames_noSilentOverwrite();
	void copy_conflictNotInPolicyMap_isSkippedNotReplaced();
	void copy_verifyOff_stillSucceeds();

	// MARK: - Identity gates
	void copy_refuses_source_with_wrong_size();
	void copy_refuses_source_with_wrong_umid();
	void copy_accepts_database_byte_order_mob_claims();
	void copy_missing_source_fails_cleanly();
	void delete_refuses_identity_mismatch();

	// MARK: - Move
	void move_happy_sameVolume_removesSource();
	void move_replace_sends_replaced_original_to_trash();
	void move_flatten_caseVariantNames_nothingLost();
	void move_conflictNotInPolicyMap_isSkippedNotReplaced();
	void move_copyLeg_movesFileAndRemovesSource();
	void move_copyLeg_replace_trashesParkedAndRemovesSource();

	// MARK: - Delete
	void delete_moves_file_to_trash_and_records_final();
	void delete_fallbackTrash_neverOverwritesPriorCatch();

	// MARK: - Ownership guards (adversarial review 2026-08-30)
	void parkedfile_never_deletes_a_file_it_did_not_write();
	void parkedfile_discards_only_its_own_write();
	void parkedfile_disarm_freezes_disk_state();
	void parkedfile_reports_a_partial_it_could_not_delete();
	void copier_racer_at_destination_survives();

	// MARK: - Rename
	void rename_moves_file_and_fires_folder_hook();
	void rename_refuses_occupied_destination();
	void rename_precancelled_runs_nothing();

	// MARK: - Journal integration
	void journal_survives_run_and_plan_precedes_ops();
	void plan_records_volume_fingerprints();

private:
	QString stageFixtureMxf(const QString &dir);

	OpRunner::Totals runRequest(const OpRequest &req, TestSink &sink, const QString &journalDir);
};

void TestOpRunner::cleanup()
{
	qunsetenv("MEDIAMUSTER_DISABLE_CLONEFILE");
	qunsetenv("MEDIAMUSTER_DISABLE_COPYFILEEX");
	qunsetenv("MEDIAMUSTER_FORCE_MOVE_COPY");
	qunsetenv("MEDIAMUSTER_DISABLE_OS_TRASH");
	qunsetenv("MEDIAMUSTER_TRASH_ROOT");
	// Verification is a process-wide toggle; a test that turns it off
	// must not leak that state into the next test.
	OpVerify::setEnabled(true);
}

QString TestOpRunner::stageFixtureMxf(const QString &dir)
{
	// A real Avid header slice, so the content-identity half is live in
	// these runs exactly as it will be in production.
	const QString fixture =
		QStringLiteral(FIXTURES_DIR "/avid_headers/V01.E683C412_F82F4F82F461DV.mxf");
	const QString staged = dir + QStringLiteral("/clip.mxf");
	QDir().mkpath(dir);
	if (!QFile::copy(fixture, staged))
		return {};
	return staged;
}

OpRunner::Totals TestOpRunner::runRequest(const OpRequest &req, TestSink &sink,
										  const QString &journalDir)
{
	OpRunner runner(sink, kNoCancel);
	const auto totals = runner.run(req, journalDir);
	// Surface per-item failures in the test log — a bare "succeeded==0"
	// assertion failure is undebuggable without the sentence the runner
	// actually produced.
	for (const SinkItem &item : sink.items)
		if (!item.ok)
			qWarning() << "item failed:" << item.name << item.error;
	return totals;
}

// MARK: - Path helpers

void TestOpRunner::buildDestPath_preserve_true_uses_avid_structure()
{
	QCOMPARE(OpRunner::buildDestPath(QStringLiteral("clip.mxf"), QStringLiteral("5"),
									 QStringLiteral("/dest"), true),
			 QStringLiteral("/dest/Avid MediaFiles/MXF/5/clip.mxf"));
}

void TestOpRunner::buildDestPath_preserve_false_flattens()
{
	QCOMPARE(OpRunner::buildDestPath(QStringLiteral("clip.mxf"), QStringLiteral("5"),
									 QStringLiteral("/dest"), false),
			 QStringLiteral("/dest/clip.mxf"));
}

void TestOpRunner::buildDestPath_omf_row_preserves_to_omfi_root()
{
	// OMF-era: an OMF row's mxfFolder is the flat "OMFI MediaFiles" root,
	// so preserve must rebuild that folder beside Avid MediaFiles — never
	// "Avid MediaFiles/MXF/OMFI MediaFiles/". Any spelling, like the
	// scanner accepts; flatten is unchanged.
	QCOMPARE(OpRunner::buildDestPath(QStringLiteral("slate.omf"), Conventions::kOmfMediaFilesDir,
									 QStringLiteral("/dest"), true),
			 QStringLiteral("/dest/OMFI MediaFiles/slate.omf"));
	// A lowercase source spelling still lands in Avid's own spelling.
	QCOMPARE(OpRunner::buildDestPath(QStringLiteral("tone.wav"), QStringLiteral("omfi mediafiles"),
									 QStringLiteral("/dest"), true),
			 QStringLiteral("/dest/OMFI MediaFiles/tone.wav"));
	QCOMPARE(OpRunner::buildDestPath(QStringLiteral("slate.omf"), Conventions::kOmfMediaFilesDir,
									 QStringLiteral("/dest"), false),
			 QStringLiteral("/dest/slate.omf"));
}

void TestOpRunner::generateRenamePath_first_slot_when_no_copies()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dest = tmp.path() + "/clip.mxf";
	writeFile(dest, "x");
	QCOMPARE(OpRunner::generateRenamePath(dest).value_or(QString()),
			 tmp.path() + QStringLiteral("/clip (2).mxf"));
}

void TestOpRunner::generateRenamePath_skips_existing_slots()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dest = tmp.path() + "/clip.mxf";
	writeFile(dest, "x");
	writeFile(tmp.path() + "/clip (2).mxf", "x");
	writeFile(tmp.path() + "/clip (3).mxf", "x");
	QCOMPARE(OpRunner::generateRenamePath(dest).value_or(QString()),
			 tmp.path() + QStringLiteral("/clip (4).mxf"));
}

void TestOpRunner::generateRenamePath_returns_nullopt_when_exhausted()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dest = tmp.path() + "/clip.mxf";
	writeFile(dest, "x");
	for (int n = 1; n <= 999; ++n)
		writeFile(tmp.path() +
					  QStringLiteral("/clip (%1).mxf").arg(n),
				  "x");
	QVERIFY(!OpRunner::generateRenamePath(dest).has_value());
}

void TestOpRunner::generateRenamePath_handles_extensionless_path()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dest = tmp.path() + "/CLIPFILE";
	writeFile(dest, "x");
	QCOMPARE(OpRunner::generateRenamePath(dest).value_or(QString()),
			 tmp.path() + QStringLiteral("/CLIPFILE (2)"));
}

// MARK: - Copy

void TestOpRunner::copy_writes_file_and_leaves_source()
{
	// Force the buffered path: an APFS clone records no hash (nothing
	// was rewritten), and this test pins the hash-in-journal behaviour.
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = stageFixtureMxf(tmp.path() + "/src");
	QVERIFY(!src.isEmpty());
	const QString dest = tmp.path() + "/dest";
	QDir().mkpath(dest);
	const QByteArray original = readFile(src);
	const QString umid = MxfParser::parseHeader(src).umid;
	QVERIFY(!umid.isEmpty());

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	OpItem it = makeItem(src, original.size());
	it.masterMobId = umid; // the scan's claim, correct — the gate must pass
	it.clipName = QStringLiteral("A001_C002");
	req.items = {it};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(totals.failed, 0);

	QCOMPARE(readFile(dest + "/clip.mxf"), original);
	QVERIFY2(QFile::exists(src), "copy must leave the source in place");
	QCOMPARE(sink.items.size(), 1);
	QVERIFY(sink.items[0].ok);
	QCOMPARE(sink.items[0].path, src);

	// The journal recorded the full story: source identity on the op
	// line, hash + landed identity on the done line.
	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(recs.size(), 1);
	QCOMPARE(recs[0].ops.size(), 1);
	QCOMPARE(recs[0].ops[0].srcId.contentUmid, umid);
	QVERIFY(recs[0].ops[0].completed);
	QCOMPARE(recs[0].ops[0].landedId.contentUmid, umid);
	QVERIFY2(!recs[0].ops[0].hash.isEmpty(), "a verified buffered copy must record its hash");
	QCOMPARE(recs[0].plan.size(), 1);
	QCOMPARE(recs[0].plan[0].clipName, QStringLiteral("A001_C002"));
}

void TestOpRunner::copy_skip_policy_leaves_existing_destination()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(src, 3, QStringLiteral("skip"))};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.skipped, 1);
	QCOMPARE(totals.succeeded, 0);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));
	// A skip is success=true + skipped=true — anything else and the
	// UI's row pruning would misfire.
	QCOMPARE(sink.items.size(), 1);
	QVERIFY(sink.items[0].ok);
	QVERIFY(sink.items[0].skipped);

	// And the journal says the file was CONCLUDED, or a resume would
	// offer it again.
	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(recs.size(), 1);
	QCOMPARE(recs[0].ops.size(), 1);
	QVERIFY(recs[0].ops[0].skipped);
}

void TestOpRunner::copy_replace_sends_replaced_original_to_trash()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");
	// The seam stands in for the volume root, so it must be an ancestor
	// of the media — exactly like the real volume root is.
	qputenv("MEDIAMUSTER_TRASH_ROOT", tmp.path().toUtf8());

	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW CONTENT");
	writeFile(dest + "/clip.mxf", "PRECIOUS ORIGINAL");

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(src, 11, QStringLiteral("replace"))};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("NEW CONTENT"));
	QVERIFY2(QDir(dest).entryList({"*__copyreplace*"}, QDir::Files).isEmpty(),
			 "the parked temp must not be left behind");

	// THE v2 change of heart: the replaced original is not hard-deleted
	// (v1's ParkedFile::commit did exactly that) — it went to the trash,
	// and the journal says where, so undo can bring it back.
	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(recs.size(), 1);
	const QString parkedFinal = recs[0].ops[0].parkedFinal;
	QVERIFY2(!parkedFinal.isEmpty(), "the done line must record where the original went");
	QCOMPARE(readFile(parkedFinal), QByteArray("PRECIOUS ORIGINAL"));
	// The op line captured the replaced file's identity before parking.
	QCOMPARE(recs[0].ops[0].parkedOriginalId.size, qint64(17));
}

void TestOpRunner::copy_keepboth_policy_keeps_both()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "OLD");

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(src, 3, QStringLiteral("keepboth"))};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("OLD"));
	QCOMPARE(readFile(dest + "/clip (2).mxf"), QByteArray("NEW"));
}

void TestOpRunner::copy_flatten_sameName_keepsBothFiles()
{
	// Two selected files from different folders, same leaf name,
	// flattened into one destination: the second must divert to a
	// " (2)" sibling, never silently overwrite the first.
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString srcA = tmp.path() + "/1/clip.mxf";
	const QString srcB = tmp.path() + "/2/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(srcA, "FIRST");
	writeFile(srcB, "SECOND");
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(srcA, 5), makeItem(srcB, 6)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 2);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("FIRST"));
	QCOMPARE(readFile(dest + "/clip (2).mxf"), QByteArray("SECOND"));
}

// The same promise, with a symlink somewhere in the destination path —
// "/tmp" -> "/private/tmp", or a user's own symlink to a project drive.
// The claimed-destination key used to be computed from the canonical path
// once the first file existed and from the plain absolute path before it
// did; through a symlink those two disagree, the lookup missed, and the
// second file was reported "a file appeared at this destination after the
// preview" and silently skipped. The preview had already promised the user
// "clip (2).mxf".
void TestOpRunner::copy_flatten_sameName_keepsBothFiles_throughASymlinkedDestination()
{
#ifdef Q_OS_WIN
	QSKIP("Windows has no POSIX symlink to build the fixture from; "
		  "tst_pathkey pins the key invariant on every platform.");
#else
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString srcA = tmp.path() + "/1/clip.mxf";
	const QString srcB = tmp.path() + "/2/clip.mxf";
	const QString realDest = tmp.path() + "/dest";
	const QString linkedDest = tmp.path() + "/via-link";
	writeFile(srcA, "FIRST");
	writeFile(srcB, "SECOND");
	QVERIFY(QDir().mkpath(realDest));
	QVERIFY2(QFile::link(realDest, linkedDest), "could not create the symlink fixture");

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = linkedDest; // the user picked the symlinked spelling
	req.items = {makeItem(srcA, 5), makeItem(srcB, 6)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());

	QCOMPARE(totals.skipped, 0);
	QCOMPARE(totals.succeeded, 2);
	QCOMPARE(readFile(realDest + "/clip.mxf"), QByteArray("FIRST"));
	QCOMPARE(readFile(realDest + "/clip (2).mxf"), QByteArray("SECOND"));
#endif
}

void TestOpRunner::copy_flatten_caseVariantNames_noSilentOverwrite()
{
	// On the case-insensitive filesystems macOS and Windows default to,
	// "CLIP.mxf" and "clip.mxf" are the same destination even though
	// every string comparison says otherwise. Both files must survive.
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString srcA = tmp.path() + "/1/CLIP.mxf";
	const QString srcB = tmp.path() + "/2/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(srcA, "UPPER");
	writeFile(srcB, "lower");
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(srcA, 5), makeItem(srcB, 5)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());

	// Both byte streams must exist at the destination, whatever names
	// they ended up under.
	const QStringList landed = QDir(dest).entryList(QDir::Files);
	QByteArray all;
	for (const QString &name : landed)
		all += readFile(dest + QLatin1Char('/') + name);
	QVERIFY2(all.contains("UPPER") && all.contains("lower"),
			 qPrintable(QStringLiteral("expected both files to survive, got: %1 (%2 ok)")
							.arg(landed.join(QStringLiteral(", ")))
							.arg(totals.succeeded)));
}

void TestOpRunner::copy_conflictNotInPolicyMap_isSkippedNotReplaced()
{
	// A conflict the dialog never showed the user must NEVER fall
	// through to Replace (the NTFS case-variant incident).
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "FOREIGN");

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(src, 3)}; // no policy on purpose

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.skipped, 1);
	QCOMPARE(totals.failed, 0);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("FOREIGN"));
	QVERIFY(sink.items[0].skipped);
}

void TestOpRunner::copy_verifyOff_stillSucceeds()
{
	OpVerify::setEnabled(false);
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, QByteArray(1024, 'V'));
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(src, 1024)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray(1024, 'V'));
}

// MARK: - Identity gates

void TestOpRunner::copy_refuses_source_with_wrong_size()
{
	// The scan said 100 bytes; the file on disk is 3. Whatever is at
	// that path now, it is not what the user selected: refuse, explain,
	// touch nothing.
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	OpItem it = makeItem(src, 100);
	it.clipName = QStringLiteral("A001_C002");
	req.items = {it};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.failed, 1);
	QCOMPARE(totals.succeeded, 0);
	QVERIFY(!QFile::exists(dest + "/clip.mxf"));
	QCOMPARE(sink.items.size(), 1);
	QVERIFY(!sink.items[0].ok);
	// The refusal names the clip and the difference, in plain words.
	QVERIFY(sink.items[0].error.contains(QStringLiteral("A001_C002")));
	QVERIFY(sink.items[0].error.contains(QStringLiteral("size")));
}

void TestOpRunner::copy_refuses_source_with_wrong_umid()
{
	// Size matches, but the Avid ID inside the file belongs to a
	// different clip than the scan recorded — the same-size-swap case
	// only content identity can catch.
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = stageFixtureMxf(tmp.path() + "/src");
	QVERIFY(!src.isEmpty());
	const QString dest = tmp.path() + "/dest";
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	OpItem it = makeItem(src, QFileInfo(src).size());
	// A well-formed claim that simply isn't this file's.
	it.mobId = QStringLiteral(
		"060a2b3401010105.01010f1013000000.dead0000dead0000.dead0000dead0000");
	it.masterMobId = it.mobId;
	req.items = {it};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.failed, 1);
	QVERIFY(!QFile::exists(dest + "/clip.mxf"));
	QVERIFY(sink.items[0].error.contains(QStringLiteral("Avid media ID")));
}

// The scan's mob-id claims come from Avid's DATABASES (PMR/MDB), whose
// byte order for the ID's middle fields differs from the MXF header's —
// same identity, two hex spellings, bridged by MobId::toPmrForm. The
// gate must accept a database-order claim for a file whose header holds
// the MXF-order form; on 2026-08-30 the raw-string comparison refused
// every healthy database-described file on Marty's real media.
void TestOpRunner::copy_accepts_database_byte_order_mob_claims()
{
	// The exact pair from the real failure, pinned as data: the MDB's
	// spelling of a master mob vs the file header's spelling.
	QCOMPARE(
		MobId::toPmrForm(QStringLiteral(
			"060a2b3401010105.01010f1013000000.1cddd67e12559006.24da46fc807fda72")),
		QStringLiteral("060a2b3401010105.01010f1013000000.7ed6dd1c55120690.24da46fc807fda72"));

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString fixture =
		QStringLiteral(FIXTURES_DIR "/avid_headers/A01.E683C413_F82F4F82F4625A.mxf");
	const QString src = tmp.path() + "/src/clip.mxf";
	QDir().mkpath(tmp.path() + "/src");
	QVERIFY(QFile::copy(fixture, src));

	// Claim the file's identity the way the SCAN would state it: the
	// header UMID re-encoded into database byte order.
	const QString headerUmid = MxfParser::parseHeader(src).umid;
	QVERIFY(!headerUmid.isEmpty());
	OpItem it;
	it.src = src;
	it.name = QStringLiteral("clip.mxf");
	it.bytes = QFileInfo(src).size();
	it.mobId = MobId::toPmrForm(headerUmid);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = tmp.path() + "/dest";
	req.items = {it};

	TestSink sink;
	std::atomic<bool> cancel{false};
	OpRunner runner(sink, cancel);
	const OpRunner::Totals t = runner.run(req, tmp.path() + "/journal");

	QVERIFY2(t.succeeded == 1,
			 qPrintable(sink.items.isEmpty() ? QString() : sink.items.first().error));
	QVERIFY(QFile::exists(tmp.path() + "/dest/clip.mxf"));
}

void TestOpRunner::copy_missing_source_fails_cleanly()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString dest = tmp.path() + "/dest";
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(tmp.path() + "/src/gone.mxf", 100)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.failed, 1);
	QVERIFY(sink.items[0].error.contains(QStringLiteral("no longer")));
}

void TestOpRunner::delete_refuses_identity_mismatch()
{
	// The most important gate of all: a delete pointed at a file that
	// is not what the scan recorded must refuse — this is the "never
	// guess what file is being operated on" rule at the sharpest edge.
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");
	qputenv("MEDIAMUSTER_TRASH_ROOT", tmp.path().toUtf8());
	const QString src = tmp.path() + "/media/clip.mxf";
	writeFile(src, "REPLACED CONTENT!");

	OpRequest req;
	req.kind = OpKind::Delete;
	req.items = {makeItem(src, 4)}; // scan said 4 bytes; disk says 17

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.failed, 1);
	QCOMPARE(totals.succeeded, 0);
	QVERIFY2(QFile::exists(src), "a refused delete must leave the file exactly where it was");
}

// MARK: - Move

void TestOpRunner::move_happy_sameVolume_removesSource()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "MOVING");
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = dest;
	req.items = {makeItem(src, 6)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("MOVING"));
	QVERIFY2(!QFile::exists(src), "a move must remove its source");

	// The journal's done line captured where (and what) landed.
	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(recs.size(), 1);
	QVERIFY(recs[0].ops[0].completed);
	QCOMPARE(recs[0].ops[0].landedId.size, qint64(6));
}

void TestOpRunner::move_replace_sends_replaced_original_to_trash()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");
	// The seam stands in for the volume root, so it must be an ancestor
	// of the media — exactly like the real volume root is.
	qputenv("MEDIAMUSTER_TRASH_ROOT", tmp.path().toUtf8());

	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW CONTENT");
	writeFile(dest + "/clip.mxf", "PRECIOUS ORIGINAL");

	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = dest;
	req.items = {makeItem(src, 11, QStringLiteral("replace"))};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("NEW CONTENT"));
	QVERIFY(!QFile::exists(src));
	QVERIFY(QDir(dest).entryList({"*__movereplace*"}, QDir::Files).isEmpty());

	const auto recs = OpJournal::scan(journalDir.path());
	const QString parkedFinal = recs[0].ops[0].parkedFinal;
	QVERIFY2(!parkedFinal.isEmpty(), "the replaced original's trash address must be journaled");
	QCOMPARE(readFile(parkedFinal), QByteArray("PRECIOUS ORIGINAL"));
}

void TestOpRunner::move_flatten_caseVariantNames_nothingLost()
{
	// The stakes are higher than Copy's variant: a silently overwritten
	// moved file is GONE (its source was already removed). Nothing may
	// be lost, whatever names things land under.
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString srcA = tmp.path() + "/1/CLIP.mxf";
	const QString srcB = tmp.path() + "/2/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(srcA, "UPPER");
	writeFile(srcB, "lower");
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = dest;
	req.items = {makeItem(srcA, 5), makeItem(srcB, 5)};

	TestSink sink;
	runRequest(req, sink, journalDir.path());

	// Count every surviving byte stream across sources and destination:
	// both must exist somewhere.
	QByteArray all;
	for (const QString &name : QDir(dest).entryList(QDir::Files))
		all += readFile(dest + QLatin1Char('/') + name);
	all += readFile(srcA);
	all += readFile(srcB);
	QVERIFY2(all.contains("UPPER") && all.contains("lower"),
			 "a case-variant flatten must never lose a moved file");
}

void TestOpRunner::move_conflictNotInPolicyMap_isSkippedNotReplaced()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW");
	writeFile(dest + "/clip.mxf", "FOREIGN");

	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = dest;
	req.items = {makeItem(src, 3)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.skipped, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("FOREIGN"));
	QVERIFY2(QFile::exists(src), "a skipped move must leave its source alone");
}

void TestOpRunner::move_copyLeg_movesFileAndRemovesSource()
{
	// Every QTemporaryDir sits on one filesystem, so the cross-volume
	// leg is unreachable without the seam; the clone seam forces the
	// hashing loop this test pins.
	qputenv("MEDIAMUSTER_FORCE_MOVE_COPY", "1");
	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, QByteArray(1024, 'M'));
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = dest;
	req.items = {makeItem(src, 1024)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray(1024, 'M'));
	QVERIFY(!QFile::exists(src));

	// The copy leg hashes; the journal keeps the evidence.
	const auto recs = OpJournal::scan(journalDir.path());
	QVERIFY(!recs[0].ops[0].hash.isEmpty());
}

void TestOpRunner::move_copyLeg_replace_trashesParkedAndRemovesSource()
{
	qputenv("MEDIAMUSTER_FORCE_MOVE_COPY", "1");
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");
	// The seam stands in for the volume root, so it must be an ancestor
	// of the media — exactly like the real volume root is.
	qputenv("MEDIAMUSTER_TRASH_ROOT", tmp.path().toUtf8());

	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "NEW CONTENT");
	writeFile(dest + "/clip.mxf", "PRECIOUS ORIGINAL");

	OpRequest req;
	req.kind = OpKind::Move;
	req.destRoot = dest;
	req.items = {makeItem(src, 11, QStringLiteral("replace"))};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dest + "/clip.mxf"), QByteArray("NEW CONTENT"));
	QVERIFY(!QFile::exists(src));

	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(readFile(recs[0].ops[0].parkedFinal), QByteArray("PRECIOUS ORIGINAL"));
}

// MARK: - Delete

void TestOpRunner::delete_moves_file_to_trash_and_records_final()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");
	qputenv("MEDIAMUSTER_TRASH_ROOT", tmp.path().toUtf8());

	const QString src = tmp.path() + "/media/clip.mxf";
	writeFile(src, "DELETED CONTENT");

	OpRequest req;
	req.kind = OpKind::Delete;
	req.items = {makeItem(src, 15)};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.succeeded, 1);
	QVERIFY2(!QFile::exists(src), "the file must leave its original location");

	// Never a hard delete: the bytes are in the MediaMuster Trash, at
	// the address the journal recorded.
	const auto recs = OpJournal::scan(journalDir.path());
	const QString finalPath = recs[0].ops[0].finalPath;
	QVERIFY(finalPath.contains(QStringLiteral("_MediaMuster_Trash")));
	QCOMPARE(readFile(finalPath), QByteArray("DELETED CONTENT"));
	// And the post-op dialog knows files went to the app trash.
	QCOMPARE(sink.trashCount, 1);
}

void TestOpRunner::delete_fallbackTrash_neverOverwritesPriorCatch()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	qputenv("MEDIAMUSTER_DISABLE_OS_TRASH", "1");
	qputenv("MEDIAMUSTER_TRASH_ROOT", tmp.path().toUtf8());

	// Delete the same path twice: the second catch must divert to a
	// " (2)" sibling, never destroy the first (an earlier delete's
	// safety copy).
	const QString src = tmp.path() + "/media/clip.mxf";

	writeFile(src, "FIRST CATCH");
	OpRequest req1;
	req1.kind = OpKind::Delete;
	req1.items = {makeItem(src, 11)};
	TestSink sink1;
	QCOMPARE(runRequest(req1, sink1, journalDir.path()).succeeded, 1);

	writeFile(src, "SECOND CATCH");
	OpRequest req2;
	req2.kind = OpKind::Delete;
	req2.items = {makeItem(src, 12)};
	TestSink sink2;
	QCOMPARE(runRequest(req2, sink2, journalDir.path()).succeeded, 1);

	// Both catches survive, under distinct names.
	const QString binDir = tmp.path() + QStringLiteral("/_MediaMuster_Trash/media");
	const QStringList catches = QDir(binDir).entryList(QDir::Files);
	QCOMPARE(catches.size(), 2);
	QByteArray all;
	for (const QString &name : catches)
		all += readFile(binDir + QLatin1Char('/') + name);
	QVERIFY(all.contains("FIRST CATCH") && all.contains("SECOND CATCH"));
}

// MARK: - Ownership guards (adversarial review 2026-08-30)

// The rule under test (findings 1, 3, 7): restore() may only delete the
// destination when the engine itself wrote it — proven by
// noteDestinationWritten(). A file that appeared there any other way
// (another Avid client, the user in Finder, the user's own moved file
// after a failed rollback) survives, and the parked original stays put,
// reported as stranded.
void TestOpRunner::parkedfile_never_deletes_a_file_it_did_not_write()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dst = tmp.path() + "/clip.mxf";
	writeFile(dst, "ORIGINAL");

	QString parkedPath;
	{
		ParkedFile park(dst, Conventions::kCopyReplaceTag);
		QVERIFY(park.park());
		parkedPath = park.path();

		// A racer lands at the vacated slot; the engine never wrote it.
		writeFile(dst, "RACER");

		QVERIFY2(!park.restore(), "restore into an occupied, not-ours slot must refuse");
		QVERIFY(park.isStranded());
		QCOMPARE(readFile(dst), QByteArray("RACER"));
		QCOMPARE(readFile(parkedPath), QByteArray("ORIGINAL"));

		// The caller's contract after a stranded flag: disarm so the
		// destructor can't change disk after the journal's last word.
		park.disarm();
	}
	// Destructor ran; nothing may have changed.
	QCOMPARE(readFile(dst), QByteArray("RACER"));
	QCOMPARE(readFile(parkedPath), QByteArray("ORIGINAL"));
}

void TestOpRunner::parkedfile_discards_only_its_own_write()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dst = tmp.path() + "/clip.mxf";
	writeFile(dst, "ORIGINAL");

	ParkedFile park(dst, Conventions::kCopyReplaceTag);
	QVERIFY(park.park());
	writeFile(dst, "OUR-PARTIAL");
	park.noteDestinationWritten();

	QVERIFY(park.restore());
	QCOMPARE(readFile(dst), QByteArray("ORIGINAL")); // partial gone, original home
	QVERIFY(!QFile::exists(park.path()));
}

void TestOpRunner::parkedfile_disarm_freezes_disk_state()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dst = tmp.path() + "/clip.mxf";
	writeFile(dst, "ORIGINAL");

	QString parkedPath;
	{
		ParkedFile park(dst, Conventions::kMoveReplaceTag);
		QVERIFY(park.park());
		parkedPath = park.path();
		park.disarm();
	}
	// Disarmed: the destructor put nothing back and deleted nothing.
	QVERIFY(!QFile::exists(dst));
	QCOMPARE(readFile(parkedPath), QByteArray("ORIGINAL"));
}

// restore() must never claim a clean rollback it did not achieve. When the
// engine's OWN unfinished write will not delete — a file another program
// still holds open is the everyday Windows case — a truncated file is left
// sitting under a real media name. Swallowing that had the caller journal
// an ordinary failure, recovery read it as "nothing happened", and the
// fragment stayed on disk for good: media to Avid, "already exists" to the
// next run.
void TestOpRunner::parkedfile_reports_a_partial_it_could_not_delete()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString dir = tmp.path() + "/dest";
	QVERIFY(QDir().mkpath(dir));
	const QString dst = dir + "/clip.mxf";

	// An EMPTY destination slot: nothing to park, so the only thing
	// restore() has to undo is our own partial write.
	ParkedFile park(dst, Conventions::kCopyReplaceTag);
	QVERIFY(park.path().isEmpty());
	QVERIFY(park.park());
	writeFile(dst, "OUR-PARTIAL");
	park.noteDestinationWritten();

	// Block the removal the way the real world does.
#ifdef Q_OS_WIN
	// No FILE_SHARE_DELETE: DeleteFile fails with a sharing violation,
	// exactly as when Media Composer or a scanner has the file open.
	const QString native = QDir::toNativeSeparators(dst);
	HANDLE hold = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), GENERIC_READ,
								FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	QVERIFY2(hold != INVALID_HANDLE_VALUE, "could not hold the file open");
#else
	// Removing a file needs write permission on its FOLDER, not the file.
	QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner));
#endif

	const bool restored = park.restore();

#ifdef Q_OS_WIN
	::CloseHandle(hold);
#else
	QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
#endif

	QVERIFY2(!restored, "restore() must not report success while its partial is still there");
	QVERIFY(park.isStranded());
	QVERIFY2(park.destinationLeftBehind(),
			 "the caller needs to tell this apart from a stranded parked original");
	QCOMPARE(park.destinationPath(), dst);
	QCOMPARE(readFile(dst), QByteArray("OUR-PARTIAL"));

	park.disarm(); // the runner's contract after any stranded flag
	QVERIFY(!park.isStranded());
	QVERIFY(!park.destinationLeftBehind());
}

// Finding 3 end-to-end at the copier: between parking the old
// destination and opening the new one, another process creates a file at
// the path. NewOnly refuses; the racer's file must survive and the
// parked original must still be waiting, reported stranded.
void TestOpRunner::copier_racer_at_destination_survives()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString src = tmp.path() + "/src.mxf";
	const QString dst = tmp.path() + "/dst/clip.mxf";
	writeFile(src, "SOURCE-BYTES");
	writeFile(dst, "ORIGINAL");

	qputenv("MEDIAMUSTER_DISABLE_CLONEFILE", "1");
	qputenv("MEDIAMUSTER_DISABLE_COPYFILEEX", "1"); // both native paths, or Windows stays native

	ParkedFile park(dst, Conventions::kCopyReplaceTag);
	QVERIFY(park.park());
	writeFile(dst, "RACER"); // lands after the park, before the open

	OpCopier copier;
	std::atomic<bool> cancel{false};
	const OpCopier::Result res =
		copier.copy(src, dst, park, cancel, NativeFile::Durability::Disk, {}, {});

	qunsetenv("MEDIAMUSTER_DISABLE_CLONEFILE");
	qunsetenv("MEDIAMUSTER_DISABLE_COPYFILEEX");

	QVERIFY(res.outcome == OpCopier::Outcome::Failed);
	QVERIFY2(res.error.contains(QStringLiteral("left untouched")),
			 qPrintable(res.error));
	QCOMPARE(readFile(dst), QByteArray("RACER"));
	QCOMPARE(readFile(park.path()), QByteArray("ORIGINAL"));
	QVERIFY(park.isStranded());
	park.disarm(); // the runner would journal a dirty fail here
}

// MARK: - Rename

void TestOpRunner::rename_moves_file_and_fires_folder_hook()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString srcDir = tmp.path() + "/MXF/1";
	const QString dstDir = tmp.path() + "/MXF/2";
	const QString src = srcDir + "/clip.mxf";
	writeFile(src, "REBALANCED");
	QDir().mkpath(dstDir);

	OpRequest req;
	req.kind = OpKind::Rename;
	OpItem it = makeItem(src, 10);
	it.renameDst = dstDir + "/clip.mxf";
	it.groupKey = QStringLiteral("g1");
	req.items = {it};

	TestSink sink;
	QStringList touched;
	OpRunner runner(sink, kNoCancel);
	runner.onRenameFolderTouched = [&touched](const QString &folder) { touched << folder; };
	const auto totals = runner.run(req, journalDir.path());

	QCOMPARE(totals.succeeded, 1);
	QCOMPARE(readFile(dstDir + "/clip.mxf"), QByteArray("REBALANCED"));
	QVERIFY(!QFile::exists(src));
	// Both folders' Avid databases went stale; the hook (the Rebalance
	// adapter's reset) must fire once per folder.
	QVERIFY(touched.contains(QDir(srcDir).absolutePath()) ||
			touched.contains(srcDir));
	QCOMPARE(touched.size(), 2);

	// And the rename is a first-class journaled op: write-ahead line,
	// done line, recoverable like everything else.
	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(recs.size(), 1);
	QCOMPARE(recs[0].kind, OpKind::Rename);
	QVERIFY(recs[0].ops[0].completed);
}

void TestOpRunner::rename_refuses_occupied_destination()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/MXF/1/clip.mxf";
	const QString dst = tmp.path() + "/MXF/2/clip.mxf";
	writeFile(src, "MINE");
	writeFile(dst, "THEIRS");

	OpRequest req;
	req.kind = OpKind::Rename;
	OpItem it = makeItem(src, 4);
	it.renameDst = dst;
	req.items = {it};

	TestSink sink;
	const auto totals = runRequest(req, sink, journalDir.path());
	QCOMPARE(totals.failed, 1);
	QCOMPARE(readFile(src), QByteArray("MINE"));
	QCOMPARE(readFile(dst), QByteArray("THEIRS"));
}

void TestOpRunner::rename_precancelled_runs_nothing()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/MXF/1/clip.mxf";
	writeFile(src, "STAY");

	OpRequest req;
	req.kind = OpKind::Rename;
	OpItem it = makeItem(src, 4);
	it.renameDst = tmp.path() + "/MXF/2/clip.mxf";
	req.items = {it};

	TestSink sink;
	std::atomic<bool> cancelled{true};
	OpRunner runner(sink, cancelled);
	const auto totals = runner.run(req, journalDir.path());
	QCOMPARE(totals.succeeded, 0);
	QVERIFY2(QFile::exists(src), "a pre-cancelled run must touch nothing");
}

// MARK: - Journal integration

void TestOpRunner::journal_survives_run_and_plan_precedes_ops()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "JOURNALED");
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	// Size must match the payload byte-for-byte, or captureAndCheckSource
	// refuses the item before it is ever copied.
	req.items = {makeItem(src, 9)};

	TestSink sink;
	runRequest(req, sink, journalDir.path());

	// v2 retention: the finished journal STAYS (it is the undo
	// candidate). v1 pruned it here — which is why undo never worked.
	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(recs.size(), 1);
	QVERIFY(recs[0].complete);
	QVERIFY(OpJournal::latestUndoable(journalDir.path()).has_value());

	// The write-ahead property, checked structurally: in the file's
	// actual line order, the plan precedes every op line.
	QFile f(recs[0].path);
	QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
	int planLine = -1, firstOpLine = -1, line = 0;
	while (!f.atEnd())
	{
		const QByteArray l = f.readLine();
		if (planLine < 0 && l.contains("\"record\":\"plan\""))
			planLine = line;
		if (firstOpLine < 0 && l.contains("\"record\":\"op\""))
			firstOpLine = line;
		++line;
	}
	QVERIFY(planLine >= 0 && firstOpLine >= 0);
	QVERIFY2(planLine < firstOpLine, "the plan must be written before the first op");
}

void TestOpRunner::plan_records_volume_fingerprints()
{
	QTemporaryDir tmp, journalDir;
	QVERIFY(tmp.isValid() && journalDir.isValid());
	const QString src = tmp.path() + "/src/clip.mxf";
	const QString dest = tmp.path() + "/dest";
	writeFile(src, "VOLUMED");
	QDir().mkpath(dest);

	OpRequest req;
	req.kind = OpKind::Copy;
	req.destRoot = dest;
	req.items = {makeItem(src, 7)};

	TestSink sink;
	runRequest(req, sink, journalDir.path());

	// Everything here is one temp volume, but its fingerprint must be
	// in the plan — that is what lets recovery re-find a drive that
	// came back under another name, and refuse an impostor.
	const auto recs = OpJournal::scan(journalDir.path());
	QCOMPARE(recs.size(), 1);
	QVERIFY(!recs[0].volumes.isEmpty());
	QVERIFY(recs[0].volumes[0].matches(VolumeIdentity::capture(tmp.path())));
}

QTEST_APPLESS_MAIN(TestOpRunner)
#include "tst_oprunner.moc"
