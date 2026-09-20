#include "conventions.h"
#include "avidmedialayout.h"

#include <QtTest>

// Conventions: the shared facts about Avid's folder layout and essence
// naming. These predicates are what keep the scanner, the rebalancer,
// and the copy-path builder agreeing on what counts as Avid media —
// the drift this header was extracted to end (a lowercase 'mxf' share
// scanned but wouldn't rebalance; AppleDouble "._*.mxf" siblings
// counted as media in the table but not in the folder budget).

class TestConventions : public QObject
{
	Q_OBJECT

private slots:
	void mxf_root_name_is_case_insensitive();
	void omf_root_is_not_an_mxf_root();
	void mxf_root_under_builds_canonical_path();
	void dot_hidden_names();
	void mxf_extension_is_case_insensitive();
	void essence_name_combinations();
	void avid_media_extensions();
	void folder_budget_thresholds_stay_ordered();

	// MARK: - OMF-era
	void omf_root_constant_and_omf_root_under();
	void omf_era_extension_set();
	void creating_folder_name_is_case_insensitive();
	void system_drive_media_bases_per_platform();
	void database_file_names_cover_both_spellings();
	void managed_mxf_folder_names_preserve_readable_spellings_data();
	void managed_mxf_folder_names_preserve_readable_spellings();
	void managed_roots_require_complete_component_names();
	void ume_paths_require_complete_component_names();
	void managed_media_folder_locations_data();
	void managed_media_folder_locations();
	void managed_format_families_keep_their_own_suffixes();
	void reserved_media_folder_names_are_case_insensitive();
};

void TestConventions::mxf_root_name_is_case_insensitive()
{
	// The lowercase spelling is the live bug this fixes: Linux-hosted
	// SMB shares and hand-restored backups carry 'mxf', the scanner
	// accepted it, and the rebalance picker didn't.
	QVERIFY(Conventions::isMxfRootName(QStringLiteral("MXF")));
	QVERIFY(Conventions::isMxfRootName(QStringLiteral("mxf")));
	QVERIFY(Conventions::isMxfRootName(QStringLiteral("Mxf")));
	QVERIFY(!Conventions::isMxfRootName(QStringLiteral("MXF2")));
	QVERIFY(!Conventions::isMxfRootName(QStringLiteral("Avid MediaFiles")));
	QVERIFY(!Conventions::isMxfRootName(QString()));
}

void TestConventions::omf_root_is_not_an_mxf_root()
{
	// The scanner scans OMF roots; the rebalancer must never treat one
	// as an MXF root. The two predicates stay distinct on purpose.

	// Avid's real OMF root: a TOP-LEVEL folder beside "Avid MediaFiles",
	// matched case-insensitively like every other folder name here.
	QVERIFY(Conventions::isOmfRootName(QStringLiteral("OMFI MediaFiles")));
	QVERIFY(Conventions::isOmfRootName(QStringLiteral("omfi mediafiles")));

	// A bare "OMF" was a prototype guess. Media Composer 25.12 writes no
	// such folder, so accepting it only ever matched hand-made ones.
	QVERIFY(!Conventions::isOmfRootName(QStringLiteral("OMF")));
	QVERIFY(!Conventions::isOmfRootName(QStringLiteral("omf")));
	// Nor is OMFI a subfolder of Avid MediaFiles, the way MXF is.
	QVERIFY(!Conventions::isOmfRootName(QStringLiteral("OMFI")));

	// The two roots never collide.
	QVERIFY(!Conventions::isMxfRootName(QStringLiteral("OMFI MediaFiles")));
	QVERIFY(!Conventions::isOmfRootName(QStringLiteral("MXF")));
}

void TestConventions::mxf_root_under_builds_canonical_path()
{
	QCOMPARE(Conventions::mxfRootUnder(QStringLiteral("/Volumes/Nexis")),
			 QStringLiteral("/Volumes/Nexis/Avid MediaFiles/MXF"));
}

void TestConventions::dot_hidden_names()
{
	QVERIFY(Conventions::isDotHidden(QStringLiteral("._clip.mxf")));
	QVERIFY(Conventions::isDotHidden(QStringLiteral(".DS_Store")));
	QVERIFY(!Conventions::isDotHidden(QStringLiteral("clip.mxf")));
	QVERIFY(!Conventions::isDotHidden(QStringLiteral("clip.with.dots.mxf")));
}

void TestConventions::mxf_extension_is_case_insensitive()
{
	QVERIFY(Conventions::hasMxfExtension(QStringLiteral("clip.mxf")));
	QVERIFY(Conventions::hasMxfExtension(QStringLiteral("CLIP.MXF")));
	QVERIFY(Conventions::hasMxfExtension(QStringLiteral("clip.Mxf")));
	// MediaFile::extension is the bare normalised ".mxf"; the predicate
	// must accept it so the Stage-2 gate can use the same rule.
	QVERIFY(Conventions::hasMxfExtension(QStringLiteral(".mxf")));
	QVERIFY(!Conventions::hasMxfExtension(QStringLiteral("clip.mxfx")));
	QVERIFY(!Conventions::hasMxfExtension(QStringLiteral("clip.mov")));
	QVERIFY(!Conventions::hasMxfExtension(QStringLiteral("mxf")));
}

void TestConventions::essence_name_combinations()
{
	QVERIFY(Conventions::countsAsEssenceName(QStringLiteral("clip.mxf")));
	QVERIFY(Conventions::countsAsEssenceName(QStringLiteral("CLIP.MXF")));
	// The Windows/SMB AppleDouble sibling: .mxf extension, still junk.
	QVERIFY(!Conventions::countsAsEssenceName(QStringLiteral("._clip.mxf")));
	QVERIFY(!Conventions::countsAsEssenceName(QStringLiteral("msmMMOB.mdb")));
	QVERIFY(!Conventions::countsAsEssenceName(QStringLiteral(".DS_Store")));
	// Replacement and backup files do not occupy the media folder budget.
	for (const QString &name : {QStringLiteral("clip.mxf.__copyreplace_ab12"),
		QStringLiteral("clip.mxf.__movereplace_ab12"), QStringLiteral("clip.omf.partial"),
		QStringLiteral("clip.wav.backup")})
		QVERIFY2(!Conventions::countsAsEssenceName(name), qPrintable(name));
}

void TestConventions::avid_media_extensions()
{
	QVERIFY(Conventions::hasAvidMediaExtension(QStringLiteral("clip.mxf")));
	QVERIFY(Conventions::hasAvidMediaExtension(QStringLiteral("legacy.OMF")));
	QVERIFY(Conventions::hasAvidMediaExtension(QStringLiteral("audio.aif")));
	QVERIFY(Conventions::hasAvidMediaExtension(QStringLiteral("audio.WAV")));
	QVERIFY(!Conventions::hasAvidMediaExtension(QStringLiteral("export.mov")));
	QVERIFY(!Conventions::hasAvidMediaExtension(QStringLiteral("msmMMOB.mdb")));
	QVERIFY(!Conventions::hasAvidMediaExtension(QStringLiteral("msmFMID.pmr")));
	QVERIFY(!Conventions::hasAvidMediaExtension(QStringLiteral("Thumbs.db")));
	QVERIFY(!Conventions::hasAvidMediaExtension(QStringLiteral("desktop.ini")));
	// The full filename must end in a supported extension.
	QVERIFY(!Conventions::hasAvidMediaExtension(
		QStringLiteral("clip.mxf.__movereplace_ab12")));
}

void TestConventions::folder_budget_thresholds_stay_ordered()
{
	// Avid's ceiling is the fixed point; everything else is ours and must
	// stay strictly below it, in warning order. A future edit that puts
	// the amber line above the red one, or lets the packing target reach
	// Avid's limit, fails here rather than in a user's project.
	static_assert(Conventions::kFolderMax == 5000, "Avid's own ceiling");
	static_assert(Conventions::kFolderTarget < Conventions::kFolderMax,
				  "packing must stop below Avid's ceiling");
	static_assert(Conventions::kFolderCritical < Conventions::kFolderTarget,
				  "red must come before the packing target");
	static_assert(Conventions::kFolderWarn < Conventions::kFolderCritical,
				  "amber must come before red");

	// The budget rule and the table rule are deliberately different: the
	// budget counts .mxf only, the table admits OMF-era audio too.
	QVERIFY(Conventions::countsAsEssenceName(QStringLiteral("clip.mxf")));
	QVERIFY(!Conventions::countsAsEssenceName(QStringLiteral("track.wav")));
	QVERIFY(AvidMediaLayout::acceptsFileName(AvidMediaLayout::Family::Omf, QStringLiteral("track.wav")));
}

// MARK: - OMF-era

void TestConventions::omf_root_constant_and_omf_root_under()
{
	// The name is Avid's, spelled once; the predicate reads the constant.
	QCOMPARE(QString(Conventions::kOmfMediaFilesDir), QStringLiteral("OMFI MediaFiles"));
	QVERIFY(Conventions::isOmfRootName(QString(Conventions::kOmfMediaFilesDir)));

	// One level, not two: the OMF folder IS the media root. Beside the
	// MXF root, never inside it.
	QCOMPARE(Conventions::omfRootUnder(QStringLiteral("/Volumes/Archive")),
			 QStringLiteral("/Volumes/Archive/OMFI MediaFiles"));
	QVERIFY(!Conventions::omfRootUnder(QStringLiteral("/v")).contains(Conventions::kAvidMediaFilesDir));
	QVERIFY(!Conventions::omfRootUnder(QStringLiteral("/v")).contains(Conventions::kMxfDir));
	QVERIFY(Conventions::mxfRootUnder(QStringLiteral("/v")) != Conventions::omfRootUnder(QStringLiteral("/v")));
}

void TestConventions::omf_era_extension_set()
{
	// The v1 legacy set: .omf audio/video, .aif/.wav audio. Case-insensitive
	// like every other extension test here.
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("slate.omf")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("SLATE.OMF")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("tone.aif")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("tone.wav")));
	// .mxf is the OTHER era; the scanner dispatches on this split.
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("clip.mxf")));
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("tone.aiff")));
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("export.mov")));
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("msmMMOB.mdb")));

	QVERIFY(!Conventions::countsAsEssenceName(QStringLiteral("slate.omf")));
}

void TestConventions::creating_folder_name_is_case_insensitive()
{
	// Avid's transient capture folder, in both eras; never media.
	QCOMPARE(QString(Conventions::kCreatingDir), QStringLiteral("Creating"));
	QVERIFY(Conventions::isCreatingFolderName(QStringLiteral("Creating")));
	QVERIFY(Conventions::isCreatingFolderName(QStringLiteral("creating")));
	QVERIFY(Conventions::isCreatingFolderName(QStringLiteral("CREATING")));
	QVERIFY(!Conventions::isCreatingFolderName(QStringLiteral("Creating2")));
	QVERIFY(!Conventions::isCreatingFolderName(QStringLiteral("1")));
	QVERIFY(!Conventions::isCreatingFolderName(QString()));
	// It is neither media root.
	QVERIFY(!Conventions::isMxfRootName(QString(Conventions::kCreatingDir)));
	QVERIFY(!Conventions::isOmfRootName(QString(Conventions::kCreatingDir)));
}

void TestConventions::system_drive_media_bases_per_platform()
{
	// Avid's fixed system-drive placement, per platform. Every entry is a
	// base under which BOTH folder names are probed, so none of them may
	// already end in a media folder name.
	const QStringList bases = Conventions::systemDriveMediaBases();
#if defined(Q_OS_MAC)
	QCOMPARE(bases, QStringList{QStringLiteral("/Users/Shared/AvidMediaComposer")});
#elif defined(Q_OS_WIN)
	QCOMPARE(bases, (QStringList{QStringLiteral("C:/Users/Public/Documents/Avid Media Composer"),
								 QStringLiteral("C:/")}));
#else
	QVERIFY(bases.isEmpty());
#endif
	for (const QString &base : bases)
	{
		QVERIFY2(!base.endsWith(Conventions::kAvidMediaFilesDir), qPrintable(base));
		QVERIFY2(!base.endsWith(Conventions::kOmfMediaFilesDir), qPrintable(base));
		QVERIFY2(!base.contains(QLatin1Char('\\')), qPrintable(base));
	}
}

void TestConventions::database_file_names_cover_both_spellings()
{
	// msm* for managed media, ama* for AMA-linked folders; the scanner
	// reads whichever are present. None of them is media.
	QCOMPARE(int(Conventions::kPmrFileNames.size()), 2);
	QCOMPARE(int(Conventions::kMdbFileNames.size()), 2);
	QCOMPARE(QString(Conventions::kPmrFileNames[0]), QStringLiteral("msmFMID.pmr"));
	QCOMPARE(QString(Conventions::kPmrFileNames[1]), QStringLiteral("amaFMID.pmr"));
	QCOMPARE(QString(Conventions::kMdbFileNames[0]), QStringLiteral("msmMMOB.mdb"));
	QCOMPARE(QString(Conventions::kMdbFileNames[1]), QStringLiteral("amaMMOB.mdb"));
	for (const QLatin1String name : Conventions::kPmrFileNames)
		for (const auto family : {AvidMediaLayout::Family::Mxf, AvidMediaLayout::Family::Omf})
			QVERIFY2(!AvidMediaLayout::acceptsFileName(family, QString(name)), name.data());
	for (const QLatin1String name : Conventions::kMdbFileNames)
		for (const auto family : {AvidMediaLayout::Family::Mxf, AvidMediaLayout::Family::Omf})
			QVERIFY2(!AvidMediaLayout::acceptsFileName(family, QString(name)), name.data());
}

void TestConventions::managed_mxf_folder_names_preserve_readable_spellings_data()
{
	QTest::addColumn<QString>("name");
	QTest::addColumn<bool>("accepted");
	QTest::addColumn<QString>("prefix");
	QTest::addColumn<QString>("digits");
	QTest::newRow("local") << QStringLiteral("1") << true << QString{} << QStringLiteral("1");
	QTest::newRow("padded-local") << QStringLiteral("001") << true << QString{} << QStringLiteral("001");
	QTest::newRow("workstation") << QStringLiteral("EditSuite.12") << true << QStringLiteral("EditSuite") << QStringLiteral("12");
	QTest::newRow("padded-workstation") << QStringLiteral("EditSuite.001") << true << QStringLiteral("EditSuite") << QStringLiteral("001");
	QTest::newRow("dotted-workstation") << QStringLiteral("edit.suite.2") << true << QStringLiteral("edit.suite") << QStringLiteral("2");
	QTest::newRow("large-number") << QStringLiteral("999999999999999999999999") << true << QString{} << QStringLiteral("999999999999999999999999");
	for (const QString &name : {QString{}, QStringLiteral("0"), QStringLiteral("000"), QStringLiteral("host.0"),
		QStringLiteral("host.000"), QStringLiteral(".1"), QStringLiteral(".host.1"), QStringLiteral("host."),
		QStringLiteral("host"), QStringLiteral("+1"), QStringLiteral("-1"), QStringLiteral(" 1"), QStringLiteral("1 "),
		QStringLiteral("١"), QStringLiteral("１"), QStringLiteral("one/1"), QStringLiteral("one\\1")})
		QTest::newRow(qPrintable(QStringLiteral("reject-%1").arg(name))) << name << false << QString{} << QString{};
}

void TestConventions::managed_mxf_folder_names_preserve_readable_spellings()
{
	QFETCH(QString, name);
	QFETCH(bool, accepted);
	QFETCH(QString, prefix);
	QFETCH(QString, digits);
	const auto result = AvidMediaLayout::parseMxfFolderName(name);
	QCOMPARE(result.has_value(), accepted);
	if (result)
	{
		QCOMPARE(result->prefix, prefix);
		QCOMPARE(result->digits, digits);
	}
}

void TestConventions::managed_roots_require_complete_component_names()
{
	QVERIFY(AvidMediaLayout::isMxfRoot(QStringLiteral("/project/Avid MediaFiles/MXF")));
	QVERIFY(AvidMediaLayout::isMxfRoot(QStringLiteral("/project/avid mediafiles/mxf/")));
	QVERIFY(AvidMediaLayout::isMxfRoot(QStringLiteral("/project/./Avid MediaFiles/other/../MXF")));
	for (const QString &path : {QStringLiteral("/project/MXF"), QStringLiteral("Avid MediaFiles/MXF"),
		QStringLiteral("/project/My Avid MediaFiles/MXF"), QStringLiteral("/project/Avid MediaFiles/MXF-backup"),
		QStringLiteral("/project/OMFI MediaFiles/Avid MediaFiles/MXF"),
		QStringLiteral("/project/omfi mediafiles/archive/Avid MediaFiles/MXF"),
		QStringLiteral("/project/Avid MediaFiles/UME/archive/Avid MediaFiles/MXF")})
		QVERIFY2(!AvidMediaLayout::isMxfRoot(path), qPrintable(path));
	QVERIFY(AvidMediaLayout::isOmfRoot(QStringLiteral("/project/OMFI MediaFiles")));
	QVERIFY(AvidMediaLayout::isOmfRoot(QStringLiteral("/project/omfi mediafiles/")));
	for (const QString &path : {QStringLiteral("/project/OMFI MediaFiles-backup"), QStringLiteral("OMFI MediaFiles"),
		QStringLiteral("/project/Avid MediaFiles/OMFI MediaFiles"),
		QStringLiteral("/project/Avid MediaFiles/MXF/1/OMFI MediaFiles"),
		QStringLiteral("/project/Avid MediaFiles/UME/OMFI MediaFiles")})
		QVERIFY2(!AvidMediaLayout::isOmfRoot(path), qPrintable(path));
}

void TestConventions::ume_paths_require_complete_component_names()
{
	for (const QString &path : {QStringLiteral("/project/Avid MediaFiles/UME"),
		QStringLiteral("/project/avid mediafiles/ume/1"),
		QStringLiteral("/project/Avid MediaFiles/UME/archive/Avid MediaFiles/MXF/1")})
		QVERIFY2(AvidMediaLayout::isInsideUmeRoot(path), qPrintable(path));
	for (const QString &path : {QStringLiteral("/project/UME/1"),
		QStringLiteral("/project/My Avid MediaFiles/UME/1"),
		QStringLiteral("/project/Avid MediaFiles/UME backup/1"),
		QStringLiteral("/project/Avid MediaFiles/UME/../MXF/1")})
		QVERIFY2(!AvidMediaLayout::isInsideUmeRoot(path), qPrintable(path));
}

void TestConventions::managed_media_folder_locations_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<bool>("accepted");
	QTest::addColumn<bool>("omf");
	QTest::addColumn<QString>("root");
	QTest::addColumn<QString>("folder");
	QTest::newRow("local-mxf") << QStringLiteral("/project/Avid MediaFiles/MXF/1") << true << false
		<< QStringLiteral("/project/Avid MediaFiles/MXF") << QStringLiteral("1");
	QTest::newRow("workstation-mxf") << QStringLiteral("/project/Avid MediaFiles/MXF/EditSuite.001/") << true << false
		<< QStringLiteral("/project/Avid MediaFiles/MXF") << QStringLiteral("EditSuite.001");
	QTest::newRow("case-preserved") << QStringLiteral("/project/avid mediafiles/mxf/./003") << true << false
		<< QStringLiteral("/project/avid mediafiles/mxf") << QStringLiteral("003");
	QTest::newRow("flat-omf") << QStringLiteral("/project/OMFI MediaFiles") << true << true
		<< QStringLiteral("/project/OMFI MediaFiles") << QStringLiteral("OMFI MediaFiles");
	QTest::newRow("workstation-omf") << QStringLiteral("/project/OMFI MediaFiles/EditSuite") << true << true
		<< QStringLiteral("/project/OMFI MediaFiles") << QStringLiteral("EditSuite");
	for (const QString &path : {QStringLiteral("/project/MXF/1"), QStringLiteral("/project/Archived session"),
		QStringLiteral("/project/Avid MediaFiles/MXF"), QStringLiteral("/project/Avid MediaFiles/MXF/arbitrary"),
		QStringLiteral("/project/Avid MediaFiles/MXF/0"), QStringLiteral("/project/Avid MediaFiles/MXF/.host.1"),
		QStringLiteral("/project/Avid MediaFiles/MXF/1/2"), QStringLiteral("/project/OMFI MediaFiles/host/child"),
		QStringLiteral("/project/OMFI MediaFiles/Creating"), QStringLiteral("/project/OMFI MediaFiles/TEMP"),
		QStringLiteral("/project/OMFI MediaFiles/Quarantine"), QStringLiteral("/project/OMFI MediaFiles/Quarantined Files"),
		QStringLiteral("/project/OMFI MediaFiles/.hidden"),
		QStringLiteral("/project/OMFI MediaFiles/Avid MediaFiles/MXF/1"),
		QStringLiteral("/project/omfi mediafiles/archive/Avid MediaFiles/MXF/host.1"),
		QStringLiteral("/project/Avid MediaFiles/UME/archive/OMFI MediaFiles/host"),
		QStringLiteral("/project/Avid MediaFiles/UME/archive/Avid MediaFiles/MXF/1")})
		QTest::newRow(qPrintable(QStringLiteral("reject-%1").arg(path))) << path << false << false << QString{} << QString{};
}

void TestConventions::managed_media_folder_locations()
{
	QFETCH(QString, path);
	QFETCH(bool, accepted);
	QFETCH(bool, omf);
	QFETCH(QString, root);
	QFETCH(QString, folder);
	const auto result = AvidMediaLayout::locateMediaFolder(path);
	QCOMPARE(result.has_value(), accepted);
	if (result)
	{
		QCOMPARE(result->family, omf ? AvidMediaLayout::Family::Omf : AvidMediaLayout::Family::Mxf);
		QCOMPARE(result->rootPath, root);
		QCOMPARE(result->folderName, folder);
	}
}

void TestConventions::managed_format_families_keep_their_own_suffixes()
{
	using AvidMediaLayout::Family;
	QVERIFY(AvidMediaLayout::acceptsFileName(Family::Mxf, QStringLiteral("clip.MXF")));
	QVERIFY(!AvidMediaLayout::acceptsFileName(Family::Omf, QStringLiteral("clip.mxf")));
	for (const QString &name : {QStringLiteral("video.OMF"), QStringLiteral("audio.aif"), QStringLiteral("audio.WAV")})
	{
		QVERIFY2(AvidMediaLayout::acceptsFileName(Family::Omf, name), qPrintable(name));
		QVERIFY2(!AvidMediaLayout::acceptsFileName(Family::Mxf, name), qPrintable(name));
	}
	for (const QString &name : {QStringLiteral("._clip.mxf"), QStringLiteral(".clip.omf"), QStringLiteral("audio.sd2"),
		QStringLiteral("audio.aiff"), QStringLiteral("msmMMOB.mdb"), QStringLiteral("nested/clip.mxf"),
		QStringLiteral("nested\\clip.wav"), QStringLiteral(".mxf"), QStringLiteral(".wav"),
		QStringLiteral("notes.txt"), QStringLiteral("sheet.xlsx"), QStringLiteral("audio.SD2"),
		QStringLiteral(".DS_Store"), QStringLiteral("Thumbs.db"), QStringLiteral("desktop.ini"),
		QStringLiteral("export.mov"), QStringLiteral("clip.mxf.__copyreplace_ab12"),
		QStringLiteral("clip.mxf.__movereplace_ab12"), QStringLiteral("clip.omf.partial"),
		QStringLiteral("clip.wav.backup")})
		for (const auto family : {Family::Mxf, Family::Omf})
			QVERIFY2(!AvidMediaLayout::acceptsFileName(family, name), qPrintable(name));
}

void TestConventions::reserved_media_folder_names_are_case_insensitive()
{
	for (const QString &name : {QStringLiteral("Creating"), QStringLiteral("temp"), QStringLiteral("QUARANTINE"),
		QStringLiteral("Quarantined Files"), QStringLiteral(".hidden")})
		QVERIFY2(AvidMediaLayout::isReservedFolderName(name), qPrintable(name));
	QVERIFY(!AvidMediaLayout::isReservedFolderName(QStringLiteral("EditSuite")));
	QVERIFY(!AvidMediaLayout::isReservedFolderName(QStringLiteral("1")));
}

QTEST_APPLESS_MAIN(TestConventions)
#include "tst_conventions.moc"
