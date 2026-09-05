// OmfParser: one OMF-era essence file read tail-first. Every pin below is
// against the real fixtures in fixtures/omf — the 80 slates Media Composer
// ships and the two audio files MC 26.8 wrote — joined through their
// version-2 PMRs to the msmMMOB.mdb rows for the same files, which must
// say the same thing. The expected strings were produced by the parser
// once and checked against the rules named beside them before pinning.

#include "mdbparser.h"
#include "mxfparser.h"
#include "omfparser.h"
#include "omfuid.h"
#include "pmrparser.h"
#include "testbento.h"
#include "testomf.h"
#include "testutil.h"

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	constexpr qint64 kTailBudget = 64 * 1024; ///< parseHeader must read less than this from any file.

	QString fx(const char *rel)
	{
		return QStringLiteral(FIXTURES_DIR "/") + QLatin1String(rel);
	}

	QStringList slates()
	{
		return QDir(fx("omf/avid_supporting")).entryList({QStringLiteral("*.omf")}, QDir::Files, QDir::Name);
	}
} // namespace

class TestOmfParser : public QObject
{
	Q_OBJECT
private slots:
	// Every one of the 80 slates
	void omf_every_slate_parses_with_a_named_codec();
	void omf_pmr_pairs_name_the_file_and_master_mobs();
	void omf_mdb_row_agrees_with_the_file();

	// One file per resolution id
	void omf_video_facts_by_resolution_id();

	// The three mobs' attributes
	void omf_attributes_come_from_master_file_and_source_mobs();

	// MC 2026 audio
	void omf_audio_files_describe_the_tones();

	// Gates and parity
	void omf_non_bento_input_fails_cleanly();
	void omf_container_without_a_media_descriptor_is_not_valid();
	void omf_finalise_parity_with_a_header();
	void sdii_omf1_and_omf2_semantics();
	void ambiguous_master_is_not_guessed();
	void unreadable_descriptor_is_not_media();
	void multiple_embedded_files_are_not_collapsed();
	void avid_legacy_version_alias_data();
	void avid_legacy_version_alias();
	void avid_legacy_version_excludes_compositions();
};

void TestOmfParser::avid_legacy_version_alias_data()
{
	QTest::addColumn<bool>("compact");
	QTest::addColumn<bool>("big");
	QTest::addColumn<bool>("modernHead");
	QTest::addColumn<QByteArray>("version");
	QTest::addColumn<int>("expected");
	for (bool compact : {false, true})
		for (bool big : {false, true})
		{
			const QByteArray suffix = QByteArray(compact ? "-bento2" : "-bento1") + (big ? "-MM" : "-II");
			QTest::newRow(("avid-0001" + suffix).constData())
				<< compact << big << false << QByteArray::fromHex("0001") << int(OmfObjects::Revision::Omf1);
			QTest::newRow(("standard-0100" + suffix).constData())
				<< compact << big << false << QByteArray::fromHex("0100") << int(OmfObjects::Revision::Omf1);
			QTest::newRow(("standard-0200" + suffix).constData())
				<< compact << big << true << QByteArray::fromHex("0200") << int(OmfObjects::Revision::Omf2);
		}
	for (const QByteArray &raw : {QByteArray(), QByteArray::fromHex("00"), QByteArray::fromHex("0000"),
		QByteArray::fromHex("0002"), QByteArray::fromHex("0300"), QByteArray::fromHex("000100")})
		QTest::newRow(("unknown-" + raw.toHex()).constData())
			<< false << false << false << raw << int(OmfObjects::Revision::Unknown);
	QTest::newRow("alias-does-not-apply-to-omf2-head")
		<< true << false << true << QByteArray::fromHex("0001") << int(OmfObjects::Revision::Unknown);
}

void TestOmfParser::avid_legacy_version_alias()
{
	QFETCH(bool, compact);
	QFETCH(bool, big);
	QFETCH(bool, modernHead);
	QFETCH(QByteArray, version);
	QFETCH(int, expected);
	TestOmf::Writer w(compact, big);
	w.setImmediate(1, modernHead ? "OMFI:OOBJ:ObjClass" : "OMFI:ObjID", QByteArray("HEAD", 4));
	w.setImmediate(1, modernHead ? "OMFI:HEAD:Version" : "OMFI:Version", version);
	w.setImmediate(1, modernHead ? "OMFI:HEAD:ByteOrder" : "OMFI:ByteOrder", QByteArray(big ? "MM" : "II", 2));
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const QString path = temp.filePath("revision.omf");
	QVERIFY(tryWriteFile(path, w.build()));
	BentoFile b;
	QVERIFY(b.open(path));
	QCOMPARE(int(OmfObjects::revision(b)), expected);
	QCOMPARE(b.containerVersion(), compact ? 2 : 1);
	QCOMPARE(b.isBigEndian(), big);
}

void TestOmfParser::avid_legacy_version_excludes_compositions()
{
	BentoBuilder w;
	w.setImmediate(1, "OMFI:ObjID", QByteArray("HEAD", 4));
	w.setImmediate(1, "OMFI:Version", QByteArray::fromHex("0001"));
	for (quint32 usage : {0u, 1u, 7u, 99u})
	{
		const quint32 mob = w.addObject("MOBJ");
		w.set(mob, "OMFI:MOBJ:MobID", TestOmf::uid(usage + 1));
		w.setU32(mob, "OMFI:MOBJ:UsageCode", usage);
	}
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const QString path = temp.filePath("roles.mdb");
	QVERIFY(tryWriteFile(path, w.build()));
	const MdbDatabase db = MdbParser::load(path);
	QCOMPARE(db.revision, OmfObjects::Revision::Omf1);
	QCOMPARE(db.masters.size(), 2);
	QVERIFY(db.masters.contains(OmfUid::canonicalHex(TestOmf::uid(2)))); // usage1 precompute
	QVERIFY(db.masters.contains(OmfUid::canonicalHex(TestOmf::uid(8)))); // usage7 master
	QVERIFY(!db.masters.contains(OmfUid::canonicalHex(TestOmf::uid(1)))); // usage0 composition
	QVERIFY(!db.masters.contains(OmfUid::canonicalHex(TestOmf::uid(100))));
}

void TestOmfParser::sdii_omf1_and_omf2_semantics()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	for (int variant : {0, 1, 2, 3})
	{
		const bool omf2 = variant != 0, compact = variant >= 2, big = variant == 3;
		const QString path = temp.filePath(omf2 ? "sdii-2.omf" : "sdii-1.omf");
		QFile f(path);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write(TestOmf::sdii(omf2, false, false, compact, big));
		f.close();
		const OmfMetadata m = OmfParser::parseHeader(path);
		QCOMPARE(m.revision, omf2 ? OmfObjects::Revision::Omf2 : OmfObjects::Revision::Omf1);
		QVERIFY(m.essence.valid);
		QVERIFY(m.essence.isAudio);
		QCOMPARE(m.essence.codec, QStringLiteral("SDII"));
		QCOMPARE(m.essence.sampleRate, 48000);
		QCOMPARE(m.essence.channels, 2);
		QCOMPARE(m.essence.bitDepth, QStringLiteral("24-bit"));
		QCOMPARE(m.essence.durationFrames, qint64(50));
		QCOMPARE(m.essence.clipName, QStringLiteral("SDII clip"));
		QCOMPARE(m.essence.projectName, QStringLiteral("SDII project"));
		QCOMPARE(m.essence.sourceFilePath, QStringLiteral("C:\\Original\\session.sd2"));
		QCOMPARE(m.essence.classificationKnown, !omf2); // no Avid UsageCode in standard OMF2
		QCOMPARE(m.essence.umid, OmfUid::canonicalHex(TestOmf::uid(1)));
		QCOMPARE(m.fileMobId, OmfUid::canonicalHex(TestOmf::uid(2)));
		QCOMPARE(m.startTimecode, omf2 ? qint64(0x10000002aULL) : qint64(90000));
		QCOMPARE(m.timecodeFps, 25);
	}
}

void TestOmfParser::ambiguous_master_is_not_guessed()
{
	QTemporaryDir temp;
	const QString path = temp.filePath("ambiguous.omf");
	QFile f(path);
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(TestOmf::sdii(true, true));
	f.close();
	const auto m = OmfParser::parseHeader(path);
	QVERIFY(m.essence.valid);
	QVERIFY(m.essence.umid.isEmpty());
	QVERIFY(m.essence.clipName.isEmpty());
}

void TestOmfParser::unreadable_descriptor_is_not_media()
{
	QTemporaryDir temp;
	const QString path = temp.filePath("bad.omf");
	QFile f(path);
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(TestOmf::sdii(true, false, true));
	f.close();
	QVERIFY(!OmfParser::parseHeader(path).essence.valid);
}

void TestOmfParser::multiple_embedded_files_are_not_collapsed()
{
	BentoBuilder w;
	for (quint32 id : {1u, 2u})
	{
		const auto mob = w.addObject("MOBJ"), desc = w.addObject("SD2D");
		w.set(mob, "OMFI:MOBJ:MobID", TestOmf::uid(id));
		w.setHandle(mob, "OMFI:MOBJ:PhysicalMedia", desc);
		w.setRational(desc, "OMFI:MDFL:SampleRate", 48000, 1);
	}
	QTemporaryDir temp;
	const QString path = writeFileIn(temp.path(), QStringLiteral("multiple.omf"), w.build());
	QVERIFY(!OmfParser::parseHeader(path).essence.valid);
}

// MARK: - Every one of the 80 slates

void TestOmfParser::omf_every_slate_parses_with_a_named_codec()
{
	const QDir dir(fx("omf/avid_supporting"));
	const QStringList names = slates();
	QCOMPARE(names.size(), 80);
	for (const QString &name : names)
	{
		const QByteArray fnBytes = name.toUtf8();
		const char *fn = fnBytes.constData();
		qint64 read = 0;
		const OmfMetadata m = OmfParser::parseHeader(dir.filePath(name), &read);
		QCOMPARE(m.revision, OmfObjects::Revision::Omf1);
		const MxfMetadata &e = m.essence;
		QVERIFY2(e.valid, fn);
		QVERIFY2(!e.isAudio, fn);
		QVERIFY2(!e.codec.isEmpty(), fn);
		QVERIFY2(!e.codec.startsWith(QLatin1String("Unknown")), qPrintable(name + QStringLiteral(": ") + e.codec));
		QVERIFY2(!e.codec.contains(QLatin1String("unknown variant")), qPrintable(name + QStringLiteral(": ") + e.codec));
		QVERIFY2(!e.resolution.isEmpty(), fn);
		QVERIFY2(!e.fps.isEmpty(), fn);
		// Every slate is one frame of 8-bit video.
		QCOMPARE(e.durationFrames, qint64(1));
		QCOMPARE(e.bitDepth, QStringLiteral("8-bit"));
		// Identity: the master's name is the clip name, and it is the
		// material name (a rung of the clip-name ladder), never a fallback.
		QVERIFY2(e.clipNameFromMaterial, fn);
		QVERIFY2(!e.clipName.isEmpty(), fn);
		QVERIFY2(!e.projectName.isEmpty(), fn);
		// Every slate was imported from a still: 65 say so with _SRCFILE,
		// the oldest 15 only through the source mob's MDES locator.
		QVERIFY2(!e.sourceFilePath.isEmpty(), fn);
		QVERIFY2(!e.isPrecompute, fn);
		// Two distinct wrapped ids: the master (umid) and the file mob.
		QVERIFY2(OmfUid::isOmfForm(e.umid), qPrintable(name + QStringLiteral(": ") + e.umid));
		QVERIFY2(OmfUid::isOmfForm(m.fileMobId), qPrintable(name + QStringLiteral(": ") + m.fileMobId));
		QVERIFY2(m.fileMobId != e.umid, fn);
		// Tail-first: the essence is never materialised.
		QVERIFY2(read > 0 && read < kTailBudget, qPrintable(name + QStringLiteral(": read ") + QString::number(read)));
	}
}

void TestOmfParser::omf_pmr_pairs_name_the_file_and_master_mobs()
{
	struct Folder
	{
		const char *dir;
		int pairs;
	};
	const Folder kFolders[] = {{"omf/avid_supporting", 80}, {"omf/mc2026_audio", 2}};
	for (const Folder &folder : kFolders)
	{
		const QDir dir(fx(folder.dir));
		bool ok = false;
		const QVector<PmrEntry> pmr = PmrParser::parse(dir.filePath(QStringLiteral("msmFMID.pmr")), &ok);
		QVERIFY2(ok, folder.dir);
		QCOMPARE(pmr.size(), folder.pairs);
		for (const PmrEntry &entry : pmr)
		{
			const QByteArray fnBytes = entry.fileName.toUtf8();
			const char *fn = fnBytes.constData();
			QVERIFY2(dir.exists(entry.fileName), fn);
			const OmfMetadata m = OmfParser::parseHeader(dir.filePath(entry.fileName));
			QVERIFY2(m.essence.valid, fn);
			// The file's own ids are exactly what the version-2 PMR pairs.
			QVERIFY2(m.fileMobId == entry.mobId, qPrintable(entry.fileName + QStringLiteral(": ") + m.fileMobId));
			QVERIFY2(m.essence.umid == entry.masterMobId,
					 qPrintable(entry.fileName + QStringLiteral(": ") + m.essence.umid));
		}
	}
}

// The twin of tst_mdbparser's header comparison: the database MC 26.8
// regenerated beside the slates must describe each file the way the file
// describes itself. Codec names agree because OmfResolutions aliases the
// 4CC spellings the database rewrote (MXF1 for AUNC/DV-C, 2402 for 2502).
void TestOmfParser::omf_mdb_row_agrees_with_the_file()
{
	struct Folder
	{
		const char *dir;
		int pairs;
		bool binInFile; ///< The 2021 slates carry _ORG_BIN in the file; MC 2026 writes it to the MDB only.
	};
	const Folder kFolders[] = {{"omf/avid_supporting", 80, true}, {"omf/mc2026_audio", 2, false}};
	for (const Folder &folder : kFolders)
	{
		const QDir dir(fx(folder.dir));
		bool ok = false;
		const MdbDatabase db = MdbParser::load(dir.filePath(QStringLiteral("msmMMOB.mdb")), &ok);
		QVERIFY2(ok, folder.dir);
		const QVector<PmrEntry> pmr = PmrParser::parse(dir.filePath(QStringLiteral("msmFMID.pmr")), &ok);
		QVERIFY2(ok, folder.dir);
		QCOMPARE(pmr.size(), folder.pairs);
		for (const PmrEntry &entry : pmr)
		{
			const QByteArray fnBytes = entry.fileName.toUtf8();
			const char *fn = fnBytes.constData();
			QVERIFY2(db.files.contains(entry.mobId), fn);
			QVERIFY2(db.masters.contains(entry.masterMobId), fn);
			const MdbFileMob &row = db.files[entry.mobId];
			const MdbMasterMob &master = db.masters[entry.masterMobId];
			const OmfMetadata m = OmfParser::parseHeader(dir.filePath(entry.fileName));
			const MxfMetadata &e = m.essence;
			QVERIFY2(row.essenceComplete, fn);
			QVERIFY2(e.valid, fn);
			QCOMPARE(e.isAudio, row.essence.isAudio);
			QVERIFY2(e.codec == row.essence.codec,
					 qPrintable(entry.fileName + QStringLiteral(": file ") + e.codec + QStringLiteral(" mdb ") +
								row.essence.codec));
			QCOMPARE(e.resolution, row.essence.resolution);
			QCOMPARE(e.fps, row.essence.fps);
			QCOMPARE(e.durationFrames, row.essence.durationFrames);
			QCOMPARE(e.bitDepth, row.essence.bitDepth);
			QCOMPARE(e.sampleRate, row.essence.sampleRate);
			QCOMPARE(e.channels, row.essence.channels);
			QCOMPARE(e.timecodeBase, row.essence.timecodeBase);
			QCOMPARE(e.dropFrame, row.essence.dropFrame);
			QCOMPARE(e.projectName, row.project);
			QCOMPARE(e.clipName, master.clipName);
			if (folder.binInFile)
				QCOMPARE(m.bin, master.bin);
			else
			{
				// Verified from the bytes: the tone files hold no MCBR object at
				// all, so the bin is the database's alone for MC 2026 media.
				QVERIFY2(m.bin.isEmpty(), fn);
				QVERIFY2(!master.bin.isEmpty(), fn);
			}
			QCOMPARE(e.hasImportSetting, master.isImported);
			QCOMPARE(e.isPrecompute, master.usageCode == 1);
		}
	}
}

// MARK: - One file per resolution id

// Rules each value follows (see omfresolutions.cpp and MxfParser::finalise):
//   codec       bare Avid short name from OmfResolutions for the (id, 4CC)
//               pair; DNxHD-era ids (1235..1489) through OmfObjects::ulFromResId
//               and the app's kEntries/kDnxTiers names; any name starting
//               "DV " gets finalise's " i(PAL)" / " p(NTSC)" suffix unless it
//               already ends in a digit + i/p ("DV 100 1080i", "DV 100 720p")
//               or lacks the space ("DV25P 411", the .vr's own spelling).
//   resolution  StoredWidth x StoredHeight, the height DOUBLED for layout 1
//               (separate fields) and reported AS STORED for layout 2
//               (single field) and layout 0 (full frame).
//   fps         OMFI:MDFL:SampleRate through applyEditRate's speed matcher:
//               2997/100 → 29.97, 23976/1000 → 23.976, 59940/1000 → 59.94.
void TestOmfParser::omf_video_facts_by_resolution_id()
{
	struct Pin
	{
		const char *file;
		const char *codec;
		const char *resolution;
		const char *fps;
		int frameLayout;
	};
	const Pin kPins[] = {
		// JFIF: 78 and 110 are single-field (layout 2) rasters shown as stored
		{"BLACK_352x243x1_JFIF12S.omf", "15:1s", "352x248", "29.97", 2},		 // 78  JFIF, 352x248 layout 2
		{"BLACK_720x243x2_JFIF35.omf", "20:1", "720x496", "29.97", 1},		 // 82  JFIF, 720x248 layout 1 → doubled
		{"BLACK_720x486x1_JFIF25P.omf", "28:1", "720x496", "24", 0},			 // 104 JFIF, 720x496 layout 0
		{"BLACK_288x243x1_JFIF15m.omf", "10:1m", "288x248", "29.97", 2},		 // 110 JFIF, 288x248 layout 2
		{"BLACK_288x288x1_JFIF20mP.omf", "8:1m", "288x296", "24", 0},		 // 112 JFIF, 288x296 layout 0
		// DV: finalise's i/p(PAL/NTSC) suffix, from layout + fps/height
		{"BLACK_720x480x1_DV411.omf", "DV 25 411 i(NTSC)", "720x480", "29.97", 1}, // 140 DV/C, 720x240 layout 1, NTSC
		{"BLACK_720x576x1_DV420.omf", "DV 25 420 i(PAL)", "720x576", "25", 1},	 // 141 DV/C, 720x288 layout 1, PAL
		{"BLACK_720x480x1_DV50.omf", "DV 50 i(NTSC)", "720x480", "29.97", 1},	 // 142 DV/C, 720x240 layout 1, NTSC
		{"BLACK_720x480x1_DV411P.omf", "DV25P 411", "720x480", "24", 0},		 // 143 DV/C, no space → no suffix
		{"BLACK_720x576x1_DV420P.omf", "DV 25P 420 p(PAL)", "720x576", "25", 0}, // 144 DV/C, layout 0 → p, PAL
		// Uncompressed and MPEG 50
		{"BLACK_720x243x2_UNCOMP.omf", "1:1", "720x496", "29.97", 1},			// 151 AUNC, 720x248 layout 1
		{"BLACK_720x576x1_UNCOMP_24P.omf", "1:1", "720x592", "24", 0},			// 152 AUNC, 720x592 layout 0
		{"BLACK_720x576x1_MPEG50.omf", "MPEG 50", "720x608", "25", 1},			// 160 MPG2, 720x304 layout 1
		// DNxHD-era ids: the MXF path's names for the rebuilt label
		{"BLACK_1920x1080x1_DNxHD_115.omf", "Avid DNx SQ (DNxHD 115)", "1920x1080", "23.976", 0}, // 1237 at 23.976
		{"BLACK_1920x540x2_AVHD_145.omf", "Avid DNx SQ (DNxHD 145)", "1920x1080", "29.97", 1},	// 1242 at 29.97
		{"BLACK_1920x540x2_AVHD_220.omf", "Avid DNx HQ (DNxHD 220)", "1920x1080", "29.97", 1},	// 1243 at 29.97
		{"BLACK_1440x540x2_DNxHD.omf", "Avid DNx TR", "1920x1080", "29.97", 1},					// 1244 (the 0x0D spelling)
		{"BLACK_1280x720x1_DNxHD_145.omf", "Avid DNx SQ (DNxHD 145)", "1280x720", "59.94", 0}, // 2012 DNxHD whitepaper: 720p SQ at 59.94
		// DV 100 (UNVERIFIED names; already qualified, so no suffix)
		{"BLACK_1920x540x2_DV100_115.omf", "DV 100 1080i", "1920x1080", "25", 1}, // 2500 DV/C, 1920x540 layout 1
		{"BLACK_1280x720x1_DV100_90.omf", "DV 100 720p", "1280x720", "59.94", 0}, // 2502 DV/C, 1280x720 layout 0
	};
	const QDir dir(fx("omf/avid_supporting"));
	for (const Pin &pin : kPins)
	{
		const QString name = QLatin1String(pin.file);
		const OmfMetadata m = OmfParser::parseHeader(dir.filePath(name));
		const MxfMetadata &e = m.essence;
		QVERIFY2(e.valid, pin.file);
		QVERIFY2(e.codec == QLatin1String(pin.codec), qPrintable(name + QStringLiteral(": codec ") + e.codec));
		QVERIFY2(e.resolution == QLatin1String(pin.resolution),
				 qPrintable(name + QStringLiteral(": res ") + e.resolution));
		QVERIFY2(e.fps == QLatin1String(pin.fps), qPrintable(name + QStringLiteral(": fps ") + e.fps));
		QCOMPARE(e.frameLayout, pin.frameLayout);
		QVERIFY2(e.heightIsFrameHeight, pin.file);
	}
}

// MARK: - The three mobs' attributes

// The master carries the clip name, the bin and the import setting; the
// file mob its own locator; the source mob the 2021 slates' _PJ. Each
// value was read from the bytes before pinning.
void TestOmfParser::omf_attributes_come_from_master_file_and_source_mobs()
{
	const QDir dir(fx("omf/avid_supporting"));

	// A slide with a bin on the master and the project on the source mob.
	const OmfMetadata jfif35 = OmfParser::parseHeader(dir.filePath(QStringLiteral("BLACK_720x243x2_JFIF35.omf")));
	QVERIFY(jfif35.essence.valid);
	QCOMPARE(jfif35.essence.clipName, QStringLiteral("Black 720x486.PICT"));
	QVERIFY(jfif35.essence.clipNameFromMaterial);
	QCOMPARE(jfif35.bin, QStringLiteral("NTSC slides"));
	QCOMPARE(jfif35.essence.projectName, QStringLiteral("NTSC slides"));
	// No _SRCFILE anywhere in this 2001-era slate: the import path is the
	// WINL locator on the source mob's MDES (object 68019 → WINL 68018),
	// which is a locator, not an import setting.
	QVERIFY(!jfif35.essence.hasImportSetting);
	QCOMPARE(jfif35.essence.sourceFilePath,
			 QStringLiteral("C:\\WINNT\\Profiles\\dhoag\\DESKTOP\\Avid Media Slides\\720wide\\Black 720x486.PICT"));
	QCOMPARE(jfif35.timecodeFps, 30);
	QCOMPARE(jfif35.startTimecode, qint64(108000)); // 01:00:00:00 at 30
	QVERIFY(!jfif35.essence.dropFrame);
	QCOMPARE(jfif35.essence.umid,
			 QStringLiteral("060a2b3401010101.01010f0013000000.129450353b599b28.060e2b347f7f2a80"));
	QCOMPARE(jfif35.fileMobId,
			 QStringLiteral("060a2b3401010101.01010f0013000000.129450353c599b28.060e2b347f7f2a80"));

	// A Windows import: _IMPORTSETTING/_SRCFILE is a WINL locator on the
	// master, _MEDIAFILE a WINL on the file mob, _PJ on the source mob.
	const OmfMetadata dnx = OmfParser::parseHeader(dir.filePath(QStringLiteral("BLACK_1920x1080x1_DNxHD_115.omf")));
	QVERIFY(dnx.essence.valid);
	QCOMPARE(dnx.essence.clipName, QStringLiteral("Black 1920 x 1080.psd"));
	QVERIFY(dnx.essence.hasImportSetting);
	QCOMPARE(dnx.essence.sourceFilePath,
			 QStringLiteral("C//Documents and Settings/eblair/Desktop/Black 1920 x 1080.psd"));
	QCOMPARE(dnx.mediaFilePath, QStringLiteral("C//OMFI MediaFiles/Black 1920 x 1080.p412CADC5.omf"));
	QCOMPARE(dnx.essence.projectName, QStringLiteral("1080p 23.976"));
	QVERIFY(dnx.bin.isEmpty()); // no _ORG_BIN on this master
	QCOMPARE(dnx.timecodeFps, 24);
	QCOMPARE(dnx.startTimecode, qint64(86400)); // 01:00:00:00 at 24
	QVERIFY(!dnx.essence.dropFrame);

	// Drop frame comes from the same TCCP: an NTSC DV slate with Flags = 1.
	const OmfMetadata dv411 = OmfParser::parseHeader(dir.filePath(QStringLiteral("BLACK_720x480x1_DV411.omf")));
	QVERIFY(dv411.essence.valid);
	QVERIFY(dv411.essence.dropFrame);
	QCOMPARE(dv411.startTimecode, qint64(900));
	QCOMPARE(dv411.timecodeFps, 30);
}

// MARK: - MC 2026 audio

// Two one-minute tones MC 26.8 wrote fresh: WAVE and AIFF-C, 48 kHz,
// 24-bit, mono, 25 fps, in project zTeßt_PAL_25p. The essence file carries
// no _ORG_BIN (the MDB is the only source of the bin), and MC 2026 keeps
// _PJ on the file mob.
void TestOmfParser::omf_audio_files_describe_the_tones()
{
	struct Pin
	{
		const char *file;
		const char *clipName;
		const char *umid;
		const char *fileMobId;
	};
	const Pin kPins[] = {
		{"TONE_100A01.6A972974.039700.wav", "TONE: 1000 Hz @ -14.0 dB.1",
		 "060a2b3401010101.01010f0013000000.7429976a4e397047.060e2b347f7f2a80",
		 "060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"},
		{"TONE_100A01.6A972997.0C53E0.aif", "TONE: 1000 Hz @ -20.0 dB.2",
		 "060a2b3401010101.01010f0013000000.9729976a3dc57047.060e2b347f7f2a80",
		 "060a2b3401010101.01010f0013000000.9729976a3ec57047.060e2b347f7f2a80"},
	};
	const QDir dir(fx("omf/mc2026_audio"));
	const QString project = QString::fromUtf8("zTe\xc3\x9ft_PAL_25p");
	for (const Pin &pin : kPins)
	{
		qint64 read = 0;
		const OmfMetadata m = OmfParser::parseHeader(dir.filePath(QLatin1String(pin.file)), &read);
		QCOMPARE(m.revision, OmfObjects::Revision::Omf1);
		const MxfMetadata &e = m.essence;
		QVERIFY2(e.valid, pin.file);
		QVERIFY2(e.isAudio, pin.file);
		QCOMPARE(e.sampleRate, 48000);
		QCOMPARE(e.channels, 1);
		QCOMPARE(e.bitDepth, QStringLiteral("24-bit"));
		QCOMPARE(e.durationFrames, qint64(1500)); // 2,880,002 samples × 25 ÷ 48000
		QCOMPARE(e.timecodeBase, 25);
		// OMF-era: Avid labels legacy audio by container (its format menus:
		// "WAVE (OMF)", "AIFF-C (OMF)"), never "PCM" — that name stays with
		// MXF-era audio.
		const QString avidLabel = QString::fromLatin1(pin.file).endsWith(QLatin1String(".wav"))
									  ? QStringLiteral("WAVE (OMF)")
									  : QStringLiteral("AIFF-C (OMF)");
		QCOMPARE(e.codec, avidLabel);
		QVERIFY2(e.resolution.isEmpty(), pin.file);
		QVERIFY2(e.fps.isEmpty(), pin.file);
		QCOMPARE(e.clipName, QString::fromLatin1(pin.clipName));
		QVERIFY2(e.clipNameFromMaterial, pin.file);
		QCOMPARE(e.projectName, project);
		QVERIFY2(m.bin.isEmpty(), pin.file);
		QVERIFY2(!e.hasImportSetting, pin.file);
		QVERIFY2(!e.isPrecompute, pin.file);
		QCOMPARE(e.umid, QLatin1String(pin.umid));
		QCOMPARE(m.fileMobId, QLatin1String(pin.fileMobId));
		// The TCCP sits on the 32-byte physical mob the file mob's SCLP names.
		QCOMPARE(m.timecodeFps, 25);
		QCOMPARE(m.startTimecode, qint64(90000)); // 01:00:00:00 at 25
		QVERIFY2(!e.dropFrame, pin.file);
		// 8.7 MB of essence, read as a few KB of tail.
		QVERIFY2(read > 0 && read < kTailBudget, qPrintable(QString::number(read)));
	}
}

// MARK: - Gates and parity

void TestOmfParser::omf_non_bento_input_fails_cleanly()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// A plain RIFF stub — what a non-Avid .wav in an OMF folder looks like.
	// The reader checks a possible RIFF omfi chunk as well as the tail.
	// It must skip the data payload instead of reading the audio samples.
	QByteArray riff("RIFF");
	riff.append(BentoBuilder::le32(1024 - 8));
	riff.append("WAVEfmt ");
	riff.append(BentoBuilder::le32(16));
	riff.append(QByteArray(16, '\0'));
	riff.append("data");
	riff.append(BentoBuilder::le32(1024 - 44));
	riff.append(QByteArray(1024 - riff.size(), '\0'));
	const QString stub = writeFileIn(tmp.path(), QStringLiteral("plain.wav"), riff);
	QVERIFY(!stub.isEmpty());
	qint64 read = -1;
	const OmfMetadata m = OmfParser::parseHeader(stub, &read);
	QVERIFY(!m.essence.valid);
	QVERIFY(m.fileMobId.isEmpty());
	QVERIFY(m.essence.umid.isEmpty());
	QVERIFY(read > 0 && read < 128);

	// A missing file reads nothing at all.
	read = -1;
	QVERIFY(!OmfParser::parseHeader(tmp.path() + QStringLiteral("/missing.omf"), &read).essence.valid);
	QCOMPARE(read, qint64(0));

	// Too small for a label.
	read = -1;
	const QString tiny = writeFileIn(tmp.path(), QStringLiteral("tiny.omf"), QByteArray(10, 'x'));
	QVERIFY(!OmfParser::parseHeader(tiny, &read).essence.valid);
	QCOMPARE(read, qint64(0));
}

void TestOmfParser::omf_container_without_a_media_descriptor_is_not_valid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// A Bento container with no mobs at all.
	{
		BentoBuilder w;
		const quint32 head = w.addObject("HEAD");
		w.setString(head, "OMFI:HEAD:Version", "2.1");
		const QString path = writeFileIn(tmp.path(), QStringLiteral("nomobs.omf"), w.build());
		QVERIFY(!path.isEmpty());
		const OmfMetadata m = OmfParser::parseHeader(path);
		QVERIFY(!m.essence.valid);
		QVERIFY(m.fileMobId.isEmpty());
	}

	// A master mob alone: identity is read, essence stays invalid.
	{
		BentoBuilder w;
		const quint32 master = w.addObject("MOBJ");
		w.set(master, "OMFI:MOBJ:MobID", QByteArray::fromHex("2a000000") + QByteArray::fromHex("0102030405060708"));
		w.setString(master, "OMFI:CPNT:Name", "Lonely master");
		w.setU32(master, "OMFI:MOBJ:UsageCode", 7);
		const QString path = writeFileIn(tmp.path(), QStringLiteral("masteronly.omf"), w.build());
		QVERIFY(!path.isEmpty());
		const OmfMetadata m = OmfParser::parseHeader(path);
		QVERIFY(!m.essence.valid);
		QVERIFY(m.essence.clipName.isEmpty()); // no file mob, so nothing beyond the gate is read
		QVERIFY(m.fileMobId.isEmpty());
	}
}

// The parity check tst_mxfparser makes for the MDB producer, for the OMF
// producer: the facts a header would present (a FIELD height, no flag)
// finalise to exactly what the file reader reports.
void TestOmfParser::omf_finalise_parity_with_a_header()
{
	const QDir dir(fx("omf/avid_supporting"));

	// A table-named codec (no label) on a layout-1 raster.
	{
		const OmfMetadata m = OmfParser::parseHeader(dir.filePath(QStringLiteral("BLACK_720x243x2_JFIF35.omf")));
		QVERIFY(m.essence.valid);
		QVERIFY(m.essence.essenceContainerLabel.isEmpty());
		QVERIFY(m.essence.heightIsFrameHeight);

		MxfMetadata hdr;
		hdr.codec = QStringLiteral("20:1"); // pre-filled, as readDescriptor does before finalise
		hdr.width = 720;
		hdr.height = 248; // the stored field height
		hdr.frameLayout = 1;
		MxfParser::applyEditRate(hdr, 2997, 100);
		MxfParser::finalise(hdr);
		QCOMPARE(hdr.valid, m.essence.valid);
		QCOMPARE(hdr.resolution, m.essence.resolution);
		QCOMPARE(hdr.codec, m.essence.codec);
		QCOMPARE(hdr.fps, m.essence.fps);
	}

	// A DNxHD label rebuilt from the resolution id, and the DV suffix rule.
	{
		const OmfMetadata m = OmfParser::parseHeader(dir.filePath(QStringLiteral("BLACK_1920x540x2_AVHD_220.omf")));
		QVERIFY(m.essence.valid);
		QVERIFY(!m.essence.essenceContainerLabel.isEmpty());

		MxfMetadata hdr;
		hdr.essenceContainerLabel = m.essence.essenceContainerLabel;
		hdr.width = 1920;
		hdr.height = 540;
		hdr.frameLayout = 1;
		MxfParser::applyEditRate(hdr, 2997, 100);
		MxfParser::finalise(hdr);
		QCOMPARE(hdr.resolution, m.essence.resolution);
		QCOMPARE(hdr.codec, m.essence.codec);
		QCOMPARE(hdr.codec, QStringLiteral("Avid DNx HQ (DNxHD 220)"));
	}
	{
		const OmfMetadata m = OmfParser::parseHeader(dir.filePath(QStringLiteral("BLACK_720x576x1_DV420.omf")));
		QVERIFY(m.essence.valid);

		MxfMetadata hdr;
		hdr.codec = QStringLiteral("DV 25 420");
		hdr.width = 720;
		hdr.height = 288;
		hdr.frameLayout = 1;
		MxfParser::applyEditRate(hdr, 25, 1);
		MxfParser::finalise(hdr);
		QCOMPARE(hdr.codec, m.essence.codec);
		QCOMPARE(hdr.codec, QStringLiteral("DV 25 420 i(PAL)"));
	}
}

QTEST_APPLESS_MAIN(TestOmfParser)
#include "tst_omfparser.moc"
