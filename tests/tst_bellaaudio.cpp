// Native audio wire controls. Only the native-audio reader is used.
#include "bella/audioreader_p.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace
{
	using Read = Bella::OmfReadResult;
	using Encoding = Bella::OmfAudioEncoding;
	template <typename T> QByteArray number(T value, bool big = false)
	{
		QByteArray bytes(sizeof(T), '\0');
		if (big)
			qToBigEndian(value, bytes.data());
		else
			qToLittleEndian(value, bytes.data());
		return bytes;
	}
	QByteArray chunk(const char *key, const QByteArray &body, bool big = false)
	{
		return QByteArray(key, 4) + number<quint32>(quint32(body.size()), big) + body + QByteArray(body.size() & 1, '\0');
	}
	QByteArray wave(const QByteArray &chunks)
	{
		return QByteArray("RIFF") + number<quint32>(quint32(chunks.size() + 4)) + "WAVE" + chunks;
	}
	QByteArray aiff(const QByteArray &chunks, bool compressed = false)
	{
		return QByteArray("FORM") + number<quint32>(quint32(chunks.size() + 4), true) + (compressed ? "AIFC" : "AIFF") + chunks;
	}
	QByteArray fmt(quint16 code = 1, quint16 bits = 24, quint32 frequency = 48000, quint16 align = 3)
	{
		auto result = number(code) + number<quint16>(1) + number(frequency) + number<quint32>(frequency) + number(align) + number(bits);
		if (code != 1)
			result += number<quint16>(0);
		return result;
	}
	QByteArray extensible(quint16 precision = 20, quint16 storage = 24, bool floating = false)
	{
		return fmt(1, storage).replace(0, 2, number<quint16>(0xfffe)) + number<quint16>(22) + number(precision) +
		       number<quint32>(1) + QByteArray::fromHex(floating ? "0300000000001000800000aa00389b71" : "0100000000001000800000aa00389b71");
	}
	QByteArray comm(quint32 frames = 2, qint16 bits = 24, const QByteArray &frequency = QByteArray::fromHex("400ebb80000000000000"), const QByteArray &coding = {})
	{
		auto result = number<qint16>(1, true) + number(frames, true) + number(bits, true) + frequency;
		if (!coding.isEmpty())
			result += coding + '\0'; // Complete empty Pascal compression name.
		return result;
	}

	struct Range { qint64 offset = 0; qint64 length = 0; };
	struct Patch { qint64 offset = 0; QByteArray bytes; };
	class GuardedFile final : public QFile
	{
	public:
		using QFile::QFile;
		QVector<Range> denied;
		QVector<Patch> patches;
		std::optional<qint64> virtualExtent;
		std::atomic_bool *stop = nullptr;
		int cancelAfterRead = 0;
		int failAtRead = 0;
		int reads = 0;
		qint64 bytesRead = 0;
		bool forbiddenRead = false;
		bool unexpectedRead = false;
		qint64 size() const override { return virtualExtent.value_or(QFile::size()); }
	protected:
		qint64 readData(char *destination, qint64 length) override
		{
			++reads;
			const qint64 offset = pos();
			for (const auto &range : denied)
				if (offset < range.offset + range.length && range.offset < offset + length)
				{
					forbiddenRead = true;
					return -1;
				}
			if (reads == failAtRead)
				return -1;
			qint64 count = -1;
			if (virtualExtent)
			{
				for (const auto &patch : patches)
					if (offset >= patch.offset && offset - patch.offset <= patch.bytes.size() &&
					    length <= patch.bytes.size() - (offset - patch.offset))
					{
						std::memcpy(destination, patch.bytes.constData() + offset - patch.offset, std::size_t(length));
						count = length;
						break;
					}
				if (count < 0)
					unexpectedRead = true;
			}
			else
				count = QFile::readData(destination, length);
			if (count > 0)
				bytesRead += count;
			if (stop && reads == cancelAfterRead)
				stop->store(true);
			return count;
		}
	};

	Bella::Detail::NativeAudioRead parse(const QByteArray &bytes)
	{
		QTemporaryDir directory;
		QFile output(directory.filePath(QStringLiteral("authored.audio")));
		if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
			return {};
		output.close();
		QFile file(output.fileName());
		if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
			return {};
		const std::atomic_bool stop{false};
		return Bella::Detail::readNativeAudio(file, QByteArrayView(bytes).first(std::min(qsizetype(12), bytes.size())), stop);
	}
	bool rate(const Bella::Detail::NativeAudioRead &result, qint32 numerator, qint32 denominator = 1)
	{
		return result.information.samplingRate && result.information.samplingRate->numerator == numerator &&
		       result.information.samplingRate->denominator == denominator;
	}
}

class TestBellaAudio final : public QObject
{
	Q_OBJECT
private slots:
	void ordinaryNative_data();
	void ordinaryNative();
	void nativeWaveQualification_data();
	void nativeWaveQualification();
	void nativeWaveRateRange_data();
	void nativeWaveRateRange();
	void nativeAiffQualification_data();
	void nativeAiffQualification();
	void exactNativeAiffRange_data();
	void exactNativeAiffRange();
	void framing_data();
	void framing();
	void discoveryAndPayloadExclusion();
	void ssndAlignmentAndFverCompatibility();
	void repeatedFormats();
	void rf64Navigation();
	void rf64Failures_data();
	void rf64Failures();
	void cancellationAndIo();
	void returnsOwnedFacts();
};

void TestBellaAudio::ordinaryNative_data()
{
	QTest::addColumn<bool>("isAiff");
	QTest::addColumn<bool>("zero");
	QTest::newRow("PCM WAVE") << false << false;
	QTest::newRow("PCM WAVE zero") << false << true;
	QTest::newRow("AIFF") << true << false;
	QTest::newRow("AIFF zero") << true << true;
}
void TestBellaAudio::ordinaryNative()
{
	QFETCH(bool, isAiff);
	QFETCH(bool, zero);
	const auto bytes = isAiff ? aiff(chunk("COMM", comm(zero ? 0 : 2), true) + chunk("SSND", QByteArray(8, '\0'), true))
	                          : wave(chunk("fmt ", fmt()) + chunk("data", QByteArray(zero ? 0 : 6, '\x7f')));
	const auto result = parse(bytes);
	QCOMPARE(result.reading, Read::Completed);
	QVERIFY(result.locationDiscoveryComplete);
	QCOMPARE(result.declaredContainerEnd, std::optional<qint64>(bytes.size()));
	QCOMPARE(result.information.kind, Bella::OmfKind::Audio);
	QVERIFY(rate(result, 48000));
	QCOMPARE(result.information.bitDepth, std::optional<quint16>(24));
	QCOMPARE(result.information.encoding, Encoding::Pcm);
	QCOMPARE(result.information.sampleFrameCount, std::optional<qint64>(zero ? 0 : 2));
	QVERIFY(!result.omfLocation);
	QVERIFY(!result.multipleOmfLocations);
	QVERIFY(result.problemNotes.isEmpty());
}

void TestBellaAudio::nativeWaveQualification_data()
{
	QTest::addColumn<QByteArray>("format");
	QTest::addColumn<int>("precision");
	QTest::addColumn<int>("encoding");
	QTest::addColumn<bool>("countKnown");
	QTest::addColumn<bool>("malformed");
	QTest::newRow("unknown coding retains ordinary depth") << fmt(6, 16) << 16 << int(Encoding::Unknown) << false << false;
	QTest::newRow("native float24 is preserved") << fmt(3, 24) << 24 << int(Encoding::IeeeFloat) << true << false;
	QTest::newRow("native float32") << fmt(3, 32) << 32 << int(Encoding::IeeeFloat) << true << false;
	QTest::newRow("extensible PCM valid20 storage24") << extensible() << 20 << int(Encoding::Pcm) << true << false;
	QTest::newRow("extensible float retains native precision") << extensible(24, 24, true) << 24 << int(Encoding::IeeeFloat) << true << false;
	auto unknown = extensible();
	unknown[39] = char(0x70); // Full GUID required, not just its first word.
	QTest::newRow("unsupported full GUID") << unknown << 0 << int(Encoding::Unknown) << false << false;
	QTest::newRow("short GUID retains base fields") << extensible().first(24) << 0 << int(Encoding::Unknown) << false << true;
	auto badSize = extensible();
	badSize.replace(16, 2, number<quint16>(23));
	QTest::newRow("oversized cbSize retains complete native subtype") << badSize << 20 << int(Encoding::Pcm) << true << true;
	auto shortSize = extensible();
	shortSize.replace(16, 2, number<quint16>(20));
	QTest::newRow("undersized cbSize does not qualify subtype") << shortSize << 0 << int(Encoding::Unknown) << false << true;
	QTest::newRow("precision exceeds storage") << extensible(25) << 0 << int(Encoding::Pcm) << true << true;
	QTest::newRow("known zero valid precision is not storage") << extensible(0) << 0 << int(Encoding::Pcm) << true << false;
	QTest::newRow("zero alignment cannot supply count") << fmt(1, 24, 48000, 0) << 24 << int(Encoding::Pcm) << false << false;
}
void TestBellaAudio::nativeWaveQualification()
{
	QFETCH(QByteArray, format);
	QFETCH(int, precision);
	QFETCH(int, encoding);
	QFETCH(bool, countKnown);
	QFETCH(bool, malformed);
	const auto result = parse(wave(chunk("fmt ", format) + chunk("data", QByteArray(6, '\0'))));
	QCOMPARE(result.reading, malformed ? Read::Malformed : Read::Completed);
	QVERIFY(result.locationDiscoveryComplete);
	QCOMPARE(result.information.kind, Bella::OmfKind::Audio);
	QVERIFY(rate(result, 48000));
	QCOMPARE(result.information.bitDepth, precision ? std::optional<quint16>(quint16(precision)) : std::nullopt);
	QCOMPARE(int(result.information.encoding), encoding);
	QCOMPARE(result.information.sampleFrameCount, countKnown ? std::optional<qint64>(2) : std::nullopt);
	QCOMPARE(!result.problemNotes.isEmpty(), malformed);
}

void TestBellaAudio::nativeWaveRateRange_data()
{
	QTest::addColumn<quint32>("frequency");
	QTest::addColumn<bool>("usable");
	QTest::newRow("zero unavailable") << quint32(0) << false;
	QTest::newRow("actual uncommon11127 no whitelist") << quint32(11127) << true;
	QTest::newRow("48000") << quint32(48000) << true;
	QTest::newRow("signed32 maximum") << quint32(std::numeric_limits<qint32>::max()) << true;
	QTest::newRow("unsigned value above native range") << (quint32(std::numeric_limits<qint32>::max()) + 1) << false;
	QTest::newRow("unsigned32 maximum") << std::numeric_limits<quint32>::max() << false;
}
void TestBellaAudio::nativeWaveRateRange()
{
	QFETCH(quint32, frequency);
	QFETCH(bool, usable);
	const auto result = parse(wave(chunk("fmt ", fmt(1, 24, frequency)) + chunk("data", QByteArray(6, '\0'))));
	QCOMPARE(result.reading, Read::Completed);
	QCOMPARE(result.information.samplingRate.has_value(), usable);
	if (usable)
		QVERIFY(rate(result, qint32(frequency)));
	QCOMPARE(result.information.bitDepth, std::optional<quint16>(24));
	QCOMPARE(result.information.encoding, Encoding::Pcm);
	QVERIFY(result.problemNotes.isEmpty());
}

void TestBellaAudio::nativeAiffQualification_data()
{
	QTest::addColumn<QByteArray>("common");
	QTest::addColumn<int>("precision");
	QTest::addColumn<int>("encoding");
	QTest::addColumn<bool>("malformed");
	for (const auto &coding : {QByteArray("NONE"), QByteArray("twos"), QByteArray("sowt"), QByteArray("in24"), QByteArray("in32")})
		QTest::newRow(coding.constData()) << comm(2, 24, QByteArray::fromHex("400ebb80000000000000"), coding) << 24 << int(Encoding::Pcm) << false;
	for (const auto &coding : {QByteArray("fl32"), QByteArray("FL32"), QByteArray("fl64"), QByteArray("FL64")})
		QTest::newRow(coding.constData()) << comm(2, 24, QByteArray::fromHex("400ebb80000000000000"), coding) << 24 << int(Encoding::IeeeFloat) << false;
	QTest::newRow("in24 preserves recorded16") << comm(2, 16, QByteArray::fromHex("400ebb80000000000000"), "in24") << 16 << int(Encoding::Pcm) << false;
	QTest::newRow("unknown coding retains depth count and rate") << comm(2, 24, QByteArray::fromHex("400ebb80000000000000"), "Xxxx") << 24 << int(Encoding::Unknown) << false;
	QTest::newRow("missing required coding retains independent fields") << comm() << 24 << int(Encoding::Unknown) << true;
	QTest::newRow("missing name retains coding") << comm(2, 24, QByteArray::fromHex("400ebb80000000000000"), "in24").first(22) << 24 << int(Encoding::Pcm) << true;
	auto badName = comm(2, 24, QByteArray::fromHex("400ebb80000000000000"), "in24");
	badName[22] = char(20);
	QTest::newRow("truncated descriptive name retains coding") << badName << 24 << int(Encoding::Pcm) << true;
}
void TestBellaAudio::nativeAiffQualification()
{
	QFETCH(QByteArray, common);
	QFETCH(int, precision);
	QFETCH(int, encoding);
	QFETCH(bool, malformed);
	const auto result = parse(aiff(chunk("COMM", common, true) + chunk("SSND", QByteArray(8, '\0'), true), true));
	QCOMPARE(result.reading, malformed ? Read::Malformed : Read::Completed);
	QVERIFY(result.locationDiscoveryComplete);
	QVERIFY(rate(result, 48000));
	QCOMPARE(result.information.kind, Bella::OmfKind::Audio);
	QCOMPARE(result.information.bitDepth, std::optional<quint16>(quint16(precision)));
	QCOMPARE(result.information.sampleFrameCount, std::optional<qint64>(2));
	QCOMPARE(int(result.information.encoding), encoding);
	QCOMPARE(!result.problemNotes.isEmpty(), malformed);
}

void TestBellaAudio::exactNativeAiffRange_data()
{
	QTest::addColumn<QByteArray>("frequency");
	QTest::addColumn<qint32>("numerator");
	QTest::addColumn<qint32>("denominator");
	QTest::newRow("44100") << QByteArray::fromHex("400eac44000000000000") << qint32(44100) << qint32(1);
	QTest::newRow("48000.5 exact") << QByteArray::fromHex("400ebb80800000000000") << qint32(96001) << qint32(2);
	QTest::newRow("maximum numerator") << QByteArray::fromHex("401dfffffffe00000000") << std::numeric_limits<qint32>::max() << qint32(1);
	QTest::newRow("maximum supported denominator") << QByteArray::fromHex("3fe18000000000000000") << qint32(1) << qint32(1 << 30);
	QTest::newRow("native unnormal encoding remains accepted") << QByteArray::fromHex("400f5dc0000000000000") << qint32(48000) << qint32(1);
	for (const auto &hex : {"401e8000000000000000", "3fe08000000000000000", "c00ebb80000000000000", "7fff8000000000000000", "00000000000000000000"})
		QTest::newRow(hex) << QByteArray::fromHex(hex) << qint32(0) << qint32(0);
}
void TestBellaAudio::exactNativeAiffRange()
{
	QFETCH(QByteArray, frequency);
	QFETCH(qint32, numerator);
	QFETCH(qint32, denominator);
	const auto result = parse(aiff(chunk("COMM", comm(2, 24, frequency), true)));
	QCOMPARE(result.reading, Read::Completed);
	QCOMPARE(result.information.samplingRate.has_value(), numerator > 0);
	if (numerator > 0)
		QVERIFY(rate(result, numerator, denominator));
	QCOMPARE(result.information.sampleFrameCount, std::optional<qint64>(2));
	QCOMPARE(result.information.bitDepth, std::optional<quint16>(24));
	QVERIFY(result.problemNotes.isEmpty());
}

void TestBellaAudio::framing_data()
{
	QTest::addColumn<QByteArray>("bytes");
	QTest::addColumn<int>("reading");
	QTest::addColumn<bool>("nativeFacts");
	QTest::addColumn<bool>("discoveryComplete");
	QTest::newRow("short native header") << QByteArray("RIFFxx") << int(Read::Incomplete) << false << false;
	QTest::newRow("unsupported form") << (QByteArray("RIFX") + QByteArray(8, '\0')) << int(Read::Unsupported) << false << false;
	QTest::newRow("small declared form") << (QByteArray("RIFF") + number<quint32>(3) + "WAVE") << int(Read::Malformed) << false << false;
	QTest::newRow("empty valid form has no Audio Kind") << wave({}) << int(Read::Completed) << false << true;
	QTest::newRow("short fmt local failure") << wave(chunk("fmt ", QByteArray(4, '\0'))) << int(Read::Malformed) << false << true;
	QTest::newRow("later incomplete chunk header") << wave(chunk("fmt ", fmt()) + "bad") << int(Read::Malformed) << true << false;
	QTest::newRow("data exceeds physical file") << wave(chunk("fmt ", fmt()) + "data" + number<quint32>(100) + "xy") << int(Read::Incomplete) << true << false;
	auto clipped = wave(chunk("fmt ", fmt()) + chunk("data", QByteArray(6, '\0')));
	clipped.replace(4, 4, number<quint32>(quint32(clipped.size() - 8 - 4)));
	QTest::newRow("chunk exceeds form but file bytes exist") << clipped << int(Read::Malformed) << true << false;
	auto odd = wave(chunk("fmt ", fmt()) + chunk("data", QByteArray(3, '\0')));
	odd.replace(4, 4, number<quint32>(quint32(odd.size() - 9)));
	QTest::newRow("alignment outside form is not guessed") << odd << int(Read::Malformed) << true << false;
}
void TestBellaAudio::framing()
{
	QFETCH(QByteArray, bytes);
	QFETCH(int, reading);
	QFETCH(bool, nativeFacts);
	QFETCH(bool, discoveryComplete);
	const auto result = parse(bytes);
	QCOMPARE(int(result.reading), reading);
	QCOMPARE(result.locationDiscoveryComplete, discoveryComplete);
	QCOMPARE(result.information.kind == Bella::OmfKind::Audio, nativeFacts);
	QCOMPARE(rate(result, 48000), nativeFacts);
	QCOMPARE(result.information.bitDepth.has_value(), nativeFacts);
	if (reading != int(Read::Completed))
		QVERIFY(!result.problemNotes.isEmpty());
}

void TestBellaAudio::discoveryAndPayloadExclusion()
{
	const auto first = chunk("fmt ", fmt()) + chunk("data", QByteArray(600, '\x6a'));
	const qint64 omfStart = 12 + first.size() + 8;
	const auto bytes = wave(first + chunk("omfi", QByteArray(80, 'q')) + chunk("JUNK", QByteArray(101, 'j')) + chunk("omfi", QByteArray(24, 'r')) + chunk("tail", "done"));
	QTemporaryDir directory;
	const auto path = directory.filePath(QStringLiteral("guarded.wav"));
	QFile output(path);
	QVERIFY(output.open(QIODevice::WriteOnly));
	QCOMPARE(output.write(bytes), qint64(bytes.size()));
	output.close();
	GuardedFile file(path);
	file.denied = {{44, 600}, {omfStart, 80}, {omfStart + 88, 101}};
	QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
	const std::atomic_bool stop{false};
	const auto result = Bella::Detail::readNativeAudio(file, QByteArrayView(bytes).first(12), stop);
	QCOMPARE(result.reading, Read::Completed);
	QVERIFY(result.locationDiscoveryComplete);
	QVERIFY(result.multipleOmfLocations);
	QVERIFY(result.omfLocation);
	QCOMPARE(result.omfLocation->payloadOffset, omfStart);
	QCOMPARE(result.omfLocation->payloadLength, qint64(80));
	QCOMPARE(result.omfLocation->labelOffset, std::optional<qint64>(omfStart + 56));
	QCOMPARE(result.information.sampleFrameCount, std::optional<qint64>(200));
	QVERIFY(!file.forbiddenRead);
	QCOMPARE(file.bytesRead, qint64(6 * 8 + 16));
	QVERIFY(file.isOpen());
	const auto shortOmf = parse(wave(chunk("omfi", "abc") + chunk("tail", "x")));
	QCOMPARE(shortOmf.reading, Read::Completed);
	QVERIFY(shortOmf.locationDiscoveryComplete);
	QVERIFY(shortOmf.omfLocation);
	QVERIFY(!shortOmf.omfLocation->labelOffset);
	QVERIFY(!shortOmf.multipleOmfLocations);
	const auto withTrailer = parse(wave(chunk("fmt ", fmt())) + QByteArray(24, 'z'));
	QVERIFY(withTrailer.locationDiscoveryComplete);
	QCOMPARE(withTrailer.declaredContainerEnd, std::optional<qint64>(36));
}

void TestBellaAudio::ssndAlignmentAndFverCompatibility()
{
	const auto common = comm(2, 24, QByteArray::fromHex("400ebb80000000000000"), "in24");
	const auto sound = number<quint32>(3410, true) + number<quint32>(4096, true) + QByteArray(3416, '\x55');
	const auto prefix = chunk("COMM", common, true);
	const qint64 samples = 12 + prefix.size() + 8 + 8;
	for (const auto &version : {QByteArray{}, chunk("FVER", QByteArray::fromHex("00000001"), true), chunk("FVER", "bad", true)})
	{
		const auto bytes = aiff(prefix + chunk("SSND", sound, true) + version + chunk("tail", "after", true), true);
		QTemporaryDir directory;
		QFile output(directory.filePath(QStringLiteral("authored.aif")));
		QVERIFY(output.open(QIODevice::WriteOnly));
		QCOMPARE(output.write(bytes), qint64(bytes.size()));
		output.close();
		GuardedFile file(output.fileName());
		file.denied = {{samples, sound.size() - 8}};
		QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
		const std::atomic_bool stop{false};
		const auto result = Bella::Detail::readNativeAudio(file, QByteArrayView(bytes).first(12), stop);
		QCOMPARE(result.reading, Read::Completed);
		QVERIFY(result.locationDiscoveryComplete);
		QCOMPARE(result.information.sampleFrameCount, std::optional<qint64>(2));
		QVERIFY(rate(result, 48000));
		QVERIFY(result.problemNotes.isEmpty());
		QVERIFY(!file.forbiddenRead);
	}
	const auto badOffset = parse(aiff(prefix + chunk("SSND", number<quint32>(100, true) + number<quint32>(0, true), true), true));
	QCOMPARE(badOffset.reading, Read::Malformed);
	QVERIFY(badOffset.locationDiscoveryComplete);
	QCOMPARE(badOffset.information.sampleFrameCount, std::optional<qint64>(2));
	QVERIFY(rate(badOffset, 48000));
}

void TestBellaAudio::repeatedFormats()
{
	const auto data = chunk("data", QByteArray(6, '\0'));
	const auto equal = parse(wave(chunk("fmt ", fmt()) + data + chunk("fmt ", fmt())));
	QVERIFY(rate(equal, 48000));
	QCOMPARE(equal.information.sampleFrameCount, std::optional<qint64>(2));
	QVERIFY(equal.problemNotes.isEmpty());
	const auto conflict = parse(wave(chunk("fmt ", fmt()) + data + chunk("fmt ", fmt(1, 24, 44100)) + chunk("fmt ", fmt())));
	QCOMPARE(conflict.reading, Read::Completed);
	QVERIFY(!conflict.information.samplingRate);
	QCOMPARE(conflict.information.bitDepth, std::optional<quint16>(24));
	QCOMPARE(conflict.problemNotes.size(), 1);
	const auto unavailable = parse(wave(chunk("fmt ", fmt(6, 16)) + data + chunk("fmt ", fmt())));
	QVERIFY(rate(unavailable, 48000));
	QVERIFY(!unavailable.information.bitDepth); // Two usable precisions conflict.
	QCOMPARE(unavailable.information.encoding, Encoding::Pcm);
	QCOMPARE(unavailable.information.sampleFrameCount, std::optional<qint64>(2));
	const auto noHybrid = parse(wave(chunk("fmt ", fmt(1, 24, 0)) + data + chunk("fmt ", fmt(6, 24))));
	QVERIFY(rate(noHybrid, 48000));
	QVERIFY(!noHybrid.information.sampleFrameCount);
	const auto twoData = parse(wave(chunk("fmt ", fmt()) + data + data));
	QVERIFY(!twoData.information.sampleFrameCount);
	QVERIFY(rate(twoData, 48000));
	const auto remainder = parse(wave(chunk("fmt ", fmt()) + chunk("data", QByteArray(4, '\0'))));
	QVERIFY(!remainder.information.sampleFrameCount);
	QVERIFY(rate(remainder, 48000));
}

void TestBellaAudio::rf64Navigation()
{
	// A virtual QFile supplies only the authored metadata at real wire offsets.
	// Any attempt to request the 5 GiB sample extent fails the test.
	const quint64 samples = quint64{5} << 30;
	const auto format = chunk("fmt ", fmt());
	const qint64 dataOffset = 12 + 8 + 28 + format.size() + 8;
	const auto suffix = chunk("omfi", QByteArray(24, 'o')) + chunk("tail", "last");
	const quint64 fileSize = quint64(dataOffset) + samples + quint64(suffix.size());
	const auto sizes = number<quint64>(fileSize - 8) + number(samples) + number<quint64>(999) + number<quint32>(0);
	const auto prefix = QByteArray("RF64") + number<quint32>(0xffffffff) + "WAVE" + chunk("ds64", sizes) + format + "data" + number<quint32>(0xffffffff);
	QTemporaryDir directory;
	QFile output(directory.filePath(QStringLiteral("virtual.wav")));
	QVERIFY(output.open(QIODevice::WriteOnly));
	QCOMPARE(output.write(prefix), qint64(prefix.size()));
	output.close();
	GuardedFile file(output.fileName());
	file.virtualExtent = qint64(fileSize);
	file.patches = {{0, prefix}, {dataOffset + qint64(samples), suffix}};
	file.denied = {{dataOffset, qint64(samples)}};
	QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
	const std::atomic_bool stop{false};
	const auto result = Bella::Detail::readNativeAudio(file, QByteArrayView(prefix).first(12), stop);
	QCOMPARE(result.reading, Read::Completed);
	QVERIFY(result.locationDiscoveryComplete);
	QVERIFY(result.omfLocation);
	QCOMPARE(result.omfLocation->payloadOffset, dataOffset + qint64(samples) + 8);
	QVERIFY(!result.information.sampleFrameCount); // 5 GiB is not divisible by blockAlign3; ds64.sampleCount is not a substitute.
	QVERIFY(rate(result, 48000));
	QVERIFY(!file.forbiddenRead);
	QVERIFY(!file.unexpectedRead);
	QVERIFY(file.bytesRead < 200);
	for (const bool extendedHeader : {false, true})
	{
		const auto chunks = format + chunk("data", QByteArray(6, 'a')) + "data" + number<quint32>(0xffffffff) + QByteArray(8, 'b') + "data" + number<quint32>(0xffffffff) + QByteArray(10, 'c');
		const quint64 extent = 12 + 8 + 28 + 24 + quint64(chunks.size());
		const auto table = number<quint64>(extendedHeader ? extent - 8 : 0) + number<quint64>(999) + number<quint64>(100) + number<quint32>(2) + "data" + number<quint64>(8) + "data" + number<quint64>(10);
		const auto actual = QByteArray("RF64") + number<quint32>(extendedHeader ? 0xffffffff : quint32(extent - 8)) + "WAVE" + chunk("ds64", table) + chunks;
		const auto repeated = parse(actual);
		QCOMPARE(repeated.reading, Read::Completed);
		QVERIFY(repeated.locationDiscoveryComplete);
		QVERIFY(!repeated.information.sampleFrameCount);
		QVERIFY(rate(repeated, 48000));
	}
}

void TestBellaAudio::rf64Failures_data()
{
	QTest::addColumn<QByteArray>("chunks");
	QTest::newRow("no first ds64") << chunk("fmt ", fmt());
	QTest::newRow("short ds64") << chunk("ds64", QByteArray(20, '\0'));
	const auto invalid = number<quint64>(60) + number<quint64>(6) + number<quint64>(2) + number<quint32>(2) + "data" + number<quint64>(6);
	QTest::newRow("truncated table count") << chunk("ds64", invalid);
	const auto sizes = number<quint64>(0) + number<quint64>(6) + number<quint64>(2) + number<quint32>(0);
	QTest::newRow("duplicate ds64") << (chunk("ds64", sizes) + chunk("ds64", sizes));
	QTest::newRow("missing size table entry") << (chunk("ds64", sizes) + chunk("data", QByteArray(6, '\0')) + "data" + number<quint32>(0xffffffff));
}
void TestBellaAudio::rf64Failures()
{
	QFETCH(QByteArray, chunks);
	const auto result = parse(QByteArray("RF64") + number<quint32>(quint32(chunks.size() + 4)) + "WAVE" + chunks);
	QCOMPARE(result.reading, Read::Malformed);
	QVERIFY(!result.locationDiscoveryComplete);
	QVERIFY(!result.problemNotes.isEmpty());
}

void TestBellaAudio::cancellationAndIo()
{
	std::atomic_bool stopped{true};
	QFile unopened;
	const auto early = Bella::Detail::readNativeAudio(unopened, {}, stopped);
	QCOMPARE(early.reading, Read::Cancelled);
	QVERIFY(!early.locationDiscoveryComplete);
	stopped.store(false);
	QCOMPARE(Bella::Detail::readNativeAudio(unopened, {}, stopped).reading, Read::IoError);
	const auto bytes = wave(chunk("fmt ", fmt()) + chunk("data", QByteArray(6, '\0')));
	for (const bool cancel : {false, true})
	{
		QTemporaryDir directory;
		QFile output(directory.filePath(QStringLiteral("interrupted.wav")));
		QVERIFY(output.open(QIODevice::WriteOnly));
		QCOMPARE(output.write(bytes), qint64(bytes.size()));
		output.close();
		GuardedFile file(output.fileName());
		stopped.store(false);
		file.stop = &stopped;
		file.cancelAfterRead = cancel ? 3 : 0;
		file.failAtRead = cancel ? 0 : 3;
		QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
		const auto result = Bella::Detail::readNativeAudio(file, QByteArrayView(bytes).first(12), stopped);
		QCOMPARE(result.reading, cancel ? Read::Cancelled : Read::IoError);
		QCOMPARE(file.reads, 3);
		QVERIFY(!result.locationDiscoveryComplete);
		QCOMPARE(result.information.kind, Bella::OmfKind::Audio);
		QVERIFY(rate(result, 48000));
		QCOMPARE(result.information.bitDepth, std::optional<quint16>(24));
		QVERIFY(!result.information.sampleFrameCount);
		QCOMPARE(result.problemNotes.size(), 1);
		QVERIFY(file.isOpen());
	}
}

void TestBellaAudio::returnsOwnedFacts()
{
	const auto original = parse(wave(chunk("fmt ", fmt()) + chunk("data", QByteArray(6, '\0')) + chunk("omfi", QByteArray(24, 'x'))));
	auto copied = original;
	const auto moved = std::move(copied);
	QCOMPARE(moved.reading, Read::Completed);
	QVERIFY(rate(moved, 48000));
	QCOMPARE(moved.information.sampleFrameCount, std::optional<qint64>(2));
	QVERIFY(moved.omfLocation);
	QCOMPARE(moved.omfLocation->payloadLength, qint64(24));
}

QTEST_APPLESS_MAIN(TestBellaAudio)
#include "tst_bellaaudio.moc"
