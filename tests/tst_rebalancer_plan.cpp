#include "mediafile.h"
#include "rebalanceplan.h"
#include "rebalancer.h"
#include "opjournal.h"
#include "testpause.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#include <memory>

class TestRebalancerPlan : public QObject
{
	Q_OBJECT
private slots:
	void missing_root_yields_empty_plan();
	void noop_when_already_balanced();
	void consolidates_relatives_into_home_folder();
	void quarantined_folder_left_alone();
	void out_of_scope_media_files_dropped();

	// Only real media files occupy Avid's per-folder budget. The folder's
	// own databases (msmFMID.pmr / msmMMOB.mdb), dot-hidden files (incl.
	// AppleDouble "._*"), and Windows shell junk used to inflate the
	// preview's count AND steal slots from the 4999-cap packing.
	void folder_count_excludes_databases_and_hidden_files();
	void host_prefix_isolates_consolidation();
	void home_full_falls_back_to_existing_folder();
	void new_folder_when_all_existing_are_full();

	// Execution — now through the ops engine, so every rename is
	// write-ahead journaled and recoverable. What has to hold is that an
	// approved plan actually lands, and that a folder which won't accept
	// renames stops the run before anything moves — proved with a
	// scratch file, never by renaming one of the user's clips (which is
	// what used to strand real media).
	void initTestCase();
	void cleanupTestCase();
	void execute_moves_the_planned_files();
	void execute_runs_a_planner_built_plan_and_creates_its_new_folder();
	void execute_resets_the_avid_databases_of_every_folder_it_touches();
	void execute_never_clobbers_an_existing_destination();
	void execute_cancel_keeps_what_already_landed();
	void destruction_during_execution_joins_the_engine();
	void execute_aborts_when_a_donor_folder_is_read_only();

private:
	/// Returns "<tmp>/Avid MediaFiles/MXF"; creates the path.
	static QString stageMxfRoot(const QTemporaryDir &tmp);

	/// Sandbox for the engine's journals: outlives every test so the env
	/// var keeps pointing at a real directory, and the real AppData
	/// journal (with a user's live undo candidate) is never touched.
	QTemporaryDir m_journalDir;

	/// Fills <mxfRoot>/<folderName>/ with `fillerCount` empty .mxf-named
	/// files. Bumps the on-disk count without producing MediaFiles —
	/// only .mxf entries count toward the folder budget, so cap tests
	/// use these to make a folder 'full' without indexing thousands of
	/// MediaFiles.
	static void makeFillers(const QString &mxfRoot, const QString &folderName, int fillerCount);

	/// Creates one empty file under <mxfRoot>/<folderName>/<name>
	/// and returns a MediaFile pointing at it.
	static MediaFile makeMxf(const QString &mxfRoot, const QString &folderName, const QString &name,
							 const QString &masterMobId = {}, qint64 sizeBytes = 1000);

	/// Counts ops with this src-to-dest folder pair.
	static int opsBetween(const RebalancePlan &p, const QString &srcFolder,
						  const QString &destFolder);
};

void TestRebalancerPlan::initTestCase()
{
	QVERIFY(m_journalDir.isValid());
	qputenv("MEDIAMUSTER_JOURNAL_DIR", m_journalDir.path().toUtf8());
}

void TestRebalancerPlan::cleanupTestCase()
{
	qunsetenv("MEDIAMUSTER_JOURNAL_DIR");
}

QString TestRebalancerPlan::stageMxfRoot(const QTemporaryDir &tmp)
{
	const QString root = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF");
	[[maybe_unused]] const bool ok = QDir().mkpath(root);
	Q_ASSERT(ok);
	return root;
}

void TestRebalancerPlan::makeFillers(const QString &mxfRoot, const QString &folderName,
									 int fillerCount)
{
	const QString folder = mxfRoot + QLatin1Char('/') + folderName;
	QDir().mkpath(folder);
	for (int i = 0; i < fillerCount; ++i)
	{
		QFile f(folder + QStringLiteral("/filler_%1.mxf").arg(i));
		[[maybe_unused]] const bool opened = f.open(QIODevice::WriteOnly);
		Q_ASSERT(opened);
		f.close();
	}
}

MediaFile TestRebalancerPlan::makeMxf(const QString &mxfRoot, const QString &folderName,
									  const QString &name, const QString &masterMobId,
									  qint64 sizeBytes)
{
	const QString folder = mxfRoot + QLatin1Char('/') + folderName;
	QDir().mkpath(folder);
	const QString path = folder + QLatin1Char('/') + name;
	QFile f(path);
	[[maybe_unused]] const bool opened = f.open(QIODevice::WriteOnly);
	Q_ASSERT(opened);
	// The on-disk size must MATCH the claimed sizeBytes: the engine's
	// identity gate refuses to touch a file whose real size differs from
	// what the scan recorded (that is the point of the gate), so a
	// fixture that claims 1000 bytes over an empty file would be refused
	// as a swapped file. resize() gives any size as a sparse file for
	// free — honest metadata without writing the bytes.
	if (sizeBytes > 0)
	{
		[[maybe_unused]] const bool sized = f.resize(sizeBytes);
		Q_ASSERT(sized);
	}
	f.close();

	MediaFile mf;
	mf.filePath = path;
	mf.mxfFolder = folderName;
	mf.masterMobId = masterMobId;
	mf.sizeBytes = sizeBytes;
	return mf;
}

int TestRebalancerPlan::opsBetween(const RebalancePlan &p, const QString &srcFolder,
								   const QString &destFolder)
{
	int n = 0;
	for (const auto &op : p.ops)
	{
		const QString actualSrc = QFileInfo(op.srcPath).dir().dirName();
		if (actualSrc == srcFolder && op.dest.display() == destFolder)
			++n;
	}
	return n;
}

// MARK: - Tests

void TestRebalancerPlan::missing_root_yields_empty_plan()
{
	const RebalancePlan p =
		Rebalancer::computePlan(QStringLiteral("/nope/does/not/exist"), QStringLiteral("Vol"), {});
	QCOMPARE(p.ops.size(), 0);
	QCOMPARE(p.newFolders.size(), 0);
	QCOMPARE(p.folders.size(), 0);
}

void TestRebalancerPlan::noop_when_already_balanced()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// Three files in "1", one relatives group, all already home.
	const QVector<MediaFile> files{
		makeMxf(root, "1", "a.mxf", "C1"),
		makeMxf(root, "1", "b.mxf", "C1"),
		makeMxf(root, "1", "c.mxf", "C1"),
	};

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 0);
	QCOMPARE(p.newFolders.size(), 0);
}

void TestRebalancerPlan::consolidates_relatives_into_home_folder()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// Comp C1: two members in '1', one in '2'. Home wins on member
	// count, giving '1'. The '2' member moves to '1'.
	const QVector<MediaFile> files{
		makeMxf(root, "1", "a.mxf", "C1"),
		makeMxf(root, "1", "b.mxf", "C1"),
		makeMxf(root, "2", "c.mxf", "C1"),
	};

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 1);
	QCOMPARE(opsBetween(p, "2", "1"), 1);
	QCOMPARE(p.newFolders.size(), 0);
}

void TestRebalancerPlan::quarantined_folder_left_alone()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	makeFillers(root, "Quarantined Files", 3);
	const QVector<MediaFile> files{
		makeMxf(root, "1", "a.mxf"),
		makeMxf(root, "1", "b.mxf"),
	};

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 0);

	// Quarantined appears in folders[] but marked out-of-scope.
	bool quarantinedSeen = false;
	for (const auto &fs : p.folders)
	{
		if (fs.name == QStringLiteral("Quarantined Files"))
		{
			QVERIFY(!fs.inScope);
			QCOMPARE(fs.count, 3);
			quarantinedSeen = true;
		}
	}
	QVERIFY(quarantinedSeen);
}

void TestRebalancerPlan::folder_count_excludes_databases_and_hidden_files()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// Two real media files plus everything that must NOT count: Avid
	// databases, a dot-hidden file, an AppleDouble sidecar (named .mxf but
	// not media), Windows shell junk, and a stray non-MXF file. Only .mxf
	// entries occupy Avid's per-folder budget.
	const QVector<MediaFile> files{
		makeMxf(root, "1", "a.mxf"),
		makeMxf(root, "1", "b.mxf"),
	};
	for (const char *junk : {"msmFMID.pmr", "msmMMOB.mdb", ".DS_Store", "._a.mxf", "Thumbs.db",
							 "desktop.ini", "render.mov"})
	{
		QFile f(root + QStringLiteral("/1/") + QLatin1String(junk));
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.close();
	}

	// Avid's transient capture staging folder must stay out of scope — its
	// contents never count toward any folder's budget.
	makeFillers(root, "Creating", 2);

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);

	bool sawOne = false, sawCreating = false;
	for (const auto &fs : p.folders)
	{
		if (fs.name == QStringLiteral("1"))
		{
			QCOMPARE(fs.count, 2); // media only; databases and junk invisible
			sawOne = true;
		}
		if (fs.name == QStringLiteral("Creating"))
		{
			QVERIFY(!fs.inScope);
			sawCreating = true;
		}
	}
	QVERIFY(sawOne);
	QVERIFY(sawCreating);
}

void TestRebalancerPlan::out_of_scope_media_files_dropped()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// One in-scope file, one with an unparseable folder name.
	// The second is dropped from planning entirely.
	const QVector<MediaFile> files{
		makeMxf(root, "1", "a.mxf"),
		makeMxf(root, "Quarantined Files", "b.mxf"),
	};

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 0);
}

void TestRebalancerPlan::host_prefix_isolates_consolidation()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// Two prefixes; each balances within itself. The cross-prefix
	// move never happens.
	const QVector<MediaFile> files{
		makeMxf(root, "MartysiMac.1", "a.mxf", "C1"),
		makeMxf(root, "MartysiMac.1", "b.mxf", "C1"),
		makeMxf(root, "MartysiMac.2", "c.mxf", "C1"),
		makeMxf(root, "Edit14.5", "d.mxf", "C2"),
	};

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 1);
	QCOMPARE(opsBetween(p, "MartysiMac.2", "MartysiMac.1"), 1);
}

void TestRebalancerPlan::home_full_falls_back_to_existing_folder()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// "1" is at the cap minus 1 (4998 fillers + 1 C1 member = 4999).
	// "2" holds 3 C1 strays + 0 fillers (3 on disk). The C1 group of
	// 4 members can't all fit in home "1" (would be 5002). First-fit
	// finds "2" (3 + 4 = 7). Plan moves the "1" member to "2".
	makeFillers(root, "1", 4998);
	const QVector<MediaFile> files{
		makeMxf(root, "1", "home_member.mxf", "C1"),
		makeMxf(root, "2", "stray_a.mxf", "C1"),
		makeMxf(root, "2", "stray_b.mxf", "C1"),
		makeMxf(root, "2", "stray_c.mxf", "C1"),
	};

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 1);
	QCOMPARE(opsBetween(p, "1", "2"), 1);
	QCOMPARE(p.newFolders.size(), 0);
}

void TestRebalancerPlan::new_folder_when_all_existing_are_full()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// Both '1' and '2' are at the cap. A 6-member C1 group can't
	// fit anywhere existing, so the planner allocates '3' and routes all
	// members into it.
	makeFillers(root, "1", 4998);
	makeFillers(root, "2", 4994);
	const QVector<MediaFile> files{
		makeMxf(root, "1", "m1.mxf", "C1"),
		makeMxf(root, "2", "m2.mxf", "C1"),
		makeMxf(root, "2", "m3.mxf", "C1"),
		makeMxf(root, "2", "m4.mxf", "C1"),
		makeMxf(root, "2", "m5.mxf", "C1"),
		makeMxf(root, "2", "m6.mxf", "C1"),
	};

	const RebalancePlan p = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(p.newFolders.size(), 1);
	QCOMPARE(p.newFolders.first().display(), QStringLiteral("3"));
	QCOMPARE(p.ops.size(), 6);
	QCOMPARE(opsBetween(p, "1", "3"), 1);
	QCOMPARE(opsBetween(p, "2", "3"), 5);
}

void TestRebalancerPlan::execute_moves_the_planned_files()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	// Six under-cap clips in folder 1 and an empty folder 2. The planner
	// leaves that alone (nothing is over the cap), so the ops below are
	// hand-built: this test is about execution, not planning. The
	// planner-built case is the next test.
	QVector<MediaFile> files;
	for (int i = 0; i < 6; ++i)
		files << makeMxf(root, QStringLiteral("1"), QStringLiteral("clip%1.mxf").arg(i),
						 QStringLiteral("mob%1").arg(i), 1000);
	makeFillers(root, QStringLiteral("2"), 0);

	RebalancePlan plan = Rebalancer::computePlan(root, QStringLiteral("EDIT"), files);
	// Force a redistribution the planner would otherwise leave alone: send
	// half of folder 1 to folder 2 by hand, so this test pins execution,
	// not planning (which the tests above already cover).
	plan.ops.clear();
	for (int i = 0; i < 3; ++i)
		plan.ops.append({root + QStringLiteral("/1/clip%1.mxf").arg(i),
						 FolderName{QString(), 2},
						 QStringLiteral("mob%1").arg(i),
						 1000});

	Rebalancer r;
	QSignalSpy finished(&r, &Rebalancer::finished);
	QSignalSpy aborted(&r, &Rebalancer::aborted);
	r.executeAsync(plan);
	// QTRY_*, never QSignalSpy::wait(): the worker can emit before wait()
	// is even entered, and wait() would then sit out its whole timeout and
	// report failure for a run that succeeded.
	QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 30000);

	QCOMPARE(aborted.count(), 0);
	QCOMPARE(finished.first().at(0).toInt(), 3); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 0); // failed
	for (int i = 0; i < 3; ++i)
	{
		QVERIFY2(QFile::exists(root + QStringLiteral("/2/clip%1.mxf").arg(i)), "moved file missing");
		QVERIFY2(!QFile::exists(root + QStringLiteral("/1/clip%1.mxf").arg(i)), "source left behind");
	}
	for (int i = 3; i < 6; ++i)
		QVERIFY2(QFile::exists(root + QStringLiteral("/1/clip%1.mxf").arg(i)), "untouched file moved");

	// The pre-flight scratch file must never survive the run.
	QVERIFY2(QDir(root + QStringLiteral("/1"))
				 .entryList({QStringLiteral(".mm_preflight_*")}, QDir::Files | QDir::Hidden)
				 .isEmpty(),
			 "pre-flight scratch file left behind");
}

void TestRebalancerPlan::execute_runs_a_planner_built_plan_and_creates_its_new_folder()
{
	// The other execution tests hand-build their ops, which never exercises
	// plan.newFolders — so the mkpath loop could be deleted and they would
	// all still pass. This one runs exactly what the planner returned: both
	// existing folders are at the cap, so the group has to go to a folder
	// that does not exist yet.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	makeFillers(root, "1", 4998);
	makeFillers(root, "2", 4994);
	const QVector<MediaFile> files{
		makeMxf(root, "1", "m1.mxf", "C1"),
		makeMxf(root, "2", "m2.mxf", "C1"),
		makeMxf(root, "2", "m3.mxf", "C1"),
		makeMxf(root, "2", "m4.mxf", "C1"),
		makeMxf(root, "2", "m5.mxf", "C1"),
		makeMxf(root, "2", "m6.mxf", "C1"),
	};

	const RebalancePlan plan = Rebalancer::computePlan(root, "Vol", files);
	QCOMPARE(plan.newFolders.size(), 1);
	QVERIFY2(!QDir(root + QStringLiteral("/3")).exists(), "folder 3 must not exist yet");

	Rebalancer r;
	QSignalSpy finished(&r, &Rebalancer::finished);
	r.executeAsync(plan);
	QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 30000);

	QCOMPARE(finished.first().at(0).toInt(), 6); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 0); // failed
	QVERIFY2(QDir(root + QStringLiteral("/3")).exists(), "the plan's new folder was never created");
	for (const char *name : {"m1.mxf", "m2.mxf", "m3.mxf", "m4.mxf", "m5.mxf", "m6.mxf"})
		QVERIFY2(QFile::exists(root + QStringLiteral("/3/") + QLatin1String(name)),
				 qPrintable(QString(QLatin1String(name)) + " never reached the new folder"));
}

void TestRebalancerPlan::execute_resets_the_avid_databases_of_every_folder_it_touches()
{
	// Avid's per-folder databases are wrong the moment a file moves in or
	// out, and they must be gone before the next crash, not after the run:
	// a database that survives, parses, and doesn't mention the clips now
	// sitting beside it is what makes the scanner report them as "No
	// reference" — the state a user culls from.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	const QVector<MediaFile> files{makeMxf(root, "1", "clip.mxf", "mob0", 1000)};
	makeFillers(root, "2", 0);

	const QStringList dbs{root + QStringLiteral("/1/msmMMOB.mdb"), root + QStringLiteral("/1/msmFMID.pmr"),
						  root + QStringLiteral("/2/msmMMOB.mdb"), root + QStringLiteral("/2/msmFMID.pmr")};
	for (const QString &db : dbs)
	{
		QFile f(db);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write("STALE");
	}

	RebalancePlan plan = Rebalancer::computePlan(root, "Vol", files);
	plan.ops.clear();
	plan.ops.append({root + QStringLiteral("/1/clip.mxf"), FolderName{QString(), 2},
					 QStringLiteral("mob0"), 1000});

	Rebalancer r;
	QSignalSpy finished(&r, &Rebalancer::finished);
	r.executeAsync(plan);
	QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 30000);

	QCOMPARE(finished.first().at(0).toInt(), 1);
	for (const QString &db : dbs)
		QVERIFY2(!QFile::exists(db), qPrintable(db + QStringLiteral(" survived the rebalance")));
}

void TestRebalancerPlan::execute_never_clobbers_an_existing_destination()
{
	// Two folders can hold same-named essence after a manual copy. With no
	// journal behind it, this guard is the only thing between a rebalance
	// and a destroyed clip.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	const QVector<MediaFile> files{makeMxf(root, "1", "clip.mxf", "mob0", 1000)};
	makeFillers(root, "2", 0);
	{
		QFile occupied(root + QStringLiteral("/2/clip.mxf"));
		QVERIFY(occupied.open(QIODevice::WriteOnly));
		occupied.write("THE OTHER CLIP");
	}

	RebalancePlan plan = Rebalancer::computePlan(root, "Vol", files);
	plan.ops.clear();
	plan.ops.append({root + QStringLiteral("/1/clip.mxf"), FolderName{QString(), 2},
					 QStringLiteral("mob0"), 1000});

	Rebalancer r;
	QSignalSpy finished(&r, &Rebalancer::finished);
	r.executeAsync(plan);
	QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 30000);

	QCOMPARE(finished.first().at(0).toInt(), 0); // succeeded
	QCOMPARE(finished.first().at(1).toInt(), 1); // failed
	QVERIFY2(QFile::exists(root + QStringLiteral("/1/clip.mxf")), "the source must stay put");
	QFile kept(root + QStringLiteral("/2/clip.mxf"));
	QVERIFY(kept.open(QIODevice::ReadOnly));
	QCOMPARE(kept.readAll(), QByteArray("THE OTHER CLIP"));
}

void TestRebalancerPlan::execute_cancel_keeps_what_already_landed()
{
	// "Stop and keep" is the contract the journal-free design rests on: a
	// cancelled (or crashed) run leaves a legal layout, never a lost file.
	// Cancel is requested from the first progress signal — the throttle
	// only emits once 33 ms have passed, so on a fast disk the run can
	// finish first; the flag is therefore asserted only when it is set,
	// while the property that matters — every clip at exactly one of its
	// two paths — is asserted unconditionally.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	constexpr int kFiles = 400;
	QVector<MediaFile> files;
	for (int i = 0; i < kFiles; ++i)
		files << makeMxf(root, "1", QStringLiteral("clip%1.mxf").arg(i),
						 QStringLiteral("mob%1").arg(i), 100);
	makeFillers(root, "2", 0);

	RebalancePlan plan = Rebalancer::computePlan(root, "Vol", files);
	plan.ops.clear();
	for (int i = 0; i < kFiles; ++i)
		plan.ops.append({root + QStringLiteral("/1/clip%1.mxf").arg(i), FolderName{QString(), 2},
						 QStringLiteral("mob%1").arg(i), 100});

	Rebalancer r;
	QSignalSpy finished(&r, &Rebalancer::finished);
	// No context object: a direct connection, so the flag is set on the
	// worker thread the instant the first progress fires.
	QObject::connect(&r, &Rebalancer::progress, [&r]
					 { r.cancel(); });
	r.executeAsync(plan);
	QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 60000);

	const int succeeded = finished.first().at(0).toInt();
	const int failed = finished.first().at(1).toInt();
	const bool cancelled = finished.first().at(2).toBool();

	int atSource = 0, atDest = 0;
	for (int i = 0; i < kFiles; ++i)
	{
		const bool src = QFile::exists(root + QStringLiteral("/1/clip%1.mxf").arg(i));
		const bool dst = QFile::exists(root + QStringLiteral("/2/clip%1.mxf").arg(i));
		QVERIFY2(src != dst, qPrintable(QStringLiteral("clip%1 is in neither folder or both").arg(i)));
		src ? ++atSource : ++atDest;
	}
	QCOMPARE(atDest, succeeded);
	QCOMPARE(atSource, kFiles - succeeded);
	QCOMPARE(failed, 0);
	if (cancelled)
		QVERIFY2(succeeded < kFiles, "a cancelled run must not have moved everything");
}

void TestRebalancerPlan::destruction_during_execution_joins_the_engine()
{
	// Rebalancer's QObject child owns a second worker which calls back into
	// the parent. Destruction must stop and join it before parent teardown,
	// preserving landed files and completing its journal synchronously.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);
	const QVector<MediaFile> files{
		makeMxf(root, "1", "a.mxf", "mobA"),
		makeMxf(root, "1", "b.mxf", "mobB"),
		makeMxf(root, "1", "c.mxf", "mobC"),
	};
	makeFillers(root, "2", 0);
	RebalancePlan plan = Rebalancer::computePlan(root, "Vol", files);
	plan.ops.clear();
	for (const MediaFile &file : files)
		plan.ops.append({file.filePath, FolderName{QString(), 2},
						 file.masterMobId, file.sizeBytes});

	TestPause::setEnabled(true);
	const auto restorePause = qScopeGuard([]
										  { TestPause::setEnabled(false); });
	auto rebalancer = std::make_unique<Rebalancer>();
	QSignalSpy progress(rebalancer.get(), &Rebalancer::progress);
	rebalancer->executeAsync(plan);
	QTRY_VERIFY_WITH_TIMEOUT(!progress.isEmpty(), 30000);
	rebalancer.reset();

	int landed = 0;
	for (const MediaFile &file : files)
	{
		const bool atSource = QFile::exists(file.filePath);
		const bool atDest = QFile::exists(root + "/2/" + QFileInfo(file.filePath).fileName());
		QVERIFY(atSource != atDest);
		landed += atDest;
	}
	QVERIFY(landed > 0);
	QVERIFY(landed < files.size());
	const auto records = OpJournal::scan(m_journalDir.path());
	QCOMPARE(records.size(), 1);
	QVERIFY(records.first().complete);
	QVERIFY(records.first().cancelled);
	QVERIFY(!records.first().dirty);
	QCOMPARE(records.first().doneCount(), landed);
}

void TestRebalancerPlan::execute_aborts_when_a_donor_folder_is_read_only()
{
#ifdef Q_OS_WIN
	QSKIP("Directory write-protection doesn't block renames the same way on Windows.");
#endif
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	QVector<MediaFile> files;
	for (int i = 0; i < 2; ++i)
		files << makeMxf(root, QStringLiteral("1"), QStringLiteral("clip%1.mxf").arg(i),
						 QStringLiteral("mob%1").arg(i), 1000);

	RebalancePlan plan = Rebalancer::computePlan(root, QStringLiteral("EDIT"), files);
	plan.ops.clear();
	plan.ops.append({root + QStringLiteral("/1/clip0.mxf"), FolderName{QString(), 2},
					 QStringLiteral("mob0"), 1000});

	makeFillers(root, QStringLiteral("2"), 0); // the target must exist, or
											   // "nothing arrived" proves nothing
	const QString donor = root + QStringLiteral("/1");
	QVERIFY(QFile::setPermissions(donor, QFile::ReadOwner | QFile::ExeOwner));
	// Put the permissions back even if an assertion below returns early —
	// otherwise QTemporaryDir can't clean up and every failing run litters
	// the machine.
	const auto restore = qScopeGuard(
		[&donor]
		{
			QFile::setPermissions(donor,
								  QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
		});

	Rebalancer r;
	QSignalSpy finished(&r, &Rebalancer::finished);
	QSignalSpy aborted(&r, &Rebalancer::aborted);
	r.executeAsync(plan);
	QTRY_VERIFY_WITH_TIMEOUT(!aborted.isEmpty(), 30000);

	QCOMPARE(finished.count(), 0);
	QVERIFY2(QFile::exists(donor + QStringLiteral("/clip0.mxf")),
			 "nothing may move once the pre-flight fails");
	QVERIFY2(!QFile::exists(root + QStringLiteral("/2/clip0.mxf")), "no file may reach the target");
}

// computePlan is pure-sync, but executeAsync runs on a worker thread and
// reports through queued signals, so these need a real event loop.
QTEST_MAIN(TestRebalancerPlan)
#include "tst_rebalancer_plan.moc"
