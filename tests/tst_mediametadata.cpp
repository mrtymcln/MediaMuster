// Shared codec lookup and metadata derivation used by MXF, OMF and MDB readers.
// Binary parsing and real-file integration remain in the corresponding parser tests.

#include "mediametadata.h"

#include <QByteArray>
#include <QString>
#include <QTest>
#include <limits>

namespace
{
	QByteArray ul(const char *hex)
	{
		return QByteArray::fromHex(hex);
	}
} // namespace

class TestMediaMetadata : public QObject
{
	Q_OBJECT
private slots:
	void exact_duration_conversion();
	void dnx_uncompressed_numeric_variants();
	void codec_labels_data();
	void codec_labels();
	void unknown_ul_infers_family_from_structure();
	void fully_unknown_ul_falls_back_to_hex();
	void applyEditRate_labels_fractional_rates();
	void mdb_style_metadata_finalises_like_a_header();
	void finalise_is_idempotent_and_does_not_guess();
};

void TestMediaMetadata::dnx_uncompressed_numeric_variants()
{
	struct Case { bool fixed; int code; const char *bits; const char *format; };
	// RDD 50:2019 Table 5. Identical depth codes have different meanings
	// under the two complete essence coding labels.
	const Case cases[] = {{false, 10, "10-bit", "Integer"}, {false, 253, "16-bit", "Half float"},
		{false, 254, "32-bit", "Float"}, {true, 254, "16-bit", "S2.14 fixed point"},
		{true, 10, "16-bit", "10.6 fixed point"}, {true, 12, "16-bit", "12.4 fixed point"}};
	for (const auto &item : cases)
	{
		MediaMetadata meta;
		meta.compressionLabel = ul(item.fixed ? "060e2b340401010d0401020203070200" : "060e2b340401010d0401020203070100");
		meta.componentDepth = item.code;
		meta.bitDepth = MediaMetadataUtil::bitDepthLabel(item.code);
		MediaMetadataUtil::finalise(meta);
		QCOMPARE(meta.bitDepth, QString::fromLatin1(item.bits));
		QCOMPARE(meta.sampleFormat, QString::fromLatin1(item.format));
	}
	MediaMetadata unknown;
	unknown.componentDepth = 254;
	MediaMetadataUtil::finalise(unknown);
	QVERIFY(unknown.sampleFormat.isEmpty());
	QVERIFY(unknown.bitDepth.isEmpty());
}

void TestMediaMetadata::exact_duration_conversion()
{
	using Source = MediaDuration::Source;
	MediaDuration samples{1468800, {48000, 1}, {30000, 1001}, Source::Descriptor};
	QCOMPARE(samples.displayFrames(), qint64(917));
	QCOMPARE(samples.units, qint64(1468800)); // The 133 extra samples are not discarded.
	QCOMPARE(samples.rate.numerator, 48000);
	QCOMPARE(samples.displayRate.denominator, 1001);
	MediaDuration fractional{48000, {96000, 2}, {60000, 2002}, Source::Descriptor};
	QCOMPARE(fractional.displayFrames(), qint64(30));
	QCOMPARE(fractional.rate.denominator, 2); // Preserve original, unreduced fractions.
	MediaDuration half{1, {2, 1}, {1, 1}, Source::Descriptor};
	QCOMPARE(half.displayFrames(), qint64(1));
	MediaDuration large{9007199254740993LL, {1, 1}, {1, 1}, Source::Descriptor};
	QCOMPARE(large.displayFrames(), large.units); // Above double's exact integer range.
	large.units = std::numeric_limits<qint64>::max();
	large.rate = {2147483647, 2147483646};
	large.displayRate = large.rate;
	QCOMPARE(large.displayFrames(), large.units); // Wide products, exact cancellation.
	large.rate = {2147483647, 30000};
	large.displayRate = {24000, 1001};
	large.units = 9223372036854775000LL;
	QCOMPARE(large.displayFrames(), qint64(3089287167392607));
	large.rate = {1, 1};
	large.displayRate = {2, 1};
	QCOMPARE(large.displayFrames(), qint64(0)); // Overflow is unknown, not wrapping.
	samples.displayRate = {};
	QVERIFY(samples.known());
	QCOMPARE(samples.displayFrames(), qint64(0));
	QCOMPARE(samples.units, qint64(1468800));
}

void TestMediaMetadata::codec_labels_data()
{
	QTest::addColumn<QByteArray>("label");
	QTest::addColumn<QString>("frameRate");
	QTest::addColumn<QString>("expected");
	const auto add = [](const char *tag, const char *hex, const char *frameRate, const char *expected)
	{
		QTest::newRow(tag) << ul(hex) << QString::fromLatin1(frameRate) << QString::fromLatin1(expected);
	};
	add("empty", "", "25", "");
	add("pcm", "060E2B34040101010D01030102060100", "", "PCM");
	add("prores-422", "060E2B34040101010D010301020C0301", "", "Apple ProRes 422");

	// DNxHD keeps its rate-dependent technical bitrate beside the current Avid tier.
	const char *sq = "060E2B340401010A0401020271030000";
	add("dnxhd-sq-25", sq, "25", "Avid DNx SQ (DNxHD 120)");
	add("dnxhd-sq-29.97", sq, "29.97", "Avid DNx SQ (DNxHD 145)");
	add("dnxhd-sq-50", sq, "50", "Avid DNx SQ (DNxHD 240)");
	add("dnxhd-sq-59.94", sq, "59.94", "Avid DNx SQ (DNxHD 290)");
	add("dnxhd-sq-unsupported-rate", sq, "48", "Avid DNx SQ");
	add("dnxhd-sq-no-rate", sq, "", "Avid DNx SQ");
	add("dnxhd-hqx", "060E2B340401010A0401020271010000", "25", "Avid DNx HQX (DNxHD 185X)");

	// The 720p CIDs use their own bitrate table, distinct from 1080-line DNxHD.
	const char *sq720 = "060E2B340401010A0401020271120000";
	add("720p-sq-29.97", sq720, "29.97", "Avid DNx SQ (DNxHD 75)");
	add("720p-sq-25", sq720, "25", "Avid DNx SQ (DNxHD 60)");
	add("720p-sq-23.976", sq720, "23.976", "Avid DNx SQ (DNxHD 60)");
	add("720p-sq-59.94", sq720, "59.94", "Avid DNx SQ (DNxHD 145)");
	add("720p-hq", "060E2B340401010A0401020271110000", "25", "Avid DNx HQ (DNxHD 90)");
	add("720p-hqx", "060E2B340401010A0401020271100000", "29.97", "Avid DNx HQX (DNxHD 110x)");

	// DNxHR names contain the tier alone, regardless of rate.
	add("dnxhr-sq", "060E2B340401010D0401020271270000", "25", "Avid DNx SQ");
	add("dnxhr-444", "060E2B340401010D0401020271240000", "50", "Avid DNx 444");
}

void TestMediaMetadata::codec_labels()
{
	QFETCH(QByteArray, label);
	QFETCH(QString, frameRate);
	QFETCH(QString, expected);
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(label, frameRate), expected);
}

void TestMediaMetadata::unknown_ul_infers_family_from_structure()
{
	// Not in the table, but byte[8]=04, byte[9]=01, byte[12]=71 → VC-3 family.
	const QString name = MediaMetadataUtil::codecFromCompressionLabel(
		ul("060E2B340401010A0401020271FF0000"), QStringLiteral("25"));
	QVERIFY2(name.startsWith(QStringLiteral("VC-3")), qPrintable(name));
	QVERIFY(name.contains(QStringLiteral("unknown variant")));
}

void TestMediaMetadata::fully_unknown_ul_falls_back_to_hex()
{
	const QString name = MediaMetadataUtil::codecFromCompressionLabel(
		ul("112233445566778899AABBCCDDEEFF00"), QString());
	QVERIFY2(name.startsWith(QStringLiteral("Unknown (")), qPrintable(name));
}

// The MDB stores rates as decimal approximations (2997/100) where the MXF
// stores exact fractions (30000/1001); the label must come out identical
// whichever spelling arrives, and audio rationals go to sampleRate instead.
void TestMediaMetadata::applyEditRate_labels_fractional_rates()
{
	struct Case
	{
		quint32 num, den;
		const char *frameRate;
		int base;
	};
	const Case cases[] = {
		{24000, 1001, "23.976", 24},
		{2997, 100, "29.97", 30},
		{60000, 2002, "29.97", 30},
		{30000, 1001, "29.97", 30},
		{25, 1, "25", 25},
		{23976, 1000, "23.976", 24},
		{50, 1, "50", 50},
		{60000, 1001, "59.94", 60},
	};
	for (const Case &c : cases)
	{
		MediaMetadata m;
		MediaMetadataUtil::applyEditRate(m, c.num, c.den);
		QCOMPARE(m.frameRate, QString::fromLatin1(c.frameRate));
		QCOMPARE(m.frameRateRatio.numerator, qint32(c.num));
		QCOMPARE(m.frameRateRatio.denominator, qint32(c.den));
		QCOMPARE(m.timecodeBase, c.base);
		QCOMPARE(m.sampleRate, 0);
	}
	MediaMetadata a;
	a.isAudio = true;
	MediaMetadataUtil::applyEditRate(a, 48000, 1);
	QCOMPARE(a.sampleRate, 48000);
	QCOMPARE(a.sampleRateRatio.numerator, 48000);
	QCOMPARE(a.sampleRateRatio.denominator, 1);
	QVERIFY(a.frameRate.isEmpty());

	MediaMetadata z;
	MediaMetadataUtil::applyEditRate(z, 25, 0); // zero denominator: ignored, not a crash
	QVERIFY(z.frameRate.isEmpty());
}

// A struct filled from msmMMOB.mdb instead of a header: already full-frame
// height, real frame layout, label, frame rate. finalise() must derive exactly what
// the header path derives — and must NOT double a layout-1 height twice.
void TestMediaMetadata::mdb_style_metadata_finalises_like_a_header()
{
	const QByteArray sq = ul("060E2B340401010A0401020271080000");

	MediaMetadata db;
	db.compressionLabel = sq;
	db.width = 1920;
	db.height = 1080; // the MDB producer already doubled its 540
	db.frameLayout = 1;
	db.heightIsFrameHeight = true;
	MediaMetadataUtil::applyEditRate(db, 25, 1);
	MediaMetadataUtil::finalise(db);
	QVERIFY(db.valid);
	QCOMPARE(db.resolution, QStringLiteral("1920x1080"));
	QCOMPARE(db.codec, QStringLiteral("Avid DNx SQ (DNxHD 120)"));

	// The same facts as a header presents them: a field height, no flag.
	MediaMetadata hdr;
	hdr.compressionLabel = sq;
	hdr.width = 1920;
	hdr.height = 540;
	hdr.frameLayout = 1;
	MediaMetadataUtil::applyEditRate(hdr, 25, 1);
	MediaMetadataUtil::finalise(hdr);
	QCOMPARE(hdr.resolution, db.resolution);
	QCOMPARE(hdr.codec, db.codec);
	QCOMPARE(hdr.valid, db.valid);

	// Layout 3 is a full height in a header but a half height in the MDB;
	// the producer normalises and flags, finalise leaves it alone.
	MediaMetadata l3;
	l3.compressionLabel = sq;
	l3.width = 1920;
	l3.height = 1080;
	l3.frameLayout = 3;
	l3.heightIsFrameHeight = true;
	MediaMetadataUtil::applyEditRate(l3, 25, 1);
	MediaMetadataUtil::finalise(l3);
	QCOMPARE(l3.resolution, QStringLiteral("1920x1080"));

	// Audio from the database: samples + sample rate + frame count.
	MediaMetadata au;
	au.isAudio = true;
	au.sampleRate = 48000;
	au.pcmDescriptor = true;
	au.descriptorDuration = 2880002; // samples
	au.structuralDuration = 1500;	 // frames at 25
	MediaMetadataUtil::finalise(au);
	QVERIFY(au.valid);
	QCOMPARE(au.timecodeBase, 25);
	QCOMPARE(au.duration.displayFrames(), qint64(1500));
	QCOMPARE(au.codec, QString::fromLatin1(kPcmAudioName));
	QCOMPARE(MediaMetadataUtil::bitDepthLabel(24), QStringLiteral("24-bit"));
	QVERIFY(MediaMetadataUtil::bitDepthLabel(254).isEmpty());
	QVERIFY(MediaMetadataUtil::bitDepthLabel(253).isEmpty());
}

void TestMediaMetadata::finalise_is_idempotent_and_does_not_guess()
{
	MediaMetadata picture;
	picture.width = 1920;
	picture.height = 540;
	picture.frameLayout = 1;
	MediaMetadataUtil::finalise(picture);
	MediaMetadataUtil::finalise(picture);
	QCOMPARE(picture.height, 1080);
	QVERIFY(picture.codec.isEmpty());
	MediaMetadata corrupt;
	corrupt.width = 1920;
	corrupt.height = std::numeric_limits<int>::min();
	corrupt.frameLayout = 1;
	MediaMetadataUtil::finalise(corrupt);
	QCOMPARE(corrupt.height, 0);
	QVERIFY(!corrupt.valid);
	MediaMetadata sound;
	sound.isAudio = true;
	sound.sampleRate = 48000;
	MediaMetadataUtil::finalise(sound);
	QVERIFY(sound.valid);
	QVERIFY(sound.codec.isEmpty());
	sound.pcmDescriptor = true;
	MediaMetadataUtil::finalise(sound);
	QCOMPARE(sound.codec, QString::fromLatin1(kPcmAudioName));
	QVERIFY(
		!MediaMetadataUtil::codecFromCompressionLabel(ul("060e2b34040101010d99111111111111"), {})
			 .startsWith("Avid"));
}

QTEST_APPLESS_MAIN(TestMediaMetadata)
#include "tst_mediametadata.moc"
