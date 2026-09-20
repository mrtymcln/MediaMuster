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
	void unlisted_media_with_readable_databases_is_no_reference();
	void corrupt_pmr_flags_no_database_and_mdb_still_recovers();
	void structurally_incomplete_pmr_is_not_a_trusted_index_data();
	void structurally_incomplete_pmr_is_not_a_trusted_index();
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
	void precompute_category_from_current_database_data();
	void precompute_category_from_current_database();
	void precompute_category_conflict_and_stale_database();
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

	// Managed media trees may be copied anywhere and selected directly.
	// OMFI media is flat or one workstation folder below OMFI MediaFiles;
	// selecting an arbitrary ancestor does not start a recursive search.
	void omf_is_disabled_for_all_path_shapes_data();
	void omf_is_disabled_for_all_path_shapes();
	void omf_disabled_preserves_mxf_and_its_databases_data();
	void omf_disabled_preserves_mxf_and_its_databases();
	void unsupported_file_suffixes_are_ignored_data();
	void unsupported_file_suffixes_are_ignored();
	void stale_omf_database_does_not_describe_replacement();
	void incomplete_omf_database_recovers_file_identity_from_header_data();
	void incomplete_omf_database_recovers_file_identity_from_header();
	void omf_volume_root_scans_both_folders();
	void copied_media_tree_requires_its_direct_base_or_managed_root();
	void omf_root_pointed_at_directly_never_scans_as_mxf_folders();
	void omf_root_without_a_pmr_gets_identity_from_its_header();
	void omf_video_rows_show_avid_short_names();
	void shared_omf_folder_uses_current_databases_and_header_fallback();
	void shared_omf_folder_without_pmr_uses_its_media_headers();
	void shared_omf_folder_without_any_database_is_scanned();
	void unrelated_database_cannot_reclassify_omf_audio_data();
	void unrelated_database_cannot_reclassify_omf_audio();
	void incomplete_omf_audio_in_a_shared_folder_keeps_its_format();
	void incomplete_omf_identity_clears_unrelated_stale_database_metadata();
	void reserved_folders_are_skipped_under_mxf_and_omfi();
	void current_omf_database_does_not_open_media_data();
	void current_omf_database_does_not_open_media();
	void ama_databases_are_read();

	// What the OMF-era rework must NOT have changed for MXF-era media: a
	// hand-added path of any shape still resolves to its MXF root (and a
	// derived ".../Avid MediaFiles" is a folder, never a drive);
	// an unreadable ama* twin beside a readable msm* pair does not fail the folder.
	void manual_path_inside_avid_mediafiles_resolves_to_its_mxf_root();
	void ume_paths_are_ignored_data();
	void ume_paths_are_ignored();
	void ume_exclusion_preserves_supported_siblings();
	void overlapping_volume_and_manual_roots_scan_each_folder_once();
	void case_distinct_shared_folders_remain_distinct();
	void cross_format_files_are_excluded_even_with_omf_enabled();
	void unsupported_manual_locations_are_rejected_data();
	void unsupported_manual_locations_are_rejected();
	void mxf_quarantined_files_remain_diagnostic();
	void media_file_symlinks_are_excluded_data();
	void media_file_symlinks_are_excluded();
	void quarantine_alias_cannot_expand_a_normal_media_folder();
	void ordinary_wave_outside_omfi_is_excluded_data();
	void ordinary_wave_outside_omfi_is_excluded();
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
	QCOMPARE(mf.mediaFolderName, QStringLiteral("1"));
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
// neither (an unreadable media header cannot supply a matching ID). This is the
// only combination allowed to claim "No reference".
void TestScanner::unlisted_media_with_readable_databases_is_no_reference()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));

	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);

	writeJunk(folder + QStringLiteral("/stray.mxf"), 16);

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.includeOmf = true;
	opts.volumePaths = QStringList{tmp.path()};
	scanner.startScan(opts);

	QVERIFY2(finishedSpy.wait(5000), "MediaScanner::scanFinished did not fire within 5 s");
	const auto results = finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(results.size(), 1);
	const MediaFile &mf = results.first();

	QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoReference); // PMR readable, file not in it
	QVERIFY(mf.hasNoProject()); // the unreadable header cannot name a project
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

	writeJunk(folder + QStringLiteral("/stray.mxf"), 16);

	MediaScanner scanner;
	QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);

	MediaScanner::Options opts;
	opts.includeOmf = true;
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

	// The stray file has no readable identity; nothing can vouch for it while the
	// PMR is dead, so it must NOT be claimed as a verified miss.
	const MediaFile *stray = findByName(QStringLiteral("stray.mxf"));
	QVERIFY(stray != nullptr);
	QCOMPARE(stray->dbStatus, MediaFile::DbStatus::DbUnreadable);
	QVERIFY(stray->hasNoProject());
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
		key.append(char(0x01));	   // byte[13]
		key.append(char(setType)); // byte[14] — the set type
		key.append(char(0x00));	   // byte[15]
		QByteArray out = key;
		out.append(char(value.size())); // BER short form
		out += value;
		return out;
	}

	/// A CDCI picture descriptor carrying stored width/height. Without one the
	/// parse yields MediaMetadata::valid == false and applyMediaMetadata skips
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
	// applyMediaMetadata skips every field and the test would pass for the
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

	QByteArray singleLegacyPmr(const QByteArray &name, quint32 modified)
	{
		// ReadPmrRec version 1: file UID8, name length/name, DTM.
		return BentoBuilder::le32(0x7a9) + BentoBuilder::le32(1) + BentoBuilder::le32(1) +
			   TestOmf::uid(2).mid(4) + BentoBuilder::le32(quint32(name.size())).left(2) + name +
			   BentoBuilder::le32(modified);
	}

	QByteArray waveOmf(bool omf2, bool includeMaster = true, quint32 fileUid = 2)
	{
		// A supported WAVE descriptor with an explicit master-to-file graph.
		// OMF2 deliberately omits Avid's UsageCode: identity must survive
		// even when the media/precompute classification is unknown.
		TestOmf::Writer w(omf2, false);
		const auto object = [&](const char *cls)
		{
			const quint32 obj = w.addObject(cls);
			if (omf2)
				w.setImmediate(obj, "OMFI:OOBJ:ObjClass", QByteArray(cls, 4));
			return obj;
		};
		const auto ref = [&](quint32 obj, const char *prop, quint32 target)
		{
			if (omf2)
				w.set(obj, prop, w.word(target));
			else
				w.setHandle(obj, prop, target);
		};
		const auto refs = [&](quint32 obj, const char *prop, quint32 target)
		{
			if (omf2)
				w.set(obj, prop, w.half(1) + w.word(target));
			else
				w.setHandles(obj, prop, {target});
		};
		w.setImmediate(1, omf2 ? "OMFI:OOBJ:ObjClass" : "OMFI:ObjID", QByteArray("HEAD", 4));
		w.setImmediate(1, omf2 ? "OMFI:HEAD:Version" : "OMFI:Version", QByteArray::fromHex(omf2 ? "0200" : "0100"));
		w.setImmediate(1, omf2 ? "OMFI:HEAD:ByteOrder" : "OMFI:ByteOrder", QByteArray("II", 2));
		const quint32 master = includeMaster ? object(omf2 ? "MMOB" : "MOBJ") : 0;
		const quint32 file = object(omf2 ? "SMOB" : "MOBJ");
		const quint32 desc = object("WAVD"), data = object("WAVE");
		w.set(file, "OMFI:MOBJ:MobID", TestOmf::uid(fileUid));
		w.set(data, omf2 ? "OMFI:MDAT:MobID" : "OMFI:WAVE:MobID", TestOmf::uid(fileUid));
		ref(file, omf2 ? "OMFI:SMOB:MediaDescription" : "OMFI:MOBJ:PhysicalMedia", desc);
		if (includeMaster)
		{
			w.set(master, "OMFI:MOBJ:MobID", TestOmf::uid(1));
			w.setString(master, omf2 ? "OMFI:MOBJ:Name" : "OMFI:CPNT:Name", "WAVE clip");
			if (!omf2)
				w.setU32(master, "OMFI:MOBJ:UsageCode", 7);
			const quint32 attrs = object("ATTR"), attr = object("ATTB");
			ref(master, omf2 ? "OMFI:MOBJ:UserAttributes" : "OMFI:CPNT:Attributes", attrs);
			refs(attrs, "OMFI:ATTR:AttrRefs", attr);
			w.setString(attr, "OMFI:ATTB:Name", "_PJ");
			w.setU32(attr, "OMFI:ATTB:Kind", 2);
			w.setString(attr, "OMFI:ATTB:StringAttribute", "WAVE project");
		}
		const QByteArray fmt = w.half(1) + w.half(2) + w.word(48000) + w.word(288000) + w.half(6) + w.half(24);
		const QByteArray body = QByteArray("WAVEfmt ") + w.word(quint32(fmt.size())) + fmt;
		w.set(desc, "OMFI:WAVD:Summary", QByteArray("RIFF") + w.word(quint32(body.size())) + body);
		w.setRational(desc, "OMFI:MDFL:SampleRate", 48000, 1);
		w.set(desc, "OMFI:MDFL:Length", omf2 ? w.wide(96000) : w.word(96000));
		for (const quint32 mob : {master, file})
		{
			if (mob == 0)
				continue;
			const quint32 track = object(omf2 ? "MSLT" : "TRAK"), clip = object("SCLP");
			refs(mob, omf2 ? "OMFI:MOBJ:Slots" : "OMFI:TRKG:Tracks", track);
			ref(track, omf2 ? "OMFI:MSLT:Segment" : "OMFI:TRAK:TrackComponent", clip);
			w.setRational(omf2 ? track : mob, omf2 ? "OMFI:MSLT:EditRate" : "OMFI:CPNT:EditRate", 25, 1);
			w.set(clip, "OMFI:SCLP:SourceID", TestOmf::uid(mob == master ? fileUid : 3));
		}
		return w.build();
	}

	QByteArray incompleteWaveOmf(quint32 fileUid)
	{
		BentoBuilder w;
		const quint32 head = w.addObject("HEAD"), file = w.addObject("MOBJ"), desc = w.addObject("WAVD");
		w.setImmediate(head, "OMFI:Version", QByteArray::fromHex("0100"));
		w.set(file, "OMFI:MOBJ:MobID", TestOmf::uid(fileUid));
		w.setHandle(file, "OMFI:MOBJ:PhysicalMedia", desc);
		// File identity and a selected descriptor exist, but neither technical
		// properties nor a master establish usable metadata or classification.
		return w.build();
	}

	QByteArray categoryDatabase(const QByteArray &masterId, const QByteArray &fileId,
								bool importObject, int videoTracks, bool malformedAttribute = false)
	{
		BentoBuilder w;
		const quint32 head = w.addObject("HEAD");
		w.setImmediate(head, "OMFI:Version", QByteArray::fromHex("0100"));
		const quint32 master = w.addObject("MOBJ"), file = w.addObject("MOBJ"), pcm = w.addObject("PCMA");
		w.set(master, "OMFI:MOBJ:MobID", masterId);
		w.setU32(master, "OMFI:MOBJ:UsageCode", 1);
		w.setString(master, "OMFI:CPNT:Name", "Sequence,Resize+1");
		if (importObject)
		{
			const quint32 attrs = w.addObject("ATTR"), attr = w.addObject("ATTB");
			w.setHandle(master, "OMFI:CPNT:Attributes", attrs);
			w.setHandles(attrs, "OMFI:ATTR:AttrRefs", {attr});
			w.setString(attr, "OMFI:ATTB:Name", "_IMPORTSETTING");
			if (malformedAttribute)
				w.setU32(attr, "OMFI:ATTB:Kind", 3);
			else
				w.setU16(attr, "OMFI:ATTB:Kind", 3);
			w.setHandle(attr, "OMFI:ATTB:ObjAttribute", 0); // Avid tests the found kind, not the payload.
		}
		QVector<quint32> tracks;
		for (int n = 0; n < videoTracks; ++n)
		{
			const quint32 track = w.addObject("TRAK"), component = w.addObject("SCLP");
			w.setHandle(track, "OMFI:TRAK:TrackComponent", component);
			w.setU16(component, "OMFI:CPNT:TrackKind", 1);
			tracks.append(track);
		}
		w.setHandles(master, "OMFI:TRKG:Tracks", tracks);
		w.set(file, "OMFI:MOBJ:MobID", fileId);
		w.setHandle(file, "OMFI:MOBJ:PhysicalMedia", pcm);
		w.setRational(file, "OMFI:CPNT:EditRate", 25, 1);
		w.setRational(pcm, "OMFI:MDFL:SampleRate", 48000, 1);
		w.setU32(pcm, "OMFI:MDFL:Length", 96000);
		w.setU16(pcm, "OMFI:MDAU:BitsPerSample", 24);
		w.setU16(pcm, "OMFI:MDAU:NumChannels", 1);
		return w.build();
	}

	QVector<MediaFile> runScan(const QString &root, bool includeOmf = false)
	{
		MediaScanner scanner;
		QSignalSpy finishedSpy(&scanner, &MediaScanner::scanFinished);
		MediaScanner::Options opts;
		opts.volumePaths = QStringList{root};
		opts.includeOmf = includeOmf;
		scanner.startScan(opts);
		if (!finishedSpy.wait(5000))
			return {};
		return finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	}
} // namespace

void TestScanner::structurally_incomplete_pmr_is_not_a_trusted_index_data()
{
	QTest::addColumn<QByteArray>("pmr");
	QTest::addColumn<bool>("readable");
	QTest::addColumn<bool>("listsTone");

	QFile fixture(fixturesDir() + QStringLiteral("/msmFMID.pmr"));
	QVERIFY(fixture.open(QIODevice::ReadOnly));
	const QByteArray original = fixture.readAll();
	QCOMPARE(fixture.error(), QFileDevice::NoError);
	// The real fixture contains one MBCS record, followed by one Unicode
	// record for the same file. Pin the framing before changing only counts
	// or the boundary of a section; the media/database fixtures stay intact.
	QCOMPARE(original.size(), 248);
	QCOMPARE(original.left(12), BentoBuilder::le32(0x7a9) + BentoBuilder::le32(8) + BentoBuilder::le32(1));
	constexpr qsizetype unicodeOffset = 125;
	QCOMPARE(original.mid(unicodeOffset, 8), BentoBuilder::le32(16) + BentoBuilder::le32(1));
	QTest::newRow("healthy") << original << true << true;

	QByteArray empty = original.left(12);
	empty.replace(8, 4, BentoBuilder::le32(0));
	QTest::newRow("valid-empty") << empty << true << false;

	QByteArray primaryCount = original;
	primaryCount.replace(8, 4, BentoBuilder::le32(0));
	QTest::newRow("under-declared-primary-count") << primaryCount << false << false;

	QByteArray unicodeCount = original;
	unicodeCount.replace(unicodeOffset + 4, 4, BentoBuilder::le32(0));
	QTest::newRow("under-declared-unicode-count") << unicodeCount << false << false;

	QByteArray unknownExtension = original;
	unknownExtension.replace(unicodeOffset, 4, BentoBuilder::le32(17));
	QTest::newRow("unknown-extension") << unknownExtension << false << false;
	QTest::newRow("trailing-bytes") << original + QByteArray("unexpected") << false << false;
	QTest::newRow("truncated-unicode-header") << original.left(unicodeOffset + 4) << false << false;
	QTest::newRow("truncated-unicode-record") << original.left(original.size() - 1) << false << false;
}

void TestScanner::structurally_incomplete_pmr_is_not_a_trusted_index()
{
	QFETCH(QByteArray, pmr);
	QFETCH(bool, readable);
	QFETCH(bool, listsTone);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), pmr));
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(kToneName, folder);
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified);
	QVERIFY(tryWriteFile(folder + QStringLiteral("/stray.mxf"), QByteArray(16, '\x11')));

	const auto results = runScan(tmp.path(), true);
	QCOMPARE(results.size(), 2);
	const MediaFile *tone = nullptr;
	const MediaFile *stray = nullptr;
	for (const MediaFile &mf : results)
	{
		if (mf.fileName == kToneName)
			tone = &mf;
		else if (mf.fileName == QStringLiteral("stray.mxf"))
			stray = &mf;
	}
	QVERIFY(tone != nullptr);
	QVERIFY(stray != nullptr);

	// A partial entry must not make the MXF Listed, and a partial/empty
	// result must not make the stray file a verified No Reference match.
	// Only a complete index, including the genuinely empty control, can
	// certify that miss.
	const auto unlistedStatus = readable ? MediaFile::DbStatus::NoReference : MediaFile::DbStatus::DbUnreadable;
	QCOMPARE(tone->dbStatus, listsTone ? MediaFile::DbStatus::Listed : unlistedStatus);
	QCOMPARE(stray->dbStatus, unlistedStatus);
	QCOMPARE(tone->isNoDatabase(), !readable);
	QCOMPARE(stray->isNoDatabase(), !readable);
	if (!readable)
	{
		QCOMPARE(tone->dbStatusText().label, QStringLiteral("No Database"));
		QCOMPARE(stray->dbStatusText().label, QStringLiteral("No Database"));
	}

	// Damaged PMR data cannot skip the header read just because its surviving
	// record has a matching timestamp. The intact header/MDB still recover
	// descriptive metadata; the MDB-only bin name proves the re-join ran.
	QCOMPARE(tone->databaseMetadataCurrent, listsTone);
	QCOMPARE(tone->clipNameSource, listsTone ? MediaFile::ClipNameSource::Mdb : MediaFile::ClipNameSource::MaterialPackage);
	QCOMPARE(tone->clipName, kToneClip);
	QCOMPARE(tone->project, QStringLiteral("block 1729"));
	QCOMPARE(tone->masterMobId, QStringLiteral("060a2b3401010105.01010f1013000000.d2467dea74110690.91901e6a605d3613"));
	QVERIFY(!tone->originalBin.isEmpty());
	QCOMPARE(tone->kind, MediaFile::Kind::Audio);
	QVERIFY(tone->sampleRate > 0);
}

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
	const MediaMetadata header = MxfParser::parseHeader(replacement);
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
	QCOMPARE(mf.precomputeCategory, header.precomputeCategory);
	QCOMPARE(mf.sampleRate, 0);
	QCOMPARE(mf.codec, header.codec);
}

void TestScanner::precompute_category_from_current_database_data()
{
	QTest::addColumn<bool>("importObject");
	QTest::addColumn<int>("videoTracks");
	QTest::addColumn<bool>("malformedAttribute");
	QTest::addColumn<QString>("expected");
	QTest::newRow("render-no-import") << false << 0 << false << QStringLiteral("Rendered Effects");
	QTest::newRow("two-track-import-null-payload") << true << 2 << false << QStringLiteral("Titles and Matte Keys");
	QTest::newRow("one-track-import") << true << 1 << false << QStringLiteral("Rendered Effects");
	QTest::newRow("unreadable-kind") << true << 2 << true << QStringLiteral("unknown");
}

void TestScanner::precompute_category_from_current_database()
{
	QFETCH(bool, importObject);
	QFETCH(int, videoTracks);
	QFETCH(bool, malformedAttribute);
	QFETCH(QString, expected);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"),
						 categoryDatabase(kLadderMob, kToneFileId, importObject, videoTracks, malformedAttribute)));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"),
						 singlePmr("render.mxf", kToneFileId, kLadderMob, {}, kToneModified)));
	writeJunk(folder + QStringLiteral("/render.mxf"), 4096);
	setModified(folder + QStringLiteral("/render.mxf"), kToneModified);
	const auto rows = runScan(tmp.path());
	QCOMPARE(rows.size(), 1);
	const auto &mf = rows.first();
	QVERIFY(mf.databaseMetadataCurrent);
	QVERIFY(mf.needsHeaderRead); // Missing project; failed read must preserve current database evidence.
	QCOMPARE(mf.type, MediaFile::Type::Precompute);
	QCOMPARE(mf.precomputeCategoryDisplay(), expected);
	QCOMPARE(mf.effect, QStringLiteral("Resize")); // Name-derived details never decide the parent.
}

void TestScanner::precompute_category_conflict_and_stale_database()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	const QString fixture = QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seqDD866C6BV.mxf");
	const auto header = MxfParser::parseHeader(fixturesDir() + QLatin1Char('/') + fixture);
	QCOMPARE(header.precomputeCategory, MediaFile::PrecomputeCategory::RenderedEffects);
	const QByteArray fileId = QByteArray::fromHex(MobId::toPmrForm(header.fileMobId).toLatin1());
	const QByteArray masterId = QByteArray::fromHex(MobId::toPmrForm(header.umid).toLatin1());
	QVERIFY(QFile::copy(fixturesDir() + QLatin1Char('/') + fixture, folder + QStringLiteral("/render.mxf")));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"), categoryDatabase(masterId, fileId, true, 2)));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), singlePmr("render.mxf", fileId, masterId, {}, kToneModified)));
	setModified(folder + QStringLiteral("/render.mxf"), kToneModified);
	const auto current = runScan(tmp.path());
	QCOMPARE(current.size(), 1);
	QVERIFY(current.first().databaseMetadataCurrent);
	QCOMPARE(current.first().type, MediaFile::Type::Precompute);
	QCOMPARE(current.first().precomputeCategoryDisplay(), QStringLiteral("unknown"));
	setModified(folder + QStringLiteral("/render.mxf"), kToneModified + 10);
	const auto stale = runScan(tmp.path());
	QCOMPARE(stale.size(), 1);
	QVERIFY(!stale.first().databaseMetadataCurrent);
	QCOMPARE(stale.first().precomputeCategory, MediaFile::PrecomputeCategory::RenderedEffects);
}

void TestScanner::pmr_v1_recovers_unique_master_from_mdb()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"), waveOmf(false)));
	const QByteArray name("sample.omf");
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), singleLegacyPmr(name, kToneModified)));
	writeJunk(folder + QStringLiteral("/sample.omf"), 4096);
	setModified(folder + QStringLiteral("/sample.omf"), kToneModified);
	const auto rows = runScan(tmp.path(), true);
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QCOMPARE(mf.mobId, OmfUid::canonicalHex(TestOmf::uid(2)));
	QCOMPARE(mf.masterMobId, OmfUid::canonicalHex(TestOmf::uid(1)));
	QCOMPARE(mf.project, QStringLiteral("WAVE project"));
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
	QVERIFY(tryWriteFile(folder + QStringLiteral("/sample.omf"), waveOmf(true)));
	const auto rows = runScan(tmp.path(), true);
	QCOMPARE(rows.size(), 1);
	const MediaFile &mf = rows.first();
	QCOMPARE(mf.masterMobId, OmfUid::canonicalHex(TestOmf::uid(1)));
	QCOMPARE(mf.mobId, OmfUid::canonicalHex(TestOmf::uid(2)));
	QCOMPARE(mf.kind, MediaFile::Kind::Audio);
	QCOMPARE(mf.type, MediaFile::Type::Unknown); // MMOB identity and Avid render classification are separate
	QCOMPARE(mf.project, QStringLiteral("WAVE project"));
}

void TestScanner::mxf_header_keeps_master_identity_with_unknown_classification()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(folder));
	const auto set = [](quint8 type, const QByteArray &value)
	{
		QByteArray key = QByteArray::fromHex("060e2b34025301010d01010101010000");
		key[14] = char(type);
		return key + QByteArray(1, char(value.size())) + value;
	};
	QByteArray name;
	for (char c : QByteArray("Sequence,3D_Warp+1"))
	{
		name.append('\0');
		name.append(c);
	}
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
	const MediaMetadata parsed = MxfParser::parseHeader(path);
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
		{
			QTest::qFail("MediaScanner::scanFinished did not fire within 10 s", __FILE__, __LINE__);
			return {};
		}
		return finishedSpy.takeFirst().at(0).value<QVector<MediaFile>>();
	}

	QVector<MediaFile> runManualScan(const QString &folder, bool includeOmf = false)
	{
		MediaScanner::Options opts;
		opts.manualPaths = QStringList{folder};
		opts.includeOmf = includeOmf;
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
	void checkOmfAudioMetadata(const MediaFile &mf, const QString &clip, const QString &bin,
							   const QString &fileMob, const QString &masterMob)
	{
		QVERIFY(mf.omfEra);
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

	void checkOmfAudioRow(const MediaFile &mf, const QString &clip, const QString &bin, const QString &fileMob,
						  const QString &masterMob)
	{
		QCOMPARE(mf.mediaFolderName, kOmfFolder);
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::Listed);
		checkOmfAudioMetadata(mf, clip, bin, fileMob, masterMob);
	}
} // namespace

void TestScanner::omf_is_disabled_for_all_path_shapes_data()
{
	QTest::addColumn<QString>("shape");
	QTest::addColumn<bool>("manual");
	QTest::newRow("volume-root") << QStringLiteral("root") << false;
	QTest::newRow("manual-root") << QStringLiteral("root") << true;
	QTest::newRow("manual-nested") << QStringLiteral("nested") << true;
	QTest::newRow("manual-direct") << QStringLiteral("direct") << true;
	QTest::newRow("manual-descendant") << QStringLiteral("descendant") << true;
	QTest::newRow("volume-descendant") << QStringLiteral("descendant") << false;
	QTest::newRow("manual-renamed") << QStringLiteral("renamed") << true;
}

void TestScanner::omf_is_disabled_for_all_path_shapes()
{
	QFETCH(QString, shape);
	QFETCH(bool, manual);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QString legacyFolder = Conventions::omfRootUnder(tmp.path());
	if (shape == QStringLiteral("nested"))
		legacyFolder = Conventions::omfRootUnder(tmp.path() + QStringLiteral("/Archive/Project"));
	else if (shape == QStringLiteral("renamed"))
		legacyFolder = tmp.path() + QStringLiteral("/Legacy archive");
	QVERIFY(QDir().mkpath(legacyFolder));
	copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), legacyFolder);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), legacyFolder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, legacyFolder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfAif, legacyFolder);
	QVERIFY(tryWriteFile(legacyFolder + QStringLiteral("/sample.OMF"), waveOmf(false)));

	// Even an MXF tree moved inside OMFI must not make a manually added
	// OMFI path or descendant scan as an ordinary media root while disabled.
	const QString descendant = legacyFolder + QStringLiteral("/Archive");
	const QString nestedMxf = Conventions::mxfRootUnder(descendant) + QStringLiteral("/1");
	QVERIFY(QDir().mkpath(nestedMxf));
	copyFixture(kToneName, nestedMxf);
	QString path = tmp.path();
	if (shape == QStringLiteral("direct") || shape == QStringLiteral("renamed"))
		path = legacyFolder;
	else if (shape == QStringLiteral("descendant"))
		path = descendant;

	MediaScanner::Options options;
	QVERIFY(!options.includeOmf);
	if (manual)
		options.manualPaths = QStringList{path};
	else
		options.volumePaths = QStringList{path};
	QVERIFY(runScanWith(options).isEmpty());
}

void TestScanner::omf_disabled_preserves_mxf_and_its_databases_data()
{
	QTest::addColumn<QString>("shape");
	QTest::newRow("volume-root") << QStringLiteral("volume");
	QTest::newRow("manual-root") << QStringLiteral("manual");
	QTest::newRow("manual-nested") << QStringLiteral("nested");
	QTest::newRow("manual-numbered-folder") << QStringLiteral("numbered");
	QTest::newRow("manual-shared-folder") << QStringLiteral("shared");
}

void TestScanner::omf_disabled_preserves_mxf_and_its_databases()
{
	QFETCH(QString, shape);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString project = shape == QStringLiteral("nested")
		? tmp.path() + QStringLiteral("/Archive/Project") : tmp.path();
	const QString folder = Conventions::mxfRootUnder(project) +
		(shape == QStringLiteral("shared") ? QStringLiteral("/Editor.3") : QStringLiteral("/1"));
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	copyFixture(kToneName, folder);
	setModified(folder + QLatin1Char('/') + kToneName, kToneModified);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfAif, folder);
	QVERIFY(tryWriteFile(folder + QStringLiteral("/sample.OMF"), waveOmf(false)));
	const QString omfi = Conventions::omfRootUnder(project);
	QVERIFY(QDir().mkpath(omfi));
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfi);

	MediaScanner::Options options;
	if (shape == QStringLiteral("volume"))
		options.volumePaths = QStringList{tmp.path()};
	else
		options.manualPaths = QStringList{
			shape == QStringLiteral("numbered") || shape == QStringLiteral("shared") ? folder : project};
	const auto results = runScanWith(options);
	QCOMPARE(results.size(), 1);
	const auto &mxf = results.first();
	QCOMPARE(mxf.fileName, kToneName);
	QVERIFY(!mxf.omfEra);
	QVERIFY(mxf.databaseMetadataCurrent);
	QVERIFY(!mxf.needsHeaderRead);
	QCOMPARE(mxf.dbStatus, MediaFile::DbStatus::Listed);
	QCOMPARE(mxf.clipName, kToneClip);
	QCOMPARE(mxf.clipNameSource, MediaFile::ClipNameSource::Mdb);
	QCOMPARE(mxf.sampleRate, 48000);
}

void TestScanner::unsupported_file_suffixes_are_ignored_data()
{
	QTest::addColumn<QString>("suffix");
	QTest::addColumn<bool>("omf");
	for (const QString &suffix : {QStringLiteral(".txt"), QStringLiteral(".xlsx"), QStringLiteral(".sd2"),
		QStringLiteral(".SD2"), QStringLiteral(".aiff"), QStringLiteral(".mov")})
		for (const bool omf : {false, true})
			QTest::newRow(qPrintable(suffix + (omf ? QStringLiteral("-omf") : QStringLiteral("-mxf")))) << suffix << omf;
}

void TestScanner::unsupported_file_suffixes_are_ignored()
{
	QFETCH(QString, suffix);
	QFETCH(bool, omf);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = omf ? Conventions::omfRootUnder(tmp.path())
		: Conventions::mxfRootUnder(tmp.path()) + QStringLiteral("/1");
	QVERIFY(QDir().mkpath(folder));
	const QString ignoredName = QStringLiteral("ignored") + suffix;
	QVERIFY(tryWriteFile(folder + QLatin1Char('/') + ignoredName, QByteArray(128, 'x')));
	const QString supportedName = omf ? kOmfWav : kToneName;
	copyFixture(omf ? QStringLiteral("omf/mc2026_audio/") + supportedName : supportedName, folder);
	const auto rows = runScan(tmp.path(), true);
	QCOMPARE(rows.size(), 1);
	QCOMPARE(rows.first().fileName, supportedName);
	QCOMPARE(rows.first().omfEra, omf);
	QCOMPARE(rows.first().sampleRate, 48000);
}

void TestScanner::stale_omf_database_does_not_describe_replacement()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmMMOB.mdb"), waveOmf(false)));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), singleLegacyPmr("replaced.omf", kToneModified)));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/replaced.omf"), waveOmf(false, true, 4)));
	setModified(folder + QStringLiteral("/replaced.omf"), kToneModified + 10);
	const auto rows = runScan(tmp.path(), true);
	QCOMPARE(rows.size(), 1);
	QCOMPARE(rows.first().fileName, QStringLiteral("replaced.omf"));
	QVERIFY(!rows.first().databaseMetadataCurrent);
	QVERIFY(rows.first().needsHeaderRead);
	QCOMPARE(rows.first().mobId, OmfUid::canonicalHex(TestOmf::uid(4)));
	QCOMPARE(rows.first().codec, QStringLiteral("WAVE (OMF)"));
	QCOMPARE(rows.first().sampleRate, 48000);
}

void TestScanner::incomplete_omf_database_recovers_file_identity_from_header_data()
{
	QTest::addColumn<quint32>("fileUid");
	QTest::newRow("different-identity") << quint32(4);
	QTest::newRow("same-identity") << quint32(2);
}

void TestScanner::incomplete_omf_database_recovers_file_identity_from_header()
{
	QFETCH(quint32, fileUid);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(folder));
	const QString path = folder + QStringLiteral("/replaced.omf");
	const QString mdbPath = folder + QStringLiteral("/msmMMOB.mdb");
	QVERIFY(tryWriteFile(mdbPath, incompleteWaveOmf(2)));
	QVERIFY(tryWriteFile(folder + QStringLiteral("/msmFMID.pmr"), singleLegacyPmr("replaced.omf", kToneModified)));
	QVERIFY(tryWriteFile(path, waveOmf(false, false, fileUid)));
	setModified(path, kToneModified);

	// The indexed timestamp matches, but the database's incomplete
	// technical description requires a header read. The actual
	// WAVE is usable yet lacks a master, so HeaderStatus stays Incomplete.
	const MdbDatabase database = MdbParser::load(mdbPath);
	const QString oldFileId = OmfUid::canonicalHex(TestOmf::uid(2));
	QVERIFY(database.files.contains(oldFileId));
	QVERIFY(!database.files.value(oldFileId).essenceComplete);
	QVERIFY(PmrParser::trailerMatchesModified(kToneModified, QFileInfo(path).lastModified()));
	const OmfMetadata header = OmfParser::parseHeader(path);
	QVERIFY(header.essence.valid);
	QCOMPARE(header.essence.headerStatus, MediaMetadata::HeaderStatus::Incomplete);
	QVERIFY(header.essence.umid.isEmpty());
	QCOMPARE(header.fileMobId, OmfUid::canonicalHex(TestOmf::uid(fileUid)));
	QCOMPARE(header.fileMobId == oldFileId, fileUid == 2);
	QCOMPARE(header.essence.codec, QStringLiteral("WAVE (OMF)"));

	const auto rows = runScan(tmp.path(), true);
	QCOMPARE(rows.size(), 1);
	QCOMPARE(rows.first().fileName, QStringLiteral("replaced.omf"));
	QCOMPARE(rows.first().mobId, header.fileMobId);
	QVERIFY(!rows.first().databaseMetadataCurrent);
	QVERIFY(rows.first().needsHeaderRead);
	QCOMPARE(rows.first().codec, QStringLiteral("WAVE (OMF)"));
	QCOMPARE(rows.first().sampleRate, 48000);
}

void TestScanner::omf_volume_root_scans_both_folders()
{
	// A drive with both roots at its top level: the MXF tree exactly as
	// every test above builds it, and the flat OMF root beside it.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mediaFolderName = tmp.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(QDir().mkpath(mediaFolderName));
	copyFixture(QStringLiteral("msmFMID.pmr"), mediaFolderName);
	copyFixture(QStringLiteral("msmMMOB.mdb"), mediaFolderName);
	copyFixture(kToneName, mediaFolderName);

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

	const auto results = runScan(tmp.path(), true);
	QCOMPARE(results.size(), 3);

	const MediaFile *mxf = rowNamed(results, kToneName);
	QVERIFY(mxf != nullptr);
	QCOMPARE(mxf->mediaFolderName, QStringLiteral("1"));
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

void TestScanner::copied_media_tree_requires_its_direct_base_or_managed_root()
{
	// A copied project retains managed media trees, but selecting an
	// unrelated ancestor must not search down to discover them.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString project = tmp.path() + QStringLiteral("/Project");
	const QString mediaFolderName = Conventions::mxfRootUnder(project) + QStringLiteral("/1");
	QVERIFY(QDir().mkpath(mediaFolderName));
	copyFixture(kToneName, mediaFolderName);
	const QString omfRoot = Conventions::omfRootUnder(project);
	QVERIFY(QDir().mkpath(omfRoot));
	copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), omfRoot);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot);

	QVERIFY(runScan(tmp.path(), true).isEmpty());
	QVERIFY(runManualScan(tmp.path(), true).isEmpty());

	const auto manual = runManualScan(project, true);
	QCOMPARE(manual.size(), 2);
	QVERIFY(rowNamed(manual, kToneName) != nullptr);
	const MediaFile *wav = rowNamed(manual, kOmfWav);
	QVERIFY(wav != nullptr);
	QCOMPARE(wav->mediaFolderName, kOmfFolder);
	QCOMPARE(wav->volumePath, project);

	QCOMPARE(runManualScan(omfRoot, true).size(), 1);
	QCOMPARE(runManualScan(Conventions::mxfRootUnder(project)).size(), 1);
	QCOMPARE(runManualScan(mediaFolderName).size(), 1);
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

	const auto results = runManualScan(omfRoot, true);
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
		const auto results = runScan(tmp.path(), true);
		QCOMPARE(results.size(), 1);
		const MediaFile &mf = results.first();
		QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoDatabase);
		QCOMPARE(mf.mediaFolderName, kOmfFolder);
		QCOMPARE(mf.kind, MediaFile::Kind::Audio);
		QCOMPARE(mf.codec, QStringLiteral("WAVE (OMF)")); // OMF-era: Avid's container label
		QCOMPARE(mf.clipName, kOmfWavClip);
		QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
		QCOMPARE(mf.project, kOmfProject);
		QCOMPARE(mf.mobId, kOmfWavFileMob);			// the file's own identity
		QCOMPARE(mf.masterMobId, kOmfWavMasterMob); // verified by the file's graph
		QCOMPARE(mf.originalBin, kOmfWavBin);		// which is the only place a bin lives
		QVERIFY(!mf.isInvalidUmid);
	}

	// With no database at all the tail still names the clip, the project and
	// the file mob and master; this file's bin only lives in the MDB.
	QVERIFY(QFile::remove(omfRoot + QStringLiteral("/msmMMOB.mdb")));
	{
		const auto results = runScan(tmp.path(), true);
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

	const auto results = runScan(tmp.path(), true);
	QCOMPARE(results.size(), 3);
	for (const Pin &pin : kPins)
	{
		const MediaFile *mf = rowNamed(results, QLatin1String(pin.file));
		QVERIFY2(mf != nullptr, pin.file);
		QCOMPARE(mf->mediaFolderName, kOmfFolder);
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

void TestScanner::shared_omf_folder_uses_current_databases_and_header_fallback()
{
	// A shared-storage workstation folder owns its own database pair.
	// Current records describe the media without a header read; stale and
	// unlisted media recover their metadata from the individual files.
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
	const QString folder = Conventions::omfRootUnder(tmp.path()) + QStringLiteral("/EditorOne");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("omf/avid_supporting/msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("omf/avid_supporting/msmMMOB.mdb"), folder);
	for (const Pin &pin : kPins)
	{
		copyFixture(QStringLiteral("omf/avid_supporting/") + QLatin1String(pin.file), folder);
		if (pin.stamp)
			setModified(folder + QLatin1Char('/') + QLatin1String(pin.file), kSlateModified);
	}
	// A WAVE file these databases do not list: its own tail establishes
	// OMF and names the clip independently of every other file in the folder.
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);

	const auto results = runManualScan(folder, true);
	QCOMPARE(results.size(), 4);
	for (const Pin &pin : kPins)
	{
		const MediaFile *mf = rowNamed(results, QLatin1String(pin.file));
		QVERIFY2(mf != nullptr, pin.file);
		QVERIFY2(mf->omfEra, pin.file);
		QCOMPARE(mf->mediaFolderName, QStringLiteral("EditorOne"));
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

void TestScanner::shared_omf_folder_without_pmr_uses_its_media_headers()
{
	// The same folder with only the clip database. No PMR means no index
	// to be listed in and no way to establish database freshness. Both files
	// are read from their own tails, and the slate's MDB record joins by identity.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path()) + QStringLiteral("/EditorOne");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("omf/avid_supporting/msmMMOB.mdb"), folder);
	copyFixture(QStringLiteral("omf/avid_supporting/BLACK_720x243x2_JFIF35.omf"), folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);

	const auto results = runManualScan(folder, true);
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

void TestScanner::shared_omf_folder_without_any_database_is_scanned()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString base = tmp.path() + QStringLiteral("/Copied project");
	const QString root = Conventions::omfRootUnder(base);
	const QString folder = root + QStringLiteral("/EditorOne");
	QVERIFY(QDir().mkpath(folder + QStringLiteral("/Old")));
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfAif, folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder + QStringLiteral("/Old"));
	for (const QString &selected : {base, root, folder})
	{
		const auto rows = runManualScan(selected, true);
		QCOMPARE(rows.size(), 2);
		const MediaFile *wav = rowNamed(rows, kOmfWav);
		const MediaFile *aif = rowNamed(rows, kOmfAif);
		QVERIFY(wav != nullptr);
		QVERIFY(aif != nullptr);
		checkOmfAudioMetadata(*wav, kOmfWavClip, {}, kOmfWavFileMob, kOmfWavMasterMob);
		checkOmfAudioMetadata(*aif, kOmfAifClip, {}, kOmfAifFileMob, kOmfAifMasterMob);
		for (const MediaFile &row : rows)
		{
			QCOMPARE(row.dbStatus, MediaFile::DbStatus::NoDatabase);
			QCOMPARE(row.mediaFolderName, QStringLiteral("EditorOne"));
			QCOMPARE(QFileInfo(row.filePath).absolutePath(), folder);
			QCOMPARE(row.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
		}
		QVERIFY(runManualScan(selected).isEmpty());
	}
	QVERIFY(runManualScan(folder + QStringLiteral("/Old"), true).isEmpty());
}

void TestScanner::unrelated_database_cannot_reclassify_omf_audio_data()
{
	QTest::addColumn<QString>("databaseState");
	QTest::newRow("current-matching-pair") << QStringLiteral("current");
	QTest::newRow("stale-matching-pair") << QStringLiteral("stale");
	QTest::newRow("matching-mdb-only") << QStringLiteral("mdb-only");
	QTest::newRow("matching-pmr-only") << QStringLiteral("pmr-only");
	QTest::newRow("unrelated-records-only") << QStringLiteral("unrelated");
}

void TestScanner::unrelated_database_cannot_reclassify_omf_audio()
{
	QFETCH(QString, databaseState);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString base = tmp.path() + QStringLiteral("/Archived session");
	const QString folder = Conventions::omfRootUnder(base);
	const QString mxfFolder = Conventions::mxfRootUnder(base) + QStringLiteral("/1");
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(QDir().mkpath(mxfFolder));
	const bool unrelated = databaseState == QStringLiteral("unrelated");
	const bool matchingPmr = !unrelated && databaseState != QStringLiteral("mdb-only");
	const bool matchingMdb = !unrelated && databaseState != QStringLiteral("pmr-only");
	if (matchingPmr)
		copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), folder);
	if (matchingMdb)
		copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), folder);
	if (unrelated)
	{
		copyFixture(QStringLiteral("msmFMID.pmr"), folder);
		copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	}
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfAif, folder);
	copyFixture(kToneName, mxfFolder);
	const quint32 staleOffset = databaseState == QStringLiteral("stale") ? 86400u : 0u;
	setModified(folder + QLatin1Char('/') + kOmfWav, kOmfWavModified + staleOffset);
	setModified(folder + QLatin1Char('/') + kOmfAif, kOmfAifModified + staleOffset);
	setModified(mxfFolder + QLatin1Char('/') + kToneName, kToneModified);

	struct AudioPin
	{
		QString name;
		QString clip;
		QString bin;
		QString fileMob;
		QString masterMob;
	};
	const AudioPin pins[] = {
		{kOmfWav, kOmfWavClip, kOmfWavBin, kOmfWavFileMob, kOmfWavMasterMob},
		{kOmfAif, kOmfAifClip, kOmfAifBin, kOmfAifFileMob, kOmfAifMasterMob},
	};
	for (bool withUnrelatedIndex : {false, true})
	{
		if (withUnrelatedIndex)
			QVERIFY(QFile::copy(fixturesDir() + QStringLiteral("/msmFMID.pmr"),
							   folder + QStringLiteral("/amaFMID.pmr")));
		const auto rows = runManualScan(base, true);
		QCOMPARE(rows.size(), 3);
		for (const AudioPin &pin : pins)
		{
			const MediaFile *row = rowNamed(rows, pin.name);
			QVERIFY(row != nullptr);
			checkOmfAudioMetadata(*row, pin.clip, matchingMdb ? pin.bin : QString{}, pin.fileMob, pin.masterMob);
			QCOMPARE(row->mediaFolderName, kOmfFolder);
			QCOMPARE(row->clipNameSource, databaseState == QStringLiteral("current")
				? MediaFile::ClipNameSource::Mdb : MediaFile::ClipNameSource::MaterialPackage);
			QCOMPARE(row->databaseMetadataCurrent, databaseState == QStringLiteral("current"));
			const auto expectedStatus = matchingPmr ? MediaFile::DbStatus::Listed
				: (withUnrelatedIndex || unrelated) ? MediaFile::DbStatus::NoReference
				: MediaFile::DbStatus::NoDatabase;
			QCOMPARE(row->dbStatus, expectedStatus);
		}
		const MediaFile *mxf = rowNamed(rows, kToneName);
		QVERIFY(mxf != nullptr);
		QVERIFY(!mxf->omfEra);
		QCOMPARE(mxf->clipName, kToneClip);
		QCOMPARE(mxf->sampleRate, 48000);
	}

	// Disabling OMF remains a hard admission gate, even in a mixed archive.
	const auto publicRows = runManualScan(base);
	QCOMPARE(publicRows.size(), 1);
	QCOMPARE(publicRows.first().fileName, kToneName);
	QVERIFY(!publicRows.first().omfEra);
	QCOMPARE(publicRows.first().clipName, kToneClip);
}

void TestScanner::incomplete_omf_audio_in_a_shared_folder_keeps_its_format()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path()) + QStringLiteral("/EditorOne");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	const QString name = QStringLiteral("incomplete.wav");
	const QString path = folder + QLatin1Char('/') + name;
	QVERIFY(tryWriteFile(path, waveOmf(false, false)));
	QCOMPARE(OmfParser::parseHeader(path).essence.headerStatus, MediaMetadata::HeaderStatus::Incomplete);

	// The selected WAVE descriptor establishes OMF even though no master
	// exists to name the clip or classify it as media versus precompute.
	const auto rows = runManualScan(folder, true);
	QCOMPARE(rows.size(), 1);
	const MediaFile &row = rows.first();
	QVERIFY(row.omfEra);
	QCOMPARE(row.mobId, OmfUid::canonicalHex(TestOmf::uid(2)));
	QVERIFY(row.masterMobId.isEmpty());
	QVERIFY(row.clipName.isEmpty());
	QCOMPARE(row.type, MediaFile::Type::Unknown);
	QCOMPARE(row.kind, MediaFile::Kind::Audio);
	QCOMPARE(row.codec, QStringLiteral("WAVE (OMF)"));
	QCOMPARE(row.sampleRate, 48000);
	QVERIFY(runManualScan(folder).isEmpty());
}

void TestScanner::incomplete_omf_identity_clears_unrelated_stale_database_metadata()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::omfRootUnder(tmp.path()) + QStringLiteral("/EditorOne");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), folder);
	const QString path = folder + QLatin1Char('/') + kOmfWav;
	constexpr quint32 replacementUid = 4;
	QVERIFY(tryWriteFile(path, incompleteWaveOmf(replacementUid)));
	setModified(path, kOmfWavModified + 86400u);

	// The old PMR/MDB has a complete description, but its timestamp is
	// stale and the selected descriptor belongs to a different file.
	const MdbDatabase database = MdbParser::load(folder + QStringLiteral("/msmMMOB.mdb"));
	QVERIFY(database.files.contains(kOmfWavFileMob));
	QVERIFY(database.files.value(kOmfWavFileMob).essenceComplete);
	QVERIFY(database.masters.contains(kOmfWavMasterMob));
	QVERIFY(!PmrParser::trailerMatchesModified(kOmfWavModified, QFileInfo(path).lastModified()));
	const OmfMetadata header = OmfParser::parseHeader(path);
	QVERIFY(header.hasMediaDescriptor);
	QVERIFY(!header.essence.valid);
	QVERIFY(!header.essence.classificationKnown);
	QCOMPARE(header.essence.headerStatus, MediaMetadata::HeaderStatus::Incomplete);
	QCOMPARE(header.fileMobId, OmfUid::canonicalHex(TestOmf::uid(replacementUid)));
	QVERIFY(header.fileMobId != kOmfWavFileMob);

	const auto rows = runManualScan(folder, true);
	QCOMPARE(rows.size(), 1);
	const MediaFile &row = rows.first();
	QVERIFY(row.omfEra);
	QCOMPARE(row.mobId, header.fileMobId);
	QVERIFY(row.masterMobId.isEmpty());
	QVERIFY(row.clipName.isEmpty());
	QCOMPARE(row.clipNameSource, MediaFile::ClipNameSource::None);
	QVERIFY(row.project.isEmpty());
	QVERIFY(row.originalBin.isEmpty());
	QVERIFY(row.sourceFilePath.isEmpty());
	QVERIFY(row.sourceFileName.isEmpty());
	QVERIFY(row.sourceContainer.isEmpty());
	QVERIFY(!row.databaseMetadataCurrent);
	QCOMPARE(row.type, MediaFile::Type::Unknown);
	QCOMPARE(row.kind, MediaFile::Kind::Unknown);
	QVERIFY(row.codec.isEmpty());
	QVERIFY(row.bitDepth.isEmpty());
	QCOMPARE(row.sampleRate, 0);
	QCOMPARE(row.channels, 0);
	QCOMPARE(row.durationFrames, qint64(0));
	QCOMPARE(row.timecodeBase, 0);
}

void TestScanner::reserved_folders_are_skipped_under_mxf_and_omfi()
{
	// Reserved staging and quarantine names are not ordinary media leaves.
	// MXF's explicit Quarantined Files diagnostic path has separate coverage.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mxfRoot = Conventions::mxfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(mxfRoot + QStringLiteral("/1")));
	copyFixture(kToneName, mxfRoot + QStringLiteral("/1"));
	for (const QString &name : {QStringLiteral("Creating"), QStringLiteral("Temp"), QStringLiteral("Quarantine")})
	{
		QVERIFY(QDir().mkpath(mxfRoot + QLatin1Char('/') + name));
		copyFixture(kToneName, mxfRoot + QLatin1Char('/') + name);
	}

	const QString omfRoot = Conventions::omfRootUnder(tmp.path());
	QVERIFY(QDir().mkpath(omfRoot));
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot);
	for (const QString &name : {QStringLiteral("Creating"), QStringLiteral("Temp"),
							   QStringLiteral("Quarantine"), QStringLiteral("Quarantined Files")})
	{
		QVERIFY(QDir().mkpath(omfRoot + QLatin1Char('/') + name));
		copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omfRoot + QLatin1Char('/') + name);
		QVERIFY(runManualScan(omfRoot + QLatin1Char('/') + name, true).isEmpty());
	}

	const auto results = runScan(tmp.path(), true);
	QCOMPARE(results.size(), 2);
	QVERIFY(rowNamed(results, kToneName) != nullptr);
	QVERIFY(rowNamed(results, kOmfWav) != nullptr);
	for (const MediaFile &f : results)
		QVERIFY2(!Conventions::isCreatingFolderName(f.mediaFolderName), qPrintable(f.filePath));
}

void TestScanner::current_omf_database_does_not_open_media_data()
{
	QTest::addColumn<bool>("shared");
	QTest::newRow("flat-omfi") << false;
	QTest::newRow("shared-workstation") << true;
}

void TestScanner::current_omf_database_does_not_open_media()
{
	QFETCH(bool, shared);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString root = Conventions::omfRootUnder(tmp.path());
	const QString folder = root + (shared ? QStringLiteral("/EditorOne") : QString{});
	QVERIFY(QDir().mkpath(folder));
	copyFixture(QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), folder);
	// The current database is sufficient; parsing these payloads cannot
	// supply any metadata. The log also asserts no header pass was started.
	writeJunk(folder + QLatin1Char('/') + kOmfWav, 4096);
	writeJunk(folder + QLatin1Char('/') + kOmfAif, 4096);
	setModified(folder + QLatin1Char('/') + kOmfWav, kOmfWavModified);
	setModified(folder + QLatin1Char('/') + kOmfAif, kOmfAifModified);
	MediaScanner scanner;
	QVector<LogMsg> logs;
	connect(&scanner, &MediaScanner::scanLogBatch, this, [&logs](const QVector<LogMsg> &batch) { logs += batch; });
	QSignalSpy finished(&scanner, &MediaScanner::scanFinished);
	MediaScanner::Options options;
	options.manualPaths = {root};
	options.includeOmf = true;
	scanner.startScan(options);
	QVERIFY(finished.wait(10000));
	const auto rows = finished.takeFirst().at(0).value<QVector<MediaFile>>();
	QCOMPARE(rows.size(), 2);
	for (const MediaFile &row : rows)
	{
		QVERIFY(row.databaseMetadataCurrent);
		QVERIFY(!row.needsHeaderRead);
		QCOMPARE(row.clipNameSource, MediaFile::ClipNameSource::Mdb);
		QCOMPARE(row.sampleRate, 48000);
		QCOMPARE(row.channels, 1);
		QVERIFY(row.omfEra);
	}
	QVERIFY(!logs.isEmpty());
	for (const LogMsg &log : logs)
	{
		QVERIFY2(!log.message.contains(QStringLiteral("needing metadata verification")), qPrintable(log.message));
		QVERIFY2(!log.message.contains(QStringLiteral("header bytes")), qPrintable(log.message));
	}
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
		QCOMPARE(rows.first().mediaFolderName, QStringLiteral("1"));
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

void TestScanner::ume_paths_are_ignored_data()
{
	QTest::addColumn<bool>("root");
	QTest::addColumn<bool>("siblingMxf");
	QTest::addColumn<bool>("alias");
	QTest::newRow("root-with-mxf-sibling") << true << true << false;
	QTest::newRow("child-with-mxf-sibling") << false << true << false;
	QTest::newRow("root-without-mxf-sibling") << true << false << false;
	QTest::newRow("database-child-without-mxf-sibling") << false << false << false;
	QTest::newRow("root-alias") << true << true << true;
	QTest::newRow("database-child-alias") << false << false << true;
}

void TestScanner::ume_paths_are_ignored()
{
	QFETCH(bool, root);
	QFETCH(bool, siblingMxf);
	QFETCH(bool, alias);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString ume = tmp.path() + QStringLiteral("/Avid MediaFiles/UME");
	const QString mediaFolder = ume + QStringLiteral("/1");
	QVERIFY(QDir().mkpath(mediaFolder));
	// These readable databases and media would otherwise be admitted by
	// the manual database-folder fallback. UME is excluded by location.
	copyFixture(QStringLiteral("msmFMID.pmr"), mediaFolder);
	copyFixture(QStringLiteral("msmMMOB.mdb"), mediaFolder);
	copyFixture(kToneName, mediaFolder);
	if (siblingMxf)
	{
		const QString mxf = Conventions::mxfRootUnder(tmp.path()) + QStringLiteral("/1");
		QVERIFY(QDir().mkpath(mxf));
		copyFixture(kToneName, mxf);
	}
	QString path = root ? ume : mediaFolder;
	if (alias)
	{
#ifdef Q_OS_UNIX
		const QString link = tmp.path() + QStringLiteral("/Media alias");
		QVERIFY(QFile::link(path, link));
		path = link;
#else
		QSKIP("QFile::link does not create directory symlinks on this platform");
#endif
	}
	// Enabling OMF must not opt UME into the scan or redirect this path
	// to its supported MXF sibling.
	QVERIFY(runManualScan(path).isEmpty());
	QVERIFY(runManualScan(path, true).isEmpty());
}

void TestScanner::ume_exclusion_preserves_supported_siblings()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mxfRoot = Conventions::mxfRootUnder(tmp.path());
	const QString mxf = mxfRoot + QStringLiteral("/1");
	const QString ume = tmp.path() + QStringLiteral("/Avid MediaFiles/UME/1");
	const QString omf = Conventions::omfRootUnder(tmp.path());
	for (const QString &folder : {mxf, ume, omf})
		QVERIFY(QDir().mkpath(folder));
	copyFixture(kToneName, mxf);
	copyFixture(kToneName, ume);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, omf);
#ifdef Q_OS_UNIX
	// The per-folder guard must also reject a UME target reached beneath MXF.
	QVERIFY(QFile::link(ume, mxfRoot + QStringLiteral("/2")));
#endif
	for (bool manual : {false, true})
	{
		MediaScanner::Options options;
		options.includeOmf = true;
		if (manual)
			options.manualPaths = QStringList{tmp.path()};
		else
			options.volumePaths = QStringList{tmp.path()};
		const auto rows = runScanWith(options);
		QCOMPARE(rows.size(), 2);
		const MediaFile *mxfRow = rowNamed(rows, kToneName);
		QVERIFY(mxfRow);
		QCOMPARE(mxfRow->filePath, mxf + QLatin1Char('/') + kToneName);
		QVERIFY(rowNamed(rows, kOmfWav));
	}
	const auto avidRows = runManualScan(tmp.path() + QStringLiteral("/Avid MediaFiles"));
	QCOMPARE(avidRows.size(), 1);
	QCOMPARE(avidRows.first().filePath, mxf + QLatin1Char('/') + kToneName);
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

void TestScanner::cross_format_files_are_excluded_even_with_omf_enabled()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString mxf = Conventions::mxfRootUnder(tmp.path()) + QStringLiteral("/Editor.1");
	const QString omfi = Conventions::omfRootUnder(tmp.path());
	for (const QString &folder : {mxf, omfi})
	{
		QVERIFY(QDir().mkpath(folder));
		copyFixture(kToneName, folder);
		copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);
		copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfAif, folder);
		copyFixture(QStringLiteral("omf/avid_supporting/BLACK_720x243x2_JFIF35.omf"), folder);
	}
	const auto rows = runScan(tmp.path(), true);
	QCOMPARE(rows.size(), 4);
	for (const MediaFile &row : rows)
	{
		if (row.fileName == kToneName)
		{
			QCOMPARE(QFileInfo(row.filePath).absolutePath(), mxf);
			QVERIFY(!row.omfEra);
			QCOMPARE(row.clipName, kToneClip);
		}
		else
		{
			QCOMPARE(QFileInfo(row.filePath).absolutePath(), omfi);
			QVERIFY(row.omfEra);
			QVERIFY(!row.codec.isEmpty());
		}
	}
	const auto publicRows = runScan(tmp.path());
	QCOMPARE(publicRows.size(), 1);
	QCOMPARE(publicRows.first().filePath, mxf + QLatin1Char('/') + kToneName);
}

void TestScanner::unsupported_manual_locations_are_rejected_data()
{
	QTest::addColumn<QString>("relativeFolder");
	QTest::addColumn<bool>("withDatabases");
	QTest::addColumn<bool>("selectParent");
	QTest::newRow("loose-media") << QStringLiteral("Loose media") << false << false;
	QTest::newRow("loose-media-with-databases") << QStringLiteral("Loose media") << true << false;
	QTest::newRow("standalone-mxf-root") << QStringLiteral("MXF/1") << true << true;
	QTest::newRow("standalone-mxf-leaf") << QStringLiteral("MXF/1") << true << false;
	QTest::newRow("partial-avid-path-component") << QStringLiteral("Avid MediaFiles backup/MXF/1") << true << false;
	QTest::newRow("partial-omfi-path-component") << QStringLiteral("OMFI MediaFiles backup") << true << false;
	QTest::newRow("unsupported-mxf-leaf") << QStringLiteral("Avid MediaFiles/MXF/Archive") << true << false;
	QTest::newRow("mxf-leaf-grandchild") << QStringLiteral("Avid MediaFiles/MXF/1/Extra") << true << false;
	QTest::newRow("omfi-workstation-grandchild") << QStringLiteral("OMFI MediaFiles/Editor/Extra") << true << false;
}

void TestScanner::unsupported_manual_locations_are_rejected()
{
	QFETCH(QString, relativeFolder);
	QFETCH(bool, withDatabases);
	QFETCH(bool, selectParent);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + QLatin1Char('/') + relativeFolder;
	QVERIFY(QDir().mkpath(folder));
	if (withDatabases)
	{
		copyFixture(QStringLiteral("msmFMID.pmr"), folder);
		copyFixture(QStringLiteral("msmMMOB.mdb"), folder);
	}
	copyFixture(kToneName, folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);
	copyFixture(QStringLiteral("omf/avid_supporting/BLACK_720x243x2_JFIF35.omf"), folder);
	const QString selected = selectParent ? QFileInfo(folder).absolutePath() : folder;
	QVERIFY(runManualScan(selected).isEmpty());
	QVERIFY(runManualScan(selected, true).isEmpty());
}

void TestScanner::mxf_quarantined_files_remain_diagnostic()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = Conventions::mxfRootUnder(tmp.path()) + QStringLiteral("/Quarantined Files/Old");
	QVERIFY(QDir().mkpath(folder));
	copyFixture(kToneName, folder);
	copyFixture(QStringLiteral("omf/mc2026_audio/") + kOmfWav, folder);
	const auto rows = runScan(tmp.path(), true);
	QCOMPARE(rows.size(), 1);
	QCOMPARE(rows.first().fileName, kToneName);
	QVERIFY(rows.first().isQuarantined);
	QCOMPARE(rows.first().mediaFolderName, QStringLiteral("Quarantined Files"));
	QVERIFY(!rows.first().omfEra);
}

void TestScanner::media_file_symlinks_are_excluded_data()
{
	QTest::addColumn<bool>("omf");
	QTest::addColumn<QString>("targetFolder");
	QTest::newRow("mxf-link-to-ume") << false << QStringLiteral("Avid MediaFiles/UME/1");
	QTest::newRow("mxf-link-to-loose-media") << false << QStringLiteral("Loose media");
	QTest::newRow("omf-wave-link-to-loose-media") << true << QStringLiteral("Loose media");
}

void TestScanner::media_file_symlinks_are_excluded()
{
#ifdef Q_OS_UNIX
	QFETCH(bool, omf);
	QFETCH(QString, targetFolder);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = omf ? Conventions::omfRootUnder(tmp.path())
		: Conventions::mxfRootUnder(tmp.path()) + QStringLiteral("/1");
	const QString target = tmp.path() + QLatin1Char('/') + targetFolder;
	QVERIFY(QDir().mkpath(folder));
	QVERIFY(QDir().mkpath(target));
	const QString prefix = omf ? QStringLiteral("omf/mc2026_audio/") : QString{};
	const QString name = omf ? kOmfWav : kToneName;
	copyFixture(prefix + name, target);
	setModified(target + QLatin1Char('/') + name, omf ? kOmfWavModified : kToneModified);
	copyFixture(prefix + QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(prefix + QStringLiteral("msmMMOB.mdb"), folder);
	const QString linked = folder + QLatin1Char('/') + name;
	QVERIFY(QFile::link(target + QLatin1Char('/') + name, linked));
	QVERIFY(QFileInfo(linked).isSymLink());

	// A current database cannot admit the link, while an ordinary sibling
	// remains scannable. The source bytes belong to the linked target only.
	const QString sibling = omf ? kOmfAif : QStringLiteral("actual.mxf");
	const QString siblingFixture = omf ? QStringLiteral("omf/mc2026_audio/") + kOmfAif : kToneName;
	QVERIFY(QFile::copy(fixturesDir() + QLatin1Char('/') + siblingFixture, folder + QLatin1Char('/') + sibling));
	const auto rows = runManualScan(folder, true);
	QCOMPARE(rows.size(), 1);
	QCOMPARE(rows.first().fileName, sibling);
	QVERIFY(!rows.first().clipName.isEmpty());
#else
	QSKIP("QFile::link creates shortcuts rather than symbolic links on this platform");
#endif
}

void TestScanner::quarantine_alias_cannot_expand_a_normal_media_folder()
{
#ifdef Q_OS_UNIX
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString selectedBase = tmp.path() + QStringLiteral("/Selected");
	const QString root = Conventions::mxfRootUnder(selectedBase);
	const QString folder = Conventions::mxfRootUnder(tmp.path() + QStringLiteral("/Actual")) + QStringLiteral("/1");
	const QString nested = folder + QStringLiteral("/Nested archive");
	QVERIFY(QDir().mkpath(root));
	QVERIFY(QDir().mkpath(nested));
	copyFixture(kToneName, folder);
	copyFixture(kToneName, nested);
	const QString alias = root + QStringLiteral("/Quarantined Files");
	QVERIFY(QFile::link(folder, alias));

	// Its actual target is in another managed tree, ensuring de-duplication
	// cannot accidentally hide the alias's incorrect recursive traversal.
	QVERIFY(runScan(selectedBase, true).isEmpty());
	// Selecting the directory alias may resolve to its legitimate managed
	// target, but its label must never grant recursive quarantine handling.
	const auto aliasRows = runManualScan(alias, true);
	QCOMPARE(aliasRows.size(), 1);
	QCOMPARE(aliasRows.first().filePath, folder + QLatin1Char('/') + kToneName);
	QVERIFY(!aliasRows.first().isQuarantined);
#else
	QSKIP("QFile::link does not create directory symlinks on this platform");
#endif
}

void TestScanner::ordinary_wave_outside_omfi_is_excluded_data()
{
	QTest::addColumn<bool>("managedMxf");
	QTest::addColumn<bool>("matchingDatabase");
	QTest::newRow("mxf-folder-unrelated-databases") << true << false;
	QTest::newRow("renamed-folder-unrelated-databases") << false << false;
	QTest::newRow("renamed-folder-current-omf-databases") << false << true;
}

void TestScanner::ordinary_wave_outside_omfi_is_excluded()
{
	QFETCH(bool, managedMxf);
	QFETCH(bool, matchingDatabase);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString folder = tmp.path() + (managedMxf ? QStringLiteral("/Avid MediaFiles/MXF/1")
												  : QStringLiteral("/Archived session"));
	QVERIFY(QDir().mkpath(folder));
	const QString fixturePrefix = matchingDatabase ? QStringLiteral("omf/mc2026_audio/") : QString{};
	copyFixture(fixturePrefix + QStringLiteral("msmFMID.pmr"), folder);
	copyFixture(fixturePrefix + QStringLiteral("msmMMOB.mdb"), folder);
	// A complete ordinary RIFF WAVE: PCM, 48 kHz, mono, 16-bit, two samples.
	// It has no embedded OMF metadata, so it cannot acquire an Avid layout
	// from its suffix, its neighbours, or a coincidentally matching timestamp.
	const QByteArray ordinaryWave = QByteArray::fromHex(
		"524946462800000057415645666d7420100000000100010080bb00000077010002001000"
		"646174610400000000000000");
	QVERIFY(tryWriteFile(folder + QLatin1Char('/') + kOmfWav, ordinaryWave));
	setModified(folder + QLatin1Char('/') + kOmfWav, kOmfWavModified);
	QVERIFY(runManualScan(folder, true).isEmpty());
}

void TestScanner::unreadable_ama_twin_does_not_mark_the_folder_unreadable()
{
	// Folder 1: a readable msm* pair plus junk under both ama* names. The
	// msm* index stands, so the tone stays database-covered and an unreadable
	// stray MXF is a verified miss, as it was before the twins were read.
	// Folder 2: junk ama* files ALONE; with nothing else of
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
		writeJunk(folder + QStringLiteral("/stray.mxf"), 16);
	}

	const auto results = runScan(tmp.path(), true);
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
		else if (mf.mediaFolderName == QStringLiteral("1"))
			QCOMPARE(mf.dbStatus, MediaFile::DbStatus::NoReference);
		else
			QCOMPARE(mf.dbStatus, MediaFile::DbStatus::DbUnreadable);
	}
}

QTEST_GUILESS_MAIN(TestScanner)
#include "tst_scanner.moc"
