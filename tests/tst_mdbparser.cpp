// Unit tests for MdbParser::parse over hand-crafted msmMMOB.mdb buffers.
//
// The MDB is Avid's own flat object stream, not Bento and not AAF. A named
// object ends with a self-checking header closed by its MOB ID:
//
//   <name>\0 <handle> <u16 count> <count x handle> <MOB:32>
//
// and the object's attributes are written BEFORE it, each as
//
//   <value>\0 [<value-utf8>\0] <_KEY>\0 <handle>
//
// The builders below emit exactly that, so these buffers are the same shape as
// a real database rather than a guess at one. (They used to lay out a "Name"
// marker next to its value — a layout that only ever occurs in the property
// dictionary at the very front of a real file, which is why the old parser
// scored zero clip names on a real 360-file folder.)

#include "mdbparser.h"
#include "mobid.h"
#include "mxfparser.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	// A 32-byte Avid MOB: the 06 0A 2B 34 signature the parser scans for, then
	// `tag` and zeros (kept non-printable so they don't leak into any string
	// scan). Vary `tag` for a second, distinct MOB.
	QByteArray mob32(char tag = '\0')
	{
		return QByteArray::fromHex("060a2b34") + QByteArray(1, tag) + QByteArray(27, '\0');
	}

	// An 8-byte object handle: a little-endian u32 whose high word is 1, then a
	// u32 zero. Ids stay below 0x2000 so neither id byte is printable and the
	// handle can't be mistaken for a string by the value scan.
	QByteArray handle(quint16 id)
	{
		QByteArray b;
		b.append(char(id & 0xFF));
		b.append(char(id >> 8));
		b.append('\x01');
		b.append(QByteArray(5, '\0'));
		return b;
	}

	// One attribute: the value first, then the key that names it. Avid writes
	// non-ASCII values twice (MacRoman then UTF-8); pass `macRomanTwin` to
	// reproduce that.
	void addAttribute(QByteArray &b, const QByteArray &value, const QByteArray &key,
					  quint16 id = 0x0300, const QByteArray &macRomanTwin = {})
	{
		if (!macRomanTwin.isEmpty())
		{
			b.append(macRomanTwin);
			b.append('\0');
		}
		if (!value.isEmpty())
		{
			b.append(value);
			b.append('\0');
		}
		b.append(key);
		b.append('\0');
		b.append(handle(id));
	}

	// A bare token with no key of its own — how the container hint is written.
	void addToken(QByteArray &b, const QByteArray &token)
	{
		b.append(token);
		b.append('\0');
	}

	// The header that closes a named object. `declaredCount` defaults to the
	// number of members actually written; pass a different value to build the
	// mismatch the framing check is supposed to reject.
	void addRecord(QByteArray &b, const QByteArray &name, const QByteArray &mob, int members = 1,
				   int declaredCount = -1)
	{
		if (declaredCount < 0)
			declaredCount = members;
		b.append(name);
		b.append('\0');
		b.append(handle(0x0100));
		b.append(char(declaredCount & 0xFF));
		b.append(char((declaredCount >> 8) & 0xFF));
		for (int i = 0; i < members; ++i)
			b.append(handle(quint16(0x0200 + i)));
		b.append(mob);
	}

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
} // namespace

class TestMdbParser : public QObject
{
	Q_OBJECT
private slots:
	void extracts_full_record_from_one_object();
	void buildMobMap_keys_by_hex();
	void no_mob_yields_no_records();
	void too_small_file_returns_empty();

	// The header is self-checking: the member count must match the handles
	// that follow it AND the run must land exactly on the MOB. Without both,
	// any 8 handle-shaped bytes before a MOB would invent a record.
	void wrong_member_count_is_not_a_record();
	void handles_without_a_name_are_not_a_record();

	// Names come out of the header verbatim, so nothing needs to guess whether
	// a string "looks like" a clip name. Avid reel/scene names contain '/'
	// (e.g. "63/5.new.01"), and post-production names things zOLD/zArchive on
	// purpose (the prefix sorts to the bottom of a bin).
	void slash_and_z_prefixed_names_are_kept_verbatim();
	void a_stray_path_in_the_span_is_not_the_clip_name();

	// Avid writes every non-ASCII value twice, MacRoman first then UTF-8 —
	// Media Composer's 1988 Mac-exclusive heritage. The MacRoman twin decodes
	// to replacement characters under UTF-8 and must never be elected; a value
	// with no clean copy shows blank rather than garbled.
	void utf8_twin_wins_over_macroman_twin();
	void macroman_only_value_yields_blank_not_mojibake();
	void multibyte_bin_name_survives_intact();
	void real_accented_bin_mdb_never_yields_mojibake();

	// A clip's file MOBs sit inside the master's record and carry no header of
	// their own, but the scanner looks up mobId AND masterMobId — so they must
	// inherit the record. And every MOB must keep a map entry either way:
	// dropping one would read as "not in the database" and could mislabel a
	// file as unreferenced.
	void file_mobs_inherit_the_master_record();
	void every_mob_keeps_a_map_entry();

	// Avid's placeholder MOB is byte-identical across unrelated projects and
	// appears in no MXF, so it names no media. The old parser emitted it.
	void placeholder_mob_is_not_a_record();

	// Validity gate: parse() used to report ok=true for ANY file over 64
	// bytes, so a corrupt or foreign msmMMOB.mdb "parsed" to zero records
	// and the scanner labelled the folder's unmatched files a verified
	// "No reference" instead of the honest "No database". A file must now
	// carry the Avid property-dictionary fingerprint near the front (the
	// real preludes vary: "_USER..." and "block NNNN\0_USER...") or contain
	// at least one Avid MOB before ok can be true.
	void real_fixture_mdbs_pass_the_validity_gate();
	void garbage_named_mdb_reports_not_ok();
	void mob_only_buffer_still_passes_the_gate();

	// End to end against real media: MXF headers resolved against the MDB
	// captured from their own folder, checked with each file's own
	// MaterialPackage name so the test can't pass by agreeing with itself.
	void mdb_join_resolves_real_mxf_files();
};

void TestMdbParser::extracts_full_record_from_one_object()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QByteArray buf;
	addAttribute(buf, {}, "_IMPORTSETTING", 0x0300);
	addAttribute(buf, "/Users/marty/Desktop/My Great Clip.mov", "_SRCFILE", 0x0301);
	addToken(buf, "QTFF");
	addAttribute(buf, "MyProjectBin", "_ORG_BIN", 0x0302);
	addRecord(buf, "My Great Clip", mob32());
	buf.append(QByteArray(16, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	const MdbRecord &r = records.first();
	QCOMPARE(r.mobIdHex, MobId::format(mob32()));
	QCOMPARE(r.clipName, QStringLiteral("My Great Clip"));
	QCOMPARE(r.sourceFilePath, QStringLiteral("/Users/marty/Desktop/My Great Clip.mov"));
	QCOMPARE(r.sourceFileName, QStringLiteral("My Great Clip.mov"));
	QCOMPARE(r.sourceContainer, QStringLiteral("QTFF"));
	QCOMPARE(r.bin, QStringLiteral("MyProjectBin"));
	QVERIFY(r.isImported);
}

void TestMdbParser::buildMobMap_keys_by_hex()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QByteArray buf;
	addRecord(buf, "Clip One", mob32());
	buf.append(QByteArray(32, '\0')); // pad past the 64-byte MDB minimum

	const auto map = MdbParser::buildMobMap(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(map.size(), 1);
	QVERIFY(map.contains(MobId::format(mob32())));
	QCOMPARE(map.value(MobId::format(mob32())).clipName, QStringLiteral("Clip One"));
}

void TestMdbParser::no_mob_yields_no_records()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Plenty of strings, but no 06 0A 2B 34 MOB signature anywhere.
	QByteArray buf;
	for (int i = 0; i < 10; ++i)
		addToken(buf, "not a mob just text");
	QVERIFY(buf.size() >= 64);

	QVERIFY(MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf)).isEmpty());
}

void TestMdbParser::too_small_file_returns_empty()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QVERIFY(MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", mob32())).isEmpty()); // 32 < 64
}

void TestMdbParser::wrong_member_count_is_not_a_record()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// One member written, three declared. The MOB still gets a map entry, but
	// nothing before it may be read as this object's name.
	QByteArray buf;
	addRecord(buf, "Not A Record", mob32(), /*members=*/1, /*declaredCount=*/3);
	buf.append(QByteArray(32, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	QVERIFY2(records.first().clipName.isEmpty(),
			 qPrintable("mismatched count accepted: " + records.first().clipName));
}

void TestMdbParser::handles_without_a_name_are_not_a_record()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Handle-shaped bytes and a plausible count, but no NUL-terminated name in
	// front of them — the run must not be promoted to a record.
	QByteArray buf(48, '\0');
	buf.append(handle(0x0100));
	buf.append(char(1));
	buf.append(char(0));
	buf.append(handle(0x0200));
	buf.append(mob32());
	buf.append(QByteArray(16, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	QVERIFY(records.first().clipName.isEmpty());
}

void TestMdbParser::slash_and_z_prefixed_names_are_kept_verbatim()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	QByteArray buf;
	addAttribute(buf, "zArchive_2020", "_ORG_BIN", 0x0300);
	addRecord(buf, "63/5.new.01", mob32());
	buf.append(QByteArray(16, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().clipName, QStringLiteral("63/5.new.01"));
	QCOMPARE(records.first().bin, QStringLiteral("zArchive_2020"));
}

void TestMdbParser::a_stray_path_in_the_span_is_not_the_clip_name()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// A path sitting loose in the record's attribute span, keyed to nothing.
	// The name comes from the header, so the path can't displace it — and with
	// no _SRCFILE key it isn't the source path either.
	QByteArray buf;
	addToken(buf, "/Volumes/EDIT/scratch/bar.mxf");
	addRecord(buf, "Real Clip Name", mob32());
	buf.append(QByteArray(16, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().clipName, QStringLiteral("Real Clip Name"));
	QVERIFY(records.first().sourceFilePath.isEmpty());
}

void TestMdbParser::utf8_twin_wins_over_macroman_twin()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// The verified on-disk layout: [MacRoman]\0[UTF-8]\0[_ORG_BIN].
	// "Caf\x8e t\x91st" is MacRoman for "Café tëst"; raw escapes so
	// source-file encoding can't drift what the bytes actually are.
	QByteArray buf;
	addAttribute(buf, QByteArray("Caf\xc3\xa9 t\xc3\xabst"), "_ORG_BIN", 0x0300,
				 QByteArray("Caf\x8e t\x91st"));
	addRecord(buf, "Accented Clip", mob32());
	buf.append(QByteArray(16, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().bin, QString::fromUtf8("Caf\xc3\xa9 t\xc3\xabst"));
}

void TestMdbParser::macroman_only_value_yields_blank_not_mojibake()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// No UTF-8 twin — only the MacRoman copy sits before the key. Blank over
	// garbled: the twin has letters and passed the old label heuristics before
	// the replacement-character guard existed.
	QByteArray buf;
	addAttribute(buf, QByteArray("Caf\x8e t\x91st"), "_ORG_BIN", 0x0300);
	addRecord(buf, "Accented Clip", mob32());
	buf.append(QByteArray(16, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	QVERIFY2(records.first().bin.isEmpty(),
			 qPrintable("MacRoman twin elected as bin: " + records.first().bin));
}

void TestMdbParser::multibyte_bin_name_survives_intact()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// "Проект Финал" as raw UTF-8 (hex-escaped so MSVC's source-charset
	// handling can't garble it): 23 bytes on disk, 12 UTF-16 code units.
	const QByteArray nameUtf8("\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB5\xD0\xBA\xD1\x82 "
							  "\xD0\xA4\xD0\xB8\xD0\xBD\xD0\xB0\xD0\xBB");
	QCOMPARE(nameUtf8.size(), 23);

	QByteArray buf;
	addAttribute(buf, nameUtf8, "_ORG_BIN", 0x0300);
	addRecord(buf, "Cyrillic Clip", mob32());
	buf.append(QByteArray(16, '\0'));

	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().bin, QString::fromUtf8(nameUtf8));
}

void TestMdbParser::real_accented_bin_mdb_never_yields_mojibake()
{
	// Real MC 2025 MDB whose bin is named "Café tëst" (fixture captured
	// 2026-07-20). The clip imported into that bin must resolve the UTF-8
	// form, and no record anywhere in the file may carry a replacement
	// character in any field.
	const QString path = QStringLiteral(FIXTURES_DIR "/msmMMOB_macroman.mdb");
	QVERIFY2(QFile::exists(path), qPrintable(path));

	bool ok = false;
	const auto records = MdbParser::parse(path, &ok);
	QVERIFY(ok);
	QVERIFY(!records.isEmpty());

	const QString expectedBin = QString::fromUtf8("Caf\xc3\xa9 t\xc3\xabst");
	bool sawAccentedBin = false;
	const QChar fffd(QChar::ReplacementCharacter);
	for (const MdbRecord &r : records)
	{
		if (r.bin == expectedBin)
			sawAccentedBin = true;
		QVERIFY2(!r.bin.contains(fffd), qPrintable("mojibake bin: " + r.bin));
		QVERIFY2(!r.clipName.contains(fffd), qPrintable("mojibake clip: " + r.clipName));
		QVERIFY2(!r.sourceFilePath.contains(fffd), qPrintable("mojibake path: " + r.sourceFilePath));
	}
	QVERIFY2(sawAccentedBin, "no record resolved the UTF-8 'Café tëst' bin name");
}

void TestMdbParser::file_mobs_inherit_the_master_record()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// The file MOB sits inside the master's record and carries no header. The
	// scanner looks it up by MediaFile::mobId, so it must resolve to the same
	// attributes as the master.
	QByteArray buf;
	buf.append(mob32('\x01')); // file MOB, no header of its own
	addAttribute(buf, "Rushes", "_ORG_BIN", 0x0300);
	addRecord(buf, "Master Clip", mob32('\x02'));
	buf.append(QByteArray(16, '\0'));

	const auto map = MdbParser::buildMobMap(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(map.size(), 2);
	const MdbRecord fileMob = map.value(MobId::format(mob32('\x01')));
	QCOMPARE(fileMob.clipName, QStringLiteral("Master Clip"));
	QCOMPARE(fileMob.bin, QStringLiteral("Rushes"));
	// Each entry still reports its own identity, not the master's.
	QCOMPARE(fileMob.mobIdHex, MobId::format(mob32('\x01')));
}

void TestMdbParser::every_mob_keeps_a_map_entry()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// A MOB far past the last record belongs to no span. It still needs an
	// entry: an absent key reads as "not in the database", which is what makes
	// the scanner call a file unreferenced.
	QByteArray buf;
	addRecord(buf, "Master Clip", mob32('\x02'));
	buf.append(QByteArray(64, '\0'));
	buf.append(mob32('\x03'));
	buf.append(QByteArray(16, '\0'));

	const auto map = MdbParser::buildMobMap(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(map.size(), 2);
	QVERIFY(map.contains(MobId::format(mob32('\x03'))));
	QVERIFY(map.value(MobId::format(mob32('\x03'))).clipName.isEmpty());
}

void TestMdbParser::placeholder_mob_is_not_a_record()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Avid's constant placeholder: 508 byte-identical copies in one real
	// database, 272 in another, 38 in a third, and present in no MXF.
	const QByteArray placeholder = QByteArray::fromHex(
		"060a2b340101010001010f0013000000bacc8a260647d528e5cd3f5b5f40443f");
	QCOMPARE(placeholder.size(), MobId::kRawSize);

	QByteArray buf;
	buf.append(placeholder);
	buf.append(QByteArray(16, '\0'));
	addRecord(buf, "Master Clip", mob32());
	buf.append(QByteArray(16, '\0'));

	const auto map = MdbParser::buildMobMap(writeMdb(tmp.path() + "/msmMMOB.mdb", buf));

	QCOMPARE(map.size(), 1);
	QVERIFY(map.contains(MobId::format(mob32())));
	QVERIFY(!map.contains(MobId::format(placeholder)));
}

void TestMdbParser::real_fixture_mdbs_pass_the_validity_gate()
{
	// Two genuine MDBs with different preludes: one opens straight at
	// "_USER", the other with a "block 1729" prefix before it. The gate
	// must scan for the fingerprint, not demand it at byte 0.
	const char *kFixtures[] = {FIXTURES_DIR "/msmMMOB.mdb", FIXTURES_DIR "/msmMMOB_macroman.mdb"};
	for (const char *path : kFixtures)
	{
		QVERIFY2(QFile::exists(QLatin1String(path)), path);
		bool ok = false;
		const auto records = MdbParser::parse(QLatin1String(path), &ok);
		QVERIFY2(ok, path);
		QVERIFY2(!records.isEmpty(), path);
	}
}

void TestMdbParser::garbage_named_mdb_reports_not_ok()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// None of these contain the property-dictionary strings or an Avid MOB
	// signature; each must fail the gate instead of vouching for a folder.
	const struct
	{
		const char *label;
		QByteArray bytes;
	} kGarbage[] = {
		{"all_zeros", QByteArray(4096, '\0')},
		{"repeating_junk", QByteArray(4096, '\xAB')},
		{"fake_jpeg", QByteArray::fromHex("ffd8ffe000104a464946") + QByteArray(4096, '\x5A')},
	};

	for (const auto &g : kGarbage)
	{
		bool ok = true;
		const auto records = MdbParser::parse(
			writeMdb(tmp.path() + QStringLiteral("/%1.mdb").arg(QLatin1String(g.label)), g.bytes),
			&ok);
		QVERIFY2(!ok, g.label);
		QVERIFY2(records.isEmpty(), g.label);
	}
}

void TestMdbParser::mob_only_buffer_still_passes_the_gate()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// No property-dictionary fingerprint, but a real MOB signature: the
	// parser can evidently read this file, so the gate must let it through
	// (this is also what keeps every synthetic buffer in this suite valid).
	QByteArray buf;
	addRecord(buf, "Gate Clip", mob32());
	buf.append(QByteArray(48, '\0'));

	bool ok = false;
	const auto records = MdbParser::parse(writeMdb(tmp.path() + "/msmMMOB.mdb", buf), &ok);
	QVERIFY(ok);
	QCOMPARE(records.size(), 1);
	QCOMPARE(records.first().clipName, QStringLiteral("Gate Clip"));
}

void TestMdbParser::mdb_join_resolves_real_mxf_files()
{
	// corpus_headers/*.mxf and corpus_headers/msmMMOB_round3.mdb come from the
	// same Avid folder: all 795 headers resolve into that database. They only
	// look unrelated if you compare MOB IDs raw — an MXF stores a MobID's
	// middle fields little-endian and the MDB big-endian, so the join has to
	// go through MobId::toPmrForm first (0/795 match without it, 795/795 with).
	// That conversion is the thing this test exists to pin.
	bool ok = false;
	const auto map = MdbParser::buildMobMap(
		QStringLiteral(FIXTURES_DIR "/corpus_headers/msmMMOB_round3.mdb"), &ok);
	QVERIFY(ok);
	QVERIFY(!map.isEmpty());

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
		const QString path = QStringLiteral(FIXTURES_DIR "/corpus_headers/") + QLatin1String(file);
		QVERIFY2(QFile::exists(path), qPrintable(path));

		const MxfMetadata meta = MxfParser::parseHeader(path);
		QVERIFY2(meta.valid, file);
		QVERIFY2(meta.clipNameFromMaterial, file);

		// An MXF writes a MobID's middle fields little-endian and the MDB
		// big-endian; without this conversion every lookup misses.
		QVERIFY2(!map.contains(meta.umid), "raw MXF UMID must not key the MDB");
		const QString key = MobId::toPmrForm(meta.umid);
		QVERIFY2(map.contains(key), qPrintable(QLatin1String(file) + QStringLiteral(" -> ") + key));

		const MdbRecord &rec = map[key];

		// Ground truth: the name the file carries in its own MaterialPackage.
		QCOMPARE(rec.clipName, meta.clipName);
		// And the record's source path must be the file this clip was imported
		// from — which ties the record to the right clip a second way, since
		// Avid named the import after the clip.
		QCOMPARE(rec.sourceFileName, meta.clipName + QStringLiteral(".mov"));
		QCOMPARE(rec.bin, expectedBin);
		QVERIFY2(rec.isImported, file);

		// The bin is named after the format its clips are in, so the raster
		// the MXF reports has to be the one the bin claims.
		if (!meta.resolution.isEmpty())
		{
			QCOMPARE(meta.resolution, QStringLiteral("1920x1080"));
			QVERIFY2(rec.bin.contains(QStringLiteral("1080")), qPrintable(rec.bin));
		}
	}
}

QTEST_APPLESS_MAIN(TestMdbParser)
#include "tst_mdbparser.moc"
