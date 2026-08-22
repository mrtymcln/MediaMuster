// Drives MediaScanner against a fake Avid layout built from real
// PMR/MDB/MXF fixtures. Covers Stage 1 + 2 + the join.

#include "mediafile.h"
#include "debugslowdown.h"
#include "mediascanner.h"
#include "testbento.h"
#include "pmrparser.h"

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

	QVERIFY(!mf.isNoReference);
	QVERIFY(!mf.isNoDatabase());
	QVERIFY(!mf.isNoProject);
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

	// Recovery discriminators. NB: clipName is NOT one — Stage 2 reads it from
	// the MXF MaterialPackage, so it is set with or without the MDB (the no-mdb
	// partner test confirms that). The genuine signals are the flags, the
	// sentinel, and the adopted MOB ID, which only the MDB join can supply.
	QVERIFY(!mf.isInvalidUmid);		// a good UMID is what the lookup keys on
	QVERIFY(!mf.isNoReference); // Stage 3 flipped it back
	QVERIFY(!mf.isNoDatabase());
	QVERIFY(mf.isNoProject); // found in MDB, but no project association
	QCOMPARE(mf.project, QStringLiteral("No project"));
	const QString adoptedMob =
		QStringLiteral("060a2b3401010105.01010f1013000000.d2467dea74110690.91901e6a605d3613");
	QCOMPARE(mf.mobId, adoptedMob); // canonical MOB ID adopted from the MDB record
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

	// Stage 3 did run — the MDB's identity and flags were adopted...
	QVERIFY(mf.isNoProject);
	QCOMPARE(mf.project, QStringLiteral("No project"));
	QCOMPARE(mf.mobId, QStringLiteral("060a2b3401010105.01010f1013000000."
									  "d2467dea74110690.91901e6a605d3613"));

	// ...but the clip name stayed with the media file's own MaterialPackage
	// name. Before the fix this read "Wrong Neighbour Clip". Still true under
	// the 2026-08-14 ladder for a different reason: the MDB name is applied
	// now, but it ranks below a MaterialPackage name and this file has one.
	QCOMPARE(mf.clipName, QStringLiteral("TONE: 1000 Hz @ -14.0 dB.1"));
	QCOMPARE(mf.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
}

// Partner to unreferenced_mxf_recovered_via_mdb, with NO databases in the
// folder at all. An absent database can't verify a miss, so the app must not
// claim "No reference" — the honest state is "No database" (never indexed),
// and Stage 3 has nothing to recover against.
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

	QVERIFY(!mf.isNoReference); // absent databases can't verify a miss
	QCOMPARE(int(mf.dbIssue), int(MediaFile::DbIssue::NeverIndexed));
	QCOMPARE(mf.project, QStringLiteral("No database"));
	QVERIFY(!mf.isNoProject);	 // recovery never ran
	QVERIFY(mf.mobId.isEmpty()); // no MOB adopted — the discriminator vs the recovered case
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

	QVERIFY(mf.isNoReference); // both databases readable, file in neither: verified
	QVERIFY(!mf.isNoDatabase());
	QCOMPARE(mf.project, QStringLiteral("No reference"));
	QVERIFY(!mf.isNoProject);
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

	// The MXF's UMID is in the readable MDB: recovered despite the dead PMR.
	const MediaFile *mxf = findByName(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"));
	QVERIFY(mxf != nullptr);
	QVERIFY(mxf->isNoProject);
	QVERIFY(!mxf->isNoDatabase());
	QCOMPARE(mxf->project, QStringLiteral("No project"));

	// The wav has no UMID; nothing can vouch for it while the PMR is dead, so
	// it must NOT be claimed as a verified miss.
	const MediaFile *wavFile = findByName(QStringLiteral("tone.wav"));
	QVERIFY(wavFile != nullptr);
	QVERIFY(!wavFile->isNoReference);
	QCOMPARE(int(wavFile->dbIssue), int(MediaFile::DbIssue::Unreadable));
	QCOMPARE(wavFile->project, QStringLiteral("No database"));
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

	bool writeFile(const QString &path, const QByteArray &bytes)
	{
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly))
			return false;
		const bool ok = f.write(bytes) == bytes.size();
		f.close();
		return ok;
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

	QVERIFY(writeFile(folder + QStringLiteral("/msmFMID.pmr"), ladderPmr("garbage.mxf")));
	QVERIFY(writeFile(folder + QStringLiteral("/msmMMOB.mdb"), ladderMdb("Interview Take 3")));

	// The media file itself is unparseable, so no MaterialPackage name will
	// ever arrive — the case that used to fall back to the filename.
	QVERIFY(writeFile(folder + QStringLiteral("/garbage.mxf"), QByteArray(2048, '\x11')));

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
	QVERIFY(writeFile(folder + QStringLiteral("/msmFMID.pmr"), ladderPmr("garbage.mxf")));
	QByteArray otherMdb = ladderMdb("Some Other Clip");
	otherMdb.replace(kLadderMob, QByteArray::fromHex("060a2b340101010501010f1013000000"
													 "aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbb"));
	QVERIFY(writeFile(folder + QStringLiteral("/msmMMOB.mdb"), otherMdb));
	QVERIFY(writeFile(folder + QStringLiteral("/garbage.mxf"), QByteArray(2048, '\x11')));

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
	QVERIFY(writeFile(folder + QStringLiteral("/tape.mxf"), mxf));

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
	QVERIFY(writeFile(folder + QStringLiteral("/Untitled_Sequence.0B4A3F9FV.mxf"),
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
	// enumerated and only the scanner's AvidLayout::isDotHidden skip keeps
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
	// cache and THEN hits DebugSlowdown's 4-second pause, so cancelling
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
		DebugSlowdown::setEnabled(true);
		QSignalSpy spy(&scanner, &MediaScanner::scanFinished);
		scanner.startScan(opts);
		QTest::qWait(500);
		scanner.cancelScan();
		QVERIFY2(spy.wait(20000), "cancelled scan did not finish");
		DebugSlowdown::setEnabled(false);
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
		// Without the clear, Stage 3 rejoins this file against the
		// cancelled scan's cached MDB and reports "No project".
		QVERIFY2(results.first().isNoDatabase(),
				 qPrintable(QStringLiteral("stale cache leaked; project = ") +
							results.first().project));
		QCOMPARE(results.first().project, QStringLiteral("No database"));
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
	QVERIFY(!mf.isNoReference && !mf.isNoDatabase() && !mf.isNoProject);
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

QTEST_GUILESS_MAIN(TestScanner)
#include "tst_scanner.moc"