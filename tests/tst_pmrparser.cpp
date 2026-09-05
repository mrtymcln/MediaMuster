// Unit tests for PmrParser::parse over hand-crafted msmFMID.pmr buffers.
// Exact buffers exercise the recovered version/endian branches, independent
// Unicode sets, field boundaries and truncation. Real PMRs pin current output.
// The omf_v2_* cases cover the OMF-era version-2 layout (8-byte MOBs) the
// same way, plus the two real version-2 files under fixtures/omf.

#include "mobid.h"
#include "omfuid.h"
#include "pmrparser.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QtGlobal>
#include <QRegularExpression>

#include <limits>

namespace
{
	void u16le(QByteArray &b, quint16 v)
	{
		b.append(char(v & 0xff));
		b.append(char((v >> 8) & 0xff));
	}
	void u32le(QByteArray &b, quint32 v)
	{
		for (int i = 0; i < 4; ++i)
			b.append(char((v >> (8 * i)) & 0xff));
	}

	/// Header, in Avid's own field names: MAGIC, VERSION, numMobs — all u32
	/// LE. MAGIC is 0x000007A9 in every real PMR; the parameter exists so a
	/// test can write a wrong one deliberately.
	QByteArray pmrHeader(quint32 pairCount, quint32 version = 8, quint32 magic = 0x000007A9)
	{
		QByteArray b;
		u32le(b, magic);
		u32le(b, version);
		u32le(b, pairCount);
		return b;
	}

	// FILE record: 32-byte MOB | u16 nameLen | name | u16 projLen | project.
	QByteArray fileRecord(const QByteArray &mob, const QByteArray &name, const QByteArray &project)
	{
		QByteArray b = mob;
		u16le(b, quint16(name.size()));
		b.append(name);
		u16le(b, quint16(project.size()));
		b.append(project);
		return b;
	}

	// MASTER record: 32-byte MOB | u32 trailer (the file's mtime, Unix seconds).
	QByteArray masterRecord(const QByteArray &mob, quint32 trailer = 0)
	{
		QByteArray b = mob;
		u32le(b, trailer);
		return b;
	}

	// The Unicode record set MC 2025 appends: u32 16 | u32 count | the pairs
	// again, FILE names as UTF-8 behind two NUL bytes that nameLen counts.
	QByteArray unicodeHeader(quint32 pairCount)
	{
		QByteArray b;
		u32le(b, 16);
		u32le(b, pairCount);
		return b;
	}
	QByteArray unicodeFileRecord(const QByteArray &mob, const QByteArray &utf8Name, const QByteArray &project)
	{
		QByteArray b = mob;
		u16le(b, quint16(utf8Name.size() + 2));
		b.append(char(0));
		b.append(char(0));
		b.append(utf8Name);
		u16le(b, quint16(project.size()));
		b.append(project);
		return b;
	}

	QByteArray fileMob()
	{
		return QByteArray::fromHex("060a2b3401010105"
								   "01010f1013000000"
								   "4a507dea74110690"
								   "7a361e6a605d3613");
	}
	QByteArray masterMob()
	{
		return QByteArray::fromHex("060a2b3401010105"
								   "01010f1013000000"
								   "d2467dea74110690"
								   "91901e6a605d3613");
	}

	// MARK: - OMF-era (version 2) helpers

	// A version-2 record is the same grammar with an 8-byte MOB in the MBCS
	// set, so fileRecord/masterRecord serve both widths; what changes is the
	// header's version and the MOB bytes. These start with arbitrary bytes
	// (no 06 0a 2b 34 prefix), exactly as the real files do.
	QByteArray pmrHeaderOmf(quint32 pairCount)
	{
		return pmrHeader(pairCount, /*version=*/2);
	}
	QByteArray fileMob8()
	{
		return QByteArray::fromHex("7429976a70397047");
	}
	QByteArray masterMob8()
	{
		return QByteArray::fromHex("7429976a4e397047");
	}
	// The 32-byte form Avid writes into a version-2 file's Unicode set: the
	// 8 bytes inside its fixed prefix and suffix.
	QByteArray wrapped(const QByteArray &eight)
	{
		const auto raw = OmfUid::wrap8(reinterpret_cast<const unsigned char *>(eight.constData()));
		return QByteArray(reinterpret_cast<const char *>(raw.data()), int(raw.size()));
	}
	QString canonical8(const QByteArray &eight)
	{
		return OmfUid::canonicalFromPmr8(reinterpret_cast<const unsigned char *>(eight.constData()));
	}

	void ordered16(QByteArray &bytes, quint16 value, bool bigEndian)
	{
		for (int i = 0; i < 2; ++i)
			bytes.append(char(value >> (8 * (bigEndian ? 1 - i : i))));
	}
	void ordered32(QByteArray &bytes, quint32 value, bool bigEndian)
	{
		for (int i = 0; i < 4; ++i)
			bytes.append(char(value >> (8 * (bigEndian ? 3 - i : i))));
	}
	QByteArray orderedHeader(qint32 version, quint32 count, bool bigEndian)
	{
		QByteArray bytes;
		ordered32(bytes, 0x7a9, bigEndian);
		ordered32(bytes, quint32(version), bigEndian);
		ordered32(bytes, count, bigEndian);
		return bytes;
	}
	QByteArray orderedUnicodeHeader(quint32 count, bool bigEndian)
	{
		QByteArray bytes;
		ordered32(bytes, 16, bigEndian);
		ordered32(bytes, count, bigEndian);
		return bytes;
	}
	QByteArray orderedMob(qint32 version, bool bigEndian, bool master = false)
	{
		if (version <= 7)
		{
			if (!bigEndian)
				return master ? masterMob8() : fileMob8();
			// Two BE uint32 words; explicitly specified, independent of parser.
			return QByteArray::fromHex(master ? "6a9729744770394e" : "6a97297447703970");
		}
		if (!bigEndian)
			return master ? masterMob() : fileMob();
		// AAF ID: label/instance bytes, BE Data1/2/3, then raw Data4 bytes.
		return QByteArray::fromHex(master
									   ? "060a2b340101010501010f1013000000ea7d46d21174900691901e6a605d3613"
									   : "060a2b340101010501010f1013000000ea7d504a117490067a361e6a605d3613");
	}
	QByteArray orderedRecord(qint32 version, bool bigEndian, const QByteArray &name,
							 const QByteArray &project = "project", quint32 modified = 0x12345678)
	{
		QByteArray bytes = orderedMob(version, bigEndian);
		ordered16(bytes, quint16(name.size() + (version == 16 ? 2 : 0)), bigEndian);
		if (version == 16)
			bytes.append(2, '\0');
		bytes += name;
		if (version != 1)
		{
			ordered16(bytes, quint16(project.size()), bigEndian);
			bytes += project;
			bytes += orderedMob(version, bigEndian, true);
		}
		ordered32(bytes, modified, bigEndian);
		return bytes;
	}

	QString writePmr(const QString &path, const QByteArray &bytes)
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

class TestPmrParser : public QObject
{
	Q_OBJECT
private slots:
	void parses_one_file_comp_pair();
	void empty_project_still_yields_entry();
	void empty_record_sets_are_valid();
	void implausible_pair_count_is_rejected();
	void file_comp_desync_bails_with_what_it_has();
	void truncated_body_stops_cleanly();
	void too_small_file_returns_empty();

	// Unknown positive versions are rejected by Avid's version gate.
	void unsupported_version_is_rejected();

	// Avid checks a magic word (0x000007A9) before the version; a file that
	// is not a PMR at all must be refused before its bytes are read as a
	// version number.
	void wrong_magic_is_refused();

	void non_smpte_master_identity_is_preserved();
	void non_smpte_file_identity_is_preserved();

	// The `ok` out-param is how the scanner tells "readable database, entry
	// genuinely absent" from "database can't vouch for anything" — a bail
	// that still returns partial entries must NOT report ok.
	void ok_flag_reports_clean_vs_failed_parses();

	// Avid writes PMR strings in MacRoman — Media Composer's 1988 Mac
	// heritage — not UTF-8: real projects arrive as "t\x91st" (ë) and
	// "\xA7" (ß). Decoding them as UTF-8 painted replacement characters
	// into the Project column for every accented name (found live,
	// 2026-07-30). Valid UTF-8 must still decode as UTF-8, so a future
	// Avid that writes UTF-8 keeps working.
	void macroman_project_names_decode_correctly();

	// The second record set (VERSION_UNICODE = 16) carries the filenames in
	// UTF-8 — the only place a character MacRoman cannot hold survives. It
	// replaces the complete record vector. Malformed sets report failure.
	void unicode_section_names_take_precedence();
	void malformed_unicode_section_keeps_mbcs_names();
	// The 4 bytes after a MASTER MOB are the essence file's mtime.
	void trailer_is_the_file_modified_time();
	void real_fixture_pmrs_parse_with_unicode_names();
	// Two spellings of the same instant: Unix UTC (MC 2025) and Mac 1904-epoch
	// local time (older MC, seen on a 2018–19 folder). Both must match; a
	// different instant, or an unknown trailer, must not.
	void trailer_matches_modified_in_both_epochs();
	void exact_one_hour_clock_exception_data();
	void exact_one_hour_clock_exception();

	// OMF-era: the version-2 layout under `OMFI MediaFiles`. Same grammar,
	// 8-byte MOBs with no fixed prefix, widened to the 32-byte form Avid's
	// own Unicode set uses — so an entry from either era carries one id
	// spelling. The version-8 path is pinned unchanged by every case above.
	void omf_v2_pair_parses_with_wrapped_mobs();
	void omf_v2_unicode_set_replaces_names();
	void omf_v2_file_desync_bails_with_what_it_has();
	void omf_v2_real_avid_supporting_fixture();
	void omf_v2_real_mc2026_audio_fixture();
	void accepted_versions_and_byte_orders_data();
	void accepted_versions_and_byte_orders();
	void independent_unicode_records_data();
	void independent_unicode_records();
	void every_truncated_boundary_reports_failure_data();
	void every_truncated_boundary_reports_failure();
	void project_lengths_are_checked();
	void unicode_framing_and_encoding_are_checked();
	void filename_limits_and_null_strings();
	void unicode_filename_limit_counts_utf16_units();
	void null_identities_are_explicit();
};

void TestPmrParser::parses_one_file_comp_pair()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray buf = pmrHeader(1) + fileRecord(fileMob(), "CLIP.A01.mxf", "block 1729") +
						   masterRecord(masterMob());
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf));

	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("CLIP.A01.mxf"));
	QCOMPARE(entries[0].project, QStringLiteral("block 1729"));
	QCOMPARE(entries[0].mobId, MobId::format(fileMob()));
	QCOMPARE(entries[0].masterMobId, MobId::format(masterMob()));
}

void TestPmrParser::empty_project_still_yields_entry()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray buf =
		pmrHeader(1) + fileRecord(fileMob(), "NOPROJ.mxf", "") + masterRecord(masterMob());
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf));

	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("NOPROJ.mxf"));
	QVERIFY(entries[0].project.isEmpty());
}

void TestPmrParser::empty_record_sets_are_valid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (const auto &buf : {pmrHeader(0), pmrHeader(0) + unicodeHeader(0)})
	{
		bool ok = false;
		QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/empty.pmr", buf), &ok).isEmpty());
		QVERIFY(ok);
	}
}

void TestPmrParser::implausible_pair_count_is_rejected()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray buf = pmrHeader(6'000'000) + fileMob(); // impossible with this body size
	QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf)).isEmpty());
}

void TestPmrParser::file_comp_desync_bails_with_what_it_has()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Two pairs claimed, but where the 3rd record's FILE should be, the leading
	// u16 is 0 — that's MASTER-shaped, so the parser bails and keeps pair 0.
	QByteArray buf = pmrHeader(2) + fileRecord(fileMob(), "GOOD.mxf", "proj") + masterRecord(masterMob());
	buf += fileMob(); // stray 32-byte MOB
	u16le(buf, 0);	  // nameLen 0 → desync signal
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf));

	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("GOOD.mxf"));
}

void TestPmrParser::truncated_body_stops_cleanly()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Header claims 3 pairs; only one pair's worth of bytes is present.
	// The surviving entry is returned, but ok must be false: the missing
	// tail could have contained any file, so the database can't vouch for
	// misses and the scanner must classify unmatched files as "No
	// database", never a verified "No reference". (Regression: these
	// truncation breaks used to fall through to ok=true.)
	const QByteArray buf =
		pmrHeader(3) + fileRecord(fileMob(), "ONE.mxf", "proj") + masterRecord(masterMob());
	bool ok = true;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf), &ok);

	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("ONE.mxf"));
	QVERIFY2(!ok, "body truncation must report ok=false despite partial entries");
}

void TestPmrParser::too_small_file_returns_empty()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray buf = pmrHeader(1) + QByteArray(10, '\0'); // 22 bytes, under header+MOB
	QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf)).isEmpty());
}

void TestPmrParser::unsupported_version_is_rejected()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Version 16 is accepted only as the optional second section.
	for (qint32 version : {9, 15, 16, 17, std::numeric_limits<qint32>::max()})
		for (bool bigEndian : {false, true})
		{
			const QByteArray buf = orderedHeader(version, 1, bigEndian) + orderedRecord(8, bigEndian, "CLIP.A01.mxf", "block 1729");
			bool ok = true;
			QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf), &ok).isEmpty());
			QVERIFY(!ok);
		}
}

void TestPmrParser::non_smpte_master_identity_is_preserved()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Read_AAFMobID consumes all 32 bytes without a prefix gate. Identity
	// semantics cannot be substituted for record framing.
	const QByteArray unusualMaster(32, 'X');
	const QByteArray buf = pmrHeader(1) + fileRecord(fileMob(), "CLIP.A01.mxf", "proj") +
						   masterRecord(unusualMaster, 123);
	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].masterMobId, MobId::format(unusualMaster));
	QCOMPARE(entries[0].fileModifiedSecs, 123u);
}

void TestPmrParser::non_smpte_file_identity_is_preserved()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray unusualFile(32, 'Z');
	const QByteArray buf = pmrHeader(2) + fileRecord(fileMob(), "ONE.mxf", "proj") +
						   masterRecord(masterMob()) + fileRecord(unusualFile, "TWO.mxf", "proj") + masterRecord(masterMob());
	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 2);
	QCOMPARE(entries[1].mobId, MobId::format(unusualFile));
	QCOMPARE(entries[1].masterMobId, MobId::format(masterMob()));
}

void TestPmrParser::ok_flag_reports_clean_vs_failed_parses()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	bool ok = false;

	// Clean end-to-end parse: ok=true.
	const QByteArray good = pmrHeader(1) + fileRecord(fileMob(), "CLIP.A01.mxf", "block 1729") +
							masterRecord(masterMob());
	QCOMPARE(PmrParser::parse(writePmr(tmp.path() + "/good.pmr", good), &ok).size(), 1);
	QVERIFY(ok);

	// Missing file: ok=false.
	QVERIFY(PmrParser::parse(tmp.path() + "/nonexistent.pmr", &ok).isEmpty());
	QVERIFY(!ok);

	// Too small to be real: ok=false.
	QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/small.pmr", QByteArray("JUNKJUNKJUNKJUNK")), &ok)
				.isEmpty());
	QVERIFY(!ok);

	// Desync bail returns partial entries — and must still report ok=false,
	// otherwise the scanner would treat a half-read database as authoritative.
	QByteArray desync = pmrHeader(2) + fileRecord(fileMob(), "GOOD.mxf", "proj") +
						masterRecord(masterMob());
	desync += fileMob(); // stray 32-byte MOB
	u16le(desync, 0);	 // nameLen 0 → desync signal
	QCOMPARE(PmrParser::parse(writePmr(tmp.path() + "/desync.pmr", desync), &ok).size(), 1);
	QVERIFY(!ok);
}

void TestPmrParser::macroman_project_names_decode_correctly()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Pair 1: MacRoman project "tëst_25P" (ë = 0x91) — the real corpus bytes.
	// Pair 2: valid-UTF-8 project "café" — must stay UTF-8, never re-read
	// as MacRoman ("Ã©" mojibake in the other direction).
	const QByteArray macRoman("t\x91st_25P");
	const QByteArray utf8("caf\xc3\xa9");
	const QByteArray buf = pmrHeader(2) +
						   fileRecord(fileMob(), "CLIP.A01.mxf", macRoman) +
						   masterRecord(masterMob()) +
						   fileRecord(fileMob(), "CLIP.A02.mxf", utf8) +
						   masterRecord(masterMob());

	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf));

	QCOMPARE(entries.size(), 2);
	QCOMPARE(entries[0].project, QString::fromUtf8("t\xc3\xabst_25P")); // tëst_25P
	QCOMPARE(entries[1].project, QString::fromUtf8("caf\xc3\xa9"));		// café
}

void TestPmrParser::wrong_magic_is_refused()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray buf =
		pmrHeader(1, 8, 0x0000DEAD) + fileRecord(fileMob(), "CLIP.A01.mxf", "block 1729") +
		masterRecord(masterMob());
	bool ok = true;
	const QVector<PmrEntry> entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf), &ok);
	QVERIFY2(entries.isEmpty(), "a file with the wrong magic must yield nothing");
	QVERIFY2(!ok, "and must report failure so the folder degrades to 'No database'");
}

void TestPmrParser::unicode_section_names_take_precedence()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Avid writes '?' where MacRoman has no character (ę); the UTF-8 set has it.
	const QByteArray macName("zT?t_clip.mxf");
	const QByteArray utf8Name("zT\xc4\x99t_clip.mxf");
	QByteArray b = pmrHeader(1) + fileRecord(fileMob(), macName, "proj") + masterRecord(masterMob(), 7);
	b += unicodeHeader(1) + unicodeFileRecord(fileMob(), utf8Name, "proj") + masterRecord(masterMob(), 7);

	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", b), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries.first().fileName, QString::fromUtf8("zT\xc4\x99t_clip.mxf"));
	QCOMPARE(entries.first().project, QStringLiteral("proj"));
	QCOMPARE(entries.first().mobId, MobId::format(fileMob()));
	QCOMPARE(entries.first().masterMobId, MobId::format(masterMob()));
	QCOMPARE(entries.first().fileModifiedSecs, 7u);
}

void TestPmrParser::malformed_unicode_section_keeps_mbcs_names()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray mbcs = pmrHeader(1) + fileRecord(fileMob(), "mbcs.mxf", "proj") + masterRecord(masterMob());
	// A different count is valid, but claiming two and storing only one is
	// truncated. Keep MBCS recovery rows without authorizing index misses.
	const QByteArray truncated = mbcs + unicodeHeader(2) + unicodeFileRecord(fileMob(), "utf8.mxf", "proj") +
								 masterRecord(masterMob());
	bool ok = true;
	auto entries = PmrParser::parse(writePmr(tmp.path() + "/a.pmr", truncated), &ok);
	QVERIFY(!ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries.first().fileName, QStringLiteral("mbcs.mxf"));

	QByteArray badLength = mbcs + unicodeHeader(1) + fileMob();
	u16le(badLength, 300);
	badLength += "short";
	entries = PmrParser::parse(writePmr(tmp.path() + "/b.pmr", badLength), &ok);
	QVERIFY(!ok);
	QCOMPARE(entries.first().fileName, QStringLiteral("mbcs.mxf"));

	entries = PmrParser::parse(writePmr(tmp.path() + "/c.pmr", mbcs), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.first().fileName, QStringLiteral("mbcs.mxf"));
}

void TestPmrParser::trailer_is_the_file_modified_time()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray b = pmrHeader(1) + fileRecord(fileMob(), "clip.mxf", "proj") + masterRecord(masterMob(), 1778755394u);
	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", b), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries.first().fileModifiedSecs, 1778755394u);
}

void TestPmrParser::real_fixture_pmrs_parse_with_unicode_names()
{
	bool ok = false;
	// The one-pair fixture: TONE file, trailer = its mtime when captured.
	auto tone = PmrParser::parse(QStringLiteral(FIXTURES_DIR "/msmFMID.pmr"), &ok);
	QVERIFY(ok);
	QCOMPARE(tone.size(), 1);
	QCOMPARE(tone.first().fileName, QStringLiteral("TONE_100A01.EA7D504A.611740.mxf"));
	QCOMPARE(tone.first().project, QStringLiteral("block 1729"));
	QCOMPARE(tone.first().fileModifiedSecs, 1778755394u);

	// The corpus PMRs: every pair parses, every entry has both MOBs and a
	// trailer, and the ß filenames arrive as ß (from the UTF-8 set).
	struct Gen
	{
		const char *path;
		int pairs;
	};
	const Gen gens[] = {{FIXTURES_DIR "/corpus_headers/msmFMID.pmr", 435},
						{FIXTURES_DIR "/corpus_headers/msmFMID_round3.pmr", 360}};
	for (const Gen &g : gens)
	{
		const auto entries = PmrParser::parse(QLatin1String(g.path), &ok);
		QVERIFY2(ok, g.path);
		QCOMPARE(entries.size(), g.pairs);
		int eszett = 0;
		for (const PmrEntry &e : entries)
		{
			QVERIFY2(!e.mobId.isEmpty() && !e.masterMobId.isEmpty(), qPrintable(e.fileName));
			QVERIFY2(e.fileModifiedSecs > 1'700'000'000u, qPrintable(e.fileName));
			QVERIFY2(!e.fileName.contains(QChar(QChar::ReplacementCharacter)), qPrintable(e.fileName));
			if (e.fileName.contains(QChar(0xDF)))
				++eszett;
		}
		if (g.pairs == 435)
			QVERIFY2(eszett >= 6, qPrintable(QString::number(eszett)));
	}
}

void TestPmrParser::trailer_matches_modified_in_both_epochs()
{
	// A real pair from /Volumes/EDIT/Avid MediaFiles/MXF/1 (Feb 2019): the
	// file's mtime, and the trailer Avid wrote = mtime + 2082844800 + 11 h
	// (AEDT). The test derives the local offset the same way the code does,
	// so it holds on any machine; on a Sydney machine the literal is 3632750026.
	const qint64 mtime = 1549865626;
	const QDateTime onDisk = QDateTime::fromSecsSinceEpoch(mtime);
	const qint64 macLocal = mtime + 2082844800 + onDisk.offsetFromUtc();
	QVERIFY(PmrParser::trailerMatchesModified(quint32(macLocal), onDisk));
	QVERIFY(PmrParser::trailerMatchesModified(quint32(macLocal + 2), onDisk));
	// Unix UTC, as MC 2025 writes it.
	QVERIFY(PmrParser::trailerMatchesModified(quint32(mtime), onDisk));
	QVERIFY(PmrParser::trailerMatchesModified(quint32(mtime - 1), onDisk));
	// Not the same instant, in either spelling.
	QVERIFY(!PmrParser::trailerMatchesModified(quint32(mtime + 60), onDisk));
	QVERIFY(!PmrParser::trailerMatchesModified(quint32(macLocal + 7200), onDisk));
	// Unknown trailer, invalid date.
	QVERIFY(!PmrParser::trailerMatchesModified(0, onDisk));
	QVERIFY(!PmrParser::trailerMatchesModified(quint32(mtime), QDateTime()));
}

void TestPmrParser::exact_one_hour_clock_exception_data()
{
	QTest::addColumn<bool>("legacyEpoch");
	QTest::addColumn<int>("delta");
	QTest::addColumn<bool>("expected");
	for (const bool legacy : {false, true})
		for (const int delta : {-7200, -3602, -3601, -3600, -3599, -3598, -3, -2, 0,
								2, 3, 3598, 3599, 3600, 3601, 3602, 7200})
			QTest::newRow(qPrintable(QString("%1-%2").arg(legacy ? "Mac-local" : "Unix").arg(delta)))
				<< legacy << delta << (qAbs(delta) <= 2 || qAbs(delta) == 3600);
}

void TestPmrParser::exact_one_hour_clock_exception()
{
	QFETCH(bool, legacyEpoch);
	QFETCH(int, delta);
	QFETCH(bool, expected);
	const qint64 mtime = 1549865626;
	const QDateTime onDisk = QDateTime::fromSecsSinceEpoch(mtime);
	const qint64 storedTime = mtime + (legacyEpoch ? 2082844800 + onDisk.offsetFromUtc() : 0);
	QCOMPARE(PmrParser::trailerMatchesModified(quint32(storedTime + delta), onDisk), expected);
	// Zero remains unknown even if adding the new clock rule would make it
	// appear one hour from the file. A missing date also has no clock evidence.
	QVERIFY(!PmrParser::trailerMatchesModified(0, QDateTime::fromSecsSinceEpoch(3600)));
	QVERIFY(!PmrParser::trailerMatchesModified(0, QDateTime::fromSecsSinceEpoch(-3600)));
	QVERIFY(!PmrParser::trailerMatchesModified(quint32(storedTime), QDateTime()));
}

void TestPmrParser::omf_v2_pair_parses_with_wrapped_mobs()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Neither MOB starts with the Avid prefix — the version-8 guard would
	// bail on record 0; version 2 must read it as a normal pair.
	const QByteArray buf = pmrHeaderOmf(1) + fileRecord(fileMob8(), "TONE.wav", "proj") +
						   masterRecord(masterMob8(), 1788291444u);
	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf), &ok);

	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("TONE.wav"));
	QCOMPARE(entries[0].project, QStringLiteral("proj"));
	QCOMPARE(entries[0].mobId, canonical8(fileMob8()));
	QCOMPARE(entries[0].masterMobId, canonical8(masterMob8()));
	QCOMPARE(entries[0].mobId, QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"));
	QVERIFY(OmfUid::isOmfForm(entries[0].mobId));
	QVERIFY(OmfUid::isOmfForm(entries[0].masterMobId));
	QCOMPARE(entries[0].fileModifiedSecs, 1788291444u);

	// A one-pair version-2 file is 37 bytes — under the 44 the version-8
	// minimum-size guard demands. It must not be refused as "too small".
	const QByteArray tiny = pmrHeaderOmf(1) + fileRecord(fileMob8(), "a", "") + masterRecord(masterMob8());
	QCOMPARE(tiny.size(), 37);
	QCOMPARE(PmrParser::parse(writePmr(tmp.path() + "/tiny.pmr", tiny), &ok).size(), 1);
	QVERIFY(ok);
}

void TestPmrParser::omf_v2_unicode_set_replaces_names()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// The Unicode set of a version-2 file carries the MOBs already in the
	// 32-byte wrapped form (verified on both real files), which is how the
	// FILE-MOB match lines up with the widened MBCS entry.
	const QByteArray macName("zT?t_clip.omf");
	const QByteArray utf8Name("zT\xc4\x99t_clip.omf");
	QByteArray b = pmrHeaderOmf(1) + fileRecord(fileMob8(), macName, "proj") + masterRecord(masterMob8(), 7);
	b += unicodeHeader(1) + unicodeFileRecord(wrapped(fileMob8()), utf8Name, "proj") +
		 masterRecord(wrapped(masterMob8()), 7);

	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", b), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries.first().fileName, QString::fromUtf8("zT\xc4\x99t_clip.omf"));
	QCOMPARE(entries.first().project, QStringLiteral("proj"));
	QCOMPARE(entries.first().mobId, canonical8(fileMob8()));
	QCOMPARE(entries.first().masterMobId, canonical8(masterMob8()));
	QCOMPARE(entries.first().fileModifiedSecs, 7u);
}

void TestPmrParser::omf_v2_file_desync_bails_with_what_it_has()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// With no MOB prefix to check in version 2, the FILE record's nameLen is
	// the desync detector: two pairs claimed, and where the 3rd record's
	// FILE should be, the leading u16 is 0 — MASTER-shaped. Keep pair 0,
	// report ok=false.
	QByteArray buf = pmrHeaderOmf(2) + fileRecord(fileMob8(), "GOOD.omf", "proj") + masterRecord(masterMob8());
	buf += fileMob8(); // stray 8-byte MOB
	u16le(buf, 0);	   // nameLen 0 → desync signal
	bool ok = true;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf), &ok);

	QVERIFY(!ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("GOOD.omf"));
	QCOMPARE(entries[0].masterMobId, canonical8(masterMob8()));
}

void TestPmrParser::omf_v2_real_avid_supporting_fixture()
{
	// Avid's shipped SupportingFiles database: 80 pairs, no project names,
	// trailers of 1626810310 (73 pairs) or 1626810312 (7 pairs) — the
	// install's write time, two seconds apart — and a Unicode set that
	// repeats every name unchanged.
	bool ok = false;
	const auto entries = PmrParser::parse(QStringLiteral(FIXTURES_DIR "/omf/avid_supporting/msmFMID.pmr"), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 80);
	QSet<QString> fileMobs;
	for (const PmrEntry &e : entries)
	{
		QVERIFY2(OmfUid::isOmfForm(e.mobId), qPrintable(e.fileName + ' ' + e.mobId));
		QVERIFY2(OmfUid::isOmfForm(e.masterMobId), qPrintable(e.fileName + ' ' + e.masterMobId));
		QVERIFY2(e.mobId != e.masterMobId, qPrintable(e.fileName));
		QVERIFY2(e.project.isEmpty(), qPrintable(e.project));
		QVERIFY2(e.fileName.endsWith(QLatin1String(".omf")), qPrintable(e.fileName));
		QVERIFY2(e.fileModifiedSecs == 1626810310u || e.fileModifiedSecs == 1626810312u,
				 qPrintable(e.fileName + QLatin1Char(' ') + QString::number(e.fileModifiedSecs)));
		fileMobs.insert(e.mobId);
	}
	QCOMPARE(fileMobs.size(), 80);
	QCOMPARE(entries.first().fileName, QStringLiteral("BLACK_720x576x1_DV420.omf"));
	QCOMPARE(entries.first().mobId, QStringLiteral("060a2b3401010101.01010f0013000000.5f489d3ab16ff300.060e2b347f7f2a80"));
	QCOMPARE(entries.first().masterMobId, QStringLiteral("060a2b3401010101.01010f0013000000.5f489d3ab06ff300.060e2b347f7f2a80"));
	QCOMPARE(entries.first().fileModifiedSecs, 1626810310u);
	QCOMPARE(entries.last().fileName, QStringLiteral("OFFLINE_288x243x1_JFIF20mP.omf"));
}

void TestPmrParser::omf_v2_real_mc2026_audio_fixture()
{
	// MC 26.8 wrote this beside two fresh audio files: 2 pairs, the ß project
	// in MacRoman (0xA7) everywhere, and trailers equal to each file's mtime
	// at capture. The four canonical hexes are the wrapped 8-byte MOBs.
	bool ok = false;
	const auto entries = PmrParser::parse(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/msmFMID.pmr"), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 2);
	const QString project = QString::fromUtf8("zTe\xc3\x9ft_PAL_25p");

	QCOMPARE(entries[0].fileName, QStringLiteral("TONE_100A01.6A972974.039700.wav"));
	QCOMPARE(entries[0].project, project);
	QCOMPARE(entries[0].mobId, QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"));
	QCOMPARE(entries[0].masterMobId, QStringLiteral("060a2b3401010101.01010f0013000000.7429976a4e397047.060e2b347f7f2a80"));
	QCOMPARE(entries[0].fileModifiedSecs, 1788291444u);

	QCOMPARE(entries[1].fileName, QStringLiteral("TONE_100A01.6A972997.0C53E0.aif"));
	QCOMPARE(entries[1].project, project);
	QCOMPARE(entries[1].mobId, QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3ec57047.060e2b347f7f2a80"));
	QCOMPARE(entries[1].masterMobId, QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3dc57047.060e2b347f7f2a80"));
	QCOMPARE(entries[1].fileModifiedSecs, 1788291480u);

	// The index keys on the same names, so a lookup by filename finds each.
	const PmrIndex index = PmrParser::buildFileMap(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/msmFMID.pmr"), &ok);
	QVERIFY(ok);
	QCOMPARE(index.size(), 2);
}

void TestPmrParser::accepted_versions_and_byte_orders_data()
{
	QTest::addColumn<qint32>("version");
	QTest::addColumn<bool>("bigEndian");
	// Signed <9 is the actual LoadPMR branch, with no lower-bound check.
	for (const qint32 version : {std::numeric_limits<qint32>::min(), -1, 0, 1, 2, 3, 4, 5, 6, 7, 8})
		for (bool bigEndian : {false, true})
			QTest::newRow(qPrintable(QString::number(version) + (bigEndian ? "-BE" : "-LE")))
				<< version << bigEndian;
}

void TestPmrParser::accepted_versions_and_byte_orders()
{
	QFETCH(qint32, version);
	QFETCH(bool, bigEndian);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QByteArray bytes = orderedHeader(version, 2, bigEndian) + orderedRecord(version, bigEndian, "ONE.media", "one", 0x12345678) + orderedRecord(version, bigEndian, "TWO.media", "two", 0x87654321);
	bool ok = false;
	auto entries = PmrParser::parse(writePmr(tmp.path() + "/versions.pmr", bytes), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 2);
	QCOMPARE(entries[0].fileName, QStringLiteral("ONE.media"));
	QCOMPARE(entries[1].fileName, QStringLiteral("TWO.media"));
	QCOMPARE(entries[0].fileModifiedSecs, 0x12345678u);
	QCOMPARE(entries[1].fileModifiedSecs, 0x87654321u);
	QCOMPARE(entries[0].mobId, version <= 7 ? canonical8(fileMob8()) : MobId::format(fileMob()));
	if (version == 1)
	{
		// No project/master bytes exist in v1; consuming either would shift
		// the timestamp and next record. Do not invent a master from file ID.
		QVERIFY(entries[0].masterMobId.isEmpty());
		QVERIFY(entries[0].project.isEmpty());
	}
	else
	{
		QCOMPARE(entries[0].masterMobId, version <= 7 ? canonical8(masterMob8()) : MobId::format(masterMob()));
		QCOMPARE(entries[0].project, QStringLiteral("one"));
	}

	bytes += orderedUnicodeHeader(1, bigEndian) + orderedRecord(16, bigEndian, QString::fromUtf8("東京.media").toUtf8(), "unicode", 0xa1b2c3d4);
	entries = PmrParser::parse(writePmr(tmp.path() + "/versions.pmr", bytes), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QString::fromUtf8("東京.media"));
	QCOMPARE(entries[0].mobId, MobId::format(fileMob()));
	QCOMPARE(entries[0].masterMobId, MobId::format(masterMob()));
	QCOMPARE(entries[0].project, QStringLiteral("unicode"));
	QCOMPARE(entries[0].fileModifiedSecs, 0xa1b2c3d4u);
}

void TestPmrParser::independent_unicode_records_data()
{
	QTest::addColumn<bool>("bigEndian");
	QTest::addColumn<int>("mbcsCount");
	QTest::addColumn<int>("unicodeCount");
	for (bool bigEndian : {false, true})
		for (const auto counts : {std::pair<int, int>{0, 2}, {1, 2}, {3, 1}, {1, 0}})
			QTest::newRow(qPrintable(QString("%1-%2-%3").arg(bigEndian ? "BE" : "LE").arg(counts.first).arg(counts.second)))
				<< bigEndian << counts.first << counts.second;
}

void TestPmrParser::independent_unicode_records()
{
	QFETCH(bool, bigEndian);
	QFETCH(int, mbcsCount);
	QFETCH(int, unicodeCount);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	QByteArray bytes = orderedHeader(8, mbcsCount, bigEndian);
	for (int i = 0; i < mbcsCount; ++i)
		bytes += orderedRecord(8, bigEndian, "mbcs" + QByteArray::number(i), "old", 1);
	bytes += orderedUnicodeHeader(unicodeCount, bigEndian);
	for (int i = 0; i < unicodeCount; ++i)
	{
		QByteArray record = orderedRecord(16, bigEndian, QString::fromUtf8("東京").toUtf8() + QByteArray::number(i), "new", 2 + i);
		if (i == 1)
			record.replace(0, 32, orderedMob(16, bigEndian, true)); // identity absent from MBCS
		bytes += record;
	}
	bool ok = false;
	const QString path = writePmr(tmp.path() + "/sets.pmr", bytes);
	const auto entries = PmrParser::parse(path, &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), unicodeCount ? unicodeCount : mbcsCount);
	if (unicodeCount)
	{
		QCOMPARE(entries[0].fileName, QString::fromUtf8("東京0"));
		QCOMPARE(entries[0].project, QStringLiteral("new"));
		QCOMPARE(entries[0].fileModifiedSecs, 2u);
		if (unicodeCount == 2)
		{
			QCOMPARE(entries[1].mobId, MobId::format(masterMob()));
			QCOMPARE(entries[1].fileName, QString::fromUtf8("東京1"));
			QCOMPARE(entries[1].fileModifiedSecs, 3u);
		}
	}
	else
		QCOMPARE(entries[0].fileName, QStringLiteral("mbcs0"));
	const auto index = PmrParser::buildFileMap(path, &ok);
	QVERIFY(ok);
	QCOMPARE(index.size(), entries.size());
}

void TestPmrParser::every_truncated_boundary_reports_failure_data()
{
	QTest::addColumn<qint32>("version");
	QTest::addColumn<bool>("bigEndian");
	for (qint32 version : {1, 2, 8})
		for (bool bigEndian : {false, true})
			QTest::newRow(qPrintable(QString::number(version) + (bigEndian ? "-BE" : "-LE"))) << version << bigEndian;
}

void TestPmrParser::every_truncated_boundary_reports_failure()
{
	QFETCH(qint32, version);
	QFETCH(bool, bigEndian);
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray base = orderedHeader(version, 1, bigEndian) + orderedRecord(version, bigEndian, "record.media", "project", 0x12345678);
	const QByteArray complete = base + orderedUnicodeHeader(1, bigEndian) + orderedRecord(16, bigEndian, QString::fromUtf8("日本.media").toUtf8(), "unicode", 0x87654321);
	for (qsizetype length = 0; length < complete.size(); ++length)
	{
		// An old-style PMR ending exactly after the first complete set is valid.
		const bool expectedOk = length == base.size();
		if (!expectedOk)
			QTest::ignoreMessage(QtWarningMsg, QRegularExpression(".*"));
		bool ok = true;
		const auto entries = PmrParser::parse(writePmr(tmp.path() + "/truncated.pmr", complete.first(length)), &ok);
		QCOMPARE_EQ(ok, expectedOk);
		if (length == base.size() - 1 || length == base.size() - 2)
		{
			QVERIFY(!entries.isEmpty());
			QCOMPARE(entries.last().fileModifiedSecs, 0u);
			QVERIFY(entries.last().masterMobId.isEmpty());
		}
	}
	bool ok = false;
	QCOMPARE(PmrParser::parse(writePmr(tmp.path() + "/complete.pmr", complete), &ok).size(), 1);
	QVERIFY(ok);
}

void TestPmrParser::project_lengths_are_checked()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (const bool bigEndian : {false, true})
	{
		for (int length : {0, 63, 64, 1023, 1024, 65534})
		{
			const QByteArray bytes = orderedHeader(8, 1, bigEndian) + orderedRecord(8, bigEndian, "file.media", QByteArray(length, 'p'));
			bool ok = true;
			const auto entries = PmrParser::parse(writePmr(tmp.path() + "/project.pmr", bytes), &ok);
			QCOMPARE(ok, length < 64);
			if (ok)
				QCOMPARE(entries[0].project.size(), length);
		}
		// The previous parser skipped an invalid project length and attached
		// these next bytes as master/timestamp, falsely reporting success.
		QByteArray bytes = orderedHeader(8, 1, bigEndian) + orderedMob(8, bigEndian);
		ordered16(bytes, 1, bigEndian);
		bytes += 'f';
		ordered16(bytes, 1024, bigEndian);
		bytes += orderedMob(8, bigEndian, true);
		ordered32(bytes, 0x12345678, bigEndian);
		bool ok = true;
		QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/missing-project.pmr", bytes), &ok).isEmpty());
		QVERIFY(!ok);
	}
}

void TestPmrParser::unicode_framing_and_encoding_are_checked()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (const bool bigEndian : {false, true})
	{
		const QByteArray header = orderedHeader(8, 0, bigEndian) + orderedUnicodeHeader(1, bigEndian);
		for (int kind = 0; kind < 5; ++kind)
		{
			QByteArray record = orderedRecord(16, bigEndian, "name.media");
			if (kind == 0)
				record[34] = 1; // required first reserved byte
			else if (kind == 1 || kind == 2)
			{
				QByteArray length;
				ordered16(length, kind == 1 ? 0 : 1, bigEndian);
				record.replace(32, 2, length); // too small for marker
			}
			else if (kind == 3)
				record = orderedRecord(16, bigEndian, QByteArray("bad\xc3", 4)); // incomplete UTF-8
			else
				record = orderedRecord(16, bigEndian, QByteArray(1024, 'a')); // input capacity
			bool ok = true;
			QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/bad-unicode.pmr", header + record), &ok).isEmpty());
			QVERIFY(!ok);
		}
		// The actual reader checks only the first of the two reserved bytes.
		QByteArray record = orderedRecord(16, bigEndian, "name.media");
		record[35] = 1;
		bool ok = false;
		const auto entries = PmrParser::parse(writePmr(tmp.path() + "/reserved.pmr", header + record), &ok);
		QVERIFY(ok);
		QCOMPARE(entries[0].fileName, QStringLiteral("name.media"));
	}
}

void TestPmrParser::filename_limits_and_null_strings()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (const qint32 version : {1, 2, 8, 16})
	{
		const QByteArray header = version == 16 ? pmrHeader(0) + unicodeHeader(3) : pmrHeader(3, version);
		const QByteArray bytes = header + orderedRecord(version, false, QByteArray(255, 'a')) + orderedRecord(version, false, QByteArray(256, 'b')) + orderedRecord(version, false, "next.media");
		bool ok = false;
		auto entries = PmrParser::parse(writePmr(tmp.path() + "/names.pmr", bytes), &ok);
		QVERIFY(ok);
		QCOMPARE(entries.size(), 2); // invalid long name skipped; following record survives
		QCOMPARE(entries[0].fileName.size(), 255);
		QCOMPARE(entries[1].fileName, QStringLiteral("next.media"));

		QByteArray nullName = orderedMob(version, false);
		u16le(nullName, 0xffff);
		if (version != 1)
		{
			u16le(nullName, 0xffff); // null project is also a valid counted string
			nullName += orderedMob(version, false, true);
		}
		u32le(nullName, 1);
		const QByteArray nullHeader = version == 16 ? pmrHeader(0) + unicodeHeader(2) : pmrHeader(2, version);
		entries = PmrParser::parse(writePmr(tmp.path() + "/null-name.pmr",
											nullHeader + nullName + orderedRecord(version, false, "next.media")),
								   &ok);
		QVERIFY(ok);
		QCOMPARE(entries.size(), 1);
		QCOMPARE(entries[0].fileName, QStringLiteral("next.media"));
	}
	QByteArray record = fileMob();
	u16le(record, 1);
	record += 'f';
	u16le(record, 0xffff);
	record += masterRecord(masterMob(), 99);
	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/null-project.pmr", pmrHeader(1) + record), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QVERIFY(entries[0].project.isEmpty());
	QCOMPARE(entries[0].fileModifiedSecs, 99u);
}

void TestPmrParser::unicode_filename_limit_counts_utf16_units()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString bmpName(255, QChar(0x65e5)); // 765 UTF-8 bytes, 255 UTF-16 units
	const QString emoji = QString::fromUtf8("😀");
	const QString astralName = emoji.repeated(127) + QLatin1Char('x');
	QCOMPARE(astralName.size(), 255);
	QByteArray bytes = pmrHeader(0) + unicodeHeader(4) + orderedRecord(16, false, bmpName.toUtf8()) + orderedRecord(16, false, astralName.toUtf8()) + orderedRecord(16, false, QString(256, QChar(0x65e5)).toUtf8()) + orderedRecord(16, false, emoji.repeated(128).toUtf8());
	bool ok = false;
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/unicode-length.pmr", bytes), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 2);
	QCOMPARE(entries[0].fileName, bmpName);
	QCOMPARE(entries[1].fileName, astralName);
}

void TestPmrParser::null_identities_are_explicit()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	for (const qint32 version : {2, 8})
	{
		const QByteArray nullId(version == 2 ? 8 : 32, '\0');
		bool ok = false;
		const QByteArray nullMaster = pmrHeader(1, version) + fileRecord(orderedMob(version, false), "file.media", "project") + masterRecord(nullId, 99);
		const auto entries = PmrParser::parse(writePmr(tmp.path() + "/null-master.pmr", nullMaster), &ok);
		QVERIFY(ok);
		QCOMPARE(entries.size(), 1);
		QVERIFY(entries[0].masterMobId.isEmpty());
		QCOMPARE(entries[0].fileModifiedSecs, 99u);

		const QByteArray nullFile = pmrHeader(1, version) + fileRecord(nullId, "file.media", "project") + masterRecord(orderedMob(version, false, true), 99);
		QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/null-file.pmr", nullFile), &ok).isEmpty());
		QVERIFY(!ok);
	}
}

QTEST_APPLESS_MAIN(TestPmrParser)
#include "tst_pmrparser.moc"
