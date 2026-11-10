// Naming controls use established labels and Avid operating points. These are
// authored inputs to the shared formatter, not claims about binary parsing.
#include "scanner_lookup/compression_names.h"

#include <QTest>
#include <limits>

// MARK: - Test helpers

namespace
{
	constexpr qint64 kMissingAlphaDepth = std::numeric_limits<qint64>::min();

	Bella::Lookup::CompressionFacts legacyDvFacts()
	{
		Bella::Lookup::CompressionFacts facts;
		facts.descriptorClass = QStringLiteral("CDCI");
		facts.legacyCompressionIdentifier = QByteArrayLiteral("DV/C");
		facts.legacyResolutionId = 140;
		facts.isCodingPropertyAbsent = true;
		facts.frameLayout = 1;
		return facts;
	}

	Bella::Lookup::CompressionFacts dnxHqxFacts()
	{
		Bella::Lookup::CompressionFacts facts;
		facts.codingLabel = QByteArray::fromHex("060e2b340401010a0401020271010000");
		facts.rasterSize = {1920, 1080};
		facts.namingRate = {24000, 1001};
		facts.frameLayout = 0;
		facts.rawComponentDepth = 10;
		facts.horizontalSubsampling = 2;
		facts.verticalSubsampling = 1;
		return facts;
	}
}

// MARK: - Test cases

class TestCompressionNames : public QObject
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
		Bella::Lookup::CompressionFacts facts;
		facts.codingLabel = label;
		const auto names = Bella::Lookup::compressionNames(facts);
		QCOMPARE(names.compressionName, expected);
		QVERIFY(!names.usedLegacyCompressionIdentifiers);
	}

	void legacy_dv_needs_the_recorded_standard()
	{
		// Genuine Avid slates use the same DV/C + 140 pair for both standards.
		// Their NTSC edit rate is recorded as 2997/100, not 30000/1001.
		auto facts = legacyDvFacts();
		facts.rasterSize = {720, 480};
		facts.namingRate = {2997, 100};
		const auto ntsc = Bella::Lookup::compressionNames(facts);
		QCOMPARE(ntsc.compressionName, QStringLiteral("DV NTSC 25Mbps 4:1:1"));
		QVERIFY(ntsc.usedLegacyCompressionIdentifiers);

		facts.rasterSize = {720, 576};
		facts.namingRate = {25, 1};
		const auto pal = Bella::Lookup::compressionNames(facts);
		QCOMPARE(pal.compressionName, QStringLiteral("DV PAL 25Mbps 4:1:1"));
		QVERIFY(pal.usedLegacyCompressionIdentifiers);

		// A known pair alone cannot distinguish PAL from NTSC. Neither can
		// a PAL raster paired with an NTSC rate. Do not guess a standard.
		facts.rasterSize = {};
		const auto unspecified = Bella::Lookup::compressionNames(facts);
		QVERIFY(!unspecified.compressionName.contains(QLatin1String("PAL")));
		QVERIFY(!unspecified.compressionName.contains(QLatin1String("NTSC")));
		facts.rasterSize = {720, 576};
		facts.namingRate = {2997, 100};
		const auto contradictory = Bella::Lookup::compressionNames(facts);
		QVERIFY(!contradictory.compressionName.contains(QLatin1String("PAL")));
		QVERIFY(!contradictory.compressionName.contains(QLatin1String("NTSC")));
	}

	void recorded_coding_is_not_overridden_by_legacy_hints()
	{
		auto facts = legacyDvFacts();
		facts.rasterSize = {720, 480};
		facts.namingRate = {2997, 100};
		facts.isCodingPropertyAbsent = false;

		facts.codingLabel = QByteArray::fromHex("060e2b34040101010401020202020200");
		const auto recordedPal = Bella::Lookup::compressionNames(facts);
		QCOMPARE(recordedPal.compressionName, QStringLiteral("DV PAL 25Mbps 4:1:1"));
		QVERIFY(!recordedPal.usedLegacyCompressionIdentifiers);

		facts.codingLabel = QByteArray::fromHex("060e2b340401010a0401020271040000");
		const auto recordedDnx = Bella::Lookup::compressionNames(facts);
		QCOMPARE(recordedDnx.compressionName, QStringLiteral("Avid DNx HQ"));
		QVERIFY(!recordedDnx.usedLegacyCompressionIdentifiers);

		facts.codingLabel = QByteArray::fromHex("060e2b3404010101040102020202ffff");
		const auto unknown = Bella::Lookup::compressionNames(facts);
		QVERIFY(!unknown.compressionName.contains(QLatin1String("DV")));
		QVERIFY(!unknown.usedLegacyCompressionIdentifiers);
	}

	void missingCodingMustBeConfirmedBeforeLegacyFallback()
	{
		auto facts = legacyDvFacts();
		facts.legacyResolutionId = 141;
		facts.rasterSize = {720, 576};
		facts.namingRate = {25, 1};
		facts.isCodingPropertyAbsent = false;
		const auto unavailable = Bella::Lookup::compressionNames(facts);
		QVERIFY(unavailable.compressionName.isEmpty());
		QVERIFY(!unavailable.usedLegacyCompressionIdentifiers);

		facts.isCodingPropertyAbsent = true;
		const auto absent = Bella::Lookup::compressionNames(facts);
		QCOMPARE(absent.compressionName, QStringLiteral("IEC-DV PAL 25Mbps 4:2:0"));
		QVERIFY(absent.usedLegacyCompressionIdentifiers);
	}

	void finalFallbackNeedsMatchingFacts_data()
	{
		QTest::addColumn<QString>("descriptorClass");
		QTest::addColumn<QByteArray>("legacyCompression");
		QTest::addColumn<bool>("codingAbsent");
		QTest::addColumn<QByteArray>("label");
		QTest::addColumn<qint64>("alphaDepth");
		QTest::addColumn<QString>("expected");
		QTest::addColumn<bool>("expectedLegacyIdentifiersUsed");
		const auto unknownLabel = QByteArray::fromHex("060e2b3404010101040102020202ffff");
		QTest::newRow("rgba-missing-alpha")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << true << QByteArray{}
			<< kMissingAlphaDepth << QString{} << false;
		QTest::newRow("rgba-negative-alpha")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << true << QByteArray{}
			<< qint64(-8) << QString{} << false;
		QTest::newRow("rgba-zero-alpha")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << true << QByteArray{}
			<< qint64(0) << QString{} << false;
		QTest::newRow("rgba-alpha-8")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << true << QByteArray{}
			<< qint64(8) << QStringLiteral("1:1 Alpha 8bit") << true;
		QTest::newRow("rgba-alpha-10")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << true << QByteArray{}
			<< qint64(10) << QStringLiteral("1:1 Alpha 10bit") << true;
		QTest::newRow("rgba-alpha-16")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << true << QByteArray{}
			<< qint64(16) << QStringLiteral("Uncompressed alpha") << true;
		QTest::newRow("rgba-coding-unavailable")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << false << QByteArray{}
			<< qint64(8) << QString{} << false;
		QTest::newRow("rgba-unknown-coding")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << false << unknownLabel
			<< qint64(8) << QString{} << false;
		QTest::newRow("rgba-nonempty-coding-despite-absence")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("NONE") << true << unknownLabel
			<< qint64(8) << QString{} << false;
		QTest::newRow("other-descriptor")
			<< QStringLiteral("CDCI") << QByteArrayLiteral("NONE") << true << QByteArray{}
			<< qint64(8) << QString{} << false;
		QTest::newRow("other-legacy-compression")
			<< QStringLiteral("RGBA") << QByteArrayLiteral("RLE ") << true << QByteArray{}
			<< qint64(8) << QString{} << false;
		QTest::newRow("pcma")
			<< QStringLiteral("PCMA") << QByteArray{} << true << QByteArray{}
			<< kMissingAlphaDepth << QStringLiteral("PCM") << false;
		QTest::newRow("wave")
			<< QStringLiteral("WAVE") << QByteArray{} << true << QByteArray{}
			<< kMissingAlphaDepth << QStringLiteral("PCM") << false;
		QTest::newRow("pcma-coding-unavailable")
			<< QStringLiteral("PCMA") << QByteArray{} << false << QByteArray{}
			<< kMissingAlphaDepth << QString{} << false;
		QTest::newRow("wave-unknown-coding")
			<< QStringLiteral("WAVE") << QByteArray{} << false << unknownLabel
			<< kMissingAlphaDepth << QString{} << false;
		QTest::newRow("pcma-nonempty-coding-despite-absence")
			<< QStringLiteral("PCMA") << QByteArray{} << true << unknownLabel
			<< kMissingAlphaDepth << QString{} << false;
	}

	void finalFallbackNeedsMatchingFacts()
	{
		QFETCH(QString, descriptorClass);
		QFETCH(QByteArray, legacyCompression);
		QFETCH(bool, codingAbsent);
		QFETCH(QByteArray, label);
		QFETCH(qint64, alphaDepth);
		QFETCH(QString, expected);
		QFETCH(bool, expectedLegacyIdentifiersUsed);
		Bella::Lookup::CompressionFacts facts;
		facts.descriptorClass = descriptorClass;
		facts.legacyCompressionIdentifier = legacyCompression;
		facts.isCodingPropertyAbsent = codingAbsent;
		facts.codingLabel = label;
		if (alphaDepth != kMissingAlphaDepth)
			facts.alphaChannelBitDepth = alphaDepth;
		const auto names = Bella::Lookup::compressionNames(facts);
		QCOMPARE(names.compressionName, expected);
		QVERIFY(names.newDnx.isEmpty());
		QVERIFY(names.oldDnx.isEmpty());
		QVERIFY(names.reallyOldDnx.isEmpty());
		QCOMPARE(names.usedLegacyCompressionIdentifiers, expectedLegacyIdentifiersUsed);
	}

	// MARK: - Compression family labels

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
		Bella::Lookup::CompressionFacts facts;
		facts.codingLabel = label;
		QCOMPARE(Bella::Lookup::compressionNames(facts).compressionName, expected);
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
		Bella::Lookup::CompressionFacts facts;
		facts.codingLabel = label;
		QCOMPARE(Bella::Lookup::compressionNames(facts).compressionName, expected);
	}

	void jpeg2000LabelsUseFamilyName_data()
	{
		QTest::addColumn<QByteArray>("label");
		QTest::addColumn<QString>("expected");
		QTest::newRow("generic")
			<< QByteArray::fromHex("060e2b34040101070401020203010100") << QStringLiteral("JPEG2000");
		QTest::newRow("registered-020a")
			<< QByteArray::fromHex("060e2b340401010d040102020301020a") << QStringLiteral("JPEG2000");
		QTest::newRow("registered-0312")
			<< QByteArray::fromHex("060e2b340401010d0401020203010312") << QStringLiteral("JPEG2000");
		QTest::newRow("registered-0104")
			<< QByteArray::fromHex("060e2b34040101090401020203010104") << QStringLiteral("JPEG2000");
		QTest::newRow("unknown-neighbour")
			<< QByteArray::fromHex("060e2b340401010704010202030101ff") << QString{};
	}

	void jpeg2000LabelsUseFamilyName()
	{
		QFETCH(QByteArray, label);
		QFETCH(QString, expected);
		Bella::Lookup::CompressionFacts facts;
		facts.codingLabel = label;
		QCOMPARE(Bella::Lookup::compressionNames(facts).compressionName, expected);

		facts.rasterSize = {1920, 1080};
		facts.namingRate = {25, 1};
		facts.frameLayout = 0;
		facts.rawComponentDepth = 10;
		facts.horizontalSubsampling = 2;
		facts.verticalSubsampling = 1;
		QCOMPARE(Bella::Lookup::compressionNames(facts).compressionName, expected);

		facts.rasterSize = {720, 576};
		QCOMPARE(Bella::Lookup::compressionNames(facts).compressionName, expected);
		facts.rasterSize = {3840, 2160};
		QCOMPARE(Bella::Lookup::compressionNames(facts).compressionName, expected);

		facts.rasterSize = {1920, 1080};
		facts.namingRate = {12345, 1000};
		facts.frameLayout = 2;
		facts.rawComponentDepth = 12;
		facts.horizontalSubsampling = 4;
		facts.verticalSubsampling = 2;
		const auto names = Bella::Lookup::compressionNames(facts);
		QCOMPARE(names.compressionName, expected);
		QVERIFY(names.newDnx.isEmpty());
		QVERIFY(names.oldDnx.isEmpty());
		QVERIFY(names.reallyOldDnx.isEmpty());
		QVERIFY(!names.usedLegacyCompressionIdentifiers);
	}

	// MARK: - DNx operating points

	void dnx_aliases_require_an_exact_operating_point()
	{
		auto facts = dnxHqxFacts();
		const auto hd = Bella::Lookup::compressionNames(facts);
		QCOMPARE(hd.newDnx, QStringLiteral("Avid DNx HQX"));
		QCOMPARE(hd.oldDnx, QStringLiteral("DNxHD HQX"));
		QCOMPARE(hd.reallyOldDnx, QStringLiteral("DNxHD 175x"));
		QCOMPARE(hd.compressionName, QStringLiteral("Avid DNx HQX [DNxHD 175x]"));

		facts.namingRate = {23976, 1000};
		const auto approximateRate = Bella::Lookup::compressionNames(facts);
		QVERIFY(approximateRate.reallyOldDnx.isEmpty());
		QCOMPARE(approximateRate.compressionName, QStringLiteral("Avid DNx HQX"));

		// DNxHR can store an HD raster; that does not make it DNxHD 175x.
		facts.namingRate = {24000, 1001};
		facts.codingLabel = QByteArray::fromHex("060e2b340401010d0401020271250000");
		const auto hr = Bella::Lookup::compressionNames(facts);
		QCOMPARE(hr.newDnx, QStringLiteral("Avid DNx HQX"));
		QCOMPARE(hr.oldDnx, QStringLiteral("DNxHR HQX"));
		QVERIFY(hr.reallyOldDnx.isEmpty());
		QCOMPARE(hr.compressionName, QStringLiteral("Avid DNx HQX"));
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
		// Authored formatter inputs: valid HD geometry cannot establish an unknown coding profile.
		auto facts = dnxHqxFacts();
		facts.codingLabel = label;
		facts.rawComponentDepth = depth;
		facts.horizontalSubsampling = horizontal;
		const auto names = Bella::Lookup::compressionNames(facts);
		QVERIFY(names.compressionName.isEmpty());
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
		Bella::Lookup::CompressionFacts facts;
		facts.codingLabel = QByteArray::fromHex(fixedPoint ? "060e2b340401010d0401020203070200"
														   : "060e2b340401010d0401020203070100");
		facts.bitDepthText = bitDepth;
		facts.sampleFormatText = sampleFormat;
		const auto names = Bella::Lookup::compressionNames(facts);
		QCOMPARE(names.compressionName, expected);
		QVERIFY(names.newDnx.isEmpty());
		QVERIFY(names.oldDnx.isEmpty());
		QVERIFY(names.reallyOldDnx.isEmpty());
	}

	void dnxUncompressedAlphaBoundaries_data()
	{
		QTest::addColumn<QByteArray>("label");
		QTest::addColumn<qint64>("alphaDepth");
		QTest::addColumn<QString>("expected");
		QTest::newRow("standard-zero-alpha")
			<< QByteArray::fromHex("060e2b340401010d0401020203070100") << qint64(0)
			<< QStringLiteral("Avid DNxUncompressed — 16-bit Integer");
		QTest::newRow("alpha-8")
			<< QByteArray::fromHex("060e2b340401010d0401020203070100") << qint64(8) << QStringLiteral("DNxUncompressed");
		QTest::newRow("standard-alpha-10")
			<< QByteArray::fromHex("060e2b340401010d0401020203070100") << qint64(10)
			<< QStringLiteral("Avid DNxUncompressed — 16-bit Integer");
		QTest::newRow("alpha-16")
			<< QByteArray::fromHex("060e2b340401010d0401020203070100") << qint64(16) << QStringLiteral("DNxUncompressed");
		QTest::newRow("fixed-point-alpha-8")
			<< QByteArray::fromHex("060e2b340401010d0401020203070200") << qint64(8)
			<< QStringLiteral("Avid DNxUncompressed — 16-bit Integer");
		QTest::newRow("fixed-point-alpha-16")
			<< QByteArray::fromHex("060e2b340401010d0401020203070200") << qint64(16)
			<< QStringLiteral("Avid DNxUncompressed — 16-bit Integer");
		QTest::newRow("legacy-rle")
			<< QByteArray::fromHex("060e2b34040101030e04410101030000") << kMissingAlphaDepth << QStringLiteral("DNxUncompressed");
		QTest::newRow("legacy-rle-alpha-10")
			<< QByteArray::fromHex("060e2b34040101030e04410101030000") << qint64(10) << QStringLiteral("DNxUncompressed");
		QTest::newRow("unknown-alpha-8")
			<< QByteArray::fromHex("060e2b340401010d040102020307ff00") << qint64(8) << QString{};
		QTest::newRow("unknown-alpha-16")
			<< QByteArray::fromHex("060e2b340401010d040102020307ff00") << qint64(16) << QString{};
	}

	void dnxUncompressedAlphaBoundaries()
	{
		QFETCH(QByteArray, label);
		QFETCH(qint64, alphaDepth);
		QFETCH(QString, expected);
		Bella::Lookup::CompressionFacts facts;
		facts.codingLabel = label;
		if (alphaDepth != kMissingAlphaDepth)
			facts.alphaChannelBitDepth = alphaDepth;
		facts.bitDepthText = QStringLiteral("16-bit");
		facts.sampleFormatText = QStringLiteral("Integer");
		const auto names = Bella::Lookup::compressionNames(facts);
		QCOMPARE(names.compressionName, expected);
		QVERIFY(names.newDnx.isEmpty());
		QVERIFY(names.oldDnx.isEmpty());
		QVERIFY(names.reallyOldDnx.isEmpty());
		QVERIFY(!names.usedLegacyCompressionIdentifiers);
	}
};

QTEST_APPLESS_MAIN(TestCompressionNames)
#include "tst_compression_names.moc"
