// MdbParser: msmMMOB.mdb read through its Bento table of contents. The
// assertions that matter are fixture-driven — the corpus databases joined to
// the real MXF headers captured from the same folders, with the header as
// ground truth for every technical field — plus a few builder-made
// containers for the merge and gate rules.

#include "bentofile.h"
#include "mdbparser.h"
#include "mobid.h"
#include "mxfparser.h"
#include "pmrparser.h"
#include "testbento.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	QString writeMdb(const QString &path, const QByteArray &bytes)
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile f(path);
		if (f.open(QIODevice::WriteOnly))
		{
			f.write(bytes);
			f.close();
		}
		return path;
	}

	QString fx(const char *rel)
	{
		return QStringLiteral(FIXTURES_DIR "/") + QLatin1String(rel);
	}

	const QByteArray kToneFileMob = QByteArray::fromHex("060a2b340101010501010f1013000000"
														 "4a507dea741106907a361e6a605d3613");
	const QByteArray kToneMasterMob = QByteArray::fromHex("060a2b340101010501010f1013000000"
														   "d2467dea7411069091901e6a605d3613");
} // namespace

class TestMdbParser : public QObject
{
	Q_OBJECT
private slots:
	// Gate
	void missing_or_garbage_file_reports_not_ok();
	void mob_signature_without_a_bento_label_is_not_an_mdb();
	void bento_without_mobs_is_ok_and_empty();
	void real_fixture_mdbs_load();

	// Shape and merge rules
	void tiny_fixture_covers_the_tone_file();
	void duplicate_mobj_objects_are_merged_first_non_empty_wins();
	void source_mob_is_neither_file_nor_master();

	// Strings
	void real_accented_bin_mdb_never_yields_mojibake();
	void macroman_only_precompute_names_decode();

	// The joins: PMR pairs → MDB records → MXF truth
	void mdb_join_resolves_real_mxf_files();
	void every_pmr_pair_is_described_and_essence_matches_the_header();
	void mpga_audio_is_not_essence_complete();
};

// MARK: - Gate

void TestMdbParser::missing_or_garbage_file_reports_not_ok()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	bool ok = true;
	QVERIFY(MdbParser::load(tmp.path() + QStringLiteral("/nope.mdb"), &ok).isEmpty());
	QVERIFY(!ok);

	const struct
	{
		const char *label;
		QByteArray bytes;
	} kGarbage[] = {
		{"tiny", QByteArray("xx")},
		{"all_zeros", QByteArray(4096, '\0')},
		{"repeating_junk", QByteArray(4096, '\xAB')},
		{"fake_jpeg", QByteArray::fromHex("ffd8ffe000104a464946") + QByteArray(4096, '\x5A')},
	};
	for (const auto &g : kGarbage)
	{
		ok = true;
		const MdbDatabase db =
			MdbParser::load(writeMdb(tmp.path() + QStringLiteral("/%1.mdb").arg(QLatin1String(g.label)), g.bytes), &ok);
		QVERIFY2(!ok, g.label);
		QVERIFY2(db.isEmpty(), g.label);
	}
}

void TestMdbParser::mob_signature_without_a_bento_label_is_not_an_mdb()
{
	// The old parser's gate let any buffer with an Avid MOB prefix through.
	// The TOC reader needs the container: no label, no database.
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QByteArray buf("Gate Clip");
	buf.append('\0');
	buf.append(kToneMasterMob);
	buf.append(QByteArray(64, '\0'));
	bool ok = true;
	QVERIFY(MdbParser::load(writeMdb(tmp.path() + "/msmMMOB.mdb", buf), &ok).isEmpty());
	QVERIFY(!ok);
}

void TestMdbParser::bento_without_mobs_is_ok_and_empty()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	BentoBuilder w;
	const quint32 o = w.addObject("HEAD");
	w.setString(o, "OMFI:HEAD:Version", "2.1");
	bool ok = false;
	const MdbDatabase db = MdbParser::load(writeMdb(tmp.path() + "/msmMMOB.mdb", w.build()), &ok);
	QVERIFY(ok);
	QVERIFY(db.isEmpty());
}

void TestMdbParser::real_fixture_mdbs_load()
{
	const char *kFixtures[] = {"msmMMOB.mdb", "msmMMOB_macroman.mdb", "corpus_headers/msmMMOB.mdb",
							   "corpus_headers/msmMMOB_round3.mdb"};
	for (const char *rel : kFixtures)
	{
		bool ok = false;
		const MdbDatabase db = MdbParser::load(fx(rel), &ok);
		QVERIFY2(ok, rel);
		QVERIFY2(!db.masters.isEmpty(), rel);
		QVERIFY2(!db.files.isEmpty(), rel);
	}
}

// MARK: - Shape and merge rules

// The 53 KB fixture describes the TONE clip tst_scanner drives: one file mob
// with a PCM descriptor, and a master mob whose name sits on one MOBJ object
// and whose bin sits on a SECOND object carrying the same MobID.
void TestMdbParser::tiny_fixture_covers_the_tone_file()
{
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("msmMMOB.mdb"), &ok);
	QVERIFY(ok);

	const QString fileHex = MobId::format(kToneFileMob);
	const QString masterHex = MobId::format(kToneMasterMob);
	QVERIFY(db.files.contains(fileHex));
	QVERIFY(db.masters.contains(masterHex));

	const MdbFile &f = db.files[fileHex];
	QCOMPARE(f.usageCode, 0);
	QVERIFY(f.essenceComplete);
	QVERIFY(f.essence.isAudio);
	QVERIFY(f.essence.valid);
	QCOMPARE(f.essence.codec, QString::fromLatin1(kPcmAudioName));

	// The technical fields equal what the media file's own header says.
	const MxfMetadata hdr = MxfParser::parseHeader(fx("TONE_100A01.EA7D504A.611740.mxf"));
	QVERIFY(hdr.valid);
	QCOMPARE(f.essence.sampleRate, hdr.sampleRate);
	QCOMPARE(f.essence.channels, hdr.channels);
	QCOMPARE(f.essence.bitDepth, hdr.bitDepth);
	QCOMPARE(f.essence.durationFrames, hdr.durationFrames);
	QCOMPARE(f.essence.timecodeBase, hdr.timecodeBase);
	QCOMPARE(f.essence.codec, hdr.codec);

	const MdbMaster &m = db.masters[masterHex];
	QCOMPARE(m.clipName, QStringLiteral("TONE: 1000 Hz @ -14.0 dB.1"));
	QCOMPARE(m.clipName, hdr.clipName);
	QCOMPARE(m.usageCode, 7);
	// Multi-script bin name from the UTF-8 twin. Raw bytes so the source
	// file's encoding can't drift the assertion.
	const QString expectedBin = QString::fromUtf8("No\xCC\x88n English bin na\xCC\x81me\xE2\x84\xA2"
												  " \xE4\xBD\xA0\xE5\xA5\xBD \xE6\xBC\xA2");
	QCOMPARE(m.bin, expectedBin);
}

void TestMdbParser::duplicate_mobj_objects_are_merged_first_non_empty_wins()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	BentoBuilder w;
	// First object: name + usage. Second object, same MobID: a different
	// name (must lose) and the bin (must win — nobody else has it).
	const quint32 a = w.addObject("MOBJ");
	w.set(a, "OMFI:MOBJ:MobID", kToneMasterMob);
	w.setString(a, "OMFI:CPNT:Name", "First Name");
	w.setU32(a, "OMFI:MOBJ:UsageCode", 7);

	const quint32 b = w.addObject("MOBJ");
	w.set(b, "OMFI:MOBJ:MobID", kToneMasterMob);
	w.setString(b, "OMFI:CPNT:Name", "Second Name");
	const quint32 attr = w.addObject("ATTR");
	const quint32 attb = w.addObject("ATTB");
	const quint32 mcbr = w.addObject("MCBR");
	w.setHandle(b, "OMFI:CPNT:Attributes", attr);
	w.setHandles(attr, "OMFI:ATTR:AttrRefs", {attb});
	w.setString(attb, "OMFI:ATTB:Name", "_ORG_BIN");
	w.setU16(attb, "OMFI:ATTB:Kind", 3);
	w.setHandle(attb, "OMFI:ATTB:ObjAttribute", mcbr);
	w.setString(mcbr, "OMFI:MCBR:MC:binName", QByteArray("Caf\x8e bin", 9)); // MacRoman é
	w.setString(mcbr, "OMFI:MCBR:MC:binNameUTF8", QByteArray("Caf\xc3\xa9 bin", 10));

	bool ok = false;
	const MdbDatabase db = MdbParser::load(writeMdb(tmp.path() + "/msmMMOB.mdb", w.build()), &ok);
	QVERIFY(ok);
	QCOMPARE(db.masters.size(), 1);
	QVERIFY(db.files.isEmpty());
	const MdbMaster &m = db.masters[MobId::format(kToneMasterMob)];
	QCOMPARE(m.clipName, QStringLiteral("First Name"));
	QCOMPARE(m.usageCode, 7);
	QCOMPARE(m.bin, QString::fromUtf8("Café bin"));
}

void TestMdbParser::source_mob_is_neither_file_nor_master()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	BentoBuilder w;
	// A source mob: PhysicalMedia → MDES (the import/tape source). It must not
	// land in `files` (no media descriptor) nor in `masters` (it has media).
	const quint32 src = w.addObject("MOBJ");
	const quint32 mdes = w.addObject("MDES");
	w.set(src, "OMFI:MOBJ:MobID", kToneFileMob);
	w.setHandle(src, "OMFI:MOBJ:PhysicalMedia", mdes);
	// A real file mob beside it, so the database isn't empty.
	const quint32 file = w.addObject("MOBJ");
	const quint32 pcma = w.addObject("PCMA");
	w.set(file, "OMFI:MOBJ:MobID", kToneMasterMob);
	w.setHandle(file, "OMFI:MOBJ:PhysicalMedia", pcma);
	w.setRational(pcma, "OMFI:MDFL:SampleRate", 48000, 1);
	w.setU32(pcma, "OMFI:MDFL:Length", 48000);
	w.setRational(file, "OMFI:CPNT:EditRate", 25, 1);
	w.setU16(pcma, "OMFI:MDAU:BitsPerSample", 24);
	w.setU16(pcma, "OMFI:MDAU:NumChannels", 2);

	bool ok = false;
	const MdbDatabase db = MdbParser::load(writeMdb(tmp.path() + "/msmMMOB.mdb", w.build()), &ok);
	QVERIFY(ok);
	QVERIFY(db.masters.isEmpty());
	QCOMPARE(db.files.size(), 1);
	const MdbFile &f = db.files[MobId::format(kToneMasterMob)];
	QVERIFY(f.essenceComplete);
	QVERIFY(f.essence.isAudio);
	QCOMPARE(f.essence.sampleRate, 48000);
	QCOMPARE(f.essence.channels, 2);
	QCOMPARE(f.essence.bitDepth, QStringLiteral("24-bit"));
	QCOMPARE(f.essence.durationFrames, qint64(25)); // 48000 samples × 25/48000
	QCOMPARE(f.essence.timecodeBase, 25);
}

// MARK: - Strings

void TestMdbParser::real_accented_bin_mdb_never_yields_mojibake()
{
	// Real MC 2025 MDB whose bin is named "Café tëst" (fixture captured
	// 2026-07-20). The clip imported into that bin must resolve the UTF-8
	// form, and no record anywhere in the file may carry a replacement
	// character in any field.
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("msmMMOB_macroman.mdb"), &ok);
	QVERIFY(ok);
	QVERIFY(!db.masters.isEmpty());

	const QString expectedBin = QString::fromUtf8("Caf\xc3\xa9 t\xc3\xabst");
	bool sawAccentedBin = false;
	const QChar fffd(QChar::ReplacementCharacter);
	for (const MdbMaster &m : db.masters)
	{
		if (m.bin == expectedBin)
			sawAccentedBin = true;
		QVERIFY2(!m.bin.contains(fffd), qPrintable("mojibake bin: " + m.bin));
		QVERIFY2(!m.clipName.contains(fffd), qPrintable("mojibake clip: " + m.clipName));
		QVERIFY2(!m.sourceFilePath.contains(fffd), qPrintable("mojibake path: " + m.sourceFilePath));
	}
	QVERIFY2(sawAccentedBin, "no record resolved the UTF-8 'Café tëst' bin name");
}

// Precompute records store their name ONCE, in MacRoman (media records store
// it twice, MacRoman then UTF-8). The old parser rejected the MacRoman copy
// and six renders came out blank; AvidText::decode turns A7 into ß.
void TestMdbParser::macroman_only_precompute_names_decode()
{
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("corpus_headers/msmMMOB.mdb"), &ok);
	QVERIFY(ok);
	const QVector<PmrEntry> pmr = PmrParser::parse(fx("corpus_headers/msmFMID.pmr"));
	QCOMPARE(pmr.size(), 435);

	int renders = 0, withEszett = 0;
	QSet<QString> names;
	for (const PmrEntry &e : pmr)
	{
		QVERIFY2(db.files.contains(e.mobId), qPrintable(e.fileName));
		QVERIFY2(db.masters.contains(e.masterMobId), qPrintable(e.fileName));
		const MdbFile &f = db.files[e.mobId];
		const MdbMaster &m = db.masters[e.masterMobId];
		if (f.usageCode == 9)
		{
			QCOMPARE(m.usageCode, 1);
			++renders;
			QVERIFY2(!m.clipName.isEmpty(), qPrintable(e.fileName));
			QVERIFY2(!m.clipName.contains(QChar(QChar::ReplacementCharacter)), qPrintable(m.clipName));
			if (m.clipName.contains(QChar(0xDF)))
				++withEszett;
			names.insert(m.clipName);
		}
		else
		{
			QCOMPARE(f.usageCode, 0);
			QCOMPARE(m.usageCode, 7);
		}
	}
	QCOMPARE(renders, 15);
	QVERIFY2(withEszett >= 6, qPrintable(QString::number(withEszett)));
	QVERIFY(names.contains(QString::fromUtf8("zT_\xc3\x9ft_1080i_50_seq,1.85_Mask+2")));
}

// MARK: - Joins

void TestMdbParser::mdb_join_resolves_real_mxf_files()
{
	// corpus_headers/*.mxf and corpus_headers/msmMMOB_round3.mdb come from the
	// same Avid folder: all 795 headers resolve into that database. They only
	// look unrelated if you compare MOB IDs raw — an MXF stores a MobID's
	// middle fields little-endian and the MDB big-endian, so the join has to
	// go through MobId::toPmrForm first (0/795 match without it, 795/795 with).
	// That conversion is the thing this test exists to pin.
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("corpus_headers/msmMMOB_round3.mdb"), &ok);
	QVERIFY(ok);
	QVERIFY(!db.masters.isEmpty());

	// One clip's video and audio relatives, twice over. All four were imported
	// into the same bin, so all four records must agree on it.
	const char *kFiles[] = {
		"V01.E6966CE5_A3C580A3C588BV.mxf",
		"A01.E6966CE6_A3C580A3C589AA.mxf",
		"V01.E6967022_CB0BE0CB0BE6CV.mxf",
		"A01.E6967023_CB0BE0CB0BE82A.mxf",
	};
	// No independent source exists for the bin — the MXF doesn't carry one —
	// so this value is a characterisation of the database, pinned to catch a
	// regression rather than to prove the read. The clip name and source path
	// below are the assertions that actually verify attribution.
	const QString expectedBin = QString::fromUtf8("zTe\xc3\x9ft_1080i_59.94 Bin1");

	for (const char *file : kFiles)
	{
		const QString path = fx("corpus_headers/") + QLatin1String(file);
		QVERIFY2(QFile::exists(path), qPrintable(path));

		const MxfMetadata meta = MxfParser::parseHeader(path);
		QVERIFY2(meta.valid, file);
		QVERIFY2(meta.clipNameFromMaterial, file);

		// An MXF writes a MobID's middle fields little-endian and the MDB
		// big-endian; without this conversion every lookup misses.
		QVERIFY2(!db.masters.contains(meta.umid), "raw MXF UMID must not key the MDB");
		const QString key = MobId::toPmrForm(meta.umid);
		QVERIFY2(db.masters.contains(key), qPrintable(QLatin1String(file) + QStringLiteral(" -> ") + key));

		const MdbMaster &rec = db.masters[key];

		// Ground truth: the name the file carries in its own MaterialPackage.
		QCOMPARE(rec.clipName, meta.clipName);
		// And the record's source path must be the file this clip was imported
		// from — which ties the record to the right clip a second way, since
		// Avid named the import after the clip. The header's own TaggedValues
		// must agree on path and container.
		QCOMPARE(rec.sourceFileName, meta.clipName + QStringLiteral(".mov"));
		QCOMPARE(rec.sourceFilePath, meta.sourceFilePath);
		QCOMPARE(rec.sourceContainer, QStringLiteral("QTFF"));
		QCOMPARE(rec.sourceContainer, meta.sourceContainer);
		QCOMPARE(rec.bin, expectedBin);
		QVERIFY2(rec.isImported, file);
		QVERIFY2(meta.hasImportSetting, file);

		// The bin is named after the format its clips are in, so the raster
		// the MXF reports has to be the one the bin claims.
		if (!meta.resolution.isEmpty())
		{
			QCOMPARE(meta.resolution, QStringLiteral("1920x1080"));
			QVERIFY2(rec.bin.contains(QStringLiteral("1080")), qPrintable(rec.bin));
		}
	}
}

// The whole point of the database path: for every file the PMR names, the
// MDB describes the file mob AND the master mob, and every technical field
// the database yields equals the one the file's own header yields. Both
// database generations, 795 files. Codec label byte-identical (which covers
// the AUID reorder and the DIDResolutionID fallback), resolution (which
// covers the layout-3 half-height rule), fps label, durations, bits,
// channels, sample rate, kind, clip name.
void TestMdbParser::every_pmr_pair_is_described_and_essence_matches_the_header()
{
	struct Gen
	{
		const char *pmr;
		const char *mdb;
		int pairs;
	};
	const Gen gens[] = {
		{"corpus_headers/msmFMID.pmr", "corpus_headers/msmMMOB.mdb", 435},
		{"corpus_headers/msmFMID_round3.pmr", "corpus_headers/msmMMOB_round3.mdb", 360},
	};
	int compared = 0, incomplete = 0;
	for (const Gen &g : gens)
	{
		bool ok = false;
		const MdbDatabase db = MdbParser::load(fx(g.mdb), &ok);
		QVERIFY2(ok, g.mdb);
		const QVector<PmrEntry> pmr = PmrParser::parse(fx(g.pmr), &ok);
		QVERIFY2(ok, g.pmr);
		QCOMPARE(pmr.size(), g.pairs);

		for (const PmrEntry &e : pmr)
		{
			const QString path = fx("corpus_headers/") + e.fileName;
			QVERIFY2(QFile::exists(path), qPrintable(path));
			QVERIFY2(db.files.contains(e.mobId), qPrintable(e.fileName));
			QVERIFY2(db.masters.contains(e.masterMobId), qPrintable(e.fileName));
			const MdbFile &f = db.files[e.mobId];
			const MdbMaster &m = db.masters[e.masterMobId];

			const MxfMetadata hdr = MxfParser::parseHeader(path);
			QVERIFY2(hdr.valid, qPrintable(e.fileName));
			QCOMPARE(m.clipName, hdr.clipName);
			QCOMPARE(f.essence.isAudio, hdr.isAudio);
			QCOMPARE(m.usageCode == 1, hdr.isPrecompute); // the master mob's code is the verdict

			if (!f.essenceComplete)
			{
				++incomplete;
				continue; // the MPGA case — asserted separately
			}
			const MxfMetadata &db_ = f.essence;
			const char *fn = e.fileName.toUtf8().constData();
			QVERIFY2(db_.valid, fn);
			QVERIFY2(db_.essenceContainerLabel == hdr.essenceContainerLabel,
					 qPrintable(e.fileName + QStringLiteral(": label ") + db_.essenceContainerLabel.toHex() +
								QStringLiteral(" vs ") + hdr.essenceContainerLabel.toHex()));
			QVERIFY2(db_.codec == hdr.codec,
					 qPrintable(e.fileName + QStringLiteral(": codec ") + db_.codec + QStringLiteral(" vs ") + hdr.codec));
			QVERIFY2(db_.resolution == hdr.resolution,
					 qPrintable(e.fileName + QStringLiteral(": res ") + db_.resolution + QStringLiteral(" vs ") +
								hdr.resolution));
			QVERIFY2(db_.fps == hdr.fps,
					 qPrintable(e.fileName + QStringLiteral(": fps ") + db_.fps + QStringLiteral(" vs ") + hdr.fps));
			QVERIFY2(db_.bitDepth == hdr.bitDepth,
					 qPrintable(e.fileName + QStringLiteral(": bits ") + db_.bitDepth + QStringLiteral(" vs ") +
								hdr.bitDepth));
			QVERIFY2(db_.sampleRate == hdr.sampleRate, fn);
			QVERIFY2(db_.channels == hdr.channels, fn);
			QVERIFY2(db_.durationFrames == hdr.durationFrames,
					 qPrintable(e.fileName + QStringLiteral(": dur ") + QString::number(db_.durationFrames) +
								QStringLiteral(" vs ") + QString::number(hdr.durationFrames)));
			QVERIFY2(db_.timecodeBase == hdr.timecodeBase, fn);
			QVERIFY2(db_.dropFrame == hdr.dropFrame, fn);
			++compared;
		}
	}
	QCOMPARE(compared + incomplete, 795);
	QCOMPARE(incomplete, 1);
}

// MPEG audio carries no codec label in the MDB (only MPGA:BitRate etc.), so
// the database cannot name its codec and the scanner must read the header.
void TestMdbParser::mpga_audio_is_not_essence_complete()
{
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("corpus_headers/msmMMOB.mdb"), &ok);
	QVERIFY(ok);
	const QVector<PmrEntry> pmr = PmrParser::parse(fx("corpus_headers/msmFMID.pmr"));
	QString mob;
	for (const PmrEntry &e : pmr)
		if (e.fileName == QLatin1String("A01.E68C35B3_2C34B2C34B61AA.mxf"))
			mob = e.mobId;
	QVERIFY(!mob.isEmpty());
	QVERIFY(db.files.contains(mob));
	const MdbFile &f = db.files[mob];
	QVERIFY(f.essence.isAudio);
	QVERIFY(!f.essenceComplete);

	const MxfMetadata hdr = MxfParser::parseHeader(fx("corpus_headers/A01.E68C35B3_2C34B2C34B61AA.mxf"));
	QVERIFY(hdr.valid);
	QVERIFY2(hdr.codec.contains(QStringLiteral("MP2")), qPrintable(hdr.codec));
}

QTEST_APPLESS_MAIN(TestMdbParser)
#include "tst_mdbparser.moc"
