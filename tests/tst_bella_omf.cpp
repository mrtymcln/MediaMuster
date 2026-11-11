// Fresh authored OMF1 graphs. These are standalone/embedded OMF inputs, never
// MDB-mode calls. Native wrappers contain only tiny authored dummy sound extents.
#include "scanner/omf_parser.h"
#include "media/omf_uid.h"
#include "test_bento.h"

#include <QFile>
#include <QTemporaryFile>
#include <QtEndian>
#include <QtTest>
#include <array>
#include <atomic>
#include <limits>
#include <variant>

// MARK: - Test helpers

namespace
{
	using TestMediaEngineBento::number;
	using Writer = TestMediaEngineBento::TypedBento;
	using Bella::OmfCheckResult;
	using Bella::OmfReadResult;

	QByteArray uid(quint32 serial, bool big)
	{
		return number<quint32>(42, big) + number<quint32>(0x11223344, big) + number(serial, big);
	}
	QByteArray canonicalUid(quint32 serial)
	{
		return QByteArray::fromHex("060a2b340101010101010f001300000044332211") +
			   number(serial) + QByteArray::fromHex("060e2b347f7f2a80");
	}
	void object(Writer &writer, quint32 handle, const QByteArray &tag)
	{
		writer.add(handle, "OMFI:ObjID", "omfi:ObjectTag", tag, true);
	}
	void erase(Writer &writer, quint32 owner, const QByteArray &name)
	{
		const auto property = writer.properties.value(name);
		writer.entries.removeIf([&](const auto &entry)
								{ return entry.object == owner && entry.property == property; });
	}
	void put(Writer &writer, quint32 owner, const QByteArray &name, const QByteArray &type,
			 const QByteArray &value, bool immediate = false)
	{
		erase(writer, owner, name);
		writer.add(owner, name, type, value, immediate);
	}
	template <typename T>
	void scalar(Writer &writer, quint32 owner, const QByteArray &name,
				const QByteArray &type, T value)
	{
		put(writer, owner, name, type, number(value, writer.metadataBig), sizeof(T) <= 4);
	}
	void rate(Writer &writer, quint32 owner, const QByteArray &name, qint32 numerator,
			  qint32 denominator, const QByteArray &type = "omfi:ExactEditRate")
	{
		put(writer, owner, name, type, number(numerator, writer.metadataBig) + number(denominator, writer.metadataBig));
	}
	void reference(Writer &writer, quint32 owner, const QByteArray &name, quint32 target)
	{
		put(writer, owner, name, "omfi:ObjRef", writer.reference(target));
	}
	void references(Writer &writer, quint32 owner, const QByteArray &name, const QVector<quint32> &targets)
	{
		erase(writer, owner, name);
		writer.referenceArray(owner, name, targets);
	}
	void members(Writer &writer, const QVector<quint32> &objects)
	{
		references(writer, 1, "OMFI:ObjectSpine", objects);
	}
	void text(Writer &writer, quint32 owner, const QByteArray &name, const QByteArray &bytes)
	{
		put(writer, owner, name, "omfi:String", bytes + '\0');
	}
	void component(Writer &writer, quint32 handle, const QByteArray &tag, qint16 kind,
				   qint32 length = 250, qint32 numerator = 25, qint32 denominator = 1)
	{
		object(writer, handle, tag);
		scalar(writer, handle, "OMFI:CPNT:TrackKind", "omfi:TrackType", kind);
		scalar(writer, handle, "OMFI:CLIP:Length", "omfi:Long", length);
		rate(writer, handle, "OMFI:CPNT:EditRate", numerator, denominator);
	}
	void track(Writer &writer, quint32 handle, quint32 segment, qint16 label = 1)
	{
		object(writer, handle, "TRAK");
		scalar(writer, handle, "OMFI:TRAK:LabelNumber", "omfi:Short", label);
		reference(writer, handle, "OMFI:TRAK:TrackComponent", segment);
	}
	void sourceClip(Writer &writer, quint32 handle, const QByteArray &id, qint16 kind = 1,
					qint32 length = 250, qint32 numerator = 25, qint32 denominator = 1)
	{
		component(writer, handle, "SCLP", kind, length, numerator, denominator);
		put(writer, handle, "OMFI:SCLP:SourceID", "omfi:UID", id);
		scalar(writer, handle, "OMFI:SCLP:SourceTrack", "omfi:Short", qint16(1));
		scalar(writer, handle, "OMFI:SCLP:SourcePosition", "omfi:Long", qint32(0));
	}
	Writer specimen(bool big = false, const QByteArray &descriptor = "CDCI")
	{
		Writer writer;
		writer.metadataBig = big;
		writer.head();
		members(writer, {100, 300, 600});
		object(writer, 100, "MOBJ");
		put(writer, 100, "OMFI:MOBJ:MobID", "omfi:UID", uid(11, big));
		reference(writer, 100, "OMFI:MOBJ:PhysicalMedia", 200);
		rate(writer, 100, "OMFI:CPNT:EditRate", 25, 1);
		references(writer, 100, "OMFI:TRKG:Tracks", {110});
		track(writer, 110, 120);
		sourceClip(writer, 120, QByteArray(12, '\0'));
		object(writer, 200, descriptor);
		scalar(writer, 200, "OMFI:MDFL:Length", "omfi:Long", qint32(250));
		rate(writer, 200, "OMFI:MDFL:SampleRate", 25, 1);
		scalar(writer, 200, "OMFI:DIDD:StoredWidth", "omfi:Long", qint32(1920));
		scalar(writer, 200, "OMFI:DIDD:StoredHeight", "omfi:Long", qint32(540));
		scalar(writer, 200, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", qint16(1));
		scalar(writer, 200, "OMFI:CDCI:ComponentWidth", "omfi:Long", qint32(10));
		scalar(writer, 200, "OMFI:CDCI:HorizontalSubsampling", "omfi:Ulong", quint32(2));
		scalar(writer, 200, "OMFI:CDCI:VerticalSubsampling", "omfi:Ulong", quint32(1));
		text(writer, 200, "OMFI:DIDD:Compression", "AVHD");
		scalar(writer, 200, "OMFI:DIDD:DIDResolutionID", "omfi:Long", qint32(1243));
		object(writer, 300, "MOBJ");
		put(writer, 300, "OMFI:MOBJ:MobID", "omfi:UID", uid(22, big));
		scalar(writer, 300, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", qint32(7));
		text(writer, 300, "OMFI:CPNT:Name", QByteArray("  Master ", 9) + char(0xa7) + "  ");
		references(writer, 300, "OMFI:TRKG:Tracks", {310});
		track(writer, 310, 320);
		sourceClip(writer, 320, uid(11, big));
		object(writer, 600, "TIFF");
		put(writer, 600, "OMFI:TIFF:MobID", "omfi:UID", uid(11, big));
		writer.mobIndex(1, "OMFI:SourceMobs", {{100, uid(11, big)}});
		writer.mobIndex(1, "OMFI:CompositionMobs", {{300, uid(22, big)}});
		writer.mobIndex(1, "OMFI:MediaData", {{600, uid(11, big)}});
		return writer;
	}
	struct Attribute
	{
		QByteArray name;
		quint16 kind = 2;
		QByteArray value;
		quint32 target = 0;
	};
	void attributes(Writer &writer, quint32 owner, quint32 list, const QVector<Attribute> &values)
	{
		object(writer, list, "ATTR");
		if (owner != 0)
			reference(writer, owner, "OMFI:CPNT:Attributes", list);
		QVector<quint32> handles;
		for (qsizetype n = 0; n < values.size(); ++n)
		{
			const auto handle = list + 1 + quint32(n);
			const auto &value = values[n];
			handles.append(handle);
			object(writer, handle, "ATTB");
			text(writer, handle, "OMFI:ATTB:Name", value.name);
			scalar(writer, handle, "OMFI:ATTB:Kind", "omfi:AttrKind", value.kind);
			if (value.kind == 3)
				reference(writer, handle, "OMFI:ATTB:ObjAttribute", value.target);
			else if (value.kind == 4)
			{
				put(writer, handle, "OMFI:ATTB:BobData", "omfi:VarLenBytes", value.value + '\0');
				scalar(writer, handle, "OMFI:ATTB:BobSize", "omfi:Long", qint32(value.value.size() + 1));
			}
			else
				text(writer, handle, "OMFI:ATTB:StringAttribute", value.value);
		}
		references(writer, list, "OMFI:ATTR:AttrRefs", handles);
	}
	void originalBin(Writer &writer, const QByteArray &unicode, const QByteArray &legacy)
	{
		attributes(writer, 300, 1000, {{"_ORG_BIN", 3, {}, 1100}});
		object(writer, 1100, "MCBR");
		if (!unicode.isNull())
			text(writer, 1100, "OMFI:MCBR:MC:binNameUTF8", unicode);
		if (!legacy.isNull())
			text(writer, 1100, "OMFI:MCBR:MC:binName", legacy);
	}
	QByteArray chunk(const QByteArray &name, const QByteArray &payload, bool big = false)
	{
		return name + number(quint32(payload.size()), big) + payload + QByteArray(payload.size() & 1, '\0');
	}
	QByteArray waveFormat(quint32 frequency = 48000, quint16 bits = 16)
	{
		return number<quint16>(1) + number<quint16>(1) + number(frequency) +
			   number<quint32>(frequency * (bits / 8)) + number<quint16>(bits / 8) + number(bits);
	}
	QByteArray waveSummary(quint32 frequency = 48000, quint16 bits = 24)
	{
		const auto body = chunk("JUNK", QByteArray(4100, 'j')) + chunk("fmt ", waveFormat(frequency, bits));
		return "RIFF" + number(quint32(body.size() + 4)) + "WAVE" + body;
	}
	QByteArray aiffSummary(bool compressed = false, const QByteArray &coding = "in24")
	{
		auto common = number<qint16>(1, true) + number<quint32>(96000, true) +
					  number<qint16>(24, true) + QByteArray::fromHex("400ebb80000000000000");
		if (compressed)
			common += coding + QByteArray(2, '\0');
		const auto body = chunk("JUNK", QByteArray(626, 'j'), true) + chunk("COMM", common, true);
		return "FORM" + number(quint32(body.size() + 4), true) + (compressed ? "AIFC" : "AIFF") + body;
	}
	Writer audio(const QByteArray &descriptor, bool big)
	{
		auto writer = specimen(big, descriptor);
		put(writer, 600, "OMFI:ObjID", "omfi:ObjectTag", descriptor == "AIFD" ? QByteArray("AIFC") : QByteArray("WAVE"), true);
		erase(writer, 600, "OMFI:TIFF:MobID");
		put(writer, 600, descriptor == "AIFD" ? "OMFI:AIFC:MobID" : "OMFI:WAVE:MobID", "omfi:UID", uid(11, big));
		rate(writer, 200, "OMFI:MDFL:SampleRate", 48000, 1);
		scalar(writer, 200, "OMFI:MDFL:Length", "omfi:Long", qint32(96000));
		for (const quint32 handle : {120u, 320u})
			scalar(writer, handle, "OMFI:CPNT:TrackKind", "omfi:TrackType", qint16(2));
		if (descriptor == "WAVD" || descriptor == "WAVE")
			put(writer, 200, "OMFI:WAVD:Summary", "omfi:DataValue", waveSummary());
		else if (descriptor == "AIFD")
			put(writer, 200, "OMFI:AIFD:Summary", "omfi:DataValue", aiffSummary(true));
		else
		{
			rate(writer, 200, descriptor == "MPGA" ? "OMFI:MDAU:AudioSamplingRate" : "OMFI:PCMA:AudioSamplingRate", 44100, 1);
			scalar(writer, 200, "OMFI:MDAU:BitsPerSample", "omfi:UInt16", quint16(24));
		}
		return writer;
	}
	Bella::OmfReport parse(const QByteArray &bytes, bool details = false)
	{
		QTemporaryFile file;
		if (!file.open() || file.write(bytes) != bytes.size() || !file.flush())
		{
			QTest::qFail("Cannot save authored OMF metadata", __FILE__, __LINE__);
			return {};
		}
		const std::atomic_bool stop{false};
		return Bella::parseOmf(file.fileName(), stop, details);
	}
	Bella::OmfReport parse(const Writer &writer, bool details = false) { return parse(writer.build(), details); }
	QByteArray embeddedWave(const QVector<Writer> &writers, bool trailingOrdinaryChunk = false)
	{
		QByteArray bytes = "RIFF" + QByteArray(4, '\0') + "WAVE";
		bytes += chunk("fmt ", waveFormat());
		bytes += chunk("data", QByteArray(8, '\0'));
		for (const auto &writer : writers)
			bytes += chunk("omfi", writer.build(quint32(bytes.size() + 8)));
		if (trailingOrdinaryChunk)
			bytes += chunk("JUNK", "ordinary");
		qToLittleEndian<quint32>(quint32(bytes.size() - 8), bytes.data() + 4);
		return bytes;
	}
	void expectedFileId(const Bella::OmfReport &report)
	{
		QVERIFY(report.fileInformation.fileMobId);
		QCOMPARE(report.fileInformation.fileMobId->bytes().toByteArray(), canonicalUid(11));
	}
}

// MARK: - Test fixture

class TestBellaOmf final : public QObject
{
	Q_OBJECT
private slots:
	void openedIdentity_data();
	void openedIdentity();
	void reportDefaults();
	void cancellationAndIo();
	void supportedOrdersAndVersions_data();
	void supportedOrdersAndVersions();
	void completeIdentities_data();
	void completeIdentities();
	void physicalOwnership_data();
	void physicalOwnership();
	void descriptorOrderDoesNotEraseClass();
	void mpegMediaDataIdentity();
	void masterSelection_data();
	void masterSelection();
	void projectSelection_data();
	void projectSelection();
	void originalBinText_data();
	void originalBinText();
	void optionalCategory_data();
	void optionalCategory();
	void audioAuthorities_data();
	void audioAuthorities();
	void pictureValues_data();
	void pictureValues();
	void timingAndNumbering_data();
	void timingAndNumbering();
	void physicalNumberingBoundary_data();
	void physicalNumberingBoundary();
	void localAndContainerDamage_data();
	void localAndContainerDamage();
	void nativeAndOmfIndependence_data();
	void nativeAndOmfIndependence();
	void returnedValuesOwnStorage();
};

// MARK: - Defaults and supported containers

void TestBellaOmf::reportDefaults()
{
	const Bella::OmfReport report;
	QVERIFY(report.path.isEmpty());
	QCOMPARE(report.nativeAudioReading, OmfReadResult::NotRead);
	QCOMPARE(report.omfReading, OmfReadResult::NotRead);
	QCOMPARE(report.sizeDateCheck, OmfCheckResult::NotChecked);
	QCOMPARE(report.nativeAudioInformation.kind, Bella::OmfKind::Unknown);
	QVERIFY(!report.nativeAudioInformation.sampleFrameCount);
	QVERIFY(!report.nativeAudioInformation.samplingRate);
	QVERIFY(!report.nativeAudioInformation.bitDepth);
	QCOMPARE(report.nativeAudioInformation.encoding, Bella::OmfAudioEncoding::Unknown);
	const auto &file = report.fileInformation;
	QVERIFY(!file.fileMobId);
	QVERIFY(file.project.isEmpty());
	QCOMPARE(file.projectSelection, Bella::OmfProjectSelection::Unavailable);
	QCOMPARE(file.projectKey, Bella::OmfProjectKey::None);
	QCOMPARE(file.kind, Bella::OmfKind::Unknown);
	QVERIFY(!file.descriptorClass);
	QVERIFY(!file.length);
	QVERIFY(!file.sampleRate);
	QVERIFY(!file.audioEditRate);
	QCOMPARE(file.durationClockSource, Bella::OmfDurationClockSource::Unavailable);
	QCOMPARE(file.numbering, Bella::OmfNumbering::Unavailable);
	QVERIFY(!file.audioFrequency);
	QVERIFY(!file.resolution);
	QVERIFY(!file.bitDepth.bits);
	QVERIFY(!file.bitDepth.rawComponentWidth);
	QCOMPARE(file.bitDepth.representation, Bella::OmfDepthRepresentation::Unknown);
	QCOMPARE(file.compression.codingState, Bella::OmfCodingState::Unavailable);
	QVERIFY(!file.compression.legacyCompression);
	QVERIFY(!file.compression.resolutionId);
	QVERIFY(!file.compression.namingGeometry);
	QVERIFY(!file.compression.frameLayout);
	QVERIFY(!file.compression.horizontalSubsampling);
	QVERIFY(!file.compression.verticalSubsampling);
	QCOMPARE(file.compression.audioEncoding, Bella::OmfAudioEncoding::Unknown);
	QVERIFY(!file.compression.alphaDepth);
	const auto &clip = report.associatedClipInformation;
	QVERIFY(!clip.masterMobId);
	QVERIFY(clip.clipName.isEmpty());
	QVERIFY(clip.originalBin.isEmpty());
	QCOMPARE(clip.type, Bella::OmfType::Unknown);
	QCOMPARE(clip.precomputeCategory, Bella::OmfPrecomputeCategory::Unknown);
	QVERIFY(report.problemNotes.isEmpty());
}

void TestBellaOmf::cancellationAndIo()
{
	const QString path = QStringLiteral("/nonexistent/Bella-authored-no-file.omf");
	const std::atomic_bool cancel{true};
	const auto cancelled = Bella::parseOmf(path, cancel, true);
	QCOMPARE(cancelled.path, path);
	QCOMPARE(cancelled.nativeAudioReading, OmfReadResult::Cancelled);
	QCOMPARE(cancelled.omfReading, OmfReadResult::Cancelled);
	QCOMPARE(cancelled.sizeDateCheck, OmfCheckResult::NotChecked);
	QVERIFY(!cancelled.fileInformation.fileMobId);
	const std::atomic_bool go{false};
	const auto missing = Bella::parseOmf(path, go, false);
	QCOMPARE(missing.path, path);
	QCOMPARE(missing.nativeAudioReading, OmfReadResult::IoError);
	QCOMPARE(missing.omfReading, OmfReadResult::IoError);
}

void TestBellaOmf::supportedOrdersAndVersions_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("version");
	for (const bool big : {false, true})
		for (int version = 0; version != 6; ++version)
			QTest::newRow(qPrintable(QStringLiteral("%1-version-%2").arg(big ? "MM" : "II").arg(version))) << big << version;
}
void TestBellaOmf::supportedOrdersAndVersions()
{
	QFETCH(bool, big);
	QFETCH(int, version);
	auto writer = specimen(big);
	const std::array<QByteArray, 6> versions{{QByteArray::fromHex("0100"), QByteArray::fromHex("0001"), QByteArray::fromHex("0107"), QByteArray::fromHex("0200"), QByteArray::fromHex("0000"), QByteArray::fromHex("0100")}};
	put(writer, 1, "OMFI:Version", "omfi:VersionType", versions[std::size_t(version)], true);
	const auto capture = writer.build();
	const auto report = parse(version == 5 ? TestMediaEngineBento::withBento2Version(capture) : capture);
	QCOMPARE(report.nativeAudioReading, OmfReadResult::NotPresent);
	if (version > 1)
	{
		QCOMPARE(report.omfReading, OmfReadResult::Unsupported);
		QVERIFY(!report.fileInformation.fileMobId);
		QVERIFY(!report.associatedClipInformation.masterMobId);
		return;
	}
	QCOMPARE(report.omfReading, OmfReadResult::Completed);
	QCOMPARE(report.sizeDateCheck, OmfCheckResult::Verified);
	expectedFileId(report);
	const auto &file = report.fileInformation;
	QCOMPARE(file.kind, Bella::OmfKind::Video);
	QVERIFY(file.descriptorClass);
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(file.descriptorClass->data()), 4), QByteArray("CDCI"));
	QCOMPARE(file.length, std::optional<qint64>(250));
	QVERIFY(file.sampleRate);
	QCOMPARE(file.sampleRate->words.numerator, 25);
	QCOMPARE(file.sampleRate->words.denominator, 1);
	QCOMPARE(file.sampleRate->encoding, Bella::OmfRateEncoding::ExactEditRate);
	QCOMPARE(file.durationClockSource, Bella::OmfDurationClockSource::DescriptorSampleRate);
	QCOMPARE(file.numbering, Bella::OmfNumbering::NotRequired);
	QVERIFY(file.resolution);
	QCOMPARE(file.resolution->dimensions.width, 1920);
	QCOMPARE(file.resolution->dimensions.height, 1080);
	QCOMPARE(file.bitDepth.bits, std::optional<quint32>(10));
	QCOMPARE(file.bitDepth.representation, Bella::OmfDepthRepresentation::Integer);
	const auto &clip = report.associatedClipInformation;
	QVERIFY(clip.masterMobId);
	QCOMPARE(clip.masterMobId->bytes().toByteArray(), canonicalUid(22));
	QCOMPARE(clip.clipName, QString::fromUtf8("  Master ß  "));
	QCOMPARE(clip.type, Bella::OmfType::Media);
	QCOMPARE(clip.precomputeCategory, Bella::OmfPrecomputeCategory::Unknown);
}

// MARK: - Physical and master ownership

void TestBellaOmf::physicalOwnership_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 12; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-physical-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::physicalOwnership()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	for (const auto &index : {"OMFI:SourceMobs", "OMFI:CompositionMobs", "OMFI:MediaData"})
		erase(writer, 1, index);
	if (variation == 1)
		members(writer, {100, 300});
	if (variation == 2)
		put(writer, 600, "OMFI:TIFF:MobID", "omfi:UID", uid(99, big));
	if (variation == 3)
		put(writer, 600, "OMFI:ObjID", "omfi:ObjectTag", "ATTR", true);
	if (variation == 4)
		members(writer, {100, 300, 600, 9999});
	if (variation == 5)
		put(writer, 100, "OMFI:MOBJ:MobID", "omfi:String", uid(11, big));
	if (variation == 6)
	{
		put(writer, 100, "OMFI:MOBJ:MobID", "omfi:UID", QByteArray(12, '\0'));
		put(writer, 600, "OMFI:TIFF:MobID", "omfi:UID", QByteArray(12, '\0'));
	}
	if (variation == 7)
		members(writer, {300, 600});
	if (variation == 8)
		writer.add(100, "OMFI:MOBJ:MobID", "omfi:UID", uid(11, big));
	if (variation == 9)
		writer.add(100, "OMFI:MOBJ:MobID", "omfi:UID", uid(12, big));
	if (variation == 10)
		reference(writer, 100, "OMFI:MOBJ:PhysicalMedia", 600); // MediaData is not a descriptor.
	if (variation == 11)
		writer.mobIndex(1, "OMFI:MediaData", {{600, uid(99, big)}}); // Same width, contradictory catalogue UID.
	const auto report = parse(writer);
	if (variation == 0 || variation == 8)
	{
		QCOMPARE(report.omfReading, OmfReadResult::Completed);
		expectedFileId(report);
		QVERIFY(report.associatedClipInformation.masterMobId);
	}
	else
	{
		QVERIFY(!report.fileInformation.fileMobId);
		QVERIFY(!report.associatedClipInformation.masterMobId);
		QCOMPARE(report.fileInformation.kind, Bella::OmfKind::Unknown);
	}
	QCOMPARE(report.nativeAudioReading, OmfReadResult::NotPresent);
}

void TestBellaOmf::descriptorOrderDoesNotEraseClass()
{
	auto writer = specimen();
	put(writer, 200, "OMFI:ByteOrder", "omfi:Short", "ZZ", true);
	const auto report = parse(writer);
	QCOMPARE(report.omfReading, OmfReadResult::Completed);
	expectedFileId(report);
	const auto &file = report.fileInformation;
	QCOMPARE(file.kind, Bella::OmfKind::Video);
	QVERIFY(file.descriptorClass);
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(file.descriptorClass->data()), 4), QByteArray("CDCI"));
	QVERIFY(!file.length);
	QVERIFY(!file.sampleRate);
	QVERIFY(!file.bitDepth.bits);
	QVERIFY(!file.resolution);
	QCOMPARE(file.durationClockSource, Bella::OmfDurationClockSource::Unavailable);
	QCOMPARE(file.numbering, Bella::OmfNumbering::Unavailable);
	QVERIFY(report.associatedClipInformation.masterMobId);
}

void TestBellaOmf::mpegMediaDataIdentity()
{
	// Authored control for the genuine MPEG50 slate's MPEG/MDAT ownership
	// representation. This does not contain or decode that slate's picture data.
	auto writer = specimen(false, "MPGI");
	put(writer, 600, "OMFI:ObjID", "omfi:ObjectTag", "MPEG", true);
	erase(writer, 600, "OMFI:TIFF:MobID");
	put(writer, 600, "OMFI:MDAT:MobID", "omfi:UID", uid(11, false));
	const auto report = parse(writer);
	QCOMPARE(report.omfReading, OmfReadResult::Completed);
	expectedFileId(report);
	QCOMPARE(report.fileInformation.kind, Bella::OmfKind::Video);
	QVERIFY(report.fileInformation.descriptorClass);
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(report.fileInformation.descriptorClass->data()), 4), QByteArray("MPGI"));
	QCOMPARE(report.fileInformation.length, std::optional<qint64>(250));
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.type, Bella::OmfType::Media);
}

void TestBellaOmf::masterSelection_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 9; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-master-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::masterSelection()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	erase(writer, 1, "OMFI:CompositionMobs");
	if (variation == 0)
		erase(writer, 300, "OMFI:CPNT:Name");
	if (variation == 1)
		scalar(writer, 300, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", qint32(0));
	if (variation == 2)
		reference(writer, 300, "OMFI:MOBJ:PhysicalMedia", 200);
	if (variation == 3)
		put(writer, 320, "OMFI:SCLP:SourceID", "omfi:UID", uid(99, big));
	if (variation == 4)
		put(writer, 320, "OMFI:SCLP:SourceID", "omfi:UID", QByteArray(11, '\0'));
	if (variation == 5)
		reference(writer, 310, "OMFI:TRAK:TrackComponent", 1100);
	if (variation == 6)
	{
		object(writer, 700, "MOBJ");
		put(writer, 700, "OMFI:MOBJ:MobID", "omfi:UID", uid(23, big));
		scalar(writer, 700, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", qint32(7));
		references(writer, 700, "OMFI:TRKG:Tracks", {710});
		track(writer, 710, 720);
		sourceClip(writer, 720, uid(11, big));
		members(writer, {100, 300, 600, 700});
	}
	if (variation == 7)
	{
		// A second exact link to the same master is not a second master.
		track(writer, 311, 321, 2);
		sourceClip(writer, 321, uid(11, big));
		references(writer, 300, "OMFI:TRKG:Tracks", {310, 311});
	}
	if (variation == 8)
	{
		object(writer, 700, "MOBJ");
		put(writer, 700, "OMFI:MOBJ:MobID", "omfi:UID", uid(22, big));
		scalar(writer, 700, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", qint32(7));
		text(writer, 700, "OMFI:CPNT:Name", "Inactive lookalike");
	}
	const auto report = parse(writer);
	expectedFileId(report);
	QCOMPARE(report.fileInformation.length, std::optional<qint64>(250));
	const auto &clip = report.associatedClipInformation;
	if (variation == 0 || variation == 7 || variation == 8)
	{
		QVERIFY(clip.masterMobId);
		QCOMPARE(clip.masterMobId->bytes().toByteArray(), canonicalUid(22));
		QCOMPARE(clip.type, Bella::OmfType::Media);
		if (variation == 0)
			QVERIFY(clip.clipName.isEmpty());
		else
			QCOMPARE(clip.clipName, QString::fromUtf8("  Master ß  "));
	}
	else
	{
		QVERIFY(!clip.masterMobId);
		QVERIFY(clip.clipName.isEmpty());
		QCOMPARE(clip.type, Bella::OmfType::Unknown);
	}
}

// MARK: - Project and bin metadata

void TestBellaOmf::projectSelection_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 13; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-project-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::projectSelection()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	QString expected = QStringLiteral("  Project  ");
	auto source = Bella::OmfProjectSelection::OwnFile;
	auto key = Bella::OmfProjectKey::Pj;
	if (variation == 0)
		attributes(writer, 100, 1000, {{"_PJ", 2, "  Project  "}, {"PROJNAME", 2, "Lower"}});
	if (variation == 1 || variation == 3)
	{
		attributes(writer, 100, 1000, {{"_PJ", 2, variation == 3 ? QByteArray(" \t ") : QByteArray{}}, {"PROJNAME", 2, "  Project  "}});
		key = Bella::OmfProjectKey::ProjName;
	}
	if (variation == 2)
	{
		attributes(writer, 100, 1000, {{"_USER", 3, {}, 1100}});
		attributes(writer, 0, 1100, {{"_PJ", 2, "  Project  "}});
		key = Bella::OmfProjectKey::UserPj;
	}
	if (variation == 4)
	{
		attributes(writer, 100, 1000, {{"_PJ", 2, "First"}, {"_PJ", 2, "Second"}, {"PROJNAME", 2, "Lower"}});
		expected.clear();
		source = Bella::OmfProjectSelection::Conflict;
		key = Bella::OmfProjectKey::None;
	}
	if (variation == 5)
		attributes(writer, 100, 1000, {{"_PJ", 2, "  Project  "}, {"_PJ", 2, "  Project  "}});
	if (variation == 6)
	{
		expected = QString::fromUtf8("  Unicode 漢字  ");
		attributes(writer, 100, 1000, {{"_PJ", 4, QByteArray(2, '\0') + expected.toUtf8()}});
	}
	if (variation == 7)
	{
		attributes(writer, 100, 1000, {{"_PJ", 4, QByteArray::fromHex("0100c328")}, {"PROJNAME", 2, "  Project  "}});
		key = Bella::OmfProjectKey::ProjName;
	}
	if (variation >= 8 && variation <= 10)
	{
		erase(writer, 1, "OMFI:SourceMobs");
		object(writer, 700, "MOBJ");
		put(writer, 700, "OMFI:MOBJ:MobID", "omfi:UID", uid(33, big));
		// Explicit original-source role; it is not an unindexed composition.
		reference(writer, 700, "OMFI:MOBJ:PhysicalMedia", 750);
		object(writer, 750, "MDES");
		references(writer, 700, "OMFI:TRKG:Tracks", {710});
		track(writer, 710, 720);
		component(writer, 720, "FILL", 1);
		put(writer, 120, "OMFI:SCLP:SourceID", "omfi:UID", uid(33, big));
		attributes(writer, 700, 2000, {{"_PJ", 2, "  Project  "}});
		members(writer, {100, 300, 600, 700});
		source = Bella::OmfProjectSelection::ImmediateSources;
		if (variation >= 9)
		{
			object(writer, 800, "MOBJ");
			put(writer, 800, "OMFI:MOBJ:MobID", "omfi:UID", uid(44, big));
			reference(writer, 800, "OMFI:MOBJ:PhysicalMedia", 850);
			object(writer, 850, "MDES");
			references(writer, 800, "OMFI:TRKG:Tracks", {810});
			track(writer, 810, 820);
			component(writer, 820, "FILL", 1);
			track(writer, 111, 121, 2);
			sourceClip(writer, 121, uid(44, big));
			references(writer, 100, "OMFI:TRKG:Tracks", {110, 111});
			attributes(writer, 800, 2100, {{"PROJNAME", 2, variation == 9 ? QByteArray("  Project  ") : QByteArray("Other")}});
			members(writer, {100, 300, 600, 700, 800});
			key = Bella::OmfProjectKey::Mixed;
			if (variation == 10)
			{
				expected.clear();
				source = Bella::OmfProjectSelection::Conflict;
				key = Bella::OmfProjectKey::None;
			}
		}
	}
	if (variation == 11 || variation == 12)
	{
		attributes(writer, variation == 11 ? 300 : 100, 1000, {{variation == 11 ? QByteArray("_PJ") : QByteArray("_PJ_EXTRA"), 2, "Ignored"}});
		expected.clear();
		source = Bella::OmfProjectSelection::Unavailable;
		key = Bella::OmfProjectKey::None;
	}
	const auto report = parse(writer);
	expectedFileId(report);
	QCOMPARE(report.fileInformation.project, expected);
	QCOMPARE(report.fileInformation.projectSelection, source);
	QCOMPARE(report.fileInformation.projectKey, key);
	QVERIFY(report.associatedClipInformation.masterMobId);
}

void TestBellaOmf::originalBinText_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 10; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-bin-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::originalBinText()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	const QString unicode = QString::fromUtf8("  Bïn 漢字  ");
	QString expected = unicode;
	originalBin(writer, unicode.toUtf8(), "Legacy");
	if (variation == 1)
	{
		text(writer, 1100, "OMFI:MCBR:MC:binNameUTF8", QByteArray::fromHex("c328"));
		expected = QStringLiteral("Legacy");
	}
	if (variation == 2)
	{
		text(writer, 1100, "OMFI:MCBR:MC:binNameUTF8", "");
		expected = QStringLiteral("Legacy");
	}
	if (variation == 3)
	{
		erase(writer, 1100, "OMFI:MCBR:MC:binNameUTF8");
		text(writer, 1100, "OMFI:MCBR:MC:binName", QByteArray::fromHex("c3a9"));
		expected = QString(QChar(0x221a)) + QChar(0x00a9); // Valid UTF-8 bytes remain legacy MacRoman here.
	}
	if (variation == 4)
	{
		erase(writer, 1100, "OMFI:MCBR:MC:binNameUTF8");
		erase(writer, 1100, "OMFI:MCBR:MC:binName");
		expected.clear();
	}
	if (variation == 5)
	{
		writer.add(1100, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", QByteArray("Conflict\0", 9));
		expected.clear();
	}
	if (variation == 6)
		writer.add(1100, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", unicode.toUtf8() + '\0');
	if (variation == 7)
	{
		put(writer, 1100, "OMFI:ObjID", "omfi:ObjectTag", "ATTR", true);
		expected.clear();
	}
	if (variation == 8)
	{
		scalar(writer, 1001, "OMFI:ATTB:Kind", "omfi:AttrKind", quint16(2));
		expected.clear();
	}
	if (variation == 9)
	{
		reference(writer, 1001, "OMFI:ATTB:ObjAttribute", 0);
		expected.clear();
	}
	const auto report = parse(writer);
	expectedFileId(report);
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.originalBin, expected);
	QCOMPARE(report.associatedClipInformation.type, Bella::OmfType::Media);
	QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("  Master ß  "));
}

void TestBellaOmf::optionalCategory_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 11; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-category-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::optionalCategory()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	scalar(writer, 300, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", qint32(variation == 10 ? 7 : 1));
	auto expected = Bella::OmfPrecomputeCategory::RenderedEffects;
	if (variation == 0 || variation == 10)
		expected = Bella::OmfPrecomputeCategory::Unknown;
	if (variation == 2)
		attributes(writer, 300, 1000, {});
	if (variation == 3)
		attributes(writer, 300, 1000, {{"_IMPORTSETTING", 2, "A string is not an object"}});
	if (variation >= 4 && variation <= 8)
	{
		attributes(writer, 300, 1000, {{"_IMPORTSETTING", 3, {}, 0}}); // Null payload still records object presence.
		if (variation >= 5)
		{
			track(writer, 311, 321, 2);
			sourceClip(writer, 321, uid(99, big), variation == 8 ? 2 : 1);
			references(writer, 300, "OMFI:TRKG:Tracks", {310, 311});
			expected = variation == 8 ? Bella::OmfPrecomputeCategory::RenderedEffects : Bella::OmfPrecomputeCategory::TitlesAndMatteKeys;
		}
		if (variation == 6)
		{
			scalar(writer, 1001, "OMFI:ATTB:Kind", "omfi:AttrKind", quint16(99));
			expected = Bella::OmfPrecomputeCategory::Unknown;
		}
		if (variation == 7)
		{
			erase(writer, 1001, "OMFI:ATTB:Kind");
			expected = Bella::OmfPrecomputeCategory::Unknown;
		}
	}
	if (variation == 9)
	{
		attributes(writer, 300, 1000, {{"_USER", 3, {}, 1100}});
		attributes(writer, 0, 1100, {{"_IMPORTSETTING", 3, {}, 0}});
	}
	const auto report = parse(writer, variation != 0);
	expectedFileId(report);
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.type, variation == 10 ? Bella::OmfType::Media : Bella::OmfType::Precompute);
	QCOMPARE(report.associatedClipInformation.precomputeCategory, expected);
}

// MARK: - Audio and picture facts

void TestBellaOmf::audioAuthorities_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 15; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-audio-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::audioAuthorities()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	const QByteArray descriptor = variation == 0 || variation == 5 || variation == 6 || variation == 14 ? QByteArray("PCMA")
								  : variation == 1														? QByteArray("MPGA")
								  : variation == 3 || variation == 8 || variation == 13					? QByteArray("AIFD")
								  : variation == 4														? QByteArray("WAVE")
																										: QByteArray("WAVD");
	auto writer = audio(descriptor, big);
	if (variation == 5)
		erase(writer, 200, "OMFI:PCMA:AudioSamplingRate");
	if (variation == 6)
		put(writer, 200, "OMFI:PCMA:AudioSamplingRate", "omfi:String", number<qint32>(44100, big) + number<qint32>(1, big));
	if (variation == 7 || variation == 8)
		erase(writer, 200, "OMFI:MDFL:SampleRate");
	if (variation == 9)
		writer.add(200, "OMFI:MDFL:SampleRate", "omfi:ExactEditRate", number<qint32>(44100, big) + number<qint32>(1, big));
	if (variation == 10)
	{
		auto format = waveFormat(48000, 24);
		qToLittleEndian<quint16>(99, format.data());
		const auto body = chunk("fmt ", format);
		put(writer, 200, "OMFI:WAVD:Summary", "omfi:DataValue", "RIFF" + number(quint32(body.size() + 4)) + "WAVE" + body);
	}
	if (variation == 11)
		scalar(writer, 200, "OMFI:MDFL:Length", "omfi:Long", qint32(0));
	if (variation == 12)
	{
		// A descriptor Summary may include a data header without its samples.
		const auto body = chunk("fmt ", waveFormat(48000, 24)) + QByteArray("data") + number<quint32>(288000);
		put(writer, 200, "OMFI:WAVD:Summary", "omfi:DataValue", "RIFF" + number<quint32>(288000 + quint32(body.size()) + 4) + "WAVE" + body);
	}
	if (variation == 13)
		put(writer, 200, "OMFI:AIFD:Summary", "omfi:DataValue", aiffSummary(true, "ZZZZ"));
	if (variation == 14)
		rate(writer, 200, "OMFI:MDAU:AudioSamplingRate", 22050, 1);
	const auto report = parse(writer);
	expectedFileId(report);
	const auto &file = report.fileInformation;
	QCOMPARE(file.kind, Bella::OmfKind::Audio);
	QCOMPARE(file.length, std::optional<qint64>(variation == 11 ? 0 : 96000));
	QVERIFY(file.audioEditRate);
	QCOMPARE(file.audioEditRate->words.numerator, 25);
	QCOMPARE(file.audioEditRate->words.denominator, 1);
	QCOMPARE(file.numbering, Bella::OmfNumbering::NotRequired);
	if (variation == 5 || variation == 6)
		QVERIFY(!file.audioFrequency);
	else
	{
		QVERIFY(file.audioFrequency);
		if (descriptor == "PCMA" || descriptor == "MPGA")
		{
			const auto *frequency = std::get_if<Bella::OmfRateWords>(&*file.audioFrequency);
			QVERIFY(frequency);
			QCOMPARE(frequency->numerator, 44100);
			QCOMPARE(frequency->denominator, 1);
		}
		else if (descriptor == "AIFD")
		{
			const auto *frequency = std::get_if<std::array<quint8, 10>>(&*file.audioFrequency);
			QVERIFY(frequency);
			QCOMPARE(QByteArray(reinterpret_cast<const char *>(frequency->data()), 10), QByteArray::fromHex("400ebb80000000000000"));
		}
		else
		{
			const auto *frequency = std::get_if<quint32>(&*file.audioFrequency);
			QVERIFY(frequency);
			QCOMPARE(*frequency, quint32(48000));
		}
	}
	if (variation == 7 || variation == 8)
	{
		QVERIFY(!file.sampleRate);
		QCOMPARE(file.durationClockSource, Bella::OmfDurationClockSource::SummaryFrequency);
	}
	else if (variation == 9)
	{
		QVERIFY(!file.sampleRate);
		QCOMPARE(file.durationClockSource, Bella::OmfDurationClockSource::Unavailable);
	}
	else
	{
		QVERIFY(file.sampleRate);
		QCOMPARE(file.sampleRate->words.numerator, 48000);
		QCOMPARE(file.durationClockSource, Bella::OmfDurationClockSource::DescriptorSampleRate);
	}
	if (variation != 10 && variation != 13)
	{
		QCOMPARE(file.bitDepth.bits, std::optional<quint32>(24));
		QCOMPARE(file.bitDepth.representation, Bella::OmfDepthRepresentation::Integer);
	}
	if (descriptor == "WAVD" || descriptor == "WAVE" || descriptor == "AIFD")
		QCOMPARE(file.compression.audioEncoding, variation == 10 || variation == 13 ? Bella::OmfAudioEncoding::Unknown : Bella::OmfAudioEncoding::Pcm);
	QVERIFY(!file.resolution);
	QVERIFY(!file.bitDepth.rawComponentWidth);
}

void TestBellaOmf::pictureValues_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 13; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-picture-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::pictureValues()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big, variation >= 8 && variation <= 10 ? QByteArray("RGBA") : variation == 11 ? QByteArray("TIFD")
																										 : QByteArray("CDCI"));
	if (variation == 0 || variation == 1 || variation == 7)
	{
		scalar(writer, 200, "OMFI:DIDD:DisplayWidth", "omfi:Long", qint32(1900));
		scalar(writer, 200, "OMFI:DIDD:DisplayHeight", "omfi:Long", qint32(variation == 1 ? 541 : 500));
		scalar(writer, 200, "OMFI:DIDD:DisplayXOffset", "omfi:Long", qint32(20));
		scalar(writer, 200, "OMFI:DIDD:DisplayYOffset", "omfi:Long", qint32(variation == 1 ? 0 : 40));
		if (variation == 7)
			put(writer, 200, "OMFI:DIDD:SampledWidth", "omfi:Long", QByteArray(1, '\1'), true);
	}
	if (variation == 2)
		scalar(writer, 200, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", qint16(2));
	if (variation == 3)
		scalar(writer, 200, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", qint16(3));
	if (variation == 4)
		scalar(writer, 200, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", qint16(99));
	if (variation == 5)
		put(writer, 200, "OMFI:CDCI:ComponentWidth", "omfi:String", number<qint32>(10, big));
	if (variation == 6)
		erase(writer, 200, "OMFI:DIDD:StoredWidth");
	if (variation >= 8 && variation <= 10)
	{
		const auto roles = variation == 10 ? QByteArray::fromHex("4100") : QByteArray::fromHex("52474200");
		const auto widths = variation == 10 ? QByteArray::fromHex("0800") : variation == 9 ? QByteArray::fromHex("08100800")
																						   : QByteArray::fromHex("08080800");
		put(writer, 200, "OMFI:RGBA:PixelLayout", "omfi:VarLenBytes", roles);
		put(writer, 200, "OMFI:RGBA:PixelStructure", "omfi:VarLenBytes", widths);
		if (variation == 10)
			text(writer, 200, "OMFI:DIDD:Compression", "NONE");
	}
	const auto canonicalCoding = QByteArray::fromHex("060e2b340401010d0401020203020200");
	if (variation == 12)
		put(writer, 200, "OMFI:DIDD:EssenceCompression", "omfi:GUID",
			QByteArray::fromHex(big ? "0401020203020200060e2b340401010d" : "0202010402030002060e2b340401010d"));
	const auto report = parse(writer);
	expectedFileId(report);
	const auto &file = report.fileInformation;
	QCOMPARE(file.kind, Bella::OmfKind::Video);
	if (variation == 1 || variation == 4 || variation == 6)
		QVERIFY(!file.resolution);
	else
	{
		QVERIFY(file.resolution);
		QCOMPARE(file.resolution->dimensions.width, qint64(variation == 0 || variation == 7 ? 1900 : 1920));
		QCOMPARE(file.resolution->dimensions.height, qint64(variation == 0 || variation == 7 ? 1000 : variation == 2 ? 540
																													 : 1080));
		QCOMPARE(file.resolution->origin, variation == 0 || variation == 7 ? Bella::OmfRasterOrigin::Display : Bella::OmfRasterOrigin::Stored);
	}
	if (variation == 5 || variation == 9 || variation == 11)
		QVERIFY(!file.bitDepth.bits);
	else
		QCOMPARE(file.bitDepth.bits, std::optional<quint32>(variation >= 8 && variation <= 10 ? 8 : 10));
	if (variation == 10)
		QCOMPARE(file.compression.alphaDepth, std::optional<quint16>(8));
	else
		QVERIFY(!file.compression.alphaDepth);
	if (variation == 12)
	{
		QCOMPARE(file.compression.codingState, Bella::OmfCodingState::Present);
		QCOMPARE(QByteArray(reinterpret_cast<const char *>(file.compression.codingLabel.data()), 16), canonicalCoding);
	}
	else
		QCOMPARE(file.compression.codingState, Bella::OmfCodingState::Absent);
	QVERIFY(file.compression.legacyCompression);
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(file.compression.legacyCompression->data()), 4), variation == 10 ? QByteArray("NONE") : QByteArray("AVHD"));
	QCOMPARE(file.compression.resolutionId, std::optional<qint64>(1243));
}

// MARK: - Timing and numbering

void TestBellaOmf::timingAndNumbering_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 10; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-numbering-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::timingAndNumbering()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	const bool indirect = variation >= 6;
	rate(writer, 200, "OMFI:MDFL:SampleRate", 2997, 100);
	rate(writer, 100, "OMFI:CPNT:EditRate", 2997, 100);
	rate(writer, 120, "OMFI:CPNT:EditRate", 2997, 100);
	if (indirect)
	{
		erase(writer, 1, "OMFI:SourceMobs");
		members(writer, {100, 300, 600, 700});
		object(writer, 700, "MOBJ");
		put(writer, 700, "OMFI:MOBJ:MobID", "omfi:UID", uid(33, big));
		// Explicit original-source role; it is not an unindexed composition.
		reference(writer, 700, "OMFI:MOBJ:PhysicalMedia", 750);
		object(writer, 750, "MDES");
		rate(writer, 700, "OMFI:CPNT:EditRate", 2997, 100);
		put(writer, 120, "OMFI:SCLP:SourceID", "omfi:UID", uid(33, big));
		track(writer, 710, 720);
		component(writer, 720, "FILL", 1, 250, 2997, 100);
		references(writer, 700, "OMFI:TRKG:Tracks", {710, 711});
		track(writer, 711, 721);
		component(writer, 721, "TCCP", 3, 250, 2997, 100);
	}
	else if (variation != 0)
	{
		references(writer, 100, "OMFI:TRKG:Tracks", {110, 711});
		track(writer, 711, 721);
		component(writer, 721, "TCCP", 3, 250, 2997, 100);
	}
	if (variation != 0)
	{
		scalar(writer, 721, "OMFI:TCCP:Flags", "omfi:Long", quint32(variation == 2 ? 2 : 3));
		scalar(writer, 721, "OMFI:TCCP:FPS", "omfi:Short", qint16(variation == 5 ? 25 : 30));
		scalar(writer, 721, "OMFI:TCCP:StartTC", "omfi:Long", qint32(1000)); // Never added to physical Length.
	}
	if (variation == 3)
		erase(writer, 721, "OMFI:TCCP:Flags");
	if (variation == 4)
		put(writer, 721, "OMFI:TCCP:Flags", "omfi:Long", QByteArray(2, '\0'), true);
	if (variation == 7)
		scalar(writer, 120, "OMFI:SCLP:SourcePosition", "omfi:Long", qint32(200));
	if (variation == 8)
		rate(writer, 721, "OMFI:CPNT:EditRate", 25, 1);
	if (variation == 9)
	{
		rate(writer, 200, "OMFI:MDFL:SampleRate", 25, 1);
		reference(writer, 100, "OMFI:TRKG:Tracks", 9999); // Irrelevant to a supported NDF-only clock.
	}
	const auto report = parse(writer);
	expectedFileId(report);
	QCOMPARE(report.fileInformation.length, std::optional<qint64>(250));
	QVERIFY(report.fileInformation.sampleRate);
	QCOMPARE(report.fileInformation.sampleRate->words.numerator, variation == 9 ? 25 : 2997);
	QCOMPARE(report.fileInformation.sampleRate->words.denominator, variation == 9 ? 1 : 100);
	QCOMPARE(report.fileInformation.sampleRate->encoding, Bella::OmfRateEncoding::ExactEditRate);
	const auto expected = variation == 0 || variation == 3 || variation == 8 ? Bella::OmfNumbering::MissingNdfFallback
						  : variation == 1 || variation == 6				 ? Bella::OmfNumbering::RecordedDf
						  : variation == 2									 ? Bella::OmfNumbering::RecordedNdf
						  : variation == 9									 ? Bella::OmfNumbering::NotRequired
																			 : Bella::OmfNumbering::Unavailable;
	QCOMPARE(report.fileInformation.numbering, expected);
}

void TestBellaOmf::physicalNumberingBoundary_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("one physical frame versus 250-unit editorial span") << 0;
	QTest::newRow("applicable own NDF and source DF conflict") << 1;
	QTest::newRow("nonterminal immediate source does not prove TC absence") << 2;
	QTest::newRow("terminal immediate source proves applicable absence") << 3;
	QTest::newRow("own TC outside physical interval does not oppose applicable source DF") << 4;
}
void TestBellaOmf::physicalNumberingBoundary()
{
	QFETCH(int, variation);
	auto writer = specimen();
	rate(writer, 200, "OMFI:MDFL:SampleRate", 2997, 100);
	scalar(writer, 200, "OMFI:MDFL:Length", "omfi:Long", qint32(1));
	rate(writer, 100, "OMFI:CPNT:EditRate", 2997, 100);
	rate(writer, 120, "OMFI:CPNT:EditRate", 2997, 100);
	const auto addTimecode = [&](quint32 trackHandle, quint32 clipHandle, quint32 flags)
	{
		track(writer, trackHandle, clipHandle);
		component(writer, clipHandle, "TCCP", 3, 1, 2997, 100);
		scalar(writer, clipHandle, "OMFI:TCCP:Flags", "omfi:Long", flags);
		scalar(writer, clipHandle, "OMFI:TCCP:FPS", "omfi:Short", qint16(30));
		scalar(writer, clipHandle, "OMFI:TCCP:StartTC", "omfi:Long", qint32(0));
	};
	if (variation <= 1)
	{
		references(writer, 100, "OMFI:TRKG:Tracks", {110, 711});
		addTimecode(711, 721, variation == 0 ? 3 : 0);
	}
	if (variation >= 1)
	{
		erase(writer, 1, "OMFI:SourceMobs");
		members(writer, {100, 300, 600, 700});
		object(writer, 700, "MOBJ");
		put(writer, 700, "OMFI:MOBJ:MobID", "omfi:UID", uid(33, false));
		reference(writer, 700, "OMFI:MOBJ:PhysicalMedia", 750);
		object(writer, 750, "MDES");
		rate(writer, 700, "OMFI:CPNT:EditRate", 2997, 100);
		put(writer, 120, "OMFI:SCLP:SourceID", "omfi:UID", uid(33, false));
		track(writer, 710, 720);
		sourceClip(writer, 720, variation == 2 ? uid(44, false) : QByteArray(12, '\0'), 1, 250, 2997, 100);
		references(writer, 700, "OMFI:TRKG:Tracks", variation == 1 || variation == 4 ? QVector<quint32>{710, 712} : QVector<quint32>{710});
		if (variation == 1 || variation == 4)
			addTimecode(712, 722, 3);
	}
	if (variation == 4)
	{
		// Its recorded NDF component occupies [10,11), outside the physical
		// interval [0,1). The FILL prefix does not record a contrary mode.
		references(writer, 100, "OMFI:TRKG:Tracks", {110, 711});
		track(writer, 711, 730);
		component(writer, 730, "SEQU", 3, 11, 2997, 100);
		component(writer, 731, "FILL", 3, 10, 2997, 100);
		component(writer, 732, "TCCP", 3, 1, 2997, 100);
		references(writer, 730, "OMFI:SEQU:Sequence", {731, 732});
		scalar(writer, 732, "OMFI:TCCP:Flags", "omfi:Long", quint32(0));
		scalar(writer, 732, "OMFI:TCCP:FPS", "omfi:Short", qint16(30));
		scalar(writer, 732, "OMFI:TCCP:StartTC", "omfi:Long", qint32(0));
	}
	const auto report = parse(writer);
	QCOMPARE(report.omfReading, OmfReadResult::Completed);
	expectedFileId(report);
	QCOMPARE(report.fileInformation.length, std::optional<qint64>(1));
	QVERIFY(report.fileInformation.sampleRate);
	QCOMPARE(report.fileInformation.sampleRate->words.numerator, 2997);
	QCOMPARE(report.fileInformation.sampleRate->words.denominator, 100);
	const auto expected = variation == 0 || variation == 4 ? Bella::OmfNumbering::RecordedDf
						  : variation == 3				   ? Bella::OmfNumbering::MissingNdfFallback
														   : Bella::OmfNumbering::Unavailable;
	QCOMPARE(report.fileInformation.numbering, expected);
	QCOMPARE(report.fileInformation.kind, Bella::OmfKind::Video);
}

// MARK: - Damage boundaries and native independence

void TestBellaOmf::localAndContainerDamage_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 12; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-damage-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::localAndContainerDamage()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	if (variation == 2)
	{
		put(writer, 200, "OMFI:ByteOrder", "omfi:Short", big ? QByteArray("MM") : QByteArray("II"), true);
		writer.add(200, "Private:Optional", "PrivateType", "unneeded payload");
		// An unresolvable optional definition cannot establish coding absence.
		const auto id = writer.properties.take("Private:Optional");
		writer.entries.append({id, 24, 21, QByteArray("Private:Optional")}); // No NUL terminator.
	}
	if (variation == 3)
		put(writer, 200, "OMFI:MDFL:SampleRate", "omfi:String", QByteArray(8, '\0'));
	if (variation == 4)
		put(writer, 200, "OMFI:MDFL:Length", "omfi:Long", QByteArray(2, '\0'), true);
	if (variation == 5)
		scalar(writer, 200, "OMFI:MDFL:Length", "omfi:Length64", qint64(250));
	if (variation == 6)
		scalar(writer, 200, "OMFI:MDFL:Length", "omfi:Length32", qint32(250));
	if (variation == 7)
		scalar(writer, 200, "OMFI:MDFL:Length", "omfi:Long", qint32(-1));
	if (variation == 9)
		put(writer, 1, "OMFI:ObjectSpine", "omfi:ObjRefArray", QByteArray::fromHex("010001"));
	if (variation == 10)
	{
		erase(writer, 300, "OMFI:CPNT:Name");
		writer.add(300, "OMFI:CPNT:Name", "omfi:String", "Hel", false, true);
		writer.add(300, "OMFI:CPNT:Name", "omfi:String", QByteArray("lo\0", 3));
	}
	if (variation == 11)
		writer.add(200, "Private:Irrelevant", "omfi:VarLenBytes", "unneeded");
	auto bytes = writer.build();
	const auto toc = qFromLittleEndian<quint32>(bytes.constData() + bytes.size() - 8);
	if (variation == 0 || variation == 1 || variation == 11)
	{
		const QByteArray wanted = variation == 0 ? QByteArray("OMFI:CPNT:Name") : variation == 1 ? QByteArray("OMFI:MDFL:SampleRate")
																								 : QByteArray("Private:Irrelevant");
		const quint32 owner = variation == 0 ? 300 : 200;
		const auto property = writer.properties.value(wanted);
		for (qsizetype n = 0; n < writer.entries.size(); ++n)
			if (writer.entries[n].object == owner && writer.entries[n].property == property)
				qToLittleEndian<quint32>(quint32(bytes.size() + 100), bytes.data() + toc + n * 24 + 12);
	}
	if (variation == 8)
	{
		const auto size = qFromLittleEndian<quint32>(bytes.constData() + bytes.size() - 4);
		qToLittleEndian<quint32>(size - 1, bytes.data() + bytes.size() - 4); // Unsafe fixed-entry TOC framing.
	}
	const auto report = parse(bytes);
	if (variation == 8 || variation == 9)
	{
		QVERIFY(report.omfReading != OmfReadResult::Completed);
		QVERIFY(!report.fileInformation.fileMobId);
		QVERIFY(!report.associatedClipInformation.masterMobId);
		QCOMPARE(report.fileInformation.kind, Bella::OmfKind::Unknown);
		return;
	}
	QCOMPARE(report.omfReading, OmfReadResult::Completed);
	expectedFileId(report);
	QCOMPARE(report.fileInformation.kind, Bella::OmfKind::Video);
	QCOMPARE(report.fileInformation.bitDepth.bits, std::optional<quint32>(10));
	QVERIFY(report.associatedClipInformation.masterMobId);
	if (variation == 0)
		QVERIFY(report.associatedClipInformation.clipName.isEmpty());
	else
		QCOMPARE(report.associatedClipInformation.clipName, variation == 10 ? QStringLiteral("Hello") : QString::fromUtf8("  Master ß  "));
	if (variation >= 4 && variation <= 7)
		QVERIFY(!report.fileInformation.length);
	else
		QCOMPARE(report.fileInformation.length, std::optional<qint64>(250));
	if (variation == 1 || variation == 3)
		QVERIFY(!report.fileInformation.sampleRate);
	else
		QVERIFY(report.fileInformation.sampleRate);
	if (variation == 2)
		QCOMPARE(report.fileInformation.compression.codingState, Bella::OmfCodingState::Unavailable);
}

void TestBellaOmf::nativeAndOmfIndependence_data()
{
	QTest::addColumn<int>("variation");
	for (int n = 0; n != 7; ++n)
		QTest::newRow(qPrintable(QStringLiteral("native-omf-%1").arg(n))) << n;
}
void TestBellaOmf::nativeAndOmfIndependence()
{
	QFETCH(int, variation);
	auto writer = audio("WAVD", false);
	if (variation == 6)
		put(writer, 200, "OMFI:WAVD:Summary", "omfi:DataValue", waveSummary(44100, 24));
	QVector<Writer> graphs;
	if (variation != 0)
		graphs.append(writer);
	if (variation == 2)
		graphs.append(writer);
	auto bytes = embeddedWave(graphs, variation == 3);
	if (variation == 4)
	{
		// First omfi payload is after RIFF/fmt/data and its eight-byte header.
		const qsizetype graphStart = 60;
		const auto graphLength = qFromLittleEndian<quint32>(bytes.constData() + graphStart - 4);
		const auto label = graphStart + graphLength - 24;
		const auto size = qFromLittleEndian<quint32>(bytes.constData() + label + 20);
		qToLittleEndian<quint32>(size - 1, bytes.data() + label + 20);
	}
	if (variation == 5)
		qToLittleEndian<quint32>(0, bytes.data() + 24); // Isolated native frequency damage.
	const auto report = parse(bytes, true);
	if (variation != 5)
	{
		QCOMPARE(report.nativeAudioReading, OmfReadResult::Completed);
		QCOMPARE(report.nativeAudioInformation.kind, Bella::OmfKind::Audio);
		QCOMPARE(report.nativeAudioInformation.sampleFrameCount, std::optional<qint64>(4));
		QVERIFY(report.nativeAudioInformation.samplingRate);
		QCOMPARE(report.nativeAudioInformation.samplingRate->numerator, 48000);
		QCOMPARE(report.nativeAudioInformation.bitDepth, std::optional<quint16>(16));
		QCOMPARE(report.nativeAudioInformation.encoding, Bella::OmfAudioEncoding::Pcm);
	}
	else
		QVERIFY(!report.nativeAudioInformation.samplingRate);
	if (variation == 0)
	{
		QCOMPARE(report.omfReading, OmfReadResult::NotPresent);
		QVERIFY(!report.fileInformation.fileMobId);
	}
	else if (variation == 2 || variation == 4)
	{
		if (variation == 2)
			QCOMPARE(report.omfReading, OmfReadResult::Unsupported);
		else
			QVERIFY(report.omfReading != OmfReadResult::Completed);
		QVERIFY(!report.fileInformation.fileMobId);
		QVERIFY(!report.associatedClipInformation.masterMobId);
		QVERIFY(!report.problemNotes.isEmpty());
	}
	else
	{
		QCOMPARE(report.omfReading, OmfReadResult::Completed);
		expectedFileId(report);
		QCOMPARE(report.fileInformation.length, std::optional<qint64>(96000));
		QVERIFY(report.fileInformation.audioFrequency);
		const auto *frequency = std::get_if<quint32>(&*report.fileInformation.audioFrequency);
		QVERIFY(frequency);
		QCOMPARE(*frequency, quint32(variation == 6 ? 44100 : 48000));
		QCOMPARE(report.fileInformation.bitDepth.bits, std::optional<quint32>(24));
	}
	QCOMPARE(report.sizeDateCheck, OmfCheckResult::Verified);
}

// MARK: - Owned results and complete identities

void TestBellaOmf::returnedValuesOwnStorage()
{
	Bella::OmfReport report;
	QString path;
	{
		auto writer = specimen();
		attributes(writer, 100, 2000, {{"_PJ", 2, "Owned Project"}});
		originalBin(writer, QByteArray("Owned Bin"), QByteArray("Legacy"));
		QTemporaryFile file;
		QVERIFY(file.open());
		const auto bytes = writer.build();
		QCOMPARE(file.write(bytes), qint64(bytes.size()));
		QVERIFY(file.flush());
		path = file.fileName();
		const std::atomic_bool stop{false};
		report = Bella::parseOmf(path, stop, false);
	}
	QVERIFY(!QFile::exists(path));
	QCOMPARE(report.path, path);
	expectedFileId(report);
	QCOMPARE(report.fileInformation.project, QStringLiteral("Owned Project"));
	QCOMPARE(report.associatedClipInformation.originalBin, QStringLiteral("Owned Bin"));
	QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("  Master ß  "));
	const auto copied = report;
	report = {};
	QCOMPARE(copied.fileInformation.project, QStringLiteral("Owned Project"));
	QCOMPARE(copied.associatedClipInformation.masterMobId->bytes().toByteArray(), canonicalUid(22));
	for (const auto &note : copied.problemNotes)
		QVERIFY(!note.contains(path));
}

void TestBellaOmf::completeIdentities_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	for (const bool big : {false, true})
		for (int n = 0; n != 10; ++n)
			QTest::newRow(qPrintable(QStringLiteral("%1-full-id-%2").arg(big ? "MM" : "II").arg(n))) << big << n;
}
void TestBellaOmf::completeIdentities()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	const auto modernCanonical = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f443322116655887718191a1b1c1d1e1f");
	const auto modernWire = big ? QByteArray::fromHex("000102030405060708090a0b0c0d0e0f112233445566778818191a1b1c1d1e1f") : modernCanonical;
	auto masterWire = modernWire;
	masterWire[0] = char(0x9a);
	auto masterCanonical = modernCanonical;
	masterCanonical[0] = char(0x9a);
	auto fileWire = variation == 2 || variation == 5 || variation == 9 ? uid(11, big) : modernWire;
	auto selectedMaster = variation == 1 ? uid(22, big) : masterWire;
	if (variation == 4)
		fileWire = QByteArray(32, '\0');
	if (variation == 5)
		selectedMaster = QByteArray(32, '\0');
	if (variation == 8)
		fileWire = modernWire.first(16); // An InstanceUID is not a complete MobID.
	if (variation == 9)
		selectedMaster = masterWire.first(16);
	if (variation == 6)
	{
		fileWire = number<quint32>(7, big) + number<quint32>(0x11223344, big) + number<quint32>(11, big);
		selectedMaster = number<quint32>(7, big) + number<quint32>(0x11223344, big) + number<quint32>(22, big);
	}
	put(writer, 100, "OMFI:MOBJ:MobID", "omfi:UID", fileWire);
	put(writer, 600, "OMFI:TIFF:MobID", "omfi:UID", fileWire);
	put(writer, 320, "OMFI:SCLP:SourceID", "omfi:UID", fileWire);
	put(writer, 300, "OMFI:MOBJ:MobID", "omfi:UID", selectedMaster);
	// A 12-byte catalogue key is not expanded and substituted for a 32-byte
	// object's own identity. Its separately recorded reference supplies membership.
	if (variation == 6)
	{
		for (const auto &index : {"OMFI:SourceMobs", "OMFI:CompositionMobs", "OMFI:MediaData"})
			erase(writer, 1, index);
		writer.mobIndex(1, "OMFI:SourceMobs", {{100, fileWire}});
		writer.mobIndex(1, "OMFI:CompositionMobs", {{300, selectedMaster}});
		writer.mobIndex(1, "OMFI:MediaData", {{600, fileWire}});
	}
	if (variation == 3 || variation == 7)
	{
		auto other = fileWire;
		other[0] ^= char(0x40); // Same suffix, different complete identity.
		put(writer, variation == 3 ? 320 : 600, variation == 3 ? "OMFI:SCLP:SourceID" : "OMFI:TIFF:MobID", "omfi:UID", other);
	}
	const auto report = parse(writer);
	if (variation == 4 || variation == 7 || variation == 8)
	{
		QVERIFY(!report.fileInformation.fileMobId);
		QVERIFY(!report.associatedClipInformation.masterMobId);
		return;
	}
	QVERIFY(report.fileInformation.fileMobId);
	const auto expectedFile = variation == 6									   ? number<quint32>(7) + number<quint32>(0x11223344) + number<quint32>(11)
							  : variation == 2 || variation == 5 || variation == 9 ? canonicalUid(11)
																				   : modernCanonical;
	QCOMPARE(report.fileInformation.fileMobId->bytes().toByteArray(), expectedFile);
	if (variation == 3 || variation == 5 || variation == 9)
		QVERIFY(!report.associatedClipInformation.masterMobId);
	else
	{
		QVERIFY(report.associatedClipInformation.masterMobId);
		const auto expectedMaster = variation == 6	 ? number<quint32>(7) + number<quint32>(0x11223344) + number<quint32>(22)
									: variation == 1 ? canonicalUid(22)
													 : masterCanonical;
		QCOMPARE(report.associatedClipInformation.masterMobId->bytes().toByteArray(), expectedMaster);
	}
}

void TestBellaOmf::openedIdentity_data()
{
	QTest::addColumn<bool>("big");
	QTest::addColumn<int>("variation");
	const std::array<const char *, 14> names{
		"complete identity", "wrong full file identity", "two saved masters", "one missing saved master",
		"broken second master", "technical values not required", "unsupported OMF revision",
		"multiple embedded containers", "single embedded container", "unlinked master",
		"noncanonical expected spelling", "broken container", "descriptor not required", "unknown descriptor class"};
	for (const bool big : {false, true})
		for (int i = 0; i < int(names.size()); ++i)
			QTest::newRow(qPrintable(QStringLiteral("%1 %2").arg(big ? "MM" : "II", names[std::size_t(i)]))) << big << i;
}

void TestBellaOmf::openedIdentity()
{
	QFETCH(bool, big);
	QFETCH(int, variation);
	auto writer = specimen(big);
	QString expectedFile = OmfUid::toIdText(canonicalUid(11));
	QStringList masters{OmfUid::toIdText(canonicalUid(22))};
	const bool expected = variation == 0 || variation == 2 || variation == 5 || variation == 8 || variation >= 12;
	if (variation == 1)
	{
		auto different = canonicalUid(11);
		different[0] ^= char(0x40);
		expectedFile = OmfUid::toIdText(different);
	}
	if (variation >= 2 && variation <= 4)
	{
		erase(writer, 1, "OMFI:CompositionMobs");
		object(writer, 700, "MOBJ");
		put(writer, 700, "OMFI:MOBJ:MobID", "omfi:UID", uid(23, big));
		scalar(writer, 700, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", qint32(7));
		references(writer, 700, "OMFI:TRKG:Tracks", {710});
		track(writer, 710, 720);
		sourceClip(writer, 720, uid(11, big));
		members(writer, {100, 300, 600, 700});
		masters.append(OmfUid::toIdText(canonicalUid(23)));
		if (variation == 3)
			masters.append(OmfUid::toIdText(canonicalUid(24)));
		if (variation == 4)
			reference(writer, 710, "OMFI:TRAK:TrackComponent", 999);
	}
	if (variation == 5)
	{
		erase(writer, 200, "OMFI:MDFL:SampleRate");
		erase(writer, 200, "OMFI:MDFL:Length");
	}
	if (variation == 6)
		put(writer, 1, "OMFI:Version", "omfi:VersionType", QByteArray::fromHex("0002"), true);
	if (variation == 9)
		put(writer, 320, "OMFI:SCLP:SourceID", "omfi:UID", uid(99, big));
	if (variation == 10)
		expectedFile.remove(QLatin1Char('.'));
	if (variation == 12 || variation == 13)
	{
		masters.clear();
		if (variation == 12)
			erase(writer, 100, "OMFI:MOBJ:PhysicalMedia");
		else
			put(writer, 200, "OMFI:ObjID", "omfi:ObjectTag", "????", true);
	}
	auto content = variation == 7 ? embeddedWave({writer, writer}) : variation == 8 ? embeddedWave({writer})
																					: writer.build();
	if (variation == 11)
		content.chop(1);
	QTemporaryFile source;
	QVERIFY(source.open());
	QCOMPARE(source.write(content), qint64(content.size()));
	QVERIFY(source.flush());
	QVERIFY(source.seek(7));
	const auto handle = source.handle();
	QCOMPARE(Bella::omfIdentityMatches(source, expectedFile, masters), expected);
	QVERIFY(source.isOpen());
	QCOMPARE(source.handle(), handle);
	QCOMPARE(source.pos(), qint64(7));
	if (variation == 2)
	{
		QVERIFY(!parse(writer).associatedClipInformation.masterMobId);
		QVERIFY(Bella::omfIdentityMatches(source, {}, masters));
	}
	source.close();
	QVERIFY(!Bella::omfIdentityMatches(source, expectedFile, masters));
}

QTEST_GUILESS_MAIN(TestBellaOmf)
#include "tst_bella_omf.moc"
