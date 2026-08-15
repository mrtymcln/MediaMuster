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

	// MASTER record: 32-byte MOB | 4 trailer bytes.
	QByteArray masterRecord(const QByteArray &mob)
	{
		return mob + QByteArray(4, '\0');
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

QTEST_APPLESS_MAIN(TestPmrParser)
#include "tst_pmrparser.moc"