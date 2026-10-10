// Exercises the fresh legacy reader with real Avid files and independently
// authored containers. Large sample ranges are guarded so metadata-only reads
// cannot accidentally pass by copying the audio or video into memory.

#include "mediaengine/omfreader.h"
#include "mediaengine/audioreader_p.h"
#include "testmediaenginebento.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QTest>
#include <algorithm>
#include <cstring>

namespace
{
	using TestMediaEngineBento::number;
	using TestMediaEngineBento::TypedBento;
	using Outcome = MediaEngine::ParsedSource::Outcome;
	using Container = MediaEngine::ParsedSource::Container;

	QByteArray chunk(const QByteArray &id, const QByteArray &payload, bool big = false)
	{
		return id + number(quint32(payload.size()), big) + payload + QByteArray(payload.size() & 1, '\0');
	}

	QByteArray waveFormat()
	{
		return number<quint16>(1) + number<quint16>(1) + number<quint32>(48000) + number<quint32>(144000) + number<quint16>(3) + number<quint16>(24);
	}

	QByteArray wave(const QByteArray &chunks)
	{
		return QByteArray("RIFF") + number(quint32(chunks.size() + 4)) + "WAVE" + chunks;
	}

	QByteArray aiffCommon(bool compressed = false)
	{
		QByteArray common = number<quint16>(1, true) + number<quint32>(2, true) + number<quint16>(24, true) + QByteArray::fromHex("400ebb80000000000000");
		if (compressed)
			common += QByteArray("in24\x0e"
								 "24-bit Integer\0",
								 20);
		return common;
	}

	QByteArray aiff(const QByteArray &chunks, bool compressed = false)
	{
		return QByteArray("FORM") + number(quint32(chunks.size() + 4), true) + (compressed ? "AIFC" : "AIFF") + chunks;
	}

	MediaEngine::ParsedSource parse(QByteArray bytes)
	{
		QBuffer source(&bytes);
		source.open(QIODevice::ReadOnly);
		MediaEngine::Cancellation cancellation;
		return MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	}

	const MediaEngine::RawProperty *nativeChunk(const MediaEngine::ParsedSource &result, const QByteArray &key)
	{
		for (const auto &value : result.unownedProperties)
			if (value.locator.key == key)
				return &value;
		return nullptr;
	}

	const MediaEngine::RawProperty *objectProperty(const MediaEngine::ParsedSource &result, const QString &name)
	{
		for (const auto &object : result.objects)
			for (const auto &value : object.properties)
				if (value.locator.name == name)
					return &value;
		return nullptr;
	}

	TypedBento smallOmf(const QByteArray &name)
	{
		TypedBento writer;
		writer.head(1);
		writer.add(101, "OMFI:ObjID", "omfi:ObjectTag", "MOBJ", true);
		writer.add(101, "OMFI:CPNT:Name", "omfi:String", name + '\0');
		writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", QByteArray::fromHex("2a0000000102030405060708"));
		return writer;
	}

	// A virtual sparse file: only declared pieces occupy RAM. Reads into guarded
	// sample ranges fail immediately, including broad reads that overlap a range.
	class GuardedDevice final : public QIODevice
	{
	public:
		struct Piece
		{
			qint64 offset;
			QByteArray bytes;
		};
		QVector<Piece> pieces;
		QVector<MediaEngine::ByteRange> forbidden;
		qint64 extent = 0, bytesRead = 0;
		bool forbiddenRead = false;
		MediaEngine::Cancellation *cancellation = nullptr;
		qint64 cancelAfter = -1;
		qint64 stopOffset = -1;
		bool cancelAtStop = false;

		explicit GuardedDevice(const QByteArray &bytes = {})
		{
			pieces.append({0, bytes});
			extent = bytes.size();
			open(QIODevice::ReadOnly | QIODevice::Unbuffered);
		}
		qint64 size() const override { return extent; }
		bool isSequential() const override { return false; }
		bool seek(qint64 position) override { return position >= 0 && QIODevice::seek(position); }

	protected:
		qint64 readData(char *destination, qint64 count) override
		{
			const qint64 start = pos();
			if (start >= extent)
				return 0;
			count = qMin(count, extent - start);
			if (start == stopOffset)
			{
				setErrorString(QStringLiteral("Injected metadata I/O failure"));
				return -1;
			}
			if (start < stopOffset && count > stopOffset - start)
				count = stopOffset - start;
			for (const auto &range : forbidden)
			{
				if (start < range.offset + range.length && range.offset < start + count)
				{
					forbiddenRead = true;
					setErrorString(QStringLiteral("Reader attempted to load sample data"));
					return -1;
				}
			}
			std::memset(destination, 0, size_t(count));
			for (const auto &piece : pieces)
			{
				const qint64 begin = qMax(start, piece.offset);
				const qint64 end = qMin(start + count, piece.offset + piece.bytes.size());
				if (end > begin)
					std::memcpy(destination + begin - start, piece.bytes.constData() + begin - piece.offset, size_t(end - begin));
			}
			bytesRead += count;
			if (cancellation && ((cancelAfter >= 0 && bytesRead >= cancelAfter) || (cancelAtStop && start + count == stopOffset)))
				cancellation->cancel();
			return count;
		}
		qint64 writeData(const char *, qint64) override { return -1; }
	};
}

class TestMediaEngineLegacy final : public QObject
{
	Q_OBJECT
private slots:
	void plainNativeAudio_data();
	void plainNativeAudio();
	void repeatedNativePropertiesStaySeparate();
	void waveExtensibleKeepsPrecisionAndAssignments();
	void aifcKeepsCompressionAndSoundOffset();
	void bentoEssenceIsNotRead();
	void sparseRf64SkipsSamples();
	void partialDeferredMetadata_data();
	void partialDeferredMetadata();
	void continuedMetadataAndEssence();
	void ambiguousEssencePropertyStaysDeferred();
	void rf64RepeatedSizesAndOrdinaryHeader();
	void rf64TruncatedTable();
	void truncatedNativeExtensionsKeepBaseFields();
	void multipleEmbeddedGraphsKeepTheirContexts();
	void inputReceiptAndBorrowedDevice();
	void malformedAndPartialMetadata();
	void malformedEmbeddedGraphKeepsNativeMetadata();
	void cancellation();
	void realAvidSlates_data();
	void realAvidSlates();
	void realAvidAudio_data();
	void realAvidAudio();
	void realAudioSummariesMatchNativeFields_data();
	void realAudioSummariesMatchNativeFields();
	void audioSummaryFieldsFollowFragmentedRanges();
};

void TestMediaEngineLegacy::plainNativeAudio_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<bool>("isWave");
	QTest::newRow("plain-wave") << wave(chunk("fmt ", waveFormat()) + chunk("data", QByteArray(6, '\0'))) << true;
	QTest::newRow("plain-aiff") << aiff(chunk("COMM", aiffCommon(), true) + chunk("SSND", QByteArray(8 + 6, '\0'), true)) << false;
}

void TestMediaEngineLegacy::plainNativeAudio()
{
	QFETCH(QByteArray, bytes);
	QFETCH(bool, isWave);
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, isWave ? Container::Wave : Container::Aiff);
	QVERIFY(result.embeddedSources.isEmpty());
	const auto *description = nativeChunk(result, isWave ? "fmt " : "COMM");
	QVERIFY(description);
	QVERIFY(description->bytesRetained);
	const auto values = description->decoded.toMap();
	QCOMPARE(values.value(isWave ? "nChannels" : "numChannels").toUInt(), 1u);
	QCOMPARE(values.value(isWave ? "wBitsPerSample" : "sampleSize").toUInt(), 24u);
	QCOMPARE(values.value(isWave ? "nSamplesPerSec" : "sampleRate").toDouble(), 48000.0);
	const auto *samples = nativeChunk(result, isWave ? "data" : "SSND");
	QVERIFY(samples);
	QVERIFY(!samples->bytesRetained);
	QVERIFY(samples->encoding.isEmpty());
	QVERIFY(!samples->locator.ranges.isEmpty());
}

void TestMediaEngineLegacy::repeatedNativePropertiesStaySeparate()
{
	QByteArray second = waveFormat();
	second.replace(4, 4, number<quint32>(44100));
	const auto result = parse(wave(chunk("fmt ", waveFormat()) + chunk("data", QByteArray(6, '\0')) + chunk("fmt ", second)));
	QVector<const MediaEngine::RawProperty *> formats;
	for (const auto &value : result.unownedProperties)
		if (value.locator.key == "fmt ")
			formats.append(&value);
	QCOMPARE(formats.size(), 2);
	QCOMPARE(formats[0]->decoded.toMap().value("nSamplesPerSec").toUInt(), 48000u);
	QCOMPARE(formats[1]->decoded.toMap().value("nSamplesPerSec").toUInt(), 44100u);
	QVERIFY(formats[0]->locator.ranges[0].offset < formats[1]->locator.ranges[0].offset);
}

void TestMediaEngineLegacy::waveExtensibleKeepsPrecisionAndAssignments()
{
	QByteArray format = number<quint16>(0xfffe) + number<quint16>(2) + number<quint32>(48000) + number<quint32>(288000) + number<quint16>(6) + number<quint16>(24) + number<quint16>(22) + number<quint16>(20) + number<quint32>(3) + QByteArray::fromHex("0100000000001000800000aa00389b71");
	const auto result = parse(wave(chunk("fmt ", format) + chunk("data", QByteArray(12, '\0'))));
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *description = nativeChunk(result, "fmt ");
	QVERIFY(description);
	const auto values = description->decoded.toMap();
	QCOMPARE(values.value("wBitsPerSample").toUInt(), 24u);
	QCOMPARE(values.value("wValidBitsPerSample").toUInt(), 20u);
	QCOMPARE(values.value("dwChannelMask").toUInt(), 3u);
	QCOMPARE(values.value("SubFormat").toByteArray(), QByteArray::fromHex("0100000000001000800000aa00389b71"));
}

void TestMediaEngineLegacy::aifcKeepsCompressionAndSoundOffset()
{
	const QByteArray sound = number<quint32>(3, true) + number<quint32>(0, true) + QByteArray(3 + 6, '\0');
	const auto result = parse(aiff(chunk("COMM", aiffCommon(true), true) + chunk("SSND", sound, true), true));
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *description = nativeChunk(result, "COMM");
	QVERIFY(description);
	QCOMPARE(description->decoded.toMap().value("compressionType").toByteArray(), QByteArray("in24"));
	QCOMPARE(description->decoded.toMap().value("compressionName").toString(), QStringLiteral("24-bit Integer"));
	const auto *samples = nativeChunk(result, "SSND");
	QVERIFY(samples);
	QCOMPARE(samples->decoded.toMap().value("offset").toUInt(), 3u);
	QCOMPARE(samples->decoded.toMap().value("blockSize").toUInt(), 0u);
	QVERIFY(!samples->bytesRetained);
}

void TestMediaEngineLegacy::bentoEssenceIsNotRead()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "OMFI:ObjID", "omfi:ObjectTag", "IDAT", true);
	writer.add(101, "OMFI:IDAT:ImageData", "omfi:DataValue", QByteArray(512 * 1024, 'X'));
	writer.add(102, "OMFI:WAVD:Summary", "omfi:VarLenBytes", wave(chunk("fmt ", waveFormat())));
	writer.add(102, "Vendor:PrivateMetadata", "omfi:DataValue", QByteArray::fromHex("00fe1234"));
	GuardedDevice source(writer.build());
	source.forbidden.append({64, 512 * 1024 - 128});
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	QVERIFY(!source.forbiddenRead);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, Container::Omf);
	const auto *samples = objectProperty(result, "OMFI:IDAT:ImageData");
	QVERIFY(samples);
	QVERIFY(!samples->bytesRetained);
	QVERIFY(samples->encoding.isEmpty());
	QCOMPARE(samples->locator.ranges.size(), 1);
	QCOMPARE(samples->locator.ranges[0].length, qint64(512 * 1024));
	const auto *summary = objectProperty(result, "OMFI:WAVD:Summary");
	QVERIFY(summary);
	QVERIFY(summary->bytesRetained);
	QVERIFY(summary->encoding.startsWith("RIFF"));
	const auto *unknown = objectProperty(result, "Vendor:PrivateMetadata");
	QVERIFY(unknown);
	QCOMPARE(unknown->encoding, QByteArray::fromHex("00fe1234"));
	QVERIFY(source.bytesRead < 32768);
}

void TestMediaEngineLegacy::sparseRf64SkipsSamples()
{
	const quint64 dataSize = quint64(5) * 1024 * 1024 * 1024;
	const QByteArray tail = chunk("xtra", "kept");
	const quint64 dataOffset = 12 + 36 + 24 + 8;
	const quint64 fileSize = dataOffset + dataSize + quint64(tail.size());
	QByteArray ds64 = number(fileSize - 8) + number(dataSize) + number(dataSize / 3) + number<quint32>(0);
	QByteArray prefix = QByteArray("RF64") + number<quint32>(0xffffffff) + "WAVE" + chunk("ds64", ds64) + chunk("fmt ", waveFormat()) + "data" + number<quint32>(0xffffffff);
	QCOMPARE(quint64(prefix.size()), dataOffset);
	GuardedDevice source(prefix);
	source.extent = qint64(fileSize);
	source.pieces.append({qint64(dataOffset + dataSize), tail});
	source.forbidden.append({qint64(dataOffset + 64), qint64(dataSize - 128)});
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	QVERIFY(!source.forbiddenRead);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, Container::Wave);
	const auto *samples = nativeChunk(result, "data");
	QVERIFY(samples);
	QCOMPARE(samples->locator.ranges.size(), 1);
	QCOMPARE(samples->locator.ranges[0].offset, qint64(dataOffset));
	QCOMPARE(samples->locator.ranges[0].length, qint64(dataSize));
	QVERIFY(!samples->bytesRetained);
	const auto *afterSamples = nativeChunk(result, "xtra");
	QVERIFY(afterSamples);
	QCOMPARE(afterSamples->locator.ranges.size(), 1);
	QCOMPARE(afterSamples->locator.ranges[0].offset, qint64(dataOffset + dataSize + 8));
	QCOMPARE(afterSamples->locator.ranges[0].length, qint64(4));
	if (afterSamples->bytesRetained)
		QCOMPARE(afterSamples->encoding, QByteArray("kept"));
	QVERIFY(source.bytesRead < 32768);
}

void TestMediaEngineLegacy::partialDeferredMetadata_data()
{
	QTest::addColumn<bool>("cancelRead");
	QTest::newRow("cancel-after-four-property-bytes") << true;
	QTest::newRow("io-failure-after-four-property-bytes") << false;
}

void TestMediaEngineLegacy::partialDeferredMetadata()
{
	QFETCH(bool, cancelRead);
	TypedBento writer;
	writer.head(1);
	writer.add(100, "OMFI:IDAT:ImageData", "omfi:DataValue", QByteArray(256, 'X'));
	writer.add(101, "Vendor:LongMetadata", "omfi:VarLenBytes", QByteArray("retained metadata after interruption"));
	const QByteArray bytes = writer.build();
	GuardedDevice source(bytes);
	MediaEngine::Cancellation cancellation;
	source.cancellation = &cancellation;
	source.stopOffset = 256 + 4;
	source.cancelAtStop = cancelRead;
	const auto result = MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	QCOMPARE(result.outcome, cancelRead ? Outcome::Cancelled : Outcome::IoError);
	// Cancellation may stop name interpretation; the native property ID still
	// connects the retained bytes to the already-retained dictionary and TOC.
	const MediaEngine::RawProperty *partial = nullptr;
	for (const auto &object : result.objects)
		if (object.handle == 101)
			for (const auto &value : object.properties)
				if (value.bento && value.bento->property == writer.property("Vendor:LongMetadata"))
					partial = &value;
	QVERIFY(partial);
	QCOMPARE(partial->state, PropertyReadState::Unreadable);
	QVERIFY(partial->bytesRetained);
	QCOMPARE(partial->encoding, QByteArray("reta"));
	QByteArray reconstructed;
	for (const auto &range : partial->locator.ranges)
		reconstructed += bytes.mid(range.offset, range.length);
	QCOMPARE(reconstructed, partial->encoding);
	QVERIFY(partial->bento);
	QVERIFY(!partial->bento->tocRanges.isEmpty());
}

void TestMediaEngineLegacy::continuedMetadataAndEssence()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "Vendor:Continued", "omfi:VarLenBytes", "abc", false, true);
	writer.add(101, "Vendor:Continued", "omfi:VarLenBytes", "defg");
	writer.add(102, "OMFI:IDAT:ImageData", "omfi:DataValue", QByteArray(256 * 1024, 'A'), false, true);
	writer.add(102, "OMFI:IDAT:ImageData", "omfi:DataValue", QByteArray(256 * 1024, 'B'));
	const QByteArray bytes = writer.build();
	GuardedDevice source(bytes);
	source.forbidden.append({7 + 64, 512 * 1024 - 128});
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	QVERIFY(!source.forbiddenRead);
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *metadata = objectProperty(result, "Vendor:Continued");
	QVERIFY(metadata);
	QCOMPARE(metadata->encoding, QByteArray("abcdefg"));
	QCOMPARE(metadata->locator.ranges.size(), 2);
	QByteArray reconstructed;
	for (const auto &range : metadata->locator.ranges)
		reconstructed += bytes.mid(range.offset, range.length);
	QCOMPARE(reconstructed, metadata->encoding);
	const auto *samples = objectProperty(result, "OMFI:IDAT:ImageData");
	QVERIFY(samples);
	QCOMPARE(samples->locator.ranges.size(), 2);
	QCOMPARE(samples->locator.ranges[0].length, qint64(256 * 1024));
	QCOMPARE(samples->locator.ranges[1].length, qint64(256 * 1024));
	QVERIFY(!samples->bytesRetained);
	QVERIFY(samples->encoding.isEmpty());
}

void TestMediaEngineLegacy::ambiguousEssencePropertyStaysDeferred()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "Vendor:AmbiguousData", "omfi:DataValue", QByteArray(256 * 1024, 'X'));
	const quint32 id = writer.property("Vendor:AmbiguousData");
	writer.entries.append({id, 24, 21, QByteArray("OMFI:IDAT:ImageData\0", 20)});
	GuardedDevice source(writer.build());
	source.forbidden.append({64, 256 * 1024 - 128});
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	QVERIFY(!source.forbiddenRead);
	const MediaEngine::RawProperty *ambiguous = nullptr;
	for (const auto &object : result.objects)
		if (object.handle == 101)
			for (const auto &value : object.properties)
				if (value.bento && value.bento->property == id)
					ambiguous = &value;
	QVERIFY(ambiguous);
	QVERIFY(!ambiguous->bytesRetained);
	QVERIFY(ambiguous->encoding.isEmpty());
	QCOMPARE(ambiguous->locator.ranges.size(), 1);
	QCOMPARE(ambiguous->locator.ranges[0].length, qint64(256 * 1024));
	QVERIFY(!ambiguous->interpretation.isEmpty());
}

void TestMediaEngineLegacy::rf64RepeatedSizesAndOrdinaryHeader()
{
	// A normal-sized first data chunk does not consume the ds64 primary size.
	// Later sentinel-sized chunks consume their same-ID table entries in order.
	const QByteArray chunks = chunk("fmt ", waveFormat()) + chunk("data", QByteArray(6, 'a')) + QByteArray("data") + number<quint32>(0xffffffff) + QByteArray(8, 'b') + QByteArray("data") + number<quint32>(0xffffffff) + QByteArray(10, 'c');
	const quint64 fileSize = 12 + 8 + 28 + 2 * 12 + quint64(chunks.size());
	for (const bool sentinelHeader : {false, true})
	{
		// An ordinary RF64 32-bit size takes precedence over ds64.riffSize.
		const QByteArray ds64 = number(sentinelHeader ? fileSize - 8 : quint64(0)) + number<quint64>(999) + number<quint64>(2) + number<quint32>(2) + "data" + number<quint64>(8) + "data" + number<quint64>(10);
		const auto result = parse(QByteArray("RF64") + number(sentinelHeader ? quint32(0xffffffff) : quint32(fileSize - 8)) + "WAVE" + chunk("ds64", ds64) + chunks);
		QCOMPARE(result.outcome, Outcome::Complete);
		QVector<qint64> lengths;
		for (const auto &value : result.unownedProperties)
			if (value.locator.key == "data")
				lengths.append(value.locator.ranges[0].length);
		QCOMPARE(lengths, QVector<qint64>({6, 8, 10}));
		const auto *sizes = nativeChunk(result, "ds64");
		QVERIFY(sizes);
		const auto table = sizes->decoded.toMap().value("table").toList();
		QCOMPARE(table.size(), 2);
		QCOMPARE(table[0].toMap().value("chunkSize").toULongLong(), quint64(8));
		QCOMPARE(table[1].toMap().value("chunkSize").toULongLong(), quint64(10));
	}
}

void TestMediaEngineLegacy::rf64TruncatedTable()
{
	const QByteArray ds64 = number<quint64>(60) + number<quint64>(6) + number<quint64>(2) + number<quint32>(2) + "data" + number<quint64>(6); // Only one of two declared entries.
	const auto result = parse(QByteArray("RF64") + number<quint32>(0xffffffff) + "WAVE" + chunk("ds64", ds64));
	QCOMPARE(result.outcome, Outcome::Malformed);
	const auto *sizes = nativeChunk(result, "ds64");
	QVERIFY(sizes);
	QCOMPARE(sizes->encoding, ds64);
	QCOMPARE(sizes->state, PropertyReadState::Unreadable);
	QCOMPARE(sizes->decoded.toMap().value("tableLength").toUInt(), 2u);
	QVERIFY(!result.diagnostics.isEmpty());
}

void TestMediaEngineLegacy::truncatedNativeExtensionsKeepBaseFields()
{
	QByteArray format = waveFormat();
	format.replace(0, 2, number<quint16>(0xfffe));
	format += number<quint16>(22) + number<quint16>(20); // Valid bits present, remaining extension absent.
	const auto badWave = parse(wave(chunk("fmt ", format)));
	QCOMPARE(badWave.outcome, Outcome::Malformed);
	const auto *waveDescription = nativeChunk(badWave, "fmt ");
	QVERIFY(waveDescription);
	QCOMPARE(waveDescription->encoding, format);
	QCOMPARE(waveDescription->state, PropertyReadState::Unreadable);
	const auto waveFields = waveDescription->decoded.toMap();
	QCOMPARE(waveFields.value("wBitsPerSample").toUInt(), 24u);
	QCOMPARE(waveFields.value("cbSize").toUInt(), 22u);
	QVERIFY(!waveFields.contains("wValidBitsPerSample")); // Cannot qualify the union without its GUID.

	QByteArray common = aiffCommon(true);
	common.chop(8);
	const auto badAiff = parse(aiff(chunk("COMM", common, true), true));
	QCOMPARE(badAiff.outcome, Outcome::Malformed);
	const auto *aiffDescription = nativeChunk(badAiff, "COMM");
	QVERIFY(aiffDescription);
	QCOMPARE(aiffDescription->encoding, common);
	QCOMPARE(aiffDescription->state, PropertyReadState::Unreadable);
	const auto aiffFields = aiffDescription->decoded.toMap();
	QCOMPARE(aiffFields.value("sampleSize").toUInt(), 24u);
	QCOMPARE(aiffFields.value("sampleRate").toDouble(), 48000.0);
	QCOMPARE(aiffFields.value("compressionType").toByteArray(), QByteArray("in24"));
	QCOMPARE(aiffFields.value("compressionNameLength").toUInt(), 14u);
	QVERIFY(!aiffFields.contains("compressionName"));
}

void TestMediaEngineLegacy::multipleEmbeddedGraphsKeepTheirContexts()
{
	QByteArray chunks = chunk("fmt ", waveFormat()) + chunk("data", QByteArray(6, '\0'));
	const quint32 firstStart = quint32(12 + chunks.size() + 8);
	chunks += chunk("omfi", smallOmf("First graph").build(firstStart));
	const quint32 secondStart = quint32(12 + chunks.size() + 8);
	chunks += chunk("omfi", smallOmf("Second graph").build(secondStart));
	chunks += chunk("xtra", "after both graphs");
	const QByteArray bytes = wave(chunks);
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.embeddedSources.size(), 2);
	for (qsizetype i = 0; i < result.embeddedSources.size(); ++i)
	{
		const auto &child = result.embeddedSources[i];
		QCOMPARE(child.container, Container::Omf);
		QCOMPARE(child.outcome, Outcome::Complete);
		QCOMPARE(child.embedding.key, QByteArray("omfi"));
		const auto *name = objectProperty(child, "OMFI:CPNT:Name");
		QVERIFY(name);
		QCOMPARE(name->decoded.toString(), i == 0 ? QStringLiteral("First graph") : QStringLiteral("Second graph"));
		QVERIFY(!name->locator.ranges.isEmpty());
		const auto range = name->locator.ranges[0];
		QVERIFY(range.offset >= (i == 0 ? firstStart : secondStart));
		QCOMPARE(bytes.mid(range.offset, range.length), name->encoding);
		for (const auto &object : child.objects)
			QCOMPARE(object.snapshot, child.snapshot);
	}
	QVERIFY(nativeChunk(result, "xtra"));
}

void TestMediaEngineLegacy::inputReceiptAndBorrowedDevice()
{
	QByteArray bytes = smallOmf("Receipt").build();
	QBuffer source(&bytes);
	QVERIFY(source.open(QIODevice::ReadOnly));
	QVERIFY(source.seek(7));
	auto input = QSharedPointer<SourceSnapshot>::create();
	input->path = QStringLiteral("/scan/OMFI MediaFiles/wrong-extension.wav");
	input->source = MetadataSource::Pmr;
	input->readState = SourceReadState::NotRead;
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::OmfReader{}.read(source, {input, cancellation});
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, Container::Omf);
	QVERIFY(source.isOpen());
	QVERIFY(result.snapshot);
	QCOMPARE(result.snapshot->source, MetadataSource::Omf);
	QCOMPARE(result.snapshot->path, input->path);
	QCOMPARE(input->readState, SourceReadState::NotRead);
	for (const auto &object : result.objects)
		QCOMPARE(object.snapshot, result.snapshot);
}

void TestMediaEngineLegacy::malformedAndPartialMetadata()
{
	QByteArray goodChunks = chunk("fmt ", waveFormat());
	QByteArray malformedChunk = QByteArray("data") + number<quint32>(1024) + "short";
	const auto malformed = parse(wave(goodChunks + malformedChunk));
	QVERIFY(malformed.outcome != Outcome::Complete);
	QVERIFY(nativeChunk(malformed, "fmt "));
	const auto *fmt = nativeChunk(malformed, "fmt ");
	QCOMPARE(fmt->decoded.toMap().value("nSamplesPerSec").toUInt(), 48000u);
	QVERIFY(!malformed.diagnostics.isEmpty());

	QByteArray truncated = wave(goodChunks + chunk("data", QByteArray(6, '\0')));
	truncated.chop(3);
	QVERIFY(parse(truncated).outcome != Outcome::Complete);
	QVERIFY(parse(QByteArray("RIFF\x01\x02", 6)).outcome != Outcome::Complete);
	QVERIFY(parse(QByteArray("unrecognised data")).outcome != Outcome::Complete);

	const auto valid = smallOmf("retained").build();
	QVERIFY(parse(valid.left(valid.size() - 10)).outcome != Outcome::Complete);
}

void TestMediaEngineLegacy::malformedEmbeddedGraphKeepsNativeMetadata()
{
	QByteArray chunks = chunk("fmt ", waveFormat()) + chunk("data", QByteArray(6, '\0'));
	const quint32 base = quint32(12 + chunks.size() + 8);
	QByteArray broken = smallOmf("incomplete graph").build(base);
	// Label still identifies Bento, but its TOC lies outside the physical file.
	broken.replace(broken.size() - 8, 4, number<quint32>(0xfffffff0));
	chunks += chunk("omfi", broken);
	const auto result = parse(wave(chunks));
	QCOMPARE(result.container, Container::Wave);
	QCOMPARE(result.outcome, Outcome::Incomplete);
	const auto *format = nativeChunk(result, "fmt ");
	QVERIFY(format);
	QCOMPARE(format->decoded.toMap().value("nSamplesPerSec").toUInt(), 48000u);
	QCOMPARE(result.embeddedSources.size(), 1);
	QVERIFY(result.embeddedSources[0].outcome != Outcome::Complete);
	QVERIFY(!result.embeddedSources[0].diagnostics.isEmpty());
}

void TestMediaEngineLegacy::cancellation()
{
	QByteArray bytes = wave(chunk("fmt ", waveFormat()) + chunk("data", QByteArray(6, '\0')));
	QBuffer buffer(&bytes);
	QVERIFY(buffer.open(QIODevice::ReadOnly));
	MediaEngine::Cancellation cancelled;
	cancelled.cancel();
	QCOMPARE(MediaEngine::OmfReader{}.read(buffer, {{}, cancelled}).outcome, Outcome::Cancelled);
	MediaEngine::Cancellation during;
	GuardedDevice source(bytes);
	source.cancellation = &during;
	source.cancelAfter = 12;
	QCOMPARE(MediaEngine::OmfReader{}.read(source, {{}, during}).outcome, Outcome::Cancelled);
}

void TestMediaEngineLegacy::realAvidSlates_data()
{
	QTest::addColumn<QString>("path");
	const QDir directory(QStringLiteral(FIXTURES_DIR) + "/omf/avid_supporting");
	const auto names = directory.entryList({QStringLiteral("*.omf")}, QDir::Files, QDir::Name);
	QCOMPARE(names.size(), 80);
	for (const auto &name : names)
		QTest::newRow(qPrintable(name)) << directory.filePath(name);
}

void TestMediaEngineLegacy::realAvidSlates()
{
	QFETCH(QString, path);
	QFile source(path);
	QVERIFY(source.open(QIODevice::ReadOnly));
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, Container::Omf);
	QVERIFY(result.snapshot);
	QCOMPARE(result.snapshot->source, MetadataSource::Omf);
	QVERIFY(!result.objects.isEmpty());
	QVERIFY(objectProperty(result, "OMFI:MOBJ:MobID"));
	const auto *samples = objectProperty(result, "OMFI:IDAT:ImageData");
	QVERIFY(samples);
	QVERIFY(!samples->bytesRetained);
	QVERIFY(samples->encoding.isEmpty());
	QVERIFY(!samples->locator.ranges.isEmpty());
	for (const auto &object : result.objects)
		QCOMPARE(object.snapshot, result.snapshot);
}

void TestMediaEngineLegacy::realAvidAudio_data()
{
	QTest::addColumn<QString>("name");
	QTest::addColumn<bool>("isWave");
	QTest::newRow("MediaComposer-WAVE") << QStringLiteral("TONE_100A01.6A972974.039700.wav") << true;
	QTest::newRow("MediaComposer-AIFC") << QStringLiteral("TONE_100A01.6A972997.0C53E0.aif") << false;
}

void TestMediaEngineLegacy::realAvidAudio()
{
	QFETCH(QString, name);
	QFETCH(bool, isWave);
	QFile source(QStringLiteral(FIXTURES_DIR) + "/omf/mc2026_audio/" + name);
	QVERIFY(source.open(QIODevice::ReadOnly));
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::OmfReader{}.read(source, {{}, cancellation});
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, isWave ? Container::Wave : Container::Aiff);
	const auto *description = nativeChunk(result, isWave ? "fmt " : "COMM");
	QVERIFY(description);
	const auto fields = description->decoded.toMap();
	QCOMPARE(fields.value(isWave ? "nChannels" : "numChannels").toUInt(), 1u);
	QCOMPARE(fields.value(isWave ? "wBitsPerSample" : "sampleSize").toUInt(), 24u);
	QCOMPARE(fields.value(isWave ? "nSamplesPerSec" : "sampleRate").toDouble(), 48000.0);
	QVERIFY(nativeChunk(result, "umid"));
	QVERIFY(nativeChunk(result, "minf"));
	QCOMPARE(result.embeddedSources.size(), 1);
	const auto &child = result.embeddedSources[0];
	QCOMPARE(child.outcome, Outcome::Complete);
	QCOMPARE(child.container, Container::Omf);
	QCOMPARE(child.embedding.key, QByteArray("omfi"));
	QVERIFY(objectProperty(child, "OMFI:MOBJ:MobID"));
	const auto *data = objectProperty(child, isWave ? "OMFI:WAVE:Data" : "OMFI:AIFC:Data");
	QVERIFY(data);
	QVERIFY(!data->bytesRetained);
	QCOMPARE(data->locator.ranges.size(), 1);
	QCOMPARE(data->locator.ranges[0].offset, qint64(0));
	QCOMPARE(data->locator.ranges[0].length, isWave ? qint64(8649716) : qint64(8646724));
	const auto *summary = objectProperty(child, isWave ? "OMFI:WAVD:Summary" : "OMFI:AIFD:Summary");
	QVERIFY(summary);
	QVERIFY(summary->bytesRetained);
	QCOMPARE(summary->locator.ranges[0].offset, isWave ? qint64(8650086) : qint64(8647094));
}

void TestMediaEngineLegacy::realAudioSummariesMatchNativeFields_data()
{
	realAvidAudio_data();
}

void TestMediaEngineLegacy::realAudioSummariesMatchNativeFields()
{
	QFETCH(QString, name);
	QFETCH(bool, isWave);
	QFile input(QStringLiteral(FIXTURES_DIR) + "/omf/mc2026_audio/" + name);
	QVERIFY(input.open(QIODevice::ReadOnly));
	MediaEngine::Cancellation cancellation;
	const auto source = MediaEngine::OmfReader{}.read(input, {{}, cancellation});
	QCOMPARE(source.outcome, Outcome::Complete);
	QCOMPARE(source.embeddedSources.size(), 1);
	const auto &embedded = source.embeddedSources.first();
	const auto parentName = isWave ? QStringLiteral("OMFI:WAVD:Summary") : QStringLiteral("OMFI:AIFD:Summary");
	const auto *summary = objectProperty(embedded, parentName);
	const auto *copied = objectProperty(embedded, parentName + (isWave ? QStringLiteral(".fmt ") : QStringLiteral(".COMM")));
	const auto *native = nativeChunk(source, isWave ? "fmt " : "COMM");
	QVERIFY(summary && copied && native);
	QCOMPARE(summary->state, PropertyReadState::Present);
	QCOMPARE(summary->encoding.size(), isWave ? 4152 : 726);
	QVERIFY(input.seek(summary->locator.ranges.first().offset));
	QCOMPARE(input.read(summary->encoding.size()), summary->encoding);
	QCOMPARE(copied->decoded.toMap(), native->decoded.toMap());
	QCOMPARE(copied->bento, summary->bento);
	QCOMPARE(copied->locator.objectNumber, summary->locator.objectNumber);
	const auto *precision = objectProperty(embedded, copied->locator.name + (isWave ? QStringLiteral(".wBitsPerSample") : QStringLiteral(".sampleSize")));
	QVERIFY(precision);
	QCOMPARE(precision->state, PropertyReadState::Present);
	QCOMPARE(precision->decoded.toInt(), 24);
	QCOMPARE(precision->locator.ranges.first().offset, copied->locator.ranges.first().offset + (isWave ? 14 : 6));
	if (isWave)
	{
		// This genuine short copy advertises millions of omitted sound bytes.
		QCOMPARE(summary->encoding.mid(4088, 4), QByteArray("data"));
		QCOMPARE(qFromLittleEndian<quint32>(summary->encoding.constData() + 4092), quint32(8640006));
	}
	else
	{
		const auto *compressionName = objectProperty(embedded, copied->locator.name + QStringLiteral(".compressionName"));
		QVERIFY(compressionName);
		QCOMPARE(compressionName->encoding, QByteArray("24-bit Integer"));
		QCOMPARE(compressionName->decoded.toString(), QStringLiteral("24-bit Integer"));
		QCOMPARE(compressionName->textEncoding, std::optional<MediaEngine::TextEncoding>(MediaEngine::TextEncoding::Ascii));
		QCOMPARE(compressionName->locator.ranges.first().offset, summary->locator.ranges.first().offset + 655);
	}
}

void TestMediaEngineLegacy::audioSummaryFieldsFollowFragmentedRanges()
{
	// Microsoft WAVEFORMATEXTENSIBLE: 24 storage bits, 20 valid bits, stereo
	// speaker mask and PCM GUID. Only its Bento storage is fragmented here.
	const auto format = number<quint16>(0xfffe) + number<quint16>(2) + number<quint32>(48000) + number<quint32>(288000) +
		number<quint16>(6) + number<quint16>(24) + number<quint16>(22) + number<quint16>(20) + number<quint32>(3) +
		QByteArray::fromHex("0100000000001000800000aa00389b71");
	MediaEngine::RawProperty summary;
	summary.locator.name = QStringLiteral("OMFI:WAVD:Summary");
	summary.locator.objectNumber = 201;
	summary.encoding = wave(chunk("fmt ", format));
	summary.locator.ranges = {{100, 50}, {900, 10}};
	summary.state = PropertyReadState::Present;
	auto context = QSharedPointer<MediaEngine::BentoPropertyContext>::create();
	context->property = 123;
	context->typeName = QStringLiteral("omfi:DataValue");
	summary.bento = context;
	const auto fields = MediaEngine::Detail::decodeAudioSummary(summary);
	const auto guid = std::find_if(fields.cbegin(), fields.cend(), [](const auto &field)
		{ return field.locator.name == QLatin1String("OMFI:WAVD:Summary.fmt .SubFormat"); });
	QVERIFY(guid != fields.cend());
	QCOMPARE(guid->state, PropertyReadState::Present);
	QCOMPARE(guid->encoding, QByteArray::fromHex("0100000000001000800000aa00389b71"));
	QCOMPARE(guid->locator.objectNumber, quint64(201));
	QCOMPARE(guid->bento, summary.bento);
	QCOMPARE(guid->locator.ranges.size(), 2);
	QCOMPARE(guid->locator.ranges[0].offset, qint64(144));
	QCOMPARE(guid->locator.ranges[0].length, qint64(6));
	QCOMPARE(guid->locator.ranges[1].offset, qint64(900));
	QCOMPARE(guid->locator.ranges[1].length, qint64(10));
}

QTEST_APPLESS_MAIN(TestMediaEngineLegacy)
#include "tst_mediaenginelegacy.moc"
