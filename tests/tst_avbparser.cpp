// Drives AvbParser over hand-crafted Bento buffers. Every byte is
// controlled, so we can hit the exact edges: valid SMPTE UMID vs valid Avid
// MOB, Bento sentinels that must be filtered, ASCII hex-string MOBs, boundary
// reads at EOF, and truncation. No binary fixture to maintain.

#include "avbparser.h"
#include "mobid.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <array>

namespace
{
	// The 12-byte header every real Avid bin starts with. The literal is split so
	// `\x00` can't swallow the following 'D' as a hex digit — the same trick
	// avbparser.cpp uses.
	QByteArray bentoHeader()
	{
		return QByteArray("\x06\x00"
						  "DomainDJBO",
						  12);
	}

	QString writeBin(const QString &path, const QByteArray &body, bool withHeader = true)
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QFile f(path);
		if (f.open(QIODevice::WriteOnly))
		{
			if (withHeader)
				f.write(bentoHeader());
			f.write(body);
			f.close();
		}
		return path;
	}

	// The two strings insertBothForms() drops in for every MOB it finds: the
	// canonical dotted form and its middle-field-swapped twin.
	QString formOf(const QByteArray &raw32)
	{
		return MobId::format(reinterpret_cast<const unsigned char *>(raw32.constData()));
	}
	QString swappedFormOf(const QByteArray &raw32)
	{
		std::array<unsigned char, MobId::kRawSize> sw{};
		MobId::swapMiddleFields(reinterpret_cast<const unsigned char *>(raw32.constData()), sw.data());
		return MobId::format(sw.data());
	}

	// A valid Avid MOB: 06 0A 2B 34, then byte[4]=01, byte[12]=44, byte[20]=48.
	QByteArray avidMob()
	{
		return QByteArray::fromHex("060a2b340101010501010f1044000000785634124899aabb0123456789abcdef");
	}

	// A valid SMPTE UMID: 06 0E 2B 34, then byte[4]=04, byte[5]=01.
	QByteArray smpteUmid()
	{
		return QByteArray::fromHex("060e2b3404010d0102010101010000112233445566778899aabbccddeeff1020");
	}
} // namespace

class TestAvbParser : public QObject
{
	Q_OBJECT
private slots:
	void missing_file_returns_invalid();
	void non_bento_header_returns_invalid();
	void binary_avid_mob_found_both_endian_forms();
	void binary_smpte_umid_found();
	void bento_sentinel_is_filtered_out();
	void hex_string_mob_is_decoded();

	// Hex is hex regardless of case: no bin surveyed writes uppercase, but a
	// MOB that arrived as "060A2B34..." was invisible to the text pass (the
	// lowercase-only character test skipped it), silently under-matching the
	// bin filter. The decoder itself always accepted either case.
	void uppercase_hex_string_mob_is_decoded();

	void mob_at_exact_eof_boundary_is_read();
	void marker_in_tail_is_not_over_read();

	// Bins written by pre-Intel-era Mac Media Composer are BIG-endian:
	// header `00 06` + "Domain" + unreversed "OBJD" (confirmed by two
	// independent reverse-engineering projects, pyavb and libavid). The
	// LE-only header check silently rejected them. The MOB scans them-
	// selves are endian-agnostic: insertBothForms records both middle-
	// field byte orders for every MOB found.
	void big_endian_legacy_bin_is_parsed();
};

void TestAvbParser::missing_file_returns_invalid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const AvbBin bin = AvbParser::parse(tmp.path() + "/does-not-exist.avb");
	QVERIFY(!bin.valid);
	QVERIFY(bin.mobIds.isEmpty());
}

void TestAvbParser::non_bento_header_returns_invalid()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path =
		writeBin(tmp.path() + "/fake.avb", QByteArray("this is not an Avid bin at all"), false);
	const AvbBin bin = AvbParser::parse(path);
	QVERIFY(!bin.valid);
	QVERIFY(bin.mobIds.isEmpty());
}

void TestAvbParser::binary_avid_mob_found_both_endian_forms()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray mob = avidMob();
	// Leading non-0x06 padding; the MOB sits mid-buffer.
	const QByteArray body = QByteArray(4, '\x11') + mob + QByteArray(4, '\x22');
	const AvbBin bin = AvbParser::parse(writeBin(tmp.path() + "/a.avb", body));

	QVERIFY(bin.valid);
	QVERIFY2(bin.mobIds.contains(formOf(mob)), "canonical form missing");
	QVERIFY2(bin.mobIds.contains(swappedFormOf(mob)), "byte-swapped twin missing");
	QCOMPARE(bin.mobIds.size(), 2);
}

void TestAvbParser::binary_smpte_umid_found()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QByteArray umid = smpteUmid();
	const AvbBin bin = AvbParser::parse(writeBin(tmp.path() + "/u.avb", umid));

	QVERIFY(bin.valid);
	QVERIFY(bin.mobIds.contains(formOf(umid)));
	QVERIFY(bin.mobIds.contains(swappedFormOf(umid)));
}

void TestAvbParser::bento_sentinel_is_filtered_out()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// 06 0E 2B 34 prefix, but byte[4]=7F not 04 — a Bento sentinel, not a UMID.
	const QByteArray sentinel =
		QByteArray::fromHex("060e2b347f7f2a80111111111111111111111111111111111111111111111111");
	const AvbBin bin = AvbParser::parse(writeBin(tmp.path() + "/s.avb", sentinel));

	QVERIFY(bin.valid);
	QVERIFY2(!bin.mobIds.contains(formOf(sentinel)), "sentinel must not be treated as a MOB");
	QVERIFY(bin.mobIds.isEmpty());
}

void TestAvbParser::hex_string_mob_is_decoded()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// A 64-char ASCII hex MOB (Avid also writes MOBs as text). Non-hex bytes
	// on either side so the scanned run is exactly the 64 chars.
	const QByteArray hexAscii =
		"060a2b340101010501010f10130000004a507dea741106907a361e6a605d3613";
	const QByteArray body = QByteArray(1, '\x00') + hexAscii + QByteArray(1, '\x00');
	const AvbBin bin = AvbParser::parse(writeBin(tmp.path() + "/h.avb", body));

	const QByteArray decoded = QByteArray::fromHex(hexAscii);
	QVERIFY(bin.valid);
	QVERIFY(bin.mobIds.contains(formOf(decoded)));
	QVERIFY(bin.mobIds.contains(swappedFormOf(decoded)));
}

void TestAvbParser::uppercase_hex_string_mob_is_decoded()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// Same MOB as hex_string_mob_is_decoded, upper-cased. fromHex decodes
	// either case, so both spellings must resolve to the same canonical forms.
	const QByteArray hexAscii =
		"060A2B340101010501010F10130000004A507DEA741106907A361E6A605D3613";
	const QByteArray body = QByteArray(1, '\x00') + hexAscii + QByteArray(1, '\x00');
	const AvbBin bin = AvbParser::parse(writeBin(tmp.path() + "/H.avb", body));

	const QByteArray decoded = QByteArray::fromHex(hexAscii);
	QVERIFY(bin.valid);
	QVERIFY2(bin.mobIds.contains(formOf(decoded)), "uppercase hex MOB not found");
	QVERIFY(bin.mobIds.contains(swappedFormOf(decoded)));
}

void TestAvbParser::mob_at_exact_eof_boundary_is_read()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// The MOB is the final 32 bytes: exercises the `end = data + size - 32`
	// boundary — p == end must still read the full 32 bytes.
	const QByteArray mob = avidMob();
	const QByteArray body = QByteArray(4, '\x11') + mob; // no trailing padding
	const AvbBin bin = AvbParser::parse(writeBin(tmp.path() + "/eof.avb", body));

	QVERIFY(bin.valid);
	QVERIFY(bin.mobIds.contains(formOf(mob)));
}

void TestAvbParser::marker_in_tail_is_not_over_read()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	// A 06 0A 2B 34 marker with fewer than 32 bytes after it: can't be a full
	// MOB, must be ignored without reading past the buffer.
	const QByteArray body = QByteArray(8, '\x11') + QByteArray::fromHex("060a2b34") +
							QByteArray(16, '\x11');
	const AvbBin bin = AvbParser::parse(writeBin(tmp.path() + "/tail.avb", body));

	QVERIFY(bin.valid);
	QVERIFY(bin.mobIds.isEmpty());
}

void TestAvbParser::big_endian_legacy_bin_is_parsed()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());

	// Big-endian Bento header: byte-order mark 00 06, "Domain", fourcc
	// UNreversed. Body carries one valid Avid MOB.
	const QByteArray beHeader = QByteArray("\x00\x06"
										   "DomainOBJD",
										   12);
	const QByteArray mob = avidMob();
	const QString path = tmp.path() + QStringLiteral("/legacy.avb");
	QFile f(path);
	QVERIFY(f.open(QIODevice::WriteOnly));
	f.write(beHeader);
	f.write(QByteArray(4, '\x11'));
	f.write(mob);
	f.close();

	const AvbBin bin = AvbParser::parse(path);
	QVERIFY2(bin.valid, "big-endian bin rejected as not-a-bin");
	QVERIFY(bin.mobIds.contains(formOf(mob)));
	QVERIFY(bin.mobIds.contains(swappedFormOf(mob)));
}

QTEST_APPLESS_MAIN(TestAvbParser)
#include "tst_avbparser.moc"