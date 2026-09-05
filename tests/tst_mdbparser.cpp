// MdbParser: msmMMOB.mdb read through its Bento table of contents. The
// assertions that matter are fixture-driven — the corpus databases joined to
// the real MXF headers captured from the same folders, with the header as
// ground truth for every technical field — plus a few builder-made
// containers for the merge and gate rules.

#include "bentofile.h"
#include "mdbparser.h"
#include "mobid.h"
#include "mxfparser.h"
#include "omfobjects.h"
#include "omfresolutions.h"
#include "omfuid.h"
#include "pmrparser.h"
#include "testbento.h"
#include "testomf.h"

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

	// OMF-era: a 12-byte omfi:UID as MC writes it — "2a000000" then the 8
	// identity bytes the v2 PMR stores.
	QByteArray omfUid(const char *core8Hex)
	{
		return QByteArray::fromHex("2a000000") + QByteArray::fromHex(core8Hex);
	}

	QByteArray le32(quint32 v)
	{
		return BentoBuilder::le32(v);
	}

	QByteArray be32(quint32 v)
	{
		QByteArray b(4, '\0');
		qToBigEndian<quint32>(v, b.data());
		return b;
	}

	QByteArray be16(quint16 v)
	{
		QByteArray b(2, '\0');
		qToBigEndian<quint16>(v, b.data());
		return b;
	}

	/// The PMR entries of an OMF-era folder keyed by file name.
	QHash<QString, PmrEntry> pmrByName(const QString &pmrPath, int expectedPairs)
	{
		bool ok = false;
		const QVector<PmrEntry> entries = PmrParser::parse(pmrPath, &ok);
		QHash<QString, PmrEntry> out;
		if (!ok || entries.size() != expectedPairs)
			return out;
		for (const PmrEntry &e : entries)
			out.insert(e.fileName, e);
		return out;
	}

	/// The first MOBJ object carrying `mobIdHex`, plus the raw-keyed map the
	/// walker hops through — what MdbParser::load builds privately.
	quint32 findMob(const BentoFile &b, const OmfObjects::Props &p, const QString &mobIdHex,
					OmfObjects::ObjectByMob &objectByMob)
	{
		quint32 found = 0;
		for (quint32 obj : b.objectsWithProperty(p.mobId))
		{
			if (b.objectClass(obj) != "MOBJ")
				continue;
			const QByteArray raw = b.bytes(obj, p.mobId);
			if (!objectByMob.contains(raw))
				objectByMob.insert(raw, obj);
			if (found == 0 && OmfUid::canonicalHex(raw) == mobIdHex)
				found = obj;
		}
		return found;
	}
} // namespace

class TestMdbParser : public QObject
{
	Q_OBJECT
private slots:
	void uncompressed_alpha_requires_explicit_none_and_component_arrays();
	void omf2_roles_and_file_master_ancestry();
	void descriptor_failure_never_creates_a_master();
	void external_toolkit_semantic_regression();
	void omf2_video_uses_full_mixed_field_height_and_64_bit_length();
	void tiff_summary_respects_own_byte_order_and_avid_short_values();
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

	// OMF-era
	void omf_resolution_table_keys_on_id_and_fourcc();
	void omf_resolution_id_1244_names_avid_dnx_tr();
	void omf_wave_summary_is_chunk_walked();
	void omf_aifc_summary_is_chunk_walked();
	void omf_era_mdb_describes_every_pmr_pair_with_wrapped_ids();
	void omf_era_mdb_video_facts_by_resolution_id();
	void omf_era_audio_mdb_describes_both_tone_files();
	void omf_timecode_is_reached_through_either_mob_width();
	void omf_sd2d_descriptor_reads_its_two_properties();
	void omf_winl_and_unxl_locators_yield_the_source_path();
	void mxf_era_master_keeps_the_macl_only_srcfile_rule();
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
		QCOMPARE(db.revision, OmfObjects::Revision::Omf1);
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

	const MdbFileMob &f = db.files[fileHex];
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

	const MdbMasterMob &m = db.masters[masterHex];
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
	const MdbMasterMob &m = db.masters[MobId::format(kToneMasterMob)];
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
	const MdbFileMob &f = db.files[MobId::format(kToneMasterMob)];
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
	for (const MdbMasterMob &m : db.masters)
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
		const MdbFileMob &f = db.files[e.mobId];
		const MdbMasterMob &m = db.masters[e.masterMobId];
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

		const MdbMasterMob &rec = db.masters[key];

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
			const MdbFileMob &f = db.files[e.mobId];
			const MdbMasterMob &m = db.masters[e.masterMobId];

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
			const QByteArray fnBytes = e.fileName.toUtf8();
			const char *fn = fnBytes.constData();
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
	const MdbFileMob &f = db.files[mob];
	QVERIFY(f.essence.isAudio);
	QVERIFY(!f.essenceComplete);

	const MxfMetadata hdr = MxfParser::parseHeader(fx("corpus_headers/A01.E68C35B3_2C34B2C34B61AA.mxf"));
	QVERIFY(hdr.valid);
	QVERIFY2(hdr.codec.contains(QStringLiteral("MP2")), qPrintable(hdr.codec));
}

// MARK: - OMF-era

void TestMdbParser::omf_resolution_table_keys_on_id_and_fourcc()
{
	// The property is NUL-terminated in the file; both spellings match.
	QCOMPARE(OmfResolutions::name(82, QByteArray("JFIF\0", 5)), QStringLiteral("20:1"));
	QCOMPARE(OmfResolutions::name(82, QByteArray("JFIF")), QStringLiteral("20:1"));
	QCOMPARE(OmfResolutions::name(78, QByteArray("JFIF")), QStringLiteral("15:1s"));
	QCOMPARE(OmfResolutions::name(141, QByteArray("DV/C")), QStringLiteral("DV 25 420"));
	QCOMPARE(OmfResolutions::name(160, QByteArray("MPG2")), QStringLiteral("MPEG 50"));
	// The 4CC is part of the key: a reused id under another family names nothing.
	QVERIFY(OmfResolutions::name(82, QByteArray("DV/C")).isEmpty());
	QVERIFY(OmfResolutions::name(82, QByteArray("JFI")).isEmpty());
	QVERIFY(OmfResolutions::name(82, QByteArray("JFIFX")).isEmpty());
	// MC 26.8's regenerated database spells uncompressed as MXF1; the file says AUNC.
	QCOMPARE(OmfResolutions::name(151, QByteArray("AUNC")), QStringLiteral("1:1"));
	QCOMPARE(OmfResolutions::name(151, QByteArray("MXF1")), QStringLiteral("1:1"));
	QCOMPARE(OmfResolutions::name(2500, QByteArray("DV/C")), QStringLiteral("DV 100 1080i"));
	QCOMPARE(OmfResolutions::name(2500, QByteArray("MXF1")), QStringLiteral("DV 100 1080i"));
	QCOMPARE(OmfResolutions::name(2502, QByteArray("DV/C")), QStringLiteral("DV 100 720p"));
	// The DNxHD range is deliberately absent — those route through ulFromResId.
	QVERIFY(OmfResolutions::name(1237, QByteArray("AVHD")).isEmpty());
	QVERIFY(OmfResolutions::name(1244, QByteArray("AVHD")).isEmpty());
}

void TestMdbParser::omf_resolution_id_1244_names_avid_dnx_tr()
{
	// 1244 is registered under the 0x0D version byte (mxfparser.cpp's MC
	// 25.12 additions); the 0x0A spelling every other DNxHD id uses names
	// nothing for it. The neighbours keep the 0x0A spelling.
	QCOMPARE(OmfObjects::ulFromResId(1244), QByteArray::fromHex("060E2B340401010D04010202710A0000"));
	QCOMPARE(MxfParser::codecFromEssenceLabel(OmfObjects::ulFromResId(1244), QStringLiteral("29.97")),
			 QStringLiteral("Avid DNx TR"));
	QCOMPARE(OmfObjects::ulFromResId(1243), QByteArray::fromHex("060e2b340401010a0401020271090000"));
	QCOMPARE(OmfObjects::ulFromResId(1237), QByteArray::fromHex("060e2b340401010a0401020271030000"));
	QVERIFY(OmfObjects::ulFromResId(1234).isEmpty());
	QVERIFY(OmfObjects::ulFromResId(1490).isEmpty());
}

void TestMdbParser::omf_wave_summary_is_chunk_walked()
{
	// A `bext` chunk of odd size comes first (as MC writes it), so a reader
	// that trusts a fixed offset — or forgets RIFF's odd-size padding —
	// never reaches `fmt `.
	QByteArray fmt = le32(1).left(2) + le32(2).left(2) + le32(44100) + le32(44100 * 4) + le32(4).left(2) +
					 le32(16).left(2);
	QByteArray body = QByteArray("WAVE") + QByteArray("bext") + le32(3) + QByteArray("abc") + QByteArray(1, '\0') +
					  QByteArray("fmt ") + le32(16) + fmt + QByteArray("data") + le32(0);
	const QByteArray blob = QByteArray("RIFF") + le32(quint32(body.size())) + body;

	const OmfObjects::WaveSummary s = OmfObjects::readWaveSummary(blob);
	QVERIFY(s.valid);
	QCOMPARE(int(s.formatTag), 1);
	QCOMPARE(s.channels, 2);
	QCOMPARE(s.sampleRate, 44100);
	QCOMPARE(s.bits, 16);

	// Not a WAVE; `data` before any `fmt `; a `fmt ` too short to hold its fields.
	QVERIFY(!OmfObjects::readWaveSummary(QByteArray("FORM") + le32(4) + QByteArray("AIFF")).valid);
	QVERIFY(!OmfObjects::readWaveSummary(QByteArray("RIFF") + le32(12) + QByteArray("WAVE") + QByteArray("data") +
										 le32(0) + QByteArray("fmt ") + le32(16) + fmt)
				 .valid);
	QVERIFY(!OmfObjects::readWaveSummary(QByteArray("RIFF") + le32(20) + QByteArray("WAVE") + QByteArray("fmt ") +
										 le32(16) + fmt.left(8))
				 .valid);
	QVERIFY(!OmfObjects::readWaveSummary(QByteArray()).valid);

	// The real thing: MC 26.8's OMFI:WAVD:Summary for the fixture tone.
	bool ok = false;
	BentoFile b;
	{
		QFile f(fx("omf/mc2026_audio/msmMMOB.mdb"));
		QVERIFY(f.open(QIODevice::ReadOnly));
		QVERIFY(b.load(f.readAll()));
	}
	const int prop = b.propertyId("OMFI:WAVD:Summary");
	QVERIFY(prop >= 0);
	const QVector<quint32> objs = b.objectsWithProperty(prop);
	QVERIFY(!objs.isEmpty());
	for (quint32 o : objs)
	{
		const OmfObjects::WaveSummary real = OmfObjects::readWaveSummary(b.bytes(o, prop));
		QVERIFY(real.valid);
		QCOMPARE(int(real.formatTag), 1);
		QCOMPARE(real.channels, 1);
		QCOMPARE(real.sampleRate, 48000);
		QCOMPARE(real.bits, 24);
		ok = true;
	}
	QVERIFY(ok);
}

void TestMdbParser::omf_aifc_summary_is_chunk_walked()
{
	// 48 kHz as an 80-bit extended: 400E BB80 0000 0000 0000.
	const QByteArray rate48k = QByteArray::fromHex("400ebb800000000000000000").left(10);
	QByteArray comm = be16(1) + be32(2880002) + be16(24) + rate48k + QByteArray("in24");
	QByteArray body = QByteArray("AIFC") + QByteArray("bext") + be32(5) + QByteArray("hello") + QByteArray(1, '\0') +
					  QByteArray("COMM") + be32(quint32(comm.size())) + comm + QByteArray("SSND") + be32(0);
	const QByteArray blob = QByteArray("FORM") + be32(quint32(body.size())) + body;

	const OmfObjects::AifcSummary s = OmfObjects::readAifcSummary(blob);
	QVERIFY(s.valid);
	QCOMPARE(s.channels, 1);
	QCOMPARE(s.frames, qint64(2880002));
	QCOMPARE(s.bits, 24);
	QCOMPARE(s.sampleRate, 48000);
	QCOMPARE(s.compressionType, QByteArray("in24"));

	// A plain AIFF COMM is 18 bytes: no compression type.
	QByteArray comm18 = be16(2) + be32(100) + be16(16) + QByteArray::fromHex("400eac44000000000000");
	const QByteArray aiff = QByteArray("FORM") + be32(4 + 8 + 18) + QByteArray("AIFF") + QByteArray("COMM") +
							be32(18) + comm18;
	const OmfObjects::AifcSummary plain = OmfObjects::readAifcSummary(aiff);
	QVERIFY(plain.valid);
	QCOMPARE(plain.channels, 2);
	QCOMPARE(plain.sampleRate, 44100);
	QCOMPARE(plain.bits, 16);
	QVERIFY(plain.compressionType.isEmpty());

	QVERIFY(!OmfObjects::readAifcSummary(QByteArray("RIFF") + le32(4) + QByteArray("WAVE")).valid);
	QVERIFY(!OmfObjects::readAifcSummary(QByteArray("FORM") + be32(4) + QByteArray("AIFC")).valid);
	QVERIFY(!OmfObjects::readAifcSummary(QByteArray("FORM") + be32(30) + QByteArray("AIFC") + QByteArray("COMM") +
										 be32(18) + comm18.left(9))
				 .valid);

	// The real thing: MC 26.8's OMFI:AIFD:Summary for the fixture tone.
	BentoFile b;
	{
		QFile f(fx("omf/mc2026_audio/msmMMOB.mdb"));
		QVERIFY(f.open(QIODevice::ReadOnly));
		QVERIFY(b.load(f.readAll()));
	}
	const int prop = b.propertyId("OMFI:AIFD:Summary");
	QVERIFY(prop >= 0);
	const QVector<quint32> objs = b.objectsWithProperty(prop);
	QCOMPARE(objs.size(), 1);
	const OmfObjects::AifcSummary real = OmfObjects::readAifcSummary(b.bytes(objs.first(), prop));
	QVERIFY(real.valid);
	QCOMPARE(real.channels, 1);
	QCOMPARE(real.frames, qint64(2880002));
	QCOMPARE(real.bits, 24);
	QCOMPARE(real.sampleRate, 48000);
	QCOMPARE(real.compressionType, QByteArray("in24"));
}

// The database MC 26.8 regenerates beside its 80 shipped OMF slates: every
// mob is a 12-byte omfi:UID, every key comes out in Avid's wrapped 32-byte
// form, and every pair the version-2 PMR names is described on both sides.
void TestMdbParser::omf_era_mdb_describes_every_pmr_pair_with_wrapped_ids()
{
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("omf/avid_supporting/msmMMOB.mdb"), &ok);
	QVERIFY(ok);
	QCOMPARE(db.masters.size(), 80);
	QCOMPARE(db.files.size(), 80);
	for (auto it = db.masters.cbegin(); it != db.masters.cend(); ++it)
	{
		QVERIFY2(OmfUid::isOmfForm(it.key()), qPrintable(it.key()));
		QCOMPARE(it.key(), it->mobIdHex);
		QVERIFY2(!it->clipName.isEmpty(), qPrintable(it.key()));
	}
	for (auto it = db.files.cbegin(); it != db.files.cend(); ++it)
		QVERIFY2(OmfUid::isOmfForm(it.key()), qPrintable(it.key()));

	const QVector<PmrEntry> pmr = PmrParser::parse(fx("omf/avid_supporting/msmFMID.pmr"), &ok);
	QVERIFY(ok);
	QCOMPARE(pmr.size(), 80);
	for (const PmrEntry &e : pmr)
	{
		QVERIFY2(db.files.contains(e.mobId), qPrintable(e.fileName));
		QVERIFY2(db.masters.contains(e.masterMobId), qPrintable(e.fileName));
		const MdbFileMob &f = db.files[e.mobId];
		const QByteArray fnBytes = e.fileName.toUtf8();
		const char *fn = fnBytes.constData();
		// Every slate is one frame of 8-bit video with a codec the table can
		// name — no header read is ever needed for this folder.
		QVERIFY2(f.essenceComplete, fn);
		QVERIFY2(f.essence.valid, fn);
		QVERIFY2(!f.essence.isAudio, fn);
		QVERIFY2(!f.essence.codec.isEmpty(), fn);
		QVERIFY2(!f.essence.codec.startsWith(QLatin1String("Unknown")), qPrintable(f.essence.codec));
		QVERIFY2(!f.essence.resolution.isEmpty(), fn);
		QVERIFY2(!f.essence.fps.isEmpty(), fn);
		QCOMPARE(f.essence.durationFrames, qint64(1));
		QCOMPARE(f.essence.bitDepth, QStringLiteral("8-bit"));
		QCOMPARE(f.usageCode, 0);
		// The v2 PMR has no project column; the _PJ attribute on the file or
		// source mob is the only source, and every slate carries one.
		QVERIFY2(!f.project.isEmpty(), fn);
	}
}

// One file per resolution id, pinned against the filename tokens Avid
// itself chose and against the .vr short names — see omfresolutions.cpp.
void TestMdbParser::omf_era_mdb_video_facts_by_resolution_id()
{
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("omf/avid_supporting/msmMMOB.mdb"), &ok);
	QVERIFY(ok);
	const QHash<QString, PmrEntry> pmr = pmrByName(fx("omf/avid_supporting/msmFMID.pmr"), 80);
	QCOMPARE(pmr.size(), 80);

	// DNxHD-era ids keep whatever the MXF path names their label — the
	// filename tokens (AVHD_145, AVHD_220) agree with the tier table.
	const QString dnx1242 =
		MxfParser::codecFromEssenceLabel(OmfObjects::ulFromResId(1242), QStringLiteral("29.97"));
	const QString dnx1252 =
		MxfParser::codecFromEssenceLabel(OmfObjects::ulFromResId(1252), QStringLiteral("59.94"));
	QCOMPARE(dnx1242, QStringLiteral("Avid DNx SQ (DNxHD 145)"));
	QCOMPARE(dnx1252, QStringLiteral("Avid DNx SQ (DNxHD 145)")); // 2012 DNxHD whitepaper: 720p SQ at 59.94

	struct Pin
	{
		const char *file;
		const char *codec;
		const char *resolution;
		const char *fps;
		const char *project;
		bool dropFrame;
	};
	const Pin kPins[] = {
		// JFIF — single-field rasters (layout 2) show as stored, field rasters (layout 1) doubled
		{"BLACK_352x243x1_JFIF12S.omf", "15:1s", "352x248", "29.97", "NTSC slides", false},
		{"BLACK_720x243x2_JFIF35.omf", "20:1", "720x496", "29.97", "NTSC slides", false},
		{"BLACK_720x288x2_JFIF42.omf", "20:1", "720x592", "25", "PAL slides", false},
		{"BLACK_720x486x1_JFIF25P.omf", "28:1", "720x496", "24", "NTSC Film w_ topness fix", false},
		{"BLACK_288x243x1_JFIF15m.omf", "10:1m", "288x248", "29.97", "Symphony NTSC", false},
		{"BLACK_288x243x1_JFIF20mP.omf", "8:1m", "288x248", "24", "24P NTSC", false},
		// DV — finalise adds the i/p(PAL/NTSC) qualifier exactly as it does for MXF-era DV
		{"BLACK_720x480x1_DV411.omf", "DV 25 411 i(NTSC)", "720x480", "29.97", "NTSC DV", true},
		{"FORMAT_720x576x1_DV420.omf", "DV 25 420 i(PAL)", "720x576", "25", "PAL 420", false},
		{"BLACK_720x480x1_DV50.omf", "DV 50 i(NTSC)", "720x480", "29.97", "NTSC", false},
		{"BLACK_720x480x1_DV411P.omf", "DV25P 411", "720x480", "24", "24P NTSC", false},
		{"BLACK_720x576x1_DV420P.omf", "DV 25P 420 p(PAL)", "720x576", "25", "25P Pal Titles", false},
		// Uncompressed (MXF1 in this database) and MPEG 50
		{"BLACK_720x243x2_UNCOMP.omf", "1:1", "720x496", "29.97", "PeleSympProj", false},
		{"BLACK_720x576x1_UNCOMP_24P.omf", "1:1", "720x592", "24", "PAL Film", false},
		{"BLACK_720x576x1_MPEG50.omf", "MPEG 50", "720x608", "25", "Sample PAL MPEG media", false},
		// DV 100 (UNVERIFIED names; 2402 is how this database re-ids the 2502 files)
		{"BLACK_1920x540x2_DV100_115.omf", "DV 100 1080i", "1920x1080", "25", "1080i 50", false},
		{"BLACK_1280x720x1_DV100_90.omf", "DV 100 720p", "1280x720", "59.94", "DC 720p 59.94", true},
		// DNxHD-era ids through ulFromResId, including the 1244 gap fix
		{"BLACK_1440x540x2_DNxHD.omf", "Avid DNx TR", "1920x1080", "29.97", "1080i60 HDV", true},
		{"BLACK_1920x1080x1_DNxHD_115.omf", "Avid DNx SQ (DNxHD 115)", "1920x1080", "23.976", "1080p 23.976",
		 false},
		{"BLACK_1920x540x2_AVHD_220.omf", "Avid DNx HQ (DNxHD 220)", "1920x1080", "29.97", "1080i59_94", false},
	};
	for (const Pin &pin : kPins)
	{
		const QString name = QLatin1String(pin.file);
		QVERIFY2(pmr.contains(name), pin.file);
		const PmrEntry &e = pmr[name];
		QVERIFY2(db.files.contains(e.mobId), pin.file);
		const MdbFileMob &f = db.files[e.mobId];
		QVERIFY2(f.essence.codec == QLatin1String(pin.codec),
				 qPrintable(name + QStringLiteral(": codec ") + f.essence.codec));
		QVERIFY2(f.essence.resolution == QLatin1String(pin.resolution),
				 qPrintable(name + QStringLiteral(": res ") + f.essence.resolution));
		QVERIFY2(f.essence.fps == QLatin1String(pin.fps), qPrintable(name + QStringLiteral(": fps ") + f.essence.fps));
		QVERIFY2(f.project == QLatin1String(pin.project), qPrintable(name + QStringLiteral(": project ") + f.project));
		QCOMPARE(f.essence.dropFrame, pin.dropFrame);
		QVERIFY2(f.essenceComplete, pin.file);
	}

	// The two DNxHD ids the app's table does not know name what the MXF
	// path would name them, never "Unknown".
	QCOMPARE(db.files[pmr[QStringLiteral("BLACK_1920x540x2_AVHD_145.omf")].mobId].essence.codec, dnx1242);
	QCOMPARE(db.files[pmr[QStringLiteral("BLACK_1280x720x1_DNxHD_145.omf")].mobId].essence.codec, dnx1252);

	// Master-side facts: clip name and bin from the master's own objects.
	const MdbMasterMob &jfif35 = db.masters[pmr[QStringLiteral("BLACK_720x243x2_JFIF35.omf")].masterMobId];
	QCOMPARE(jfif35.clipName, QStringLiteral("Black 720x486.PICT"));
	QCOMPARE(jfif35.bin, QStringLiteral("NTSC slides"));
	const MdbMasterMob &dv420 = db.masters[pmr[QStringLiteral("FORMAT_720x576x1_DV420.omf")].masterMobId];
	QCOMPARE(dv420.clipName, QStringLiteral("WRONG_FORMAT_720x576.PICT"));
	QCOMPARE(dv420.bin, QStringLiteral("PAL 420 Bin2"));
	QVERIFY(dv420.isImported);
	// The 2021 QuickTime-referenced slates carry a MACL path AND a `UNC Path`.
	const MdbMasterMob &avhd = db.masters[pmr[QStringLiteral("BLACK_1920x540x2_AVHD_145.omf")].masterMobId];
	QCOMPARE(avhd.sourceFileName, QStringLiteral("BLACK_1920x540x2_AVHD_145.png"));
	QVERIFY2(avhd.sourceFilePath.contains(QStringLiteral("UDevC00169755_QTref")), qPrintable(avhd.sourceFilePath));
}

// The database MC 26.8 wrote fresh for two one-minute tones (a WAVE and an
// AIFF-C), in project zTeßt_PAL_25p, bins WAVE(OMF) and AIFF-C(OMF).
void TestMdbParser::omf_era_audio_mdb_describes_both_tone_files()
{
	bool ok = false;
	const MdbDatabase db = MdbParser::load(fx("omf/mc2026_audio/msmMMOB.mdb"), &ok);
	QVERIFY(ok);
	QCOMPARE(db.masters.size(), 2);
	QCOMPARE(db.files.size(), 2);
	const QHash<QString, PmrEntry> pmr = pmrByName(fx("omf/mc2026_audio/msmFMID.pmr"), 2);
	QCOMPARE(pmr.size(), 2);
	const QString project = QString::fromUtf8("zTe\xc3\x9ft_PAL_25p");

	struct Pin
	{
		const char *file;
		const char *clipName;
		const char *bin;
	};
	const Pin kPins[] = {
		{"TONE_100A01.6A972974.039700.wav", "TONE: 1000 Hz @ -14.0 dB.1", "WAVE(OMF)"},
		{"TONE_100A01.6A972997.0C53E0.aif", "TONE: 1000 Hz @ -20.0 dB.2", "AIFF-C(OMF)"},
	};
	for (const Pin &pin : kPins)
	{
		const QString name = QLatin1String(pin.file);
		QVERIFY2(pmr.contains(name), pin.file);
		const PmrEntry &e = pmr[name];
		QVERIFY2(OmfUid::isOmfForm(e.mobId), pin.file);
		QVERIFY2(db.files.contains(e.mobId), pin.file);
		QVERIFY2(db.masters.contains(e.masterMobId), pin.file);

		const MdbFileMob &f = db.files[e.mobId];
		QVERIFY2(f.essenceComplete, pin.file);
		QVERIFY2(f.essence.valid, pin.file);
		QVERIFY2(f.essence.isAudio, pin.file);
		QCOMPARE(f.essence.sampleRate, 48000);
		QCOMPARE(f.essence.channels, 1);
		QCOMPARE(f.essence.bitDepth, QStringLiteral("24-bit"));
		QCOMPARE(f.essence.durationFrames, qint64(1500)); // 2,880,002 samples × 25 ÷ 48000
		QCOMPARE(f.essence.timecodeBase, 25);
		// OMF-era: the database names legacy audio the way Avid's own menus
		// do ("WAVE (OMF)" / "AIFF-C (OMF)"); "PCM" is the MXF-era name.
		const QString avidLabel = QString::fromLatin1(pin.file).endsWith(QLatin1String(".wav"))
									  ? QStringLiteral("WAVE (OMF)")
									  : QStringLiteral("AIFF-C (OMF)");
		QCOMPARE(f.essence.codec, avidLabel);
		QVERIFY2(f.essence.resolution.isEmpty(), pin.file);
		QVERIFY2(!f.essence.dropFrame, pin.file);
		QCOMPARE(f.usageCode, 0);
		QCOMPARE(f.project, project); // MC 2026 puts _PJ on the file mob

		const MdbMasterMob &m = db.masters[e.masterMobId];
		QCOMPARE(m.clipName, QString::fromLatin1(pin.clipName));
		QCOMPARE(m.bin, QString::fromLatin1(pin.bin));
		QCOMPARE(m.usageCode, 7);
		QVERIFY2(!m.isImported, pin.file);
		QVERIFY2(m.project.isEmpty(), pin.file);
	}
}

// A SCLP names its source mob in that mob's own width: 12 bytes for the
// 2021 slates' source mobs, 32 for the UMID MC 2026 writes on the physical
// mob. Both hops must land on the TCCP, which also yields start and rate.
void TestMdbParser::omf_timecode_is_reached_through_either_mob_width()
{
	struct Pin
	{
		const char *mdb;
		const char *pmr;
		int pairs;
		const char *file;
		qint64 start;
		int fps;
		bool drop;
		int sourceIdWidth;
	};
	const Pin kPins[] = {
		{"omf/mc2026_audio/msmMMOB.mdb", "omf/mc2026_audio/msmFMID.pmr", 2, "TONE_100A01.6A972974.039700.wav", 90000,
		 25, false, MobId::kRawSize},
		{"omf/avid_supporting/msmMMOB.mdb", "omf/avid_supporting/msmFMID.pmr", 80, "BLACK_720x243x2_JFIF35.omf",
		 108000, 30, false, OmfUid::kUidSize},
		{"omf/avid_supporting/msmMMOB.mdb", "omf/avid_supporting/msmFMID.pmr", 80, "BLACK_1920x1080x1_DNxHD_115.omf",
		 86400, 24, false, OmfUid::kUidSize},
		{"omf/avid_supporting/msmMMOB.mdb", "omf/avid_supporting/msmFMID.pmr", 80, "BLACK_720x480x1_DV411.omf", 900,
		 30, true, OmfUid::kUidSize},
	};
	for (const Pin &pin : kPins)
	{
		BentoFile b;
		{
			QFile f(fx(pin.mdb));
			QVERIFY2(f.open(QIODevice::ReadOnly), pin.mdb);
			QVERIFY2(b.load(f.readAll()), pin.mdb);
		}
		const OmfObjects::Props p(b);
		const QHash<QString, PmrEntry> pmr = pmrByName(fx(pin.pmr), pin.pairs);
		QVERIFY2(pmr.contains(QLatin1String(pin.file)), pin.file);

		OmfObjects::ObjectByMob objectByMob;
		const quint32 fileMob = findMob(b, p, pmr[QLatin1String(pin.file)].mobId, objectByMob);
		QVERIFY2(fileMob != 0, pin.file);
		QCOMPARE(b.bytes(fileMob, p.mobId).size(), qsizetype(OmfUid::kUidSize));

		// The file mob itself carries no TCCP; only the hop reaches one.
		QSet<quint32> seen;
		const quint32 tccp = OmfObjects::findTimecodeComponent(b, p, fileMob, objectByMob, seen, 0);
		QVERIFY2(tccp != 0, pin.file);
		const OmfObjects::Timecode tc = OmfObjects::readTimecode(b, p, tccp);
		QVERIFY(tc.found);
		QCOMPARE(tc.start, pin.start);
		QCOMPARE(tc.fps, pin.fps);
		QCOMPARE(tc.dropFrame, pin.drop);

		// And the mob the hop went through is of the width this pin claims.
		const quint32 src = OmfObjects::findSourceMob(b, p, fileMob, objectByMob);
		QVERIFY2(src != 0, pin.file);
		QCOMPARE(b.bytes(src, p.mobId).size(), qsizetype(pin.sourceIdWidth));
	}
	QVERIFY(!OmfObjects::readTimecode(BentoFile(), OmfObjects::Props(BentoFile()), 0).found);
}

// Sound Designer II: built to the MC binary's property names, UNVERIFIED —
// no specimen exists (README). The descriptor keeps bits and channels in
// two u16 properties instead of a header blob.
void TestMdbParser::omf_sd2d_descriptor_reads_its_two_properties()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	BentoBuilder w;
	const QByteArray uid = omfUid("0102030405060708");
	const quint32 file = w.addObject("MOBJ");
	const quint32 sd2d = w.addObject("SD2D");
	w.set(file, "OMFI:MOBJ:MobID", uid);
	w.setU32(file, "OMFI:MOBJ:UsageCode", 0);
	w.setRational(file, "OMFI:CPNT:EditRate", 25, 1);
	w.setHandle(file, "OMFI:MOBJ:PhysicalMedia", sd2d);
	w.setRational(sd2d, "OMFI:MDFL:SampleRate", 48000, 1);
	w.setU32(sd2d, "OMFI:MDFL:Length", 96000);
	w.setU16(sd2d, "OMFI:SD2D:BitsPerSample", 16);
	w.setU16(sd2d, "OMFI:SD2D:NumChannels", 2);

	bool ok = false;
	const MdbDatabase db = MdbParser::load(writeMdb(tmp.path() + "/msmMMOB.mdb", w.build()), &ok);
	QVERIFY(ok);
	QVERIFY(db.masters.isEmpty());
	QCOMPARE(db.files.size(), 1);
	const QString key = OmfUid::canonicalHex(uid);
	QVERIFY(OmfUid::isOmfForm(key));
	QVERIFY(db.files.contains(key));
	const MdbFileMob &f = db.files[key];
	QVERIFY(f.essenceComplete);
	QVERIFY(f.essence.isAudio);
	QCOMPARE(f.essence.sampleRate, 48000);
	QCOMPARE(f.essence.channels, 2);
	QCOMPARE(f.essence.bitDepth, QStringLiteral("16-bit"));
	QCOMPARE(f.essence.durationFrames, qint64(50)); // 96000 samples × 25/48000
	QCOMPARE(f.essence.timecodeBase, 25);
	// OMF-era: Avid's own label for Sound Designer II media, no wrapper tag.
	QCOMPARE(f.essence.codec, QStringLiteral("SDII"));
}

// OMF files written on Windows or UNIX point _SRCFILE at a WINL / UNXL
// rather than the MACL every MXF-era database uses; the path comes out
// the same way. The same builder covers _PJ on a master and _MEDIAFILE.
void TestMdbParser::omf_winl_and_unxl_locators_yield_the_source_path()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	BentoBuilder w;

	// Master A: _IMPORTSETTING → ATTR → _SRCFILE → WINL (FL:PathName), plus _PJ.
	const QByteArray uidA = omfUid("a1a2a3a4a5a6a7a8");
	const quint32 a = w.addObject("MOBJ");
	w.set(a, "OMFI:MOBJ:MobID", uidA);
	w.setString(a, "OMFI:CPNT:Name", "Windows import");
	w.setU32(a, "OMFI:MOBJ:UsageCode", 7);
	const quint32 attrA = w.addObject("ATTR");
	const quint32 importA = w.addObject("ATTB");
	const quint32 importAttrA = w.addObject("ATTR");
	const quint32 srcA = w.addObject("ATTB");
	const quint32 winl = w.addObject("WINL");
	const quint32 pjA = w.addObject("ATTB");
	w.setHandle(a, "OMFI:CPNT:Attributes", attrA);
	w.setHandles(attrA, "OMFI:ATTR:AttrRefs", {importA, pjA});
	w.setString(importA, "OMFI:ATTB:Name", "_IMPORTSETTING");
	w.setU16(importA, "OMFI:ATTB:Kind", 3);
	w.setHandle(importA, "OMFI:ATTB:ObjAttribute", importAttrA);
	w.setHandles(importAttrA, "OMFI:ATTR:AttrRefs", {srcA});
	w.setString(srcA, "OMFI:ATTB:Name", "_SRCFILE");
	w.setU16(srcA, "OMFI:ATTB:Kind", 3);
	w.setHandle(srcA, "OMFI:ATTB:ObjAttribute", winl);
	w.setString(winl, "OMFI:FL:PathName", "C:\\clips\\tone.wav");
	w.setString(pjA, "OMFI:ATTB:Name", "_PJ");
	w.setU16(pjA, "OMFI:ATTB:Kind", 2);
	w.setString(pjA, "OMFI:ATTB:StringAttribute", "Win Project");

	// Master B: _SRCFILE → UNXL carrying only the UNIX locator's own property.
	const QByteArray uidB = omfUid("b1b2b3b4b5b6b7b8");
	const quint32 b = w.addObject("MOBJ");
	w.set(b, "OMFI:MOBJ:MobID", uidB);
	w.setString(b, "OMFI:CPNT:Name", "UNIX import");
	const quint32 attrB = w.addObject("ATTR");
	const quint32 srcB = w.addObject("ATTB");
	const quint32 unxl = w.addObject("UNXL");
	const quint32 mediaB = w.addObject("ATTB");
	const quint32 mediaLoc = w.addObject("MACL");
	w.setHandle(b, "OMFI:CPNT:Attributes", attrB);
	w.setHandles(attrB, "OMFI:ATTR:AttrRefs", {srcB, mediaB});
	w.setString(srcB, "OMFI:ATTB:Name", "_SRCFILE");
	w.setU16(srcB, "OMFI:ATTB:Kind", 3);
	w.setHandle(srcB, "OMFI:ATTB:ObjAttribute", unxl);
	w.setString(unxl, "OMFI:UNXL:PathName", "/mnt/clips/tone.aif");
	w.setString(mediaB, "OMFI:ATTB:Name", "_MEDIAFILE");
	w.setU16(mediaB, "OMFI:ATTB:Kind", 3);
	w.setHandle(mediaB, "OMFI:ATTB:ObjAttribute", mediaLoc);
	w.setString(mediaLoc, "OMFI:FL:POSIXPathName", "/Volumes/Media/OMFI MediaFiles/tone.aif");

	const QByteArray bytes = w.build();
	bool ok = false;
	const MdbDatabase db = MdbParser::load(writeMdb(tmp.path() + "/msmMMOB.mdb", bytes), &ok);
	QVERIFY(ok);
	QCOMPARE(db.masters.size(), 2);
	QVERIFY(db.files.isEmpty());

	const MdbMasterMob &ma = db.masters[OmfUid::canonicalHex(uidA)];
	QCOMPARE(ma.sourceFilePath, QStringLiteral("C:\\clips\\tone.wav"));
	QCOMPARE(ma.sourceFileName, QStringLiteral("tone.wav"));
	QVERIFY(ma.isImported);
	QCOMPARE(ma.project, QStringLiteral("Win Project"));

	const MdbMasterMob &mb = db.masters[OmfUid::canonicalHex(uidB)];
	QCOMPARE(mb.sourceFilePath, QStringLiteral("/mnt/clips/tone.aif"));
	QCOMPARE(mb.sourceFileName, QStringLiteral("tone.aif"));
	QVERIFY(!mb.isImported); // no _IMPORTSETTING on this one
	QVERIFY(mb.project.isEmpty());

	// _MEDIAFILE is not surfaced by the database record; the walker keeps it.
	BentoFile bf;
	QVERIFY(bf.load(bytes));
	const OmfObjects::Props p(bf);
	OmfObjects::Attributes attrs;
	attrs.omfEra = true; // as MdbParser sets it for a 12-byte mob: the UNXL is admitted
	QSet<quint32> seen;
	OmfObjects::walkAttributes(bf, p, BentoFile::handle(bf.bytes(b, p.attrs)), attrs, seen, 0);
	QCOMPARE(attrs.mediaFilePath, QStringLiteral("/Volumes/Media/OMFI MediaFiles/tone.aif"));
	QCOMPARE(attrs.sourceFilePath, QStringLiteral("/mnt/clips/tone.aif"));
}

// The other side of the locator rule: an MXF-era (32-byte) master never
// takes a WINL/UNXL _SRCFILE, however its attributes are ordered, so a
// Windows-written MXF-era database reports the Source File it always did —
// the kind-2 `UNC Path` — and a MACL still resolves exactly as before.
void TestMdbParser::mxf_era_master_keeps_the_macl_only_srcfile_rule()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	BentoBuilder w;

	// Master A: _IMPORTSETTING → ATTR → _SRCFILE → WINL, written BEFORE the
	// `UNC Path` attribute. The path must come from the UNC attribute.
	const QByteArray mobA = QByteArray::fromHex("060a2b340101010501010f1013000000"
												"a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1");
	const quint32 a = w.addObject("MOBJ");
	w.set(a, "OMFI:MOBJ:MobID", mobA);
	w.setString(a, "OMFI:CPNT:Name", "Windows MXF import");
	w.setU32(a, "OMFI:MOBJ:UsageCode", 7);
	const quint32 attrA = w.addObject("ATTR");
	const quint32 importA = w.addObject("ATTB");
	const quint32 importAttrA = w.addObject("ATTR");
	const quint32 srcA = w.addObject("ATTB");
	const quint32 winlA = w.addObject("WINL");
	const quint32 uncA = w.addObject("ATTB");
	w.setHandle(a, "OMFI:CPNT:Attributes", attrA);
	w.setHandles(attrA, "OMFI:ATTR:AttrRefs", {importA, uncA});
	w.setString(importA, "OMFI:ATTB:Name", "_IMPORTSETTING");
	w.setU16(importA, "OMFI:ATTB:Kind", 3);
	w.setHandle(importA, "OMFI:ATTB:ObjAttribute", importAttrA);
	w.setHandles(importAttrA, "OMFI:ATTR:AttrRefs", {srcA});
	w.setString(srcA, "OMFI:ATTB:Name", "_SRCFILE");
	w.setU16(srcA, "OMFI:ATTB:Kind", 3);
	w.setHandle(srcA, "OMFI:ATTB:ObjAttribute", winlA);
	w.setString(winlA, "OMFI:FL:PathName", "C:\\clips\\import.mov");
	w.setString(uncA, "OMFI:ATTB:Name", "UNC Path");
	w.setU16(uncA, "OMFI:ATTB:Kind", 2);
	w.setString(uncA, "OMFI:ATTB:StringAttribute", "\\\\server\\share\\import.mov");

	// Master B: the WINL alone. Nothing names a path the MXF-era rule accepts.
	const QByteArray mobB = QByteArray::fromHex("060a2b340101010501010f1013000000"
												"b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2");
	const quint32 b = w.addObject("MOBJ");
	w.set(b, "OMFI:MOBJ:MobID", mobB);
	w.setString(b, "OMFI:CPNT:Name", "Windows MXF import, no UNC");
	const quint32 attrB = w.addObject("ATTR");
	const quint32 srcB = w.addObject("ATTB");
	const quint32 winlB = w.addObject("WINL");
	w.setHandle(b, "OMFI:CPNT:Attributes", attrB);
	w.setHandles(attrB, "OMFI:ATTR:AttrRefs", {srcB});
	w.setString(srcB, "OMFI:ATTB:Name", "_SRCFILE");
	w.setU16(srcB, "OMFI:ATTB:Kind", 3);
	w.setHandle(srcB, "OMFI:ATTB:ObjAttribute", winlB);
	w.setString(winlB, "OMFI:FL:PathName", "C:\\clips\\orphan.mov");

	// Master C: a MACL, the locator every MXF-era database uses — unchanged.
	const QByteArray mobC = QByteArray::fromHex("060a2b340101010501010f1013000000"
												"c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3");
	const quint32 c = w.addObject("MOBJ");
	w.set(c, "OMFI:MOBJ:MobID", mobC);
	w.setString(c, "OMFI:CPNT:Name", "Mac MXF import");
	const quint32 attrC = w.addObject("ATTR");
	const quint32 srcC = w.addObject("ATTB");
	const quint32 maclC = w.addObject("MACL");
	w.setHandle(c, "OMFI:CPNT:Attributes", attrC);
	w.setHandles(attrC, "OMFI:ATTR:AttrRefs", {srcC});
	w.setString(srcC, "OMFI:ATTB:Name", "_SRCFILE");
	w.setU16(srcC, "OMFI:ATTB:Kind", 3);
	w.setHandle(srcC, "OMFI:ATTB:ObjAttribute", maclC);
	w.setString(maclC, "OMFI:FL:POSIXPathName", "/Volumes/Media/import.mov");

	bool ok = false;
	const MdbDatabase db = MdbParser::load(writeMdb(tmp.path() + "/msmMMOB.mdb", w.build()), &ok);
	QVERIFY(ok);
	QCOMPARE(db.masters.size(), 3);

	const MdbMasterMob &ma = db.masters[BentoFile::mobIdHex(mobA)];
	QCOMPARE(ma.sourceFilePath, QStringLiteral("\\\\server\\share\\import.mov"));
	QCOMPARE(ma.sourceFileName, QStringLiteral("import.mov"));
	QVERIFY(ma.isImported);

	const MdbMasterMob &mb = db.masters[BentoFile::mobIdHex(mobB)];
	QVERIFY2(mb.sourceFilePath.isEmpty(), qPrintable(mb.sourceFilePath));
	QVERIFY(mb.sourceFileName.isEmpty());

	const MdbMasterMob &mc = db.masters[BentoFile::mobIdHex(mobC)];
	QCOMPARE(mc.sourceFilePath, QStringLiteral("/Volumes/Media/import.mov"));
	QCOMPARE(mc.sourceFileName, QStringLiteral("import.mov"));
}

void TestMdbParser::omf2_roles_and_file_master_ancestry()
{
	QTemporaryDir temp;
	for (bool ambiguous : {false, true})
	{
		bool ok = false;
		const auto db = MdbParser::load(writeMdb(temp.filePath("roles.mdb"), TestOmf::sdii(true, ambiguous)), &ok);
		QVERIFY(ok);
		QCOMPARE(db.revision, OmfObjects::Revision::Omf2);
		QCOMPARE(db.files.size(), 1);
		QCOMPARE(db.masters.size(), ambiguous ? 2 : 1);
		QVERIFY(!db.masters.contains(OmfUid::canonicalHex(TestOmf::uid(99)))); // CMOB
		QVERIFY(!db.masters.contains(OmfUid::canonicalHex(TestOmf::uid(3)))); // physical SMOB
		QVERIFY(!db.masters.value(OmfUid::canonicalHex(TestOmf::uid(1))).classificationKnown);
		const auto file = db.files.value(OmfUid::canonicalHex(TestOmf::uid(2)));
		QVERIFY(file.essenceComplete);
		QCOMPARE(file.essence.codec, QStringLiteral("SDII"));
		QCOMPARE(file.masterMobId, ambiguous ? QString() : OmfUid::canonicalHex(TestOmf::uid(1)));
	}
}

void TestMdbParser::descriptor_failure_never_creates_a_master()
{
	QTemporaryDir temp;
	for (bool omf2 : {false, true})
	{
		bool ok = false;
		const auto db = MdbParser::load(writeMdb(temp.filePath("bad.mdb"), TestOmf::sdii(omf2, false, true)), &ok);
		QVERIFY(ok);
		QVERIFY(db.files.isEmpty());
		QVERIFY(!db.masters.contains(OmfUid::canonicalHex(TestOmf::uid(2))));
	}
}

void TestMdbParser::external_toolkit_semantic_regression()
{
	// Original toolkit specimens are intentionally not redistributed here.
	// Set OMF_TOOLKIT_SAMPLES to a local omfkt22/NTProjects_VS10 directory.
	const QString samples = qEnvironmentVariable("OMF_TOOLKIT_SAMPLES");
	if (samples.isEmpty())
		QSKIP("Set OMF_TOOLKIT_SAMPLES to validate external toolkit writer outputs");
	bool ok = false;
	const auto two = MdbParser::load(QDir(samples).filePath("Simple2x.omf"), &ok);
	QVERIFY(ok);
	QCOMPARE(two.revision, OmfObjects::Revision::Omf2);
	// unittest/MkSimple.c CreateMasterMob writes two Null Master Mobs,
	// both with null source clips, plus a separate Simple Composition.
	QCOMPARE(two.masters.size(), 2);
	QVERIFY(two.files.isEmpty());
	for (const auto &master : two.masters)
		QCOMPARE(master.clipName, QStringLiteral("Null Master Mob"));
	const auto one = MdbParser::load(QDir(samples).filePath("Simple1x.omf"), &ok);
	QVERIFY(ok);
	QCOMPARE(one.revision, OmfObjects::Revision::Omf1);
	QVERIFY(!one.masters.isEmpty());
	// MkDesc.c writes tape and film source mobs, neither file nor master.
	const auto physical = MdbParser::load(QDir(samples).filePath("MkDesc2x.omf"), &ok);
	QVERIFY(ok);
	QCOMPARE(physical.revision, OmfObjects::Revision::Omf2);
	QVERIFY(physical.isEmpty());
	const auto complex = MdbParser::load(QDir(samples).filePath("Complx2x.omf"), &ok);
	QVERIFY(ok);
	QCOMPARE(complex.files.size(), 3);
	int audio = 0, video = 0;
	// MkCplx.c CreateVideoAudioMedia writes 320x240 TIFF video and two
	// mono16-bit44.1kHz AIFF tracks, all owned by Synchronized AV.
	for (const auto &file : complex.files)
	{
		QVERIFY(file.essence.valid);
		QCOMPARE(complex.masters.value(file.masterMobId).clipName, QStringLiteral("Synchronized AV"));
		if (file.essence.isAudio)
		{
			++audio;
			QCOMPARE(file.essence.sampleRate, 44100);
			QCOMPARE(file.essence.channels, 1);
			QCOMPARE(file.essence.bitDepth, QStringLiteral("16-bit"));
			QCOMPARE(file.essence.codec, QStringLiteral("AIFF-C (OMF)"));
		}
		else
		{
			++video;
			QCOMPARE(file.essence.width, 320);
			QCOMPARE(file.essence.height, 240);
			QCOMPARE(file.essence.bitDepth, QStringLiteral("8-bit"));
			QCOMPARE(file.essence.codec, QStringLiteral("JPEG (TIFF)"));
		}
	}
	QCOMPARE(audio, 2);
	QCOMPARE(video, 1);
}

void TestMdbParser::omf2_video_uses_full_mixed_field_height_and_64_bit_length()
{
	for (bool big : {false, true})
	{
		TestOmf::Writer w(true, big);
		w.setImmediate(1, "OMFI:OOBJ:ObjClass", "HEAD");
		w.setImmediate(1, "OMFI:HEAD:Version", QByteArray::fromHex("0200"));
		w.setImmediate(1, "OMFI:HEAD:ByteOrder", big ? "MM" : "II");
		const auto mob = w.addObject("SMOB"), desc = w.addObject("CDCI");
		w.set(mob, "OMFI:MOBJ:MobID", w.word(42) + w.word(12) + w.word(7));
		w.set(mob, "OMFI:SMOB:MediaDescription", w.word(desc));
		w.setRational(desc, "OMFI:MDFL:SampleRate", 25, 1);
		w.set(desc, "OMFI:MDFL:Length", w.wide(0x10000002aULL));
		w.setU32(desc, "OMFI:DIDD:StoredWidth", 1920);
		w.setU32(desc, "OMFI:DIDD:StoredHeight", 1080);
		w.setU16(desc, "OMFI:DIDD:FrameLayout", 3);
		w.setU32(desc, "OMFI:CDCI:ComponentWidth", 10);
		w.setString(desc, "OMFI:DIDD:Compression", "JPEG");
		BentoFile b;
		QVERIFY(b.load(w.build()));
		OmfObjects::Props p(b);
		MxfMetadata m;
		bool codecKnown = false;
		QVERIFY(OmfObjects::readDescriptor(b, p, mob, desc, {}, m, &codecKnown));
		QVERIFY(m.valid);
		QVERIFY(codecKnown);
		QCOMPARE(m.codec, QStringLiteral("JPEG"));
		QCOMPARE(m.width, 1920);
		QCOMPARE(m.height, 1080);
		QCOMPARE(m.durationFrames, qint64(0x10000002aULL));
		QCOMPARE(m.fps, QStringLiteral("25"));
	}
}

void TestMdbParser::tiff_summary_respects_own_byte_order_and_avid_short_values()
{
	for (bool big : {false, true})
		for (bool avid : {false, true})
		{
			TestOmf::Writer w(true, big);
			w.setImmediate(1, "OMFI:OOBJ:ObjClass", "HEAD");
			w.setImmediate(1, "OMFI:HEAD:Version", QByteArray::fromHex("0200"));
			w.setImmediate(1, "OMFI:HEAD:ByteOrder", big ? "MM" : "II");
			const auto mob = w.addObject("SMOB"), desc = w.addObject("TIFD");
			w.set(mob, "OMFI:MOBJ:MobID", w.word(42) + w.word(12) + w.word(7));
			QByteArray tiff = QByteArray(big ? "MM" : "II") + w.half(42) + w.word(8) + w.half(avid ? 5 : 4);
			auto entry = [&](quint16 tag, quint16 type, quint32 value) {
				tiff += w.half(tag) + w.half(type) + w.word(1);
				tiff += type == 3 && !avid ? w.half(quint16(value)) + w.half(0) : w.word(value);
			};
			entry(256, 4, 320);
			entry(257, 4, 240);
			entry(258, 3, 8);
			entry(259, 3, 6);
			if (avid) entry(34434, 3, 4); // TIFF mixed-fields means two separate image fields
			tiff += w.word(0);
			w.set(desc, "OMFI:TIFD:Summary", tiff);
			w.setRational(desc, "OMFI:MDFL:SampleRate", 25, 1);
			w.set(desc, "OMFI:MDFL:Length", w.wide(1));
			BentoFile b;
			QVERIFY(b.load(w.build()));
			MxfMetadata m;
			QVERIFY(OmfObjects::readDescriptor(b, OmfObjects::Props(b), mob, desc, {}, m));
			QVERIFY(m.valid);
			QCOMPARE(m.width, 320);
			QCOMPARE(m.height, avid ? 480 : 240);
			QCOMPARE(m.bitDepth, QStringLiteral("8-bit"));
			QCOMPARE(m.codec, QStringLiteral("JPEG (TIFF)"));
		}
}

void TestMdbParser::uncompressed_alpha_requires_explicit_none_and_component_arrays()
{
	const struct
	{
		const char *name;
		const char *descriptor;
		QByteArray compression, layout, depths, coding;
		bool alpha;
	} cases[] = {
		{"alpha", "RGBA", "NONE", "A", QByteArray(1, 8), {}, true},
		{"terminated-arrays", "RGBA", "NONE", QByteArray("A\0", 2), QByteArray::fromHex("0800"), {}, true},
		{"missing-compression", "RGBA", {}, "A", QByteArray(1, 8), {}, false},
		{"unknown-compression", "RGBA", "ZXYZ", "A", QByteArray(1, 8), {}, false},
		{"wrong-class", "CDCI", "NONE", "A", QByteArray(1, 8), {}, false},
		{"color", "RGBA", "NONE", "RGB", QByteArray::fromHex("080808"), {}, false},
		{"alpha-and-color", "RGBA", "NONE", "AR", QByteArray::fromHex("0808"), {}, false},
		{"different-depth", "RGBA", "NONE", "A", QByteArray(1, 16), {}, false},
		{"missing-layout", "RGBA", "NONE", {}, QByteArray(1, 8), {}, false},
		{"oversized-layout", "RGBA", "NONE", QByteArray(32, 'A'), QByteArray(1, 8), {}, false},
		{"explicit-coding", "RGBA", "NONE", "A", QByteArray(1, 8), QByteArray(16, 'x'), false},
		{"unusable-coding", "RGBA", "NONE", "A", QByteArray(1, 8), QByteArray(1, 'x'), false},
	};
	for (bool omf2 : {false, true})
		for (int mobSize : {12, 32})
			for (const auto &test : cases)
			{
				TestOmf::Writer w(omf2, false);
				w.setImmediate(1, omf2 ? "OMFI:OOBJ:ObjClass" : "OMFI:ObjID", "HEAD");
				w.setImmediate(1, omf2 ? "OMFI:HEAD:Version" : "OMFI:Version", QByteArray::fromHex(omf2 ? "0200" : "0100"));
				const auto mob = w.addObject(omf2 ? "SMOB" : "MOBJ"), desc = w.addObject(test.descriptor);
				w.set(mob, "OMFI:MOBJ:MobID", QByteArray(mobSize, 'm'));
				w.setU32(desc, "OMFI:DIDD:StoredWidth", 1920);
				w.setU32(desc, "OMFI:DIDD:StoredHeight", 1080);
				w.setRational(desc, "OMFI:MDFL:SampleRate", 24, 1);
				w.set(desc, "OMFI:MDFL:Length", w.word(1));
				if (!test.compression.isNull()) w.setString(desc, "OMFI:DIDD:Compression", test.compression);
				if (!test.layout.isNull()) w.set(desc, "OMFI:RGBA:PixelLayout", test.layout);
				if (!test.depths.isNull()) w.set(desc, "OMFI:RGBA:PixelStructure", test.depths);
				if (!test.coding.isNull()) w.set(desc, "OMFI:DIDD:EssenceCompression", test.coding);
				BentoFile b;
				QVERIFY2(b.load(w.build()), test.name);
				MxfMetadata meta;
				bool known = false;
				QVERIFY(OmfObjects::readDescriptor(b, OmfObjects::Props(b), mob, desc, {}, meta, &known));
				QVERIFY(meta.valid);
				QVERIFY2((meta.codec == QStringLiteral("Uncompressed alpha")) == test.alpha, test.name);
				if (test.alpha)
				{
					QVERIFY(known);
					QCOMPARE(meta.bitDepth, QStringLiteral("8-bit"));
					QCOMPARE(meta.fps, QStringLiteral("24"));
				}
			}
}

QTEST_APPLESS_MAIN(TestMdbParser)
#include "tst_mdbparser.moc"
