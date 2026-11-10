// Literal identity examples check Avid's legacy ID wrapper. General OMF UIDs
// keep all three words, and full modern IDs remain unchanged.


#include "media/mob_id_text.h"
#include "media/omf_uid.h"

#include <QByteArray>
#include <QTest>


// MARK: - Test fixture

class TestOmfUid : public QObject
{
	Q_OBJECT

private slots:
	void toMobIdBytes_is_prefix_core_suffix();
	void toMobIdBytes_does_not_swap_middle_fields();
	void toIdText_12_bytes_wraps_the_core();
	void toIdText_32_bytes_formats_unchanged();
	void toIdText_other_widths_are_empty();
	void isWrappedOmfId_recognises_only_the_wrap();
	void omf_form_never_collides_with_mxf_form();
};

// MARK: - Canonical identity conversion

void TestOmfUid::toMobIdBytes_is_prefix_core_suffix()
{
	const unsigned char core[OmfUid::kPmrSize] = {0x74, 0x29, 0x97, 0x6a, 0x70, 0x39, 0x70, 0x47};
	const auto wrapped = OmfUid::toMobIdBytes(core);

	// FileMobId recorded for the WAVE fixture in Avid's PMR Unicode set.
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(wrapped.data()), static_cast<qsizetype>(wrapped.size())),
			 QByteArray::fromHex("060a2b340101010101010f00130000007429976a70397047060e2b347f7f2a80"));
	QCOMPARE(OmfUid::toMobIdText(core),
			 QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"));
}



void TestOmfUid::toMobIdBytes_does_not_swap_middle_fields()
{
	// The wrapper copies bytes 16..23 unchanged; MXF material-byte swapping does not apply.
	const unsigned char core[OmfUid::kPmrSize] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
	const auto wrapped = OmfUid::toMobIdBytes(core);
	for (int i = 0; i < OmfUid::kPmrSize; ++i)
		QCOMPARE(int(wrapped[16 + i]), int(core[i]));
}

void TestOmfUid::toIdText_12_bytes_wraps_the_core()
{
	// The captured Avid prefix-42 spelling has the exact PMR bridge.
	const QByteArray uid = QByteArray::fromHex("2a000000"
											   "7429976a70397047");
	QCOMPARE(OmfUid::toIdText(uid),
			 QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"));

	// A different prefix is a different UID. General OMF writers use all
	// three words; dropping the first word merges unrelated clips.
	const QByteArray uid2 = QByteArray::fromHex("00000000"
												"7429976a70397047");
	QCOMPARE(OmfUid::toIdText(uid2), QStringLiteral("omf:000000007429976a70397047"));
	QVERIFY(OmfUid::toIdText(uid2) != OmfUid::toIdText(uid));
	QVERIFY(OmfUid::toIdText(QByteArray::fromHex("010000007429976a70397047")) != OmfUid::toIdText(uid2));
}

void TestOmfUid::toIdText_32_bytes_formats_unchanged()
{
	// MC 2026 writes a real UMID on the physical mob of an OMF file; it
	// must come out exactly as MobId::format would render it.
	const QByteArray umid = QByteArray::fromHex("060a2b3401010105"
												"01010f1013000000"
												"a4bb7f1311399006"
												"6d01ce4ff0f5d57a");
	QCOMPARE(OmfUid::toIdText(umid),
			 QStringLiteral("060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a"));
	QVERIFY(!OmfUid::isWrappedOmfId(OmfUid::toIdText(umid)));
}

void TestOmfUid::toIdText_other_widths_are_empty()
{
	QCOMPARE(OmfUid::toIdText(QByteArray(12, '\0')), QString());
	QCOMPARE(OmfUid::toIdText(QByteArray()), QString());
	QCOMPARE(OmfUid::toIdText(QByteArray(8, 'x')), QString());
	QCOMPARE(OmfUid::toIdText(QByteArray(11, 'x')), QString());
	QCOMPARE(OmfUid::toIdText(QByteArray(13, 'x')), QString());
	QCOMPARE(OmfUid::toIdText(QByteArray(16, 'x')), QString());
	QCOMPARE(OmfUid::toIdText(QByteArray(31, 'x')), QString());
	QCOMPARE(OmfUid::toIdText(QByteArray(33, 'x')), QString());
}

// MARK: - Legacy identity qualification

void TestOmfUid::isWrappedOmfId_recognises_only_the_wrap()
{
	QVERIFY(OmfUid::isWrappedOmfId(
		QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80")));
	QVERIFY(OmfUid::isWrappedOmfId(
		QStringLiteral("060a2b3401010101.01010f0013000000.0000000000000000.060e2b347f7f2a80")));

	// Wrong suffix, wrong prefix, wrong length, empty: all rejected.
	QVERIFY(!OmfUid::isWrappedOmfId(
		QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a81")));
	QVERIFY(!OmfUid::isWrappedOmfId(
		QStringLiteral("060a2b3401010105.01010f0013000000.7429976a70397047.060e2b347f7f2a80")));
	QVERIFY(!OmfUid::isWrappedOmfId(QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047")));
	QVERIFY(!OmfUid::isWrappedOmfId(QString()));
}

void TestOmfUid::omf_form_never_collides_with_mxf_form()
{
	// The genuine msmMMOB_round3.mdb ID below shares the OMF wrapper's prefix.
	// Its different suffix must prevent legacy matching in either material byte order.
	const QString mxf =
		QStringLiteral("060a2b3401010105.01010f1013000000.a4bb7f1311399006.6d01ce4ff0f5d57a");
	QVERIFY(!OmfUid::isWrappedOmfId(mxf));
	QVERIFY(!OmfUid::isWrappedOmfId(MobId::swapMaterialByteOrder(mxf)));
	const QString mxfSharedPrefix =
		QStringLiteral("060a2b3401010101.01010f0013000000.38d60dbefb6d163b.8e40da9649f531f4");
	QVERIFY(!OmfUid::isWrappedOmfId(mxfSharedPrefix));
	QVERIFY(!OmfUid::isWrappedOmfId(MobId::swapMaterialByteOrder(mxfSharedPrefix)));

	const unsigned char core[OmfUid::kPmrSize] = {0xa4, 0xbb, 0x7f, 0x13, 0x11, 0x39, 0x90, 0x06};
	const QString omf = OmfUid::toMobIdText(core);
	QVERIFY(OmfUid::isWrappedOmfId(omf));
	QVERIFY(!omf.startsWith(QStringLiteral("060a2b3401010105")));
	// Material swapping leaves the wrapper's prefix and suffix intact.
	QVERIFY(OmfUid::isWrappedOmfId(MobId::swapMaterialByteOrder(omf)));
}

QTEST_APPLESS_MAIN(TestOmfUid)
#include "tst_omf_uid.moc"
