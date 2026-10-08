#include <QtTest>
#include <QBuffer>
#include <QFile>
#include <QtEndian>
#include "canon/pmrreader.h"
#include "canon/discoveryengine.h"
#include "canon/projection.h"

namespace
{
using Outcome = Canon::ParsedSource::Outcome;
const QByteArray fileId = QByteArray::fromHex("060a2b340101010501010f10130000004a507dea741106907a361e6a605d3613");
const QByteArray masterId = QByteArray::fromHex("060a2b340101010501010f1013000000d2467dea7411069091901e6a605d3613");

template <typename T> void number(QByteArray &bytes, T value, bool big)
{
	char encoded[sizeof(T)];
	if (big) qToBigEndian(value, encoded);
	else qToLittleEndian(value, encoded);
	bytes.append(encoded, sizeof(T));
}
void string(QByteArray &bytes, const QByteArray &value, bool big)
{
	number<quint16>(bytes, quint16(value.size()), big);
	bytes.append(value);
}
QByteArray header(qint32 version, quint32 count, bool big = false)
{
	QByteArray bytes;
	number<quint32>(bytes, 0x7a9, big);
	number<qint32>(bytes, version, big);
	number<quint32>(bytes, count, big);
	return bytes;
}
QByteArray record(qint32 version, bool big = false, const QByteArray &name = "take.mxf",
				  const QByteArray &project = "Project", const QByteArray &identity = fileId)
{
	QByteArray bytes = identity.left(version <= 7 ? 8 : 32);
	string(bytes, version == 16 ? QByteArray(2, '\0') + name : name, big);
	if (version != 1)
	{
		string(bytes, project, big);
		bytes.append(masterId.left(version <= 7 ? 8 : 32));
	}
	number<quint32>(bytes, 0x12345678, big);
	return bytes;
}
Canon::ParsedSource parse(QByteArray bytes)
{
	QBuffer source(&bytes);
	source.open(QIODevice::ReadOnly);
	Canon::Cancellation cancellation;
	return Canon::PmrReader{}.read(source, {{}, cancellation});
}
const Canon::RawProperty &rawProperty(const Canon::AvidObject &object, const QString &name)
{
	for (const auto &entry : object.properties)
		if (entry.locator.name == name) return entry;
	qFatal("Expected property was not preserved");
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
	qint64 cancelAfter = -1;
	bool ioFailure = false;
protected:
	qint64 readData(char *data, qint64 count) override
	{
		if (ioFailure) { setErrorString(QStringLiteral("injected I/O failure")); return -1; }
		const qint64 read = QBuffer::readData(data, qMin(count, qint64(1)));
		if (cancellation && pos() >= cancelAfter) cancellation->cancel();
		return read;
	}
};
}

class TestCanonPmr final : public QObject
{
	Q_OBJECT
private slots:
	void layouts_data();
	void layouts();
	void independentSetsAndDuplicateRecords();
	void textEncodingProvenance();
	void recordedTextAndIdentityAreNotGuessed();
	void nullAndAbsentAreDifferent();
	void malformedUnicodeAndCapacities();
	void everyTruncationAndHugeCount();
	void unsupportedRanges();
	void shortReadsIoAndCancellation();
	void realDrivePmrs();
	void realFixtures_data();
	void realFixtures();
};

void TestCanonPmr::layouts_data()
{
	QTest::addColumn<qint32>("version");
	QTest::addColumn<bool>("big");
	for (const qint32 version : {qint32(-1), qint32(0), qint32(1), qint32(2), qint32(7), qint32(8)})
		for (const bool big : {false, true})
			QTest::newRow(qPrintable(QStringLiteral("v%1-%2").arg(version).arg(big ? "BE" : "LE"))) << version << big;
}
void TestCanonPmr::layouts()
{
	QFETCH(qint32, version);
	QFETCH(bool, big);
	const QByteArray bytes = header(version, 1, big) + record(version, big);
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.container, Canon::ParsedSource::Container::Pmr);
	QCOMPARE(result.recordSets.size(), 1);
	QVERIFY(result.recordSets[0].framingComplete);
	QCOMPARE(result.recordSets[0].version, version);
	QVERIFY(result.recordSets[0].pmrFileSet == Canon::PmrFileSet::Legacy);
	QCOMPARE(result.recordSets[0].name, QStringLiteral("Legacy"));
	QCOMPARE(result.objects.size(), 1);
	const auto &object = result.objects[0];
	QCOMPARE(object.recordedIdentity, fileId.left(version <= 7 ? 8 : 32));
	QCOMPARE(object.snapshot, result.snapshot);
	QCOMPARE(object.snapshot->readState, SourceReadState::Complete);
	QCOMPARE(rawProperty(object, "Filename").decoded.toString(), QStringLiteral("take.mxf"));
	QCOMPARE(rawProperty(object, "ModificationWord").decoded.toUInt(), 0x12345678u);
	QCOMPARE(result.relationships.size(), version == 1 ? 0 : 1);
	for (const auto &entry : object.properties)
		for (const auto &range : entry.locator.ranges)
			QCOMPARE(bytes.mid(range.offset, range.length), entry.encoding);
	if (version != 1)
	{
		QCOMPARE(result.relationships[0].target, Canon::ObjectHandle(0));
		QCOMPARE(result.relationships[0].recordedReference.toByteArray(), masterId.left(version <= 7 ? 8 : 32));
		QCOMPARE(result.relationships[0].referenceEncoding, object.identityEncoding);
	}
}
void TestCanonPmr::independentSetsAndDuplicateRecords()
{
	QByteArray bytes = header(8, 2) + record(8) + record(8);
	number<qint32>(bytes, 16, false);
	number<quint32>(bytes, 1, false);
	bytes += record(16, false, "different.mxf", "Other", QByteArray(32, 'x'));
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(result.recordSets.size(), 2);
	QCOMPARE(result.recordSets[0].objects.size(), 2);
	QCOMPARE(result.recordSets[1].objects.size(), 1);
	QVERIFY(result.recordSets[1].pmrFileSet == Canon::PmrFileSet::Unicode);
	QCOMPARE(result.objects.size(), 3);
	QCOMPARE(result.objects[0].recordedIdentity, result.objects[1].recordedIdentity);
	QVERIFY(result.objects[0].handle != result.objects[1].handle);
	QCOMPARE(result.objects[2].recordedIdentity, QByteArray(32, 'x'));
	QCOMPARE(rawProperty(result.objects[2], "Filename").decoded.toString(), QStringLiteral("different.mxf"));
	bytes = header(8, 1) + record(8);
	number<qint32>(bytes, 16, false);
	number<quint32>(bytes, 0, false);
	const auto empty = parse(bytes);
	QCOMPARE(empty.outcome, Outcome::Complete);
	QCOMPARE(empty.objects.size(), 1);
	QVERIFY(empty.recordSets[1].framingComplete);
}
void TestCanonPmr::textEncodingProvenance()
{
	QByteArray bytes = header(8, 1) + record(8, false, "plain.mxf", QByteArray("\xa7", 1));
	number<qint32>(bytes, 16, false);
	number<quint32>(bytes, 1, false);
	bytes += record(16, false, "plain.mxf", QByteArray("\xa7", 1));
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	for (const auto &set : result.recordSets)
	{
		QVERIFY(set.pmrFileSet.has_value());
		const auto &object = result.objects[qsizetype(set.objects[0] - 1)];
		const auto &filename = rawProperty(object, "Filename");
		const bool unicode = set.pmrFileSet == Canon::PmrFileSet::Unicode;
		QVERIFY(filename.textEncoding == (unicode ? Canon::TextEncoding::Utf8 : Canon::TextEncoding::Ascii));
		QVERIFY(filename.textEncodingBasis == (unicode ? EvidenceBasis::Recorded : EvidenceBasis::Derived));
		const auto &project = rawProperty(object, "Project");
		QVERIFY(project.textEncoding == Canon::TextEncoding::Unknown);
		QVERIFY(!project.textEncodingBasis.has_value());
		QVERIFY(!project.decoded.isValid());
		for (const auto &name : {"FileMobId", "MasterMobId", "ModificationWord"})
		{
			QVERIFY(!rawProperty(object, name).textEncoding.has_value());
			QVERIFY(!rawProperty(object, name).textEncodingBasis.has_value());
		}
	}
	QByteArray invalid = header(8, 0);
	number<qint32>(invalid, 16, false);
	number<quint32>(invalid, 1, false);
	invalid += record(16, false, QByteArray::fromHex("ff"));
	const auto malformed = parse(invalid);
	const auto &filename = rawProperty(malformed.objects[0], "Filename");
	QCOMPARE(filename.state, PropertyReadState::Unreadable);
	QVERIFY(filename.textEncoding == Canon::TextEncoding::Utf8);
	QVERIFY(filename.textEncodingBasis == EvidenceBasis::Recorded);
	// An unreadable value still retains the encoding required by its source layout.
}

void TestCanonPmr::recordedTextAndIdentityAreNotGuessed()
{
	const QByteArray name("a\0tail", 6);
	const auto result = parse(header(8, 1) + record(8, false, name, QByteArray("\xa7", 1), QByteArray(32, '\0')));
	QCOMPARE(result.outcome, Outcome::Complete);
	const auto &object = result.objects[0];
	QCOMPARE(object.recordedIdentity, QByteArray(32, '\0'));
	QCOMPARE(rawProperty(object, "Filename").decoded.toString(), QStringLiteral("a"));
	QCOMPARE(rawProperty(object, "Filename").encoding.mid(2), name);
	QCOMPARE(rawProperty(object, "Project").state, PropertyReadState::Present);
	QVERIFY(!rawProperty(object, "Project").decoded.isValid());
	QCOMPARE(rawProperty(object, "Project").encoding.mid(2), QByteArray("\xa7", 1));
}
void TestCanonPmr::nullAndAbsentAreDifferent()
{
	QByteArray bytes = header(1, 1) + fileId.left(8);
	number<quint16>(bytes, 0xffff, false);
	number<quint32>(bytes, 0, false);
	const auto result = parse(bytes);
	QCOMPARE(result.outcome, Outcome::Complete);
	QCOMPARE(rawProperty(result.objects[0], "Filename").state, PropertyReadState::Present);
	QCOMPARE(rawProperty(result.objects[0], "Filename").encoding, QByteArray::fromHex("ffff"));
	QCOMPARE(rawProperty(result.objects[0], "MasterMobId").state, PropertyReadState::Absent);
	QVERIFY(rawProperty(result.objects[0], "Filename").textEncoding == Canon::TextEncoding::Unknown);
	QVERIFY(!rawProperty(result.objects[0], "MasterMobId").textEncoding.has_value());
	QVERIFY(rawProperty(result.objects[0], "MasterMobId").locator.ranges.isEmpty());
	const Canon::Cancellation cancellation;
	const auto projection = Canon::projectPmr(result, cancellation);
	QCOMPARE(projection.files.size(), 1);
	const auto &evidence = projection.files.first().evidence;
	for (const auto field : {MediaProperty::Project, MediaProperty::MasterMobId, MediaProperty::Compression})
	{
		const auto status = evidence.readStatus(field, result.snapshot);
		QCOMPARE(status.state, PropertyReadState::Absent);
		QCOMPARE(status.reason, PropertyReadReason::NotStoredByFormat);
	}
}
void TestCanonPmr::malformedUnicodeAndCapacities()
{
	const auto unicode = [](QByteArray name) {
		QByteArray bytes = header(8, 0);
		number<qint32>(bytes, 16, false);
		number<quint32>(bytes, 1, false);
		return bytes + record(16, false, name);
	};
	for (const QByteArray &bad : {QByteArray::fromHex("c3"), QByteArray::fromHex("ff"), QByteArray(1024, 'a')})
	{
		const auto result = parse(unicode(bad));
		QCOMPARE(result.outcome, Outcome::Malformed);
		QVERIFY(result.recordSets[1].framingComplete);
		QCOMPARE(rawProperty(result.objects[0], "Filename").state, PropertyReadState::Unreadable);
		QCOMPARE(rawProperty(result.objects[0], "Filename").encoding.mid(4), bad);
		QCOMPARE(result.relationships.size(), 1);
	}
	QByteArray big = header(2, 0, true);
	number<qint32>(big, 16, true);
	number<quint32>(big, 1, true);
	big += record(16, true, QByteArray::fromHex("c39f"));
	const auto bigResult = parse(big);
	QCOMPARE(bigResult.outcome, Outcome::Complete);
	QCOMPARE(rawProperty(bigResult.objects[0], "Filename").decoded.toString(), QString::fromUtf8("\xc3\x9f"));
	QByteArray bytes = unicode("name");
	bytes[54] = 1; // Header12 + extension8 + id32 + length2: first reserved byte.
	QCOMPARE(parse(bytes).outcome, Outcome::Malformed);
	bytes[54] = 0;
	bytes[55] = 7; // The second reserved byte is not constrained by the observed reader.
	QCOMPARE(parse(bytes).outcome, Outcome::Complete);
	for (const int length : {2047, 2048, 65534})
	{
		const auto result = parse(header(8, 1) + record(8, false, QByteArray(length, 'a')));
		QCOMPARE(result.outcome, length < 2048 ? Outcome::Complete : Outcome::Malformed);
		QCOMPARE(rawProperty(result.objects[0], "Filename").encoding.size(), length + 2);
		QCOMPARE(result.relationships.size(), 1);
	}
}
void TestCanonPmr::everyTruncationAndHugeCount()
{
	QByteArray complete = header(8, 1) + record(8);
	number<qint32>(complete, 16, false);
	number<quint32>(complete, 1, false);
	complete += record(16);
	const int baseEnd = (header(8, 1) + record(8)).size();
	for (int size = 0; size < complete.size(); ++size)
	{
		const auto result = parse(complete.left(size));
		QCOMPARE(result.outcome, size == baseEnd ? Outcome::Complete : Outcome::Incomplete);
		for (const auto &object : result.objects)
			for (const auto &entry : object.properties)
				for (const auto &range : entry.locator.ranges)
					QVERIFY(range.offset + range.length <= size);
	}
	const auto huge = parse(header(8, 0xffffffff));
	QCOMPARE(huge.outcome, Outcome::Incomplete);
	QCOMPARE(huge.objects.size(), 1); // Only the encountered partial record; no reserve(count).
	QCOMPARE(huge.recordSets[0].declaredCount, 0xffffffffu);
}
void TestCanonPmr::unsupportedRanges()
{
	const auto result = parse(header(9, 0) + QByteArray(50000, 'x'));
	QCOMPARE(result.outcome, Outcome::Unsupported);
	QVERIFY(result.objects.isEmpty());
	const auto &tail = result.unownedProperties.last();
	QVERIFY(!tail.bytesRetained);
	QVERIFY(tail.encoding.isEmpty());
	QCOMPARE(tail.locator.ranges[0].offset, qint64(8));
	QCOMPARE(tail.locator.ranges[0].length, qint64(50004));
	QCOMPARE(parse(QByteArray(12, 'x')).outcome, Outcome::Malformed);
	QByteArray extension = header(8, 1) + record(8);
	number<qint32>(extension, 17, false);
	extension += "unknown";
	const auto unknown = parse(extension);
	QCOMPARE(unknown.outcome, Outcome::Unsupported);
	QCOMPARE(unknown.objects.size(), 1);
	QVERIFY(unknown.recordSets[0].framingComplete);
}
void TestCanonPmr::shortReadsIoAndCancellation()
{
	QByteArray bytes = header(8, 1) + record(8);
	ShortBuffer source(&bytes);
	source.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
	Canon::Cancellation cancellation;
	const auto receipt = SourceSnapshotRef::create(SourceSnapshot{MetadataSource::Pmr, "source.pmr", {}, SourceReadState::NotRead});
	QCOMPARE(Canon::PmrReader{}.read(source, {receipt, cancellation}).outcome, Outcome::Complete);
	QCOMPARE(receipt->readState, SourceReadState::NotRead);
	source.ioFailure = true;
	QCOMPARE(Canon::PmrReader{}.read(source, {receipt, cancellation}).outcome, Outcome::IoError);
	source.ioFailure = false;
	source.cancellation = &cancellation;
	source.cancelAfter = 15;
	QCOMPARE(Canon::PmrReader{}.read(source, {receipt, cancellation}).outcome, Outcome::Cancelled);
	source.close();
	QCOMPARE(Canon::PmrReader{}.read(source, {{}, cancellation}).outcome, Outcome::Cancelled);
	Canon::Cancellation fresh;
	QCOMPARE(Canon::PmrReader{}.read(source, {{}, fresh}).outcome, Outcome::IoError);
	QBuffer textSource(&bytes);
	textSource.open(QIODevice::ReadOnly | QIODevice::Text);
	QCOMPARE(Canon::PmrReader{}.read(textSource, {{}, fresh}).outcome, Outcome::IoError);
	SequentialBuffer sequential(&bytes);
	sequential.open(QIODevice::ReadOnly);
	QCOMPARE(Canon::PmrReader{}.read(sequential, {{}, fresh}).outcome, Outcome::IoError);
}
void TestCanonPmr::realDrivePmrs()
{
	const QString roots = qEnvironmentVariable("MEDIAMUSTER_CANON_REAL_SCAN_ROOTS");
	if (roots.isEmpty()) QSKIP("Opt-in read-only local/EDIT PMR audit.");
	Canon::Cancellation cancellation;
	const auto discovery = Canon::DiscoveryEngine{}.discover({roots.split(';', Qt::SkipEmptyParts), true}, cancellation);
	QVERIFY(discovery.discoveryComplete);
	qsizetype databases = 0;
	qsizetype objects = 0;
	for (const auto &candidate : discovery.candidates)
	{
		if (candidate.hint != Canon::SourceCandidate::ReaderHint::Pmr) continue;
		QFile source(candidate.path);
		QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(candidate.path));
		const QFileInfo before(source);
		const auto receipt = SourceSnapshotRef::create(SourceSnapshot{MetadataSource::Pmr, candidate.path,
			before.lastModified(), SourceReadState::NotRead});
		const auto result = Canon::PmrReader{}.read(source, {receipt, cancellation});
		QVERIFY2(result.outcome == Outcome::Complete, qPrintable(candidate.path + ": " + result.diagnostics.join("; ")));
		const QFileInfo after(candidate.path);
		QCOMPARE(after.size(), before.size());
		QCOMPARE(after.lastModified(), before.lastModified());
		++databases;
		objects += result.objects.size();
		QStringList counts;
		for (const auto &set : result.recordSets)
			counts.append(QStringLiteral("%1=%2/%3").arg(set.name).arg(set.objects.size()).arg(set.declaredCount));
		qInfo().noquote() << candidate.path << counts.join(", ") << "preservedObjects=" << result.objects.size();
	}
	QVERIFY(databases > 0);
	qInfo() << "PMR databases=" << databases << "preserved source records=" << objects;
}

void TestCanonPmr::realFixtures_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<int>("records");
	QTest::newRow("tone") << QStringLiteral("msmFMID.pmr") << 1;
	QTest::newRow("corpus") << QStringLiteral("corpus_headers/msmFMID.pmr") << 435;
	QTest::newRow("round3") << QStringLiteral("corpus_headers/msmFMID_round3.pmr") << 360;
	QTest::newRow("omf-supporting") << QStringLiteral("omf/avid_supporting/msmFMID.pmr") << 80;
	QTest::newRow("omf-audio") << QStringLiteral("omf/mc2026_audio/msmFMID.pmr") << 2;
}
void TestCanonPmr::realFixtures()
{
	QFETCH(QString, path);
	QFETCH(int, records);
	QFile source(QStringLiteral(FIXTURES_DIR "/") + path);
	QVERIFY(source.open(QIODevice::ReadOnly));
	const QByteArray original = source.readAll(); // Test fixtures only; reader never readAll().
	Canon::Cancellation cancellation;
	const auto result = Canon::PmrReader{}.read(source, {{}, cancellation});
	QVERIFY2(result.outcome == Outcome::Complete, qPrintable(result.diagnostics.join("; ")));
	QCOMPARE(result.recordSets.size(), 2);
	QCOMPARE(result.recordSets[0].declaredCount, quint32(records));
	QCOMPARE(result.recordSets[1].declaredCount, quint32(records));
	QCOMPARE(result.objects.size(), records * 2);
	QCOMPARE(result.relationships.size(), records * 2);
	for (const auto &set : result.recordSets) QVERIFY(set.framingComplete);
	for (const auto &object : result.objects)
		for (const auto &entry : object.properties)
			for (const auto &range : entry.locator.ranges)
				QCOMPARE(original.mid(range.offset, range.length), entry.encoding);
}
QTEST_GUILESS_MAIN(TestCanonPmr)
#include "tst_canonpmr.moc"
