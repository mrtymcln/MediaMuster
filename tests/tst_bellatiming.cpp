// Independent arithmetic and timecode boundary expectations. No reader, old
// formatter, native executable or floating-point calculation is used as oracle.
#include "bella/timing.h"

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
	QFETCH(qint32, numerator); QFETCH(qint32, denominator); QFETCH(Rate, expected);
	const auto interpreted = Bella::interpretAvidEditRate(numerator, denominator);
	QVERIFY(interpreted);
	QVERIFY(sameRate(*interpreted, expected));
}

void TestBellaTiming::literalRatesAreNotSnapped_data()
{
	QTest::addColumn<qint32>("numerator"); QTest::addColumn<qint32>("denominator");
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
	QFETCH(qint32, numerator); QFETCH(qint32, denominator);
	const auto interpreted = Bella::interpretAvidEditRate(numerator, denominator);
	QVERIFY(interpreted);
	QVERIFY(sameRate(*interpreted, {quint64(numerator), quint64(denominator)}));
}

void TestBellaTiming::invalidSignedRateWords_data()
{
	QTest::addColumn<qint32>("numerator"); QTest::addColumn<qint32>("denominator");
	QTest::newRow("zero numerator") << qint32(0) << qint32(1);
	QTest::newRow("zero denominator") << qint32(25) << qint32(0);
	QTest::newRow("negative numerator") << qint32(-25) << qint32(1);
	QTest::newRow("negative denominator") << qint32(25) << qint32(-1);
	QTest::newRow("both negative are not positive source words") << qint32(-25) << qint32(-1);
	QTest::newRow("minimum signed word") << std::numeric_limits<qint32>::min() << qint32(1);
}
void TestBellaTiming::invalidSignedRateWords()
{
	QFETCH(qint32, numerator); QFETCH(qint32, denominator);
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
	QTest::addColumn<qint64>("count"); QTest::addColumn<Rate>("sourceRate");
	QTest::addColumn<Rate>("displayRate"); QTest::addColumn<qint64>("expected");
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
	QFETCH(qint64, count); QFETCH(Rate, sourceRate); QFETCH(Rate, displayRate); QFETCH(qint64, expected);
	const auto duration = Bella::makeExactDuration(count, sourceRate);
	QVERIFY(duration);
	QCOMPARE(Bella::displayFrameCount(*duration, displayRate), std::optional<qint64>(expected));
	QCOMPARE(duration->count, count);
}

void TestBellaTiming::wideExactConversion_data()
{
	QTest::addColumn<qint64>("count"); QTest::addColumn<Rate>("sourceRate");
	QTest::addColumn<Rate>("displayRate"); QTest::addColumn<qint64>("expected");
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
	QFETCH(qint64, count); QFETCH(Rate, sourceRate); QFETCH(Rate, displayRate); QFETCH(qint64, expected);
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
	QVERIFY(exact); QCOMPARE(exact->count, maximumCount);
	QVERIFY(!Bella::displayFrameCount(*exact, {2, 1}));
	QVERIFY(!Bella::displayFrameCount({1, {1, maximumWord}}, {1, 1}));
	QVERIFY(!Bella::displayFrameCount({maximumCount, {maximumWord - 1, maximumWord}},
		{maximumWord, maximumWord - 1})); // Final half-up result is 2^63.
	QVERIFY(!Bella::displayFrameCount({maximumCount, {maximumWord - 1, maximumWord}},
		{1, 1})); // Exactly INT64_MAX + 1/2; rounding itself overflows.
	QCOMPARE(exact->count, maximumCount); // Unavailable display is not unavailable source Duration.
}

void TestBellaTiming::ndfLabels_data()
{
	QTest::addColumn<qint64>("frames"); QTest::addColumn<Rate>("clock");
	QTest::addColumn<quint16>("nominal"); QTest::addColumn<QString>("expected");
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
	QFETCH(qint64, frames); QFETCH(Rate, clock); QFETCH(quint16, nominal); QFETCH(QString, expected);
	const auto duration = Bella::makeExactDuration(frames, clock);
	QVERIFY(duration);
	QCOMPARE(Bella::formatDuration(*duration, clock, nominal, Mode::NonDropFrame), expected);
}

void TestBellaTiming::dfBoundaries_data()
{
	QTest::addColumn<qint64>("frames"); QTest::addColumn<Rate>("clock");
	QTest::addColumn<quint16>("nominal"); QTest::addColumn<QString>("expected");
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
	QFETCH(qint64, frames); QFETCH(Rate, clock); QFETCH(quint16, nominal); QFETCH(QString, expected);
	const auto duration = Bella::makeExactDuration(frames, clock);
	QVERIFY(duration);
	QCOMPARE(Bella::formatDuration(*duration, clock, nominal, Mode::DropFrame), expected);
}

void TestBellaTiming::zeroIsDistinctFromUnavailable()
{
	const auto zero = Bella::makeExactDuration(0, {25, 1});
	QVERIFY(zero); QCOMPARE(zero->count, qint64(0));
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

QTEST_GUILESS_MAIN(TestBellaTiming)
#include "tst_bellatiming.moc"
