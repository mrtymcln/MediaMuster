#include "rebalanceplanner.h"
#include "mediafile.h"
#include "rebalanceplan.h"
#include "mobid.h"
#include <QCryptographicHash>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

class TestRebalancePlanner : public QObject
{
	Q_OBJECT
private slots:
	void parseFolderName_plain_number();
	void parseFolderName_with_host_prefix();
	void parseFolderName_rejects_quarantined();
	void parseFolderName_rejects_leading_dot();
	void parseFolderName_rejects_zero_padded();
	void parseFolderName_rejects_zero();
	void parseFolderName_rejects_negative();
	void parseFolderName_rejects_non_numeric_tail();

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
	void same_master_never_crosses_workstation_prefix();
	void exactly_4999_relatives_are_stable();
	void oversized_packed_relatives_are_stable();
	void invalid_master_ids_are_independent();
	void media_from_another_root_is_excluded();
	void home_full_falls_back_to_existing_folder();
	void new_folder_when_all_existing_are_full();

private:
	/// Returns "<tmp>/Avid MediaFiles/MXF"; creates the path.
	static QString stageMxfRoot(const QTemporaryDir &tmp);

	/// Fills <mxfRoot>/<folderName>/ with `fillerCount` empty .mxf-named
	/// files. Bumps the on-disk count without producing MediaFiles —
	/// only .mxf entries count toward the folder budget, so cap tests
	/// use these to make a folder 'full' without indexing thousands of
	/// MediaFiles.
	static void makeFillers(const QString &mxfRoot, const QString &folderName, int fillerCount);

	/// Creates one sparse file under <mxfRoot>/<folderName>/<name>
	/// and returns a MediaFile pointing at it.
	static MediaFile makeMxf(const QString &mxfRoot, const QString &folderName, const QString &name,
							 const QString &masterMobId = {}, qint64 sizeBytes = 1000);

	/// Counts ops with this src-to-dest folder pair.
	static int opsBetween(const RebalancePlan &p, const QString &srcFolder,
						  const QString &destFolder);
};

QString TestRebalancePlanner::stageMxfRoot(const QTemporaryDir &tmp)
{
	const QString root = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF");
	[[maybe_unused]] const bool ok = QDir().mkpath(root);
	Q_ASSERT(ok);
	return root;
}

void TestRebalancePlanner::makeFillers(const QString &mxfRoot, const QString &folderName,
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

MediaFile TestRebalancePlanner::makeMxf(const QString &mxfRoot, const QString &folderName,
										const QString &name, const QString &masterMobId,
										qint64 sizeBytes)
{
	const QString folder = mxfRoot + QLatin1Char('/') + folderName;
	QDir().mkpath(folder);
	const QString path = folder + QLatin1Char('/') + name;
	QFile f(path);
	[[maybe_unused]] const bool opened = f.open(QIODevice::WriteOnly);
	Q_ASSERT(opened);
	// Match the planner's claimed size without writing a media payload.
	if (sizeBytes > 0)
	{
		[[maybe_unused]] const bool sized = f.resize(sizeBytes);
		Q_ASSERT(sized);
	}
	f.close();

	MediaFile mf;
	mf.filePath = path;
	mf.mediaFolderName = folderName;
	mf.masterMobId = masterMobId.isEmpty() ? QString()
										   : MobId::format(QCryptographicHash::hash(
												 masterMobId.toUtf8(), QCryptographicHash::Sha256));
	mf.sizeBytes = sizeBytes;
	return mf;
}

int TestRebalancePlanner::opsBetween(const RebalancePlan &p, const QString &srcFolder,
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

void TestRebalancePlanner::parseFolderName_plain_number()
{
	const auto id = RebalancePlanner::parseFolderName(QStringLiteral("5"));
	QVERIFY(id.has_value());
	QCOMPARE(id->prefix, QString());
	QCOMPARE(id->n, 5);
	QCOMPARE(id->display(), QStringLiteral("5"));
}

void TestRebalancePlanner::parseFolderName_with_host_prefix()
{
	const auto id = RebalancePlanner::parseFolderName(QStringLiteral("MartysiMac.42"));
	QVERIFY(id.has_value());
	QCOMPARE(id->prefix, QStringLiteral("MartysiMac"));
	QCOMPARE(id->n, 42);
	QCOMPARE(id->display(), QStringLiteral("MartysiMac.42"));
}

void TestRebalancePlanner::parseFolderName_rejects_quarantined()
{
	QVERIFY(!RebalancePlanner::parseFolderName(QStringLiteral("Quarantined Files")).has_value());
}

void TestRebalancePlanner::parseFolderName_rejects_leading_dot()
{
	// '.5' parses tail '5' as n=5, but display would render as '5'
	// (empty prefix), which doesn't match the original '.5'.
	QVERIFY(!RebalancePlanner::parseFolderName(QStringLiteral(".5")).has_value());
}

void TestRebalancePlanner::parseFolderName_rejects_zero_padded()
{
	QVERIFY(!RebalancePlanner::parseFolderName(QStringLiteral("05")).has_value());
	QVERIFY(!RebalancePlanner::parseFolderName(QStringLiteral("MartysiMac.005")).has_value());
}

void TestRebalancePlanner::parseFolderName_rejects_zero()
{
	// Folder "0" is non-canonical for Avid. Rejected by the n > 0 guard.
	QVERIFY(!RebalancePlanner::parseFolderName(QStringLiteral("0")).has_value());
}

void TestRebalancePlanner::parseFolderName_rejects_negative()
{
	QVERIFY(!RebalancePlanner::parseFolderName(QStringLiteral("-1")).has_value());
}

void TestRebalancePlanner::parseFolderName_rejects_non_numeric_tail()
{
	QVERIFY(!RebalancePlanner::parseFolderName(QStringLiteral("MartysiMac.abc")).has_value());
}

void TestRebalancePlanner::missing_root_yields_empty_plan()
{
	const RebalancePlan p = RebalancePlanner::computePlan(QStringLiteral("/nope/does/not/exist"),
														  QStringLiteral("Vol"), {});
	QCOMPARE(p.ops.size(), 0);
	QCOMPARE(p.newFolders.size(), 0);
	QCOMPARE(p.folders.size(), 0);
}

void TestRebalancePlanner::noop_when_already_balanced()
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

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 0);
	QCOMPARE(p.newFolders.size(), 0);
}

void TestRebalancePlanner::consolidates_relatives_into_home_folder()
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

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 1);
	QCOMPARE(opsBetween(p, "2", "1"), 1);
	QCOMPARE(p.newFolders.size(), 0);
}

void TestRebalancePlanner::quarantined_folder_left_alone()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = stageMxfRoot(tmp);

	makeFillers(root, "Quarantined Files", 3);
	const QVector<MediaFile> files{
		makeMxf(root, "1", "a.mxf"),
		makeMxf(root, "1", "b.mxf"),
	};

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);
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

void TestRebalancePlanner::folder_count_excludes_databases_and_hidden_files()
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

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);

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

void TestRebalancePlanner::out_of_scope_media_files_dropped()
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

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 0);
}

void TestRebalancePlanner::host_prefix_isolates_consolidation()
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

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 1);
	QCOMPARE(opsBetween(p, "MartysiMac.2", "MartysiMac.1"), 1);
}

void TestRebalancePlanner::home_full_falls_back_to_existing_folder()
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

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);
	QCOMPARE(p.ops.size(), 1);
	QCOMPARE(opsBetween(p, "1", "2"), 1);
	QCOMPARE(p.newFolders.size(), 0);
}

void TestRebalancePlanner::new_folder_when_all_existing_are_full()
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
		makeMxf(root, "1", "m1.mxf", "C1"), makeMxf(root, "2", "m2.mxf", "C1"),
		makeMxf(root, "2", "m3.mxf", "C1"), makeMxf(root, "2", "m4.mxf", "C1"),
		makeMxf(root, "2", "m5.mxf", "C1"), makeMxf(root, "2", "m6.mxf", "C1"),
	};

	const RebalancePlan p = RebalancePlanner::computePlan(root, "Vol", files);
	QCOMPARE(p.newFolders.size(), 1);
	QCOMPARE(p.newFolders.first().display(), QStringLiteral("3"));
	QCOMPARE(p.ops.size(), 6);
	QCOMPARE(opsBetween(p, "1", "3"), 1);
	QCOMPARE(opsBetween(p, "2", "3"), 5);
}

void TestRebalancePlanner::same_master_never_crosses_workstation_prefix()
{
	QTemporaryDir tmp;
	const auto root = stageMxfRoot(tmp);
	const QVector<MediaFile> files{
		makeMxf(root, "Mac.1", "a.mxf", "same"), makeMxf(root, "Mac.2", "b.mxf", "same"),
		makeMxf(root, "PC.1", "c.mxf", "same"), makeMxf(root, "PC.2", "d.mxf", "same")};
	const auto plan = RebalancePlanner::computePlan(root, "Test", files);
	QCOMPARE(plan.ops.size(), 2);
	QCOMPARE(opsBetween(plan, "Mac.2", "Mac.1"), 1);
	QCOMPARE(opsBetween(plan, "PC.2", "PC.1"), 1);
}
void TestRebalancePlanner::exactly_4999_relatives_are_stable()
{
	QTemporaryDir tmp;
	const auto root = stageMxfRoot(tmp);
	QVector<MediaFile> files;
	for (int n = 0; n < 4999; ++n)
		files.append(makeMxf(root, "1", QString::number(n) + ".mxf", "same", 0));
	const auto plan = RebalancePlanner::computePlan(root, "Test", files);
	QVERIFY(plan.ops.isEmpty());
	QVERIFY(plan.newFolders.isEmpty());
}
void TestRebalancePlanner::oversized_packed_relatives_are_stable()
{
	QTemporaryDir tmp;
	const auto root = stageMxfRoot(tmp);
	QVector<MediaFile> files;
	for (int n = 0; n < 5000; ++n)
		files.append(makeMxf(root, n < 4999 ? "1" : "2", QString::number(n) + ".mxf", "same", 0));
	const auto plan = RebalancePlanner::computePlan(root, "Test", files);
	QVERIFY(plan.ops.isEmpty());
	QVERIFY(plan.newFolders.isEmpty());
}
void TestRebalancePlanner::invalid_master_ids_are_independent()
{
	QTemporaryDir tmp;
	const auto root = stageMxfRoot(tmp);
	QVector<MediaFile> files{makeMxf(root, "1", "a.mxf"), makeMxf(root, "2", "b.mxf")};
	for (const auto &id :
		 {QString("0000000000000000.0000000000000000.0000000000000000.0000000000000000"),
		  QString("invalid")})
	{
		for (auto &file : files)
			file.masterMobId = id;
		QVERIFY(RebalancePlanner::computePlan(root, "Test", files).ops.isEmpty());
	}
}
void TestRebalancePlanner::media_from_another_root_is_excluded()
{
	QTemporaryDir a, b;
	const auto root = stageMxfRoot(a), other = stageMxfRoot(b);
	const QVector<MediaFile> files{makeMxf(root, "1", "a.mxf", "same"),
								   makeMxf(other, "2", "b.mxf", "same")};
	QVERIFY(RebalancePlanner::computePlan(root, "Test", files).ops.isEmpty());
}
QTEST_APPLESS_MAIN(TestRebalancePlanner)
#include "tst_rebalanceplanner.moc"
