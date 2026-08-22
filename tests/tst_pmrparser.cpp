// Unit tests for PmrParser::parse over hand-crafted msmFMID.pmr buffers.
// The PMR is a fixed little-endian layout, so we can assemble exact bytes and
// hit the guards precisely: a normal FILE+MASTER pair, implausible pair counts,
// the FILE/MASTER desync bail, truncation, and the too-small-file reject.

#include "mobid.h"
#include "pmrparser.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QtGlobal>

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
	void zero_pair_count_is_rejected();
	void implausible_pair_count_is_rejected();
	void file_comp_desync_bails_with_what_it_has();
	void truncated_body_stops_cleanly();
	void too_small_file_returns_empty();

	// Integrity guards: an unknown layout version is refused outright, and
	// any record boundary that doesn't start with the Avid MOB prefix
	// (06 0a 2b 34) bails rather than committing garbage — MASTER records
	// previously attached arbitrary bytes as masterMobId unchecked.
	void unsupported_version_is_rejected();

	// Avid checks a magic word (0x000007A9) before the version; a file that
	// is not a PMR at all must be refused before its bytes are read as a
	// version number.
	void wrong_magic_is_refused();

	void corrupt_comp_mob_is_not_attached();
	void corrupt_file_mob_bails_keeping_prior_entries();

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
	// replaces names by FILE-MOB match; a malformed set is ignored.
	void unicode_section_names_take_precedence();
	void malformed_unicode_section_keeps_mbcs_names();
	// The 4 bytes after a MASTER MOB are the essence file's mtime.
	void trailer_is_the_file_modified_time();
	void real_fixture_pmrs_parse_with_unicode_names();
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

void TestPmrParser::zero_pair_count_is_rejected()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// >= 44 bytes so it clears the minimum-size check, but pairCount 0 bails.
	const QByteArray buf = pmrHeader(0) + fileMob();
	QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf)).isEmpty());
}

void TestPmrParser::implausible_pair_count_is_rejected()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray buf = pmrHeader(6'000'000) + fileMob(); // over the 5M ceiling
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
	// A perfectly well-formed pair, but the header claims version 9: the
	// record layout can't be trusted, so nothing may be returned.
	const QByteArray buf = pmrHeader(1, /*version=*/9) +
						   fileRecord(fileMob(), "CLIP.A01.mxf", "block 1729") +
						   masterRecord(masterMob());
	QVERIFY(PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf)).isEmpty());
}

void TestPmrParser::corrupt_comp_mob_is_not_attached()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Valid FILE record, then a MASTER slot holding 32 bytes of garbage.
	// The FILE entry must survive, with NO masterMobId — previously
	// the garbage was formatted and attached without any check.
	const QByteArray buf = pmrHeader(1) + fileRecord(fileMob(), "CLIP.A01.mxf", "proj") +
						   QByteArray(32, 'X') + QByteArray(4, '\0');
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf));

	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("CLIP.A01.mxf"));
	QVERIFY2(entries[0].masterMobId.isEmpty(),
			 qPrintable("garbage attached as comp MOB: " + entries[0].masterMobId));
}

void TestPmrParser::corrupt_file_mob_bails_keeping_prior_entries()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Pair 0 is intact; pair 1's FILE slot starts with garbage instead of
	// a MOB. The parser keeps pair 0 and stops.
	QByteArray buf = pmrHeader(2) + fileRecord(fileMob(), "GOOD.mxf", "proj") +
					 masterRecord(masterMob());
	buf += fileRecord(QByteArray(32, 'Z'), "BAD.mxf", "proj");
	buf += masterRecord(masterMob());
	const auto entries = PmrParser::parse(writePmr(tmp.path() + "/msmFMID.pmr", buf));

	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries[0].fileName, QStringLiteral("GOOD.mxf"));
	QCOMPARE(entries[0].masterMobId, MobId::format(masterMob()));
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
	QCOMPARE(entries[1].project, QString::fromUtf8("caf\xc3\xa9"));	   // café
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

	// Count disagrees with the MBCS set: ignored wholesale.
	QByteArray wrongCount = mbcs + unicodeHeader(2) + unicodeFileRecord(fileMob(), "utf8.mxf", "proj") +
							masterRecord(masterMob());
	bool ok = false;
	auto entries = PmrParser::parse(writePmr(tmp.path() + "/a.pmr", wrongCount), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.size(), 1);
	QCOMPARE(entries.first().fileName, QStringLiteral("mbcs.mxf"));

	// A record without the MOB prefix: the walk stops, MBCS name kept.
	QByteArray badPrefix = mbcs + unicodeHeader(1);
	badPrefix += QByteArray(32, '\x11');
	badPrefix += QByteArray(8, '\0');
	entries = PmrParser::parse(writePmr(tmp.path() + "/b.pmr", badPrefix), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.first().fileName, QStringLiteral("mbcs.mxf"));

	// A FILE MOB the MBCS set doesn't know: nothing to replace, nothing breaks.
	QByteArray otherMob = fileMob();
	otherMob[20] = '\x5a';
	QByteArray unknownMob = mbcs + unicodeHeader(1) + unicodeFileRecord(otherMob, "utf8.mxf", "proj") +
							masterRecord(masterMob());
	entries = PmrParser::parse(writePmr(tmp.path() + "/c.pmr", unknownMob), &ok);
	QVERIFY(ok);
	QCOMPARE(entries.first().fileName, QStringLiteral("mbcs.mxf"));

	// No second set at all (older files): the MBCS names are the names.
	entries = PmrParser::parse(writePmr(tmp.path() + "/d.pmr", mbcs), &ok);
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
	struct Gen { const char *path; int pairs; };
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

QTEST_APPLESS_MAIN(TestPmrParser)
#include "tst_pmrparser.moc"