#include "conventions.h"

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
	void avid_media_name_is_the_table_rule();
	void temp_renamed_media_is_still_media();
	void folder_budget_thresholds_stay_ordered();

	// MARK: - OMF-era
	void omf_root_constant_and_omf_root_under();
	void omf_era_extension_set();
	void creating_folder_name_is_case_insensitive();
	void system_drive_media_bases_per_platform();
	void database_file_names_cover_both_spellings();
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
	// Raw extension test sees only the temp suffix; stripping is
	// isAvidMediaName's job (below).
	QVERIFY(!Conventions::hasAvidMediaExtension(
		QStringLiteral("clip.mxf.__movereplace_ab12")));
}

void TestConventions::temp_renamed_media_is_still_media()
{
	// A parked file is the user's own media wearing a suffix one of our
	// operations gave it. If a crash strands it, the table is where the
	// user finds it — so it must never be filtered out.
	const QString uuid = QStringLiteral("9f2c1d3e-0000-4000-8000-abcdefabcdef");
	for (const QLatin1String tag : {Conventions::kCopyReplaceTag, Conventions::kMoveReplaceTag})
	{
		const QString parked = QStringLiteral("Clip.mxf") + tag + uuid;
		QVERIFY2(Conventions::isAvidMediaName(parked), qPrintable(parked));
		QCOMPARE(Conventions::withoutTempSuffix(parked).toString(), QStringLiteral("Clip.mxf"));
	}

	// A name that is ONLY a marker has no media base — still hidden.
	QVERIFY(!Conventions::isAvidMediaName(
		QStringLiteral(".__movereplace_9f2c1d3e-0000-4000-8000-abcdefabcdef")));
	// Stripping must not rescue a non-media file.
	QVERIFY(!Conventions::isAvidMediaName(QStringLiteral("notes.txt.__copyreplace_ab12")));
	// An ordinary name passes through untouched.
	QCOMPARE(Conventions::withoutTempSuffix(QStringLiteral("Clip.mxf")).toString(),
			 QStringLiteral("Clip.mxf"));
}

void TestConventions::avid_media_name_is_the_table_rule()
{
	QVERIFY(Conventions::isAvidMediaName(QStringLiteral("clip.mxf")));
	QVERIFY(Conventions::isAvidMediaName(QStringLiteral("legacy.omf")));
	QVERIFY(Conventions::isAvidMediaName(QStringLiteral("tone.wav")));
	// AppleDouble twins carry a media extension but are junk.
	QVERIFY(!Conventions::isAvidMediaName(QStringLiteral("._clip.mxf")));
	QVERIFY(!Conventions::isAvidMediaName(QStringLiteral(".DS_Store")));
	QVERIFY(!Conventions::isAvidMediaName(QStringLiteral("export.mov")));
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
	QVERIFY(Conventions::isAvidMediaName(QStringLiteral("track.wav")));
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
	// The legacy set: .omf video, .aif/.wav/.sd2 audio. Case-insensitive
	// like every other extension test here.
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("slate.omf")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("SLATE.OMF")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("tone.aif")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("tone.wav")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("tone.sd2")));
	QVERIFY(Conventions::hasOmfEraExtension(QStringLiteral("tone.SD2")));
	// .mxf is the OTHER era; the scanner dispatches on this split.
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("clip.mxf")));
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("tone.aiff")));
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("export.mov")));
	QVERIFY(!Conventions::hasOmfEraExtension(QStringLiteral("msmMMOB.mdb")));

	// And the umbrella admits the whole legacy set, .sd2 included, while
	// the folder budget still counts .mxf only.
	QVERIFY(Conventions::hasAvidMediaExtension(QStringLiteral("tone.sd2")));
	QVERIFY(Conventions::isAvidMediaName(QStringLiteral("tone.sd2")));
	QVERIFY(!Conventions::isAvidMediaName(QStringLiteral("._tone.sd2")));
	QVERIFY(!Conventions::countsAsEssenceName(QStringLiteral("tone.sd2")));
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
		QVERIFY2(!Conventions::isAvidMediaName(QString(name)), name.data());
	for (const QLatin1String name : Conventions::kMdbFileNames)
		QVERIFY2(!Conventions::isAvidMediaName(QString(name)), name.data());
}

QTEST_APPLESS_MAIN(TestConventions)
#include "tst_conventions.moc"
