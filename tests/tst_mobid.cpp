#include "mobid.h"

#include <QByteArray>
#include <QTest>
#include <algorithm>

class TestMobId : public QObject
{
	Q_OBJECT
private slots:
	void format_renders_canonical_dotted_hex();
	void format_too_short_buffer_returns_empty();
	void isAllZero_detects_all_zero_pattern();
	void isAllZero_rejects_real_mob();
	void isAllZero_rejects_empty();
	void swapMaterialByteOrder_swaps_middle_fields();
	void swapMaterialByteOrder_is_involution();
	void swapMaterialByteOrder_rejects_malformed();
};

void TestMobId::format_renders_canonical_dotted_hex()
{
	unsigned char raw[MobId::kRawSize] = {
		0x00,
		0x11,
		0x22,
		0x33,
		0x44,
		0x55,
		0x66,
		0x77,
		0x88,
		0x99,
		0xaa,
		0xbb,
		0xcc,
		0xdd,
		0xee,
		0xff,
		0x01,
		0x23,
		0x45,
		0x67,
		0x89,
		0xab,
		0xcd,
		0xef,
		0xfe,
		0xdc,
		0xba,
		0x98,
		0x76,
		0x54,
		0x32,
		0x10,
	};
	QCOMPARE(MobId::format(raw), QStringLiteral("0011223344556677.8899aabbccddeeff."
												"0123456789abcdef.fedcba9876543210"));
	std::fill_n(raw, MobId::kRawSize, 0xab);
	const QString lowercase = MobId::format(raw);
	QCOMPARE(lowercase, lowercase.toLower());
}

void TestMobId::format_too_short_buffer_returns_empty()
{
	QCOMPARE(MobId::format(QByteArray()), QString());
	QCOMPARE(MobId::format(QByteArray("too short, only 26 bytes!!")), QString());
}

void TestMobId::isAllZero_detects_all_zero_pattern()
{
	QVERIFY(MobId::isAllZero(QStringLiteral("0000000000000000.0000000000000000."
											"0000000000000000.0000000000000000")));
}

void TestMobId::isAllZero_rejects_real_mob()
{
	QVERIFY(!MobId::isAllZero(QStringLiteral("060a2b3401010105.01010f1013000000."
											 "a4bb7f1311399006.6d01ce4ff0f5d57a")));
}

void TestMobId::isAllZero_rejects_empty()
{
	QVERIFY(!MobId::isAllZero(QString()));
}

void TestMobId::swapMaterialByteOrder_swaps_middle_fields()
{
	// Endian swap on bytes [16..23] only.
	const QString avb = QStringLiteral("0011223344556677.8899aabbccddeeff."
									   "0123456789abcdef.0123456789abcdef");
	const QString expected = QStringLiteral("0011223344556677.8899aabbccddeeff."
											"67452301ab89efcd.0123456789abcdef");
	QCOMPARE(MobId::swapMaterialByteOrder(avb), expected);
}

void TestMobId::swapMaterialByteOrder_is_involution()
{
	const QString original = QStringLiteral("060a2b3401010105.01010f1013000000."
											"a4bb7f1311399006.6d01ce4ff0f5d57a");
	QCOMPARE(MobId::swapMaterialByteOrder(MobId::swapMaterialByteOrder(original)), original);
}

void TestMobId::swapMaterialByteOrder_rejects_malformed()
{
	QCOMPARE(MobId::swapMaterialByteOrder(QStringLiteral("not hex at all")), QString());
	QCOMPARE(MobId::swapMaterialByteOrder(QStringLiteral("0011223344556677")), QString());
	QCOMPARE(MobId::swapMaterialByteOrder(QString()), QString());
}

QTEST_APPLESS_MAIN(TestMobId)
#include "tst_mobid.moc"
