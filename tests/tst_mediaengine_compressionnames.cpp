// Naming controls use established labels and Avid operating points. These are
// authored inputs to the shared formatter, not claims about binary parsing.
#include "mediaengine/compressionnames_p.h"

#include <QTest>

namespace
{
	MediaEngine::Detail::CompressionFacts legacyDvFacts()
	{
		MediaEngine::Detail::CompressionFacts facts;
		facts.descriptorClass = QStringLiteral("CDCI");
		facts.legacyCompression = QByteArrayLiteral("DV/C");
		facts.legacyResolution = 140;
		facts.codingAbsent = true;
		facts.layout = 1;
		return facts;
	}

	MediaEngine::Detail::CompressionFacts dnxHqxFacts()
	{
		MediaEngine::Detail::CompressionFacts facts;
		facts.codingLabel = QByteArray::fromHex("060e2b340401010a0401020271010000");
		facts.geometry = {1920, 1080};
		facts.rate = {24000, 1001};
		facts.layout = 0;
		facts.depth = 10;
		facts.horizontal = 2;
		facts.vertical = 1;
		return facts;
	}
}

class TestMediaEngineCompressionNames : public QObject
{
	Q_OBJECT
private slots:
	void recorded_dv_labels_data()
	{
		QTest::addColumn<QByteArray>("label");
		QTest::addColumn<QString>("expected");
		QTest::newRow("smpte-ntsc-25-411")
			<< QByteArray::fromHex("060e2b34040101010401020202020100") << QStringLiteral("DV NTSC 25Mbps 4:1:1");
		QTest::newRow("smpte-pal-25-411")
			<< QByteArray::fromHex("060e2b34040101010401020202020200") << QStringLiteral("DV PAL 25Mbps 4:1:1");
		QTest::newRow("smpte-ntsc-50-422")
			<< QByteArray::fromHex("060e2b34040101010401020202020300") << QStringLiteral("DV NTSC 50Mbps 4:2:2");
		QTest::newRow("smpte-pal-50-422")
			<< QByteArray::fromHex("060e2b34040101010401020202020400") << QStringLiteral("DV PAL 50Mbps 4:2:2");
		QTest::newRow("iec-pal-25-420")
			<< QByteArray::fromHex("060e2b34040101010401020202010200") << QStringLiteral("IEC-DV PAL 25Mbps 4:2:0");
	}

	void recorded_dv_labels()
	{
		QFETCH(QByteArray, label);
		QFETCH(QString, expected);
		MediaEngine::Detail::CompressionFacts facts;
		facts.codingLabel = label;
		const auto names = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(names.compression, expected);
		QVERIFY(!names.legacyIdentifiersUsed);
	}

	void legacy_dv_needs_the_recorded_standard()
	{
		// Genuine Avid slates use the same DV/C + 140 pair for both standards.
		// Their NTSC edit rate is recorded as 2997/100, not 30000/1001.
		auto facts = legacyDvFacts();
		facts.geometry = {720, 480};
		facts.rate = {2997, 100};
		const auto ntsc = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(ntsc.compression, QStringLiteral("DV NTSC 25Mbps 4:1:1"));
		QVERIFY(ntsc.legacyIdentifiersUsed);

		facts.geometry = {720, 576};
		facts.rate = {25, 1};
		const auto pal = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(pal.compression, QStringLiteral("DV PAL 25Mbps 4:1:1"));
		QVERIFY(pal.legacyIdentifiersUsed);

		// A known pair alone cannot distinguish PAL from NTSC. Neither can
		// a PAL raster paired with an NTSC rate. Do not guess a standard.
		facts.geometry = {};
		const auto unspecified = MediaEngine::Detail::compressionNames(facts);
		QVERIFY(!unspecified.compression.contains(QLatin1String("PAL")));
		QVERIFY(!unspecified.compression.contains(QLatin1String("NTSC")));
		facts.geometry = {720, 576};
		facts.rate = {2997, 100};
		const auto contradictory = MediaEngine::Detail::compressionNames(facts);
		QVERIFY(!contradictory.compression.contains(QLatin1String("PAL")));
		QVERIFY(!contradictory.compression.contains(QLatin1String("NTSC")));
	}

	void recorded_coding_is_not_overridden_by_legacy_hints()
	{
		auto facts = legacyDvFacts();
		facts.geometry = {720, 480};
		facts.rate = {2997, 100};
		facts.codingAbsent = false;

		facts.codingLabel = QByteArray::fromHex("060e2b34040101010401020202020200");
		const auto recordedPal = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(recordedPal.compression, QStringLiteral("DV PAL 25Mbps 4:1:1"));
		QVERIFY(!recordedPal.legacyIdentifiersUsed);

		facts.codingLabel = QByteArray::fromHex("060e2b340401010a0401020271040000");
		const auto recordedDnx = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(recordedDnx.compression, QStringLiteral("Avid DNx HQ"));
		QVERIFY(!recordedDnx.legacyIdentifiersUsed);

		facts.codingLabel = QByteArray::fromHex("060e2b3404010101040102020202ffff");
		const auto unknown = MediaEngine::Detail::compressionNames(facts);
		QVERIFY(!unknown.compression.contains(QLatin1String("DV")));
		QVERIFY(!unknown.legacyIdentifiersUsed);
	}

	void prores_names_keep_the_chroma_family_data()
	{
		QTest::addColumn<QByteArray>("label");
		QTest::addColumn<QString>("expected");
		QTest::newRow("registered-proxy")
			<< QByteArray::fromHex("060e2b340401010d0401020203060100") << QStringLiteral("Apple ProRes 422 Proxy");
		QTest::newRow("avid-lt")
			<< QByteArray::fromHex("060e2b34040101010e04020102110200") << QStringLiteral("Apple ProRes 422 LT");
		QTest::newRow("avid-hq")
			<< QByteArray::fromHex("060e2b34040101010e04020102110400") << QStringLiteral("Apple ProRes 422 HQ");
	}

	void prores_names_keep_the_chroma_family()
	{
		QFETCH(QByteArray, label);
		QFETCH(QString, expected);
		MediaEngine::Detail::CompressionFacts facts;
		facts.codingLabel = label;
		QCOMPARE(MediaEngine::Detail::compressionNames(facts).compression, expected);
	}

	void d10_labels_distinguish_bitrate_and_standard_data()
	{
		QTest::addColumn<QByteArray>("label");
		QTest::addColumn<QString>("expected");
		// Media Composer's m2v.lua identifies both of the first two labels as
		// 50Mbps; the third changes bitrate. Their suffixes are not a bitrate ladder.
		QTest::newRow("50-pal")
			<< QByteArray::fromHex("060e2b34040101010401020201020101") << QStringLiteral("SMPTE D-10/IMX 50Mbps 625x50I");
		QTest::newRow("50-ntsc")
			<< QByteArray::fromHex("060e2b34040101010401020201020102") << QStringLiteral("SMPTE D-10/IMX 50Mbps 525x59.94I");
		QTest::newRow("40-pal")
			<< QByteArray::fromHex("060e2b34040101010401020201020103") << QStringLiteral("SMPTE D-10/IMX 40Mbps 625x50I");
	}

	void d10_labels_distinguish_bitrate_and_standard()
	{
		QFETCH(QByteArray, label);
		QFETCH(QString, expected);
		MediaEngine::Detail::CompressionFacts facts;
		facts.codingLabel = label;
		QCOMPARE(MediaEngine::Detail::compressionNames(facts).compression, expected);
	}

	void dnx_aliases_require_an_exact_operating_point()
	{
		auto facts = dnxHqxFacts();
		const auto hd = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(hd.newDnx, QStringLiteral("Avid DNx HQX"));
		QCOMPARE(hd.oldDnx, QStringLiteral("DNxHD HQX"));
		QCOMPARE(hd.reallyOldDnx, QStringLiteral("DNxHD 175x"));
		QCOMPARE(hd.compression, QStringLiteral("Avid DNx HQX [DNxHD 175x]"));

		facts.rate = {23976, 1000};
		const auto approximateRate = MediaEngine::Detail::compressionNames(facts);
		QVERIFY(approximateRate.reallyOldDnx.isEmpty());
		QCOMPARE(approximateRate.compression, QStringLiteral("Avid DNx HQX"));

		// DNxHR can store an HD raster; that does not make it DNxHD 175x.
		facts.rate = {24000, 1001};
		facts.codingLabel = QByteArray::fromHex("060e2b340401010d0401020271250000");
		const auto hr = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(hr.newDnx, QStringLiteral("Avid DNx HQX"));
		QCOMPARE(hr.oldDnx, QStringLiteral("DNxHR HQX"));
		QVERIFY(hr.reallyOldDnx.isEmpty());
		QCOMPARE(hr.compression, QStringLiteral("Avid DNx HQX"));
	}

	void unproven_dnx_identifiers_remain_unnamed_data()
	{
		QTest::addColumn<QByteArray>("label");
		QTest::addColumn<int>("depth");
		QTest::addColumn<int>("horizontal");
		QTest::newRow("former-hd-lb") << QByteArray::fromHex("060e2b34040101010d01030102060301") << 8 << 2;
		QTest::newRow("former-hd-sq") << QByteArray::fromHex("060e2b34040101010d01030102060101") << 8 << 2;
		QTest::newRow("former-hd-hq") << QByteArray::fromHex("060e2b34040101010d01030102060201") << 8 << 2;
		QTest::newRow("former-hd-hqx") << QByteArray::fromHex("060e2b34040101010d01030102060202") << 10 << 2;
		QTest::newRow("former-hr-lb") << QByteArray::fromHex("060e2b34040101010d01030102110101") << 8 << 2;
		QTest::newRow("former-hr-sq") << QByteArray::fromHex("060e2b34040101010d01030102110201") << 8 << 2;
		QTest::newRow("former-hr-hq") << QByteArray::fromHex("060e2b34040101010d01030102110301") << 8 << 2;
		QTest::newRow("former-hr-hqx") << QByteArray::fromHex("060e2b34040101010d01030102110401") << 10 << 2;
		QTest::newRow("former-hr-444") << QByteArray::fromHex("060e2b34040101010d01030102110501") << 10 << 1;
	}

	void unproven_dnx_identifiers_remain_unnamed()
	{
		QFETCH(QByteArray, label);
		QFETCH(int, depth);
		QFETCH(int, horizontal);
		// This tests the decision to remove unsupported lookup entries. These
		// are authored formatter inputs, not specimens of an Avid file format.
		// Otherwise valid HD facts cannot establish an unknown coding profile.
		auto facts = dnxHqxFacts();
		facts.codingLabel = label;
		facts.depth = depth;
		facts.horizontal = horizontal;
		const auto names = MediaEngine::Detail::compressionNames(facts);
		QVERIFY(names.compression.isEmpty());
		QVERIFY(names.newDnx.isEmpty());
		QVERIFY(names.oldDnx.isEmpty());
		QVERIFY(names.reallyOldDnx.isEmpty());
	}

	void dnx_uncompressed_retains_qualified_flavours_data()
	{
		QTest::addColumn<bool>("fixedPoint");
		QTest::addColumn<QString>("bitDepth");
		QTest::addColumn<QString>("sampleFormat");
		QTest::addColumn<QString>("expected");
		QTest::newRow("standard-float") << false << QStringLiteral("32-bit") << QStringLiteral("Float")
			<< QStringLiteral("Avid DNxUncompressed — 32-bit Float");
		QTest::newRow("standard-half") << false << QStringLiteral("16-bit") << QStringLiteral("Half float")
			<< QStringLiteral("Avid DNxUncompressed — 16-bit Half float");
		QTest::newRow("standard-integer") << false << QStringLiteral("10-bit") << QStringLiteral("Integer")
			<< QStringLiteral("Avid DNxUncompressed — 10-bit Integer");
		QTest::newRow("fixed-2.14") << true << QStringLiteral("16-bit") << QStringLiteral("S2.14 fixed point")
			<< QStringLiteral("Avid DNxUncompressed — 16-bit S2.14 fixed point");
		QTest::newRow("fixed-10.6") << true << QStringLiteral("16-bit") << QStringLiteral("10.6 fixed point")
			<< QStringLiteral("Avid DNxUncompressed — 16-bit 10.6 fixed point");
		QTest::newRow("fixed-12.4") << true << QStringLiteral("16-bit") << QStringLiteral("12.4 fixed point")
			<< QStringLiteral("Avid DNxUncompressed — 16-bit 12.4 fixed point");
	}

	void dnx_uncompressed_retains_qualified_flavours()
	{
		QFETCH(bool, fixedPoint);
		QFETCH(QString, bitDepth);
		QFETCH(QString, sampleFormat);
		QFETCH(QString, expected);
		MediaEngine::Detail::CompressionFacts facts;
		facts.codingLabel = QByteArray::fromHex(fixedPoint ? "060e2b340401010d0401020203070200"
			: "060e2b340401010d0401020203070100");
		facts.bitDepth = bitDepth;
		facts.sampleFormat = sampleFormat;
		const auto names = MediaEngine::Detail::compressionNames(facts);
		QCOMPARE(names.compression, expected);
		QVERIFY(names.newDnx.isEmpty());
		QVERIFY(names.oldDnx.isEmpty());
		QVERIFY(names.reallyOldDnx.isEmpty());
	}
};

QTEST_APPLESS_MAIN(TestMediaEngineCompressionNames)
#include "tst_mediaengine_compressionnames.moc"
