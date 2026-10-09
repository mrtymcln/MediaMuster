// Checks the fresh MDB reader against small, deliberately varied containers
// and genuine Avid databases. The expectations come from their recorded
// bytes; the production MDB parser is not used as an oracle.

#include "canon/mdbreader.h"
#include "canon/bentoreader_p.h"
#include "testcanonbento.h"

#include <QBuffer>
#include <QFile>
#include <QMap>
#include <QTest>
#include <QtEndian>
#include <algorithm>

namespace
{
using Outcome = Canon::ParsedSource::Outcome;

using TestCanonBento::number;
using TestCanonBento::TypedBento;

Canon::ParsedSource parse(QByteArray bytes)
{
	QBuffer source(&bytes);
	source.open(QIODevice::ReadOnly);
	Canon::Cancellation cancellation;
	return Canon::MdbReader{}.read(source, {{}, cancellation});
}

Canon::Detail::BentoReadResult readCompact(const QByteArray &payload, const QByteArray &toc)
{
	QByteArray bytes = payload + toc + QByteArray::fromHex("a4434da5486472d7");
	bytes += number<quint16>(0x0101) + number<quint16>(1) + number<quint16>(2) + number<quint16>(0);
	bytes += number(quint32(payload.size())) + number(quint32(toc.size()));
	QBuffer source(&bytes);
	source.open(QIODevice::ReadOnly);
	Canon::Cancellation cancellation;
	return Canon::Detail::readBento(source, cancellation);
}

const Canon::AvidObject *object(const Canon::ParsedSource &result, Canon::ObjectHandle handle)
{
	for (const auto &candidate : result.objects)
		if (candidate.handle == handle) return &candidate;
	return nullptr;
}

QVector<const Canon::RawProperty *> properties(const Canon::AvidObject &object, const QString &name)
{
	QVector<const Canon::RawProperty *> matches;
	for (const auto &property : object.properties)
		if (property.locator.name == name) matches.append(&property);
	return matches;
}

const Canon::RawProperty &recordedProperty(const Canon::AvidObject &object, const QString &name)
{
	const auto matches = properties(object, name);
	if (matches.size() != 1) qFatal("Expected exactly one preserved property: %s", qPrintable(name));
	return *matches[0];
}

QByteArray readFixture(const QString &relative)
{
	QFile file(QStringLiteral(FIXTURES_DIR) + '/' + relative);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

class SequentialBuffer final : public QBuffer
{
public:
	using QBuffer::QBuffer;
	bool isSequential() const override { return true; }
};

class ShortBuffer final : public QBuffer
{
public:
	using QBuffer::QBuffer;
	Canon::Cancellation *cancellation = nullptr;
	qint64 cancelAfterBytes = -1, readBytes = 0;
	bool ioFailure = false;
protected:
	qint64 readData(char *destination, qint64 count) override
	{
		if (ioFailure) { setErrorString(QStringLiteral("Injected read failure")); return -1; }
		const qint64 got = QBuffer::readData(destination, qMin(count, qint64(1)));
		if (got > 0) readBytes += got;
		if (cancellation && readBytes >= cancelAfterBytes) cancellation->cancel();
		return got;
	}
};

class ChangingSizeBuffer final : public QBuffer
{
public:
	using QBuffer::QBuffer;
	qint64 size() const override { return QBuffer::size() + (readStarted ? 1 : 0); }
protected:
	qint64 readData(char *destination, qint64 count) override
	{
		const qint64 got = QBuffer::readData(destination, count);
		readStarted = true;
		return got;
	}
private:
	bool readStarted = false;
};
}

class TestCanonMdb final : public QObject
{
	Q_OBJECT
private slots:
	void dictionariesDuplicatesAndUnknownProperties();
	void ambiguousDictionariesAndUnknownSchema();
	void fragmentedValues_data();
	void fragmentedValues();
	void metadataAndContainerOrders_data();
	void metadataAndContainerOrders();
	void referenceMapsAndAmbiguity();
	void fragmentedReferenceLocations();
	void compactWidthsBoundariesAndIncompleteContinuations();
	void textEncodingIsPerProperty();
	void ambiguousContextAndTimestampShapes();
	void classAndObjectByteOrderEvidence();
	void realFixtures_data();
	void realFixtures();
	void realNonEnglishEvidence();
	void realOrderedRepeatedScalars();
	void borrowedDeviceAndReceipt();
	void truncationAndCorruption();
	void shortReadsCancellationAndFailures();
};

void TestCanonMdb::dictionariesDuplicatesAndUnknownProperties()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "OMFI:ObjID", "omfi:ObjectTag", "MOBJ", true);
	writer.add(101, "OMFI:CPNT:Name", "omfi:String", QByteArray("first\0", 6));
	writer.add(101, "OMFI:CPNT:Name", "omfi:String", QByteArray("second\0", 7));
	writer.add(101, "Vendor:Future:PrivateThing", "vendor:FutureType", QByteArray::fromHex("ff0007c4"));
	const QByteArray bytes = writer.build();
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, Canon::ParsedSource::Container::Bento);
	const auto *mob = object(result, 101);
	QVERIFY(mob);
	const auto names = properties(*mob, "OMFI:CPNT:Name");
	QCOMPARE(names.size(), 2);
	QCOMPARE(names[0]->decoded.toString(), QStringLiteral("first"));
	QCOMPARE(names[1]->decoded.toString(), QStringLiteral("second"));
	const auto &unknown = recordedProperty(*mob, "Vendor:Future:PrivateThing");
	QCOMPARE(unknown.state, PropertyReadState::Present);
	QCOMPARE(unknown.encoding, QByteArray::fromHex("ff0007c4"));
	QVERIFY(!unknown.decoded.isValid());
	QVERIFY(unknown.bento);
	QCOMPARE(unknown.bento->typeName, QStringLiteral("vendor:FutureType"));
	QCOMPARE(unknown.bento->generation, 7u);
	QCOMPARE(unknown.bento->property, writer.property("Vendor:Future:PrivateThing"));
	QCOMPARE(unknown.locator.key, number(writer.property("Vendor:Future:PrivateThing"), true));
	QVERIFY(!unknown.bento->tocRanges.isEmpty());
	QVERIFY(!unknown.interpretation.isEmpty());
	QVERIFY(object(result, writer.type("vendor:FutureType")));
	QVERIFY(object(result, writer.property("Vendor:Future:PrivateThing")));
}

void TestCanonMdb::ambiguousDictionariesAndUnknownSchema()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "Vendor:FirstName", "omfi:String", QByteArray("private\0", 8));
	const quint32 propertyId = writer.property("Vendor:FirstName");
	writer.entries.append({propertyId, 24, 21, QByteArray("Vendor:OtherName\0", 17)});
	const auto ambiguous = parse(writer.build());
	QCOMPARE(ambiguous.outcome, Outcome::Complete);
	const auto *entry = object(ambiguous, 101);
	QVERIFY(entry);
	QCOMPARE(entry->properties.size(), 1);
	QVERIFY(entry->properties[0].locator.name.isEmpty());
	QCOMPARE(entry->properties[0].bento->property, propertyId);
	QCOMPARE(entry->properties[0].encoding, QByteArray("private\0", 8));
	const auto *definition = object(ambiguous, propertyId);
	QVERIFY(definition);
	QCOMPARE(definition->properties.size(), 2);

	TypedBento noHead;
	noHead.add(101, "Vendor:Link", "omfi:ObjRef", number<quint32>(102));
	noHead.add(101, "Vendor:Integer", "omfi:Long", number<quint32>(123));
	const auto unknownSchema = parse(noHead.build());
	QCOMPARE(unknownSchema.outcome, Outcome::Complete);
	QVERIFY(unknownSchema.relationships.isEmpty());
	const auto *unknownEntry = object(unknownSchema, 101);
	QVERIFY(unknownEntry);
	QVERIFY(!recordedProperty(*unknownEntry, "Vendor:Integer").decoded.isValid());
	QCOMPARE(recordedProperty(*unknownEntry, "Vendor:Link").encoding, number<quint32>(102));

	TypedBento unsupported;
	unsupported.head(3);
	QCOMPARE(parse(unsupported.build()).outcome, Outcome::Unsupported);
}

void TestCanonMdb::fragmentedValues_data()
{
	QTest::addColumn<int>("major");
	QTest::newRow("fixed TOC") << 1;
	QTest::newRow("compact TOC") << 2;
}

void TestCanonMdb::fragmentedValues()
{
	QFETCH(int, major);
	TypedBento writer;
	writer.major = major;
	writer.head(1);
	writer.add(101, "Vendor:Split", "omfi:String", "part", true, true);
	writer.add(101, "Vendor:Split", "omfi:String", QByteArray(" two\0", 5));
	writer.add(101, "Vendor:Split", "omfi:String", QByteArray("another\0", 8));
	const QByteArray bytes = writer.build();
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *entry = object(result, 101);
	QVERIFY(entry);
	const auto values = properties(*entry, "Vendor:Split");
	QCOMPARE(values.size(), 2);
	QCOMPARE(values[0]->encoding, QByteArray("part two\0", 9));
	QCOMPARE(values[0]->decoded.toString(), QStringLiteral("part two"));
	QCOMPARE(values[0]->locator.ranges.size(), 2);
	QCOMPARE(values[1]->decoded.toString(), QStringLiteral("another"));
	QByteArray reconstructed;
	for (const auto &range : values[0]->locator.ranges) reconstructed += bytes.mid(range.offset, range.length);
	QCOMPARE(reconstructed, values[0]->encoding);
}

void TestCanonMdb::metadataAndContainerOrders_data()
{
	QTest::addColumn<int>("major");
	QTest::addColumn<int>("omfVersion");
	QTest::addColumn<bool>("containerBig");
	QTest::addColumn<bool>("metadataBig");
	for (const bool metadataBig : {false, true})
	{
		QTest::newRow(metadataBig ? "Bento1 OMF1 metadata BE" : "Bento1 OMF1 metadata LE") << 1 << 1 << false << metadataBig;
		for (const bool containerBig : {false, true})
			QTest::newRow(qPrintable(QStringLiteral("Bento2 OMF2 container %1 metadata %2").arg(containerBig).arg(metadataBig)))
				<< 2 << 2 << containerBig << metadataBig;
	}
}

void TestCanonMdb::metadataAndContainerOrders()
{
	QFETCH(int, major);
	QFETCH(int, omfVersion);
	QFETCH(bool, containerBig);
	QFETCH(bool, metadataBig);
	TypedBento writer;
	writer.major = major;
	writer.containerBig = containerBig;
	writer.metadataBig = metadataBig;
	writer.head(omfVersion);
	writer.add(101, omfVersion == 1 ? "OMFI:ObjID" : "OMFI:OOBJ:ObjClass", "omfi:ObjectTag", "MOBJ", true);
	writer.add(102, omfVersion == 1 ? "OMFI:ObjID" : "OMFI:OOBJ:ObjClass", "omfi:ObjectTag", "TRAK", true);
	writer.add(101, "Vendor:Signed", "omfi:Long", number<qint32>(-1234567, metadataBig), true);
	writer.add(101, "Vendor:Unsigned", "omfi:Ushort", number<quint16>(60001, metadataBig), true);
	writer.add(101, "Vendor:Rational", "omfi:Rational", number<qint32>(30000, metadataBig) + number<qint32>(1001, metadataBig));
	writer.add(101, "Vendor:Link", "omfi:ObjRef", writer.reference(102, omfVersion));
	writer.add(101, "Vendor:Links", "omfi:ObjRefArray", number<quint16>(2, metadataBig) + writer.reference(102, omfVersion) + writer.reference(999, omfVersion));
	const auto result = parse(writer.build());
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *mob = object(result, 101);
	QVERIFY(mob);
	QCOMPARE(recordedProperty(*mob, "Vendor:Signed").decoded.toLongLong(), -1234567ll);
	QCOMPARE(recordedProperty(*mob, "Vendor:Unsigned").decoded.toUInt(), 60001u);
	const auto rational = recordedProperty(*mob, "Vendor:Rational").decoded.toMap();
	QCOMPARE(rational.value("numerator").toInt(), 30000);
	QCOMPARE(rational.value("denominator").toInt(), 1001);
	QVector<Canon::ObjectHandle> links;
	for (const auto &relationship : result.relationships)
		if (relationship.origin == 101) links.append(relationship.target);
	QCOMPARE(links, (QVector<Canon::ObjectHandle>{102, 102, 0}));
	for (const auto &relationship : result.relationships)
	{
		QVERIFY(!relationship.referenceEncoding.isEmpty());
		QVERIFY(relationship.recordedReference.isValid());
		if (relationship.target == 0) QVERIFY(!relationship.explanation.isEmpty());
	}
}

void TestCanonMdb::referenceMapsAndAmbiguity()
{
	TypedBento writer;
	writer.major = 2;
	writer.containerBig = true;
	writer.head(2);
	writer.add(102, "OMFI:OOBJ:ObjClass", "omfi:ObjectTag", "TRAK", true);
	writer.add(103, "OMFI:OOBJ:ObjClass", "omfi:ObjectTag", "TRAK", true);
	writer.add(101, "Vendor:Link", "omfi:ObjRef", number<quint32>(17, true), true, false, 900);
	writer.entries.append({900, 31, 32, number<quint32>(17, true) + number<quint32>(102, true)});
	const auto result = parse(writer.build());
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.relationships.size(), 1);
	QCOMPARE(result.relationships[0].target, Canon::ObjectHandle(102));
	const auto *origin = object(result, 101);
	QVERIFY(origin);
	QCOMPARE(recordedProperty(*origin, "Vendor:Link").bento->referenceListObject, 900u);
	writer.entries.last().bytes += number<quint32>(17, true) + number<quint32>(103, true);
	const auto ambiguous = parse(writer.build());
	QCOMPARE(ambiguous.relationships.size(), 1);
	QCOMPARE(ambiguous.relationships[0].target, Canon::ObjectHandle(0));
	QVERIFY(!ambiguous.relationships[0].explanation.isEmpty());
	QVERIFY(object(ambiguous, 900));
}

void TestCanonMdb::textEncodingIsPerProperty()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "OMFI:MCBR:MC:binName", "omfi:String", QByteArray::fromHex("4e9a6e00"));
	writer.add(101, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", QByteArray::fromHex("4e6fcc886e00"));
	writer.add(101, "Vendor:Plain", "omfi:String", QByteArray("plain\0", 6));
	writer.add(101, "Vendor:Padded", "omfi:String", QByteArray::fromHex("4100000000"));
	writer.add(101, "Vendor:LooksLikeUTF8", "omfi:String", QByteArray::fromHex("c3a900"));
	const auto result = parse(writer.build());
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *entry = object(result, 101);
	QVERIFY(entry);
	const auto &legacy = recordedProperty(*entry, "OMFI:MCBR:MC:binName");
	QVERIFY(legacy.textEncoding == Canon::TextEncoding::Unknown);
	QVERIFY(!legacy.textEncodingBasis);
	QVERIFY(!legacy.decoded.isValid());
	QCOMPARE(legacy.encoding, QByteArray::fromHex("4e9a6e00"));
	const auto &unicode = recordedProperty(*entry, "OMFI:MCBR:MC:binNameUTF8");
	QVERIFY(unicode.textEncoding == Canon::TextEncoding::Utf8);
	QVERIFY(unicode.textEncodingBasis == EvidenceBasis::Recorded);
	QCOMPARE(unicode.decoded.toString(), QString::fromUtf8("No\xcc\x88n"));
	QVERIFY(unicode.decoded.toString() != unicode.decoded.toString().normalized(QString::NormalizationForm_C));
	const auto &ascii = recordedProperty(*entry, "Vendor:Plain");
	QVERIFY(ascii.textEncoding == Canon::TextEncoding::Ascii);
	QVERIFY(ascii.textEncodingBasis == EvidenceBasis::Derived);
	const auto &padded = recordedProperty(*entry, "Vendor:Padded");
	QCOMPARE(padded.decoded.toString(), QStringLiteral("A"));
	QCOMPARE(padded.encoding, QByteArray::fromHex("4100000000"));
	const auto &unprovenEncoding = recordedProperty(*entry, "Vendor:LooksLikeUTF8");
	QVERIFY(unprovenEncoding.textEncoding == Canon::TextEncoding::Unknown);
	QVERIFY(!unprovenEncoding.textEncodingBasis.has_value());
	QVERIFY(!unprovenEncoding.decoded.isValid());
	writer.add(101, "OMFI:FL:PathNameUTF8", "omfi:String", QByteArray::fromHex("c08000"));
	const auto invalid = parse(writer.build());
	const auto *invalidEntry = object(invalid, 101);
	QVERIFY(invalidEntry);
	const auto &bad = recordedProperty(*invalidEntry, "OMFI:FL:PathNameUTF8");
	QCOMPARE(bad.state, PropertyReadState::Unreadable);
	QCOMPARE(bad.encoding, QByteArray::fromHex("c08000"));
	QVERIFY(bad.textEncoding == Canon::TextEncoding::Utf8);
	QVERIFY(!bad.decoded.isValid());
	writer.add(101, "OMFI:MSML:LastKnownVolumeUTF8", "omfi:String", QByteArray::fromHex("e28200"));
	const auto terminal = parse(writer.build());
	const auto *terminalEntry = object(terminal, 101);
	QVERIFY(terminalEntry);
	const auto &incompleteSequence = recordedProperty(*terminalEntry, "OMFI:MSML:LastKnownVolumeUTF8");
	QCOMPARE(incompleteSequence.state, PropertyReadState::Unreadable);
	QCOMPARE(incompleteSequence.encoding, QByteArray::fromHex("e28200"));
	QVERIFY(!incompleteSequence.decoded.isValid());
}

void TestCanonMdb::ambiguousContextAndTimestampShapes()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "Vendor:Integer", "omfi:Long", number<quint32>(123));
	writer.add(101, "Vendor:StandardTimestamp", "omfi:TimeStamp", number<quint32>(123456789) + char(1));
	writer.add(101, "Vendor:OtherTimestampShape", "omfi:TimeStamp", number<quint32>(123456789) + number<quint32>(1));
	const auto normal = parse(writer.build());
	QCOMPARE(normal.outcome, Outcome::Complete);
	const auto *entry = object(normal, 101);
	QVERIFY(entry);
	const auto recordedTime = recordedProperty(*entry, "Vendor:StandardTimestamp").decoded.toMap();
	QCOMPARE(recordedTime.value("secondsSince1970").toUInt(), 123456789u);
	QCOMPARE(recordedTime.value("isGMT").toBool(), true);
	const auto &otherShape = recordedProperty(*entry, "Vendor:OtherTimestampShape");
	QCOMPARE(otherShape.state, PropertyReadState::Present);
	QVERIFY(!otherShape.decoded.isValid());
	QCOMPARE(otherShape.encoding, number<quint32>(123456789) + number<quint32>(1));

	writer.add(1, "OMFI:ByteOrder", "omfi:Short", "MM", true);
	const auto conflictingOrder = parse(writer.build());
	const auto *unqualified = object(conflictingOrder, 101);
	QVERIFY(unqualified);
	QVERIFY(!recordedProperty(*unqualified, "Vendor:Integer").decoded.isValid());
	QVERIFY(!recordedProperty(*unqualified, "Vendor:Integer").bento->metadataBigEndian.has_value());
	QCOMPARE(recordedProperty(*unqualified, "Vendor:Integer").encoding, number<quint32>(123));
	QCOMPARE(properties(*object(conflictingOrder, 1), "OMFI:ByteOrder").size(), 2);

	TypedBento conflictingVersions;
	conflictingVersions.head(1);
	conflictingVersions.add(1, "OMFI:Version", "omfi:VersionType", QByteArray::fromHex("0200"), true);
	conflictingVersions.add(101, "Vendor:Reference", "omfi:ObjRef", number<quint32>(1) + number<quint32>(0));
	const auto unresolved = parse(conflictingVersions.build());
	QVERIFY(unresolved.relationships.isEmpty());
	const auto *unresolvedEntry = object(unresolved, 101);
	QVERIFY(unresolvedEntry);
	QCOMPARE(recordedProperty(*unresolvedEntry, "Vendor:Reference").encoding, number<quint32>(1) + number<quint32>(0));
}

void TestCanonMdb::compactWidthsBoundariesAndIncompleteContinuations()
{
	const QByteArray objectHeader = QByteArray(1, char(1)) + number<quint32>(20) + number<quint32>(40) + number<quint32>(0);
	// Wide offsets are encoded as high and low words; even a little-endian
	// container keeps that word order. Their range must be checked before allocation.
	QByteArray toc = objectHeader + char(25) + number<quint32>(0) + number<quint32>(0) + number<quint32>(0) + number<quint32>(3);
	const auto wide = readCompact("abc", toc);
	QCOMPARE(wide.outcome, Outcome::Complete);
	QCOMPARE(wide.values.size(), 1);
	QCOMPARE(wide.values[0].bytes, QByteArray("abc"));
	toc = objectHeader + char(7) + number<quint32>(1) + number<quint32>(0) + number<quint32>(1);
	const auto outside = readCompact("abc", toc);
	QCOMPARE(outside.outcome, Outcome::Malformed);
	QCOMPARE(outside.values.size(), 1);
	QCOMPARE(outside.values[0].state, PropertyReadState::Unreadable);
	QVERIFY(outside.values[0].bytes.isEmpty());
	toc = objectHeader + char(6) + number<quint32>(0) + number<quint32>(3);
	const auto unterminated = readCompact("abc", toc);
	QCOMPARE(unterminated.outcome, Outcome::Malformed);
	QCOMPARE(unterminated.values[0].bytes, QByteArray("abc"));
	QCOMPARE(unterminated.values[0].state, PropertyReadState::Unreadable);

	toc = QByteArray(1010, char(255)) + objectHeader + char(13) + QByteArray("ABCD");
	QCOMPARE(readCompact({}, toc).outcome, Outcome::Malformed);
	toc = objectHeader + char(13) + QByteArray("ABCD") + char(24);
	toc.append(1024 - toc.size(), char(0));
	toc += char(2);
	toc += number<quint32>(41) + number<quint32>(0) + char(13) + QByteArray("EFGH");
	const auto padded = readCompact({}, toc);
	QCOMPARE(padded.outcome, Outcome::Complete);
	QCOMPARE(padded.values.size(), 2);
	QCOMPARE(padded.values[0].bytes, QByteArray("ABCD"));
	QCOMPARE(padded.values[1].bytes, QByteArray("EFGH"));
	QCOMPARE(padded.values[1].property, 41u);
}

void TestCanonMdb::fragmentedReferenceLocations()
{
	TypedBento writer;
	writer.head(1);
	writer.add(102, "OMFI:ObjID", "omfi:ObjectTag", "TRAK", true);
	constexpr quint16 count = 257;
	QByteArray arrayBytes = number(count);
	for (quint16 i = 0; i < count; ++i) arrayBytes += writer.reference(102, 1);
	// Seven-byte pieces deliberately cut through eight-byte references.
	for (qsizetype offset = 0; offset < arrayBytes.size(); offset += 7)
		writer.add(101, "Vendor:FragmentedRefs", "omfi:ObjRefArray", arrayBytes.mid(offset, 7), false, offset + 7 < arrayBytes.size());
	const QByteArray bytes = writer.build();
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.relationships.size(), count);
	for (qsizetype index = 0; index < result.relationships.size(); ++index)
	{
		const auto &relationship = result.relationships[index];
		QCOMPARE(relationship.origin, Canon::ObjectHandle(101));
		QCOMPARE(relationship.target, Canon::ObjectHandle(102));
		QByteArray locatedBytes;
		for (const auto &range : relationship.locator.ranges) locatedBytes += bytes.mid(range.offset, range.length);
		QCOMPARE(locatedBytes, writer.reference(102, 1));
		QCOMPARE(relationship.recordedReference.toMap().value("bytes").toByteArray(), locatedBytes);
		QCOMPARE(relationship.recordedReference.toMap().value("index").toLongLong(), index);
	}
}

void TestCanonMdb::classAndObjectByteOrderEvidence()
{
	TypedBento conflict;
	conflict.head(1);
	conflict.add(1, "OMFI:ObjID", "omfi:ObjectTag", "CMOB", true);
	conflict.add(101, "Vendor:Link", "omfi:ObjRef", conflict.reference(1, 1));
	const auto conflictingClass = parse(conflict.build());
	QVERIFY(conflictingClass.relationships.isEmpty());

	TypedBento wrongType;
	wrongType.head(2);
	wrongType.entries[0].type = wrongType.type("omfi:String");
	wrongType.add(101, "OMFI:OOBJ:ObjClass", "omfi:String", "MMOB", true);
	wrongType.add(101, "Vendor:Link", "omfi:ObjRef", wrongType.reference(1, 2));
	const auto mistyped = parse(wrongType.build());
	QVERIFY(mistyped.relationships.isEmpty());
	QVERIFY(object(mistyped, 101));
	QCOMPARE(object(mistyped, 101)->role, Canon::AvidObject::Role::Unknown);

	for (const int revision : {1, 2})
		for (const QByteArray &localOrder : {QByteArray("MM"), QByteArray("??")})
		{
			TypedBento writer;
			writer.head(revision);
			writer.add(101, "Vendor:Integer", "omfi:Long", number<quint32>(128), true);
			writer.add(101, "OMFI:ByteOrder", "omfi:Short", localOrder, true);
			const auto result = parse(writer.build());
			const auto *entry = object(result, 101);
			QVERIFY(entry);
			const auto &integer = recordedProperty(*entry, "Vendor:Integer");
			QCOMPARE(integer.encoding, number<quint32>(128));
			if (revision == 1)
			{
				QVERIFY(!integer.decoded.isValid());
				QVERIFY(!integer.bento->metadataBigEndian.has_value());
			}
			else
			{
				QCOMPARE(integer.decoded.toUInt(), 128u);
				QVERIFY(integer.bento->metadataBigEndian == false);
			}
		}
}

void TestCanonMdb::realFixtures_data()
{
	QTest::addColumn<QString>("path");
	for (const auto *path : {"msmMMOB.mdb", "msmMMOB_macroman.mdb", "corpus_headers/msmMMOB.mdb",
							  "corpus_headers/msmMMOB_round3.mdb", "omf/avid_supporting/msmMMOB.mdb", "omf/mc2026_audio/msmMMOB.mdb"})
		QTest::newRow(path) << QString::fromLatin1(path);
}

void TestCanonMdb::realFixtures()
{
	QFETCH(QString, path);
	const QByteArray bytes = readFixture(path);
	QVERIFY(!bytes.isEmpty());
	const auto result = parse(bytes);
	QVERIFY2(result.outcome == Outcome::Complete, qPrintable(result.diagnostics.join('\n')));
	QVERIFY(!result.objects.isEmpty());
	QSet<Canon::ObjectHandle> handles;
	qsizetype totalProperties = 0;
	qsizetype decodedFields = 0;
	for (const auto &entry : result.objects)
	{
		QVERIFY(entry.handle != 0);
		QVERIFY(!handles.contains(entry.handle));
		handles.insert(entry.handle);
		QCOMPARE(entry.snapshot, result.snapshot);
		for (const auto &value : entry.properties)
		{
			QVERIFY(value.bento);
			if (value.locator.key == number(value.bento->property, true))
				++totalProperties;
			else
			{
				// Decoded fields inside Summary values are not extra Bento TOC
				// entries. Require their recorded parent before excluding them.
				++decodedFields;
				QVERIFY(std::any_of(entry.properties.cbegin(), entry.properties.cend(), [&](const auto &parent)
				{
					return parent.bento == value.bento && parent.locator.key == number(value.bento->property, true) &&
						value.locator.name.startsWith(parent.locator.name + QLatin1Char('.'));
				}));
			}
			QVERIFY(!value.bento->tocRanges.isEmpty());
			QCOMPARE(value.locator.objectNumber, entry.handle);
			if (!value.bytesRetained) continue;
			QByteArray reconstructed;
			for (const auto &range : value.locator.ranges)
			{
				QVERIFY(range.offset >= 0 && range.length >= 0);
				QVERIFY(range.offset <= bytes.size() && range.length <= bytes.size() - range.offset);
				reconstructed += bytes.mid(range.offset, range.length);
			}
			QCOMPARE(reconstructed, value.encoding);
		}
	}
	// These genuine fixtures use fixed Bento1 TOCs without continuations.
	// Count independently from the bytes so dropping a whole value cannot pass
	// merely because all remaining values reconstruct correctly.
	QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + bytes.size() - 12), quint16(1));
	const quint32 tocOffset = qFromLittleEndian<quint32>(bytes.constData() + bytes.size() - 8);
	const quint32 tocLength = qFromLittleEndian<quint32>(bytes.constData() + bytes.size() - 4);
	QCOMPARE(tocLength % 24, 0u);
	for (quint32 offset = tocOffset; offset < tocOffset + tocLength; offset += 24)
		QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + offset + 22) & 2, 0);
	QCOMPARE(totalProperties, qsizetype(tocLength / 24));
	QCOMPARE(decodedFields, path == QLatin1String("omf/mc2026_audio/msmMMOB.mdb") ? qsizetype(24) : qsizetype(0));
	const auto *head = object(result, 1);
	QVERIFY(head);
	int extentProperties = 0;
	for (const auto &value : head->properties)
		if (value.bento && (value.bento->property == 4 || value.bento->property == 5))
		{
			++extentProperties;
			QVERIFY(!value.bytesRetained);
			QVERIFY(value.encoding.isEmpty());
			QVERIFY(!value.locator.ranges.isEmpty());
		}
	QCOMPARE(extentProperties, 2);
}

void TestCanonMdb::realNonEnglishEvidence()
{
	const QByteArray bytes = readFixture("msmMMOB.mdb");
	QVERIFY(!bytes.isEmpty());
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.objects.size(), 875);
	qsizetype totalProperties = 0;
	for (const auto &entry : result.objects) totalProperties += entry.properties.size();
	QCOMPARE(totalProperties, 1432);
	const auto *bin = object(result, 68111);
	QVERIFY(bin);
	const auto &legacy = recordedProperty(*bin, "OMFI:MCBR:MC:binName");
	const auto &unicode = recordedProperty(*bin, "OMFI:MCBR:MC:binNameUTF8");
	QCOMPARE(legacy.encoding, QByteArray::fromHex("4e9a6e20456e676c6973682062696e206e876d65aa203f3f203f00"));
	QCOMPARE(unicode.encoding, QByteArray::fromHex("4e6fcc886e20456e676c6973682062696e206e61cc816d65e284a220e4bda0e5a5bd20e6bca200"));
	QCOMPARE(legacy.locator.ranges.size(), 1);
	QCOMPARE(legacy.locator.ranges[0].offset, 2065);
	QCOMPARE(unicode.locator.ranges[0].offset, 2092);
	QCOMPARE(unicode.decoded.toString(), QString::fromUtf8(unicode.encoding.constData(), unicode.encoding.size() - 1));
	QVERIFY(legacy.textEncoding == Canon::TextEncoding::Unknown);
	QVERIFY(unicode.textEncoding == Canon::TextEncoding::Utf8);
	for (const auto &set : result.recordSets) QVERIFY(!set.pmrFileSet.has_value());
}

void TestCanonMdb::borrowedDeviceAndReceipt()
{
	TypedBento writer;
	writer.head(1);
	QByteArray bytes = writer.build();
	QBuffer source(&bytes);
	QVERIFY(source.open(QIODevice::ReadOnly));
	QVERIFY(source.seek(5));
	auto mutableReceipt = QSharedPointer<SourceSnapshot>::create();
	mutableReceipt->path = QStringLiteral("/example/msmMMOB.mdb");
	mutableReceipt->source = MetadataSource::Mdb;
	mutableReceipt->modified = QDateTime::fromSecsSinceEpoch(123456789, Qt::UTC);
	const SourceSnapshotRef inputReceipt = mutableReceipt;
	Canon::Cancellation cancellation;
	const auto result = Canon::MdbReader{}.read(source, {inputReceipt, cancellation});
	QCOMPARE(result.outcome, Outcome::Complete);
	QVERIFY(source.isOpen());
	QVERIFY(source.seek(0));
	QCOMPARE(source.read(2), bytes.left(2));
	QVERIFY(result.snapshot);
	QVERIFY(result.snapshot != inputReceipt);
	QCOMPARE(inputReceipt->readState, SourceReadState::NotRead);
	QCOMPARE(result.snapshot->source, MetadataSource::Mdb);
	QCOMPARE(result.snapshot->readState, SourceReadState::Complete);
	QCOMPARE(result.snapshot->path, inputReceipt->path);
	QCOMPARE(result.snapshot->modified, inputReceipt->modified);
	for (const auto &entry : result.objects) QCOMPARE(entry.snapshot, result.snapshot);
}

void TestCanonMdb::realOrderedRepeatedScalars()
{
	const QByteArray bytes = readFixture("msmMMOB_macroman.mdb");
	QVERIFY(!bytes.isEmpty());
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto *descriptor = object(result, 69790);
	QVERIFY(descriptor);
	QVector<const Canon::RawProperty *> lines;
	for (const auto &value : descriptor->properties)
		if (value.bento && value.bento->property == 67810) lines.append(&value);
	QCOMPARE(lines.size(), 2);
	QCOMPARE(lines[0]->locator.name, QStringLiteral("OMFI:DIDD:VideoLineMap"));
	QCOMPARE(lines[0]->encoding, QByteArray::fromHex("1a000000"));
	QCOMPARE(lines[1]->encoding, QByteArray::fromHex("00000000"));
	QCOMPARE(lines[0]->decoded.toInt(), 26);
	QCOMPARE(lines[1]->decoded.toInt(), 0);
	QCOMPARE(lines[0]->state, PropertyReadState::Present);
	QCOMPARE(lines[1]->state, PropertyReadState::Present);
}

void TestCanonMdb::truncationAndCorruption()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "Vendor:Name", "omfi:String", QByteArray("kept\0", 5));
	const QByteArray bytes = writer.build();
	for (qsizetype size = 0; size < bytes.size(); ++size)
	{
		const auto truncated = parse(bytes.left(size));
		QVERIFY2(truncated.outcome != Outcome::Complete, qPrintable(QStringLiteral("Accepted truncation at %1").arg(size)));
	}
	QByteArray unsupported = bytes;
	qToLittleEndian<quint16>(3, unsupported.data() + unsupported.size() - 12);
	QCOMPARE(parse(unsupported).outcome, Outcome::Unsupported);
	QByteArray corrupt = bytes;
	const quint32 tocOffset = qFromLittleEndian<quint32>(corrupt.constData() + corrupt.size() - 8);
	qToLittleEndian<quint32>(0xfffffff0, corrupt.data() + tocOffset + 3 * 24 + 12);
	const auto badRange = parse(corrupt);
	QVERIFY(badRange.outcome != Outcome::Complete);
	QVERIFY(!badRange.diagnostics.isEmpty());
	QVERIFY(!badRange.objects.isEmpty());
}

void TestCanonMdb::shortReadsCancellationAndFailures()
{
	TypedBento writer;
	writer.head(1);
	writer.add(101, "Vendor:Name", "omfi:String", QByteArray("read in pieces\0", 15));
	writer.add(101, "Vendor:Next", "omfi:String", QByteArray("another\0", 8));
	QByteArray bytes = writer.build();
	Canon::Cancellation cancellation;
	ShortBuffer shortReads(&bytes);
	QVERIFY(shortReads.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
	const auto complete = Canon::MdbReader{}.read(shortReads, {{}, cancellation});
	QCOMPARE(complete.outcome, Outcome::Complete);
	QVERIFY(shortReads.readBytes > 24);
	ShortBuffer broken(&bytes);
	QVERIFY(broken.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
	broken.ioFailure = true;
	QCOMPARE(Canon::MdbReader{}.read(broken, {{}, cancellation}).outcome, Outcome::IoError);
	ShortBuffer cancelled(&bytes);
	QVERIFY(cancelled.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
	cancelled.cancellation = &cancellation;
	cancelled.cancelAfterBytes = 40;
	QCOMPARE(Canon::MdbReader{}.read(cancelled, {{}, cancellation}).outcome, Outcome::Cancelled);
	Canon::Cancellation lateCancellation;
	ShortBuffer partial(&bytes);
	QVERIFY(partial.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
	partial.cancellation = &lateCancellation;
	partial.cancelAfterBytes = 24 + qFromLittleEndian<quint32>(bytes.constData() + bytes.size() - 4) + 17;
	const auto late = Canon::MdbReader{}.read(partial, {{}, lateCancellation});
	QCOMPARE(late.outcome, Outcome::Cancelled);
	const auto *retained = object(late, 101);
	QVERIFY(retained);
	QCOMPARE(retained->properties.size(), 2);
	QCOMPARE(retained->properties[0].encoding, QByteArray("read in pieces\0", 15));
	QCOMPARE(retained->properties[1].encoding, QByteArray("an"));
	QCOMPARE(retained->properties[1].state, PropertyReadState::Unreadable);
	QBuffer fresh(&bytes);
	QVERIFY(fresh.open(QIODevice::ReadOnly));
	QCOMPARE(Canon::MdbReader{}.read(fresh, {{}, cancellation}).outcome, Outcome::Cancelled);
	Canon::Cancellation active;
	QBuffer closed(&bytes);
	QVERIFY(Canon::MdbReader{}.read(closed, {{}, active}).outcome != Outcome::Complete);
	SequentialBuffer sequential(&bytes);
	QVERIFY(sequential.open(QIODevice::ReadOnly));
	QVERIFY(Canon::MdbReader{}.read(sequential, {{}, active}).outcome != Outcome::Complete);
	QBuffer textMode(&bytes);
	QVERIFY(textMode.open(QIODevice::ReadOnly | QIODevice::Text));
	QVERIFY(Canon::MdbReader{}.read(textMode, {{}, active}).outcome != Outcome::Complete);
	ChangingSizeBuffer changing(&bytes);
	QVERIFY(changing.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
	QCOMPARE(Canon::MdbReader{}.read(changing, {{}, active}).outcome, Outcome::Incomplete);
}

QTEST_GUILESS_MAIN(TestCanonMdb)
#include "tst_canonmdb.moc"
