// Drives MediaScanner against a fake Avid layout built from real
// PMR/MDB/MXF fixtures. Covers Stage 1 + 2 + the join.

#include "conventions.h"
#include "mediafile.h"
#include "testpause.h"
#include "mediascanner.h"
#include "mdbparser.h"
#include "mobid.h"
#include "mxfparser.h"
#include "omfparser.h"
#include "omfuid.h"
#include "testbento.h"
#include "testomf.h"
#include "pmrparser.h"

#include "testutil.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestScanner : public QObject
{
	Q_OBJECT
private slots:
	void scans_folder_with_pmr_mdb_and_audio_mxf();
	void unreferenced_mxf_recovered_via_mdb();
	void stage3_mdb_name_must_not_clobber_material_name();
	void mxf_without_any_database_is_no_database();
	void wav_with_readable_dbs_is_no_reference();
	void corrupt_pmr_flags_no_database_and_mdb_still_recovers();
	void appledouble_sibling_is_never_media();
	void non_avid_files_are_invisible();
	void cancelled_scan_does_not_leak_databases_into_the_next();

	// This real effect render carries private MobAppCode 1 and the standard
	// LowerLevel UID in AAF byte order. Its name cannot decide classification.
	void effect_render_names_classify_as_precompute();

	// The clip-name ladder (user ruling 2026-08-14): MaterialPackage name,
	// else the MDB record's own name, else BLANK.
	// The filename is no longer a rung — it is a fact about the disk, not a
	// name Avid gave the clip.
	//
	// This reverses the 2026-07-30 ruling, and only because the thing being
	// ruled on changed: that ruling was made against the old parser, which
	// mined names out of a ±1 KB window and picked a neighbouring clip ~18%
	// of the time. The parser now walks the database's real record framing
	// and measured 360/360 exact on a real folder.
	void mdb_name_fills_in_when_the_mxf_has_none();
	void unknown_clip_name_is_blank_not_the_filename();

	// An MXF SourcePackage name is NOT a rung. It names what the media came
	// from — the imported file, or the tape — and measured across 1212 real
	// files it was the source filename on 1191 of them. It reaches the app as
	// sourceFileName; letting it into the Clip Name column would just put a
	// filename back where the ruling removed one.
	void source_package_name_is_not_a_clip_name();

	// Classification follows master usage metadata, never filename/clip-name
	// shapes. This mixdown has an ordinary MaterialPackage with neither usage
	// property, so it comes back as Media even though its name resembles an
	// effect. A header that cannot be parsed yields no verdict: Unknown.
	void mixdown_is_media_not_precompute();
	void unreadable_header_stays_unknown();

	// Database-first (2026-08-22). A row the folder's PMR + MDB fully
	// describe takes every technical fact from them and its file is never
	// opened; the header pass handles only what they don't cover — no PMR
	// entry, an incomplete MDB record (MPEG audio), a file changed since
	// Avid indexed it (mtime ≠ the PMR's trailer), no databases at all, or
	// unreadable databases. Database editorial details stay
	// usable until a parsed header establishes a contradictory identity.
	void database_described_row_never_reads_its_header();
	void stale_header_and_current_database_agree();
	void changed_file_falls_back_to_its_header();
	void zero_pmr_timestamp_forces_header_and_keeps_kind_and_type_unknown();
	void current_render_with_missing_project_survives_failed_header_read();
	void reused_filename_clears_old_editorial_details();
	void pmr_v1_recovers_unique_master_from_mdb();
	void omf2_header_keeps_master_identity_with_unknown_classification();
	void mxf_header_keeps_master_identity_with_unknown_classification();
	void folder_without_databases_reads_every_header();
	void mpeg_audio_falls_back_to_its_header();

	// New facts: the render's effect name/category/sequence, and the
	// file's modified time.
	void precompute_row_gets_effect_fields();
	void modified_is_the_filesystem_mtime();

	// OMF-era (2026-09-02). The legacy "OMFI MediaFiles" root is a FLAT
	// sibling of Avid MediaFiles with one database pair beside the media,
	// and volume scans follow Avid's placement rule (drive root + the fixed
	// system-drive bases) and look nowhere deeper; the two-level search
	// survives only for folders added by hand (Options::manualPaths).
	void omf_volume_root_scans_both_folders();
	void omf_volume_scan_stops_at_the_root_but_a_manual_path_goes_deeper();
	void omf_root_pointed_at_directly_never_scans_as_mxf_folders();
	void omf_root_without_a_pmr_gets_identity_from_its_header();
	void omf_video_rows_show_avid_short_names();
	void omf_folder_with_any_name_is_recognised_by_its_databases();
	void omf_folder_is_recognised_by_its_mdb_alone();
	void creating_folder_is_skipped_under_mxf_and_omfi();
	void ama_databases_are_read();

	// What the OMF-era rework must NOT have changed for MXF-era media: a
	// hand-added path of any shape still resolves to its MXF root (and a
	// derived ".../Avid MediaFiles" is a folder, never a drive); stray
	// audio in a numbered folder is listed and never opened; an unreadable
	// ama* twin beside a readable msm* pair does not fail the folder.
	void manual_path_inside_avid_mediafiles_resolves_to_its_mxf_root();
	void overlapping_volume_and_manual_roots_scan_each_folder_once();
	void case_distinct_shared_folders_remain_distinct();
	void stray_audio_in_an_mxf_folder_is_listed_but_never_opened();
	void stray_omf_in_an_mxf_folder_is_opened();
	void unreadable_ama_twin_does_not_mark_the_folder_unreadable();

private:
	static QString fixturesDir() { return QStringLiteral(FIXTURES_DIR); }
	static void copyFixture(const QString &name, const QString &destFolder);
	/// Stamp a file's modified time (Unix seconds) — the PMR's trailer for a
	/// fixture, so the scanner's staleness guard sees "still the file Avid
	/// indexed" rather than a fresh copy.
	static void setModified(const QString &path, quint32 secs);
	static QByteArray writeJunk(const QString &path, int size);
};

void TestScanner::copyFixture(const QString &name, const QString &destFolder)
{
	// `name` may carry a subdirectory (corpus_headers/...); the copy always
	// lands flat in the scanned folder.
	const QString src = fixturesDir() + QLatin1Char('/') + name;
	const QString dst = destFolder + QLatin1Char('/') + QFileInfo(name).fileName();
	QVERIFY2(QFile::copy(src, dst),
			 qPrintable(QStringLiteral("failed to copy %1 → %2").arg(src, dst)));
}

void TestScanner::setModified(const QString &path, quint32 secs)
{
	QFile f(path);
	QVERIFY2(f.open(QIODevice::ReadWrite), qPrintable(path));
	QVERIFY2(f.setFileTime(QDateTime::fromSecsSinceEpoch(secs), QFileDevice::FileModificationTime), qPrintable(path));
}

QByteArray TestScanner::writeJunk(const QString &path, int size)
{
	const QByteArray bytes(size, '\x11');
	QFile f(path);
	if (f.open(QIODevice::WriteOnly))
	{
		f.write(bytes);
		f.close();
	}
	return bytes;
}

void TestScanner::scans_folder_with_pmr_mdb_and_audio_mxf()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);

	// The supplied fixture contains just the metadata prefix.
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");
	QCOMPARE(finishedSpy.size(), 1);

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);

	const MediaFile &mf = results.first();

	QCOMPARE(mf.fileName, QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"));
	QCOMPARE(mf.mxfFolder, QStringLiteral("1"));
	QCOMPARE(mf.extension, QStringLiteral(".mxf"));
	QVERIFY(mf.sizeBytes > 0);

	// From PMR
	QCOMPARE(mf.project, QStringLiteral("block 1729"));
	QCOMPARE(mf.mobId, QStringLiteral("060a2b3401010105.01010f1013000000."
									  "4a507dea74110690.7a361e6a605d3613"));
	QCOMPARE(mf.masterMobId, QStringLiteral("060a2b3401010105.01010f1013000000."
												 "d2467dea74110690.91901e6a605d3613"));

	// From the MXF MaterialPackage (it outranks the MDB's name on the ladder)
	QCOMPARE(mf.clipName, QStringLiteral("TONE: 1000 Hz @ -14.0 dB.1"));

	// Multi-script bin name; exercises mdbparser's UTF-8 path.
	// Raw bytes so source-file encoding can't drift the assertion.
	const QString expectedBin = QString::fromUtf8("No\xCC\x88n English bin na\xCC\x81me\xE2\x84\xA2"
												  " \xE4\xBD\xA0\xE5\xA5\xBD \xE6\xBC\xA2");
	QCOMPARE(mf.originalBin, expectedBin);

	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::Listed);
	QVERIFY(!mf.hasNoProject());
	QVERIFY(!mf.isInvalidUmid);
	QVERIFY(!mf.isNonPortable);

	// From MXF (Stage 2)
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QVERIFY(!mf.codec.isEmpty());
	QVERIFY(mf.sampleRate > 0);
	QVERIFY(mf.channels > 0);

	// Audio rows leave Resolution and FPS blank — those columns are video
	// facts. (An early prototype filled them with an em-dash and the sample
	// rate; both ideas were dropped, 23 July 2026.)
	QVERIFY2(mf.resolution.isEmpty(), qPrintable(mf.resolution));
	QVERIFY2(mf.fps.isEmpty(), qPrintable(mf.fps));
}

// An unattributed MXF — no PMR entry — whose UMID is present in the folder's
// MDB is rescued by Stage 3 (recoverUnreferencedFromMdb), which re-joins it via
// its UMID. This is the Interplay case, where management lives in Interplay's
// engine rather than the local PMR. Withholding msmFMID.pmr leaves the file
// unattributed after Stage 1, so the MDB alone must flip it back — and the
// lookup runs through the memoised folder-key path.
void TestScanner::unreferenced_mxf_recovered_via_mdb()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// MDB + MXF, but deliberately NO msmFMID.pmr, so the file is unattributed
	// after Stage 1 and only Stage 3 can recover it.
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);

	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");
	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	// Recovery discriminators. NB: clipName is NOT one — the header pass reads
	// it from the MXF MaterialPackage, so it is set with or without the MDB
	// (the no-mdb partner test confirms that). The recovery signal is the
	// original bin, which this MXF header does not store. The project
	// comes from the file's own header (the same `_PJ` Avid reads when it
	// rebuilds a PMR), and the status stays honest: there is no PMR here, so
	// the folder has no index to list the file in.
	QVERIFY(!mf.isInvalidUmid); // a good UMID is what the lookup keys on
	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
	QCOMPARE(mf.project, QStringLiteral("block 1729"));
	const QString adoptedMob =
		QStringLiteral("060a2b3401010105.01010f1013000000.d2467dea74110690.91901e6a605d3613");
	QCOMPARE(mf.masterMobId, adoptedMob);
	QCOMPARE(mf.mobId, QStringLiteral("060a2b3401010105.01010f1013000000.4a507dea74110690.7a361e6a605d3613"));
	QVERIFY(!mf.originalBin.isEmpty());
}

// The clip-name ladder (MediaFile::ClipNameSource), enforced on the Stage 3
// path. The MXF MaterialPackage name outranks the MDB's name. Stage 1's
// ordering already guarantees the material name wins for PMR-attributed
// files, but Stage 3 (UMID rescue) merges the MDB AFTER Stage 2 has set the
// material name — so under last-writer-wins the MDB name used to clobber
// the authoritative one. This synthetic MDB holds a deliberately wrong name
// for the tone's MOB; the row must keep the name embedded in the media file.
void TestScanner::stage3_mdb_name_must_not_clobber_material_name()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// No msmFMID.pmr, so Stage 1 can't attribute the file and Stage 3 must.
	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);

	// A one-clip MDB: the tone's MOB (PMR/MDB byte order — what
	// MobId::toPmrForm(umid) resolves to) as a master mob carrying a WRONG
	// clip name.
	BentoBuilder w;
	const quint32 master = w.addObject("MOBJ");
	w.set(master, "OMFI:MOBJ:MobID",
		  QByteArray::fromHex("060a2b340101010501010f1013000000d2467dea7411069091901e6a605d3613"));
	w.setString(master, "OMFI:CPNT:Name", "Wrong Neighbour Clip");
	w.setU32(master, "OMFI:MOBJ:UsageCode", 7);
	const QByteArray mdb = w.build();

	QFile mdbFile(folder + QStringLiteral("/msmMMOB.mdb"));
	QVERIFY(mdbFile.open(QIODevice::WriteOnly));
	QCOMPARE(mdbFile.write(mdb), qint64(mdb.size()));
	mdbFile.close();

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);

	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");
	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	// The re-join did run — the MDB's master MOB was adopted (no PMR here,
	// so the status honestly says the folder has no index)...
	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
	QCOMPARE(mf.project, QStringLiteral("block 1729")); // from the file's own header
	QCOMPARE(mf.masterMobId, QStringLiteral("060a2b3401010105.01010f1013000000."
											"d2467dea74110690.91901e6a605d3613"));

	// ...but the clip name stayed with the media file's own MaterialPackage
	// name. Before the fix this read "Wrong Neighbour Clip". Still true under
	// the 2026-08-14 ladder for a different reason: the MDB name is applied
	// now, but it ranks below a MaterialPackage name and this file has one.
	QCOMPARE(mf.clipName, QStringLiteral("TONE: 1000 Hz @ -14.0 dB.1"));
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
}

// Partner to unreferenced_mxf_recovered_via_mdb, with NO databases in the
// folder at all. An absent index can't verify a miss, so the app must not
// claim "No reference" — the honest state is "No database" (nothing to check
// against); the project still comes from the file's own header, and the
// re-join has nothing to recover against.
void TestScanner::mxf_without_any_database_is_no_database()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// MXF only — no msmFMID.pmr, no msmMMOB.mdb.
	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);

	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");
	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase); // absent index can't verify a miss
	QCOMPARE(mf.project, QStringLiteral("block 1729"));		// the file's own header still names it
	QCOMPARE(mf.mobId, QStringLiteral("060a2b3401010105.01010f1013000000.4a507dea74110690.7a361e6a605d3613"));
	QCOMPARE(mf.masterMobId, QStringLiteral("060a2b3401010105.01010f1013000000.d2467dea74110690.91901e6a605d3613"));
	QVERIFY(mf.originalBin.isEmpty());
}

// The verified-miss case: the folder HAS readable databases and the file is in
// neither (a .wav has no UMID, so Stage 3 can't rescue it either). This is the
// only combination allowed to claim "No reference".
void TestScanner::wav_with_readable_dbs_is_no_reference()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);

	QFile wav(folder + QStringLiteral("/tone.wav"));
	QVERIFY(wav.open(QIODevice::WriteOnly));
	wav.write("RIFF----WAVEfmt ");
	wav.close();

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);

	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");
	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoReference); // PMR readable, file not in it
	QVERIFY(mf.hasNoProject()); // a .wav has no header to name one either
	QCOMPARE(mf.projectDisplay(), QStringLiteral("No project"));
}

// The live corrupt-PMR case (seen in the field as a PMR whose first record
// lacked the Avid MOB prefix): unmatched files in that folder must surface as
// "No database", not as a verified "No reference" — but a readable MDB can
// still vouch for files it contains, so Stage 3 recovery keeps working.
void TestScanner::corrupt_pmr_flags_no_database_and_mdb_still_recovers()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// A PMR too small to be real: exists, fails to parse -> Unreadable.
	QFile junkPmr(folder + QStringLiteral("/msmFMID.pmr"));
	QVERIFY(junkPmr.open(QIODevice::WriteOnly));
	junkPmr.write("JUNKJUNKJUNKJUNK");
	junkPmr.close();

	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);

	QFile wav(folder + QStringLiteral("/tone.wav"));
	QVERIFY(wav.open(QIODevice::WriteOnly));
	wav.write("RIFF----WAVEfmt ");
	wav.close();

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);

	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");
	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 2);

	const auto findByName = [&results](const QString &name) -> const MediaFile *
	{
		for (const MediaFile &f : results)
			if (f.fileName == name)
				return &f;
		return nullptr;
	};

	// The MXF's UMID is in the readable MDB: identity recovered despite the
	// dead PMR, the project read from the file's own header — and the status
	// keeps the fact that this folder's index could not be read.
	const MediaFile *mxf = findByName(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"));
	QVERIFY(mxf != nullptr);
	QCOMPARE(mxf->dbStatus, MediaFile::DbStatus::DbUnreadable);
	QCOMPARE(mxf->project, QStringLiteral("block 1729"));
	QVERIFY(!mxf->masterMobId.isEmpty()); // the re-join found the clip

	// The wav has no UMID and no header; nothing can vouch for it while the
	// PMR is dead, so it must NOT be claimed as a verified miss.
	const MediaFile *wavFile = findByName(QStringLiteral("tone.wav"));
	QVERIFY(wavFile != nullptr);
	QCOMPARE(wavFile->dbStatus, MediaFile::DbStatus::DbUnreadable);
	QVERIFY(wavFile->hasNoProject());
}

void TestScanner::effect_render_names_classify_as_precompute()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// Real MC 2025 render header (512 KB slice); no databases needed — the
	// verdict comes from the MXF UsageCode. This fixture earns its keep twice:
	// it is the ONE file of 823 that stores the UsageCode UL with its two
	// 8-byte halves swapped (the AAF AUID form), so it also pins the fact that
	// isPrecomputeUsage accepts both byte orders.
	copyFixture(QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seqDD866C6BV.mxf"), folder);

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	QCOMPARE(results.first().clipName,
			 QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq,1.85_Mask+2"));
	QCOMPARE(results.first().type, MediaFile::Type::Precompute);
}

namespace
{
	const QByteArray kLadderMob = QByteArray::fromHex("060a2b340101010501010f1013000000"
													  "d2467dea7411069091901e6a605d3613");

	/// A PMR attributing `fileName` to kLadderMob, so Stage 1 joins the file
	/// to the MDB record. LE header + FILE + MASTER.
	QByteArray ladderPmr(const QByteArray &fileName)
	{
		QByteArray pmr;
		auto u16le = [&pmr](quint16 v)
		{
			pmr.append(char(v & 0xff));
			pmr.append(char((v >> 8) & 0xff));
		};
		auto u32le = [&pmr](quint32 v)
		{
			for (int i = 0; i < 4; ++i)
				pmr.append(char((v >> (8 * i)) & 0xff));
		};
		u32le(0x000007A9); // MAGIC — Avid checks this before the version
		u32le(8);		   // VERSION
		u32le(1);		   // numMobs
		pmr.append(kLadderMob);
		u16le(quint16(fileName.size()));
		pmr.append(fileName);
		u16le(8);
		pmr.append("tst_proj", 8);
		pmr.append(kLadderMob);
		pmr.append(QByteArray(4, '\0'));
		return pmr;
	}

	/// An MDB holding one master clip for kLadderMob, built to the real
	/// Bento/OMF shape (tests/testbento.h): a MOBJ with the MobID, the clip
	/// name and a master-clip usage code.
	QByteArray ladderMdb(const QByteArray &clipName)
	{
		BentoBuilder w;
		const quint32 master = w.addObject("MOBJ");
		w.set(master, "OMFI:MOBJ:MobID", kLadderMob);
		w.setString(master, "OMFI:CPNT:Name", clipName);
		w.setU32(master, "OMFI:MOBJ:UsageCode", 7);
		return w.build();
	}

	/// One MXF metadata set: `setType` 0x36 = MaterialPackage, 0x37 =
	/// SourcePackage. Local tag 0x4401 is the package UMID, 0x4402 the name
	/// (UTF-16BE). Mirrors the builder in tst_mxfparser.cpp.
	QByteArray packageSet(quint8 setType, const QByteArray &umid32, const QString &name)
	{
		const auto u16be = [](quint16 v)
		{
			QByteArray b;
			b.append(char((v >> 8) & 0xff));
			b.append(char(v & 0xff));
			return b;
		};
		QByteArray nameBytes;
		for (QChar c : name)
		{
			nameBytes.append(char((c.unicode() >> 8) & 0xff));
			nameBytes.append(char(c.unicode() & 0xff));
		}
		QByteArray value = QByteArray::fromHex("4401") + u16be(32) + umid32;
		value += QByteArray::fromHex("4402") + u16be(quint16(nameBytes.size())) + nameBytes;

		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101");
		key.append(char(0x01));		 // byte[13]
		key.append(char(setType));	 // byte[14] — the set type
		key.append(char(0x00));		 // byte[15]
		QByteArray out = key;
		out.append(char(value.size())); // BER short form
		out += value;
		return out;
	}

	/// A CDCI picture descriptor carrying stored width/height. Without one the
	/// parse yields MxfMetadata::valid == false and applyMxfMetadata skips
	/// everything, so a package-only buffer would prove nothing about naming.
	QByteArray cdciSet(quint32 width, quint32 height)
	{
		const auto u16be = [](quint16 v)
		{
			QByteArray b;
			b.append(char((v >> 8) & 0xff));
			b.append(char(v & 0xff));
			return b;
		};
		const auto u32be = [](quint32 v)
		{
			QByteArray b;
			for (int i = 3; i >= 0; --i)
				b.append(char((v >> (8 * i)) & 0xff));
			return b;
		};
		QByteArray value = u16be(0x3203) + u16be(4) + u32be(width);
		value += u16be(0x3202) + u16be(4) + u32be(height);

		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101");
		key.append(char(0x01));
		key.append(char(0x28)); // CDCI descriptor set type
		key.append(char(0x00));
		QByteArray out = key;
		out.append(char(value.size()));
		out += value;
		return out;
	}
} // namespace

void TestScanner::mdb_name_fills_in_when_the_mxf_has_none()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), ladderPmr("garbage.mxf")));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"), ladderMdb("Interview Take 3")));

	// The media file itself is unparseable, so no MaterialPackage name will
	// ever arrive — the case that used to fall back to the filename.
	QVERIFY(tryWriteFile(folder + QStringLiteral("/garbage.mxf"), QByteArray(2048, '\x11')));

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	// The join happened (project from PMR)...
	QCOMPARE(mf.project, QStringLiteral("tst_proj"));
	// ...and the database supplied the name. This read "garbage" — the
	// filename base — until 2026-08-14.
	QCOMPARE(mf.clipName, QStringLiteral("Interview Take 3"));
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::Mdb);
}

void TestScanner::unknown_clip_name_is_blank_not_the_filename()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// Same unparseable file, and a PMR so the row is still attributed — but
	// an MDB whose only record names a DIFFERENT MOB, so nothing knows this
	// file's name.
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), ladderPmr("garbage.mxf")));
	QByteArray otherMdb = ladderMdb("Some Other Clip");
	otherMdb.replace(kLadderMob, QByteArray::fromHex("060a2b340101010501010f1013000000"
													 "aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbb"));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"), otherMdb));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/garbage.mxf"), QByteArray(2048, '\x11')));

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	QCOMPARE(mf.project, QStringLiteral("tst_proj"));
	// Nothing knew the name, so the cell stays blank. The filename is a fact
	// about the disk, not a name Avid gave the clip.
	QVERIFY2(mf.clipName.isEmpty(), qPrintable("clip name invented: " + mf.clipName));
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::None);
	// And the display string agrees — no fallback hiding behind the column.
	QVERIFY(mf.clipNameDisplay().isEmpty());
}

void TestScanner::source_package_name_is_not_a_clip_name()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// An MXF whose ONLY named package is a SourcePackage — a tape name. The
	// parser reads it (and flags it non-material); the scanner must not take
	// it. No databases either, so nothing else can answer.
	const QByteArray srcUmid = QByteArray::fromHex("060a2b340101010501010f1013000000"
												   "3333333333333333"
												   "4444444444444444");
	// The descriptor is what makes the parse `valid`; without it
	// applyMxfMetadata skips every field and the test would pass for the
	// wrong reason. Stage 2 also skips files under 1 KB, so pad past that —
	// trailing zeros read as empty KLV keys and the walk still terminates.
	QByteArray mxf = cdciSet(1920, 1080);
	mxf += packageSet(0x37, srcUmid, QStringLiteral("7302108SL"));
	mxf.append(QByteArray(2048 - mxf.size(), '\0'));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/tape.mxf"), mxf));

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	QVERIFY2(mf.clipName != QStringLiteral("7302108SL"),
			 "a SourcePackage (tape) name was adopted as the clip name");
	QVERIFY2(mf.clipName.isEmpty(), qPrintable("clip name invented: " + mf.clipName));
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::None);
}

void TestScanner::mixdown_is_media_not_precompute()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// A real Video Mixdown header. Its clip name reads exactly like a render
	// ("Untitled Sequence.05,Video Mixdown,1") and the old keyword rule called
	// it a Precompute — via 'title' inside "Untitled". But a mixdown is a
	// master clip with its own media, so Avid writes it NO UsageCode, and the
	// structural rule gets it right with no special case.
	copyFixture(QStringLiteral("corpus_headers/Untitled Sequence.175B1728V.mxf"), folder);

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	QCOMPARE(mf.clipName, QStringLiteral("Untitled Sequence.05,Video Mixdown,1"));
	QCOMPARE(mf.type, MediaFile::Type::Media);
}

void TestScanner::unreadable_header_stays_unknown()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// A render-shaped FILENAME on a file whose header is unreadable. Only the
	// metadata can classify, and there is none to read.
	QVERIFY(tryWriteFile(folder + QStringLiteral("/Untitled_Sequence.0B4A3F9FV.mxf"),
					  QByteArray(2048, '\x11')));

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	QVERIFY(mf.clipName.isEmpty());
	QCOMPARE(mf.type, MediaFile::Type::Unknown);
	QCOMPARE(mf.kind, MediaFile::Kind::Unknown);
}

void TestScanner::appledouble_sibling_is_never_media()
{
	// macOS writing to SMB leaves an AppleDouble "._clip.mxf" beside every
	// real clip. Unix enumeration hides dotfiles, so this is trivially
	// green on macOS — its teeth are on Windows CI, where the sibling IS
	// enumerated and only the scanner's Conventions::isDotHidden skip keeps
	// it out of the table, the counts, and Stage 2's MXF parse.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);
	QFile junk(folder + QStringLiteral("/._TONE_100A01.EA7D504A.611740.mxf"));
	QVERIFY(junk.open(QIODevice::WriteOnly));
	junk.write(QByteArray(4096, '\0')); // resource-fork noise, not an MXF
	junk.close();

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	QCOMPARE(results.first().fileName, QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"));
}

void TestScanner::non_avid_files_are_invisible()
{
	// The table shows only Avid media (.mxf/.omf + the OMF era's
	// .aif/.wav — user ruling 2026-08-12). Stray exports, notes, and OS
	// junk never become rows, never count, never get operated on.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);
	for (const char *stray : {"export.mov", "notes.txt", "Thumbs.db", "desktop.ini"})
	{
		QFile f(folder + QLatin1Char('/') + QLatin1String(stray));
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write("not avid media", 14);
		f.close();
	}

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);
	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");

	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	QCOMPARE(results.first().fileName, QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"));
}

void TestScanner::cancelled_scan_does_not_leak_databases_into_the_next()
{
	// The genuine failure: only the NORMAL scan exit used to clear the
	// cached MDB maps, so a CANCELLED scan left them behind and the next
	// scan attributed files from a database that no longer exists on
	// disk. A rescan after a *completed* scan proves nothing here — the
	// normal exit always cleared — so this test cancels.
	//
	// Timing: processFolderTask inserts the folder's parsed MDB into the
	// cache and THEN hits TestPause's 4-second pause, so cancelling
	// anywhere inside that window is what we need. 500 ms sits deep
	// inside 4000 ms; the only work that has to finish first is reading
	// three small fixture files.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"), folder);

	MediaScanner scanner;
	MediaScanner::Options opts;
	opts.volumePaths = QStringList{tmp.path()};

	{
		TestPause::setEnabled(true);
		QSignalSpy spy(&scanner, &MediaScanner::scanFinished);
		scanner.startScan(opts);
		QTest::qWait(500);
		scanner.cancelScan();
		QVERIFY2(spy.wait(20000), "cancelled scan did not finish");
		TestPause::setEnabled(false);
	}

	// The databases are gone by the time the user rescans.
	QVERIFY(QFile::remove(folder + QStringLiteral("/msmFMID.pmr")));
	QVERIFY(QFile::remove(folder + QStringLiteral("/msmMMOB.mdb")));

	{
		QSignalSpy spy(&scanner, &MediaScanner::scanFinished);
		scanner.startScan(opts);
		QVERIFY2(spy.wait(20000), "rescan did not finish");
		const auto results = spy.takeFirst().at(0).value<QVector<MediaFile>>();
		QCOMPARE(results.size(), 1);
		// Without the clear, the header pass re-joins this file against the
		// cancelled scan's cached MDB and adopts its editorial bin.
		QCOMPARE(results.first().dbStatus, MediaFile::DbStatus::NoDatabase);
		QVERIFY2(results.first().originalBin.isEmpty(), qPrintable(results.first().originalBin));
		QCOMPARE(results.first().masterMobId,
				 QStringLiteral("060a2b3401010105.01010f1013000000.d2467dea74110690.91901e6a605d3613"));
	}
}

// _GUILESS_ pulls in a QCoreApplication event loop; queued signals
// and QSignalSpy::wait both need one.
// MARK: - Database-first

namespace
{
	/// The TONE fixture's PMR trailer: its mtime when Avid indexed it.
	constexpr quint32 kToneModified = 1778755394u;
	const QString kToneName = QStringLiteral("TONE_100A01.EA7D504A.611740.mxf");
	const QString kToneClip = QStringLiteral("TONE: 1000 Hz @ -14.0 dB.1");
	const QByteArray kToneFileId = QByteArray::fromHex("060a2b340101010501010f10130000004a507dea741106907a361e6a605d3613");

	QByteArray singlePmr(const QByteArray &name, const QByteArray &fileId, const QByteArray &masterId,
						 const QByteArray &project, quint32 modified)
	{
		return BentoBuilder::le32(0x7a9) + BentoBuilder::le32(8) + BentoBuilder::le32(1) + fileId +
			BentoBuilder::le32(quint32(name.size())).left(2) + name +
			BentoBuilder::le32(quint32(project.size())).left(2) + project + masterId + BentoBuilder::le32(modified);
	}

	QVector<MediaFile> runScan(const QString &root)
	{
		MediaScanner scanner;
		QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
		MediaScanner::Options opts;
		opts.volumePaths = QStringList{root};
		scanner.startScan(opts);
		if (!finishedSpy.wait(5000))
			return {};
		return finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	}
} // namespace

void TestScanner::database_described_row_never_reads_its_header()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	// Junk under the real name, stamped with the PMR's mtime: a header read
	// would find nothing, so every technical fact below came from the MDB.
	writeJunk(folder + QLatin1Char('/') + kToneName, 4096);
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();
	QCOMPARE(mf.project, QStringLiteral("block 1729"));
	QCOMPARE(mf.clipName, kToneClip);
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::Mdb);
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QCOMPARE(mf.type, MediaFile::Type::Media);
	QCOMPARE(mf.codec, QString::fromLatin1(kPcmAudioName));
	QCOMPARE(mf.sampleRate, 48000);
	QVERIFY(mf.channels > 0);
	QVERIFY(mf.durationFrames > 0);
	QVERIFY(mf.timecodeBase > 0);
	QVERIFY(!mf.bitDepth.isEmpty());
	QVERIFY(mf.resolution.isEmpty());
	QVERIFY(!mf.isInvalidUmid);
	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::Listed);
	QVERIFY(!mf.hasNoProject());
}

void TestScanner::stale_header_and_current_database_agree()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(kToneName, folder);
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified);

	// Current timestamps use the database, with no header read.
	const auto normal = runScan(tmp.path());
	QCOMPARE(normal.size(), 1);
	QCOMPARE(normal.first().clipNameSource, MediaFile::ClipNameSource::Mdb);
	QVERIFY(!normal.first().needsHeaderRead);

	// Changing the timestamp triggers automatic verification. The actual
	// header still describes the same clip and must agree with the database.
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified + 10);
	const auto fromHeader = runScan(tmp.path());
	QCOMPARE(fromHeader.size(), 1);
	QVERIFY(fromHeader.first().needsHeaderRead);
	QCOMPARE(fromHeader.first().clipName, kToneClip);
	QCOMPARE(fromHeader.first().clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QCOMPARE(fromHeader.first().project, QStringLiteral("block 1729"));
	QVERIFY(!fromHeader.first().originalBin.isEmpty());
	QCOMPARE(fromHeader.first().kind, MediaFile::Kind::Audio);
	QCOMPARE(normal.first().codec, fromHeader.first().codec);
	QCOMPARE(normal.first().sampleRate, fromHeader.first().sampleRate);
	QCOMPARE(normal.first().channels, fromHeader.first().channels);
	QCOMPARE(normal.first().bitDepth, fromHeader.first().bitDepth);
	QCOMPARE(normal.first().durationFrames, fromHeader.first().durationFrames);
	QCOMPARE(normal.first().timecodeBase, fromHeader.first().timecodeBase);
	QCOMPARE(normal.first().originalBin, fromHeader.first().originalBin);
	QCOMPARE(normal.first().mobId, fromHeader.first().mobId);
	QCOMPARE(normal.first().masterMobId, fromHeader.first().masterMobId);
}

void TestScanner::changed_file_falls_back_to_its_header()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	// Same junk, but a FRESH mtime: the databases describe an older file, so
	// their technical facts must not be applied. The header says nothing, so
	// the row shows no codec — and keeps the MDB's name and bin (identity is
	// a fact about the clip, not the bytes).
	writeJunk(folder + QLatin1Char('/') + kToneName, 4096);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();
	QVERIFY2(mf.codec.isEmpty(), qPrintable(mf.codec));
	QCOMPARE(mf.sampleRate, 0);
	QCOMPARE(mf.clipName, kToneClip);
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::Mdb);
	QCOMPARE(mf.project, QStringLiteral("block 1729"));
}

void TestScanner::zero_pmr_timestamp_forces_header_and_keeps_kind_and_type_unknown()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"),
		 singlePmr(kToneName.toUtf8(), kToneFileId, kLadderMob, "block 1729", 0)));
	writeJunk(folder + QLatin1Char('/') + kToneName, 4096);
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified);

	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QVERIFY(mf.needsHeaderRead);
	QVERIFY(!mf.databaseMetadataCurrent);
	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::Listed);
	QCOMPARE(mf.kind, MediaFile::Kind::Unknown);
	QCOMPARE(mf.type, MediaFile::Type::Unknown);
	QVERIFY(mf.codec.isEmpty());
	QCOMPARE(mf.sampleRate, 0);
	QCOMPARE(mf.clipName, kToneClip); // failed reading is not a contradictory identity
}

void TestScanner::current_render_with_missing_project_survives_failed_header_read()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	BentoBuilder w;
	const quint32 master = w.addObject("MOBJ"), file = w.addObject("MOBJ"), pcm = w.addObject("PCMA");
	w.set(master, "OMFI:MOBJ:MobID", kLadderMob);
	w.setU32(master, "OMFI:MOBJ:UsageCode", 1);
	w.setString(master, "OMFI:CPNT:Name", "Sequence,Audio Effect+1");
	w.set(file, "OMFI:MOBJ:MobID", kToneFileId);
	w.setHandle(file, "OMFI:MOBJ:PhysicalMedia", pcm);
	w.setRational(file, "OMFI:CPNT:EditRate", 25, 1);
	w.setRational(pcm, "OMFI:MDFL:SampleRate", 48000, 1);
	w.setU32(pcm, "OMFI:MDFL:Length", 96000);
	w.setU16(pcm, "OMFI:MDAU:BitsPerSample", 24);
	w.setU16(pcm, "OMFI:MDAU:NumChannels", 1);
	const QString dbPath = folder + QStringLiteral("/msmMMOB.mdb");
	QVERIFY(tryWriteFile(dbPath, w.build()));
	const MdbDatabase db = MdbParser::load(dbPath);
	QVERIFY(db.files.value(MobId::format(kToneFileId)).essenceComplete);
	QVERIFY(db.masters.value(MobId::format(kLadderMob)).classificationKnown);
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"),
		 singlePmr("render.mxf", kToneFileId, kLadderMob, {}, kToneModified)));
	writeJunk(folder + QStringLiteral("/render.mxf"), 4096);
	setModified(folder + QStringLiteral("/render.mxf"), kToneModified);

	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QVERIFY(mf.databaseMetadataCurrent);
	QVERIFY(mf.needsHeaderRead); // the project is missing, despite current essence
	QVERIFY(mf.project.isEmpty());
	QCOMPARE(mf.type, MediaFile::Type::Precompute);
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QCOMPARE(mf.sampleRate, 48000);
	QCOMPARE(mf.clipName, QStringLiteral("Sequence,Audio Effect+1"));

}

void TestScanner::reused_filename_clears_old_editorial_details()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	const QString replacement = fixturesDir() + QString::fromUtf8("/zT_\xc3\x9ft_1080i_50_seqDD866C6BV.mxf");
	const MxfMetadata header = MxfParser::parseHeader(replacement);
	QVERIFY(header.valid && header.classificationKnown && header.isPrecompute);
	const QString newFileId = MobId::toPmrForm(header.fileMobId);
	const QString newMasterId = MobId::toPmrForm(header.umid);
	QVERIFY(!newFileId.isEmpty() && newFileId != MobId::format(kToneFileId));
	QVERIFY(!newMasterId.isEmpty() && newMasterId != MobId::format(kLadderMob));
	QVERIFY(QFile::copy(replacement, folder + QLatin1Char('/') + kToneName));
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified + 10);

	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QVERIFY(mf.needsHeaderRead);
	QVERIFY(!mf.databaseMetadataCurrent);
	QCOMPARE(mf.mobId, newFileId);
	QCOMPARE(mf.masterMobId, newMasterId);
	QCOMPARE(mf.clipName, header.clipName);
	QCOMPARE(mf.project, header.projectName);
	QVERIFY(mf.originalBin.isEmpty()); // the obsolete tone's MDB bin must not survive
	QCOMPARE(mf.sourceFilePath, header.sourceFilePath);
	QCOMPARE(mf.sourceContainer, header.sourceContainer);
	QCOMPARE(mf.isImported, header.hasImportSetting);
	QCOMPARE(mf.kind, MediaFile::Kind::Video);
	QCOMPARE(mf.type, MediaFile::Type::Precompute);
	QCOMPARE(mf.sampleRate, 0);
	QCOMPARE(mf.codec, header.codec);
}

void TestScanner::pmr_v1_recovers_unique_master_from_mdb()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"), TestOmf::sdii(false)));
	const QByteArray name("sample.omf");
	// ReadPmrRec version1 grammar: file UID8, name length/name, DTM.
	const QByteArray pmr = BentoBuilder::le32(0x7a9) + BentoBuilder::le32(1) + BentoBuilder::le32(1) +
		TestOmf::uid(2).mid(4) + BentoBuilder::le32(quint32(name.size())).left(2) + name +
		BentoBuilder::le32(kToneModified);
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), pmr));
	writeJunk(folder + QStringLiteral("/sample.omf"), 4096);
	setModified(folder + QStringLiteral("/sample.omf"), kToneModified);
	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QCOMPARE(mf.mobId, OmfUid::canonicalHex(TestOmf::uid(2)));
	QCOMPARE(mf.masterMobId, OmfUid::canonicalHex(TestOmf::uid(1)));
	QCOMPARE(mf.project, QStringLiteral("SDII project"));
	QCOMPARE(mf.type, MediaFile::Type::Media);
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QVERIFY(mf.databaseMetadataCurrent);
	QVERIFY(!mf.needsHeaderRead);
}

void TestScanner::omf2_header_keeps_master_identity_with_unknown_classification()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/sample.omf"), TestOmf::sdii(true, false, false, true)));
	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QCOMPARE(mf.masterMobId, OmfUid::canonicalHex(TestOmf::uid(1)));
	QCOMPARE(mf.mobId, OmfUid::canonicalHex(TestOmf::uid(2)));
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QCOMPARE(mf.type, MediaFile::Type::Unknown); // MMOB identity and Avid render classification are separate
	QCOMPARE(mf.project, QStringLiteral("SDII project"));
}

void TestScanner::mxf_header_keeps_master_identity_with_unknown_classification()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	const auto set = [](quint8 type, const QByteArray &value) {
		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101010000");
		key[14] = char(type);
		return key + QByteArray(1, char(value.size())) + value;
	};
	QByteArray name;
	for (char c : QByteArray("Sequence,3D_Warp+1")) { name.append('\0'); name.append(c); }
	QByteArray material = QByteArray::fromHex("44010020") + kLadderMob +
		QByteArray::fromHex("44080010060e2b34040101010d01010201010800") +
		QByteArray::fromHex("4402") + QByteArray(1, '\0') + QByteArray(1, char(name.size())) + name;
	const QByteArray descriptorId(16, '\x42');
	const QByteArray bytes = set(0x36, material) +
		set(0x37, QByteArray::fromHex("44010020") + kToneFileId + QByteArray::fromHex("47010010") + descriptorId) +
		set(0x28, QByteArray::fromHex("3c0a0010") + descriptorId +
			QByteArray::fromHex("32030004000007803202000400000438300100080000001900000001"));
	const QString path = folder + QStringLiteral("/unknown-usage.mxf");
	QVERIFY(tryWriteFile(path, bytes));
	const MxfMetadata parsed = MxfParser::parseHeader(path);
	QVERIFY(parsed.valid);
	QVERIFY(parsed.hasMaterialPackage);
	QVERIFY(!parsed.classificationKnown); // LowerLevel alone also describes groups/motion.
	QVERIFY(!parsed.umid.isEmpty());
	QVERIFY(!parsed.fileMobId.isEmpty());
	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QCOMPARE(mf.masterMobId, MobId::toPmrForm(parsed.umid));
	QCOMPARE(mf.mobId, MobId::toPmrForm(parsed.fileMobId));
	QCOMPARE(mf.kind, MediaFile::Kind::Video);
	QCOMPARE(mf.type, MediaFile::Type::Unknown);
	QCOMPARE(mf.clipName, QStringLiteral("Sequence,3D_Warp+1"));
	QVERIFY(mf.effect.isEmpty()); // A known effect name cannot supply the missing usage verdict.

	// A current database initially says Precompute, but its missing project
	// causes header verification. The actual material package's ambiguous
	// usage must replace that earlier verdict while preserving its identity.
	const QByteArray fileId = QByteArray::fromHex(mf.mobId.toLatin1());
	const QByteArray masterId = QByteArray::fromHex(mf.masterMobId.toLatin1());
	BentoBuilder db;
	const quint32 master = db.addObject("MOBJ"), file = db.addObject("MOBJ"), pcm = db.addObject("PCMA");
	db.set(master, "OMFI:MOBJ:MobID", masterId);
	db.setU32(master, "OMFI:MOBJ:UsageCode", 1);
	db.setString(master, "OMFI:CPNT:Name", "Sequence,3D_Warp+1");
	db.set(file, "OMFI:MOBJ:MobID", fileId);
	db.setHandle(file, "OMFI:MOBJ:PhysicalMedia", pcm);
	db.setRational(file, "OMFI:CPNT:EditRate", 25, 1);
	db.setRational(pcm, "OMFI:MDFL:SampleRate", 48000, 1);
	db.setU32(pcm, "OMFI:MDFL:Length", 96000);
	db.setU16(pcm, "OMFI:MDAU:BitsPerSample", 24);
	db.setU16(pcm, "OMFI:MDAU:NumChannels", 1);
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"), db.build()));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"),
		singlePmr("unknown-usage.mxf", fileId, masterId, {}, kToneModified)));
	setModified(path, kToneModified);
	const auto withDatabase = runScan(tmp.path());
	QCOMPARE(withDatabase.size(), 1);
	QVERIFY(withDatabase.first().databaseMetadataCurrent);
	QVERIFY(withDatabase.first().needsHeaderRead);
	QCOMPARE(withDatabase.first().masterMobId, mf.masterMobId);
	QCOMPARE(withDatabase.first().type, MediaFile::Type::Unknown);
	QVERIFY(withDatabase.first().effect.isEmpty());
}

void TestScanner::folder_without_databases_reads_every_header()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(kToneName, folder);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();
	QVERIFY(mf.isNoDatabase());
	QCOMPARE(mf.clipName, kToneClip);
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QVERIFY(!mf.codec.isEmpty());
	QVERIFY(mf.sampleRate > 0);
}

void TestScanner::mpeg_audio_falls_back_to_its_header()
{
	// The MDB carries no codec label for MPEG audio, so its record is
	// incomplete and the header decides — even though the PMR names the file
	// and the mtime matches.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("corpus_headers/msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("corpus_headers/msmMMOB.mdb"), folder);
	const QString name = QStringLiteral("A01.E68C35B3_2C34B2C34B61AA.mxf");
	copyFixture(QStringLiteral("corpus_headers/") + name, folder);
	quint32 modified = 0;
	for (const PmrEntry &e : PmrParser::parse(folder + QStringLiteral("/msmFMID.pmr")))
		if (e.fileName == name)
			modified = e.fileModifiedSecs;
	QVERIFY(modified != 0);
	setModified(folder + QLatin1Char('/') + name, modified);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QVERIFY2(mf.codec.contains(QStringLiteral("MP2")), qPrintable(mf.codec));
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QVERIFY(!mf.originalBin.isEmpty()); // identity still from the MDB
}

void TestScanner::precompute_row_gets_effect_fields()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	// A real render slice, no databases: the header's usage code says
	// Precompute, and the catalogue names the effect from the clip name.
	copyFixture(QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seqDD866C6BV.mxf"), folder);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();
	QCOMPARE(mf.type, MediaFile::Type::Precompute);
	QCOMPARE(mf.clipName, QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq,1.85_Mask+2"));
	QCOMPARE(mf.effect, QStringLiteral("1.85 Mask"));
	QCOMPARE(mf.effectCategory, QStringLiteral("Film"));
	QCOMPARE(mf.effectSequence, QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq"));
	QCOMPARE(mf.effectInstance, 2);

	// And a Media row carries none of it.
	const QString folder2 = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/2");
	QVERIFY(QDir().mkpath(folder2));
	copyFixture(kToneName, folder2);
	const auto both = runScan(tmp.path());
	QCOMPARE(both.size(), 2);
	for (const MediaFile &f : both)
		if (f.type == MediaFile::Type::Media)
		{
			QVERIFY(f.effect.isEmpty());
			QVERIFY(f.effectCategory.isEmpty());
			QCOMPARE(f.effectInstance, 0);
		}
}

void TestScanner::modified_is_the_filesystem_mtime()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(kToneName, folder);
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	QVERIFY(results.first().modified.isValid());
	QCOMPARE(results.first().modified.toSecsSinceEpoch(), qint64(kToneModified));
	QVERIFY(!results.first().modifiedDisplay().isEmpty());
}

// MARK: - OMF-era

namespace
{
	/// The MC 2026 audio pair in fixtures/omf/mc2026_audio and what the
	/// v2 PMR / MDB say about each (pinned by tst_pmrparser, tst_mdbparser
	/// and tst_omfparser; repeated here so a scanner row can be checked
	/// end to end).
	const QString kOmfWav = QStringLiteral("TONE_100A01.6A972974.039700.wav");
	const QString kOmfAif = QStringLiteral("TONE_100A01.6A972997.0C53E0.aif");
	constexpr quint32 kOmfWavModified = 1788291444u;
	constexpr quint32 kOmfAifModified = 1788291480u;
	const QString kOmfWavClip = QStringLiteral("TONE: 1000 Hz @ -14.0 dB.1");
	const QString kOmfAifClip = QStringLiteral("TONE: 1000 Hz @ -20.0 dB.2");
	const QString kOmfWavBin = QStringLiteral("WAVE(OMF)");
	const QString kOmfAifBin = QStringLiteral("AIFF-C(OMF)");
	const QString kOmfWavFileMob =
		QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80");
	const QString kOmfWavMasterMob =
		QStringLiteral("060a2b3401010101.01010f0013000000.7429976a4e397047.060e2b347f7f2a80");
	const QString kOmfAifFileMob =
		QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3ec57047.060e2b347f7f2a80");
	const QString kOmfAifMasterMob =
		QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3dc57047.060e2b347f7f2a80");
	const QString kOmfProject = QString::fromUtf8("zTe\xc3\x9ft_PAL_25p");
	const QString kOmfFolder = QString(Conventions::kOmfMediaFilesDir);
	/// The trailer of 73 of the shipped PMR's 80 pairs — every slate pinned
	/// below among them; the other 7 read 1626810312, still inside
	/// PmrParser::trailerMatchesModified's ±2 s (tst_pmrparser has the split).
	constexpr quint32 kSlateModified = 1626810310u;

	QVector<MediaFile> runScanWith(const MediaScanner::Options &opts)
	{
		MediaScanner scanner;
		QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
		scanner.startScan(opts);
		if (!finishedSpy.wait(10000))
			return {};
		return finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	}

	QVector<MediaFile> runManualScan(const QString &folder)
	{
		MediaScanner::Options opts;
		opts.manualPaths = QStringList{folder};
		return runScanWith(opts);
	}

	const MediaFile *rowNamed(const QVector<MediaFile> &rows, const QString &name)
	{
		for (const MediaFile &f : rows)
			if (f.fileName == name)
				return &f;
		return nullptr;
	}

	/// What every OMF audio row must say whichever way it was described —
	/// the database (no header read) or the file's own tail.
	void checkOmfAudioRow(const MediaFile &mf, const QString &clip, const QString &bin, const QString &fileMob,
						  const QString &masterMob)
	{
		QCOMPARE(mf.mxfFolder, kOmfFolder);
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::Listed);
		QCOMPARE(mf.kind, MediaFile::Kind::Audio);
		QCOMPARE(mf.type, MediaFile::Type::Media);
		// OMF-era: Avid's container label, never "PCM" (that is MXF-era audio).
		QCOMPARE(mf.codec, mf.fileName.endsWith(QLatin1String(".wav")) ? QStringLiteral("WAVE (OMF)")
																	   : QStringLiteral("AIFF-C (OMF)"));
		QCOMPARE(mf.clipName, clip);
		QCOMPARE(mf.project, kOmfProject);
		QCOMPARE(mf.originalBin, bin);
		QCOMPARE(mf.mobId, fileMob);
		QCOMPARE(mf.masterMobId, masterMob);
		QVERIFY(OmfUid::isOmfForm(mf.mobId));
		QVERIFY(OmfUid::isOmfForm(mf.masterMobId));
		QVERIFY(!mf.isInvalidUmid);
		QVERIFY(!mf.hasNoProject());
		QCOMPARE(mf.sampleRate, 48000);
		QCOMPARE(mf.channels, 1);
		QCOMPARE(mf.bitDepth, QStringLiteral("24-bit"));
		QCOMPARE(mf.durationFrames, qint64(1500));
		QCOMPARE(mf.timecodeBase, 25);
		QVERIFY2(mf.resolution.isEmpty(), qPrintable(mf.resolution));
		QVERIFY2(mf.fps.isEmpty(), qPrintable(mf.fps));
	}
} // namespace

void TestScanner::omf_volume_root_scans_both_folders()
{
	// A drive with both roots at its top level: the MXF tree exactly as
	// every test above builds it, and the flat OMF root beside it.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mxfFolder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(mxfFolder));
	copyFixture(QStringLiteral("msmFMID.pmr"), mxfFolder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), mxfFolder);
	copyFixture(kToneName, mxfFolder);

	const QString omfRoot = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(omfRoot));
	copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), omfRoot);
	// The real .wav, stamped with its PMR trailer; and JUNK under the .aif's
	// name, stamped likewise — a tail read of it would find no Bento label,
	// so every technical fact on that row can only have come from the MDB.
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot);
	setModified(omfRoot + QLatin1Char('/') + kOmfWav, kOmfWavModified);
	writeJunk(omfRoot + QLatin1Char('/') + kOmfAif, 4096);
	setModified(omfRoot + QLatin1Char('/') + kOmfAif, kOmfAifModified);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 3);

	const MediaFile *mxf = rowNamed(results, kToneName);
	QVERIFY(mxf != nullptr);
	QCOMPARE(mxf->mxfFolder, QStringLiteral("1"));
	QCOMPARE(mxf->clipName, kToneClip);

	const MediaFile *wav = rowNamed(results, kOmfWav);
	QVERIFY(wav != nullptr);
	checkOmfAudioRow(*wav, kOmfWavClip, kOmfWavBin, kOmfWavFileMob, kOmfWavMasterMob);
	// Database-covered: the file was never opened, so the name is the MDB's
	// rung, not the master mob's own (which OmfParser would rank higher).
	QCOMPARE(wav->clipNameSource, MediaFile::ClipNameSource::Mdb);

	const MediaFile *aif = rowNamed(results, kOmfAif);
	QVERIFY(aif != nullptr);
	checkOmfAudioRow(*aif, kOmfAifClip, kOmfAifBin, kOmfAifFileMob, kOmfAifMasterMob);
	QCOMPARE(aif->clipNameSource, MediaFile::ClipNameSource::Mdb);
}

void TestScanner::omf_volume_scan_stops_at_the_root_but_a_manual_path_goes_deeper()
{
	// Media two levels down — the `~/Documents/Project/...` layout. Avid
	// never writes that on a drive, so a VOLUME scan does not look for it
	// (user ruling 2026-09-02); the same path added by hand still finds it,
	// in both eras.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString project = tmp.path() + QStringLiteral("/Project");
	const QString mxfFolder = Conventions::mxfRootUnder(project) + QStringLiteral("/1");
	QVERIFY(QDir().mkpath(mxfFolder));
	copyFixture(kToneName, mxfFolder);
	const QString omfRoot = Conventions::omfRootUnder(project);
	QVERIFY(QDir().mkpath(omfRoot));
	copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot);

	QVERIFY(runScan(tmp.path()).isEmpty());

	const auto manual = runManualScan(tmp.path());
	QCOMPARE(manual.size(), 2);
	QVERIFY(rowNamed(manual, kToneName) != nullptr);
	const MediaFile *wav = rowNamed(manual, kOmfWav);
	QVERIFY(wav != nullptr);
	QCOMPARE(wav->mxfFolder, kOmfFolder);
	QCOMPARE(wav->volumePath, project);

	// The project folder itself, added by hand, is found at depth 0 too.
	QCOMPARE(runManualScan(project).size(), 2);
}

void TestScanner::omf_root_pointed_at_directly_never_scans_as_mxf_folders()
{
	// The OMF root, with the subfolders a real one can carry — Avid's
	// transient `Creating`, and here a stray numbered folder as well. It
	// used to pass "MXF-or-OMF root with subfolders" and be walked as an
	// MXF root: subfolder rows, the flat media skipped. The name decides now.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString omfRoot = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(omfRoot + QStringLiteral("/Creating")));
	QVERIFY(QDir().mkpath(omfRoot + QStringLiteral("/1")));
	writeJunk(omfRoot + QStringLiteral("/Creating/half.omf"), 2048);
	writeJunk(omfRoot + QStringLiteral("/1/stray.mxf"), 2048);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfAif, omfRoot);
	// Fresh mtimes: the databases describe an older file, so both rows go
	// through OmfParser — the header path — and must say the same things.

	const auto results = runManualScan(omfRoot);
	QCOMPARE(results.size(), 2);
	QVERIFY(rowNamed(results, QStringLiteral("half.omf")) == nullptr);
	QVERIFY(rowNamed(results, QStringLiteral("stray.mxf")) == nullptr);

	const MediaFile *wav = rowNamed(results, kOmfWav);
	QVERIFY(wav != nullptr);
	checkOmfAudioRow(*wav, kOmfWavClip, kOmfWavBin, kOmfWavFileMob, kOmfWavMasterMob);
	QCOMPARE(wav->clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QCOMPARE(wav->volumePath, tmp.path());

	const MediaFile *aif = rowNamed(results, kOmfAif);
	QVERIFY(aif != nullptr);
	checkOmfAudioRow(*aif, kOmfAifClip, kOmfAifBin, kOmfAifFileMob, kOmfAifMasterMob);
	QCOMPARE(aif->clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
}

void TestScanner::omf_root_without_a_pmr_gets_identity_from_its_header()
{
	// No index, so nothing lists the file: the tail supplies the clip name,
	// project and both mob IDs; the re-join through the MDB by the
	// master's id recovers the bin — the OMF twin of
	// unreferenced_mxf_recovered_via_mdb.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString omfRoot = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(omfRoot));
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot);

	{
		const auto results = runScan(tmp.path());
		QCOMPARE(results.size(), 1);
		const MediaFile &mf = results.first();
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
		QCOMPARE(mf.mxfFolder, kOmfFolder);
		QCOMPARE(mf.kind, MediaFile::Kind::Audio);
		QCOMPARE(mf.codec, QStringLiteral("WAVE (OMF)")); // OMF-era: Avid's container label
		QCOMPARE(mf.clipName, kOmfWavClip);
		QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
		QCOMPARE(mf.project, kOmfProject);
		QCOMPARE(mf.mobId, kOmfWavFileMob);		  // the file's own identity
		QCOMPARE(mf.masterMobId, kOmfWavMasterMob); // verified by the file's graph
		QCOMPARE(mf.originalBin, kOmfWavBin);		  // which is the only place a bin lives
		QVERIFY(!mf.isInvalidUmid);
	}

	// With no database at all the tail still names the clip, the project and
	// the file mob and master; this file's bin only lives in the MDB.
	QVERIFY(QFile::remove(omfRoot + QStringLiteral("/msmMMOB.mdb")));
	{
		const auto results = runScan(tmp.path());
		QCOMPARE(results.size(), 1);
		const MediaFile &mf = results.first();
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
		QCOMPARE(mf.clipName, kOmfWavClip);
		QCOMPARE(mf.project, kOmfProject);
		QCOMPARE(mf.mobId, kOmfWavFileMob);
		QCOMPARE(mf.masterMobId, kOmfWavMasterMob);
		QVERIFY(mf.originalBin.isEmpty());
		QCOMPARE(mf.codec, QStringLiteral("WAVE (OMF)")); // OMF-era: Avid's container label
	}
}

void TestScanner::omf_video_rows_show_avid_short_names()
{
	// Three of the shipped slates under their own databases: the codec is
	// the bare Avid short name (user ruling 2026-09-02) whether the MDB
	// described the row or the file's tail did — pins from tst_omfparser.
	struct Pin
	{
		const char *file;
		const char *codec;
		const char *resolution;
		const char *fps;
		bool stamp; ///< true: mtime = PMR trailer, database-covered; false: fresh, header path
	};
	const Pin kPins[] = {
		{"BLACK_720x243x2_JFIF35.omf", "20:1", "720x496", "29.97", true},
		{"BLACK_720x576x1_DV420.omf", "DV 25 420 i(PAL)", "720x576", "25", true},
		{"BLACK_1920x540x2_AVHD_220.omf", "Avid DNx HQ (DNxHD 220)", "1920x1080", "29.97", false},
	};

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString omfRoot = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(omfRoot));
	copyFixture(QStringLiteral("omf/avid_supporting/msmFMID.pmr"), omfRoot);
	copyFixture(QStringLiteral("omf/avid_supporting/msmMMOB.mdb"), omfRoot);
	for (const Pin &pin : kPins)
	{
		copyFixture(QStringLiteral("omf/avid_supporting/") + QLatin1String(pin.file), omfRoot);
		if (pin.stamp)
			setModified(omfRoot + QLatin1Char('/') + QLatin1String(pin.file), kSlateModified);
	}

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 3);
	for (const Pin &pin : kPins)
	{
		const MediaFile *mf = rowNamed(results, QLatin1String(pin.file));
		QVERIFY2(mf != nullptr, pin.file);
		QCOMPARE(mf->mxfFolder, kOmfFolder);
		QCOMPARE(mf->dbStatus, MediaFile::DbStatus::Listed);
		QCOMPARE(mf->kind, MediaFile::Kind::Video);
		QCOMPARE(mf->type, MediaFile::Type::Media);
		QCOMPARE(mf->codec, QLatin1String(pin.codec));
		QCOMPARE(mf->resolution, QLatin1String(pin.resolution));
		QCOMPARE(mf->fps, QLatin1String(pin.fps));
		QCOMPARE(mf->durationFrames, qint64(1));
		QCOMPARE(mf->bitDepth, QStringLiteral("8-bit"));
		QVERIFY2(!mf->clipName.isEmpty(), pin.file);
		QVERIFY2(!mf->project.isEmpty(), pin.file); // the v2 PMR has none; the MDB's _PJ fills it
		QVERIFY(OmfUid::isOmfForm(mf->mobId));
		QVERIFY(OmfUid::isOmfForm(mf->masterMobId));
		QCOMPARE(mf->clipNameSource,
				 pin.stamp ? MediaFile::ClipNameSource::Mdb : MediaFile::ClipNameSource::MaterialPackage);
	}
}

void TestScanner::omf_folder_with_any_name_is_recognised_by_its_databases()
{
	// Avid's own bundled slate folder is "Avid_MediaFiles" (underscore),
	// and an archive folder added by hand can be called anything. The era
	// is not the folder's name: a folder whose databases carry 12-byte
	// omfi:UIDs is an OMF folder, and every legacy-extension file in it is
	// legacy media — described by those databases when they cover it, read
	// from its own tail when they don't. Dragging the bundled folder in
	// used to list 80 rows with no codec, resolution, fps, duration or
	// project (2026-09-02): neither era claimed them.
	struct Pin
	{
		const char *file;
		const char *codec;
		const char *resolution;
		const char *fps;
		bool stamp; ///< true: mtime = PMR trailer, database-covered; false: fresh, header path
	};
	const Pin kPins[] = {
		{"BLACK_720x243x2_JFIF35.omf", "20:1", "720x496", "29.97", true},
		{"BLACK_720x576x1_DV420.omf", "DV 25 420 i(PAL)", "720x576", "25", true},
		{"BLACK_1920x540x2_AVHD_220.omf", "Avid DNx HQ (DNxHD 220)", "1920x1080", "29.97", false},
	};

	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid_MediaFiles");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("omf/avid_supporting/msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("omf/avid_supporting/msmMMOB.mdb"), folder);
	for (const Pin &pin : kPins)
	{
		copyFixture(QStringLiteral("omf/avid_supporting/") + QLatin1String(pin.file), folder);
		if (pin.stamp)
			setModified(folder + QLatin1Char('/') + QLatin1String(pin.file), kSlateModified);
	}
	// A legacy .wav these databases do not list: the folder's era still
	// admits it to the header pass, so its own tail names it.
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);

	const auto results = runManualScan(folder);
	QCOMPARE(results.size(), 4);
	for (const Pin &pin : kPins)
	{
		const MediaFile *mf = rowNamed(results, QLatin1String(pin.file));
		QVERIFY2(mf != nullptr, pin.file);
		QVERIFY2(mf->omfEra, pin.file);
		QCOMPARE(mf->mxfFolder, QStringLiteral("Avid_MediaFiles")); // the real name, never a stand-in
		QCOMPARE(mf->dbStatus, MediaFile::DbStatus::Listed);
		QCOMPARE(mf->kind, MediaFile::Kind::Video);
		QCOMPARE(mf->codec, QLatin1String(pin.codec));
		QCOMPARE(mf->resolution, QLatin1String(pin.resolution));
		QCOMPARE(mf->fps, QLatin1String(pin.fps));
		QCOMPARE(mf->durationFrames, qint64(1));
		QVERIFY2(!mf->clipName.isEmpty(), pin.file);
		QVERIFY2(!mf->project.isEmpty(), pin.file);
		QVERIFY(OmfUid::isOmfForm(mf->mobId));
		QVERIFY(OmfUid::isOmfForm(mf->masterMobId));
		QCOMPARE(mf->type, MediaFile::Type::Media);
		QCOMPARE(mf->bitDepth, QStringLiteral("8-bit"));
		QCOMPARE(mf->clipNameSource,
				 pin.stamp ? MediaFile::ClipNameSource::Mdb : MediaFile::ClipNameSource::MaterialPackage);
	}
	const MediaFile *wav = rowNamed(results, kOmfWav);
	QVERIFY(wav != nullptr);
	QVERIFY(wav->omfEra);
	QCOMPARE(wav->dbStatus, MediaFile::DbStatus::NoReference);
	QCOMPARE(wav->kind, MediaFile::Kind::Audio);
	QCOMPARE(wav->codec, QStringLiteral("WAVE (OMF)"));
	QCOMPARE(wav->clipName, kOmfWavClip);
	QCOMPARE(wav->mobId, kOmfWavFileMob);
	QCOMPARE(wav->masterMobId, kOmfWavMasterMob); // verified by the file's own graph
}

void TestScanner::omf_folder_is_recognised_by_its_mdb_alone()
{
	// The same folder with only the clip database. No PMR means no index
	// to be listed in and nothing the databases can vouch for — but the
	// MDB's keys still say OMF-era, so both files are opened and read from
	// their own tails, and the slate's MDB record joins by identity.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid_MediaFiles");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("omf/avid_supporting/msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("omf/avid_supporting/BLACK_720x243x2_JFIF35.omf"), folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);

	const auto results = runManualScan(folder);
	QCOMPARE(results.size(), 2);
	for (const MediaFile &mf : results)
	{
		QVERIFY2(mf.omfEra, qPrintable(mf.fileName));
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
		QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
		QVERIFY(OmfUid::isOmfForm(mf.mobId));
	}
	const MediaFile *slate = rowNamed(results, QStringLiteral("BLACK_720x243x2_JFIF35.omf"));
	QVERIFY(slate != nullptr);
	QCOMPARE(slate->codec, QStringLiteral("20:1"));
	QCOMPARE(slate->originalBin, QStringLiteral("NTSC slides")); // the MDB's record, joined by identity
	const MediaFile *wav = rowNamed(results, kOmfWav);
	QVERIFY(wav != nullptr);
	QCOMPARE(wav->codec, QStringLiteral("WAVE (OMF)"));
	QVERIFY(wav->originalBin.isEmpty()); // this folder's MDB has never heard of it
}

void TestScanner::creating_folder_is_skipped_under_mxf_and_omfi()
{
	// Avid's staging folder holds half-written captures. Under MXF it is a
	// sibling of the numbered folders and used to be listed as one; under
	// the flat OMF root it is the only subfolder there ever is.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mxfRoot = Conventions::mxfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(mxfRoot + QStringLiteral("/1")));
	QVERIFY(QDir().mkpath(mxfRoot + QStringLiteral("/Creating")));
	copyFixture(kToneName, mxfRoot + QStringLiteral("/1"));
	writeJunk(mxfRoot + QStringLiteral("/Creating/half.mxf"), 2048);

	const QString omfRoot = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(omfRoot + QStringLiteral("/Creating")));
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot);
	writeJunk(omfRoot + QStringLiteral("/Creating/half.omf"), 2048);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 2);
	QVERIFY(rowNamed(results, kToneName) != nullptr);
	QVERIFY(rowNamed(results, kOmfWav) != nullptr);
	for (const MediaFile &f : results)
		QVERIFY2(!Conventions::isCreatingFolderName(f.mxfFolder), qPrintable(f.filePath));
}

void TestScanner::ama_databases_are_read()
{
	// The AMA-linked spelling of the same two files. Folder 1 carries only
	// the ama* pair; folder 2 both spellings of each (the same database
	// twice, so the merge has to cope with duplicate keys). Junk under the
	// real name, stamped: every fact below is the database's.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder1 = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	const QString folder2 = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/2");
	QVERIFY(QDir().mkpath(folder1));
	QVERIFY(QDir().mkpath(folder2));
	QVERIFY(QFile::copy(fixturesDir() + QStringLiteral("/msmFMID.pmr"), folder1 + QStringLiteral("/amaFMID.pmr")));
	QVERIFY(QFile::copy(fixturesDir() + QStringLiteral("/msmMMOB.mdb"), folder1 + QStringLiteral("/amaMMOB.mdb")));
	QVERIFY(QFile::copy(fixturesDir() + QStringLiteral("/msmFMID.pmr"), folder2 + QStringLiteral("/amaFMID.pmr")));
	QVERIFY(QFile::copy(fixturesDir() + QStringLiteral("/msmMMOB.mdb"), folder2 + QStringLiteral("/amaMMOB.mdb")));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder2);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder2);
	for (const QString &folder : {folder1, folder2})
	{
		writeJunk(folder + QLatin1Char('/') + kToneName, 4096);
		setModified(folder + QLatin1Char('/') + kToneName, kToneModified);
	}

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 2);
	for (const MediaFile &mf : results)
	{
		QCOMPARE(mf.fileName, kToneName);
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::Listed);
		QCOMPARE(mf.project, QStringLiteral("block 1729"));
		QCOMPARE(mf.clipName, kToneClip);
		QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::Mdb);
		QCOMPARE(mf.codec, QString::fromLatin1(kPcmAudioName));
		QCOMPARE(mf.sampleRate, 48000);
		QVERIFY(!mf.originalBin.isEmpty());
	}
}

// MARK: - MXF-era behaviour pinned across the OMF-era rework

void TestScanner::manual_path_inside_avid_mediafiles_resolves_to_its_mxf_root()
{
	// One MXF root at the top, and a second one nested a level down that
	// the top-level short-circuit (Case 1, as before) must NOT pick up.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(kToneName, folder);
	const QString nested = tmp.path() + QStringLiteral("/Deeper/Avid MediaFiles/MXF/2");
	QVERIFY(QDir().mkpath(nested));
	copyFixture(kToneName, nested);

	// Every shape a user drags in — the drive root, "Avid MediaFiles", its
	// "MXF" folder, a numbered folder — scans the same one root: exactly
	// the row set the ticked volume yields.
	const QStringList shapes = {
		tmp.path(),
		tmp.path() + QStringLiteral("/Avid MediaFiles"),
		tmp.path() + QStringLiteral("/Avid MediaFiles/MXF"),
		folder,
	};
	for (const QString &shape : shapes)
	{
		const auto rows = runManualScan(shape);
		QVERIFY2(rows.size() == 1, qPrintable(shape + QStringLiteral(": ") + QString::number(rows.size())));
		QCOMPARE(rows.first().fileName, kToneName);
		QCOMPARE(rows.first().mxfFolder, QStringLiteral("1"));
		QCOMPARE(rows.first().clipName, kToneClip);
	}
	QCOMPARE(runScan(tmp.path()).size(), 1);

	// The derived root of a hand-added numbered folder is ".../Avid
	// MediaFiles" — a folder shape, not a drive. As a VOLUME path it is
	// probed for "<path>/Avid MediaFiles/MXF" and misses; the post-rebalance
	// rescan therefore has to hand it over as a manual path (pinned above).
	MediaScanner::Options asVolume;
	asVolume.volumePaths = QStringList{tmp.path() + QStringLiteral("/Avid MediaFiles")};
	QVERIFY(runScanWith(asVolume).isEmpty());
}

void TestScanner::overlapping_volume_and_manual_roots_scan_each_folder_once()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(kToneName, folder);
	MediaScanner::Options opts;
	opts.volumePaths = {tmp.path(), tmp.path() + QLatin1Char('/')};
	opts.manualPaths = {tmp.path(), folder, folder + QLatin1Char('/')};
	const auto rows = runScanWith(opts);
	QCOMPARE(rows.size(), 1);
	QCOMPARE(rows.first().filePath, folder + QLatin1Char('/') + kToneName);
}

void TestScanner::case_distinct_shared_folders_remain_distinct()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString upper = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/Editor.1");
	const QString lower = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/editor.1");
	QVERIFY(QDir().mkpath(upper));
	if (QDir(lower).exists())
		QSKIP("Temporary filesystem is case-insensitive; case-distinct directories cannot be created here");
	QVERIFY(QDir().mkpath(lower));
	copyFixture(kToneName, upper);
	copyFixture(kToneName, lower);
	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 2);
	QSet<QString> paths;
	for (const MediaFile &row : rows)
		paths.insert(row.filePath);
	QVERIFY(paths.contains(upper + QLatin1Char('/') + kToneName));
	QVERIFY(paths.contains(lower + QLatin1Char('/') + kToneName));
}

void TestScanner::stray_audio_in_an_mxf_folder_is_listed_but_never_opened()
{
	// A real OMF-written .wav dropped into an MXF-era numbered folder. It
	// is admitted to the table (name, size, date) as it always was, but it
	// is NOT legacy essence in the scanner's eyes — a .wav could be anything,
	// this is no OMFI MediaFiles root, and the folder's own databases are
	// MXF-era — so its tail is never read: no codec, no clip name, no MOB,
	// and the folder's PMR (which does not list it) makes it a real miss.
	// Had OmfParser opened it, the row would say "WAVE (OMF)" and name the tone.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();
	QCOMPARE(mf.fileName, kOmfWav);
	QCOMPARE(mf.mxfFolder, QStringLiteral("1"));
	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoReference);
	QVERIFY2(mf.codec.isEmpty(), qPrintable(mf.codec));
	QVERIFY2(mf.clipName.isEmpty(), qPrintable(mf.clipName));
	QVERIFY(mf.mobId.isEmpty());
	QVERIFY(mf.masterMobId.isEmpty());
	QVERIFY(mf.hasNoProject());
	QCOMPARE(mf.sampleRate, 0);
}

void TestScanner::stray_omf_in_an_mxf_folder_is_opened()
{
	// The twin of the stray-audio rule. An .omf can only be OMF media, so
	// even dropped into an MXF-era numbered folder — whose PMR does not
	// list it — its tail is read: codec, clip name and MOB from the file.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("omf/avid_supporting/BLACK_720x243x2_JFIF35.omf"), folder);

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();
	QVERIFY(mf.omfEra);
	QCOMPARE(mf.mxfFolder, QStringLiteral("1"));
	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoReference);
	QCOMPARE(mf.kind, MediaFile::Kind::Video);
	QCOMPARE(mf.codec, QStringLiteral("20:1"));
	QCOMPARE(mf.clipName, QStringLiteral("Black 720x486.PICT"));
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QVERIFY(OmfUid::isOmfForm(mf.mobId));
	// Its own graph supplies both identities, despite the unrelated MDB.
	const OmfMetadata header = OmfParser::parseHeader(mf.filePath);
	QVERIFY(!header.essence.umid.isEmpty());
	QCOMPARE(mf.masterMobId, header.essence.umid);
	QCOMPARE(mf.project, QStringLiteral("NTSC slides"));
	QCOMPARE(mf.originalBin, QStringLiteral("NTSC slides"));
}

void TestScanner::unreadable_ama_twin_does_not_mark_the_folder_unreadable()
{
	// Folder 1: a readable msm* pair plus junk under both ama* names. The
	// msm* index stands, so the tone stays database-covered and the stray
	// .wav is a verified miss — what the folder said before the twins were
	// read at all. Folder 2: junk ama* files ALONE; with nothing else of
	// their kind to read, the folder's verdict is unreadable, as any lone
	// junk database makes it.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder1 = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	const QString folder2 = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/2");
	QVERIFY(QDir().mkpath(folder1));
	QVERIFY(QDir().mkpath(folder2));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder1);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder1);
	writeJunk(folder1 + QLatin1Char('/') + kToneName, 4096);
	setModified(folder1 + QLatin1Char('/') + kToneName, kToneModified);
	for (const QString &folder : {folder1, folder2})
	{
		writeJunk(folder + QStringLiteral("/amaFMID.pmr"), 16);
		writeJunk(folder + QStringLiteral("/amaMMOB.mdb"), 16);
		QFile wav(folder + QStringLiteral("/tone.wav"));
		QVERIFY(wav.open(QIODevice::WriteOnly));
		wav.write("RIFF----WAVEfmt ");
		wav.close();
	}

	const auto results = runScan(tmp.path());
	QCOMPARE(results.size(), 3);
	for (const MediaFile &mf : results)
	{
		if (mf.fileName == kToneName)
		{
			QCOMPARE(mf.dbStatus, MediaFile::DbStatus::Listed);
			QCOMPARE(mf.clipName, kToneClip);
			QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::Mdb);
			QCOMPARE(mf.codec, QString::fromLatin1(kPcmAudioName));
		}
		else if (mf.mxfFolder == QStringLiteral("1"))
			QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoReference);
		else
			QCOMPARE(mf.dbStatus, MediaFile::DbStatus::DbUnreadable);
	}
}

QTEST_GUILESS_MAIN(TestScanner)
#include "tst_scanner.moc"
