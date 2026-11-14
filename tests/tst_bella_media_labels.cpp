// Authored reader-value controls for Bella's naming boundary. Expected names
// are pinned literals from the recorded naming contract and existing goldens;
// no reader or alternate naming implementation supplies the expected answers.
#include "scanner/media_labels.h"
#include "scanner/mdb_parser.h"
#include "scanner/mxf_parser.h"
#include "scanner/omf_parser.h"

#include <QtTest>

// MARK: - Test helpers

namespace
{
	template <std::size_t Size>
	std::array<quint8, Size> hexBytes(const char *hex)
	{
		const auto bytes = QByteArray::fromHex(hex);
		Q_ASSERT(bytes.size() == qsizetype(Size));
		std::array<quint8, Size> result{};
		for (std::size_t i = 0; i < Size; ++i)
			result[i] = quint8(bytes[qsizetype(i)]);
		return result;
	}

	std::array<quint8, 4> tag(QByteArrayView bytes)
	{
		Q_ASSERT(bytes.size() == 4);
		return {quint8(bytes[0]), quint8(bytes[1]), quint8(bytes[2]), quint8(bytes[3])};
	}

	Bella::MobId identity()
	{
		QByteArray bytes(32, '\0');
		bytes[31] = '\1';
		return std::get<Bella::MobId>(Bella::normalizeMobId(bytes, Bella::MobIdLayout::Aaf32,
															Bella::ByteOrder::BigEndian));
	}

	const auto hqx = hexBytes<16>("060e2b340401010a0401020271010000");
	const auto hrHqx = hexBytes<16>("060e2b340401010d0401020271250000");
	const auto proresHq = hexBytes<16>("060e2b340401010d0401020203060400");
	const auto unknownCoding = hexBytes<16>("060e2b3404010101040102020202ffff");
	const auto j2k = hexBytes<16>("060e2b34040101070401020203010100");
	const auto standardUncompressed = hexBytes<16>("060e2b340401010d0401020203070100");
	const auto fixedUncompressed = hexBytes<16>("060e2b340401010d0401020203070200");
	const auto mxfCdci = hexBytes<16>("060e2b34025301010d01010101012800");
	const auto mxfRgba = hexBytes<16>("060e2b34025301010d01010101012900");

	Bella::MdbFileRecord mdbHqx()
	{
		Bella::MdbFileRecord file{identity()};
		file.ownDescriptorQualified = true;
		file.kind = Bella::MdbKind::Video;
		file.descriptorClass = tag("CDCI");
		file.sampleRate = Bella::MdbRate{{24000, 1001}, Bella::MdbRateEncoding::ExactEditRate};
		file.compression.codingState = Bella::MdbCodingState::Present;
		file.compression.codingLabel = hqx;
		file.compression.namingGeometry = Bella::MdbDimensions{1920, 1080};
		file.compression.frameLayout = 0;
		file.compression.horizontalSubsampling = 2;
		file.compression.verticalSubsampling = 1;
		file.bitDepth.bits = 10;
		file.bitDepth.rawComponentWidth = 10;
		file.bitDepth.representation = Bella::MdbDepthRepresentation::Integer;
		return file;
	}

	Bella::OmfFileInformation omfHqx()
	{
		Bella::OmfFileInformation file;
		file.kind = Bella::OmfKind::Video;
		file.descriptorClass = tag("CDCI");
		file.sampleRate = Bella::OmfRate{{24000, 1001}, Bella::OmfRateEncoding::ExactEditRate};
		file.compression.codingState = Bella::OmfCodingState::Present;
		file.compression.codingLabel = hqx;
		file.compression.namingGeometry = Bella::OmfDimensions{1920, 1080};
		file.compression.frameLayout = 0;
		file.compression.horizontalSubsampling = 2;
		file.compression.verticalSubsampling = 1;
		file.bitDepth.bits = 10;
		file.bitDepth.rawComponentWidth = 10;
		file.bitDepth.representation = Bella::OmfDepthRepresentation::Integer;
		return file;
	}

	Bella::MxfFileInformation mxfHqx()
	{
		Bella::MxfFileInformation file;
		file.kind = Bella::MxfKind::Video;
		file.descriptorClass = mxfCdci;
		file.sampleRate = Bella::MxfRate{24000, 1001};
		file.compression.codingState = Bella::MxfCodingState::Present;
		file.compression.codingLabel = hqx;
		file.compression.namingGeometry = Bella::MxfDimensions{1920, 1080};
		file.compression.frameLayout = 0;
		file.compression.horizontalSubsampling = 2;
		file.compression.verticalSubsampling = 1;
		file.bitDepth.bits = 10;
		file.bitDepth.rawComponentDepth = 10;
		file.bitDepth.representation = Bella::MxfDepthRepresentation::Integer;
		return file;
	}

	void expectNames(const Bella::CompressionNames &names, const QString &compression,
					 const QString &newDnx = {}, const QString &oldDnx = {}, const QString &reallyOldDnx = {})
	{
		QCOMPARE(names.compressionName, compression);
		QCOMPARE(names.newDnx, newDnx);
		QCOMPARE(names.oldDnx, oldDnx);
		QCOMPARE(names.reallyOldDnx, reallyOldDnx);
	}
}

// MARK: - Test fixture

class TestBellaMediaLabels final : public QObject
{
	Q_OBJECT
private slots:
	void mdbNeedsOwnedDescriptor();
	void codingStateControlsStaleBytes_data();
	void codingStateControlsStaleBytes();
	void allDnxNamesUseNamingGeometry();
	void legacyDnxRateQualification_data();
	void legacyDnxRateQualification();
	void mxfNamingClockRemainsLiteral();
	void jpeg2000NameIgnoresRateEncoding_data();
	void jpeg2000NameIgnoresRateEncoding();
	void uncompressedRepresentations_data();
	void uncompressedRepresentations();
	void mixedMxfComponents_data();
	void mixedMxfComponents();
	void dnxAlphaLayout_data();
	void dnxAlphaLayout();
	void dnxAlphaNeedsMatchingMetadata_data();
	void dnxAlphaNeedsMatchingMetadata();
	void alphaNeedsSourceSpecificProof_data();
	void alphaNeedsSourceSpecificProof();
	void audioEncodingIsIndependentOfTiming_data();
	void audioEncodingIsIndependentOfTiming();
	void contradictoryAudioSummaryKeepsDnxNames();
	void mxfDescriptorKeyVariants_data();
	void mxfDescriptorKeyVariants();
	void effectAndCategory_data();
	void effectAndCategory();
	void inputsAndReturnedNamesAreOwned();
};

// MARK: - Descriptor and compression qualification

void TestBellaMediaLabels::mdbNeedsOwnedDescriptor()
{
	auto file = mdbHqx();
	file.ownDescriptorQualified = false;
	expectNames(Bella::compressionNamesFor(file), {});
	file.ownDescriptorQualified = true;
	expectNames(Bella::compressionNamesFor(file), QStringLiteral("DNx HQX [DNxHD 175x]"),
				QStringLiteral("DNx HQX"), QStringLiteral("DNxHD HQX"), QStringLiteral("DNxHD 175x"));
}

void TestBellaMediaLabels::codingStateControlsStaleBytes_data()
{
	QTest::addColumn<int>("reader");
	QTest::addColumn<int>("variation");
	for (int reader = 0; reader < 3; ++reader)
		for (int variation = 0; variation < 4; ++variation)
			QTest::newRow(qPrintable(QStringLiteral("reader-%1-state-%2").arg(reader).arg(variation))) << reader << variation;
}
void TestBellaMediaLabels::codingStateControlsStaleBytes()
{
	QFETCH(int, reader);
	QFETCH(int, variation);
	// 0: absent with stale known bytes; 1: unavailable with stale known bytes;
	// 2: present known label; 3: present unknown label. Only the presence enum
	// says whether the retained array is a recorded label.
	Bella::CompressionNames result;
	if (reader == 0)
	{
		auto file = mdbHqx();
		file.descriptorClass = tag("JPED");
		file.compression.legacyCompression = tag("JFIF");
		file.compression.resolutionId = 82;
		file.compression.codingState = variation == 0	? Bella::MdbCodingState::Absent
									   : variation == 1 ? Bella::MdbCodingState::Unavailable
														: Bella::MdbCodingState::Present;
		file.compression.codingLabel = variation == 3 ? unknownCoding : proresHq;
		result = Bella::compressionNamesFor(file);
	}
	else if (reader == 1)
	{
		auto file = omfHqx();
		file.descriptorClass = tag("JPED");
		file.compression.legacyCompression = tag("JFIF");
		file.compression.resolutionId = 82;
		file.compression.codingState = variation == 0	? Bella::OmfCodingState::Absent
									   : variation == 1 ? Bella::OmfCodingState::Unavailable
														: Bella::OmfCodingState::Present;
		file.compression.codingLabel = variation == 3 ? unknownCoding : proresHq;
		result = Bella::compressionNamesFor(file);
	}
	else
	{
		Bella::MxfFileInformation file;
		file.kind = Bella::MxfKind::Audio;
		file.compression.namingFallback = Bella::MxfNamingFallback::Pcm;
		file.compression.codingState = variation == 0	? Bella::MxfCodingState::Absent
									   : variation == 1 ? Bella::MxfCodingState::Unavailable
														: Bella::MxfCodingState::Present;
		file.compression.codingLabel = variation == 3 ? unknownCoding : proresHq;
		result = Bella::compressionNamesFor(file);
	}
	expectNames(result, variation == 0	 ? (reader == 2 ? QStringLiteral("PCM") : QStringLiteral("JFIF 20:1"))
						: variation == 2 ? QStringLiteral("Apple ProRes 422 HQ")
										 : QString{});
}

void TestBellaMediaLabels::allDnxNamesUseNamingGeometry()
{
	auto mdb = mdbHqx();
	auto omf = omfHqx();
	auto mxf = mxfHqx();
	mdb.resolution = Bella::MdbRaster{{720, 404}, Bella::MdbRasterOrigin::VerifiedProxy};
	omf.resolution = Bella::OmfRaster{{720, 404}, Bella::OmfRasterOrigin::VerifiedProxy};
	mxf.resolution = Bella::MxfRaster{{720, 404}, Bella::MxfRasterOrigin::VerifiedProxy};
	// Compression does not require the independent displayed Frame Rate flag.
	mxf.frameRateQualified = false;
	for (const auto &names : {Bella::compressionNamesFor(mdb), Bella::compressionNamesFor(omf), Bella::compressionNamesFor(mxf)})
		expectNames(names, QStringLiteral("DNx HQX [DNxHD 175x]"),
					QStringLiteral("DNx HQX"), QStringLiteral("DNxHD HQX"), QStringLiteral("DNxHD 175x"));
	mdb.compression.namingGeometry.reset();
	omf.compression.namingGeometry.reset();
	mxf.compression.namingGeometry.reset();
	for (const auto &names : {Bella::compressionNamesFor(mdb), Bella::compressionNamesFor(omf), Bella::compressionNamesFor(mxf)})
		expectNames(names, QStringLiteral("DNx HQX"), QStringLiteral("DNx HQX"), QStringLiteral("DNxHD HQX"));
}

void TestBellaMediaLabels::legacyDnxRateQualification_data()
{
	QTest::addColumn<int>("variation");
	QTest::addColumn<bool>("numberedAlias");
	QTest::newRow("finite exact decimal pair") << 0 << true;
	QTest::newRow("same supported pair with Rational encoding") << 1 << true;
	QTest::newRow("unregistered ResolutionID cannot qualify decimal") << 2 << false;
	QTest::newRow("ResolutionID and coding label indicate different DNx flavours") << 3 << false;
	QTest::newRow("Duration multiplier alias is outside naming rule") << 4 << false;
	QTest::newRow("nearby decimal is not exact") << 5 << false;
	QTest::newRow("absent coding permits narrow known ResolutionID recovery") << 6 << true;
	QTest::newRow("decimal without ResolutionID remains literal") << 7 << false;
}
void TestBellaMediaLabels::legacyDnxRateQualification()
{
	QFETCH(int, variation);
	QFETCH(bool, numberedAlias);
	auto mdb = mdbHqx();
	auto omf = omfHqx();
	mdb.sampleRate->words = {23976, 1000};
	omf.sampleRate->words = {23976, 1000};
	mdb.compression.resolutionId = 1235;
	omf.compression.resolutionId = 1235;
	if (variation == 1)
	{
		mdb.sampleRate->encoding = Bella::MdbRateEncoding::Rational;
		omf.sampleRate->encoding = Bella::OmfRateEncoding::Rational;
	}
	if (variation == 2 || variation == 3)
	{
		mdb.compression.resolutionId = variation == 2 ? 1299 : 1237;
		omf.compression.resolutionId = variation == 2 ? 1299 : 1237;
	}
	if (variation == 4 || variation == 5)
	{
		// The Duration interpreter knows 59.94 -> 60000/1001, whose DNx table
		// contains 440x. That broader alias must not manufacture a naming match.
		mdb.sampleRate->words = variation == 4 ? Bella::MdbRateWords{5994, 100} : Bella::MdbRateWords{23976001, 1000000};
		omf.sampleRate->words = variation == 4 ? Bella::OmfRateWords{5994, 100} : Bella::OmfRateWords{23976001, 1000000};
	}
	if (variation == 6)
	{
		mdb.compression.codingState = Bella::MdbCodingState::Absent;
		omf.compression.codingState = Bella::OmfCodingState::Absent;
		mdb.compression.codingLabel = proresHq;
		omf.compression.codingLabel = proresHq; // Stale array is ignored.
	}
	if (variation == 7)
	{
		mdb.compression.resolutionId.reset();
		omf.compression.resolutionId.reset();
	}
	for (const auto &names : {Bella::compressionNamesFor(mdb), Bella::compressionNamesFor(omf)})
		expectNames(names, numberedAlias ? QStringLiteral("DNx HQX [DNxHD 175x]") : QStringLiteral("DNx HQX"),
					QStringLiteral("DNx HQX"), QStringLiteral("DNxHD HQX"), numberedAlias ? QStringLiteral("DNxHD 175x") : QString{});
	QCOMPARE(mdb.sampleRate->words.numerator, variation == 4 ? qint32(5994) : variation == 5 ? qint32(23976001)
																							 : qint32(23976));
	QCOMPARE(omf.sampleRate->words.denominator, variation == 4 ? qint32(100) : variation == 5 ? qint32(1000000)
																							  : qint32(1000));
}

void TestBellaMediaLabels::mxfNamingClockRemainsLiteral()
{
	auto file = mxfHqx();
	file.sampleRate = Bella::MxfRate{23976, 1000};
	expectNames(Bella::compressionNamesFor(file), QStringLiteral("DNx HQX"), QStringLiteral("DNx HQX"), QStringLiteral("DNxHD HQX"));
	file.compression.codingLabel = hrHqx;
	file.sampleRate = Bella::MxfRate{24000, 1001};
	expectNames(Bella::compressionNamesFor(file), QStringLiteral("DNx HQX"), QStringLiteral("DNx HQX"), QStringLiteral("DNxHR HQX"));
}

void TestBellaMediaLabels::jpeg2000NameIgnoresRateEncoding_data()
{
	QTest::addColumn<bool>("ntsc");
	QTest::addColumn<bool>("typed");
	QTest::addColumn<bool>("exact");
	QTest::newRow("29.97 ExactEditRate") << true << true << false;
	QTest::newRow("29.97 Rational") << true << false << false;
	QTest::newRow("23.976 ExactEditRate") << false << true << false;
	QTest::newRow("23.976 Rational") << false << false << false;
	QTest::newRow("30000/1001 ExactEditRate") << true << true << true;
	QTest::newRow("30000/1001 Rational") << true << false << true;
	QTest::newRow("24000/1001 ExactEditRate") << false << true << true;
	QTest::newRow("24000/1001 Rational") << false << false << true;
}
void TestBellaMediaLabels::jpeg2000NameIgnoresRateEncoding()
{
	QFETCH(bool, ntsc);
	QFETCH(bool, typed);
	QFETCH(bool, exact);
	auto mdb = mdbHqx();
	auto omf = omfHqx();
	auto mxf = mxfHqx();
	mdb.compression.codingLabel = j2k;
	omf.compression.codingLabel = j2k;
	mxf.compression.codingLabel = j2k;
	const qint32 numerator = exact ? (ntsc ? 30000 : 24000) : (ntsc ? 2997 : 23976);
	const qint32 denominator = exact ? 1001 : (ntsc ? 100 : 1000);
	mdb.sampleRate = Bella::MdbRate{{numerator, denominator},
									typed ? Bella::MdbRateEncoding::ExactEditRate : Bella::MdbRateEncoding::Rational};
	omf.sampleRate = Bella::OmfRate{{numerator, denominator},
									typed ? Bella::OmfRateEncoding::ExactEditRate : Bella::OmfRateEncoding::Rational};
	mxf.sampleRate = Bella::MxfRate{numerator, denominator};
	expectNames(Bella::compressionNamesFor(mdb), QStringLiteral("JPEG2000"));
	expectNames(Bella::compressionNamesFor(omf), QStringLiteral("JPEG2000"));
	expectNames(Bella::compressionNamesFor(mxf), QStringLiteral("JPEG2000"));
	QCOMPARE(mdb.sampleRate->words.numerator, numerator);
	QCOMPARE(mdb.sampleRate->words.denominator, denominator);
	QCOMPARE(omf.sampleRate->words.numerator, numerator);
	QCOMPARE(omf.sampleRate->words.denominator, denominator);
	QCOMPARE(mxf.sampleRate->numerator, numerator);
	QCOMPARE(mxf.sampleRate->denominator, denominator);
}

// MARK: - Component and alpha naming

void TestBellaMediaLabels::uncompressedRepresentations_data()
{
	QTest::addColumn<int>("variation");
	QTest::addColumn<QString>("description");
	QTest::newRow("standard float") << 0 << QStringLiteral("32-bit Float");
	QTest::newRow("standard half float") << 1 << QStringLiteral("16-bit Half float");
	QTest::newRow("standard integer") << 2 << QStringLiteral("10-bit Integer");
	QTest::newRow("fixed S2.14") << 3 << QStringLiteral("16-bit S2.14 fixed point");
	QTest::newRow("fixed 10.6 naming survives blank generic depth") << 4 << QStringLiteral("16-bit 10.6 fixed point");
	QTest::newRow("fixed 12.4 naming survives blank generic depth") << 5 << QStringLiteral("16-bit 12.4 fixed point");
}
void TestBellaMediaLabels::uncompressedRepresentations()
{
	QFETCH(int, variation);
	QFETCH(QString, description);
	auto mdb = mdbHqx();
	auto omf = omfHqx();
	auto mxf = mxfHqx();
	const auto coding = variation < 3 ? standardUncompressed : fixedUncompressed;
	mdb.compression.codingLabel = coding;
	omf.compression.codingLabel = coding;
	mxf.compression.codingLabel = coding;
	const qint32 raw = variation == 0 || variation == 3 ? 254 : variation == 1 ? 253
															: variation == 5   ? 12
																			   : 10;
	mdb.bitDepth.rawComponentWidth = raw;
	omf.bitDepth.rawComponentWidth = raw;
	mxf.bitDepth.rawComponentDepth = quint32(raw);
	const auto depth = variation == 0 ? 32u : variation == 2 ? 10u
															 : 16u;
	mdb.bitDepth.bits = depth;
	omf.bitDepth.bits = depth;
	mxf.bitDepth.bits = depth;
	mdb.bitDepth.representation = std::array{Bella::MdbDepthRepresentation::Float, Bella::MdbDepthRepresentation::HalfFloat,
											 Bella::MdbDepthRepresentation::Integer, Bella::MdbDepthRepresentation::S2_14Fixed,
											 Bella::MdbDepthRepresentation::Fixed10_6, Bella::MdbDepthRepresentation::Fixed12_4}[variation];
	omf.bitDepth.representation = std::array{Bella::OmfDepthRepresentation::Float, Bella::OmfDepthRepresentation::HalfFloat,
											 Bella::OmfDepthRepresentation::Integer, Bella::OmfDepthRepresentation::S2_14Fixed,
											 Bella::OmfDepthRepresentation::Fixed10_6, Bella::OmfDepthRepresentation::Fixed12_4}[variation];
	mxf.bitDepth.representation = std::array{Bella::MxfDepthRepresentation::Float, Bella::MxfDepthRepresentation::HalfFloat,
											 Bella::MxfDepthRepresentation::Integer, Bella::MxfDepthRepresentation::S2_14Fixed,
											 Bella::MxfDepthRepresentation::Fixed10_6, Bella::MxfDepthRepresentation::Fixed12_4}[variation];
	if (variation >= 4)
	{
		mdb.bitDepth.bits.reset();
		omf.bitDepth.bits.reset();
		mxf.bitDepth.bits.reset();
	}
	for (const auto &names : {Bella::compressionNamesFor(mdb), Bella::compressionNamesFor(omf), Bella::compressionNamesFor(mxf)})
		expectNames(names, QStringLiteral("DNxUncompressed — ") + description);
}

void TestBellaMediaLabels::mixedMxfComponents_data()
{
	QTest::addColumn<bool>("mixed");
	QTest::newRow("distinct component order and representation retained") << true;
	QTest::newRow("uniform components compact to one value") << false;
}
void TestBellaMediaLabels::mixedMxfComponents()
{
	QFETCH(bool, mixed);
	auto file = mxfHqx();
	file.descriptorClass = mxfRgba;
	file.compression.codingLabel = standardUncompressed;
	file.bitDepth.bits.reset();
	file.bitDepth.representation = Bella::MxfDepthRepresentation::Unknown;
	file.bitDepth.rawComponentDepth.reset();
	file.bitDepth.pixelLayout = mixed ? std::array<quint8, 16>{'A', 8, 'R', 254, 'G', 253, 'B', 10}
									  : std::array<quint8, 16>{'F', 8, 'R', 10, 'G', 10, 'B', 10};
	expectNames(Bella::compressionNamesFor(file), mixed
													  ? QStringLiteral("DNxUncompressed — A: 8-bit; R: 32-bit; G: 16-bit; B: 10-bit A: Integer; R: Float; G: Half float; B: Integer")
													  : QStringLiteral("DNxUncompressed — 10-bit Integer"));
}

void TestBellaMediaLabels::dnxAlphaLayout_data()
{
	QTest::addColumn<QByteArray>("layout");
	QTest::addColumn<bool>("soleAlpha");
	QTest::newRow("alpha-8") << QByteArray::fromHex("41080000000000000000000000000000") << true;
	QTest::newRow("alpha-16") << QByteArray::fromHex("41100000000000000000000000000000") << true;
	// The supplied Media Composer matte uses both zero and '0' padding.
	QTest::newRow("recorded-alpha-16") << QByteArray::fromHex("41100000300030003000300030003000") << true;
	QTest::newRow("alpha-with-fill") << QByteArray::fromHex("46084108000000000000000000000000") << true;
	QTest::newRow("duplicate-alpha") << QByteArray::fromHex("41084108000000000000000000000000") << false;
	QTest::newRow("rgb-and-alpha") << QByteArray::fromHex("52084708420841080000000000000000") << false;
	QTest::newRow("unsupported-alpha-depth") << QByteArray::fromHex("410a0000000000000000000000000000") << false;
	QTest::newRow("unknown-component") << QByteArray::fromHex("51084108000000000000000000000000") << false;
	QTest::newRow("component-after-terminator") << QByteArray::fromHex("00004108000000000000000000000000") << false;
	QTest::newRow("no-alpha") << QByteArray::fromHex("46080000000000000000000000000000") << false;
}

void TestBellaMediaLabels::dnxAlphaLayout()
{
	QFETCH(QByteArray, layout);
	QFETCH(bool, soleAlpha);
	auto file = mxfHqx();
	file.descriptorClass = mxfRgba;
	file.compression.codingLabel = standardUncompressed;
	file.bitDepth.pixelLayout = hexBytes<16>(layout.toHex().constData());
	const auto names = Bella::compressionNamesFor(file);
	if (soleAlpha)
		expectNames(names, QStringLiteral("DNxUncompressed"));
	else
	{
		QVERIFY(names.compressionName.startsWith(QLatin1String("DNxUncompressed")));
		QVERIFY(names.newDnx.isEmpty());
		QVERIFY(names.oldDnx.isEmpty());
		QVERIFY(names.reallyOldDnx.isEmpty());
	}
}

void TestBellaMediaLabels::dnxAlphaNeedsMatchingMetadata_data()
{
	QTest::addColumn<int>("variation");
	QTest::addColumn<QString>("expected");
	QTest::newRow("unavailable-coding") << 0 << QString{};
	QTest::newRow("absent-coding") << 1 << QString{};
	QTest::newRow("unknown-coding") << 2 << QString{};
	QTest::newRow("cdci-descriptor") << 3 << QStringLiteral("DNxUncompressed — 8-bit Integer");
	QTest::newRow("missing-layout") << 4 << QStringLiteral("DNxUncompressed — 10-bit Integer");
	QTest::newRow("fixed-point-label") << 5 << QStringLiteral("DNxUncompressed — 16-bit S2.14 fixed point");
	QTest::newRow("dnxhr-lb-colour") << 6 << QStringLiteral("DNx LB");
}

void TestBellaMediaLabels::dnxAlphaNeedsMatchingMetadata()
{
	QFETCH(int, variation);
	QFETCH(QString, expected);
	auto file = mxfHqx();
	file.descriptorClass = mxfRgba;
	file.compression.codingLabel = standardUncompressed;
	file.bitDepth.pixelLayout = std::array<quint8, 16>{'A', 8};
	switch (variation)
	{
	case 0:
		file.compression.codingState = Bella::MxfCodingState::Unavailable;
		break;
	case 1:
		file.compression.codingState = Bella::MxfCodingState::Absent;
		break;
	case 2:
		file.compression.codingLabel = unknownCoding;
		break;
	case 3:
		file.descriptorClass = mxfCdci;
		break;
	case 4:
		file.bitDepth.pixelLayout.reset();
		break;
	case 5:
		file.compression.codingLabel = fixedUncompressed;
		file.bitDepth.pixelLayout = std::array<quint8, 16>{'A', 254};
		break;
	case 6:
		file.compression.codingLabel = hexBytes<16>("060e2b340401010d0401020271280000");
		break;
	}
	const auto names = Bella::compressionNamesFor(file);
	if (variation == 6)
		expectNames(names, expected, QStringLiteral("DNx LB"), QStringLiteral("DNxHR LB"));
	else
		expectNames(names, expected);
}

void TestBellaMediaLabels::alphaNeedsSourceSpecificProof_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("qualified sole alpha8") << 0;
	QTest::newRow("qualified alpha16 does not fabricate MXF alpha8 proof") << 1;
	QTest::newRow("unavailable coding is not absent") << 2;
	QTest::newRow("alpha presence without sole-component or fallback proof") << 3;
	QTest::newRow("unknown present coding blocks fallback") << 4;
}
void TestBellaMediaLabels::alphaNeedsSourceSpecificProof()
{
	QFETCH(int, variation);
	auto mdb = mdbHqx();
	auto omf = omfHqx();
	mdb.descriptorClass = tag("RGBA");
	omf.descriptorClass = tag("RGBA");
	mdb.compression.codingState = Bella::MdbCodingState::Absent;
	omf.compression.codingState = Bella::OmfCodingState::Absent;
	mdb.compression.legacyCompression = tag("NONE");
	omf.compression.legacyCompression = tag("NONE");
	Bella::MdbComponentArray codes, widths;
	codes.values[0] = 'A';
	codes.count = 1;
	widths.values[0] = variation == 1 ? 16 : 8;
	widths.count = 1;
	if (variation == 3)
	{
		codes.values[1] = 'R';
		codes.count = 2;
		widths.values[1] = 8;
		widths.count = 2;
	}
	mdb.bitDepth.pixelLayout = codes;
	mdb.bitDepth.pixelStructure = widths;
	omf.compression.alphaDepth = variation == 1 ? 16 : 8;
	if (variation == 3)
		omf.compression.alphaDepth.reset();
	Bella::MxfFileInformation mxf;
	mxf.kind = Bella::MxfKind::Video;
	mxf.descriptorClass = mxfRgba;
	mxf.compression.codingState = Bella::MxfCodingState::Absent;
	mxf.compression.namingFallback = variation == 1 || variation == 3 ? Bella::MxfNamingFallback::None
																	  : Bella::MxfNamingFallback::UncompressedAlpha8;
	mxf.bitDepth.pixelLayout = std::array<quint8, 16>{'A', 8};
	if (variation == 2)
	{
		mdb.compression.codingState = Bella::MdbCodingState::Unavailable;
		omf.compression.codingState = Bella::OmfCodingState::Unavailable;
		mxf.compression.codingState = Bella::MxfCodingState::Unavailable;
	}
	if (variation == 4)
	{
		mdb.compression.codingState = Bella::MdbCodingState::Present;
		mdb.compression.codingLabel = unknownCoding;
		omf.compression.codingState = Bella::OmfCodingState::Present;
		omf.compression.codingLabel = unknownCoding;
		mxf.compression.codingState = Bella::MxfCodingState::Present;
		mxf.compression.codingLabel = unknownCoding;
	}
	const auto legacyExpected = variation == 0	 ? QStringLiteral("1:1 Alpha 8bit")
								: variation == 1 ? QStringLiteral("Uncompressed alpha")
												 : QString{};
	expectNames(Bella::compressionNamesFor(mdb), legacyExpected);
	expectNames(Bella::compressionNamesFor(omf), legacyExpected);
	expectNames(Bella::compressionNamesFor(mxf), variation == 0 ? QStringLiteral("1:1 Alpha 8bit") : QString{});
}

// MARK: - Audio and descriptor variants

void TestBellaMediaLabels::audioEncodingIsIndependentOfTiming_data()
{
	QTest::addColumn<int>("encoding");
	QTest::addColumn<QString>("expected");
	QTest::newRow("qualified PCM") << 0 << QStringLiteral("PCM");
	QTest::newRow("qualified IEEE float") << 1 << QStringLiteral("IEEE float");
	QTest::newRow("unknown required summary encoding") << 2 << QString{};
}
void TestBellaMediaLabels::audioEncodingIsIndependentOfTiming()
{
	QFETCH(int, encoding);
	QFETCH(QString, expected);
	Bella::MdbFileRecord mdb{identity()};
	mdb.ownDescriptorQualified = true;
	mdb.kind = Bella::MdbKind::Audio;
	mdb.descriptorClass = tag("WAVD");
	mdb.compression.codingState = Bella::MdbCodingState::Absent;
	mdb.compression.audioEncoding = std::array{Bella::MdbAudioEncoding::Pcm, Bella::MdbAudioEncoding::IeeeFloat,
											   Bella::MdbAudioEncoding::Unknown}[encoding];
	Bella::OmfFileInformation omf;
	omf.kind = Bella::OmfKind::Audio;
	omf.descriptorClass = tag("AIFD");
	omf.compression.codingState = Bella::OmfCodingState::Absent;
	omf.compression.audioEncoding = std::array{Bella::OmfAudioEncoding::Pcm, Bella::OmfAudioEncoding::IeeeFloat,
											   Bella::OmfAudioEncoding::Unknown}[encoding];
	Bella::OmfNativeAudioInformation native;
	native.kind = Bella::OmfKind::Audio;
	native.encoding = std::array{Bella::OmfAudioEncoding::Pcm, Bella::OmfAudioEncoding::IeeeFloat,
								 Bella::OmfAudioEncoding::Unknown}[encoding];
	// Counts/rates/depth are deliberately absent: encoding is an independent fact.
	for (const auto &names : {Bella::compressionNamesFor(mdb), Bella::compressionNamesFor(omf), Bella::compressionNamesFor(native)})
		expectNames(names, expected);
	if (encoding == 2)
	{
		mdb.descriptorClass = tag("PCMA");
		omf.descriptorClass = tag("PCMA");
		expectNames(Bella::compressionNamesFor(mdb), QStringLiteral("PCM"));
		expectNames(Bella::compressionNamesFor(omf), QStringLiteral("PCM"));
	}
}

void TestBellaMediaLabels::contradictoryAudioSummaryKeepsDnxNames()
{
	auto mdb = mdbHqx();
	auto omf = omfHqx();
	// The conflicting DNx and IEEE float facts leave Compression blank.
	// The DNx flavour aliases survive.
	mdb.compression.audioEncoding = Bella::MdbAudioEncoding::IeeeFloat;
	omf.compression.audioEncoding = Bella::OmfAudioEncoding::IeeeFloat;
	for (const auto &names : {Bella::compressionNamesFor(mdb), Bella::compressionNamesFor(omf)})
		expectNames(names, {}, QStringLiteral("DNx HQX"), QStringLiteral("DNxHD HQX"), QStringLiteral("DNxHD 175x"));
	QCOMPARE(mdb.compression.codingLabel, hqx);
	QCOMPARE(omf.compression.audioEncoding, Bella::OmfAudioEncoding::IeeeFloat);
}

void TestBellaMediaLabels::mxfDescriptorKeyVariants_data()
{
	QTest::addColumn<quint8>("codingByte");
	QTest::addColumn<quint8>("registryVersion");
	QTest::addColumn<bool>("knownClass");
	QTest::addColumn<bool>("named");
	QTest::newRow("two-byte set framing") << quint8(0x53) << quint8(1) << true << true;
	QTest::newRow("BER set framing") << quint8(0x13) << quint8(1) << true << true;
	QTest::newRow("two-byte framing with registry-version variation") << quint8(0x53) << quint8(2) << true << true;
	QTest::newRow("BER framing with registry-version variation") << quint8(0x13) << quint8(2) << true << true;
	QTest::newRow("unknown semantic class is not CDCI") << quint8(0x53) << quint8(1) << false << false;
	QTest::newRow("unsupported set framing is not canonicalized") << quint8(0x33) << quint8(1) << true << false;
}
void TestBellaMediaLabels::mxfDescriptorKeyVariants()
{
	QFETCH(quint8, codingByte);
	QFETCH(quint8, registryVersion);
	QFETCH(bool, knownClass);
	QFETCH(bool, named);
	auto file = mxfHqx();
	(*file.descriptorClass)[5] = codingByte;
	(*file.descriptorClass)[7] = registryVersion;
	if (!knownClass)
		(*file.descriptorClass)[14] = 0x7e;
	const auto originalKey = *file.descriptorClass;
	file.compression.codingLabel = hexBytes<16>("060e2b34040101010401020100000000");
	expectNames(Bella::compressionNamesFor(file), named ? QStringLiteral("1:1 YCbCr 10bit") : QString{});
	QCOMPARE(*file.descriptorClass, originalKey);
	file.compression.codingState = Bella::MxfCodingState::Absent;
	// A recognized class and component pattern do not establish missing coding.
	expectNames(Bella::compressionNamesFor(file), {});
}

// MARK: - Effect names and owned results

void TestBellaMediaLabels::effectAndCategory_data()
{
	QTest::addColumn<bool>("precompute");
	QTest::addColumn<QString>("clip");
	QTest::addColumn<QString>("effect");
	QTest::addColumn<QString>("category");
	QTest::newRow("ordinary or unknown Type is not labelled from its name")
		<< false << QStringLiteral("Seq,3D_Warp+2") << QString{} << QString{};
	QTest::newRow("unavailable precompute name") << true << QString{} << QStringLiteral("unknown") << QStringLiteral("unknown");
	QTest::newRow("custom token keeps spaces and underscores") << true << QStringLiteral("Seq, My_custom_token +2")
															   << QStringLiteral(" My_custom_token ") << QStringLiteral("unknown");
	QTest::newRow("malformed overflowing suffix stays visible") << true << QStringLiteral("Seq,Dissolve+2147483648")
																<< QStringLiteral("Dissolve+2147483648") << QStringLiteral("unknown");
	QTest::newRow("sequence commas and rename suffix") << true << QStringLiteral("Act_1,_Scene_2,Resize,12.new.01")
													   << QStringLiteral("Resize") << QStringLiteral("Image");
	QTest::newRow("localised alias yields canonical English name") << true << QStringLiteral("Sequenz_1,Blende+2")
																   << QStringLiteral("Dissolve") << QStringLiteral("Blend");
	QTest::newRow("explicit 3DWarp compatibility alias") << true << QStringLiteral("Seq,3DWarp+1")
														 << QStringLiteral("3D Warp") << QStringLiteral("Blend");
	QTest::newRow("ambiguous categories keep catalogue order") << true << QStringLiteral("Seq,Bottom_to_Top+1")
															   << QStringLiteral("Bottom to Top") << QStringLiteral("Conceal / Peel / Push / Squeeze");
	QTest::newRow("invalid comma suffix is not silently stripped") << true << QStringLiteral("Seq,unknown_effect,4.new.one")
																   << QStringLiteral("4.new.one") << QStringLiteral("unknown");
	QTest::newRow("zero instance is valid") << true << QStringLiteral("Seq,Title,0") << QStringLiteral("Title") << QStringLiteral("Title");
	QTest::newRow("case-sensitive custom name stays custom") << true << QStringLiteral("Seq,dissolve+1")
															 << QStringLiteral("dissolve") << QStringLiteral("unknown");
}
void TestBellaMediaLabels::effectAndCategory()
{
	QFETCH(bool, precompute);
	QFETCH(QString, clip);
	QFETCH(QString, effect);
	QFETCH(QString, category);
	const auto original = clip;
	const auto names = Bella::effectNamesFor(precompute, clip);
	QCOMPARE(names.effectDisplayText, effect);
	QCOMPARE(names.effectCategoryText, category);
	QCOMPARE(clip, original);
}

void TestBellaMediaLabels::inputsAndReturnedNamesAreOwned()
{
	Bella::MdbReport report;
	report.path = QStringLiteral("/not/read/by/the/naming/adapter.mdb");
	report.problemNotes = {QStringLiteral("Original warning."), QStringLiteral("Independent later warning.")};
	report.catalogue.files.append(mdbHqx());
	auto &file = report.catalogue.files.first();
	file.sampleRate->words = {23976, 1000};
	file.compression.resolutionId = 1235;
	const auto originalIdentity = file.fileMobId;
	const auto originalNotes = report.problemNotes;
	const auto originalCoding = file.compression.codingLabel;
	const auto names = Bella::compressionNamesFor(file);
	QCOMPARE(file.fileMobId, originalIdentity);
	QCOMPARE(file.sampleRate->words.numerator, qint32(23976));
	QCOMPARE(file.sampleRate->words.denominator, qint32(1000));
	QCOMPARE(file.sampleRate->encoding, Bella::MdbRateEncoding::ExactEditRate);
	QCOMPARE(file.compression.codingLabel, originalCoding);
	QCOMPARE(file.compression.resolutionId, std::optional<qint64>(1235));
	QCOMPARE(file.compression.namingGeometry->width, qint64(1920));
	QCOMPARE(report.problemNotes, originalNotes);
	file.compression.codingLabel = proresHq;
	file.sampleRate->words = {25, 1};
	expectNames(names, QStringLiteral("DNx HQX [DNxHD 175x]"), QStringLiteral("DNx HQX"),
				QStringLiteral("DNxHD HQX"), QStringLiteral("DNxHD 175x"));
	QString clip = QStringLiteral("Seq,Resize+2");
	const auto effect = Bella::effectNamesFor(true, clip);
	clip.fill('x');
	QCOMPARE(effect.effectDisplayText, QStringLiteral("Resize"));
	QCOMPARE(effect.effectCategoryText, QStringLiteral("Image"));
}

QTEST_GUILESS_MAIN(TestBellaMediaLabels)
#include "tst_bella_media_labels.moc"
