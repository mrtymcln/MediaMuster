// Independently authored MXF packets exercise the new reader's evidence contract.
// Real Avid excerpts remain explicitly incomplete files; they still provide usable
// metadata. Guarded devices catch accidental reads of large essence payloads.

#include "canon/mxfreader.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QTest>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace
{
	using Outcome = Canon::ParsedSource::Outcome;
	using Container = Canon::ParsedSource::Container;

	QByteArray hex(const char *text) { return QByteArray::fromHex(text); }
	template <typename T>
	QByteArray number(T value)
	{
		QByteArray result(sizeof(T), '\0');
		qToBigEndian(value, result.data());
		return result;
	}
	QByteArray ber(quint64 length)
	{
		if (length < 128)
			return QByteArray(1, char(length));
		QByteArray digits;
		while (length)
		{
			digits.prepend(char(length & 255));
			length >>= 8;
		}
		return QByteArray(1, char(0x80 | digits.size())) + digits;
	}
	QByteArray klv(const QByteArray &key, const QByteArray &value)
	{
		return key + ber(quint64(value.size())) + value;
	}
	QByteArray text(const QString &value)
	{
		QByteArray result;
		for (const QChar character : value)
			result += number(character.unicode());
		return result + QByteArray(2, '\0');
	}
	const QByteArray instanceUl = hex("060e2b34010101010101150200000000");
	const QByteArray nameUl = hex("060e2b34010101010103030201000000");
	const QByteArray descriptorUl = hex("060e2b34010101020601010402030000");
	const QByteArray sampleRateUl = hex("060e2b34010101010406010100000000");
	const QByteArray unknownUl = hex("aabbccdd010203040506070809101112");
	const QByteArray materialKey = hex("060e2b34025301010d01010101013600");
	const QByteArray sourceKey = hex("060e2b34025301010d01010101013700");
	const QByteArray descriptorKey = hex("060e2b34025301010d01010101012800");
	const QByteArray essenceKey = hex("060e2b34010201010d01030115010101");
	const QByteArray privateEssenceKey = hex("060e2b34010201010e04030115010801");
	const QByteArray idA(16, 'a'), idB(16, 'b');

	using Mapping = QPair<quint16, QByteArray>;
	QByteArray primer(const QVector<Mapping> &mappings)
	{
		QByteArray value = number(quint32(mappings.size())) + number<quint32>(18);
		for (const auto &entry : mappings)
			value += number(entry.first) + entry.second;
		return klv(hex("060e2b34020501010d01020101050100"), value);
	}
	QByteArray item(quint16 tag, const QByteArray &value, bool berLength = false)
	{
		return number(tag) + (berLength ? ber(quint64(value.size())) : number(quint16(value.size()))) + value;
	}
	QByteArray partition(quint8 kind, quint64 offset, quint64 previous, quint64 footer,
						 quint64 metadataBytes = 0, quint32 bodySid = 0)
	{
		QByteArray key = hex("060e2b34020501010d01020101020400");
		key[13] = char(kind);
		QByteArray value = number<quint16>(1) + number<quint16>(3) + number<quint32>(1) + number(offset) + number(previous) + number(footer) + number(metadataBytes) + number<quint64>(0) + number<quint32>(0) + number<quint64>(0) + number(bodySid) + hex("060e2b34040101020d01020110030000") + number<quint32>(0) + number<quint32>(16);
		return klv(key, value);
	}
	QByteArray document(const QByteArray &headerMetadata, const QByteArray &footerMetadata = {})
	{
		const quint64 footerOffset = quint64(partition(2, 0, 0, 0).size() + headerMetadata.size());
		return partition(2, 0, 0, footerOffset, quint64(headerMetadata.size())) + headerMetadata + partition(4, footerOffset, 0, footerOffset, quint64(footerMetadata.size())) + footerMetadata;
	}
	Canon::ParsedSource parse(QByteArray bytes)
	{
		QBuffer source(&bytes);
		source.open(QIODevice::ReadOnly);
		Canon::Cancellation cancellation;
		return Canon::MxfReader{}.read(source, {{}, cancellation});
	}
	QVector<const Canon::RawProperty *> properties(const Canon::ParsedSource &result, const QByteArray &ul)
	{
		QVector<const Canon::RawProperty *> found;
		for (const auto &object : result.objects)
			for (const auto &property : object.properties)
				if (property.mxf && property.mxf->mappedAuid == ul)
					found.append(&property);
		return found;
	}
	QByteArray encodedRanges(const QByteArray &bytes, const Canon::PropertyLocator &locator)
	{
		QByteArray result;
		for (const auto &range : locator.ranges)
			result += bytes.mid(range.offset, range.length);
		return result;
	}

	// No large backing allocation: unwritten portions are virtual zeros. Reads from
	// forbidden essence ranges fail, including any speculative broad read across them.
	class GuardedDevice final : public QIODevice
	{
	public:
		struct Piece
		{
			qint64 offset;
			QByteArray bytes;
		};
		QVector<Piece> pieces;
		QVector<Canon::ByteRange> forbidden;
		qint64 extent = 0, bytesRead = 0, stopOffset = -1;
		bool forbiddenRead = false, changeSize = false;
		Canon::Cancellation *cancellation = nullptr;
		qint64 cancelAfter = -1;
		explicit GuardedDevice(const QByteArray &bytes = {})
		{
			pieces.append({0, bytes});
			extent = bytes.size();
			open(QIODevice::ReadOnly | QIODevice::Unbuffered);
		}
		qint64 size() const override { return extent + (changeSize && bytesRead > 0 ? 1 : 0); }
		bool isSequential() const override { return false; }
		bool seek(qint64 offset) override { return offset >= 0 && QIODevice::seek(offset); }

	protected:
		qint64 readData(char *destination, qint64 count) override
		{
			const qint64 begin = pos();
			if (begin >= extent)
				return 0;
			count = qMin(count, extent - begin);
			if (stopOffset >= 0 && begin >= stopOffset)
			{
				setErrorString(QStringLiteral("Injected metadata read failure"));
				return -1;
			}
			if (begin < stopOffset && count > stopOffset - begin)
				count = stopOffset - begin;
			for (const auto &range : forbidden)
				if (begin < range.offset + range.length && range.offset < begin + count)
				{
					forbiddenRead = true;
					setErrorString(QStringLiteral("Reader attempted to read essence"));
					return -1;
				}
			std::memset(destination, 0, size_t(count));
			for (const auto &piece : pieces)
			{
				const qint64 from = qMax(begin, piece.offset);
				const qint64 end = qMin(begin + count, piece.offset + piece.bytes.size());
				if (end > from)
					std::memcpy(destination + from - begin,
								piece.bytes.constData() + from - piece.offset, size_t(end - from));
			}
			bytesRead += count;
			if (cancellation && cancelAfter >= 0 && bytesRead >= cancelAfter)
				cancellation->cancel();
			return count;
		}
		qint64 writeData(const char *, qint64) override { return -1; }
	};
}

class TestCanonMxf final : public QObject
{
	Q_OBJECT
private slots:
	void unknownAndRepeatedProperties();
	void relocatedPrimerAndNoTagGuess();
	void ambiguousPrimerRetainsBytes();
	void berLocalLengths();
	void repeatedPartitionsKeepReferenceScope();
	void textAndRationalHaveExplicitEncodings();
	void indirectValuesPreserveRecordedByteOrder();
	void shortIndirectValueIsUnreadable();
	void duplicateIdentityDoesNotGuessReference();
	void contradictoryIdentityStillPoisonsCandidate();
	void emptyReferenceBatchRetainsNativeHeader();
	void weakReferenceDoesNotGuessInstanceUidTarget();
	void malformedAndTruncatedPackets();
	void malformedLocalItemKeepsEarlierEvidence();
	void borrowedDeviceAndReceipt();
	void cancellationAndReadFailure();
	void changingSourceCannotClaimComplete();
	void sparseEssenceIsNotRead_data();
	void sparseEssenceIsNotRead();
	void corruptHeaderCountCannotConsumeEssence_data();
	void corruptHeaderCountCannotConsumeEssence();
	void unknownHeaderPayloadStaysRangeOnly_data();
	void unknownHeaderPayloadStaysRangeOnly();
	void unsupportedMajorVersionDoesNotInventObjects();
	void genericStreamPartitionIsRetained();
	void missingPrimerMappingQualifiesSource();
	void emptyHeaderPartitionIsMalformed();
	void realAvidFixtures_data();
	void realAvidFixtures();
};

void TestCanonMxf::unknownAndRepeatedProperties()
{
	const QByteArray metadata = primer({{0x9100, instanceUl}, {0x9101, unknownUl}}) + klv(materialKey, item(0x9100, idA) + item(0x9101, "first") + item(0x9101, "second"));
	const QByteArray bytes = document(metadata);
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, Container::Mxf);
	const auto found = properties(result, unknownUl);
	QCOMPARE(found.size(), 2);
	QCOMPARE(found[0]->encoding, QByteArray("first"));
	QCOMPARE(found[1]->encoding, QByteArray("second"));
	for (const auto *property : found)
	{
		QVERIFY(!property->decoded.isValid());
		QCOMPARE(property->state, PropertyReadState::Present);
		QCOMPARE(property->mxf->localTag, quint32(0x9101));
		QVERIFY(property->mxf->primerOffset >= 0);
		QVERIFY(!property->mxf->framingRanges.isEmpty());
		QCOMPARE(encodedRanges(bytes, property->locator), property->encoding);
	}
}

void TestCanonMxf::relocatedPrimerAndNoTagGuess()
{
	const auto mapped = parse(document(primer({{0x8020, nameUl}}) + klv(materialKey, item(0x8020, text(QStringLiteral("Mapped clip"))))));
	QCOMPARE(mapped.outcome, Outcome::Complete);
	const auto names = properties(mapped, nameUl);
	QCOMPARE(names.size(), 1);
	QCOMPARE(names.first()->decoded.toString(), QStringLiteral("Mapped clip"));
	QCOMPARE(names.first()->mxf->localTag, quint32(0x8020));
	const QByteArray recorded = text(QStringLiteral("Not established as Name"));
	const auto unprimed = parse(document(klv(materialKey, item(0x4402, recorded))));
	QVERIFY(properties(unprimed, nameUl).isEmpty());
	bool retained = false;
	for (const auto &object : unprimed.objects)
		for (const auto &property : object.properties)
			if (property.mxf && property.mxf->localTag == 0x4402)
			{
				retained = true;
				QVERIFY(property.mxf->mappedAuid.isEmpty());
				QCOMPARE(property.encoding, recorded);
				QVERIFY(!property.decoded.isValid());
			}
	QVERIFY(retained);
}

void TestCanonMxf::ambiguousPrimerRetainsBytes()
{
	const QByteArray recorded = text(QStringLiteral("Ambiguous"));
	const auto result = parse(document(primer({{0x8000, nameUl}, {0x8000, unknownUl}}) + klv(materialKey, item(0x8000, recorded))));
	QVERIFY(result.outcome != Outcome::Complete);
	bool retained = false;
	for (const auto &object : result.objects)
		for (const auto &property : object.properties)
			if (property.mxf && property.mxf->localTag == 0x8000)
			{
				retained = true;
				QCOMPARE(property.encoding, recorded);
				QVERIFY(!property.decoded.isValid());
			}
	QVERIFY(retained);
}

void TestCanonMxf::berLocalLengths()
{
	QByteArray key = materialKey;
	key[5] = char(0x13); // Two-byte local tags and BER value lengths.
	const QByteArray value(260, 'x');
	const auto result = parse(document(primer({{0x8000, unknownUl}}) + klv(key, item(0x8000, value, true))));
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto found = properties(result, unknownUl);
	QCOMPARE(found.size(), 1);
	QCOMPARE(found.first()->encoding, value);
}

void TestCanonMxf::repeatedPartitionsKeepReferenceScope()
{
	const QByteArray graph = primer({{0x3c0a, instanceUl}, {0x4701, descriptorUl}}) + klv(sourceKey, item(0x3c0a, idA) + item(0x4701, idB)) + klv(descriptorKey, item(0x3c0a, idB));
	const auto result = parse(document(graph, graph));
	QCOMPARE(result.outcome, Outcome::Complete);
	QVector<const Canon::AvidObject *> packages, descriptors;
	for (const auto &object : result.objects)
	{
		if (object.recordedIdentity == idA)
			packages.append(&object);
		if (object.recordedIdentity == idB)
			descriptors.append(&object);
	}
	QCOMPARE(packages.size(), 2);
	QCOMPARE(descriptors.size(), 2);
	QVERIFY(packages[0]->handle != packages[1]->handle);
	QVERIFY(packages[0]->mxf->partitionOffset != packages[1]->mxf->partitionOffset);
	for (const auto *package : packages)
	{
		bool matched = false;
		for (const auto &relation : result.relationships)
			if (relation.origin == package->handle && relation.recordedReference.toByteArray() == idB)
			{
				for (const auto *descriptor : descriptors)
					if (relation.target == descriptor->handle)
					{
						QCOMPARE(descriptor->mxf->partitionOffset, package->mxf->partitionOffset);
						matched = true;
					}
			}
		QVERIFY(matched);
	}
}

void TestCanonMxf::textAndRationalHaveExplicitEncodings()
{
	const QString clip = QString::fromUtf8("Nön English — 你好 漢");
	const QByteArray encodedRate = number<qint32>(24000) + number<qint32>(1001);
	const auto result = parse(document(primer({{0x4402, nameUl}, {0x3001, sampleRateUl}}) + klv(materialKey, item(0x4402, text(clip))) + klv(descriptorKey, item(0x3001, encodedRate))));
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto names = properties(result, nameUl);
	QCOMPARE(names.size(), 1);
	QCOMPARE(names.first()->decoded.toString(), clip);
	QVERIFY(names.first()->textEncoding.has_value());
	QCOMPARE(*names.first()->textEncoding, Canon::TextEncoding::Utf16BE);
	const auto rates = properties(result, sampleRateUl);
	QCOMPARE(rates.size(), 1);
	QCOMPARE(rates.first()->encoding, encodedRate);
	const auto rate = rates.first()->decoded.toMap();
	QCOMPARE(rate.value(QStringLiteral("Numerator")).toInt(), 24000);
	QCOMPARE(rate.value(QStringLiteral("Denominator")).toInt(), 1001);
}

void TestCanonMxf::indirectValuesPreserveRecordedByteOrder()
{
	const QByteArray valueUl = hex("060e2b3401010102030201020a010000");
	const QByteArray taggedKey = hex("060e2b34025301010d01010101013f00");
	const QByteArray bePrefix = hex("420110020000000000060e2b3401040101");
	const QByteArray lePrefix = hex("4c0002100100000000060e2b3401040101");
	const QString name = QString::fromUtf8("Jürgen 漢");
	const QByteArray beText = text(name);
	QByteArray leText = beText;
	for (qsizetype i = 0; i < leText.size(); i += 2)
		std::swap(leText[i], leText[i + 1]);
	const QByteArray payload = primer({{0x5003, valueUl}}) + klv(taggedKey, item(0x5003, bePrefix + beText)) + klv(taggedKey, item(0x5003, lePrefix + leText));
	const auto result = parse(document(payload));
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto values = properties(result, valueUl);
	QCOMPARE(values.size(), 2);
	QCOMPARE(values[0]->decoded.toString(), name);
	QCOMPARE(values[1]->decoded.toString(), name);
	QVERIFY(values[0]->textEncoding.has_value());
	QVERIFY(values[1]->textEncoding.has_value());
	QCOMPARE(*values[0]->textEncoding, Canon::TextEncoding::Utf16BE);
	QCOMPARE(*values[1]->textEncoding, Canon::TextEncoding::Utf16LE);
	QCOMPARE(values[0]->encoding, bePrefix + beText);
	QCOMPARE(values[1]->encoding, lePrefix + leText);
}

void TestCanonMxf::shortIndirectValueIsUnreadable()
{
	const QByteArray valueUl = hex("060e2b3401010102030201020a010000");
	const QByteArray taggedKey = hex("060e2b34025301010d01010101013f00");
	const QByteArray shortValue(15, 'x');
	const auto result = parse(document(primer({{0x5003, valueUl}}) + klv(taggedKey, item(0x5003, shortValue))));
	QCOMPARE(result.outcome, Outcome::Incomplete);
	const auto values = properties(result, valueUl);
	QCOMPARE(values.size(), 1);
	QCOMPARE(values.first()->state, PropertyReadState::Unreadable);
	QCOMPARE(values.first()->encoding, shortValue);
	QVERIFY(!values.first()->decoded.isValid());
	QVERIFY(!values.first()->interpretation.isEmpty());
}

void TestCanonMxf::duplicateIdentityDoesNotGuessReference()
{
	const QByteArray graph = primer({{0x3c0a, instanceUl}, {0x4701, descriptorUl}}) + klv(sourceKey, item(0x3c0a, idA) + item(0x4701, idB)) + klv(descriptorKey, item(0x3c0a, idB)) + klv(descriptorKey, item(0x3c0a, idB));
	const auto result = parse(document(graph));
	qsizetype referenceCount = 0;
	for (const auto &relation : result.relationships)
		if (relation.recordedReference.toByteArray() == idB)
		{
			++referenceCount;
			QCOMPARE(relation.target, Canon::ObjectHandle(0));
			QVERIFY(!relation.explanation.isEmpty());
		}
	QCOMPARE(referenceCount, 1);
}

void TestCanonMxf::contradictoryIdentityStillPoisonsCandidate()
{
	const QByteArray graph = primer({{0x3c0a, instanceUl}, {0x4701, descriptorUl}}) + klv(sourceKey, item(0x3c0a, idA) + item(0x4701, idB)) + klv(descriptorKey, item(0x3c0a, QByteArray(16, 'c')) + item(0x3c0a, idB)) + klv(descriptorKey, item(0x3c0a, idB));
	const auto result = parse(document(graph));
	qsizetype referenceCount = 0;
	for (const auto &relation : result.relationships)
		if (relation.recordedReference.toByteArray() == idB)
		{
			++referenceCount;
			QCOMPARE(relation.target, Canon::ObjectHandle(0));
		}
	QCOMPARE(referenceCount, 1);
}

void TestCanonMxf::emptyReferenceBatchRetainsNativeHeader()
{
	const QByteArray tracksUl = hex("060e2b34010101020601010406050000");
	const QByteArray emptyHeader(8, '\0');
	const auto result = parse(document(primer({{0x4403, tracksUl}}) + klv(materialKey, item(0x4403, emptyHeader))));
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto values = properties(result, tracksUl);
	QCOMPARE(values.size(), 1);
	QCOMPARE(values.first()->encoding, emptyHeader);
	QVERIFY(values.first()->decoded.isValid());
	QVERIFY(values.first()->decoded.toList().isEmpty());
}

void TestCanonMxf::weakReferenceDoesNotGuessInstanceUidTarget()
{
	const QByteArray parentUl = hex("060e2b34010101020601010701000000");
	const QByteArray classKey = hex("060e2b34025301010d01010102010000");
	const QByteArray graph = primer({{0x3c0a, instanceUl}, {0x0008, parentUl}}) + klv(classKey, item(0x3c0a, idA) + item(0x0008, idB)) + klv(descriptorKey, item(0x3c0a, idB));
	const auto result = parse(document(graph));
	QCOMPARE(result.outcome, Outcome::Complete);
	qsizetype referenceCount = 0;
	for (const auto &relation : result.relationships)
		if (relation.locator.key == parentUl)
		{
			++referenceCount;
			QCOMPARE(relation.recordedReference.toByteArray(), idB);
			QCOMPARE(relation.target, Canon::ObjectHandle(0));
			QVERIFY(!relation.explanation.isEmpty());
		}
	QCOMPARE(referenceCount, 1);
}

void TestCanonMxf::malformedAndTruncatedPackets()
{
	const QByteArray prefix = partition(2, 0, 0, 0);
	QCOMPARE(parse(prefix + materialKey + QByteArray(1, char(0x80))).outcome, Outcome::Malformed);
	QCOMPARE(parse(prefix + materialKey + QByteArray(1, char(0x89)) + QByteArray(9, '\1')).outcome, Outcome::Malformed);
	QVERIFY(parse(prefix + materialKey.left(7)).outcome != Outcome::Complete);
	QVERIFY(parse(prefix + materialKey + ber(512) + QByteArray(8, '\0')).outcome != Outcome::Complete);
	QVERIFY(parse(QByteArray("not MXF")).outcome != Outcome::Complete);
}

void TestCanonMxf::malformedLocalItemKeepsEarlierEvidence()
{
	const QByteArray body = item(0x8000, "kept") + number<quint16>(0x8000) + number<quint16>(99) + "cut";
	const auto result = parse(document(primer({{0x8000, unknownUl}}) + klv(materialKey, body)));
	QVERIFY(result.outcome != Outcome::Complete);
	const auto found = properties(result, unknownUl);
	QVERIFY(!found.isEmpty());
	QCOMPARE(found.first()->encoding, QByteArray("kept"));
}

void TestCanonMxf::borrowedDeviceAndReceipt()
{
	QByteArray bytes = document(primer({{0x3c0a, instanceUl}}) + klv(materialKey, item(0x3c0a, idA)));
	QBuffer source(&bytes);
	QVERIFY(source.open(QIODevice::ReadOnly));
	QVERIFY(source.seek(5));
	auto input = QSharedPointer<SourceSnapshot>::create();
	input->path = QStringLiteral("/receipt/example.mxf");
	input->source = MetadataSource::Pmr;
	Canon::Cancellation cancellation;
	const auto result = Canon::MxfReader{}.read(source, {input, cancellation});
	QCOMPARE(result.outcome, Outcome::Complete);
	QVERIFY(source.isOpen());
	QVERIFY(result.snapshot);
	QCOMPARE(result.snapshot->path, input->path);
	QCOMPARE(result.snapshot->source, MetadataSource::Mxf);
	QCOMPARE(input->source, MetadataSource::Pmr);
	for (const auto &object : result.objects)
		QCOMPARE(object.snapshot, result.snapshot);
}

void TestCanonMxf::cancellationAndReadFailure()
{
	const QByteArray bytes = document(primer({{0x8000, unknownUl}}) + klv(materialKey, item(0x8000, QByteArray(128, 'x'))));
	Canon::Cancellation before;
	before.cancel();
	GuardedDevice cancelled(bytes);
	QCOMPARE(Canon::MxfReader{}.read(cancelled, {{}, before}).outcome, Outcome::Cancelled);
	Canon::Cancellation during;
	GuardedDevice interrupted(bytes);
	interrupted.cancellation = &during;
	interrupted.cancelAfter = 32;
	QCOMPARE(Canon::MxfReader{}.read(interrupted, {{}, during}).outcome, Outcome::Cancelled);
	Canon::Cancellation running;
	GuardedDevice broken(bytes);
	broken.stopOffset = 35;
	QCOMPARE(Canon::MxfReader{}.read(broken, {{}, running}).outcome, Outcome::IoError);
}

void TestCanonMxf::changingSourceCannotClaimComplete()
{
	GuardedDevice source(document(primer({{0x8000, unknownUl}}) + klv(materialKey, item(0x8000, "value"))));
	source.changeSize = true;
	Canon::Cancellation cancellation;
	const auto result = Canon::MxfReader{}.read(source, {{}, cancellation});
	QVERIFY(result.outcome != Outcome::Complete);
	QVERIFY(!result.diagnostics.isEmpty());
}

void TestCanonMxf::sparseEssenceIsNotRead_data()
{
	QTest::addColumn<QByteArray>("key");
	QTest::newRow("generic-container") << essenceKey;
	QTest::newRow("avid-alpha-private") << privateEssenceKey;
}

void TestCanonMxf::sparseEssenceIsNotRead()
{
	QFETCH(QByteArray, key);
	const QByteArray metadata = primer({{0x3c0a, instanceUl}}) + klv(materialKey, item(0x3c0a, idA));
	const qint64 sampleSize = qint64(5) * 1024 * 1024 * 1024;
	const qint64 bodyOffset = partition(2, 0, 0, 0).size() + metadata.size();
	const QByteArray body = partition(3, quint64(bodyOffset), 0, 0, 0, 1);
	const QByteArray essenceFraming = key + ber(quint64(sampleSize));
	const qint64 sampleOffset = bodyOffset + body.size() + essenceFraming.size();
	const qint64 footerOffset = sampleOffset + sampleSize;
	const QByteArray beginning = partition(2, 0, 0, quint64(footerOffset), quint64(metadata.size())) + metadata + partition(3, quint64(bodyOffset), 0, quint64(footerOffset), 0, 1) + essenceFraming;
	const QByteArray footer = partition(4, quint64(footerOffset), quint64(bodyOffset), quint64(footerOffset));
	GuardedDevice source(beginning);
	source.extent = footerOffset + footer.size();
	source.pieces.append({footerOffset, footer});
	source.forbidden.append({sampleOffset, sampleSize});
	Canon::Cancellation cancellation;
	const auto result = Canon::MxfReader{}.read(source, {{}, cancellation});
	QVERIFY2(!source.forbiddenRead, qPrintable(source.errorString()));
	QCOMPARE(result.outcome, Outcome::Complete);
	QVERIFY(source.bytesRead < 16384);
	QVERIFY(!result.objects.isEmpty());
}

void TestCanonMxf::corruptHeaderCountCannotConsumeEssence_data()
{
	QTest::addColumn<QByteArray>("key");
	QTest::newRow("generic-essence") << essenceKey;
	QTest::newRow("avid-alpha-essence") << privateEssenceKey;
	QTest::newRow("gc-system-ber-local-lengths") << hex("060e2b34021301010d01030104010100");
	QTest::newRow("index-table-ber-local-lengths") << hex("060e2b34021301010d01020101100100");
}

void TestCanonMxf::corruptHeaderCountCannotConsumeEssence()
{
	QFETCH(QByteArray, key);
	const QByteArray metadata = primer({{0x3c0a, instanceUl}}) + klv(materialKey, item(0x3c0a, idA));
	const qint64 sampleSize = qint64(5) * 1024 * 1024 * 1024;
	const QByteArray essenceFraming = key + ber(quint64(sampleSize));
	const qint64 sampleOffset = partition(2, 0, 0, 0).size() + metadata.size() + essenceFraming.size();
	const qint64 footerOffset = sampleOffset + sampleSize;
	const quint64 corruptCount = quint64(metadata.size() + essenceFraming.size() + sampleSize);
	const QByteArray beginning = partition(2, 0, 0, quint64(footerOffset), corruptCount) + metadata + essenceFraming;
	const QByteArray footer = partition(4, quint64(footerOffset), 0, quint64(footerOffset));
	GuardedDevice source(beginning);
	source.extent = footerOffset + footer.size();
	source.pieces.append({footerOffset, footer});
	source.forbidden.append({sampleOffset, sampleSize});
	Canon::Cancellation cancellation;
	const auto result = Canon::MxfReader{}.read(source, {{}, cancellation});
	QVERIFY2(!source.forbiddenRead, qPrintable(source.errorString()));
	QCOMPARE(result.outcome, Outcome::Malformed);
	QVERIFY(source.bytesRead < 16384);
	QVERIFY(!result.objects.isEmpty());
}

void TestCanonMxf::unknownHeaderPayloadStaysRangeOnly_data()
{
	QTest::addColumn<QByteArray>("unknownKey");
	QTest::addColumn<bool>("unsupported");
	QTest::newRow("unknown-nonlocal-header-payload") << hex("112233445566778899aabbccddeeff00") << false;
	QTest::newRow("unsupported-local-set-coding") << hex("060e2b34020301010d01010101013600") << true;
}

void TestCanonMxf::unknownHeaderPayloadStaysRangeOnly()
{
	QFETCH(QByteArray, unknownKey);
	QFETCH(bool, unsupported);
	const QByteArray metadata = primer({{0x3c0a, instanceUl}}) + klv(materialKey, item(0x3c0a, idA));
	const qint64 payloadSize = qint64(5) * 1024 * 1024 * 1024;
	const QByteArray opaqueFraming = unknownKey + ber(quint64(payloadSize));
	const qint64 payloadOffset = partition(2, 0, 0, 0).size() + metadata.size() + opaqueFraming.size();
	const qint64 footerOffset = payloadOffset + payloadSize;
	const quint64 metadataCount = quint64(metadata.size() + opaqueFraming.size() + payloadSize);
	const QByteArray beginning = partition(2, 0, 0, quint64(footerOffset), metadataCount) + metadata + opaqueFraming;
	const QByteArray footer = partition(4, quint64(footerOffset), 0, quint64(footerOffset));
	GuardedDevice source(beginning);
	source.extent = footerOffset + footer.size();
	source.pieces.append({footerOffset, footer});
	source.forbidden.append({payloadOffset, payloadSize});
	Canon::Cancellation cancellation;
	const auto result = Canon::MxfReader{}.read(source, {{}, cancellation});
	QVERIFY2(!source.forbiddenRead, qPrintable(source.errorString()));
	QCOMPARE(result.outcome, unsupported ? Outcome::Unsupported : Outcome::Complete);
	QVERIFY(source.bytesRead < 16384);
	QVERIFY(!result.objects.isEmpty());
	bool retainedRange = false;
	QVector<const Canon::RawProperty *> allProperties;
	for (const auto &property : result.unownedProperties)
		allProperties.append(&property);
	for (const auto &object : result.objects)
		for (const auto &property : object.properties)
			allProperties.append(&property);
	for (const auto *candidate : allProperties)
	{
		const auto &property = *candidate;
		if (property.locator.key == unknownKey && !property.bytesRetained)
		{
			retainedRange = true;
			QCOMPARE(property.state, PropertyReadState::Present);
			QVERIFY(property.encoding.isEmpty());
			QCOMPARE(property.locator.ranges.size(), 1);
			QCOMPARE(property.locator.ranges.first().offset, payloadOffset);
			QCOMPARE(property.locator.ranges.first().length, payloadSize);
			QVERIFY(!property.interpretation.isEmpty());
		}
	}
	QVERIFY(retainedRange);
}

void TestCanonMxf::unsupportedMajorVersionDoesNotInventObjects()
{
	const QByteArray metadata = primer({{0x3c0a, instanceUl}}) + klv(materialKey, item(0x3c0a, idA));
	QByteArray bytes = document(metadata);
	// The authored 88-byte Partition Pack has a one-byte BER length.
	bytes[17] = 0;
	bytes[18] = 2;
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Unsupported);
	QVERIFY(result.objects.isEmpty());
	QVERIFY(!result.unownedProperties.isEmpty());
}

void TestCanonMxf::genericStreamPartitionIsRetained()
{
	const QByteArray metadata = primer({});
	const quint64 bodyOffset = quint64(partition(2, 0, 0, 0).size() + metadata.size());
	const quint64 footerOffset = bodyOffset + quint64(partition(3, 0, 0, 0).size());
	QByteArray streamPartition = partition(3, bodyOffset, 0, footerOffset, 0, 1);
	streamPartition[14] = char(0x11);
	const QByteArray bytes = partition(2, 0, 0, footerOffset, quint64(metadata.size())) + metadata + streamPartition + partition(4, footerOffset, bodyOffset, footerOffset);
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	bool found = false;
	for (const auto &property : result.unownedProperties)
		if (property.locator.key == streamPartition.first(16) && property.decoded.toMap().contains(QStringLiteral("bodySID")))
		{
			found = true;
			QCOMPARE(property.decoded.toMap().value(QStringLiteral("bodySID")).toUInt(), 1u);
		}
	QVERIFY(found);
}

void TestCanonMxf::missingPrimerMappingQualifiesSource()
{
	const QByteArray recorded = text(QStringLiteral("Unmapped local name"));
	const auto result = parse(document(primer({{0x3c0a, instanceUl}}) + klv(materialKey, item(0x3c0a, idA) + item(0x4402, recorded))));
	QCOMPARE(result.outcome, Outcome::Incomplete);
	bool found = false;
	for (const auto &object : result.objects)
		for (const auto &property : object.properties)
			if (property.mxf && property.mxf->localTag == 0x4402)
			{
				found = true;
				QCOMPARE(property.encoding, recorded);
				QVERIFY(property.mxf->mappedAuid.isEmpty());
				QVERIFY(!property.decoded.isValid());
			}
	QVERIFY(found);
}

void TestCanonMxf::emptyHeaderPartitionIsMalformed()
{
	QCOMPARE(parse(document({})).outcome, Outcome::Malformed);
}

void TestCanonMxf::realAvidFixtures_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<bool>("excerpt");
	const QDir root(QStringLiteral(FIXTURES_DIR));
	QTest::newRow("full-Avid-tone") << root.filePath(QStringLiteral("TONE_100A01.EA7D504A.611740.mxf")) << false;
	for (const QString &folder : {QStringLiteral("avid_headers"), QStringLiteral("corpus_headers")})
	{
		QDir directory(root.filePath(folder));
		const auto files = directory.entryList({QStringLiteral("*.mxf")}, QDir::Files, QDir::Name);
		for (const auto &name : files)
			QTest::newRow(qPrintable(folder + '/' + name)) << directory.filePath(name) << true;
	}
}

void TestCanonMxf::realAvidFixtures()
{
	QFETCH(QString, path);
	QFETCH(bool, excerpt);
	QFile source(path);
	QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
	Canon::Cancellation cancellation;
	const auto result = Canon::MxfReader{}.read(source, {{}, cancellation});
	QCOMPARE(result.container, Container::Mxf);
	QCOMPARE(result.outcome, excerpt ? Outcome::Incomplete : Outcome::Complete);
	QVERIFY(!result.objects.isEmpty());
	QVERIFY(result.snapshot);
	QCOMPARE(result.snapshot->source, MetadataSource::Mxf);
	const auto names = properties(result, nameUl);
	QVERIFY(!names.isEmpty());
	bool dictionary = false;
	for (const auto &object : result.objects)
		if (object.mxf && object.mxf->key == hex("060e2b34025301010d01010102250000"))
			dictionary = true;
	QVERIFY(dictionary);
	for (const auto &object : result.objects)
	{
		QCOMPARE(object.snapshot, result.snapshot);
		for (const auto &property : object.properties)
		{
			if (!property.bytesRetained)
				continue;
			QByteArray exact;
			for (const auto &range : property.locator.ranges)
			{
				QVERIFY(range.offset >= 0 && range.length >= 0 && range.offset <= source.size() && range.length <= source.size() - range.offset);
				QVERIFY(source.seek(range.offset));
				exact += source.read(range.length);
			}
			QCOMPARE(exact, property.encoding);
		}
	}
}

QTEST_APPLESS_MAIN(TestCanonMxf)
#include "tst_canonmxf.moc"
