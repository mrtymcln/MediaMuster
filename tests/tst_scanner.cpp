// Drives MediaScanner against a fake Avid layout built from real
// PMR/MDB/MXF fixtures. Covers Stage 1 + 2 + the join.

#include "conventions.h"
#include "mediafile.h"
#include "testpause.h"
#include "mediascanner.h"
#include "omfuid.h"
#include "testbento.h"
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

	// Timeline effect renders are named "<sequence>,<Effect>+<n>" — the
	// fixture's material name is "zT_ßt_1080i_50_seq,1.85_Mask+2". The old
	// keyword list only knew a dozen effect names, so renders of any other
	// effect (masks, AniMatte, 3D Ball...) classified as Media. The name
	// SHAPE marks a render, whatever the effect. (The fixture's sequence
	// name deliberately contains no keyword: "Untitled..." fixtures pass by
	// accident because 'title' substring-matches "Untitled".)
	void effect_render_names_classify_as_precompute();

	// The clip-name ladder (user ruling 2026-08-14): MaterialPackage name,
	// else the MDB record's own name, else a SourcePackage name, else BLANK.
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

	// Media vs Precompute comes from the MXF UsageCode (tag 0x4408) and
	// nothing else — no filename or clip-name shapes (2026-08-14). A mixdown
	// is a MASTER CLIP, not a render, and carries no UsageCode, so it must
	// come back as Media even though its name reads like an effect. And a
	// header that can't be parsed yields no verdict: Media, not a guess.
	void mixdown_is_media_not_precompute();
	void unreadable_header_stays_media();

	// Database-first (2026-08-22). A row the folder's PMR + MDB fully
	// describe takes every technical fact from them and its file is never
	// opened; the header pass handles only what they don't cover — no PMR
	// entry, an incomplete MDB record (MPEG audio), a file changed since
	// Avid indexed it (mtime ≠ the PMR's trailer), no databases at all, or
	// the Debug "Force header scan" toggle. Identity (name/bin/source) from
	// the databases applies either way.
	void database_described_row_never_reads_its_header();
	void force_header_scan_reads_every_header();
	void changed_file_falls_back_to_its_header();
	void folder_without_databases_reads_every_header();
	void mpeg_audio_falls_back_to_its_header();

	// New facts: the render's effect name/category/sequence (CSV), and the
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
	void creating_folder_is_skipped_under_mxf_and_omfi();
	void ama_databases_are_read();

	// What the OMF-era rework must NOT have changed for MXF-era media: a
	// hand-added path of any shape still resolves to its MXF root (and a
	// derived ".../Avid MediaFiles" is a folder, never a drive); stray
	// audio in a numbered folder is listed and never opened; an unreadable
	// ama* twin beside a readable msm* pair does not fail the folder.
	void manual_path_inside_avid_mediafiles_resolves_to_its_mxf_root();
	void stray_audio_in_an_mxf_folder_is_listed_but_never_opened();
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

	// 5 s is plenty; the parser only touches ~512 KB.
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
	// (the no-mdb partner test confirms that). The genuine signal is the
	// adopted master MOB, which only the MDB re-join can supply. The project
	// comes from the file's own header (the same `_PJ` Avid reads when it
	// rebuilds a PMR), and the status stays honest: there is no PMR here, so
	// the folder has no index to list the file in.
	QVERIFY(!mf.isInvalidUmid); // a good UMID is what the lookup keys on
	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
	QCOMPARE(mf.project, QStringLiteral("block 1729"));
	const QString adoptedMob =
		QStringLiteral("060a2b3401010105.01010f1013000000.d2467dea74110690.91901e6a605d3613");
	QCOMPARE(mf.masterMobId, adoptedMob); // the clip, adopted from the MDB record
	QVERIFY(mf.mobId.isEmpty());		  // the file mob is not guessed from the header
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
	QVERIFY(mf.mobId.isEmpty());							// no MOB adopted — the discriminator vs the recovered case
	QVERIFY(mf.masterMobId.isEmpty());
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

void TestScanner::unreadable_header_stays_media()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	// A render-shaped FILENAME on a file whose header is unreadable. Only the
	// UsageCode can classify, and there isn't one to read — so the row stays
	// Media rather than being guessed at from its name.
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
	QCOMPARE(mf.type, MediaFile::Type::Media);
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
		// cancelled scan's cached MDB and adopts a master MOB from it.
		QCOMPARE(results.first().dbStatus, MediaFile::DbStatus::NoDatabase);
		QVERIFY2(results.first().masterMobId.isEmpty(),
				 qPrintable(QStringLiteral("stale cache leaked; master MOB = ") +
							results.first().masterMobId));
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

	QVector<MediaFile> runScan(const QString &root, bool forceHeaderScan = false)
	{
		MediaScanner scanner;
		QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
		MediaScanner::Options opts;
		opts.volumePaths = QStringList{root};
		opts.forceHeaderScan = forceHeaderScan;
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

void TestScanner::force_header_scan_reads_every_header()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(kToneName, folder);
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified);

	// Forced: the header is read (MaterialPackage name outranks the MDB's),
	// while identity from the databases still applies.
	const auto forced = runScan(tmp.path(), /*forceHeaderScan=*/true);
	QCOMPARE(forced.size(), 1);
	QCOMPARE(forced.first().clipName, kToneClip);
	QCOMPARE(forced.first().clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QCOMPARE(forced.first().project, QStringLiteral("block 1729"));
	QVERIFY(!forced.first().originalBin.isEmpty());
	QCOMPARE(forced.first().kind, MediaFile::Kind::Audio);

	// Default: the same file, the same facts, no header read.
	const auto normal = runScan(tmp.path());
	QCOMPARE(normal.size(), 1);
	QCOMPARE(normal.first().clipNameSource, MediaFile::ClipNameSource::Mdb);
	QCOMPARE(normal.first().codec, forced.first().codec);
	QCOMPARE(normal.first().sampleRate, forced.first().sampleRate);
	QCOMPARE(normal.first().channels, forced.first().channels);
	QCOMPARE(normal.first().bitDepth, forced.first().bitDepth);
	QCOMPARE(normal.first().durationFrames, forced.first().durationFrames);
	QCOMPARE(normal.first().timecodeBase, forced.first().timecodeBase);
	QCOMPARE(normal.first().originalBin, forced.first().originalBin);
	QCOMPARE(normal.first().mobId, forced.first().mobId);
	QCOMPARE(normal.first().masterMobId, forced.first().masterMobId);
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
	/// Every trailer in the shipped 80-pair PMR.
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
		QCOMPARE(mf.codec, QString::fromLatin1(kPcmAudioName));
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
	// the project and the FILE mob, and the re-join through the MDB by the
	// master's id recovers the bin and the master mob — the OMF twin of
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
		QCOMPARE(mf.codec, QString::fromLatin1(kPcmAudioName));
		QCOMPARE(mf.clipName, kOmfWavClip);
		QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
		QCOMPARE(mf.project, kOmfProject);
		QCOMPARE(mf.mobId, kOmfWavFileMob);		  // the file's own identity
		QCOMPARE(mf.masterMobId, kOmfWavMasterMob); // adopted from the MDB record
		QCOMPARE(mf.originalBin, kOmfWavBin);		  // which is the only place a bin lives
		QVERIFY(!mf.isInvalidUmid);
	}

	// With no database at all the tail still names the clip, the project and
	// the file mob; the master and the bin have no source and stay blank.
	QVERIFY(QFile::remove(omfRoot + QStringLiteral("/msmMMOB.mdb")));
	{
		const auto results = runScan(tmp.path());
		QCOMPARE(results.size(), 1);
		const MediaFile &mf = results.first();
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
		QCOMPARE(mf.clipName, kOmfWavClip);
		QCOMPARE(mf.project, kOmfProject);
		QCOMPARE(mf.mobId, kOmfWavFileMob);
		QVERIFY(mf.masterMobId.isEmpty());
		QVERIFY(mf.originalBin.isEmpty());
		QCOMPARE(mf.codec, QString::fromLatin1(kPcmAudioName));
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

void TestScanner::stray_audio_in_an_mxf_folder_is_listed_but_never_opened()
{
	// A real OMF-written .wav dropped into an MXF-era numbered folder. It
	// is admitted to the table (name, size, date) as it always was, but it
	// is NOT legacy essence in the scanner's eyes — only an OMFI MediaFiles
	// root is — so its tail is never read: no codec, no clip name, no MOB,
	// and the folder's PMR (which does not list it) makes it a real miss.
	// Had OmfParser opened it, the row would say PCM and name the tone.
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