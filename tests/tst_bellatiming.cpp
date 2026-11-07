// Independent arithmetic and timecode boundary expectations. No reader, old
// formatter, native executable or floating-point calculation is used as oracle.
#include "bella/timing.h"
#include "bella/mdbparser.h"
#include "bella/mxfparser.h"
#include "bella/omfparser.h"

#include <QtTest>
#include <limits>
#include <numeric>

Q_DECLARE_METATYPE(Bella::TimingRate)

namespace
{
	using Rate = Bella::TimingRate;
	using Mode = Bella::TimecodeMode;
	constexpr auto maximumCount = std::numeric_limits<qint64>::max();
	constexpr auto maximumWord = std::numeric_limits<quint64>::max();

	bool sameRate(Rate left, Rate right)
	{
		if (!left.numerator || !left.denominator || !right.numerator || !right.denominator)
			return false;
		const auto leftCommon = std::gcd(left.numerator, left.denominator);
		const auto rightCommon = std::gcd(right.numerator, right.denominator);
		return left.numerator / leftCommon == right.numerator / rightCommon &&
			   left.denominator / leftCommon == right.denominator / rightCommon;
	}

	Bella::MobId connectionIdentity()
	{
		QByteArray bytes(32, '\0');
		bytes[31] = '\1';
		return std::get<Bella::MobId>(Bella::normalizeMobId(bytes, Bella::MobIdLayout::Aaf32,
															Bella::ByteOrder::BigEndian));
	}

	std::array<quint8, 4> descriptorTag(QByteArrayView bytes)
	{
		return {quint8(bytes[0]), quint8(bytes[1]), quint8(bytes[2]), quint8(bytes[3])};
	}

	Bella::MdbFileRecord mdbPictureTiming()
	{
		Bella::MdbFileRecord file{connectionIdentity()};
		file.kind = Bella::MdbKind::Video;
		file.descriptorClass = descriptorTag("CDCI");
		file.ownDescriptorQualified = true;
		file.length = 1800;
		file.sampleRate = Bella::MdbRate{{2997, 100}, Bella::MdbRateEncoding::ExactEditRate};
		file.numbering = Bella::MdbNumbering::RecordedNdf;
		return file;
	}

	Bella::MxfFileInformation mxfPictureTiming()
	{
		Bella::MxfFileInformation file;
		file.kind = Bella::MxfKind::Video;
		file.containerDuration = 1800;
		file.sampleRate = Bella::MxfRate{30000, 1001};
		file.frameRateQualified = true;
		file.numbering = Bella::MxfNumbering::RecordedNdf;
		return file;
	}

	Bella::OmfFileInformation omfPictureTiming()
	{
		Bella::OmfFileInformation file;
		file.kind = Bella::OmfKind::Video;
		file.descriptorClass = descriptorTag("CDCI");
		file.length = 1800;
		file.sampleRate = Bella::OmfRate{{2997, 100}, Bella::OmfRateEncoding::ExactEditRate};
		file.durationClockSource = Bella::OmfDurationClockSource::DescriptorSampleRate;
		file.numbering = Bella::OmfNumbering::RecordedNdf;
		return file;
	}
}

class TestBellaTiming final : public QObject
{
	Q_OBJECT
private slots:
	void avidAliases_data();
	void avidAliases();
	void literalRatesAreNotSnapped_data();
	void literalRatesAreNotSnapped();
	void invalidSignedRateWords_data();
	void invalidSignedRateWords();
	void genericDurationDoesNotApplyAvidAliases();
	void physicalCountAndAudioSamplesArePreserved();
	void finalFrameRounding_data();
	void finalFrameRounding();
	void wideExactConversion_data();
	void wideExactConversion();
	void invalidAndOverflowAreUnavailable();
	void ndfLabels_data();
	void ndfLabels();
	void dfBoundaries_data();
	void dfBoundaries();
	void zeroIsDistinctFromUnavailable();
	void invalidPresentation();
	void mdbPictureRateContext_data();
	void mdbPictureRateContext();
	void mdbAudioRateContext_data();
	void mdbAudioRateContext();
	void mdbRequiresOwnedDescriptorPair();
	void mxfContainerClockStaysLiteral();
	void mxfAudioClockIsNotSampleFrequency();
	void mxfVideoDisplayRequiresQualification();
	void omfDurationClockSelection_data();
	void omfDurationClockSelection();
	void omfDescriptorRateContext_data();
	void omfDescriptorRateContext();
	void omfAiff80ClockPreservesUnsigned64();
	void nativeAudioTiming_data();
	void nativeAudioTiming();
	void readerDisplayModes_data();
	void readerDisplayModes();
	void missingDisplayInputsPreserveDuration_data();
	void missingDisplayInputsPreserveDuration();
	void missingCountPreservesDisplay_data();
	void missingCountPreservesDisplay();
	void connectedZeroAndRoundedZero_data();
	void connectedZeroAndRoundedZero();
	void connectionsPreserveInputFactsAndNotes();
	void automaticNominalBase_data();
	void automaticNominalBase();
	void automaticAudioDisplayClock_data();
	void automaticAudioDisplayClock();
	void automaticSourceQualifications();
	void automaticNumberingAndMissingCount_data();
	void automaticNumberingAndMissingCount();
	void explicitBaseRemainsExplicit();
};

void TestBellaTiming::avidAliases_data()
{
	QTest::addColumn<qint32>("numerator");
	QTest::addColumn<qint32>("denominator");
	QTest::addColumn<Rate>("expected");
	for (int family = 0; family != 2; ++family)
		for (qint32 multiplier = 1; multiplier <= 10; ++multiplier)
		{
			const qint32 numerator = (family == 0 ? 23976 : 2997) * multiplier;
			const qint32 denominator = family == 0 ? 1000 : 100;
			const Rate expected{quint64(family == 0 ? 24000 : 30000) * quint64(multiplier), 1001};
			QTest::newRow(qPrintable(QStringLiteral("family-%1-times-%2-original").arg(family).arg(multiplier)))
				<< numerator << denominator << expected;
			const auto common = std::gcd(numerator, denominator);
			if (common != 1)
				QTest::newRow(qPrintable(QStringLiteral("family-%1-times-%2-reduced").arg(family).arg(multiplier)))
					<< qint32(numerator / common) << qint32(denominator / common) << expected;
		}
}
void TestBellaTiming::avidAliases()
{
	QFETCH(qint32, numerator);
	QFETCH(qint32, denominator);
	QFETCH(Rate, expected);
	const auto interpreted = Bella::interpretAvidEditRate(numerator, denominator);
	QVERIFY(interpreted);
	QVERIFY(sameRate(*interpreted, expected));
}

void TestBellaTiming::literalRatesAreNotSnapped_data()
{
	QTest::addColumn<qint32>("numerator");
	QTest::addColumn<qint32>("denominator");
	const auto row = [](const char *name, qint32 numerator, qint32 denominator)
	{
		QTest::newRow(name) << numerator << denominator;
	};
	row("24 fps", 24, 1);
	row("25 fps equivalent fraction", 25000, 1000);
	row("50 fps", 50, 1);
	row("already exact NTSC", 30000, 1001);
	row("already exact film NTSC", 24000, 1001);
	row("slightly above decimal 23.976", 23976001, 1000000);
	row("slightly below decimal 29.97", 29969999, 1000000);
	row("23.976 family multiplier 11 is outside finite rule", 263736, 1000);
	row("29.97 family multiplier 11 is outside finite rule", 32967, 100);
	row("uncommon positive fraction", 12345, 67);
	row("maximum signed source word", std::numeric_limits<qint32>::max(), 1);
}
void TestBellaTiming::literalRatesAreNotSnapped()
{
	QFETCH(qint32, numerator);
	QFETCH(qint32, denominator);
	const auto interpreted = Bella::interpretAvidEditRate(numerator, denominator);
	QVERIFY(interpreted);
	QVERIFY(sameRate(*interpreted, {quint64(numerator), quint64(denominator)}));
}

void TestBellaTiming::invalidSignedRateWords_data()
{
	QTest::addColumn<qint32>("numerator");
	QTest::addColumn<qint32>("denominator");
	QTest::newRow("zero numerator") << qint32(0) << qint32(1);
	QTest::newRow("zero denominator") << qint32(25) << qint32(0);
	QTest::newRow("negative numerator") << qint32(-25) << qint32(1);
	QTest::newRow("negative denominator") << qint32(25) << qint32(-1);
	QTest::newRow("both negative are not positive source words") << qint32(-25) << qint32(-1);
	QTest::newRow("minimum signed word") << std::numeric_limits<qint32>::min() << qint32(1);
}
void TestBellaTiming::invalidSignedRateWords()
{
	QFETCH(qint32, numerator);
	QFETCH(qint32, denominator);
	QVERIFY(!Bella::interpretAvidEditRate(numerator, denominator));
}

void TestBellaTiming::genericDurationDoesNotApplyAvidAliases()
{
	// At this long count the literal decimal and the qualified Avid convention
	// differ by a whole display frame. Generic Rational must remain literal.
	const auto generic = Bella::makeExactDuration(1000000, {2997, 100});
	QVERIFY(generic);
	QCOMPARE(generic->count, qint64(1000000));
	QVERIFY(sameRate(generic->rate, {2997, 100}));
	QCOMPARE(Bella::displayFrameCount(*generic, {30, 1}), std::optional<qint64>(1001001));
	const auto avidRate = Bella::interpretAvidEditRate(2997, 100);
	QVERIFY(avidRate);
	const auto qualifiedAvid = Bella::makeExactDuration(1000000, *avidRate);
	QVERIFY(qualifiedAvid);
	QCOMPARE(Bella::displayFrameCount(*qualifiedAvid, {30, 1}), std::optional<qint64>(1001000));
}

void TestBellaTiming::physicalCountAndAudioSamplesArePreserved()
{
	const auto frame = Bella::makeExactDuration(1, {25, 1});
	QVERIFY(frame);
	QCOMPARE(frame->count, qint64(1));
	QCOMPARE(Bella::formatDuration(*frame, {25, 1}, 25, Mode::NonDropFrame), QStringLiteral("00:00:00:01"));

	// The observed OMF/MDB tone count is sixty seconds plus two samples.
	// A 1,500-unit editorial span is not substituted for those original samples.
	const auto audio = Bella::makeExactDuration(2880002, {48000, 1});
	QVERIFY(audio);
	QCOMPARE(audio->count, qint64(2880002));
	QCOMPARE(Bella::displayFrameCount(*audio, {25, 1}), std::optional<qint64>(1500));
	QCOMPARE(Bella::formatDuration(*audio, {25, 1}, 25, Mode::NonDropFrame), QStringLiteral("00:01:00:00"));
	QCOMPARE(audio->count, qint64(2880002));
	QVERIFY(sameRate(audio->rate, {48000, 1}));
}

void TestBellaTiming::finalFrameRounding_data()
{
	QTest::addColumn<qint64>("count");
	QTest::addColumn<Rate>("sourceRate");
	QTest::addColumn<Rate>("displayRate");
	QTest::addColumn<qint64>("expected");
	const auto row = [](const char *name, qint64 count, Rate source, Rate display, qint64 expected)
	{
		QTest::newRow(name) << count << source << display << expected;
	};
	row("one sample legitimately rounds to zero", 1, {48000, 1}, {25, 1}, 0);
	row("one sample below half frame", 959, {48000, 1}, {25, 1}, 0);
	row("exact half frame rounds up", 960, {48000, 1}, {25, 1}, 1);
	row("one sample above half frame", 961, {48000, 1}, {25, 1}, 1);
	row("one and half frames rounds up", 2880, {48000, 1}, {25, 1}, 2);
	row("round final frames not elapsed seconds", 7, {10, 1}, {25, 1}, 18);
	row("one third second", 1, {3, 1}, {25, 1}, 8);
	row("two thirds second", 2, {3, 1}, {25, 1}, 17);
	row("equivalent unreduced clocks retain the exact count", 1234567, {50000, 2000}, {25, 1}, 1234567);
	row("fractional display clock", 48000, {48000, 1}, {30000, 1001}, 30);
	row("full two sample precision", 2880002, {48000, 1}, {25, 1}, 1500);
}
void TestBellaTiming::finalFrameRounding()
{
	QFETCH(qint64, count);
	QFETCH(Rate, sourceRate);
	QFETCH(Rate, displayRate);
	QFETCH(qint64, expected);
	const auto duration = Bella::makeExactDuration(count, sourceRate);
	QVERIFY(duration);
	QCOMPARE(Bella::displayFrameCount(*duration, displayRate), std::optional<qint64>(expected));
	QCOMPARE(duration->count, count);
}

void TestBellaTiming::wideExactConversion_data()
{
	QTest::addColumn<qint64>("count");
	QTest::addColumn<Rate>("sourceRate");
	QTest::addColumn<Rate>("displayRate");
	QTest::addColumn<qint64>("expected");
	const auto row = [](const char *name, qint64 count, Rate source, Rate display, qint64 expected)
	{
		QTest::newRow(name) << count << source << display << expected;
	};
	// Expected quotients were calculated with independent arbitrary-precision
	// rational arithmetic, not the implementation's limb or cancellation logic.
	row("large 31-bit product with representable quotient", maximumCount,
		{2147483647, 1}, {2147483646, 1}, Q_INT64_C(9223372032559808509));
	row("large audio conversion retains exact quotient", maximumCount - 101,
		{44100, 1}, {24000, 1001}, Q_INT64_C(5014507689238531));
	row("all three numerator factors require 191 bits", maximumCount,
		{maximumWord, maximumWord - 1}, {maximumWord - 2, maximumWord - 3}, maximumCount);
	row("exact representable AIFF80 fraction above signed64", maximumCount,
		{Q_UINT64_C(9223372036854775809), Q_UINT64_C(9223372036854775808)}, {1, 1}, maximumCount - 1);
	row("U64 fraction just below half frame", maximumCount,
		{maximumWord, 1}, {1, 1}, 0);
	row("U64 fraction exactly half frame", maximumCount,
		{maximumWord - 1, 1}, {1, 1}, 1);
	row("rounded result reaches signed maximum without overflowing", maximumCount - 1,
		{maximumWord - 1, maximumWord}, {maximumWord, maximumWord - 1}, maximumCount);
	row("value just above maximum minus half rounds to maximum", maximumCount,
		{maximumWord, maximumWord - 1}, {1, 1}, maximumCount);
	row("full U64 clocks cancel without narrowing", 123,
		{maximumWord, 1}, {maximumWord, 1}, 123);
}
void TestBellaTiming::wideExactConversion()
{
	QFETCH(qint64, count);
	QFETCH(Rate, sourceRate);
	QFETCH(Rate, displayRate);
	QFETCH(qint64, expected);
	const auto duration = Bella::makeExactDuration(count, sourceRate);
	QVERIFY(duration);
	QCOMPARE(duration->count, count);
	QVERIFY(sameRate(duration->rate, sourceRate));
	QCOMPARE(Bella::displayFrameCount(*duration, displayRate), std::optional<qint64>(expected));
}

void TestBellaTiming::invalidAndOverflowAreUnavailable()
{
	QVERIFY(!Bella::makeExactDuration(-1, {25, 1}));
	QVERIFY(!Bella::makeExactDuration(1, {0, 1}));
	QVERIFY(!Bella::makeExactDuration(1, {25, 0}));
	QVERIFY(!Bella::makeExactDuration(0, {0, 0}));
	QVERIFY(!Bella::displayFrameCount({-1, {25, 1}}, {25, 1}));
	QVERIFY(!Bella::displayFrameCount({1, {0, 1}}, {25, 1}));
	QVERIFY(!Bella::displayFrameCount({1, {25, 0}}, {25, 1}));
	QVERIFY(!Bella::displayFrameCount({1, {25, 1}}, {0, 1}));
	QVERIFY(!Bella::displayFrameCount({1, {25, 1}}, {25, 0}));
	const auto exact = Bella::makeExactDuration(maximumCount, {1, 1});
	QVERIFY(exact);
	QCOMPARE(exact->count, maximumCount);
	QVERIFY(!Bella::displayFrameCount(*exact, {2, 1}));
	QVERIFY(!Bella::displayFrameCount({1, {1, maximumWord}}, {1, 1}));
	QVERIFY(!Bella::displayFrameCount({maximumCount, {maximumWord - 1, maximumWord}},
									  {maximumWord, maximumWord - 1})); // Final half-up result is 2^63.
	QVERIFY(!Bella::displayFrameCount({maximumCount, {maximumWord - 1, maximumWord}},
									  {1, 1})); // Exactly INT64_MAX + 1/2; rounding itself overflows.
	QCOMPARE(exact->count, maximumCount);		// Unavailable display is not unavailable source Duration.
}

void TestBellaTiming::ndfLabels_data()
{
	QTest::addColumn<qint64>("frames");
	QTest::addColumn<Rate>("clock");
	QTest::addColumn<quint16>("nominal");
	QTest::addColumn<QString>("expected");
	QTest::newRow("film NTSC uses nominal24 not rounded elapsed seconds")
		<< qint64(24000) << Rate{24000, 1001} << quint16(24) << QStringLiteral("00:16:40:00");
	QTest::newRow("equivalent film clock")
		<< qint64(24) << Rate{48000, 2002} << quint16(24) << QStringLiteral("00:00:01:00");
	QTest::newRow("NTSC NDF minute has no skipped labels")
		<< qint64(1800) << Rate{30000, 1001} << quint16(30) << QStringLiteral("00:01:00:00");
	QTest::newRow("hours do not wrap at24")
		<< qint64(2250000) << Rate{25, 1} << quint16(25) << QStringLiteral("25:00:00:00");
	QTest::newRow("three digit hours are preserved")
		<< qint64(9000000) << Rate{25, 1} << quint16(25) << QStringLiteral("100:00:00:00");
}
void TestBellaTiming::ndfLabels()
{
	QFETCH(qint64, frames);
	QFETCH(Rate, clock);
	QFETCH(quint16, nominal);
	QFETCH(QString, expected);
	const auto duration = Bella::makeExactDuration(frames, clock);
	QVERIFY(duration);
	QCOMPARE(Bella::formatDuration(*duration, clock, nominal, Mode::NonDropFrame), expected);
}

void TestBellaTiming::dfBoundaries_data()
{
	QTest::addColumn<qint64>("frames");
	QTest::addColumn<Rate>("clock");
	QTest::addColumn<quint16>("nominal");
	QTest::addColumn<QString>("expected");
	const auto row = [](const char *name, qint64 frames, Rate clock, quint16 nominal, const char *expected)
	{
		QTest::newRow(name) << frames << clock << nominal << QString::fromLatin1(expected);
	};
	row("30 last frame before first minute", 1799, {30000, 1001}, 30, "00;00;59;29");
	row("30 first minute skips00 and01", 1800, {30000, 1001}, 30, "00;01;00;02");
	row("30 second minute boundary", 3598, {30000, 1001}, 30, "00;02;00;02");
	row("30 last frame before tenth minute", 17981, {30000, 1001}, 30, "00;09;59;29");
	row("30 tenth minute keeps00", 17982, {30000, 1001}, 30, "00;10;00;00");
	row("30 last frame before hour", 107891, {30000, 1001}, 30, "00;59;59;29");
	row("30 hour", 107892, {30000, 1001}, 30, "01;00;00;00");
	row("30 equivalent rational", 1800, {60000, 2002}, 30, "00;01;00;02");
	row("30 no24hour wrap", 2589408, {30000, 1001}, 30, "24;00;00;00");
	row("60 last frame before first minute", 3599, {60000, 1001}, 60, "00;00;59;59");
	row("60 first minute skips00 to03", 3600, {60000, 1001}, 60, "00;01;00;04");
	row("60 second minute boundary", 7196, {60000, 1001}, 60, "00;02;00;04");
	row("60 last frame before tenth minute", 35963, {60000, 1001}, 60, "00;09;59;59");
	row("60 tenth minute keeps00", 35964, {60000, 1001}, 60, "00;10;00;00");
	row("60 last frame before hour", 215783, {60000, 1001}, 60, "00;59;59;59");
	row("60 hour", 215784, {60000, 1001}, 60, "01;00;00;00");
	row("60 equivalent rational", 3600, {120000, 2002}, 60, "00;01;00;04");
}
void TestBellaTiming::dfBoundaries()
{
	QFETCH(qint64, frames);
	QFETCH(Rate, clock);
	QFETCH(quint16, nominal);
	QFETCH(QString, expected);
	const auto duration = Bella::makeExactDuration(frames, clock);
	QVERIFY(duration);
	QCOMPARE(Bella::formatDuration(*duration, clock, nominal, Mode::DropFrame), expected);
}

void TestBellaTiming::zeroIsDistinctFromUnavailable()
{
	const auto zero = Bella::makeExactDuration(0, {25, 1});
	QVERIFY(zero);
	QCOMPARE(zero->count, qint64(0));
	QCOMPARE(Bella::displayFrameCount(*zero, {25, 1}), std::optional<qint64>(0));
	QCOMPARE(Bella::formatDuration(*zero, {25, 1}, 25, Mode::NonDropFrame), QStringLiteral("00:00:00:00"));
	QCOMPARE(Bella::formatDuration(*zero, {30000, 1001}, 30, Mode::DropFrame), QStringLiteral("00;00;00;00"));
	const auto tiny = Bella::makeExactDuration(1, {48000, 1});
	QVERIFY(tiny);
	QCOMPARE(Bella::displayFrameCount(*tiny, {25, 1}), std::optional<qint64>(0));
	QCOMPARE(Bella::formatDuration(*tiny, {25, 1}, 25, Mode::NonDropFrame), QStringLiteral("00:00:00:00"));
	QCOMPARE(tiny->count, qint64(1));
}

void TestBellaTiming::invalidPresentation()
{
	QVERIFY(Bella::formatDuration({-1, {25, 1}}, {25, 1}, 25, Mode::NonDropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({1, {0, 1}}, {25, 1}, 25, Mode::NonDropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({1, {25, 0}}, {25, 1}, 25, Mode::NonDropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({1, {25, 1}}, {25, 1}, 0, Mode::NonDropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({1, {25, 1}}, {0, 1}, 25, Mode::NonDropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({maximumCount, {25, 1}}, {50, 1}, 50, Mode::NonDropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({0, {25, 1}}, {25, 1}, 25, Mode::DropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({1800, {30, 1}}, {30, 1}, 30, Mode::DropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({3600, {60, 1}}, {60, 1}, 60, Mode::DropFrame).isEmpty());
	QVERIFY(Bella::formatDuration({3600, {60000, 1001}}, {60000, 1001}, 30, Mode::DropFrame).isEmpty());
}

void TestBellaTiming::mdbPictureRateContext_data()
{
	QTest::addColumn<QByteArray>("descriptor");
	QTest::addColumn<bool>("exactEncoding");
	QTest::addColumn<bool>("unknownKind");
	QTest::newRow("typed CDCI uses the exact Avid convention") << QByteArray("CDCI") << true << false;
	QTest::newRow("generic CDCI Rational remains literal") << QByteArray("CDCI") << false << false;
	QTest::newRow("TIFD picture context survives Unknown Kind") << QByteArray("TIFD") << true << true;
	QTest::newRow("TIFD Rational remains literal despite picture context") << QByteArray("TIFD") << false << true;
}
void TestBellaTiming::mdbPictureRateContext()
{
	QFETCH(QByteArray, descriptor);
	QFETCH(bool, exactEncoding);
	QFETCH(bool, unknownKind);
	auto file = mdbPictureTiming();
	file.descriptorClass = descriptorTag(descriptor);
	file.kind = unknownKind ? Bella::MdbKind::Unknown : Bella::MdbKind::Video;
	file.length = 1000000;
	file.sampleRate->encoding = exactEncoding ? Bella::MdbRateEncoding::ExactEditRate : Bella::MdbRateEncoding::Rational;
	// An editing clock's presence cannot turn a picture descriptor into audio.
	file.audioEditRate = Bella::MdbRate{{25, 1}, Bella::MdbRateEncoding::ExactEditRate};
	const auto timing = Bella::timingFor(file, quint16(30));
	QVERIFY(timing.duration);
	QVERIFY(timing.display);
	QCOMPARE(timing.duration->count, qint64(1000000));
	QVERIFY(sameRate(timing.duration->rate, exactEncoding ? Rate{30000, 1001} : Rate{2997, 100}));
	QVERIFY(sameRate(timing.display->displayRate, timing.duration->rate));
	QCOMPARE(Bella::displayFrameCount(*timing.duration, {30, 1}),
			 std::optional<qint64>(exactEncoding ? 1001000 : 1001001));
	QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
								   timing.display->nominalBase, timing.display->mode),
			 QStringLiteral("09:15:33:10"));
	QCOMPARE(file.sampleRate->words.numerator, qint32(2997));
	QCOMPARE(file.sampleRate->words.denominator, qint32(100));
}

void TestBellaTiming::mdbAudioRateContext_data()
{
	QTest::addColumn<QByteArray>("descriptor");
	QTest::addColumn<bool>("exactEditingClock");
	for (const QByteArray &descriptor : {QByteArray("PCMA"), QByteArray("MPGA"), QByteArray("WAVE")})
		for (const bool exact : {false, true})
			QTest::newRow(qPrintable(QString::fromLatin1(descriptor) + (exact ? " exact editing clock" : " literal editing clock")))
				<< descriptor << exact;
}
void TestBellaTiming::mdbAudioRateContext()
{
	QFETCH(QByteArray, descriptor);
	QFETCH(bool, exactEditingClock);
	auto file = mdbPictureTiming();
	file.kind = descriptor == "PCMA" ? Bella::MdbKind::Audio : Bella::MdbKind::Unknown;
	file.descriptorClass = descriptorTag(descriptor);
	file.length = 1000000;
	// ExactEditRate on this audio descriptor is the physical count divisor,
	// not the qualified video/editing-clock convention.
	file.sampleRate = Bella::MdbRate{{2997, 100}, Bella::MdbRateEncoding::ExactEditRate};
	file.audioEditRate = Bella::MdbRate{{2997, 100}, exactEditingClock ? Bella::MdbRateEncoding::ExactEditRate : Bella::MdbRateEncoding::Rational};
	file.audioFrequency = Bella::MdbAudioSourceValue(quint32(48000));
	const auto timing = Bella::timingFor(file, quint16(30));
	QVERIFY(timing.duration);
	QVERIFY(timing.display);
	QVERIFY(sameRate(timing.duration->rate, {2997, 100}));
	QVERIFY(sameRate(timing.display->displayRate, exactEditingClock ? Rate{30000, 1001} : Rate{2997, 100}));
	QCOMPARE(Bella::displayFrameCount(*timing.duration, timing.display->displayRate),
			 std::optional<qint64>(exactEditingClock ? 1000001 : 1000000));
	QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
								   timing.display->nominalBase, timing.display->mode),
			 exactEditingClock ? QStringLiteral("09:15:33:11") : QStringLiteral("09:15:33:10"));
	QCOMPARE(std::get<quint32>(*file.audioFrequency), quint32(48000));
}

void TestBellaTiming::mdbRequiresOwnedDescriptorPair()
{
	auto file = mdbPictureTiming();
	file.kind = Bella::MdbKind::Audio;
	file.descriptorClass = descriptorTag("WAVD");
	file.length = 2880002;
	file.sampleRate = Bella::MdbRate{{48000, 1}, Bella::MdbRateEncoding::ExactEditRate};
	file.audioEditRate = Bella::MdbRate{{25, 1}, Bella::MdbRateEncoding::ExactEditRate};
	file.audioFrequency = Bella::MdbAudioSourceValue(quint32(48000));
	const auto complete = Bella::timingFor(file, quint16(25));
	QVERIFY(complete.duration);
	QVERIFY(complete.display);
	QCOMPARE(complete.duration->count, qint64(2880002));
	QCOMPARE(Bella::formatDuration(*complete.duration, complete.display->displayRate,
								   complete.display->nominalBase, complete.display->mode),
			 QStringLiteral("00:01:00:00"));
	file.sampleRate.reset();
	const auto noDescriptorClock = Bella::timingFor(file, quint16(25));
	QVERIFY(!noDescriptorClock.duration); // Readable concrete frequency is not an MDB fallback.
	QVERIFY(noDescriptorClock.display);
	file.sampleRate = Bella::MdbRate{{0, 1}, Bella::MdbRateEncoding::ExactEditRate};
	QVERIFY(!Bella::timingFor(file, quint16(25)).duration);
	file.sampleRate = Bella::MdbRate{{48000, 1}, Bella::MdbRateEncoding::ExactEditRate};
	file.ownDescriptorQualified = false;
	const auto unowned = Bella::timingFor(file, quint16(25));
	QVERIFY(!unowned.duration);
	QVERIFY(!unowned.display);
}

void TestBellaTiming::mxfContainerClockStaysLiteral()
{
	auto file = mxfPictureTiming();
	file.containerDuration = 1000000;
	file.sampleRate = Bella::MxfRate{2997, 100};
	const auto timing = Bella::timingFor(file, quint16(30));
	QVERIFY(timing.duration);
	QVERIFY(timing.display);
	QCOMPARE(timing.duration->count, qint64(1000000));
	QVERIFY(sameRate(timing.duration->rate, {2997, 100}));
	QVERIFY(sameRate(timing.display->displayRate, {2997, 100}));
	QCOMPARE(Bella::displayFrameCount(*timing.duration, {30, 1}), std::optional<qint64>(1001001));
	QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
								   timing.display->nominalBase, timing.display->mode),
			 QStringLiteral("09:15:33:10"));
}

void TestBellaTiming::mxfAudioClockIsNotSampleFrequency()
{
	Bella::MxfFileInformation file;
	file.kind = Bella::MxfKind::Audio;
	file.containerDuration = 1500;
	file.sampleRate = Bella::MxfRate{25, 1};
	file.projectEditRate = Bella::MxfRate{25, 1};
	file.audioSamplingRate = Bella::MxfRate{48000, 1};
	file.numbering = Bella::MxfNumbering::NotRequired;
	const auto timing = Bella::timingFor(file, quint16(25));
	QVERIFY(timing.duration);
	QVERIFY(timing.display);
	QCOMPARE(timing.duration->count, qint64(1500));
	QVERIFY(sameRate(timing.duration->rate, {25, 1}));
	QVERIFY(sameRate(timing.display->displayRate, {25, 1}));
	QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
								   timing.display->nominalBase, timing.display->mode),
			 QStringLiteral("00:01:00:00"));
	file.projectEditRate = Bella::MxfRate{2997, 100};
	const auto literalProjectClock = Bella::timingFor(file, quint16(30));
	QVERIFY(literalProjectClock.duration);
	QVERIFY(literalProjectClock.display);
	QVERIFY(sameRate(literalProjectClock.display->displayRate, {2997, 100}));
	file.sampleRate.reset();
	QVERIFY(!Bella::timingFor(file, quint16(25)).duration);
}

void TestBellaTiming::mxfVideoDisplayRequiresQualification()
{
	auto file = mxfPictureTiming();
	file.frameRateQualified = false;
	file.projectEditRate = Bella::MxfRate{25, 1};
	file.audioSamplingRate = Bella::MxfRate{48000, 1};
	const auto timing = Bella::timingFor(file, quint16(30));
	QVERIFY(timing.duration);
	QVERIFY(!timing.display);
	QCOMPARE(timing.duration->count, qint64(1800));
	QVERIFY(sameRate(timing.duration->rate, {30000, 1001}));
}

void TestBellaTiming::omfDurationClockSelection_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("descriptor pair wins over separate frequency") << 0;
	QTest::newRow("qualified SummaryFrequency wins over retained descriptor pair") << 1;
	QTest::newRow("Unavailable cannot be repaired by readable frequency") << 2;
	QTest::newRow("missing selected summary frequency is unavailable") << 3;
	QTest::newRow("missing selected descriptor clock cannot use frequency") << 4;
	QTest::newRow("zero selected summary frequency is unavailable") << 5;
}
void TestBellaTiming::omfDurationClockSelection()
{
	QFETCH(int, variation);
	Bella::OmfFileInformation file;
	file.kind = Bella::OmfKind::Audio;
	file.descriptorClass = descriptorTag("WAVD");
	file.length = 2880002;
	file.sampleRate = Bella::OmfRate{{48000, 1}, Bella::OmfRateEncoding::ExactEditRate};
	file.audioFrequency = Bella::OmfAudioSourceValue(quint32(variation == 0 ? 44100 : 48000));
	file.audioEditRate = Bella::OmfRate{{25, 1}, Bella::OmfRateEncoding::ExactEditRate};
	file.numbering = Bella::OmfNumbering::NotRequired;
	file.durationClockSource = variation == 0 || variation == 4 ? Bella::OmfDurationClockSource::DescriptorSampleRate
							   : variation == 2					? Bella::OmfDurationClockSource::Unavailable
																: Bella::OmfDurationClockSource::SummaryFrequency;
	if (variation == 1)
		file.sampleRate = Bella::OmfRate{{44100, 1}, Bella::OmfRateEncoding::ExactEditRate};
	if (variation == 3)
		file.audioFrequency.reset();
	if (variation == 4)
		file.sampleRate.reset();
	if (variation == 5)
		file.audioFrequency = Bella::OmfAudioSourceValue(quint32(0));
	const auto timing = Bella::timingFor(file, quint16(25));
	QCOMPARE(bool(timing.duration), variation <= 1);
	QVERIFY(timing.display); // A separately qualified display clock is not the duration divisor.
	if (timing.duration)
	{
		QCOMPARE(timing.duration->count, qint64(2880002));
		QVERIFY(sameRate(timing.duration->rate, {48000, 1}));
		QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
									   timing.display->nominalBase, timing.display->mode),
				 QStringLiteral("00:01:00:00"));
	}
}

void TestBellaTiming::omfDescriptorRateContext_data()
{
	QTest::addColumn<bool>("picture");
	QTest::addColumn<bool>("exactEncoding");
	QTest::newRow("OMF typed picture uses the Avid convention") << true << true;
	QTest::newRow("OMF picture Rational stays literal") << true << false;
	QTest::newRow("OMF typed audio divisor stays literal") << false << true;
	QTest::newRow("OMF audio Rational stays literal") << false << false;
}
void TestBellaTiming::omfDescriptorRateContext()
{
	QFETCH(bool, picture);
	QFETCH(bool, exactEncoding);
	auto file = omfPictureTiming();
	file.kind = picture ? Bella::OmfKind::Video : Bella::OmfKind::Audio;
	file.descriptorClass = descriptorTag(picture ? QByteArrayView("CDCI") : QByteArrayView("WAVD"));
	file.length = 1000000;
	file.sampleRate->encoding = exactEncoding ? Bella::OmfRateEncoding::ExactEditRate : Bella::OmfRateEncoding::Rational;
	file.audioEditRate = Bella::OmfRate{{25, 1}, Bella::OmfRateEncoding::ExactEditRate};
	const auto timing = Bella::timingFor(file, picture ? quint16(30) : quint16(25));
	QVERIFY(timing.duration);
	QVERIFY(timing.display);
	const bool applyAlias = picture && exactEncoding;
	QVERIFY(sameRate(timing.duration->rate, applyAlias ? Rate{30000, 1001} : Rate{2997, 100}));
	QCOMPARE(Bella::displayFrameCount(*timing.duration, {30, 1}),
			 std::optional<qint64>(applyAlias ? 1001000 : 1001001));
	QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
								   timing.display->nominalBase, timing.display->mode),
			 picture ? QStringLiteral("09:15:33:10") : QStringLiteral("09:16:06:18"));
}

void TestBellaTiming::omfAiff80ClockPreservesUnsigned64()
{
	Bella::OmfFileInformation file;
	file.kind = Bella::OmfKind::Audio;
	file.descriptorClass = descriptorTag("AIFD");
	file.length = 2880002;
	file.durationClockSource = Bella::OmfDurationClockSource::SummaryFrequency;
	file.audioEditRate = Bella::OmfRate{{25, 1}, Bella::OmfRateEncoding::ExactEditRate};
	file.numbering = Bella::OmfNumbering::NotRequired;
	const std::array<quint8, 10> frequency48000{0x40, 0x0e, 0xbb, 0x80, 0, 0, 0, 0, 0, 0};
	file.audioFrequency = Bella::OmfAudioSourceValue(frequency48000);
	const auto tone = Bella::timingFor(file, quint16(25));
	QVERIFY(tone.duration);
	QVERIFY(tone.display);
	QVERIFY(sameRate(tone.duration->rate, {48000, 1}));
	QCOMPARE(Bella::formatDuration(*tone.duration, tone.display->displayRate,
								   tone.display->nominalBase, tone.display->mode),
			 QStringLiteral("00:01:00:00"));
	// Exact normalized IEEE80 value 1 + 2^-63. Both rational words require U64.
	const std::array<quint8, 10> wideFrequency{0x3f, 0xff, 0x80, 0, 0, 0, 0, 0, 0, 1};
	file.length = 1;
	file.audioFrequency = Bella::OmfAudioSourceValue(wideFrequency);
	const auto wide = Bella::timingFor(file, quint16(25));
	QVERIFY(wide.duration);
	QVERIFY(wide.display);
	QVERIFY(sameRate(wide.duration->rate, {Q_UINT64_C(9223372036854775809), Q_UINT64_C(9223372036854775808)}));
	QCOMPARE(Bella::formatDuration(*wide.duration, wide.display->displayRate,
								   wide.display->nominalBase, wide.display->mode),
			 QStringLiteral("00:00:01:00"));
	const auto retainedFrequency = std::get<std::array<quint8, 10>>(*file.audioFrequency);
	QCOMPARE(retainedFrequency, wideFrequency);
}

void TestBellaTiming::nativeAudioTiming_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("native exact tone retains two extra samples") << 0;
	QTest::newRow("native known zero remains exact") << 1;
	QTest::newRow("native decimal sampling fraction stays literal") << 2;
	QTest::newRow("native missing count leaves exact unavailable") << 3;
	QTest::newRow("native missing rate leaves exact unavailable") << 4;
	QTest::newRow("native invalid rate leaves exact unavailable") << 5;
}
void TestBellaTiming::nativeAudioTiming()
{
	QFETCH(int, variation);
	Bella::OmfNativeAudioInformation file;
	file.kind = Bella::OmfKind::Audio;
	file.sampleFrameCount = variation == 1 ? 0 : variation == 2 ? 1000000
																: 2880002;
	file.samplingRate = variation == 2 ? Bella::OmfNativeRate{2997, 100} : Bella::OmfNativeRate{48000, 1};
	if (variation == 3)
		file.sampleFrameCount.reset();
	if (variation == 4)
		file.samplingRate.reset();
	if (variation == 5)
		file.samplingRate = Bella::OmfNativeRate{-48000, 1};
	const auto timing = Bella::timingFor(file);
	QVERIFY(!timing.display); // No display clock or numbering exists in this value.
	QCOMPARE(bool(timing.duration), variation < 3);
	if (timing.duration)
	{
		QCOMPARE(timing.duration->count, variation == 1 ? qint64(0) : variation == 2 ? qint64(1000000)
																					 : qint64(2880002));
		QVERIFY(sameRate(timing.duration->rate, variation == 2 ? Rate{2997, 100} : Rate{48000, 1}));
	}
}

void TestBellaTiming::readerDisplayModes_data()
{
	QTest::addColumn<int>("reader");
	QTest::addColumn<int>("numbering");
	for (int reader = 0; reader < 3; ++reader)
		for (int numbering = 0; numbering < 5; ++numbering)
			QTest::newRow(qPrintable(QStringLiteral("reader-%1-numbering-%2").arg(reader).arg(numbering)))
				<< reader << numbering;
}
void TestBellaTiming::readerDisplayModes()
{
	QFETCH(int, reader);
	QFETCH(int, numbering);
	Bella::FileTiming timing;
	if (reader == 0)
	{
		auto file = mdbPictureTiming();
		file.numbering = std::array{Bella::MdbNumbering::Unavailable, Bella::MdbNumbering::NotRequired,
									Bella::MdbNumbering::MissingNdfFallback, Bella::MdbNumbering::RecordedNdf, Bella::MdbNumbering::RecordedDf}[numbering];
		timing = Bella::timingFor(file, quint16(30));
	}
	else if (reader == 1)
	{
		auto file = mxfPictureTiming();
		file.numbering = std::array{Bella::MxfNumbering::Unavailable, Bella::MxfNumbering::NotRequired,
									Bella::MxfNumbering::MissingNdfFallback, Bella::MxfNumbering::RecordedNdf, Bella::MxfNumbering::RecordedDf}[numbering];
		timing = Bella::timingFor(file, quint16(30));
	}
	else
	{
		auto file = omfPictureTiming();
		file.numbering = std::array{Bella::OmfNumbering::Unavailable, Bella::OmfNumbering::NotRequired,
									Bella::OmfNumbering::MissingNdfFallback, Bella::OmfNumbering::RecordedNdf, Bella::OmfNumbering::RecordedDf}[numbering];
		timing = Bella::timingFor(file, quint16(30));
	}
	QVERIFY(timing.duration);
	QCOMPARE(bool(timing.display), numbering != 0);
	if (timing.display)
	{
		QVERIFY(sameRate(timing.display->displayRate, {30000, 1001}));
		QCOMPARE(timing.display->nominalBase, quint16(30));
		QCOMPARE(timing.display->mode, numbering == 4 ? Mode::DropFrame : Mode::NonDropFrame);
		QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
									   timing.display->nominalBase, timing.display->mode),
				 numbering == 4 ? QStringLiteral("00;01;00;02") : QStringLiteral("00:01:00:00"));
	}
}

void TestBellaTiming::missingDisplayInputsPreserveDuration_data()
{
	QTest::addColumn<int>("reader");
	QTest::addColumn<int>("missing");
	for (int reader = 0; reader < 3; ++reader)
		for (int missing = 0; missing < 3; ++missing)
			QTest::newRow(qPrintable(QStringLiteral("reader-%1-missing-%2").arg(reader).arg(missing)))
				<< reader << missing;
}
void TestBellaTiming::missingDisplayInputsPreserveDuration()
{
	QFETCH(int, reader);
	QFETCH(int, missing);
	const auto base = missing == 0 ? std::optional<quint16>{} : std::optional<quint16>(missing == 1 ? 0 : 25);
	Bella::FileTiming timing;
	if (reader == 0)
	{
		auto file = mdbPictureTiming();
		file.kind = Bella::MdbKind::Audio;
		file.descriptorClass = descriptorTag("PCMA");
		file.length = 2880002;
		file.sampleRate = Bella::MdbRate{{48000, 1}, Bella::MdbRateEncoding::ExactEditRate};
		file.audioEditRate = Bella::MdbRate{{25, 1}, Bella::MdbRateEncoding::ExactEditRate};
		if (missing == 2)
			file.audioEditRate.reset();
		timing = Bella::timingFor(file, base);
	}
	else if (reader == 1)
	{
		Bella::MxfFileInformation file;
		file.kind = Bella::MxfKind::Audio;
		file.containerDuration = 2880002;
		file.sampleRate = Bella::MxfRate{48000, 1};
		file.projectEditRate = Bella::MxfRate{25, 1};
		file.audioSamplingRate = Bella::MxfRate{48000, 1};
		file.numbering = Bella::MxfNumbering::RecordedNdf;
		if (missing == 2)
			file.projectEditRate.reset();
		timing = Bella::timingFor(file, base);
	}
	else
	{
		Bella::OmfFileInformation file;
		file.kind = Bella::OmfKind::Audio;
		file.descriptorClass = descriptorTag("PCMA");
		file.length = 2880002;
		file.sampleRate = Bella::OmfRate{{48000, 1}, Bella::OmfRateEncoding::ExactEditRate};
		file.durationClockSource = Bella::OmfDurationClockSource::DescriptorSampleRate;
		file.audioEditRate = Bella::OmfRate{{25, 1}, Bella::OmfRateEncoding::ExactEditRate};
		file.numbering = Bella::OmfNumbering::RecordedNdf;
		if (missing == 2)
			file.audioEditRate.reset();
		timing = Bella::timingFor(file, base);
	}
	QVERIFY(timing.duration);
	QVERIFY(!timing.display);
	QCOMPARE(timing.duration->count, qint64(2880002));
	QVERIFY(sameRate(timing.duration->rate, {48000, 1}));
}

void TestBellaTiming::missingCountPreservesDisplay_data()
{
	QTest::addColumn<int>("reader");
	QTest::newRow("MDB display does not require Length") << 0;
	QTest::newRow("MXF display does not require ContainerDuration") << 1;
	QTest::newRow("OMF display does not require Length") << 2;
}
void TestBellaTiming::missingCountPreservesDisplay()
{
	QFETCH(int, reader);
	Bella::FileTiming timing;
	if (reader == 0)
	{
		auto file = mdbPictureTiming();
		file.length.reset();
		file.numbering = Bella::MdbNumbering::NotRequired;
		timing = Bella::timingFor(file, quint16(30));
	}
	else if (reader == 1)
	{
		auto file = mxfPictureTiming();
		file.containerDuration.reset();
		file.numbering = Bella::MxfNumbering::NotRequired;
		timing = Bella::timingFor(file, quint16(30));
	}
	else
	{
		auto file = omfPictureTiming();
		file.length.reset();
		file.numbering = Bella::OmfNumbering::NotRequired;
		timing = Bella::timingFor(file, quint16(30));
	}
	QVERIFY(!timing.duration);
	QVERIFY(timing.display);
	QVERIFY(sameRate(timing.display->displayRate, {30000, 1001}));
	QCOMPARE(timing.display->nominalBase, quint16(30));
	QCOMPARE(timing.display->mode, Mode::NonDropFrame);
}

void TestBellaTiming::connectedZeroAndRoundedZero_data()
{
	QTest::addColumn<int>("reader");
	QTest::addColumn<qint64>("count");
	for (int reader = 0; reader < 3; ++reader)
		for (qint64 count = 0; count < 2; ++count)
			QTest::newRow(qPrintable(QStringLiteral("reader-%1-count-%2").arg(reader).arg(count))) << reader << count;
}
void TestBellaTiming::connectedZeroAndRoundedZero()
{
	QFETCH(int, reader);
	QFETCH(qint64, count);
	Bella::FileTiming timing;
	if (reader == 0)
	{
		auto file = mdbPictureTiming();
		file.kind = Bella::MdbKind::Audio;
		file.descriptorClass = descriptorTag("PCMA");
		file.length = count;
		file.sampleRate = Bella::MdbRate{{48000, 1}, Bella::MdbRateEncoding::ExactEditRate};
		file.audioEditRate = Bella::MdbRate{{25, 1}, Bella::MdbRateEncoding::ExactEditRate};
		file.numbering = Bella::MdbNumbering::NotRequired;
		timing = Bella::timingFor(file, quint16(25));
	}
	else if (reader == 1)
	{
		Bella::MxfFileInformation file;
		file.kind = Bella::MxfKind::Audio;
		file.containerDuration = count;
		file.sampleRate = Bella::MxfRate{48000, 1};
		file.projectEditRate = Bella::MxfRate{25, 1};
		file.numbering = Bella::MxfNumbering::NotRequired;
		timing = Bella::timingFor(file, quint16(25));
	}
	else
	{
		Bella::OmfFileInformation file;
		file.kind = Bella::OmfKind::Audio;
		file.descriptorClass = descriptorTag("PCMA");
		file.length = count;
		file.sampleRate = Bella::OmfRate{{48000, 1}, Bella::OmfRateEncoding::ExactEditRate};
		file.durationClockSource = Bella::OmfDurationClockSource::DescriptorSampleRate;
		file.audioEditRate = Bella::OmfRate{{25, 1}, Bella::OmfRateEncoding::ExactEditRate};
		file.numbering = Bella::OmfNumbering::NotRequired;
		timing = Bella::timingFor(file, quint16(25));
	}
	QVERIFY(timing.duration);
	QVERIFY(timing.display);
	QCOMPARE(timing.duration->count, count);
	QCOMPARE(Bella::displayFrameCount(*timing.duration, timing.display->displayRate), std::optional<qint64>(0));
	QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
								   timing.display->nominalBase, timing.display->mode),
			 QStringLiteral("00:00:00:00"));
}

void TestBellaTiming::connectionsPreserveInputFactsAndNotes()
{
	Bella::OmfReport report;
	report.path = QStringLiteral("/a/path/that/the/timing/connection/does/not/read");
	report.nativeAudioReading = Bella::OmfReadResult::Incomplete;
	report.omfReading = Bella::OmfReadResult::Malformed;
	report.problemNotes = {QStringLiteral("Earlier native note."), QStringLiteral("Later OMF note.")};
	report.nativeAudioInformation.kind = Bella::OmfKind::Audio;
	report.nativeAudioInformation.sampleFrameCount = 1;
	report.nativeAudioInformation.samplingRate = Bella::OmfNativeRate{48000, 1};
	report.fileInformation = omfPictureTiming();
	report.fileInformation.length = 0;
	const auto notes = report.problemNotes;
	const auto path = report.path;
	const auto native = Bella::timingFor(report.nativeAudioInformation);
	const auto omf = Bella::timingFor(report.fileInformation, quint16(30));
	QVERIFY(native.duration);
	QVERIFY(!native.display);
	QCOMPARE(native.duration->count, qint64(1));
	QVERIFY(omf.duration);
	QVERIFY(omf.display);
	QCOMPARE(omf.duration->count, qint64(0));
	QCOMPARE(Bella::formatDuration(*omf.duration, omf.display->displayRate,
								   omf.display->nominalBase, omf.display->mode),
			 QStringLiteral("00:00:00:00"));
	QCOMPARE(report.problemNotes, notes);
	QCOMPARE(report.path, path);
	QCOMPARE(report.nativeAudioReading, Bella::OmfReadResult::Incomplete);
	QCOMPARE(report.omfReading, Bella::OmfReadResult::Malformed);
	QCOMPARE(report.nativeAudioInformation.sampleFrameCount, std::optional<qint64>(1));
	QCOMPARE(report.nativeAudioInformation.samplingRate->numerator, qint32(48000));
	QCOMPARE(report.fileInformation.length, std::optional<qint64>(0));
	QCOMPARE(report.fileInformation.sampleRate->words.numerator, qint32(2997));
	QCOMPARE(report.fileInformation.sampleRate->words.denominator, qint32(100));
	QCOMPARE(report.fileInformation.numbering, Bella::OmfNumbering::RecordedNdf);
	// Returned values are owned and do not change when the source value changes.
	report.nativeAudioInformation.sampleFrameCount = 999;
	report.fileInformation.sampleRate->words.numerator = 25;
	QCOMPARE(native.duration->count, qint64(1));
	QVERIFY(sameRate(omf.duration->rate, {30000, 1001}));
}

void TestBellaTiming::automaticNominalBase_data()
{
	QTest::addColumn<qint32>("numerator");
	QTest::addColumn<qint32>("denominator");
	QTest::addColumn<int>("expectedBase");
	QTest::newRow("whole 25") << qint32(25) << qint32(1) << 25;
	QTest::newRow("NTSC 30") << qint32(30000) << qint32(1001) << 30;
	QTest::newRow("fractional film 24") << qint32(24000) << qint32(1001) << 24;
	QTest::newRow("unlisted exact half rounds upward") << qint32(25) << qint32(2) << 13;
	QTest::newRow("equivalent unreduced half") << qint32(50) << qint32(4) << 13;
	QTest::newRow("just below half") << qint32(24999) << qint32(2000) << 12;
	QTest::newRow("just above half") << qint32(25001) << qint32(2000) << 13;
	QTest::newRow("positive clock rounded to zero") << qint32(49) << qint32(100) << -1;
	QTest::newRow("half reaches first positive base") << qint32(1) << qint32(2) << 1;
	QTest::newRow("odd denominator below first half") << qint32(1073741823) << qint32(2147483647) << -1;
	QTest::newRow("odd denominator above first half") << qint32(1073741824) << qint32(2147483647) << 1;
	QTest::newRow("largest nominal base") << qint32(65535) << qint32(1) << 65535;
	QTest::newRow("round down at upper boundary") << qint32(1310709999) << qint32(20000) << 65535;
	QTest::newRow("half would overflow nominal base") << qint32(131071) << qint32(2) << -1;
	QTest::newRow("whole base outside UInt16") << qint32(65536) << qint32(1) << -1;
	QTest::newRow("largest signed source word") << qint32(2147483647) << qint32(1) << -1;
}

void TestBellaTiming::automaticNominalBase()
{
	QFETCH(qint32, numerator);
	QFETCH(qint32, denominator);
	QFETCH(int, expectedBase);
	auto mdb = mdbPictureTiming();
	mdb.sampleRate = Bella::MdbRate{{numerator, denominator}, Bella::MdbRateEncoding::Rational};
	mdb.numbering = Bella::MdbNumbering::NotRequired;
	auto mxf = mxfPictureTiming();
	mxf.sampleRate = Bella::MxfRate{numerator, denominator};
	mxf.numbering = Bella::MxfNumbering::NotRequired;
	auto omf = omfPictureTiming();
	omf.sampleRate = Bella::OmfRate{{numerator, denominator}, Bella::OmfRateEncoding::Rational};
	omf.numbering = Bella::OmfNumbering::NotRequired;
	for (const auto &timing : {Bella::timingFor(mdb), Bella::timingFor(mxf), Bella::timingFor(omf)})
	{
		QVERIFY(timing.duration);
		QCOMPARE(timing.duration->count, qint64(1800));
		QCOMPARE(timing.duration->rate.numerator, quint64(numerator));
		QCOMPARE(timing.duration->rate.denominator, quint64(denominator));
		QCOMPARE(bool(timing.display), expectedBase > 0);
		if (timing.display)
		{
			QCOMPARE(timing.display->nominalBase, quint16(expectedBase));
			QCOMPARE(timing.display->displayRate.numerator, quint64(numerator));
			QCOMPARE(timing.display->displayRate.denominator, quint64(denominator));
			QCOMPARE(timing.display->mode, Mode::NonDropFrame);
		}
	}
	QCOMPARE(mdb.sampleRate->words.numerator, numerator);
	QCOMPARE(mxf.sampleRate->denominator, denominator);
	QCOMPARE(omf.sampleRate->words.denominator, denominator);
}

void TestBellaTiming::automaticAudioDisplayClock_data()
{
	QTest::addColumn<int>("reader");
	QTest::addColumn<int>("variation");
	for (int reader = 0; reader != 3; ++reader)
		for (int variation = 0; variation != 3; ++variation)
			QTest::newRow(qPrintable(QStringLiteral("reader-%1-clock-%2").arg(reader).arg(variation)))
				<< reader << variation;
}

void TestBellaTiming::automaticAudioDisplayClock()
{
	QFETCH(int, reader);
	QFETCH(int, variation);
	const qint32 numerator = variation == 1 ? 25 : 250;
	const qint32 denominator = variation == 1 ? 2 : 10;
	Bella::FileTiming timing;
	if (reader == 0)
	{
		auto file = mdbPictureTiming();
		file.kind = Bella::MdbKind::Unknown;
		file.descriptorClass = descriptorTag("MPGA");
		file.length = 96000;
		file.sampleRate = Bella::MdbRate{{48000, 1}, Bella::MdbRateEncoding::ExactEditRate};
		file.audioFrequency = Bella::MdbAudioSourceValue(quint32(96000));
		file.audioEditRate = Bella::MdbRate{{numerator, denominator}, Bella::MdbRateEncoding::Rational};
		file.numbering = Bella::MdbNumbering::NotRequired;
		if (variation == 2)
			file.audioEditRate.reset();
		timing = Bella::timingFor(file);
	}
	else if (reader == 1)
	{
		Bella::MxfFileInformation file;
		file.kind = Bella::MxfKind::Audio;
		file.containerDuration = 96000;
		file.sampleRate = Bella::MxfRate{48000, 1};
		file.audioSamplingRate = Bella::MxfRate{96000, 1};
		file.projectEditRate = Bella::MxfRate{numerator, denominator};
		file.numbering = Bella::MxfNumbering::NotRequired;
		if (variation == 2)
			file.projectEditRate.reset();
		timing = Bella::timingFor(file);
	}
	else
	{
		auto file = omfPictureTiming();
		file.kind = Bella::OmfKind::Audio;
		file.descriptorClass = descriptorTag("PCMA");
		file.length = 96000;
		file.sampleRate = Bella::OmfRate{{48000, 1}, Bella::OmfRateEncoding::ExactEditRate};
		file.audioFrequency = Bella::OmfAudioSourceValue(quint32(96000));
		file.audioEditRate = Bella::OmfRate{{numerator, denominator}, Bella::OmfRateEncoding::Rational};
		file.numbering = Bella::OmfNumbering::NotRequired;
		if (variation == 2)
			file.audioEditRate.reset();
		timing = Bella::timingFor(file);
	}
	QVERIFY(timing.duration);
	QCOMPARE(timing.duration->count, qint64(96000));
	QVERIFY(sameRate(timing.duration->rate, {48000, 1}));
	QCOMPARE(bool(timing.display), variation != 2);
	if (timing.display)
	{
		QCOMPARE(timing.display->nominalBase, quint16(variation == 1 ? 13 : 25));
		QVERIFY(sameRate(timing.display->displayRate, {quint64(numerator), quint64(denominator)}));
		QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
									   timing.display->nominalBase, timing.display->mode),
				 variation == 1 ? QStringLiteral("00:00:01:12") : QStringLiteral("00:00:02:00"));
	}
}

void TestBellaTiming::automaticSourceQualifications()
{
	auto mdb = mdbPictureTiming();
	mdb.ownDescriptorQualified = false;
	const auto unowned = Bella::timingFor(mdb);
	QVERIFY(!unowned.duration);
	QVERIFY(!unowned.display);
	mdb.ownDescriptorQualified = true;
	mdb.kind = Bella::MdbKind::Unknown;
	mdb.descriptorClass = descriptorTag("TIFD");
	const auto knownPicture = Bella::timingFor(mdb);
	QVERIFY(knownPicture.duration);
	QVERIFY(knownPicture.display);
	QVERIFY(sameRate(knownPicture.display->displayRate, {30000, 1001}));
	QCOMPARE(knownPicture.display->nominalBase, quint16(30));

	auto mxf = mxfPictureTiming();
	mxf.frameRateQualified = false;
	const auto unqualified = Bella::timingFor(mxf);
	QVERIFY(unqualified.duration);
	QVERIFY(!unqualified.display);

	auto omf = omfPictureTiming();
	omf.durationClockSource = Bella::OmfDurationClockSource::Unavailable;
	const auto noDivisor = Bella::timingFor(omf);
	QVERIFY(!noDivisor.duration);
	QVERIFY(noDivisor.display);
	QCOMPARE(noDivisor.display->nominalBase, quint16(30));
}

void TestBellaTiming::automaticNumberingAndMissingCount_data()
{
	QTest::addColumn<int>("reader");
	QTest::addColumn<int>("variation");
	for (int reader = 0; reader != 3; ++reader)
		for (int variation = 0; variation != 5; ++variation)
			QTest::newRow(qPrintable(QStringLiteral("reader-%1-numbering-%2").arg(reader).arg(variation)))
				<< reader << variation;
}

void TestBellaTiming::automaticNumberingAndMissingCount()
{
	QFETCH(int, reader);
	QFETCH(int, variation);
	auto mdb = mdbPictureTiming();
	auto mxf = mxfPictureTiming();
	auto omf = omfPictureTiming();
	mdb.numbering = Bella::MdbNumbering::RecordedDf;
	mxf.numbering = Bella::MxfNumbering::RecordedDf;
	omf.numbering = Bella::OmfNumbering::RecordedDf;
	if (variation == 1)
	{
		mdb.sampleRate->words = {5994, 100};
		mxf.sampleRate = Bella::MxfRate{60000, 1001};
		omf.sampleRate->words = {5994, 100};
		mdb.length = 3600;
		mxf.containerDuration = 3600;
		omf.length = 3600;
	}
	else if (variation == 2)
	{
		mdb.sampleRate->words = {25, 1};
		mxf.sampleRate = Bella::MxfRate{25, 1};
		omf.sampleRate->words = {25, 1};
	}
	else if (variation == 3)
	{
		mdb.numbering = Bella::MdbNumbering::Unavailable;
		mxf.numbering = Bella::MxfNumbering::Unavailable;
		omf.numbering = Bella::OmfNumbering::Unavailable;
	}
	else if (variation == 4)
	{
		mdb.length.reset();
		mxf.containerDuration.reset();
		omf.length.reset();
	}
	const auto timing = reader == 0	  ? Bella::timingFor(mdb)
						: reader == 1 ? Bella::timingFor(mxf)
									  : Bella::timingFor(omf);
	QCOMPARE(bool(timing.duration), variation != 4);
	QCOMPARE(bool(timing.display), variation != 2 && variation != 3);
	if (timing.display)
	{
		QCOMPARE(timing.display->nominalBase, quint16(variation == 1 ? 60 : 30));
		QCOMPARE(timing.display->mode, Mode::DropFrame);
		if (timing.duration)
			QCOMPARE(Bella::formatDuration(*timing.duration, timing.display->displayRate,
										   timing.display->nominalBase, timing.display->mode),
					 variation == 1 ? QStringLiteral("00;01;00;04") : QStringLiteral("00;01;00;02"));
	}
}

void TestBellaTiming::explicitBaseRemainsExplicit()
{
	auto mdb = mdbPictureTiming();
	mdb.sampleRate = Bella::MdbRate{{25, 2}, Bella::MdbRateEncoding::Rational};
	auto mxf = mxfPictureTiming();
	mxf.sampleRate = Bella::MxfRate{25, 2};
	auto omf = omfPictureTiming();
	omf.sampleRate = Bella::OmfRate{{25, 2}, Bella::OmfRateEncoding::Rational};
	for (const auto &timing : {Bella::timingFor(mdb, std::nullopt), Bella::timingFor(mdb, quint16(0)),
							   Bella::timingFor(mxf, std::nullopt), Bella::timingFor(mxf, quint16(0)),
							   Bella::timingFor(omf, std::nullopt), Bella::timingFor(omf, quint16(0))})
	{
		QVERIFY(timing.duration);
		QVERIFY(!timing.display);
		QVERIFY(sameRate(timing.duration->rate, {25, 2}));
	}
	for (const auto &timing : {Bella::timingFor(mdb, quint16(7)), Bella::timingFor(mxf, quint16(7)),
							   Bella::timingFor(omf, quint16(7))})
	{
		QVERIFY(timing.duration);
		QVERIFY(timing.display);
		QCOMPARE(timing.display->nominalBase, quint16(7));
		QVERIFY(sameRate(timing.display->displayRate, {25, 2}));
	}
	for (const auto &timing : {Bella::timingFor(mdb), Bella::timingFor(mxf), Bella::timingFor(omf)})
	{
		QVERIFY(timing.duration);
		QVERIFY(timing.display);
		QCOMPARE(timing.display->nominalBase, quint16(13));
	}
}

QTEST_GUILESS_MAIN(TestBellaTiming)
#include "tst_bellatiming.moc"
