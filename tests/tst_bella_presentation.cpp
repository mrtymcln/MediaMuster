#include "scanner/bella_scanner.h"
#include "media/media_csv.h"
#include "ui/media_filter_proxy.h"
#include "ui/media_table_model.h"
#include "ui/scan_result_p.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

// MARK: - Test helpers

namespace
{
	using Col = MediaTableModel::Column;

	Bella::ScanEntry named(const QString &name)
	{
		Bella::ScanEntry entry;
		entry.path = QDir::temp().filePath(QStringLiteral("Avid MediaFiles/MXF/1/") + name);
		entry.metadata.clipName = name;
		return entry;
	}

	QString cell(const MediaTableModel &model, int row, Col column)
	{
		return model.index(row, model.columnIndex(column)).data().toString();
	}

	QString firstClip(const MediaFilterProxy &proxy, const MediaTableModel &model)
	{
		return proxy.index(0, model.columnIndex(Col::ClipName)).data().toString();
	}
}

// MARK: - Test fixture

class TestBellaPresentation final : public QObject
{
	Q_OBJECT
private slots:
	void rendersAndExportsOneRow();
	void durationZeroBlankAndSort();
	void ratesSortExactly();
	void fixedPointDepthRendersExportsAndSorts();
	void filtersKeepFullIdentityAndExplicitUnknown();
};

// MARK: - Table and CSV presentation

void TestBellaPresentation::rendersAndExportsOneRow()
{
	auto entry = named(QStringLiteral("café.mxf"));
	entry.kelpieId = 42;
	entry.created = QDateTime(QDate(2026, 11, 7), QTime(11, 10), Qt::UTC);
	entry.modified = entry.created.addSecs(4);
	entry.volumeIdentifier = QStringLiteral("physical-volume");
	entry.volumeName = QStringLiteral("Test Volume");
	entry.volumePath = QDir::tempPath();
	entry.quarantined = true;
	entry.localReference = Bella::LocalReference::NoReference;
	auto &m = entry.metadata;
	m.fileMobId = std::get<Bella::MobId>(Bella::normalizeMobId(
		QByteArray::fromHex("060a2b340101010501010f10130000001122334455667788060e2b347f7f2a80"),
		Bella::MobIdLayout::Aaf32, Bella::ByteOrder::LittleEndian));
	m.masterMobId = std::get<Bella::MobId>(Bella::normalizeMobId(QByteArray::fromHex("2b0000001122334455667788"),
																 Bella::MobIdLayout::OmfUid12, Bella::ByteOrder::LittleEndian));
	entry.operationSafety = Bella::VerifyFileId{*m.fileMobId, Bella::FileIdOrigin::Database};
	m.clipName = QStringLiteral("=Clip");
	m.project = QStringLiteral("Project");
	m.originalBin = QStringLiteral("Rushes");
	m.kind = Bella::MatchKind::Video;
	m.type = Bella::MatchType::Precompute;
	m.resolution = Bella::MatchDimensions{1920, 1080};
	m.frameRate = Bella::MatchFrameRate{{2997, 100}, Bella::MatchRateMeaning::AvidExactEditRate};
	m.exactFrameRate = Bella::TimingRate{30000, 1001};
	m.bitDepth.bits = 10;
	m.compression.compressionName = QStringLiteral("JPEG2000");
	m.effects = {QStringLiteral("Dissolve"), QStringLiteral("Blend")};
	m.timing = {Bella::ExactDuration{0, {30000, 1001}},
				Bella::TimecodeInputs{{30000, 1001}, 30, Bella::TimecodeMode::DropFrame}};
	const auto row = mediaFileRecordFromScanEntry(entry);
	QCOMPARE(row.kelpieId, quint64(42));
	QCOMPARE(row.mediaFolderName, QStringLiteral("1"));
	QCOMPARE(row.volumeName, entry.volumeName);
	QCOMPARE(row.volumePath, entry.volumePath);
	QVERIFY(row.isQuarantined);
	QVERIFY(row.isNonPortable);
	QCOMPARE(row.fileMobId, QStringLiteral("060a2b3401010105.01010f1013000000.1122334455667788.060e2b347f7f2a80"));
	QCOMPARE(row.masterMobId, QStringLiteral("omf:2b0000001122334455667788"));
	QVERIFY(row.operationScanReceipt);
	QCOMPARE(row.operationScanReceipt->databaseFileMobIdToVerify, row.fileMobId);
	QCOMPARE(row.scanStamp.modified, entry.modified);
	QCOMPARE(row.scanStamp.volumeIdentifier, entry.volumeIdentifier);
	QVERIFY(row.bellaValues && row.bellaValues->frameRate);
	QCOMPARE(row.bellaValues->frameRate->words.numerator, qint32(2997));
	QCOMPARE(row.bellaValues->frameRate->words.denominator, qint32(100));
	QVERIFY(row.bellaValues->frameRateValue);
	QCOMPARE(row.bellaValues->frameRateValue->numerator, quint64(30000));
	QCOMPARE(row.bellaValues->frameRateValue->denominator, quint64(1001));
	QCOMPARE(row.bellaValues->timing.duration->count, qint64(0));

	MediaTableModel model;
	model.setPrecomputesEnabled(true);
	model.setMediaFiles({row});
	QCOMPARE(cell(model, 0, Col::Location), entry.path);
	QCOMPARE(cell(model, 0, Col::Duration), QStringLiteral("00;00;00;00"));
	QCOMPARE(cell(model, 0, Col::FrameRate), QStringLiteral("29.97"));
	QCOMPARE(cell(model, 0, Col::Resolution), QStringLiteral("1920x1080"));
	QCOMPARE(cell(model, 0, Col::BitDepth), QStringLiteral("10-bit"));
	QCOMPARE(cell(model, 0, Col::Compression), QStringLiteral("JPEG2000"));
	QCOMPARE(cell(model, 0, Col::Created), QStringLiteral("2026-11-07 11:10"));
	QCOMPARE(cell(model, 0, Col::NoReference), QStringLiteral("true"));
	QVERIFY(cell(model, 0, Col::SizeMB).isEmpty());
	QVERIFY(cell(model, 0, Col::PrecomputeCategory).isEmpty());

	const MediaCsv::Options options{true};
	const auto line = MediaCsv::rowLine(row, options);
	const auto headers = MediaCsv::headerLine(options).chopped(1).split(QLatin1Char(','));
	const auto fields = line.chopped(1).split(QLatin1Char(','));
	QCOMPARE(fields.size(), headers.size());
	QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Clip Name"))), QStringLiteral("\"'=Clip\""));
	QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Duration"))), QStringLiteral("\"00;00;00;00\""));
	QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Compression"))), QStringLiteral("\"JPEG2000\""));
	QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Precompute Category"))), QStringLiteral("\"\""));
	QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Size (MB)"))), QString{});
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const auto path = temporary.filePath(QStringLiteral("export.csv"));
	QVERIFY(MediaCsv::write(path, {row}, options));
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(file.readAll(), QByteArray::fromHex("efbbbf") + MediaCsv::headerLine(options).toUtf8() + line.toUtf8());
}

// MARK: - Exact sorting and filters

void TestBellaPresentation::durationZeroBlankAndSort()
{
	auto video = named(QStringLiteral("zero"));
	video.metadata.kind = Bella::MatchKind::Video;
	video.metadata.timing = {Bella::ExactDuration{0, {25, 1}}, Bella::TimecodeInputs{{25, 1}, 25}};
	auto audio = named(QStringLiteral("rounded zero"));
	audio.metadata.kind = Bella::MatchKind::Audio;
	audio.metadata.timing = {Bella::ExactDuration{1, {48000, 1}}, Bella::TimecodeInputs{{25, 1}, 25}};
	auto unclocked = named(QStringLiteral("blank"));
	unclocked.metadata.timing.duration = Bella::ExactDuration{12, {48000, 1}};
	auto longVideo = video;
	longVideo.metadata.clipName = QStringLiteral("long duration");
	longVideo.metadata.timing.duration->count = 100 * 3600 * 25;
	const auto rounded = mediaFileRecordFromScanEntry(audio);
	QCOMPARE(rounded.bellaValues->timing.duration->count, qint64(1));
	MediaTableModel model;
	model.setMediaFiles({mediaFileRecordFromScanEntry(longVideo), rounded,
						 mediaFileRecordFromScanEntry(video), mediaFileRecordFromScanEntry(unclocked)});
	QCOMPARE(cell(model, 0, Col::Duration), QStringLiteral("100:00:00:00"));
	QCOMPARE(cell(model, 1, Col::Duration), QStringLiteral("00:00:00:00"));
	QCOMPARE(cell(model, 2, Col::Duration), QStringLiteral("00:00:00:00"));
	QVERIFY(cell(model, 3, Col::Duration).isEmpty());
	QCOMPARE(cell(model, 3, Col::Kind), QStringLiteral("unknown"));
	QCOMPARE(cell(model, 3, Col::Type), QStringLiteral("unknown"));
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.sort(model.columnIndex(Col::Duration));
	QCOMPARE(firstClip(proxy, model), QStringLiteral("blank"));
	QCOMPARE(proxy.index(1, model.columnIndex(Col::ClipName)).data().toString(), QStringLiteral("zero"));
	QCOMPARE(proxy.index(2, model.columnIndex(Col::ClipName)).data().toString(), QStringLiteral("rounded zero"));
}

void TestBellaPresentation::ratesSortExactly()
{
	auto literal = named(QStringLiteral("literal"));
	literal.metadata.kind = Bella::MatchKind::Video;
	literal.metadata.frameRate = Bella::MatchFrameRate{{2997, 100}, Bella::MatchRateMeaning::Literal};
	literal.metadata.exactFrameRate = Bella::TimingRate{2997, 100};
	auto avid = literal;
	avid.metadata.clipName = QStringLiteral("Avid alias");
	avid.metadata.frameRate->meaning = Bella::MatchRateMeaning::AvidExactEditRate;
	avid.metadata.exactFrameRate = Bella::TimingRate{30000, 1001};
	MediaTableModel model;
	model.setMediaFiles({mediaFileRecordFromScanEntry(avid), mediaFileRecordFromScanEntry(literal)});
	QCOMPARE(cell(model, 0, Col::FrameRate), QStringLiteral("29.97"));
	QCOMPARE(cell(model, 1, Col::FrameRate), QStringLiteral("29.97"));
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.sort(model.columnIndex(Col::FrameRate));
	QCOMPARE(firstClip(proxy, model), QStringLiteral("literal"));

	auto fractional = named(QStringLiteral("fractional"));
	fractional.metadata.kind = Bella::MatchKind::Video;
	fractional.metadata.exactFrameRate = Bella::TimingRate{24000, 1001};
	model.setMediaFiles({mediaFileRecordFromScanEntry(fractional)});
	QCOMPARE(cell(model, 0, Col::FrameRate), QStringLiteral("23.98"));

	auto wave = named(QStringLiteral("WAVE"));
	wave.metadata.kind = Bella::MatchKind::Audio;
	wave.metadata.sampleRate = quint32(48000);
	auto aiff = named(QStringLiteral("AIFF"));
	aiff.metadata.kind = Bella::MatchKind::Audio;
	const std::array<quint8, 10> extended{0x40, 0x0e, 0xbb, 0x80, 0, 0, 0, 0, 0, 1};
	aiff.metadata.sampleRate = extended;
	auto maximum = wave;
	maximum.metadata.clipName = QStringLiteral("unsigned WAVE");
	maximum.metadata.sampleRate = std::numeric_limits<quint32>::max();
	const auto widest = mediaFileRecordFromScanEntry(maximum);
	QCOMPARE(widest.bellaValues->sampleRateValue->numerator, quint64(std::numeric_limits<quint32>::max()));
	model.setMediaFiles({widest, mediaFileRecordFromScanEntry(aiff), mediaFileRecordFromScanEntry(wave)});
	QCOMPARE(cell(model, 1, Col::SampleRate), QStringLiteral("48 kHz"));
	QCOMPARE(cell(model, 2, Col::SampleRate), QStringLiteral("48 kHz"));
	QVERIFY((std::get<std::array<quint8, 10>>(*model.fileAt(1).bellaValues->sampleRate) == extended));
	proxy.sort(model.columnIndex(Col::SampleRate));
	QCOMPARE(firstClip(proxy, model), QStringLiteral("WAVE"));
}

void TestBellaPresentation::fixedPointDepthRendersExportsAndSorts()
{
	using Depth = Bella::MatchDepthRepresentation;
	auto fixed12 = named(QStringLiteral("fixed12.mxf"));
	fixed12.metadata.bitDepth = {std::nullopt, 16, Depth::Fixed12_4};
	auto fixed10 = named(QStringLiteral("fixed10.mxf"));
	fixed10.metadata.bitDepth = {std::nullopt, 16, Depth::Fixed10_6};

	MediaTableModel model;
	model.setMediaFiles({mediaFileRecordFromScanEntry(fixed12), mediaFileRecordFromScanEntry(fixed10)});
	const std::array formats{Depth::Fixed12_4, Depth::Fixed10_6};
	const QStringList labels{QStringLiteral("12-bit"), QStringLiteral("10-bit")};
	const MediaCsv::Options options{false};
	const auto headers = MediaCsv::headerLine(options).chopped(1).split(QLatin1Char(','));
	const auto csvColumn = headers.indexOf(QStringLiteral("Bit Depth"));
	QVERIFY(csvColumn >= 0);

	for (int row = 0; row < model.rowCount(); ++row)
	{
		const auto &file = model.fileAt(row);
		QVERIFY(file.bellaValues);
		QCOMPARE(file.bellaValues->bitDepth.representation, formats.at(row));
		QCOMPARE(file.bellaValues->bitDepth.storageBits, std::optional<quint32>(16));
		QVERIFY(!file.bellaValues->bitDepth.bits);
		QCOMPARE(cell(model, row, Col::BitDepth), labels.at(row));
		const auto fields = MediaCsv::rowLine(file, options).chopped(1).split(QLatin1Char(','));
		QCOMPARE(fields.value(csvColumn), QLatin1Char('"') + labels.at(row) + QLatin1Char('"'));
	}

	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int column = model.columnIndex(Col::BitDepth);
	proxy.sort(column);
	QCOMPARE(proxy.index(0, column).data().toString(), QStringLiteral("10-bit"));
	QCOMPARE(proxy.index(1, column).data().toString(), QStringLiteral("12-bit"));
}

void TestBellaPresentation::filtersKeepFullIdentityAndExplicitUnknown()
{
	auto entry = named(QStringLiteral("One.mxf"));
	entry.metadata.type = Bella::MatchType::Precompute;
	entry.metadata.fileMobId = std::get<Bella::MobId>(Bella::normalizeMobId(
		QByteArray::fromHex("060a2b340101010501010f10130000001122334455667788060e2b347f7f2a80"),
		Bella::MobIdLayout::Aaf32, Bella::ByteOrder::LittleEndian));
	auto unknown = mediaFileRecordFromScanEntry(entry);
	entry = named(QStringLiteral("Two.mxf"));
	entry.metadata.type = Bella::MatchType::Precompute;
	entry.metadata.precomputeCategory = Bella::MatchPrecomputeCategory::RenderedEffects;
	const auto rendered = mediaFileRecordFromScanEntry(entry);
	MediaTableModel model;
	model.setPrecomputesEnabled(true);
	model.setMediaFiles({unknown, rendered});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setPrecomputesEnabled(true);
	QVERIFY(cell(model, 0, Col::PrecomputeCategory).isEmpty());
	QCOMPARE(cell(model, 1, Col::PrecomputeCategory), QStringLiteral("Rendered Effects"));
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("unknown"), {}, {}}}});
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(firstClip(proxy, model), QStringLiteral("One.mxf"));
	proxy.setPrecomputeTreeFilter({});
	proxy.setSearchText(QStringLiteral("One.mxf"));
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText({});
	AvbFilter filter;
	filter.steps.append({AvbFilter::Operation::Intersect, {}, {{QStringLiteral("060a2b3401010105.01010f1013000000.1122334455667788.060e2b347f7f2a80")}, {}}, {}});
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(firstClip(proxy, model), QStringLiteral("One.mxf"));
}

QTEST_GUILESS_MAIN(TestBellaPresentation)
#include "tst_bella_presentation.moc"
