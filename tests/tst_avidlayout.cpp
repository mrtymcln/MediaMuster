#include "avidlayout.h"

#include <QtTest>

// AvidLayout: the shared facts about Avid's folder layout and essence
// naming. These predicates are what keep the scanner, the rebalancer,
// and the copy-path builder agreeing on what counts as Avid media —
// the drift this header was extracted to end (a lowercase 'mxf' share
// scanned but wouldn't rebalance; AppleDouble "._*.mxf" siblings
// counted as media in the table but not in the folder budget).

class TestAvidLayout : public QObject
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
};

void TestAvidLayout::mxf_root_name_is_case_insensitive()
{
	// The lowercase spelling is the live bug this fixes: Linux-hosted
	// SMB shares and hand-restored backups carry 'mxf', the scanner
	// accepted it, and the rebalance picker didn't.
	QVERIFY(AvidLayout::isMxfRootName(QStringLiteral("MXF")));
	QVERIFY(AvidLayout::isMxfRootName(QStringLiteral("mxf")));
	QVERIFY(AvidLayout::isMxfRootName(QStringLiteral("Mxf")));
	QVERIFY(!AvidLayout::isMxfRootName(QStringLiteral("MXF2")));
	QVERIFY(!AvidLayout::isMxfRootName(QStringLiteral("Avid MediaFiles")));
	QVERIFY(!AvidLayout::isMxfRootName(QString()));
}

void TestAvidLayout::omf_root_is_not_an_mxf_root()
{
	// The scanner scans OMF roots; the rebalancer must never treat one
	// as an MXF root. The two predicates stay distinct on purpose.

	// Avid's real OMF root: a TOP-LEVEL folder beside "Avid MediaFiles",
	// matched case-insensitively like every other folder name here.
	QVERIFY(AvidLayout::isOmfRootName(QStringLiteral("OMFI MediaFiles")));
	QVERIFY(AvidLayout::isOmfRootName(QStringLiteral("omfi mediafiles")));

	// A bare "OMF" was a prototype guess. Media Composer 25.12 writes no
	// such folder, so accepting it only ever matched hand-made ones.
	QVERIFY(!AvidLayout::isOmfRootName(QStringLiteral("OMF")));
	QVERIFY(!AvidLayout::isOmfRootName(QStringLiteral("omf")));
	// Nor is OMFI a subfolder of Avid MediaFiles, the way MXF is.
	QVERIFY(!AvidLayout::isOmfRootName(QStringLiteral("OMFI")));

	// The two roots never collide.
	QVERIFY(!AvidLayout::isMxfRootName(QStringLiteral("OMFI MediaFiles")));
	QVERIFY(!AvidLayout::isOmfRootName(QStringLiteral("MXF")));
}

void TestAvidLayout::mxf_root_under_builds_canonical_path()
{
	QCOMPARE(AvidLayout::mxfRootUnder(QStringLiteral("/Volumes/Nexis")),
			 QStringLiteral("/Volumes/Nexis/Avid MediaFiles/MXF"));
}

void TestAvidLayout::dot_hidden_names()
{
	QVERIFY(AvidLayout::isDotHidden(QStringLiteral("._clip.mxf")));
	QVERIFY(AvidLayout::isDotHidden(QStringLiteral(".DS_Store")));
	QVERIFY(!AvidLayout::isDotHidden(QStringLiteral("clip.mxf")));
	QVERIFY(!AvidLayout::isDotHidden(QStringLiteral("clip.with.dots.mxf")));
}

void TestAvidLayout::mxf_extension_is_case_insensitive()
{
	QVERIFY(AvidLayout::hasMxfExtension(QStringLiteral("clip.mxf")));
	QVERIFY(AvidLayout::hasMxfExtension(QStringLiteral("CLIP.MXF")));
	QVERIFY(AvidLayout::hasMxfExtension(QStringLiteral("clip.Mxf")));
	// MediaFile::extension is the bare normalised ".mxf"; the predicate
	// must accept it so the Stage-2 gate can use the same rule.
	QVERIFY(AvidLayout::hasMxfExtension(QStringLiteral(".mxf")));
	QVERIFY(!AvidLayout::hasMxfExtension(QStringLiteral("clip.mxfx")));
	QVERIFY(!AvidLayout::hasMxfExtension(QStringLiteral("clip.mov")));
	QVERIFY(!AvidLayout::hasMxfExtension(QStringLiteral("mxf")));
}

void TestAvidLayout::essence_name_combinations()
{
	QVERIFY(AvidLayout::countsAsEssenceName(QStringLiteral("clip.mxf")));
	QVERIFY(AvidLayout::countsAsEssenceName(QStringLiteral("CLIP.MXF")));
	// The Windows/SMB AppleDouble sibling: .mxf extension, still junk.
	QVERIFY(!AvidLayout::countsAsEssenceName(QStringLiteral("._clip.mxf")));
	QVERIFY(!AvidLayout::countsAsEssenceName(QStringLiteral("msmMMOB.mdb")));
	QVERIFY(!AvidLayout::countsAsEssenceName(QStringLiteral(".DS_Store")));
}

void TestAvidLayout::avid_media_extensions()
{
	QVERIFY(AvidLayout::hasAvidMediaExtension(QStringLiteral("clip.mxf")));
	QVERIFY(AvidLayout::hasAvidMediaExtension(QStringLiteral("legacy.OMF")));
	QVERIFY(AvidLayout::hasAvidMediaExtension(QStringLiteral("audio.aif")));
	QVERIFY(AvidLayout::hasAvidMediaExtension(QStringLiteral("audio.WAV")));
	QVERIFY(!AvidLayout::hasAvidMediaExtension(QStringLiteral("export.mov")));
	QVERIFY(!AvidLayout::hasAvidMediaExtension(QStringLiteral("msmMMOB.mdb")));
	QVERIFY(!AvidLayout::hasAvidMediaExtension(QStringLiteral("msmFMID.pmr")));
	QVERIFY(!AvidLayout::hasAvidMediaExtension(QStringLiteral("Thumbs.db")));
	QVERIFY(!AvidLayout::hasAvidMediaExtension(QStringLiteral("desktop.ini")));
	// Raw extension test sees only the temp suffix; stripping is
	// isAvidMediaName's job (below).
	QVERIFY(!AvidLayout::hasAvidMediaExtension(
		QStringLiteral("clip.mxf.__movereplace_ab12")));
}

void TestAvidLayout::temp_renamed_media_is_still_media()
{
	// A parked file is the user's own media wearing a suffix one of our
	// operations gave it. If a crash strands it, the table is where the
	// user finds it — so it must never be filtered out.
	const QString uuid = QStringLiteral("9f2c1d3e-0000-4000-8000-abcdefabcdef");
	for (const QLatin1String tag : {AvidLayout::kCopyReplaceTag, AvidLayout::kMoveReplaceTag})
	{
		const QString parked = QStringLiteral("Clip.mxf") + tag + uuid;
		QVERIFY2(AvidLayout::isAvidMediaName(parked), qPrintable(parked));
		QCOMPARE(AvidLayout::withoutTempSuffix(parked).toString(), QStringLiteral("Clip.mxf"));
	}

	// A name that is ONLY a marker has no media base — still hidden.
	QVERIFY(!AvidLayout::isAvidMediaName(
		QStringLiteral(".__movereplace_9f2c1d3e-0000-4000-8000-abcdefabcdef")));
	// Stripping must not rescue a non-media file.
	QVERIFY(!AvidLayout::isAvidMediaName(QStringLiteral("notes.txt.__copyreplace_ab12")));
	// An ordinary name passes through untouched.
	QCOMPARE(AvidLayout::withoutTempSuffix(QStringLiteral("Clip.mxf")).toString(),
			 QStringLiteral("Clip.mxf"));
}

void TestAvidLayout::avid_media_name_is_the_table_rule()
{
	QVERIFY(AvidLayout::isAvidMediaName(QStringLiteral("clip.mxf")));
	QVERIFY(AvidLayout::isAvidMediaName(QStringLiteral("legacy.omf")));
	QVERIFY(AvidLayout::isAvidMediaName(QStringLiteral("tone.wav")));
	// AppleDouble twins carry a media extension but are junk.
	QVERIFY(!AvidLayout::isAvidMediaName(QStringLiteral("._clip.mxf")));
	QVERIFY(!AvidLayout::isAvidMediaName(QStringLiteral(".DS_Store")));
	QVERIFY(!AvidLayout::isAvidMediaName(QStringLiteral("export.mov")));
}

QTEST_APPLESS_MAIN(TestAvidLayout)
#include "tst_avidlayout.moc"
