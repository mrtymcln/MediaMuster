// OMF-era (legacy Avid media, pre-MXF). OmfUid is the bridge from the
// 8-byte MOB a version-2 msmFMID.pmr stores (and the 12-byte omfi:UID an
// OMF file or OMF-era MDB carries) to the 32-byte canonical hex every
// consumer keys on. The wrap is Avid's own: its PMR Unicode set and its
// .avb bins write the same prefix + core + suffix, so the tests pin the
// constants against the real bytes in tests/fixtures/omf rather than
// against a value someone typed in.

#include "mobid.h"
#include "omfuid.h"

#include <QByteArray>
#include <QFile>
#include <QTest>

namespace
{
	QByteArray readAll(const QString &path)
	{
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly))
			return {};
		return f.readAll();
	}

	// A version-2 PMR: 12-byte header, then the first pair's MASTER record
	// whose first 8 bytes are its MOB.
	constexpr int kPmrV2HeaderSize = 12;
} // namespace

class TestOmfUid : public QObject
{
	Q_OBJECT

private slots:
	void wrap8_is_prefix_core_suffix();
	void wrap8_matches_the_unicode_set_in_the_real_pmrs();
	void wrap8_does_not_swap_middle_fields();
	void canonicalHex_12_bytes_wraps_the_core();
	void canonicalHex_32_bytes_formats_unchanged();
	void canonicalHex_other_widths_are_empty();
	void isOmfForm_recognises_only_the_wrap();
	void omf_form_never_collides_with_mxf_form();
};

void TestOmfUid::wrap8_is_prefix_core_suffix()
{
	const unsigned char core[OmfUid::kPmrSize] = {0x74, 0x29, 0x97, 0x6a, 0x70, 0x39, 0x70, 0x47};
	const auto wrapped = OmfUid::wrap8(core);

	QCOMPARE(int(wrapped.size()), MobId::kRawSize);
	QVERIFY(std::equal(std::begin(OmfUid::kPrefix), std::end(OmfUid::kPrefix), wrapped.begin()));
	QVERIFY(std::equal(std::begin(core), std::end(core), wrapped.begin() + sizeof OmfUid::kPrefix));
	QVERIFY(std::equal(std::begin(OmfUid::kSuffix), std::end(OmfUid::kSuffix),
					   wrapped.begin() + sizeof OmfUid::kPrefix + OmfUid::kPmrSize));

	// The worked example from the plan: the wav file mob in MC 26.8's
	// own PMR Unicode set.
	QCOMPARE(OmfUid::canonicalFromPmr8(core),
			 QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"));
}

void TestOmfUid::wrap8_matches_the_unicode_set_in_the_real_pmrs()
{
	// Both fixtures: take the first MASTER record's 8-byte MOB, wrap it,
	// and require the 32 wrapped bytes to occur verbatim later in the
	// file — the Unicode set is where Avid writes the same MOB in the
	// wrapped form. If kPrefix or kSuffix were wrong by a byte, nothing
	// would match.
	const QString fixtures = QStringLiteral(FIXTURES_DIR "/omf/");
	for (const char *pmr : {"mc2026_audio/msmFMID.pmr", "avid_supporting/msmFMID.pmr"})
	{
		const QByteArray data = readAll(fixtures + QLatin1String(pmr));
		QVERIFY2(data.size() > kPmrV2HeaderSize + OmfUid::kPmrSize, pmr);

		const auto *first = reinterpret_cast<const unsigned char *>(data.constData()) + kPmrV2HeaderSize;
		const auto wrapped = OmfUid::wrap8(first);
		const QByteArray needle(reinterpret_cast<const char *>(wrapped.data()), MobId::kRawSize);

		const qsizetype at = data.indexOf(needle);
		QVERIFY2(at > kPmrV2HeaderSize + OmfUid::kPmrSize, pmr);
		// And it sits in the Unicode set, not the fixed section: the
		// 8-byte core alone appears earlier (in the record we read).
		QCOMPARE(data.indexOf(QByteArray(reinterpret_cast<const char *>(first), OmfUid::kPmrSize)),
				 qsizetype(kPmrV2HeaderSize));
		QVERIFY(OmfUid::isOmfForm(OmfUid::canonicalFromPmr8(first)));
	}
}

void TestOmfUid::wrap8_does_not_swap_middle_fields()
{
	// The wrap is Avid's layout, not an MXF UMID: the core lands at bytes
	// 16..23 in the order given. MobId::toPmrForm's swap must NOT be part
	// of it (the PMR Unicode set already holds this exact order).
	const unsigned char core[OmfUid::kPmrSize] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
	const auto wrapped = OmfUid::wrap8(core);
	for (int i = 0; i < OmfUid::kPmrSize; ++i)
		QCOMPARE(int(wrapped[16 + i]), int(core[i]));
}

void TestOmfUid::canonicalHex_12_bytes_wraps_the_core()
{
	// The captured Avid prefix-42 spelling has the exact PMR bridge.
	const QByteArray uid = QByteArray::fromHex("2a000000"
											   "7429976a70397047");
	QCOMPARE(uid.size(), OmfUid::kUidSize);
	QCOMPARE(OmfUid::canonicalHex(uid),
			 QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"));

	// A different prefix is a different UID. General OMF writers use all
	// three words; dropping the first word merges unrelated clips.
	const QByteArray uid2 = QByteArray::fromHex("00000000"
												"7429976a70397047");
	QCOMPARE(OmfUid::canonicalHex(uid2), QStringLiteral("omf:000000007429976a70397047"));
	QVERIFY(OmfUid::canonicalHex(uid2) != OmfUid::canonicalHex(uid));
	QVERIFY(OmfUid::canonicalHex(QByteArray::fromHex("010000007429976a70397047")) != OmfUid::canonicalHex(uid2));
}

void TestOmfUid::canonicalHex_32_bytes_formats_unchanged()
{
	// MC 2026 writes a real UMID on the physical mob of an OMF file; it
	// must come out exactly as MobId::format would render it.
	const QByteArray umid = QByteArray::fromHex("060a2b3401010105"
												"01010f1013000000"
												"a4bb7f1311399006"
												"6d01ce4ff0f5d57a");
	QCOMPARE(umid.size(), MobId::kRawSize);
	QCOMPARE(OmfUid::canonicalHex(umid), MobId::format(umid));
	QCOMPARE(OmfUid::canonicalHex(umid),
			 QStringLiteral("060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a"));
	QVERIFY(!OmfUid::isOmfForm(OmfUid::canonicalHex(umid)));
}

void TestOmfUid::canonicalHex_other_widths_are_empty()
{
	QCOMPARE(OmfUid::canonicalHex(QByteArray(12, '\0')), QString());
	QCOMPARE(OmfUid::canonicalHex(QByteArray()), QString());
	QCOMPARE(OmfUid::canonicalHex(QByteArray(8, 'x')), QString());
	QCOMPARE(OmfUid::canonicalHex(QByteArray(11, 'x')), QString());
	QCOMPARE(OmfUid::canonicalHex(QByteArray(13, 'x')), QString());
	QCOMPARE(OmfUid::canonicalHex(QByteArray(16, 'x')), QString());
	QCOMPARE(OmfUid::canonicalHex(QByteArray(31, 'x')), QString());
	QCOMPARE(OmfUid::canonicalHex(QByteArray(33, 'x')), QString());
}

void TestOmfUid::isOmfForm_recognises_only_the_wrap()
{
	QVERIFY(OmfUid::isOmfForm(
		QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80")));
	QVERIFY(OmfUid::isOmfForm(
		QStringLiteral("060a2b3401010101.01010f0013000000.0000000000000000.060e2b347f7f2a80")));

	// Wrong suffix, wrong prefix, wrong length, empty: all rejected.
	QVERIFY(!OmfUid::isOmfForm(
		QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a81")));
	QVERIFY(!OmfUid::isOmfForm(
		QStringLiteral("060a2b3401010105.01010f0013000000.7429976a70397047.060e2b347f7f2a80")));
	QVERIFY(!OmfUid::isOmfForm(QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047")));
	QVERIFY(!OmfUid::isOmfForm(QString()));
}

void TestOmfUid::omf_form_never_collides_with_mxf_form()
{
	// The suffix is the discriminator, not the prefix. A SMPTE UMID (most
	// MXF-era keys) differs in the prefix too, but Avid's MXF-era writer
	// also mints MobIDs carrying the wrap's exact 16-byte prefix — the
	// second key below is one of four in tests/fixtures/corpus_headers/
	// msmMMOB_round3.mdb — with a per-host random where the wrap has the
	// AAF "prefix 42" marker. Neither reads as OMF form in either byte
	// order (toPmrForm only touches bytes 16..23), and the wrap of any
	// core never reads as a UMID.
	const QString mxf =
		QStringLiteral("060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a");
	QVERIFY(!OmfUid::isOmfForm(mxf));
	QVERIFY(!OmfUid::isOmfForm(MobId::toPmrForm(mxf)));
	const QString mxfSharedPrefix =
		QStringLiteral("060a2b3401010101.01010f0013000000.38d60dbefb6d163b.8e40da9649f531f4");
	QVERIFY(!OmfUid::isOmfForm(mxfSharedPrefix));
	QVERIFY(!OmfUid::isOmfForm(MobId::toPmrForm(mxfSharedPrefix)));

	const unsigned char core[OmfUid::kPmrSize] = {0xa4, 0xbb, 0x7f, 0x13, 0x11, 0x39, 0x90, 0x06};
	const QString omf = OmfUid::canonicalFromPmr8(core);
	QVERIFY(OmfUid::isOmfForm(omf));
	QVERIFY(!omf.startsWith(QStringLiteral("060a2b3401010105")));
	// The swap maps an OMF key to another OMF key (prefix and suffix are
	// outside bytes 16..23), so a consumer that normalises through
	// toPmrForm cannot turn one into something that looks MXF-era.
	QVERIFY(OmfUid::isOmfForm(MobId::toPmrForm(omf)));
	QVERIFY(!MobId::isAllZero(omf));
}

QTEST_APPLESS_MAIN(TestOmfUid)
#include "tst_omfuid.moc"
