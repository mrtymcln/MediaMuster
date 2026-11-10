// Independent authored bins check that evidence bytes map to their source positions.

#include "avb/avb_reader.h"
#include "test_avb.h"

#include <QBuffer>
#include <QSet>
#include <QTest>
#include <cstring>

// MARK: - Test helpers

namespace
{
	using Outcome = MediaEngine::ParsedSource::Outcome;
	using Container = MediaEngine::ParsedSource::Container;

	TestAvb::Document graph(bool big = false)
	{
		TestAvb::Document document;
		document.bigEndian = big;
		document.objects = {
			{"ABIN", TestAvb::bin(big, {2})},
			{"CMPO", TestAvb::composition(big, TestAvb::Master, "Owned clip", 4, {3})},
			{"SCLP", TestAvb::sourceClip(big)},
			{"ATTR", TestAvb::attributes(big, 6, "Metadata comment")},
			{"MSML", TestAvb::mediaLocator(big)},
			{"MCBR", TestAvb::binReference(big)}};
		return document;
	}
	MediaEngine::ParsedSource parse(QByteArray bytes)
	{
		QBuffer source(&bytes);
		source.open(QIODevice::ReadOnly);
		MediaEngine::Cancellation cancellation;
		return MediaEngine::AvbReader{}.read(source, {{}, cancellation});
	}
	const MediaEngine::AvidObject *object(const MediaEngine::ParsedSource &result, MediaEngine::ObjectHandle handle)
	{
		for (const auto &candidate : result.objects)
			if (candidate.handle == handle)
				return &candidate;
		return nullptr;
	}
	const MediaEngine::RawProperty *objectProperty(const MediaEngine::AvidObject &object, const QString &name)
	{
		for (const auto &candidate : object.properties)
			if (candidate.locator.name == name)
				return &candidate;
		return nullptr;
	}
	QVector<const MediaEngine::RawProperty *> textValues(const MediaEngine::AvidObject &object, const QString &text)
	{
		QVector<const MediaEngine::RawProperty *> values;
		for (const auto &candidate : object.properties)
			if (candidate.decoded.metaType().id() == QMetaType::QString && candidate.decoded.toString() == text)
				values.append(&candidate);
		return values;
	}
	QByteArray ranges(const QByteArray &source, const MediaEngine::PropertyLocator &locator)
	{
		QByteArray value;
		for (const auto &range : locator.ranges)
			value += source.mid(range.offset, range.length);
		return value;
	}
	void endianRows()
	{
		QTest::addColumn<bool>("big");
		QTest::newRow("little-endian") << false;
		QTest::newRow("big-endian") << true;
	}

	class ControlledDevice final : public QIODevice
	{
	public:
		QByteArray bytes;
		qint64 bytesRead = 0, failAt = -1, cancelAfter = -1, extentOverride = -1;
		qint64 forbiddenAfter = -1;
		bool forbiddenRead = false;
		bool changeSize = false;
		MediaEngine::Cancellation *cancellation = nullptr;
		explicit ControlledDevice(QByteArray value) : bytes(std::move(value))
		{
			open(QIODevice::ReadOnly | QIODevice::Unbuffered);
		}
		qint64 size() const override { return (extentOverride >= 0 ? extentOverride : bytes.size()) + (changeSize && bytesRead ? 1 : 0); }
		bool isSequential() const override { return false; }
		bool seek(qint64 offset) override { return offset >= 0 && QIODevice::seek(offset); }

	protected:
		qint64 readData(char *target, qint64 count) override
		{
			const auto offset = pos();
			if (forbiddenAfter >= 0 && offset + count > forbiddenAfter)
			{
				forbiddenRead = true;
				setErrorString(QStringLiteral("Attempted opaque payload read"));
				return -1;
			}
			if (failAt >= 0 && offset >= failAt)
			{
				setErrorString(QStringLiteral("Injected AVB read failure"));
				return -1;
			}
			if (offset >= bytes.size())
				return 0;
			count = qMin(count, bytes.size() - offset);
			if (offset < failAt && count > failAt - offset)
				count = failAt - offset;
			std::memcpy(target, bytes.constData() + offset, size_t(count));
			bytesRead += count;
			if (cancellation && cancelAfter >= 0 && bytesRead >= cancelAfter)
				cancellation->cancel();
			return count;
		}
		qint64 writeData(const char *, qint64) override { return -1; }
	};
}

// MARK: - Test fixture

class TestAvbReader final : public QObject
{
	Q_OBJECT
private slots:
	void nativeGraph_data();
	void nativeGraph();
	void nativeCompositionWords_data();
	void nativeCompositionWords();
	void referencesRetainSourceIndices_data();
	void referencesRetainSourceIndices();
	void bothTextRepresentationsRemainSeparate_data();
	void bothTextRepresentationsRemainSeparate();
	void repeatedAttributeNamesAreNotCollapsed();
	void invalidUtf8RetainsOriginalBytes();
	void unknownObjectRetainsLocationAndLaterObjects();
	void hugeUnknownObjectStaysRangeOnly();
	void invalidBooleanRetainsOriginalByte();
	void unknownExtensionKeepsEarlierFields();
	void invalidRoots_data();
	void invalidRoots();
	void hostileCountsAndLengths_data();
	void hostileCountsAndLengths();
	void invalidReferencesRemainVisible();
	void truncatedDocuments_data();
	void truncatedDocuments();
	void borrowedDeviceAndReceipt();
	void cancelledAndFailedReads();
	void sourceChangeQualifiesCompletion();
};

// MARK: - Native objects and field preservation

void TestAvbReader::nativeGraph_data() { endianRows(); }
void TestAvbReader::nativeGraph()
{
	QFETCH(bool, big);
	auto document = graph(big);
	const QByteArray bytes = document.bytes();
	const auto result = parse(bytes);
	QCOMPARE(result.container, Container::Avb);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.objects.size(), document.objects.size());
	for (qsizetype i = 0; i < document.objects.size(); ++i)
	{
		const auto *parsed = object(result, quint64(i + 1));
		QVERIFY(parsed && parsed->avb);
		QCOMPARE(parsed->avb->classId, document.objects[i].type);
		QCOMPARE(parsed->avb->bigEndian, big);
		QCOMPARE(parsed->avb->framing.offset, document.chunkOffsets[i]);
		QCOMPARE(parsed->avb->framing.length, qint64(8));
		QCOMPARE(parsed->avb->value.offset, document.chunkOffsets[i] + 8);
		QCOMPARE(parsed->avb->value.length, qint64(document.objects[i].payload.size()));
		for (const auto &field : parsed->properties)
			if (field.bytesRetained)
				QCOMPARE(ranges(bytes, field.locator), field.encoding);
	}
	const auto *composition = object(result, 2);
	QVERIFY(composition);
	QCOMPARE(composition->recordedIdentity, TestAvb::Master);
	const auto *identity = objectProperty(*composition, QStringLiteral("Composition.mob_id"));
	QVERIFY(identity);
	QCOMPARE(identity->decoded.toByteArray(), TestAvb::Master);
	QVERIFY(!textValues(*composition, QStringLiteral("Owned clip")).isEmpty());
}

void TestAvbReader::nativeCompositionWords_data() { endianRows(); }
void TestAvbReader::nativeCompositionWords()
{
	QFETCH(bool, big);
	auto document = graph(big);
	document.objects[1].payload = TestAvb::composition(big, TestAvb::Master, "Native words", 4, {3}, 0, false);
	const auto result = parse(document.bytes());
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *composition = object(result, 2);
	QVERIFY(composition);
	const auto *first = objectProperty(*composition, QStringLiteral("Composition.legacy_word0"));
	const auto *second = objectProperty(*composition, QStringLiteral("Composition.legacy_word1"));
	QVERIFY(first && second);
	QCOMPARE(first->decoded.toUInt(), quint32(0x11223344));
	QCOMPARE(second->decoded.toUInt(), quint32(0x77885566));
	QVERIFY(composition->recordedIdentity.isEmpty()); // No full MobId was recorded.
}

void TestAvbReader::referencesRetainSourceIndices_data() { endianRows(); }
void TestAvbReader::referencesRetainSourceIndices()
{
	QFETCH(bool, big);
	auto document = graph(big);
	const auto bytes = document.bytes();
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	bool root = false, binMember = false, track = false, attribute = false;
	for (const auto &relation : result.relationships)
	{
		if (relation.referenceEncoding != QStringLiteral("AVB.ObjectIndex"))
			continue;
		QCOMPARE(relation.basis, EvidenceBasis::Recorded);
		QCOMPARE(relation.recordedReference.toULongLong(), relation.target);
		QCOMPARE(ranges(bytes, relation.locator).size(), 4);
		if (relation.origin == 0 && relation.target == 1)
			root = true;
		if (relation.origin == 1 && relation.target == 2)
			binMember = true;
		if (relation.origin == 2 && relation.target == 3)
			track = true;
		if (relation.origin == 2 && relation.target == 4)
			attribute = true;
	}
	QVERIFY(root && binMember && track && attribute);
}

void TestAvbReader::bothTextRepresentationsRemainSeparate_data() { endianRows(); }
void TestAvbReader::bothTextRepresentationsRemainSeparate()
{
	QFETCH(bool, big);
	TestAvb::Document document;
	document.bigEndian = big;
	const QByteArray macRoman = QByteArray("  Caf") + char(0x8e) + "  ";
	const QString unicode = QString::fromUtf8("  東京 café  ");
	document.objects = {{"ABIN", TestAvb::bin(big)}, {"MCBR", TestAvb::binReference(big, macRoman, unicode.toUtf8())}};
	const auto bytes = document.bytes();
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *reference = object(result, 2);
	QVERIFY(reference);
	const auto legacy = textValues(*reference, QString::fromUtf8("  Café  "));
	const auto modern = textValues(*reference, unicode);
	QCOMPARE(legacy.size(), 1);
	QCOMPARE(modern.size(), 1);
	QVERIFY(legacy[0]->textEncoding && modern[0]->textEncoding);
	QCOMPARE(*legacy[0]->textEncoding, MediaEngine::TextEncoding::MacRoman);
	QCOMPARE(*modern[0]->textEncoding, MediaEngine::TextEncoding::Utf8);
	QCOMPARE(ranges(bytes, legacy[0]->locator), legacy[0]->encoding);
	QCOMPARE(ranges(bytes, modern[0]->locator), modern[0]->encoding);
	QVERIFY(legacy[0]->encoding.contains(macRoman));
	QVERIFY(modern[0]->encoding.contains(unicode.toUtf8()));
}

void TestAvbReader::repeatedAttributeNamesAreNotCollapsed()
{
	TestAvb::Bytes attributes;
	attributes.tags(2, 1);
	attributes.u32(2);
	for (const auto &value : {QByteArray("First observation"), QByteArray("Second observation")})
	{
		attributes.u32(2);
		attributes.string("Repeated");
		attributes.string(value);
	}
	attributes.u8(3);
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false, {}, false, false, 2)}, {"ATTR", attributes.data}};
	const auto result = parse(document.bytes());
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *parsed = object(result, 2);
	QVERIFY(parsed);
	QCOMPARE(textValues(*parsed, QStringLiteral("Repeated")).size(), 2);
	QCOMPARE(textValues(*parsed, QStringLiteral("First observation")).size(), 1);
	QCOMPARE(textValues(*parsed, QStringLiteral("Second observation")).size(), 1);
}

void TestAvbReader::invalidUtf8RetainsOriginalBytes()
{
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false)}, {"MCBR", TestAvb::binReference(false, "Legacy", QByteArray::fromHex("c328"))}};
	const auto result = parse(document.bytes());
	QVERIFY(result.outcome != Outcome::Complete);
	const auto *reference = object(result, 2);
	QVERIFY(reference);
	QVERIFY(!textValues(*reference, QStringLiteral("Legacy")).isEmpty());
	bool unreadable = false;
	for (const auto &field : reference->properties)
		if (field.textEncoding == MediaEngine::TextEncoding::Utf8 && field.state == PropertyReadState::Unreadable)
		{
			unreadable = true;
			QVERIFY(field.encoding.contains(QByteArray::fromHex("c328")));
			QVERIFY(!field.decoded.isValid());
		}
	QVERIFY(unreadable);
}

void TestAvbReader::unknownObjectRetainsLocationAndLaterObjects()
{
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false)}, {"ZZZZ", QByteArray("uninterpreted bytes")}, {"MSML", TestAvb::mediaLocator(false)}};
	const auto bytes = document.bytes();
	const auto result = parse(bytes);
	QVERIFY(result.outcome == Outcome::Incomplete || result.outcome == Outcome::Unsupported);
	QCOMPARE(result.objects.size(), 3);
	const auto *unknown = object(result, 2);
	QVERIFY(unknown && unknown->avb);
	QCOMPARE(unknown->avb->classId, QByteArray("ZZZZ"));
	QVERIFY(!unknown->avb->interpretationComplete);
	bool retained = false;
	for (const auto &field : unknown->properties)
		if (!field.bytesRetained && !field.locator.ranges.isEmpty())
		{
			retained = true;
			QCOMPARE(ranges(bytes, field.locator), document.objects[1].payload);
		}
	QVERIFY(retained);
	const auto *later = object(result, 3);
	QVERIFY(later && later->avb && later->avb->interpretationComplete);
}

void TestAvbReader::hugeUnknownObjectStaysRangeOnly()
{
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false)}, {"ZZZZ", {}}};
	QByteArray prefix = document.bytes();
	const quint32 payloadLength = quint32(2) * 1024 * 1024 * 1024;
	TestAvb::replaceU32(prefix, document.chunkOffsets[1] + 4, payloadLength);
	ControlledDevice source(prefix);
	source.extentOverride = prefix.size() + qint64(payloadLength);
	source.forbiddenAfter = prefix.size();
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::AvbReader{}.read(source, {{}, cancellation});
	QVERIFY2(!source.forbiddenRead, qPrintable(source.errorString()));
	QVERIFY(result.outcome == Outcome::Incomplete || result.outcome == Outcome::Unsupported);
	const auto *unknown = object(result, 2);
	QVERIFY(unknown && unknown->avb);
	QCOMPARE(unknown->avb->value.length, qint64(payloadLength));
	bool rangeOnly = false;
	for (const auto &field : unknown->properties)
		if (!field.bytesRetained && field.locator.ranges.size() == 1)
		{
			rangeOnly = true;
			QCOMPARE(field.locator.ranges.first().offset, qint64(prefix.size()));
			QCOMPARE(field.locator.ranges.first().length, qint64(payloadLength));
		}
	QVERIFY(rangeOnly);
}

void TestAvbReader::invalidBooleanRetainsOriginalByte()
{
	auto document = graph();
	auto bytes = document.bytes();
	// ABIN v14: 16 prefix bytes then ref/x/y/keyframe (12), then user_placed.
	bytes[document.chunkOffsets[0] + 8 + 28] = char(2);
	const auto result = parse(bytes);
	QVERIFY(result.outcome != Outcome::Complete);
	const auto *bin = object(result, 1);
	QVERIFY(bin);
	bool invalid = false;
	for (const auto &field : bin->properties)
		if (field.locator.name == QStringLiteral("Bin.items[0].user_placed"))
		{
			invalid = true;
			QCOMPARE(field.encoding, QByteArray(1, char(2)));
			QCOMPARE(field.state, PropertyReadState::Unreadable);
			QVERIFY(!field.decoded.isValid());
		}
	QVERIFY(invalid);
}

void TestAvbReader::unknownExtensionKeepsEarlierFields()
{
	QByteArray locator = TestAvb::mediaLocator(false);
	locator.chop(1);
	locator += QByteArray::fromHex("017faa5503");
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false)}, {"MSML", locator}, {"CMPO", TestAvb::composition(false)}};
	const auto bytes = document.bytes();
	const auto result = parse(bytes);
	QVERIFY(result.outcome == Outcome::Incomplete || result.outcome == Outcome::Unsupported);
	const auto *partial = object(result, 2);
	QVERIFY(partial && partial->avb && !partial->avb->interpretationComplete);
	QVERIFY(!textValues(*partial, QStringLiteral("EDIT")).isEmpty());
	const auto *later = object(result, 3);
	QVERIFY(later);
	QCOMPARE(later->recordedIdentity, TestAvb::Master);
	bool tail = false;
	for (const auto &field : partial->properties)
		if (field.encoding.contains(QByteArray::fromHex("aa55")))
		{
			tail = true;
			QCOMPARE(ranges(bytes, field.locator), field.encoding);
		}
	QVERIFY(tail);
}

// MARK: - Malformed input boundaries

void TestAvbReader::invalidRoots_data()
{
	QTest::addColumn<quint32>("root");
	QTest::newRow("null-root") << quint32(0);
	QTest::newRow("outside-inventory") << quint32(999);
	QTest::newRow("non-bin-root") << quint32(2);
}
void TestAvbReader::invalidRoots()
{
	QFETCH(quint32, root);
	auto document = graph();
	document.root = root;
	const auto result = parse(document.bytes());
	QVERIFY(result.outcome != Outcome::Complete);
	QVERIFY(!result.diagnostics.isEmpty());
}

void TestAvbReader::hostileCountsAndLengths_data()
{
	QTest::addColumn<QByteArray>("bytes");
	auto document = graph();
	const auto original = document.bytes();
	auto objectCount = original;
	TestAvb::replaceU32(objectCount, document.countOffset, 0xffffffffu);
	QTest::newRow("object-count-exceeds-file") << objectCount;
	auto chunkLength = original;
	TestAvb::replaceU32(chunkLength, document.chunkOffsets[1] + 4, 0xffffffffu);
	QTest::newRow("chunk-extent-exceeds-file") << chunkLength;
	auto trackCount = original;
	TestAvb::replaceU32(trackCount, document.chunkOffsets[1] + 8 + TestAvb::component(false, "Owned clip", 4).size() + 11, 0xffffffffu);
	QTest::newRow("negative-track-count") << trackCount;
	TestAvb::Document sequence;
	sequence.objects = {{"ABIN", TestAvb::bin(false)}, {"SEQU", TestAvb::sequence(false, {})}};
	auto sequenceCount = sequence.bytes();
	TestAvb::replaceU32(sequenceCount, sequence.chunkOffsets[1] + 8 + TestAvb::component(false).size() + 2, 0xffffffffu);
	QTest::newRow("sequence-count-exceeds-value") << sequenceCount;
}
void TestAvbReader::hostileCountsAndLengths()
{
	QFETCH(QByteArray, bytes);
	const auto result = parse(bytes);
	QVERIFY(result.outcome != Outcome::Complete);
	QVERIFY(!result.diagnostics.isEmpty());
}

void TestAvbReader::invalidReferencesRemainVisible()
{
	TestAvb::Document document;
	document.objects = {{"ABIN", TestAvb::bin(false, {99})}};
	const auto result = parse(document.bytes());
	QVERIFY(result.outcome != Outcome::Complete);
	bool recorded = false;
	for (const auto &relation : result.relationships)
		if (relation.recordedReference.toULongLong() == 99)
		{
			recorded = true;
			QCOMPARE(relation.target, MediaEngine::ObjectHandle(0));
			QVERIFY(!relation.explanation.isEmpty());
		}
	QVERIFY(recorded);
}

void TestAvbReader::truncatedDocuments_data()
{
	QTest::addColumn<QByteArray>("bytes");
	auto document = graph();
	const auto original = document.bytes();
	for (const qsizetype length : {qsizetype(0), qsizetype(1), qsizetype(8), qsizetype(21), document.chunkOffsets[0] - 1,
								   document.chunkOffsets[1] + 4, document.chunkOffsets[1] + 13, original.size() - 1})
		QTest::newRow(qPrintable(QStringLiteral("length-%1").arg(length))) << original.first(length);
}
void TestAvbReader::truncatedDocuments()
{
	QFETCH(QByteArray, bytes);
	const auto result = parse(bytes);
	QVERIFY(result.outcome != Outcome::Complete);
}

// MARK: - Source lifecycle and cancellation

void TestAvbReader::borrowedDeviceAndReceipt()
{
	auto document = graph();
	QByteArray bytes = document.bytes();
	QBuffer source(&bytes);
	QVERIFY(source.open(QIODevice::ReadOnly));
	QVERIFY(source.seek(9));
	auto receipt = QSharedPointer<SourceSnapshot>::create();
	receipt->path = QStringLiteral("/fixture/bin.avb");
	receipt->source = MetadataSource::Pmr;
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::AvbReader{}.read(source, {receipt, cancellation});
	QCOMPARE(result.outcome, Outcome::Complete);
	QVERIFY(source.isOpen());
	QVERIFY(result.snapshot);
	QCOMPARE(result.snapshot->path, receipt->path);
	QCOMPARE(result.snapshot->source, MetadataSource::Avb);
	QCOMPARE(receipt->source, MetadataSource::Pmr);
	for (const auto &parsed : result.objects)
		QCOMPARE(parsed.snapshot, result.snapshot);
}

void TestAvbReader::cancelledAndFailedReads()
{
	auto document = graph();
	const auto bytes = document.bytes();
	MediaEngine::Cancellation before;
	before.cancel();
	ControlledDevice cancelled(bytes);
	QCOMPARE(MediaEngine::AvbReader{}.read(cancelled, {{}, before}).outcome, Outcome::Cancelled);
	MediaEngine::Cancellation during;
	ControlledDevice interrupted(bytes);
	interrupted.cancellation = &during;
	interrupted.cancelAfter = 50;
	QCOMPARE(MediaEngine::AvbReader{}.read(interrupted, {{}, during}).outcome, Outcome::Cancelled);
	MediaEngine::Cancellation running;
	ControlledDevice failed(bytes);
	failed.failAt = document.chunkOffsets[1] + 30;
	QCOMPARE(MediaEngine::AvbReader{}.read(failed, {{}, running}).outcome, Outcome::IoError);
	QBuffer unopened;
	QCOMPARE(MediaEngine::AvbReader{}.read(unopened, {{}, running}).outcome, Outcome::IoError);
}

void TestAvbReader::sourceChangeQualifiesCompletion()
{
	auto document = graph();
	ControlledDevice source(document.bytes());
	source.changeSize = true;
	MediaEngine::Cancellation cancellation;
	const auto result = MediaEngine::AvbReader{}.read(source, {{}, cancellation});
	QVERIFY(result.outcome != Outcome::Complete);
	QVERIFY(!result.diagnostics.isEmpty());
}



QTEST_APPLESS_MAIN(TestAvbReader)
#include "tst_avb_reader.moc"
