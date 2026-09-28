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
	void codec_labels_data();
	void codec_labels();
	void unknown_ul_infers_family_from_structure();
	void fully_unknown_ul_falls_back_to_hex();
	void applyEditRate_labels_fractional_rates();
	void mdb_style_metadata_finalises_like_a_header();
	void finalise_is_idempotent_and_does_not_guess();
};

void TestMediaMetadata::codec_labels_data()
{
	QTest::addColumn<QByteArray>("label");
	QTest::addColumn<QString>("fps");
	QTest::addColumn<QString>("expected");
	const auto add = [](const char *tag, const char *hex, const char *fps, const char *expected)
	{
		QTest::newRow(tag) << ul(hex) << QString::fromLatin1(fps) << QString::fromLatin1(expected);
	};
	add("empty", "", "25", "");
	add("pcm", "060E2B34040101010D01030102060100", "", "PCM");
	add("prores-422", "060E2B34040101010D010301020C0301", "", "Apple ProRes 422");

	// DNxHD keeps its rate-dependent technical bitrate beside the current Avid tier.
	const char *sq = "060E2B34040101010D01030102060101";
	add("dnxhd-sq-25", sq, "25", "Avid DNx SQ (DNxHD 120)");
	add("dnxhd-sq-29.97", sq, "29.97", "Avid DNx SQ (DNxHD 145)");
	add("dnxhd-sq-50", sq, "50", "Avid DNx SQ (DNxHD 240)");
	add("dnxhd-sq-59.94", sq, "59.94", "Avid DNx SQ (DNxHD 290)");
	add("dnxhd-sq-unsupported-rate", sq, "48", "Avid DNx SQ");
	add("dnxhd-sq-no-rate", sq, "", "Avid DNx SQ");
	add("dnxhd-hqx", "060E2B34040101010D01030102060202", "25", "Avid DNx HQX (DNxHD 185X)");

	// The 720p CIDs use their own bitrate table, distinct from 1080-line DNxHD.
	const char *sq720 = "060E2B340401010A0401020271120000";
	add("720p-sq-29.97", sq720, "29.97", "Avid DNx SQ (DNxHD 75)");
	add("720p-sq-25", sq720, "25", "Avid DNx SQ (DNxHD 60)");
	add("720p-sq-23.976", sq720, "23.976", "Avid DNx SQ (DNxHD 60)");
	add("720p-sq-59.94", sq720, "59.94", "Avid DNx SQ (DNxHD 145)");
	add("720p-hq", "060E2B340401010A0401020271110000", "25", "Avid DNx HQ (DNxHD 90)");
	add("720p-hqx", "060E2B340401010A0401020271100000", "29.97", "Avid DNx HQX (DNxHD 110x)");

	// DNxHR names contain the tier alone, regardless of rate.
	add("dnxhr-sq", "060E2B34040101010D01030102110201", "25", "Avid DNx SQ");
	add("dnxhr-444", "060E2B34040101010D01030102110501", "50", "Avid DNx 444");
}

void TestMediaMetadata::codec_labels()
{
	QFETCH(QByteArray, label);
	QFETCH(QString, fps);
	QFETCH(QString, expected);
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(label, fps), expected);
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
		const char *fps;
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
		QCOMPARE(m.fps, QString::fromLatin1(c.fps));
		QCOMPARE(m.timecodeBase, c.base);
		QCOMPARE(m.sampleRate, 0);
	}
	MediaMetadata a;
	a.isAudio = true;
	MediaMetadataUtil::applyEditRate(a, 48000, 1);
	QCOMPARE(a.sampleRate, 48000);
	QVERIFY(a.fps.isEmpty());

	MediaMetadata z;
	MediaMetadataUtil::applyEditRate(z, 25, 0); // zero denominator: ignored, not a crash
	QVERIFY(z.fps.isEmpty());
}

// A struct filled from msmMMOB.mdb instead of a header: already full-frame
// height, real frame layout, label, fps. finalise() must derive exactly what
// the header path derives — and must NOT double a layout-1 height twice.
void TestMediaMetadata::mdb_style_metadata_finalises_like_a_header()
{
	const QByteArray sq = ul("060E2B34040101010D01030102060101");

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
	au.durationFrames = 1500;		 // frames at 25
	MediaMetadataUtil::finalise(au);
	QVERIFY(au.valid);
	QCOMPARE(au.timecodeBase, 25);
	QCOMPARE(au.durationFrames, qint64(1500));
	QCOMPARE(au.codec, QString::fromLatin1(kPcmAudioName));
	QCOMPARE(MediaMetadataUtil::bitDepthLabel(24), QStringLiteral("24-bit"));
	QCOMPARE(MediaMetadataUtil::bitDepthLabel(254), QStringLiteral("Float"));
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
