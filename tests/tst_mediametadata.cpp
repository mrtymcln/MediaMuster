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
	void empty_label_returns_empty();
	void pcm_audio_ul_resolves();
	void prores_422_ul_resolves();
	void dnxhd_bitrate_follows_fps();
	void dnxhd_hqx_carries_x_suffix();
	void vc3_720p_sq_keeps_its_own_name();
	void unknown_ul_infers_family_from_structure();
	void fully_unknown_ul_falls_back_to_hex();
	void applyEditRate_labels_fractional_rates();
	void mdb_style_metadata_finalises_like_a_header();
	void finalise_is_idempotent_and_does_not_guess();
};

void TestMediaMetadata::empty_label_returns_empty()
{
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel({}, QStringLiteral("25")), QString());
}

void TestMediaMetadata::pcm_audio_ul_resolves()
{
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(ul("060E2B34040101010D01030102060100"),
														  QString()),
			 QStringLiteral("PCM"));
}

void TestMediaMetadata::prores_422_ul_resolves()
{
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(ul("060E2B34040101010D010301020C0301"),
														  QString()),
			 QStringLiteral("Apple ProRes 422"));
}

void TestMediaMetadata::dnxhd_bitrate_follows_fps()
{
	// Same UL (DNxHD SQ tier); current Avid branding leads, the legacy
	// technical bitrate name (rate-dependent) stays in the parenthesis.
	const QByteArray sq = ul("060E2B34040101010D01030102060101");
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq, QStringLiteral("25")),
			 QStringLiteral("Avid DNx SQ (DNxHD 120)"));
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq, QStringLiteral("29.97")),
			 QStringLiteral("Avid DNx SQ (DNxHD 145)"));
	// Unsupported or absent fps retains the known tier without a guessed bitrate.
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq, QStringLiteral("48")),
			 QStringLiteral("Avid DNx SQ"));
}

void TestMediaMetadata::dnxhd_hqx_carries_x_suffix()
{
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(ul("060E2B34040101010D01030102060202"),
														  QStringLiteral("25")),
			 QStringLiteral("Avid DNx HQX (DNxHD 185X)"));
}

void TestMediaMetadata::vc3_720p_sq_keeps_its_own_name()
{
	// CID 1252 (UL byte 0x12): 720p 8-bit 4:2:2 SQ. Its technical bitrate
	// names come from the 2012 whitepaper's 720p table — NOT the 1080-line
	// numbers (75 at 29.97, not 145). Display leads with current branding.
	const QByteArray sq720 = ul("060E2B340401010A0401020271120000");
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq720, QStringLiteral("29.97")),
			 QStringLiteral("Avid DNx SQ (DNxHD 75)"));
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq720, QStringLiteral("25")),
			 QStringLiteral("Avid DNx SQ (DNxHD 60)"));
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq720, QStringLiteral("23.976")),
			 QStringLiteral("Avid DNx SQ (DNxHD 60)"));
	// The 720p 50/59.94 rows are at the bottom of p9, before p10.
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq720, QStringLiteral("59.94")),
			 QStringLiteral("Avid DNx SQ (DNxHD 145)"));

	// Sibling 720p tiers (CIDs 1251/1250), same whitepaper table.
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(ul("060E2B340401010A0401020271110000"),
														  QStringLiteral("25")),
			 QStringLiteral("Avid DNx HQ (DNxHD 90)"));
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(ul("060E2B340401010A0401020271100000"),
														  QStringLiteral("29.97")),
			 QStringLiteral("Avid DNx HQX (DNxHD 110x)"));

	// DNxHR is under the same brand with no bitrate names — level only,
	// bare at every rate ("HR" is communicated by the Resolution column).
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(ul("060E2B34040101010D01030102110201"),
														  QStringLiteral("25")),
			 QStringLiteral("Avid DNx SQ"));
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(ul("060E2B34040101010D01030102110501"),
														  QStringLiteral("50")),
			 QStringLiteral("Avid DNx 444"));
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
		{24000, 1001, "23.976", 24}, {2997, 100, "29.97", 30},   {60000, 2002, "29.97", 30},
		{30000, 1001, "29.97", 30},  {25, 1, "25", 25},          {23976, 1000, "23.976", 24},
		{50, 1, "50", 50},           {60000, 1001, "59.94", 60},
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
	au.durationFrames = 1500;        // frames at 25
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
	const QByteArray sq = ul("060E2B34040101010D01030102060101");
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq, {}), QStringLiteral("Avid DNx SQ"));
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq, "50"),
			 QStringLiteral("Avid DNx SQ (DNxHD 240)"));
	QCOMPARE(MediaMetadataUtil::codecFromCompressionLabel(sq, "59.94"),
			 QStringLiteral("Avid DNx SQ (DNxHD 290)"));
	QVERIFY(
		!MediaMetadataUtil::codecFromCompressionLabel(ul("060e2b34040101010d99111111111111"), {})
			 .startsWith("Avid"));
}

QTEST_APPLESS_MAIN(TestMediaMetadata)
#include "tst_mediametadata.moc"
