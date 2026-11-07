// Authored MXF wire fixtures: KLV, Primer and local sets, not production reader objects.
// Registered property ULs are literal source vocabulary; expected facts are authored here.
#include "bella/mxfparser.h"
#include "omfuid.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QVector>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <atomic>
#include <initializer_list>
#include <iterator>
#include <limits>

namespace
{
	QByteArray hex(const char *value)
	{
		return QByteArray::fromHex(value);
	}
	template <typename T> QByteArray be(T value)
	{
		QByteArray result(sizeof(T), '\0');
		qToBigEndian(value, result.data());
		return result;
	}
	template <typename T> QByteArray le(T value)
	{
		QByteArray result(sizeof(T), '\0');
		qToLittleEndian(value, result.data());
		return result;
	}
	QByteArray length(quint64 size)
	{
		if (size < 128)
			return QByteArray(1, char(size));
		QByteArray digits;
		do
		{
			digits.prepend(char(size & 255));
			size >>= 8;
		} while (size);
		return QByteArray(1, char(0x80 | digits.size())) + digits;
	}
	QByteArray packet(const QByteArray &key, const QByteArray &value)
	{
		return key + length(quint64(value.size())) + value;
	}
	QByteArray text(const QString &value, bool terminator = true)
	{
		QByteArray result;
		for (const QChar c : value)
			result += be(c.unicode());
		return terminator ? result + QByteArray(2, '\0') : result;
	}
	QByteArray uid(quint32 id)
	{
		return be<quint64>(0x123456789abcdef0ULL) + be<quint64>(id);
	}
	QByteArray packageId(quint32 id)
	{
		return hex("060a2b340101010101010f0013000000") + be(id) + be<quint16>(0xa1b2) + be<quint16>(0xc3d4) +
		       hex("060e2b347f7f2a80");
	}
	QByteArray expectedId(quint32 id)
	{
		// Explicit expected PMR/MDB material-field order, independent of the normalizer.
		return hex("060a2b340101010101010f0013000000") + le(id) + le<quint16>(0xa1b2) + le<quint16>(0xc3d4) +
		       hex("060e2b347f7f2a80");
	}
	QByteArray rational(qint32 numerator, qint32 denominator = 1)
	{
		return be(numerator) + be(denominator);
	}
	QByteArray references(std::initializer_list<quint32> ids)
	{
		QByteArray result = be<quint32>(quint32(ids.size())) + be<quint32>(16);
		for (const auto id : ids)
			result += uid(id);
		return result;
	}
	QByteArray classKey(quint8 code)
	{
		auto key = hex("060e2b34025301010d01010101010000");
		key[14] = char(code);
		return key;
	}
	const QByteArray picture = hex("060e2b34040101010103020201000000");
	const QByteArray sound = hex("060e2b34040101010103020202000000");
	const QByteArray timecode = hex("060e2b34040101010103020101000000");
	const QByteArray ordinaryContainer = hex("060e2b340401010a0d01030102110100");
	const QByteArray alphaContainer = hex("060e2b34040101010e04030102080100");
	const QByteArray coding = hex("060e2b340401010a0401020271020000");
	constexpr quint32 fileId = 0x01020304;
	constexpr quint32 masterId = 0x11223344;
	struct Definition
	{
		const char *name;
		const char *ul;
	};
	const Definition definitions[] = {
	    {"InterchangeObject.InstanceUID", "060e2b34010101010101150200000000"},
	    {"Preface.ContentStorage", "060e2b34010101020601010402010000"},
	    {"Preface.EssenceFileMobID", "8b2f680dff611381abf1b7718efd4802"},
	    {"Preface.OperationalPattern", "060e2b34010101050102020300000000"},
	    {"Preface.EssenceContainers", "060e2b34010101050102021002010000"},
	    {"Preface.ProjectName", "a5fb7b25f61594b962fc3717492d42bf"},
	    {"Preface.ProjectEditRate", "8c70a718467ae486f36546b1387c4ee9"},
	    {"ContentStorage.Packages", "060e2b34010101020601010405010000"},
	    {"ContentStorage.EssenceContainerData", "060e2b34010101020601010405020000"},
	    {"EssenceContainerData.LinkedPackageUID", "060e2b34010101020601010601000000"},
	    {"EssenceContainerData.BodySID", "060e2b34010101040103040400000000"},
	    {"GenericPackage.PackageUID", "060e2b34010101010101151000000000"},
	    {"GenericPackage.Name", "060e2b34010101010103030201000000"},
	    {"GenericPackage.Tracks", "060e2b34010101020601010406050000"},
	    {"GenericPackage.AppCode", "a022006094eb75cb96c469924f6211d3"},
	    {"GenericPackage.MobAttributeList", "a01c0004ac969f506095818347b111d4"},
	    {"GenericTrack.TrackID", "060e2b34010101020107010100000000"},
	    {"GenericTrack.Sequence", "060e2b34010101020601010402040000"},
	    {"Track.EditRate", "060e2b34010101020530040500000000"},
	    {"Track.Origin", "060e2b34010101020702010301030000"},
	    {"StructuralComponent.DataDefinition", "060e2b34010101020407010000000000"},
	    {"StructuralComponent.Duration", "060e2b34010101020702020101030000"},
	    {"Sequence.StructuralComponents", "060e2b34010101020601010406090000"},
	    {"SourceClip.StartPosition", "060e2b34010101020702010301040000"},
	    {"SourceClip.SourcePackageID", "060e2b34010101020601010301000000"},
	    {"SourceClip.SourceTrackID", "060e2b34010101020601010302000000"},
	    {"SourcePackage.Descriptor", "060e2b34010101020601010402030000"},
	    {"FileDescriptor.LinkedTrackID", "060e2b34010101050601010305000000"},
	    {"FileDescriptor.SampleRate", "060e2b34010101010406010100000000"},
	    {"FileDescriptor.ContainerDuration", "060e2b34010101010406010200000000"},
	    {"FileDescriptor.Codec", "060e2b34010101020601010401030000"},
	    {"GenericPictureEssenceDescriptor.FrameLayout", "060e2b34010101010401030104000000"},
	    {"GenericPictureEssenceDescriptor.StoredWidth", "060e2b34010101010401050202000000"},
	    {"GenericPictureEssenceDescriptor.StoredHeight", "060e2b34010101010401050201000000"},
	    {"GenericPictureEssenceDescriptor.SampledWidth", "060e2b34010101010401050108000000"},
	    {"GenericPictureEssenceDescriptor.SampledHeight", "060e2b34010101010401050107000000"},
	    {"GenericPictureEssenceDescriptor.SampledXOffset", "060e2b34010101010401050109000000"},
	    {"GenericPictureEssenceDescriptor.SampledYOffset", "060e2b3401010101040105010a000000"},
	    {"GenericPictureEssenceDescriptor.DisplayWidth", "060e2b3401010101040105010c000000"},
	    {"GenericPictureEssenceDescriptor.DisplayHeight", "060e2b3401010101040105010b000000"},
	    {"GenericPictureEssenceDescriptor.DisplayXOffset", "060e2b3401010101040105010d000000"},
	    {"GenericPictureEssenceDescriptor.DisplayYOffset", "060e2b3401010101040105010e000000"},
	    {"GenericPictureEssenceDescriptor.PictureEssenceCoding", "060e2b34010101020401060100000000"},
	    {"GenericPictureEssenceDescriptor.ResolutionID", "a024006094eb75cbce2aca4d51ab11d3"},
	    {"CDCIEssenceDescriptor.ComponentDepth", "060e2b3401010102040105030a000000"},
	    {"CDCIEssenceDescriptor.HorizontalSubsampling", "060e2b34010101010401050105000000"},
	    {"CDCIEssenceDescriptor.VerticalSubsampling", "060e2b34010101020401050110000000"},
	    {"RGBAEssenceDescriptor.PixelLayout", "060e2b34010101020401050306000000"},
	    {"GenericSoundEssenceDescriptor.AudioSamplingRate", "060e2b34010101050402030101010000"},
	    {"GenericSoundEssenceDescriptor.QuantizationBits", "060e2b34010101040402030304000000"},
	    {"GenericSoundEssenceDescriptor.SoundEssenceCompression", "060e2b34010101020402040200000000"},
	    {"TaggedValue.Name", "060e2b34010101020302010209010000"},
	    {"TaggedValue.Value", "060e2b3401010102030201020a010000"},
	    {"TaggedValue.TaggedValueAttributeList", "a01c0004ac969f506095818547b111d4"},
	    {"TimecodeComponent.RoundedTimecodeBase", "060e2b34010101020404010102060000"},
	    {"TimecodeComponent.DropFrame", "060e2b34010101010404010105000000"},
	    {"TimecodeComponent.StartTimecode", "060e2b34010101020702010301050000"},
	    {"GenericPackage.UsageCode", "060e2b34010101070501010800000000"},
	};
	struct Field
	{
		QString name;
		QByteArray value;
	};
	struct WireSet
	{
		quint32 id;
		QByteArray key;
		QVector<Field> fields;
	};
	struct Fixture
	{
		QVector<WireSet> sets;
		quint16 tagBase = 0x8100;
		quint8 status = 4;
		quint16 major = 1, minor = 2;
		QByteArray pattern = hex("060e2b34040101020d01020110030000");
		QByteArray container = ordinaryContainer;
		quint32 primerStride = 18;
		bool berLocalLengths = false;
		qint64 headerSizeDelta = 0;
		QByteArray beforePrimer, extraHeader, suffix;
		QString omittedMapping;
		WireSet &at(quint32 id)
		{
			const auto found = std::find_if(sets.begin(), sets.end(), [id](const auto &set) { return set.id == id; });
			Q_ASSERT(found != sets.end());
			return *found;
		}
		void add(quint32 id, quint8 cls)
		{
			sets.append({id, classKey(cls), {{QStringLiteral("InterchangeObject.InstanceUID"), uid(id)}}});
		}
		void append(quint32 id, const char *name, const QByteArray &value)
		{
			at(id).fields.append({QString::fromLatin1(name), value});
		}
		void remove(quint32 id, const char *name)
		{
			auto &fields = at(id).fields;
			fields.removeIf([name](const auto &field) { return field.name == QLatin1String(name); });
		}
		void put(quint32 id, const char *name, const QByteArray &value)
		{
			remove(id, name);
			append(id, name, value);
		}
		quint16 tag(const QString &name) const
		{
			for (std::size_t i = 0; i < std::size(definitions); ++i)
				if (name == QLatin1String(definitions[i].name))
					return quint16(tagBase + i);
			Q_UNREACHABLE();
		}
		QByteArray bytes() const
		{
			QByteArray mappings;
			quint32 count = 0;
			for (const auto &definition : definitions)
				if (omittedMapping != QLatin1String(definition.name))
				{
					mappings += be(tag(QString::fromLatin1(definition.name))) + hex(definition.ul);
					++count;
				}
			QByteArray metadata =
			    packet(hex("060e2b34020501010d01020101050100"), be(count) + be(primerStride) + mappings);
			for (const auto &set : sets)
			{
				QByteArray value;
				for (const auto &field : set.fields)
				{
					Q_ASSERT(field.value.size() <= 65535);
					value += be(tag(field.name)) +
					         (berLocalLengths ? length(quint64(field.value.size()))
					                          : be<quint16>(quint16(field.value.size()))) +
					         field.value;
				}
				auto key = set.key;
				if (berLocalLengths)
					key[5] = char(0x13);
				metadata += packet(key, value);
			}
			metadata += extraHeader;
			auto key = hex("060e2b34020501010d01020101020400");
			key[14] = char(status);
			const auto extent = quint64(qint64(metadata.size()) + headerSizeDelta);
			const QByteArray pack = be(major) + be(minor) + be<quint32>(1) + be<quint64>(0) + be<quint64>(0) +
			                        be<quint64>(0) + be(extent) + be<quint64>(0) + be<quint32>(0) + be<quint64>(0) +
			                        be<quint32>(0) + pattern + be<quint32>(1) + be<quint32>(16) + container;
			return packet(key, pack) + beforePrimer + metadata + suffix;
		}
	};
	void track(Fixture &f, quint32 id, quint32 sequence, quint32 trackId, const QByteArray &rate)
	{
		f.add(id, 0x3b);
		f.append(id, "GenericTrack.TrackID", be(trackId));
		f.append(id, "GenericTrack.Sequence", uid(sequence));
		f.append(id, "Track.EditRate", rate);
		f.append(id, "Track.Origin", be<qint64>(0));
	}
	void sequence(Fixture &f, quint32 id, quint32 clip, const QByteArray &definition, qint64 duration)
	{
		f.add(id, 0x0f);
		f.append(id, "StructuralComponent.DataDefinition", definition);
		f.append(id, "StructuralComponent.Duration", be(duration));
		f.append(id, "Sequence.StructuralComponents", references({clip}));
	}
	void sourceClip(Fixture &f, quint32 id, const QByteArray &sourceId, quint32 trackId, const QByteArray &definition,
	                qint64 duration)
	{
		f.add(id, 0x11);
		f.append(id, "StructuralComponent.DataDefinition", definition);
		f.append(id, "StructuralComponent.Duration", be(duration));
		f.append(id, "SourceClip.StartPosition", be<qint64>(0));
		f.append(id, "SourceClip.SourcePackageID", sourceId);
		f.append(id, "SourceClip.SourceTrackID", be(trackId));
	}
	Fixture fixture(bool audio = false)
	{
		Fixture f;
		f.add(1, 0x2f);
		f.append(1, "Preface.ContentStorage", uid(2));
		f.append(1, "Preface.OperationalPattern", f.pattern);
		f.append(1, "Preface.EssenceContainers", be<quint32>(1) + be<quint32>(16) + f.container);
		f.append(1, "Preface.ProjectName", text(QString::fromUtf8("  Pröject 漢字  ")));
		f.append(1, "Preface.ProjectEditRate", rational(25));
		f.add(2, 0x18);
		f.append(2, "ContentStorage.Packages", references({4, 6}));
		f.append(2, "ContentStorage.EssenceContainerData", references({3}));
		f.add(3, 0x23);
		f.append(3, "EssenceContainerData.LinkedPackageUID", packageId(fileId));
		f.append(3, "EssenceContainerData.BodySID", be<quint32>(1));
		f.add(4, 0x37);
		f.append(4, "GenericPackage.PackageUID", packageId(fileId));
		f.append(4, "GenericPackage.Name", text(QStringLiteral("Physical source name is not Clip Name")));
		f.append(4, "GenericPackage.Tracks", references({10}));
		f.append(4, "SourcePackage.Descriptor", uid(5));
		f.add(5, audio ? 0x48 : 0x28);
		f.append(5, "FileDescriptor.LinkedTrackID", be<quint32>(3));
		f.append(5, "FileDescriptor.SampleRate", rational(audio ? 48000 : 25));
		f.append(5, "FileDescriptor.ContainerDuration", be<qint64>(audio ? 96000 : 250));
		if (audio)
		{
			f.append(5, "GenericSoundEssenceDescriptor.AudioSamplingRate", rational(48000));
			f.append(5, "GenericSoundEssenceDescriptor.QuantizationBits", be<quint32>(24));
		}
		else
		{
			f.append(5, "GenericPictureEssenceDescriptor.FrameLayout", QByteArray(1, '\0'));
			f.append(5, "GenericPictureEssenceDescriptor.StoredWidth", be<quint32>(1920));
			f.append(5, "GenericPictureEssenceDescriptor.StoredHeight", be<quint32>(1080));
			f.append(5, "GenericPictureEssenceDescriptor.PictureEssenceCoding", coding);
			f.append(5, "CDCIEssenceDescriptor.ComponentDepth", be<quint32>(10));
			f.append(5, "CDCIEssenceDescriptor.HorizontalSubsampling", be<quint32>(2));
			f.append(5, "CDCIEssenceDescriptor.VerticalSubsampling", be<quint32>(1));
		}
		f.add(6, 0x36);
		f.append(6, "GenericPackage.PackageUID", packageId(masterId));
		f.append(6, "GenericPackage.Name", text(QString::fromUtf8("Master — Δ")));
		f.append(6, "GenericPackage.Tracks", references({7}));
		track(f, 7, 8, 1, rational(25));
		sequence(f, 8, 9, audio ? sound : picture, audio ? 50 : 250);
		sourceClip(f, 9, packageId(fileId), 3, audio ? sound : picture, audio ? 50 : 250);
		track(f, 10, 11, 3, rational(audio ? 48000 : 25));
		sequence(f, 11, 12, audio ? sound : picture, audio ? 96000 : 250);
		sourceClip(f, 12, QByteArray(32, '\0'), 0, audio ? sound : picture, audio ? 96000 : 250);
		return f;
	}
	void taggedText(Fixture &f, quint32 owner, quint32 id, const QString &name, const QString &value)
	{
		f.add(id, 0x3f);
		f.append(id, "TaggedValue.Name", text(name));
		f.append(id, "TaggedValue.Value", hex("420110020000000000060e2b3401040101") + text(value));
		f.put(owner, "GenericPackage.MobAttributeList", references({id}));
	}
	void timecodeTrack(Fixture &f, quint32 first, qint64 duration, quint8 drop)
	{
		track(f, first, first + 1, 17, rational(30000, 1001));
		sequence(f, first + 1, first + 2, timecode, duration);
		f.add(first + 2, 0x14);
		f.append(first + 2, "StructuralComponent.DataDefinition", timecode);
		f.append(first + 2, "StructuralComponent.Duration", be(duration));
		f.append(first + 2, "TimecodeComponent.RoundedTimecodeBase", be<quint16>(30));
		f.append(first + 2, "TimecodeComponent.StartTimecode", be<qint64>(0));
		f.append(first + 2, "TimecodeComponent.DropFrame", QByteArray(1, char(drop)));
	}
	Bella::MxfReport parse(const QByteArray &bytes, bool details = false)
	{
		QTemporaryFile file;
		if (!file.open() || file.write(bytes) != bytes.size() || !file.flush())
		{
			QTest::qFail("Cannot write the authored MXF fixture", __FILE__, __LINE__);
			return {};
		}
		const std::atomic_bool stop{false};
		return Bella::parseMxf(file.fileName(), stop, details);
	}
	Bella::MxfReport parse(const Fixture &f, bool details = false)
	{
		return parse(f.bytes(), details);
	}
	QByteArray bytes(const Bella::MobId &value)
	{
		const auto view = value.bytes();
		return QByteArray(view.data(), view.size());
	}
} // namespace

class TestBellaMxf final : public QObject
{
	Q_OBJECT
  private slots:
	void openedIdentity_data();
	void openedIdentity();
	void defaults();
	void preCancelledDoesNotOpen();
	void missingFileIsIoError();
	void authoredVideo_data();
	void authoredVideo();
	void admission_data();
	void admission();
	void unsafeFraming_data();
	void unsafeFraming();
	void stopsAtSelectedHeader();
	void safelyBoundedUnknownMetadata();
	void lateMetadataBeyond64MiB();
	void alignmentFillPrecedesHeaderCount();
	void fileAndMasterOwnership_data();
	void fileAndMasterOwnership();
	void localDescriptorDamage_data();
	void localDescriptorDamage();
	void duplicateRates_data();
	void duplicateRates();
	void projectSelection_data();
	void projectSelection();
	void clipNameDamage_data();
	void clipNameDamage();
	void audioClocksStaySeparate();
	void extremeTimingKeepsSourceValues();
	void audioCodingQualification_data();
	void audioCodingQualification();
	void knownDescriptorKinds_data();
	void knownDescriptorKinds();
	void numbering_data();
	void numbering();
	void immediateSourceNumbering_data();
	void immediateSourceNumbering();
	void pictureGeometry_data();
	void pictureGeometry();
	void verifiedSingleFieldProxy_data();
	void verifiedSingleFieldProxy();
	void rgbaDepth_data();
	void rgbaDepth();
	void typeAndCategory_data();
	void typeAndCategory();
};

void TestBellaMxf::defaults()
{
	const Bella::MxfReport report;
	QCOMPARE(report.reading, Bella::MxfReadResult::NotRead);
	QCOMPARE(report.sizeDateCheck, Bella::MxfCheckResult::NotChecked);
	QVERIFY(!report.fileInformation.fileMobId);
	QVERIFY(!report.fileInformation.containerDuration);
	QVERIFY(!report.fileInformation.sampleRate);
	QVERIFY(!report.fileInformation.frameRateQualified);
	QCOMPARE(report.fileInformation.projectSelection, Bella::MxfProjectSelection::Unavailable);
	QCOMPARE(report.fileInformation.projectKey, Bella::MxfProjectKey::None);
	QCOMPARE(report.fileInformation.numbering, Bella::MxfNumbering::Unavailable);
	QCOMPARE(report.fileInformation.compression.codingState, Bella::MxfCodingState::Unavailable);
	QVERIFY(!report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.type, Bella::MxfType::Unknown);
	QVERIFY(report.problemNotes.isEmpty());
}
void TestBellaMxf::preCancelledDoesNotOpen()
{
	const std::atomic_bool stop{true};
	const auto report = Bella::parseMxf(QStringLiteral("/nonexistent/bella-pre-cancelled.mxf"), stop, false);
	QCOMPARE(report.reading, Bella::MxfReadResult::Cancelled);
	QVERIFY(!report.fileInformation.fileMobId);
	QVERIFY(!report.associatedClipInformation.masterMobId);
}
void TestBellaMxf::missingFileIsIoError()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const std::atomic_bool stop{false};
	const auto report = Bella::parseMxf(directory.filePath(QStringLiteral("missing.mxf")), stop, false);
	QCOMPARE(report.reading, Bella::MxfReadResult::IoError);
	QVERIFY(!report.fileInformation.fileMobId);
	QVERIFY(!report.problemNotes.isEmpty());
}
void TestBellaMxf::authoredVideo_data()
{
	QTest::addColumn<int>("tagBase");
	QTest::addColumn<bool>("berLocalLengths");
	QTest::newRow("fixed-length relocated tags") << 0x8100 << false;
	QTest::newRow("fixed-length different relocated tags") << 0xa700 << false;
	QTest::newRow("BER-length relocated tags") << 0x8100 << true;
	QTest::newRow("BER-length different relocated tags") << 0xa700 << true;
}
void TestBellaMxf::authoredVideo()
{
	QFETCH(int, tagBase);
	QFETCH(bool, berLocalLengths);
	auto f = fixture();
	f.tagBase = quint16(tagBase);
	f.berLocalLengths = berLocalLengths;
	f.put(5, "FileDescriptor.SampleRate", rational(50, 2));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QCOMPARE(report.sizeDateCheck, Bella::MxfCheckResult::Verified);
	const auto &file = report.fileInformation;
	QVERIFY(file.fileMobId);
	QCOMPARE(bytes(*file.fileMobId), expectedId(fileId));
	QCOMPARE(file.kind, Bella::MxfKind::Video);
	QVERIFY(file.descriptorClass);
	auto descriptorKey = classKey(0x28);
	if (berLocalLengths)
		descriptorKey[5] = char(0x13);
	QCOMPARE(QByteArray(reinterpret_cast<const char *>(file.descriptorClass->data()), 16), descriptorKey);
	QCOMPARE(file.project, QString::fromUtf8("  Pröject 漢字  "));
	QCOMPARE(file.projectSelection, Bella::MxfProjectSelection::Preface);
	QCOMPARE(file.projectKey, Bella::MxfProjectKey::None);
	QCOMPARE(file.containerDuration, std::optional<qint64>(250));
	QVERIFY(file.sampleRate);
	QCOMPARE(file.sampleRate->numerator, 50);
	QCOMPARE(file.sampleRate->denominator, 2);
	QVERIFY(file.frameRateQualified);
	QVERIFY(!file.audioSamplingRate);
	QVERIFY(file.resolution);
	QCOMPARE(file.resolution->dimensions.width, qint64(1920));
	QCOMPARE(file.resolution->dimensions.height, qint64(1080));
	QCOMPARE(file.bitDepth.bits, std::optional<quint32>(10));
	QCOMPARE(file.compression.codingState, Bella::MxfCodingState::Present);
	QCOMPARE(file.compression.horizontalSubsampling, std::optional<quint32>(2));
	const auto &clip = report.associatedClipInformation;
	QVERIFY(clip.masterMobId);
	QCOMPARE(bytes(*clip.masterMobId), expectedId(masterId));
	QCOMPARE(clip.clipName, QString::fromUtf8("Master — Δ"));
	QCOMPARE(clip.type, Bella::MxfType::Media);
	QVERIFY2(report.problemNotes.isEmpty(), qPrintable(report.problemNotes.join('\n')));
}

void TestBellaMxf::admission_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 12> names{
	    {"OP-Atom qualifier zero", "OP-Atom qualifier one", "OP-Atom qualifier two", "OP-Atom qualifier three",
	     "open incomplete", "closed incomplete", "open complete", "OP1a", "zero operational pattern", "leading run-in",
	     "future major version", "unrecognized key is not interpreted as BER"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::admission()
{
	QFETCH(int, variation);
	auto f = fixture();
	if (variation < 4)
		f.pattern[13] = char(variation);
	else if (variation < 7)
		f.status = quint8(variation - 3);
	else if (variation == 7)
	{
		f.pattern[12] = 1;
		f.pattern[13] = 1;
	}
	else if (variation == 8)
		f.pattern = QByteArray(16, '\0');
	else if (variation == 10)
		f.major = 2;
	f.put(1, "Preface.OperationalPattern", f.pattern);
	const auto data = variation == 11 ? QByteArray(16, 'x') + QByteArray(1, char(0x80))
	                                  : (variation == 9 ? QByteArray(1, 'x') : QByteArray{}) + f.bytes();
	const auto report = parse(data);
	QCOMPARE(report.reading, variation < 4 ? Bella::MxfReadResult::Completed : Bella::MxfReadResult::Unsupported);
	QCOMPARE(report.fileInformation.fileMobId.has_value(), variation < 4);
	if (variation >= 4)
	{
		QVERIFY(report.fileInformation.project.isEmpty());
		QVERIFY(!report.associatedClipInformation.masterMobId);
	}
}
void TestBellaMxf::unsafeFraming_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("Primer stride") << 0;
	QTest::newRow("declared header beyond file") << 1;
	QTest::newRow("truncated later KLV hides no prefix") << 2;
	QTest::newRow("indefinite later KLV") << 3;
	QTest::newRow("local item exceeds set") << 4;
	QTest::newRow("essence inside metadata region") << 5;
	QTest::newRow("BER-coded index segment inside metadata region") << 6;
	QTest::newRow("partition pack inside metadata region") << 7;
	QTest::newRow("RIP inside metadata region") << 8;
	QTest::newRow("zero declared metadata bytes") << 9;
	QTest::newRow("declared region too small for Primer") << 10;
}
void TestBellaMxf::unsafeFraming()
{
	QFETCH(int, variation);
	auto f = fixture();
	if (variation == 0)
		f.primerStride = 17;
	else if (variation == 1)
		f.headerSizeDelta = 1;
	else if (variation == 2)
		f.extraHeader = classKey(0x7e) + length(100) + QByteArray(1, '\0');
	else if (variation == 3)
		f.extraHeader = classKey(0x7e) + QByteArray(1, char(0x80));
	else if (variation == 4)
		f.extraHeader = packet(classKey(0x28), be<quint16>(0x8fff) + be<quint16>(20) + QByteArray(1, '\0'));
	else if (variation == 5)
		f.extraHeader = packet(hex("060e2b34010201010d01030115010101"), QByteArray(8, '\0'));
	else if (variation == 6)
		f.extraHeader = packet(hex("060e2b34021301010d01020101100100"), QByteArray{});
	else if (variation == 7)
		f.extraHeader = packet(hex("060e2b34020501010d01020101030400"), QByteArray(88, '\0'));
	else if (variation == 8)
		f.extraHeader = packet(hex("060e2b34020501010d01020101110100"), QByteArray(4, '\0'));
	auto data = f.bytes();
	if (variation >= 9)
	{
		const qsizetype packStart = 16 + length(104).size();
		qToBigEndian<quint64>(variation == 9 ? 0 : 24, data.data() + packStart + 32);
	}
	const auto report = parse(data);
	QCOMPARE(report.reading, Bella::MxfReadResult::Malformed);
	QVERIFY(!report.fileInformation.fileMobId);
	QVERIFY(report.fileInformation.project.isEmpty());
	QVERIFY(!report.associatedClipInformation.masterMobId);
	QVERIFY(!report.problemNotes.isEmpty());
}
void TestBellaMxf::stopsAtSelectedHeader()
{
	auto f = fixture();
	// Deliberately broken material outside the selected metadata region is irrelevant.
	f.suffix = hex("060e2b34010201010d01030115010101") + QByteArray(1, char(0x88)) +
	           be<quint64>(std::numeric_limits<quint64>::max());
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(bytes(*report.fileInformation.fileMobId), expectedId(fileId));
	QVERIFY(report.associatedClipInformation.masterMobId);
	QVERIFY2(report.problemNotes.isEmpty(), qPrintable(report.problemNotes.join('\n')));
}
void TestBellaMxf::safelyBoundedUnknownMetadata()
{
	auto f = fixture();
	f.extraHeader = packet(hex("060e2b34010101010101010101010101"), QByteArray(257, char(0x80)));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(bytes(*report.fileInformation.fileMobId), expectedId(fileId));
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.fileInformation.bitDepth.bits, std::optional<quint32>(10));
}
void TestBellaMxf::lateMetadataBeyond64MiB()
{
	const auto original = fixture().bytes();
	// This authored partition has one container label: 88 fixed bytes + 16.
	const qsizetype packStart = 16 + length(104).size();
	const qsizetype metadataStart = packStart + 104;
	const quint64 primerValueSize = 8 + 18 * std::size(definitions);
	const qsizetype primerSize = 16 + length(primerValueSize).size() + qsizetype(primerValueSize);
	const qint64 gap = 80 * 1024 * 1024;
	const auto unknown = hex("060e2b34010101010f7f010101010100") + length(quint64(gap));
	QByteArray first = original.first(metadataStart);
	qToBigEndian<quint64>(quint64(original.size() - metadataStart) + unknown.size() + quint64(gap),
	                      first.data() + packStart + 32);
	QTemporaryFile file;
	QVERIFY(file.open());
	QCOMPARE(file.write(first), qint64(first.size()));
	QCOMPARE(file.write(original.sliced(metadataStart, primerSize)), qint64(primerSize));
	QCOMPARE(file.write(unknown), qint64(unknown.size()));
	QVERIFY(file.seek(file.pos() + gap));
	const auto lateSets = original.sliced(metadataStart + primerSize);
	QCOMPARE(file.write(lateSets), qint64(lateSets.size()));
	QVERIFY(file.flush());
	std::atomic_bool stop{false};
	const auto report = Bella::parseMxf(file.fileName(), stop, false);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QCOMPARE(report.sizeDateCheck, Bella::MxfCheckResult::Verified);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(bytes(*report.fileInformation.fileMobId), expectedId(fileId));
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("Master — Δ"));
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
	QVERIFY(report.problemNotes.isEmpty());
}

void TestBellaMxf::alignmentFillPrecedesHeaderCount()
{
	auto f = fixture();
	f.beforePrimer = packet(hex("060e2b34010101020301021001000000"), QByteArray(37, '\0'));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(bytes(*report.fileInformation.fileMobId), expectedId(fileId));
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
}
void TestBellaMxf::fileAndMasterOwnership_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 12> names{
	    {"file package wrong role", "short file PackageUID", "two active same-ID file packages",
	     "master names wrong source track", "valid master plus unresolved candidate", "inactive same-ID source ignored",
	     "zero master PackageUID", "full source ID differs after shared prefix", "zero essence-linked package ID",
	     "two qualified distinct masters", "original zero SourceClip endpoint",
	     "conflicting InstanceUID claims cannot leave a healthy winner"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::fileAndMasterOwnership()
{
	QFETCH(int, variation);
	auto f = fixture();
	if (variation == 0)
		f.at(4).key = classKey(0x36);
	else if (variation == 1)
		f.put(4, "GenericPackage.PackageUID", QByteArray(31, 'x'));
	else if (variation == 2 || variation == 5)
	{
		f.add(30, 0x37);
		f.append(30, "GenericPackage.PackageUID", packageId(fileId));
		f.append(30, "SourcePackage.Descriptor", uid(5));
		f.append(30, "GenericPackage.Tracks", references({10}));
		if (variation == 2)
			f.put(2, "ContentStorage.Packages", references({4, 6, 30}));
	}
	else if (variation == 3)
		f.put(9, "SourceClip.SourceTrackID", be<quint32>(99));
	else if (variation == 4 || variation == 9)
	{
		f.add(30, 0x36);
		f.append(30, "GenericPackage.PackageUID", packageId(masterId + 1));
		f.append(30, "GenericPackage.Name", text(QStringLiteral("Different master")));
		f.append(30, "GenericPackage.Tracks", references({variation == 4 ? 999u : 31u}));
		f.put(2, "ContentStorage.Packages", references({4, 6, 30}));
		if (variation == 9)
		{
			track(f, 31, 32, 1, rational(25));
			sequence(f, 32, 33, picture, 250);
			sourceClip(f, 33, packageId(fileId), 3, picture, 250);
		}
	}
	else if (variation == 6)
		f.put(6, "GenericPackage.PackageUID", QByteArray(32, '\0'));
	else if (variation == 7)
		f.put(9, "SourceClip.SourcePackageID", packageId(fileId + 1));
	else if (variation == 8)
		f.put(3, "EssenceContainerData.LinkedPackageUID", QByteArray(32, '\0'));
	else if (variation == 11)
	{
		f.add(30, 0x28);
		// This object's first claim is its own UID, its second claim collides with
		// the otherwise healthy descriptor. Discarding this object must not make
		// the healthy descriptor look like a uniquely resolved target.
		f.append(30, "InterchangeObject.InstanceUID", uid(5));
	}
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	const bool fileKnown = variation != 0 && variation != 1 && variation != 2 && variation != 8 && variation != 11;
	const bool masterKnown = variation == 5 || variation == 10;
	QCOMPARE(report.fileInformation.fileMobId.has_value(), fileKnown);
	QCOMPARE(report.associatedClipInformation.masterMobId.has_value(), masterKnown);
	QCOMPARE(report.fileInformation.project, QString::fromUtf8("  Pröject 漢字  "));
	QCOMPARE(report.fileInformation.projectSelection, Bella::MxfProjectSelection::Preface);
	QCOMPARE(report.fileInformation.projectKey, Bella::MxfProjectKey::None);
	if (fileKnown)
	{
		QCOMPARE(bytes(*report.fileInformation.fileMobId), expectedId(fileId));
		QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
		QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
	}
	if (masterKnown)
	{
		QCOMPARE(bytes(*report.associatedClipInformation.masterMobId), expectedId(masterId));
		QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("Master — Δ"));
	}
	else
	{
		QVERIFY(report.associatedClipInformation.clipName.isEmpty());
		QCOMPARE(report.associatedClipInformation.type, Bella::MxfType::Unknown);
	}
}
void TestBellaMxf::localDescriptorDamage_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("negative duration") << 0;
	QTest::newRow("short duration") << 1;
	QTest::newRow("short rate") << 2;
	QTest::newRow("zero denominator") << 3;
	QTest::newRow("short component depth") << 4;
	QTest::newRow("missing duration") << 5;
	QTest::newRow("known zero duration") << 6;
	QTest::newRow("largest nonnegative duration") << 7;
}
void TestBellaMxf::localDescriptorDamage()
{
	QFETCH(int, variation);
	auto f = fixture();
	if (variation == 0)
		f.put(5, "FileDescriptor.ContainerDuration", be<qint64>(-1));
	else if (variation == 1)
		f.put(5, "FileDescriptor.ContainerDuration", be<qint32>(250));
	else if (variation == 2)
		f.put(5, "FileDescriptor.SampleRate", be<qint32>(25));
	else if (variation == 3)
		f.put(5, "FileDescriptor.SampleRate", rational(25, 0));
	else if (variation == 4)
		f.put(5, "CDCIEssenceDescriptor.ComponentDepth", be<quint16>(10));
	else if (variation == 5)
		f.remove(5, "FileDescriptor.ContainerDuration");
	else
		f.put(5, "FileDescriptor.ContainerDuration",
		      be<qint64>(variation == 6 ? 0 : std::numeric_limits<qint64>::max()));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
	QCOMPARE(report.fileInformation.sampleRate.has_value(), variation != 2 && variation != 3);
	QCOMPARE(report.fileInformation.bitDepth.bits.has_value(), variation != 4);
	if (variation == 0 || variation == 1 || variation == 5)
		QVERIFY(!report.fileInformation.containerDuration);
	else
		QCOMPARE(report.fileInformation.containerDuration,
		         std::optional<qint64>(variation == 6   ? 0
		                               : variation == 7 ? std::numeric_limits<qint64>::max()
		                                                : 250));
	if (variation < 5)
		QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMxf::duplicateRates_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("equivalent keeps first 25 over 1") << 0;
	QTest::newRow("equivalent keeps first 50 over 2") << 1;
	QTest::newRow("different numeric rates conflict") << 2;
}
void TestBellaMxf::duplicateRates()
{
	QFETCH(int, variation);
	auto f = fixture();
	f.put(5, "FileDescriptor.SampleRate", variation == 1 ? rational(50, 2) : rational(25));
	f.append(5, "FileDescriptor.SampleRate", variation == 0 ? rational(50, 2) : rational(variation == 1 ? 25 : 30));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("Master — Δ"));
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
	QCOMPARE(report.fileInformation.sampleRate.has_value(), variation != 2);
	QCOMPARE(report.fileInformation.frameRateQualified, variation != 2);
	if (report.fileInformation.sampleRate)
	{
		QCOMPARE(report.fileInformation.sampleRate->numerator, variation == 1 ? 50 : 25);
		QCOMPARE(report.fileInformation.sampleRate->denominator, variation == 1 ? 2 : 1);
	}
	if (variation == 2)
		QVERIFY(!report.problemNotes.isEmpty());
}

void TestBellaMxf::projectSelection_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 9> names{{"Preface wins", "own PJ", "blank PJ permits PROJNAME",
	                                         "bad Preface permits own PJ", "conflicting Preface blocks fallback",
	                                         "conflicting own PJ blocks lower key", "master Project is not fallback",
	                                         "known USER PJ", "unsupported value permits next key"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::projectSelection()
{
	QFETCH(int, variation);
	auto f = fixture();
	if (variation != 0 && variation != 4)
		f.remove(1, "Preface.ProjectName");
	taggedText(f, 4, 20, QStringLiteral("_PJ"), QString::fromUtf8("  Own pröject  "));
	if (variation == 2 || variation == 5 || variation == 8)
	{
		taggedText(f, 4, 21, QStringLiteral("PROJNAME"), QStringLiteral("Lower key"));
		f.put(4, "GenericPackage.MobAttributeList", references({20, 21}));
		if (variation == 2)
			f.put(20, "TaggedValue.Value", hex("420110020000000000060e2b3401040101") + text(QStringLiteral("   ")));
		else if (variation == 5)
			f.append(20, "TaggedValue.Value",
			         hex("420110020000000000060e2b3401040101") + text(QStringLiteral("Conflict")));
		else
			f.put(20, "TaggedValue.Value", QByteArray(17, char(0x7f)) + text(QStringLiteral("Unsupported")));
	}
	else if (variation == 3)
		f.append(1, "Preface.ProjectName", hex("004100"));
	else if (variation == 4)
		f.append(1, "Preface.ProjectName", text(QStringLiteral("Conflicting header Project")));
	else if (variation == 6)
	{
		f.remove(4, "GenericPackage.MobAttributeList");
		taggedText(f, 6, 21, QStringLiteral("_PJ"), QStringLiteral("Do not borrow this master Project"));
	}
	else if (variation == 7)
	{
		f.put(20, "TaggedValue.Name", text(QStringLiteral("_USER")));
		f.put(20, "TaggedValue.Value",
		      hex("420110020000000000060e2b3401040101") + text(QStringLiteral("__AttributeList")));
		f.append(20, "TaggedValue.TaggedValueAttributeList", references({21}));
		f.add(21, 0x3f);
		f.append(21, "TaggedValue.Name", text(QStringLiteral("_PJ")));
		f.append(21, "TaggedValue.Value",
		         hex("420110020000000000060e2b3401040101") + text(QStringLiteral("Nested known Project")));
	}
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QVERIFY(report.associatedClipInformation.masterMobId);
	using Selection = Bella::MxfProjectSelection;
	const std::array<Selection, 9> selections{{Selection::Preface, Selection::OwnFile, Selection::OwnFile,
	                                           Selection::OwnFile, Selection::Conflict, Selection::Conflict,
	                                           Selection::Unavailable, Selection::OwnFile,
	                                           Selection::OwnFile}};
	using Key = Bella::MxfProjectKey;
	const std::array<Key, 9> keys{{Key::None, Key::Pj, Key::ProjName, Key::Pj, Key::None,
	                               Key::None, Key::None, Key::UserPj, Key::ProjName}};
	QCOMPARE(report.fileInformation.projectSelection, selections[std::size_t(variation)]);
	QCOMPARE(report.fileInformation.projectKey, keys[std::size_t(variation)]);
	const QString expected = variation == 0                     ? QString::fromUtf8("  Pröject 漢字  ")
	                         : variation == 1 || variation == 3 ? QString::fromUtf8("  Own pröject  ")
	                         : variation == 2 || variation == 8 ? QStringLiteral("Lower key")
	                         : variation == 7                   ? QStringLiteral("Nested known Project")
	                                                            : QString{};
	QCOMPARE(report.fileInformation.project, expected);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
	if (variation == 3 || variation == 4 || variation == 5 || variation == 8)
		QVERIFY(!report.problemNotes.isEmpty());
}
void TestBellaMxf::clipNameDamage_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("valid unpadded UTF16BE") << 0;
	QTest::newRow("odd byte count") << 1;
	QTest::newRow("unpaired surrogate") << 2;
	QTest::newRow("conflicting names") << 3;
	QTest::newRow("absent name") << 4;
}
void TestBellaMxf::clipNameDamage()
{
	QFETCH(int, variation);
	auto f = fixture();
	const QString wanted = QString::fromUtf8("  漢字 — Δ  ");
	if (variation == 0)
		f.put(6, "GenericPackage.Name", text(wanted, false));
	else if (variation == 1)
		f.put(6, "GenericPackage.Name", hex("004100"));
	else if (variation == 2)
		f.put(6, "GenericPackage.Name", hex("d8000000"));
	else if (variation == 3)
		f.append(6, "GenericPackage.Name", text(QStringLiteral("Another name")));
	else
		f.remove(6, "GenericPackage.Name");
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.clipName, variation == 0 ? wanted : QString{});
	QCOMPARE(report.associatedClipInformation.type, Bella::MxfType::Media);
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
	if (variation >= 1 && variation <= 3)
		QVERIFY(!report.problemNotes.isEmpty());
}
void TestBellaMxf::audioClocksStaySeparate()
{
	for (int variation = 0; variation < 3; ++variation)
	{
		auto f = fixture(true);
		f.put(5, "GenericSoundEssenceDescriptor.AudioSamplingRate", rational(96000, 2));
		if (variation == 1)
			f.remove(1, "Preface.ProjectEditRate");
		else if (variation == 2)
			f.put(1, "Preface.ProjectEditRate", rational(0));
		const auto report = parse(f);
		QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
		const auto &file = report.fileInformation;
		QVERIFY(file.fileMobId);
		QCOMPARE(file.kind, Bella::MxfKind::Audio);
		QCOMPARE(file.containerDuration, std::optional<qint64>(96000));
		QVERIFY(file.sampleRate);
		QCOMPARE(file.sampleRate->numerator, 48000);
		QCOMPARE(file.sampleRate->denominator, 1);
		QVERIFY(file.audioSamplingRate);
		QCOMPARE(file.audioSamplingRate->numerator, 96000);
		QCOMPARE(file.audioSamplingRate->denominator, 2);
		QVERIFY(!file.frameRateQualified);
		QCOMPARE(file.projectEditRate.has_value(), variation == 0);
		if (file.projectEditRate)
			QCOMPARE(file.projectEditRate->numerator, 25);
		else
			QCOMPARE(file.numbering, Bella::MxfNumbering::Unavailable);
		QCOMPARE(file.bitDepth.bits, std::optional<quint32>(24));
		QCOMPARE(file.compression.codingState, Bella::MxfCodingState::Absent);
		QCOMPARE(file.compression.namingFallback, Bella::MxfNamingFallback::Pcm);
	}
}
void TestBellaMxf::extremeTimingKeepsSourceValues()
{
	auto f = fixture(true);
	const auto maximum = std::numeric_limits<qint64>::max();
	f.put(5, "FileDescriptor.ContainerDuration", be(maximum));
	f.put(5, "FileDescriptor.SampleRate", rational(1, std::numeric_limits<qint32>::max()));
	f.put(1, "Preface.ProjectEditRate", rational(30000, 1001));
	f.put(10, "Track.EditRate", rational(1, std::numeric_limits<qint32>::max()));
	f.put(11, "StructuralComponent.Duration", be(maximum));
	f.put(12, "StructuralComponent.Duration", be(maximum));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(maximum));
	QVERIFY(report.fileInformation.sampleRate);
	QCOMPARE(report.fileInformation.sampleRate->numerator, 1);
	QCOMPARE(report.fileInformation.sampleRate->denominator, std::numeric_limits<qint32>::max());
	QCOMPARE(report.fileInformation.numbering, Bella::MxfNumbering::Unavailable);
	QVERIFY(report.fileInformation.audioSamplingRate);
	QCOMPARE(report.fileInformation.audioSamplingRate->numerator, 48000);
	QVERIFY(report.fileInformation.fileMobId);
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("Master — Δ"));
}

void TestBellaMxf::audioCodingQualification_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("confirmed absent coding") << 0;
	QTest::newRow("unknown present label") << 1;
	QTest::newRow("short coding label") << 2;
	QTest::newRow("conflicting coding labels") << 3;
}
void TestBellaMxf::audioCodingQualification()
{
	QFETCH(int, variation);
	auto f = fixture(true);
	if (variation)
		f.append(5, "GenericSoundEssenceDescriptor.SoundEssenceCompression",
		         variation == 2 ? QByteArray(1, 'x') : QByteArray(16, char(0x7f)));
	if (variation == 3)
		f.append(5, "GenericSoundEssenceDescriptor.SoundEssenceCompression", QByteArray(16, char(0x7e)));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Audio);
	QCOMPARE(report.fileInformation.bitDepth.bits, std::optional<quint32>(24));
	QVERIFY(report.fileInformation.audioSamplingRate);
	QCOMPARE(report.fileInformation.compression.codingState, variation == 0   ? Bella::MxfCodingState::Absent
	                                                         : variation == 1 ? Bella::MxfCodingState::Present
	                                                                          : Bella::MxfCodingState::Unavailable);
	QCOMPARE(report.fileInformation.compression.namingFallback,
	         variation == 0 ? Bella::MxfNamingFallback::Pcm : Bella::MxfNamingFallback::None);
}
void TestBellaMxf::knownDescriptorKinds_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 9> names{{"GenericSound", "WaveAudio", "AES3Audio", "GenericPicture", "CDCI",
	                                         "MPEGVideo", "RGBA", "unknown class with picture properties",
	                                         "foreign full class with known suffix"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::knownDescriptorKinds()
{
	QFETCH(int, variation);
	const std::array<quint8, 9> classes{{0x42, 0x48, 0x47, 0x27, 0x28, 0x51, 0x29, 0x7e, 0x28}};
	auto f = fixture(variation < 3);
	f.at(5).key = classKey(classes[std::size_t(variation)]);
	if (variation == 8)
		f.at(5).key[8] = char(0x0e);
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QCOMPARE(report.fileInformation.kind, variation < 3   ? Bella::MxfKind::Audio
	                                      : variation < 7 ? Bella::MxfKind::Video
	                                                      : Bella::MxfKind::Unknown);
	QCOMPARE(report.fileInformation.project, QString::fromUtf8("  Pröject 漢字  "));
}
void TestBellaMxf::numbering_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 7> names{{"applicable timecode absent", "recorded DF Boolean",
	                                         "recorded NDF Boolean", "invalid Boolean value", "empty Boolean",
	                                         "incompatible nominal base", "unrelated master timecode ignored"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::numbering()
{
	QFETCH(int, variation);
	auto f = fixture();
	f.put(5, "FileDescriptor.SampleRate", rational(30000, 1001));
	f.put(7, "Track.EditRate", rational(30000, 1001));
	f.put(10, "Track.EditRate", rational(30000, 1001));
	if (variation != 0)
	{
		f.put(variation == 6 ? 6 : 4, "GenericPackage.Tracks", references({variation == 6 ? 7u : 10u, 20}));
		track(f, 20, 21, 17, rational(30000, 1001));
		sequence(f, 21, 22, timecode, 250);
		f.add(22, 0x14);
		f.append(22, "StructuralComponent.DataDefinition", timecode);
		f.append(22, "StructuralComponent.Duration", be<qint64>(250));
		f.append(22, "TimecodeComponent.RoundedTimecodeBase", be<quint16>(variation == 5 ? 25 : 30));
		f.append(22, "TimecodeComponent.StartTimecode", be<qint64>(0));
		f.append(22, "TimecodeComponent.DropFrame",
		         variation == 4 ? QByteArray{}
		                        : QByteArray(1, char(variation == 2   ? 0
		                                             : variation == 3 ? 2
		                                                              : 1)));
	}
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QVERIFY(report.fileInformation.frameRateQualified);
	const auto expected = variation == 0 || variation == 6 ? Bella::MxfNumbering::MissingNdfFallback
	                      : variation == 1                 ? Bella::MxfNumbering::RecordedDf
	                      : variation == 2                 ? Bella::MxfNumbering::RecordedNdf
	                                                       : Bella::MxfNumbering::Unavailable;
	QCOMPARE(report.fileInformation.numbering, expected);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
}
void TestBellaMxf::immediateSourceNumbering_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 10> names{
	    {"source DF covers requested 10 to 260", "source target TrackID does not exist",
	     "source timecode ends before requested interval", "source media clock unreadable",
	     "own NDF conflicts with qualified source DF", "NDF-only clock skips irrelevant broken timecode",
	     "nonterminal source cannot prove absence", "terminal source proves applicable absence",
	     "known zero duration uses applicable source point", "different source timecode clock cannot impose DF"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::immediateSourceNumbering()
{
	QFETCH(int, variation);
	auto f = fixture();
	const auto clock = variation == 5 ? rational(25) : rational(30000, 1001);
	f.put(5, "FileDescriptor.SampleRate", clock);
	f.put(7, "Track.EditRate", clock);
	f.put(10, "Track.EditRate", clock);
	f.put(12, "SourceClip.SourcePackageID", packageId(0x55667788));
	f.put(12, "SourceClip.SourceTrackID", be<quint32>(variation == 1 ? 99 : 9));
	f.put(12, "SourceClip.StartPosition", be<qint64>(10));
	f.put(2, "ContentStorage.Packages", references({4, 6, 30}));
	f.add(30, 0x37);
	f.append(30, "GenericPackage.PackageUID", packageId(0x55667788));
	f.append(30, "GenericPackage.Tracks", references({31, 34}));
	track(f, 31, 32, 9, variation == 3 ? rational(0) : rational(30000, 1001));
	sequence(f, 32, 33, picture, 500);
	sourceClip(f, 33, QByteArray(32, '\0'), 0, picture, 500);
	timecodeTrack(f, 34, variation == 2 ? 250 : 500, 1);
	if (variation == 4)
	{
		f.put(4, "GenericPackage.Tracks", references({10, 40}));
		timecodeTrack(f, 40, 250, 0);
	}
	else if (variation == 5)
		f.put(36, "TimecodeComponent.DropFrame", QByteArray{});
	else if (variation == 6 || variation == 7)
	{
		f.put(30, "GenericPackage.Tracks", references({31}));
		if (variation == 6)
			f.put(33, "SourceClip.SourcePackageID", packageId(0x99887766));
	}
	else if (variation == 8)
		f.put(5, "FileDescriptor.ContainerDuration", be<qint64>(0));
	else if (variation == 9)
		f.put(34, "Track.EditRate", rational(25));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(bytes(*report.fileInformation.fileMobId), expectedId(fileId));
	QVERIFY(report.associatedClipInformation.masterMobId);
	QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("Master — Δ"));
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(variation == 8 ? 0 : 250));
	QCOMPARE(report.fileInformation.bitDepth.bits, std::optional<quint32>(10));
	QVERIFY(report.fileInformation.sampleRate);
	QCOMPARE(report.fileInformation.sampleRate->numerator, variation == 5 ? 25 : 30000);
	QCOMPARE(report.fileInformation.sampleRate->denominator, variation == 5 ? 1 : 1001);
	const auto expected = variation == 0 || variation == 8   ? Bella::MxfNumbering::RecordedDf
	                      : variation == 5                   ? Bella::MxfNumbering::NotRequired
	                      : variation == 7 || variation == 9 ? Bella::MxfNumbering::MissingNdfFallback
	                                                         : Bella::MxfNumbering::Unavailable;
	QCOMPARE(report.fileInformation.numbering, expected);
	if (expected == Bella::MxfNumbering::Unavailable)
		QVERIFY(!report.problemNotes.isEmpty());
	else if (variation == 5)
		QVERIFY2(report.problemNotes.isEmpty(), qPrintable(report.problemNotes.join('\n')));
}

void TestBellaMxf::pictureGeometry_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 7> names{{"nested valid rectangles", "Display exceeds Sampled parent",
	                                         "wrong width field size", "SeparateFields converts height only",
	                                         "MixedFields does not double", "zero Display width is not absence",
	                                         "negative crop offset"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::pictureGeometry()
{
	QFETCH(int, variation);
	auto f = fixture();
	if (variation == 0 || variation == 1 || variation == 6)
	{
		f.put(5, "GenericPictureEssenceDescriptor.SampledWidth", be<quint32>(1900));
		f.put(5, "GenericPictureEssenceDescriptor.SampledHeight", be<quint32>(1060));
		f.put(5, "GenericPictureEssenceDescriptor.SampledXOffset", be<qint32>(10));
		f.put(5, "GenericPictureEssenceDescriptor.SampledYOffset", be<qint32>(10));
		f.put(5, "GenericPictureEssenceDescriptor.DisplayWidth", be<quint32>(variation == 1 ? 1895 : 1800));
		f.put(5, "GenericPictureEssenceDescriptor.DisplayHeight", be<quint32>(1000));
		f.put(5, "GenericPictureEssenceDescriptor.DisplayXOffset", be<qint32>(variation == 6 ? -1 : 20));
		f.put(5, "GenericPictureEssenceDescriptor.DisplayYOffset", be<qint32>(20));
	}
	else if (variation == 2)
		f.put(5, "GenericPictureEssenceDescriptor.SampledWidth", be<quint16>(1920));
	else if (variation == 3)
	{
		f.put(5, "GenericPictureEssenceDescriptor.StoredHeight", be<quint32>(540));
		f.put(5, "GenericPictureEssenceDescriptor.FrameLayout", QByteArray(1, char(1)));
	}
	else if (variation == 4)
		f.put(5, "GenericPictureEssenceDescriptor.FrameLayout", QByteArray(1, char(3)));
	else
		f.put(5, "GenericPictureEssenceDescriptor.DisplayWidth", be<quint32>(0));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
	QCOMPARE(report.fileInformation.bitDepth.bits, std::optional<quint32>(10));
	const bool usable = variation == 0 || variation == 3 || variation == 4;
	QCOMPARE(report.fileInformation.resolution.has_value(), usable);
	if (usable)
	{
		QCOMPARE(report.fileInformation.resolution->dimensions.width, qint64(variation == 0 ? 1800 : 1920));
		QCOMPARE(report.fileInformation.resolution->dimensions.height, qint64(variation == 0 ? 1000 : 1080));
		QVERIFY(report.fileInformation.sampleRate);
		QCOMPARE(report.fileInformation.sampleRate->numerator, 25);
		QCOMPARE(report.fileInformation.sampleRate->denominator, 1);
	}
	else
		QVERIFY(!report.problemNotes.isEmpty());
}
void TestBellaMxf::verifiedSingleFieldProxy_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("observed 3472") << 0;
	QTest::newRow("observed 3470") << 1;
	QTest::newRow("observed 3491") << 2;
	QTest::newRow("near-miss coding does not prove units") << 3;
}
void TestBellaMxf::verifiedSingleFieldProxy()
{
	QFETCH(int, variation);
	auto f = fixture();
	// Three independently observed descriptor configurations. The near miss changes
	// one full coding-label byte while retaining every numeric property.
	const std::array<qint32, 3> ids{{3472, 3470, 3491}};
	const int row = variation == 3 ? 0 : variation;
	const quint32 width = row == 0 ? 480u : 352u;
	const quint32 height = row == 0 ? 270u : 240u;
	auto label = hex("060e2b340401010d0401020201311101");
	if (variation == 3)
		label[15] = char(2);
	f.put(5, "GenericPictureEssenceDescriptor.PictureEssenceCoding", label);
	f.put(5, "GenericPictureEssenceDescriptor.ResolutionID", be(ids[std::size_t(row)]));
	f.put(5, "GenericPictureEssenceDescriptor.FrameLayout", QByteArray(1, char(2)));
	f.put(5, "GenericPictureEssenceDescriptor.StoredWidth", be(width));
	f.put(5, "GenericPictureEssenceDescriptor.StoredHeight", be(height));
	f.put(5, "CDCIEssenceDescriptor.ComponentDepth", be<quint32>(8));
	for (const auto *prefix : {"Sampled", "Display"})
	{
		const auto stem = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
		f.put(5, (stem + "Width").constData(), be<quint32>(row == 0 ? 1920 : 720));
		f.put(5, (stem + "Height").constData(), be<quint32>(row == 0 ? 540 : 240));
		f.put(5, (stem + "XOffset").constData(), be<qint32>(0));
		f.put(5, (stem + "YOffset").constData(), be<qint32>(0));
	}
	f.put(5, "FileDescriptor.SampleRate", rational(30000, 1001));
	f.put(5, "FileDescriptor.ContainerDuration", be<qint64>(917));
	for (const auto object : {8u, 9u, 11u, 12u})
		f.put(object, "StructuralComponent.Duration", be<qint64>(917));
	for (const auto object : {7u, 10u})
		f.put(object, "Track.EditRate", rational(30000, 1001));
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(917));
	QVERIFY(report.fileInformation.sampleRate);
	QCOMPARE(report.fileInformation.sampleRate->numerator, 30000);
	QCOMPARE(report.fileInformation.sampleRate->denominator, 1001);
	QCOMPARE(report.fileInformation.frameRateQualified, variation != 3);
	QCOMPARE(report.fileInformation.resolution.has_value(), variation != 3);
	if (report.fileInformation.resolution)
	{
		QCOMPARE(report.fileInformation.resolution->dimensions.width, qint64(width));
		QCOMPARE(report.fileInformation.resolution->dimensions.height, qint64(height));
		QCOMPARE(report.fileInformation.resolution->origin, Bella::MxfRasterOrigin::VerifiedProxy);
	}
}
void TestBellaMxf::rgbaDepth_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 8> names{{"A8 with native padding and qualified uncompressed containers",
	                                         "mixed valid component widths", "palette component", "nonpadding tail",
	                                         "common RGBA widths", "unmatched container evidence",
	                                         "present unknown coding is not absence", "short PixelLayout"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::rgbaDepth()
{
	QFETCH(int, variation);
	auto f = fixture();
	f.at(5).key = classKey(0x29);
	f.remove(5, "CDCIEssenceDescriptor.ComponentDepth");
	f.remove(5, "CDCIEssenceDescriptor.HorizontalSubsampling");
	f.remove(5, "CDCIEssenceDescriptor.VerticalSubsampling");
	QByteArray layout = hex("41080000300030003000300030003000");
	if (variation == 1)
		layout = hex("520a470a420a41020000000000000000");
	else if (variation == 2)
		layout = hex("50080000000000000000000000000000");
	else if (variation == 3)
		layout = hex("41080000520800000000000000000000");
	else if (variation == 4)
		layout = hex("52084708420841080000000000000000");
	else if (variation == 7)
		layout.chop(1);
	f.put(5, "RGBAEssenceDescriptor.PixelLayout", layout);
	if (variation == 0 || variation == 5 || variation == 6)
	{
		f.container = alphaContainer;
		f.put(1, "Preface.EssenceContainers",
		      be<quint32>(1) + be<quint32>(16) + (variation == 5 ? ordinaryContainer : alphaContainer));
		f.remove(5, "GenericPictureEssenceDescriptor.PictureEssenceCoding");
		if (variation == 6)
			f.append(5, "GenericPictureEssenceDescriptor.PictureEssenceCoding", QByteArray(16, char(0x7f)));
	}
	const auto report = parse(f);
	QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
	QVERIFY(report.fileInformation.fileMobId);
	QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
	const bool depthKnown = variation == 0 || variation == 4 || variation == 5 || variation == 6;
	QCOMPARE(report.fileInformation.bitDepth.bits.has_value(), depthKnown);
	if (depthKnown)
		QCOMPARE(*report.fileInformation.bitDepth.bits, quint32(8));
	QCOMPARE(report.fileInformation.compression.namingFallback,
	         variation == 0 ? Bella::MxfNamingFallback::UncompressedAlpha8 : Bella::MxfNamingFallback::None);
	QCOMPARE(report.fileInformation.compression.codingState,
	         variation == 0 || variation == 5 ? Bella::MxfCodingState::Absent : Bella::MxfCodingState::Present);
	QCOMPARE(report.fileInformation.containerDuration, std::optional<qint64>(250));
}
void TestBellaMxf::typeAndCategory_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 11> names{
	    {"ordinary with absent markers", "AppCode7", "precompute with import absent",
	     "two direct picture tracks and valid empty object list", "marker alone does not prove object",
	     "broken wrapper reference", "present empty UsageCode", "contradictory standard usage",
	     "unsupported explicit AppCode0", "AppCode7 versus LowerLevel", "AppCode1 with LowerLevel"}};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}
void TestBellaMxf::typeAndCategory()
{
	QFETCH(int, variation);
	auto f = fixture();
	if (variation != 0 && variation != 6)
		f.append(6, "GenericPackage.AppCode",
		         be<qint32>(variation == 1 || variation == 9 ? 7
		                    : variation == 8                 ? 0
		                                                     : 1));
	if (variation == 3 || variation == 4 || variation == 5)
	{
		taggedText(f, 6, 20, QStringLiteral("_IMPORTSETTING"), QStringLiteral("__AttributeList"));
		if (variation != 4)
			f.append(
			    20, "TaggedValue.TaggedValueAttributeList",
			    references(variation == 5 ? std::initializer_list<quint32>{999} : std::initializer_list<quint32>{}));
		if (variation == 3)
		{
			f.put(6, "GenericPackage.Tracks", references({7, 30}));
			track(f, 30, 31, 2, rational(25));
			sequence(f, 31, 32, picture, 250);
			// The second picture belongs to a different physical file. Do not count
			// only tracks targeting this fixture's file, or open the sibling.
			sourceClip(f, 32, packageId(fileId + 123), 1, picture, 250);
		}
	}
	if (variation == 6)
		f.append(6, "GenericPackage.UsageCode", QByteArray{});
	else if (variation == 7)
		f.append(6, "GenericPackage.UsageCode", hex("060e2b34040101010d01010201010600"));
	else if (variation == 9 || variation == 10)
		f.append(6, "GenericPackage.UsageCode", hex("060e2b34040101010d01010201010800"));
	for (const bool details : {false, true})
	{
		const auto report = parse(f, details);
		QCOMPARE(report.reading, Bella::MxfReadResult::Completed);
		QVERIFY(report.fileInformation.fileMobId);
		QVERIFY(report.associatedClipInformation.masterMobId);
		QCOMPARE(report.fileInformation.kind, Bella::MxfKind::Video);
		QCOMPARE(report.associatedClipInformation.clipName, QString::fromUtf8("Master — Δ"));
		const bool media = variation == 0 || variation == 1;
		const bool precompute = (variation >= 2 && variation <= 5) || variation == 10;
		QCOMPARE(report.associatedClipInformation.type, media        ? Bella::MxfType::Media
		                                                : precompute ? Bella::MxfType::Precompute
		                                                             : Bella::MxfType::Unknown);
		const auto category = !details || !precompute || variation == 4 || variation == 5
		                          ? Bella::MxfPrecomputeCategory::Unknown
		                      : variation == 3 ? Bella::MxfPrecomputeCategory::TitlesAndMatteKeys
		                                       : Bella::MxfPrecomputeCategory::RenderedEffects;
		QCOMPARE(report.associatedClipInformation.precomputeCategory, category);
	}
}

void TestBellaMxf::openedIdentity_data()
{
	QTest::addColumn<int>("variation");
	const std::array<const char *, 14> names{
		"complete identity", "wrong full file identity", "two saved masters", "one missing saved master",
		"broken second master", "descriptor not required", "technical stream ID not required",
		"unsupported layout", "malformed header", "unlinked master", "conflicting file owners",
		"conflicting Preface claim", "agreeing Preface claim", "noncanonical expected spelling"};
	for (int i = 0; i < int(names.size()); ++i)
		QTest::newRow(names[std::size_t(i)]) << i;
}

void TestBellaMxf::openedIdentity()
{
	QFETCH(int, variation);
	auto f = fixture();
	QString expectedFile = OmfUid::toIdText(expectedId(fileId));
	QStringList masters{OmfUid::toIdText(expectedId(masterId))};
	bool expected = variation == 0 || variation == 2 || variation == 5 || variation == 6 || variation == 12;
	if (variation == 1)
	{
		auto different = expectedId(fileId);
		different[0] ^= char(0x40); // Identical material suffix is insufficient.
		expectedFile = OmfUid::toIdText(different);
	}
	if (variation >= 2 && variation <= 4)
	{
		f.add(30, 0x36);
		f.append(30, "GenericPackage.PackageUID", packageId(masterId + 1));
		f.append(30, "GenericPackage.Tracks", references({31}));
		track(f, 31, 32, 1, rational(25));
		sequence(f, 32, 33, picture, 250);
		sourceClip(f, 33, packageId(fileId), 3, picture, 250);
		f.put(2, "ContentStorage.Packages", references({4, 6, 30}));
		masters.append(OmfUid::toIdText(expectedId(masterId + 1)));
		if (variation == 3)
			masters.append(OmfUid::toIdText(expectedId(masterId + 2)));
		if (variation == 4)
			f.put(31, "GenericTrack.Sequence", uid(999));
	}
	if (variation == 5)
		f.remove(4, "SourcePackage.Descriptor");
	if (variation == 6)
		f.remove(3, "EssenceContainerData.BodySID");
	if (variation == 7)
		f.status = 1;
	if (variation == 8)
		f.headerSizeDelta = 1000;
	if (variation == 9)
		f.put(9, "SourceClip.SourcePackageID", packageId(fileId + 1));
	if (variation == 10)
	{
		f.add(30, 0x37);
		f.append(30, "GenericPackage.PackageUID", packageId(fileId));
		f.put(2, "ContentStorage.Packages", references({4, 6, 30}));
	}
	if (variation == 11 || variation == 12)
		f.append(1, "Preface.EssenceFileMobID", packageId(fileId + (variation == 11)));
	if (variation == 13)
		expectedFile.remove(QLatin1Char('.'));
	const auto content = f.bytes();
	QTemporaryFile source;
	QVERIFY(source.open());
	QCOMPARE(source.write(content), qint64(content.size()));
	QVERIFY(source.flush());
	QVERIFY(source.seek(7));
	const auto handle = source.handle();
	QCOMPARE(Bella::mxfIdentityMatches(source, expectedFile, masters), expected);
	QVERIFY(source.isOpen());
	QCOMPARE(source.handle(), handle);
	QCOMPARE(source.pos(), qint64(7));
	if (variation == 2)
	{
		// Table selection intentionally has no single master. Both saved
		// operation claims still have independently established live links.
		QVERIFY(!parse(f).associatedClipInformation.masterMobId);
		QVERIFY(Bella::mxfIdentityMatches(source, {}, masters));
	}
	source.close();
	QVERIFY(!Bella::mxfIdentityMatches(source, expectedFile, masters));
}

QTEST_APPLESS_MAIN(TestBellaMxf)
#include "tst_bellamxf.moc"
