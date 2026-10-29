#include "bella/pmrparser.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>
#include <QtEndian>
#include <array>
#include <atomic>
#include <optional>
#include <utility>
#include <vector>

namespace
{
	using Read = Bella::PmrReadResult;
	using Check = Bella::PmrCheckResult;

	struct Record
	{
		QByteArray filename{"take.mxf"};
		QByteArray project{"Project"};
		QByteArray fileId;
		QByteArray masterId;
		quint32 modification = 0x12345678;
		bool nullFilename = false;
		bool nullProject = false;
		char firstMarker = '\0';
		char secondMarker = '\0';
	};

	QByteArray expectedFileId(qint32 version)
	{
		return QByteArray::fromHex(version <= 7
			? "060a2b340101010101010f001300000044332211d4c3b2a1060e2b347f7f2a80"
			: "000102030405060708090a0b0c0d0e0f443322116655887718191a1b1c1d1e1f");
	}

	QByteArray expectedMasterId(qint32 version)
	{
		return QByteArray::fromHex(version <= 7
			? "060a2b340101010101010f0013000000887766550817f6e5060e2b347f7f2a80"
			: "202122232425262728292a2b2c2d2e2fd4c3b2a1f6e5081738393a3b3c3d3e3f");
	}

	QByteArray storedId(qint32 version, bool bigEndian, bool master)
	{
		// Explicit authored wire values, independent of Bella's normalization.
		// Legacy IDs have two 32-bit words; modern IDs have 32/16/16-bit fields.
		if (version <= 7)
			return QByteArray::fromHex(master
				? (bigEndian ? "55667788e5f61708" : "887766550817f6e5")
				: (bigEndian ? "11223344a1b2c3d4" : "44332211d4c3b2a1"));
		if (master)
			return bigEndian
				? QByteArray::fromHex("202122232425262728292a2b2c2d2e2fa1b2c3d4e5f6170838393a3b3c3d3e3f")
				: expectedMasterId(version);
		return bigEndian
			? QByteArray::fromHex("000102030405060708090a0b0c0d0e0f112233445566778818191a1b1c1d1e1f")
			: expectedFileId(version);
	}

	template <typename T>
	void appendNumber(QByteArray &bytes, T value, bool bigEndian)
	{
		std::array<char, sizeof(T)> encoded{};
		if (bigEndian)
			qToBigEndian<T>(value, encoded.data());
		else
			qToLittleEndian<T>(value, encoded.data());
		bytes.append(encoded.data(), static_cast<qsizetype>(encoded.size()));
	}

	void appendText(QByteArray &bytes, const QByteArray &text, bool nullText, bool bigEndian)
	{
		if (nullText)
		{
			appendNumber<quint16>(bytes, 0xffff, bigEndian);
			return;
		}
		if (text.size() >= 0xffff)
			qFatal("Authored test text does not fit a non-null PMR length word");
		appendNumber<quint16>(bytes, static_cast<quint16>(text.size()), bigEndian);
		bytes.append(text);
	}

	QByteArray recordBytes(qint32 version, bool bigEndian, const Record &record)
	{
		QByteArray bytes = record.fileId.isEmpty() ? storedId(version, bigEndian, false) : record.fileId;
		QByteArray filename = record.filename;
		if (version == 16)
		{
			QByteArray framed;
			framed.append(record.firstMarker);
			framed.append(record.secondMarker);
			framed.append(filename);
			filename = std::move(framed);
		}
		appendText(bytes, filename, record.nullFilename, bigEndian);
		if (version != 1)
		{
			appendText(bytes, record.project, record.nullProject, bigEndian);
			bytes.append(record.masterId.isEmpty() ? storedId(version, bigEndian, true) : record.masterId);
		}
		appendNumber<quint32>(bytes, record.modification, bigEndian);
		return bytes;
	}

	void appendSet(QByteArray &bytes, qint32 version, bool bigEndian, const QVector<Record> &records)
	{
		appendNumber<qint32>(bytes, version, bigEndian);
		appendNumber<quint32>(bytes, static_cast<quint32>(records.size()), bigEndian);
		for (const auto &record : records)
			bytes.append(recordBytes(version, bigEndian, record));
	}

	QByteArray pmrBytes(qint32 version, bool bigEndian, const QVector<Record> &records)
	{
		QByteArray bytes;
		appendNumber<quint32>(bytes, 0x7a9, bigEndian);
		appendSet(bytes, version, bigEndian, records);
		return bytes;
	}

	Bella::PmrReport parseBytes(const QByteArray &bytes)
	{
		QTemporaryFile file;
		if (!file.open() || file.write(bytes) != static_cast<qint64>(bytes.size()) || !file.flush())
			qFatal("Cannot prepare temporary PMR test file: %s", qPrintable(file.errorString()));
		const QString path = file.fileName();
		file.close();
		const std::atomic_bool stopRequested{false};
		return Bella::parsePmr(path, stopRequested);
	} // The file is removed before the caller inspects the owned report.

	Bella::PmrStamp stamp(std::optional<qint64> size, std::optional<qint64> modifiedMs)
	{
		return {size, modifiedMs ? QDateTime::fromMSecsSinceEpoch(*modifiedMs, Qt::UTC) : QDateTime{}};
	}

	QByteArray recordFingerprint(const Bella::PmrSet &set)
	{
		// Independent oracle convention recorded by the fixture-evidence audit:
		// version/count and each complete field length are unsigned 32-bit BE.
		QCryptographicHash hash(QCryptographicHash::Sha256);
		hash.addData(QByteArrayView("Bella PMR full records v1\n"));
		const auto addNumber = [&hash](quint32 value)
		{
			std::array<char, sizeof(quint32)> encoded{};
			qToBigEndian<quint32>(value, encoded.data());
			hash.addData(QByteArrayView(encoded.data(), static_cast<qsizetype>(encoded.size())));
		};
		const auto addField = [&hash, &addNumber](QByteArrayView value)
		{
			addNumber(static_cast<quint32>(value.size()));
			hash.addData(value);
		};
		addNumber(static_cast<quint32>(set.version));
		addNumber(static_cast<quint32>(set.entries.size()));
		for (const auto &entry : set.entries)
		{
			addField(entry.fileMobId.bytes());
			addField(entry.filename.toUtf8());
			addField(entry.project.toUtf8());
			addField(entry.masterMobId ? entry.masterMobId->bytes() : QByteArrayView{});
		}
		return hash.result().toHex();
	}
}

class TestBellaPmr final : public QObject
{
	Q_OBJECT
private slots:
	void supportedLayouts_data();
	void supportedLayouts();
	void signedBranchBaselineIsNotHistoricalEvidence();
	void preferredUnicodeSet_data();
	void preferredUnicodeSet();
	void legacyTextAlwaysUsesMacRoman();
	void unicodeMarkersAndStrictText();
	void nulAndNullText();
	void wholeSetFailureDiscardsAllItsEntries();
	void safeBaseFramingCanReachPreferredSet();
	void brokenBaseFramingCannotFindAnAppendedSet();
	void everyBaseTruncationAndImpossibleCount();
	void everyTruncatedKnownUnicodeSetFallsBack();
	void unsupportedExtensionsAndTailsKeepReferenceOnlyWholeSets();
	void diagnosticsSeparateFileContextFromReasons();
	void emptySetsHaveIndependentReferenceCoverage();
	void unsupportedBaseAndBadMagic();
	void completeIdsAndIgnoredModificationWord();
	void nativeTextCapacitiesAreNotWireLimits();
	void stampComparisonUsesAvailableFacts();
	void preCancelledAndIoFailures();
	void reportsOwnTheirResults();
	void genuineLittleEndianFixtures_data();
	void genuineLittleEndianFixtures();
};

void TestBellaPmr::supportedLayouts_data()
{
	QTest::addColumn<int>("version");
	QTest::addColumn<bool>("bigEndian");
	for (int version = 1; version <= 8; ++version)
		for (const bool bigEndian : {false, true})
			QTest::newRow(qPrintable(QStringLiteral("authored-v%1-%2")
				.arg(version).arg(bigEndian ? QStringLiteral("BE") : QStringLiteral("LE"))))
				<< version << bigEndian;
}

void TestBellaPmr::supportedLayouts()
{
	QFETCH(int, version);
	QFETCH(bool, bigEndian);
	const QByteArray bytes = pmrBytes(version, bigEndian, {Record{}});
	const auto report = parseBytes(bytes);
	QVERIFY(report.reading == Read::Completed);
	QVERIFY(report.sizeDateCheck == Check::Verified);
	QVERIFY(report.records.canUsePmrMetadata);
	QVERIFY(report.records.referenceCoverageComplete);
	QVERIFY(report.records.knownSet);
	QCOMPARE(report.records.knownSet->version, qint32(version));
	QCOMPARE(report.records.knownSet->entries.size(), qsizetype{1});
	QVERIFY(report.source.capturedStamp.sizeBytes);
	QCOMPARE(*report.source.capturedStamp.sizeBytes, qint64(bytes.size()));
	QVERIFY(report.source.capturedStamp.modified.isValid());
	const auto &entry = report.records.knownSet->entries.front();
	QCOMPARE(entry.filename, QStringLiteral("take.mxf"));
	QCOMPARE(entry.fileMobId.bytes().toByteArray(), expectedFileId(version));
	if (version == 1)
	{
		QVERIFY(entry.project.isEmpty());
		QVERIFY(!entry.masterMobId);
	}
	else
	{
		QCOMPARE(entry.project, QStringLiteral("Project"));
		QVERIFY(entry.masterMobId);
		QCOMPARE(entry.masterMobId->bytes().toByteArray(), expectedMasterId(version));
	}
}

void TestBellaPmr::signedBranchBaselineIsNotHistoricalEvidence()
{
	// Preserve the inspected signed branch without describing these authored
	// words as specimens of genuine historical PMR releases.
	for (const qint32 version : {qint32{-1}, qint32{0}})
		for (const bool bigEndian : {false, true})
		{
			const auto report = parseBytes(pmrBytes(version, bigEndian, {Record{}}));
			QVERIFY(report.reading == Read::Completed);
			QVERIFY(report.records.canUsePmrMetadata);
			QVERIFY(report.records.referenceCoverageComplete);
			QVERIFY(report.records.knownSet);
			QCOMPARE(report.records.knownSet->version, version);
			QCOMPARE(report.records.knownSet->entries.front().fileMobId.bytes().toByteArray(), expectedFileId(version));
			QVERIFY(!report.diagnostics.isEmpty());
		}
}

void TestBellaPmr::preferredUnicodeSet_data()
{
	QTest::addColumn<int>("baseVersion");
	QTest::addColumn<bool>("bigEndian");
	for (int version = 1; version <= 8; ++version)
		for (const bool bigEndian : {false, true})
			QTest::newRow(qPrintable(QStringLiteral("authored-base%1-v16-%2")
				.arg(version).arg(bigEndian ? QStringLiteral("BE") : QStringLiteral("LE"))))
				<< version << bigEndian;
}

void TestBellaPmr::preferredUnicodeSet()
{
	QFETCH(int, baseVersion);
	QFETCH(bool, bigEndian);
	Record older;
	older.filename = "older.mxf";
	older.project = "Older project";
	Record preferred;
	preferred.filename = QByteArray::fromHex("c39f2e6d7866");
	preferred.project = QByteArray::fromHex("a7");
	QByteArray bytes = pmrBytes(baseVersion, bigEndian, {older, older});
	appendSet(bytes, 16, bigEndian, {preferred}); // Counts legitimately differ.
	const auto report = parseBytes(bytes);
	QVERIFY(report.reading == Read::Completed);
	QVERIFY(report.records.canUsePmrMetadata);
	QVERIFY(report.records.referenceCoverageComplete);
	QVERIFY(report.records.knownSet);
	QCOMPARE(report.records.knownSet->version, qint32{16});
	QCOMPARE(report.records.knownSet->entries.size(), qsizetype{1});
	const auto &entry = report.records.knownSet->entries.front();
	QCOMPARE(entry.filename, QString::fromUtf8("ß.mxf"));
	QCOMPARE(entry.project, QString::fromUtf8("ß"));
	QCOMPARE(entry.fileMobId.bytes().toByteArray(), expectedFileId(16));
	QVERIFY(entry.masterMobId);
	QCOMPARE(entry.masterMobId->bytes().toByteArray(), expectedMasterId(16));
}

void TestBellaPmr::legacyTextAlwaysUsesMacRoman()
{
	Record legacy;
	legacy.filename = QByteArray::fromHex("c39f"); // Valid UTF-8 also; MacRoman means √ü.
	legacy.project = QByteArray::fromHex("c39f");
	const auto older = parseBytes(pmrBytes(8, false, {legacy}));
	QVERIFY(older.records.canUsePmrMetadata);
	QVERIFY(older.records.referenceCoverageComplete);
	QVERIFY(older.records.knownSet);
	const QString macRoman = QString(QChar(char16_t{0x221a})) + QChar(char16_t{0x00fc});
	QCOMPARE(older.records.knownSet->entries.front().filename, macRoman);
	QCOMPARE(older.records.knownSet->entries.front().project, macRoman);

	Record unicode;
	unicode.filename = QByteArray::fromHex("c39f");
	unicode.project = QByteArray::fromHex("c39f");
	QByteArray bytes = pmrBytes(8, false, {Record{}});
	appendSet(bytes, 16, false, {unicode});
	const auto preferred = parseBytes(bytes);
	QVERIFY(preferred.records.canUsePmrMetadata);
	QVERIFY(preferred.records.referenceCoverageComplete);
	QVERIFY(preferred.records.knownSet);
	QCOMPARE(preferred.records.knownSet->entries.front().filename, QString::fromUtf8("ß"));
	QCOMPARE(preferred.records.knownSet->entries.front().project, macRoman);
}

void TestBellaPmr::unicodeMarkersAndStrictText()
{
	Record older;
	older.filename = "older.mxf";
	Record unicode;
	unicode.secondMarker = 7; // Only the first of the two marker bytes is constrained.
	QByteArray valid = pmrBytes(8, false, {older});
	appendSet(valid, 16, false, {unicode});
	const auto accepted = parseBytes(valid);
	QVERIFY(accepted.records.canUsePmrMetadata);
	QVERIFY(accepted.records.referenceCoverageComplete);
	QVERIFY(accepted.records.knownSet);
	QCOMPARE(accepted.records.knownSet->version, qint32{16});

	const std::array<QByteArray, 6> invalidUtf8{{
		QByteArray::fromHex("c3"), QByteArray::fromHex("ff"), QByteArray::fromHex("c080"),
		QByteArray::fromHex("eda080"), QByteArray::fromHex("f4908080"), QByteArray::fromHex("e228a1"),
	}};
	for (const auto &text : invalidUtf8)
	{
		Record bad;
		bad.filename = text;
		QByteArray bytes = pmrBytes(8, false, {older});
		appendSet(bytes, 16, false, {bad});
		const auto report = parseBytes(bytes);
		QVERIFY(report.reading == Read::Completed);
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(!report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->version, qint32{8});
		QCOMPARE(report.records.knownSet->entries.front().filename, QStringLiteral("older.mxf"));
		QVERIFY(!report.diagnostics.isEmpty());
	}
	unicode.firstMarker = 1;
	QByteArray badMarker = pmrBytes(8, false, {older});
	appendSet(badMarker, 16, false, {unicode});
	const auto rejected = parseBytes(badMarker);
	QVERIFY(rejected.records.canUsePmrMetadata);
	QVERIFY(!rejected.records.referenceCoverageComplete);
	QVERIFY(rejected.records.knownSet);
	QCOMPARE(rejected.records.knownSet->version, qint32{8});

	// A counted v16 filename must contain both marker bytes even when no
	// actual text follows. This authored payload provides just one byte.
	QByteArray shortMarker = pmrBytes(8, false, {older});
	appendNumber<qint32>(shortMarker, 16, false);
	appendNumber<quint32>(shortMarker, 1, false);
	shortMarker.append(storedId(16, false, false));
	appendText(shortMarker, QByteArray(1, '\0'), false, false);
	appendText(shortMarker, "Project", false, false);
	shortMarker.append(storedId(16, false, true));
	appendNumber<quint32>(shortMarker, 0, false);
	const auto shortResult = parseBytes(shortMarker);
	QVERIFY(shortResult.records.canUsePmrMetadata);
	QVERIFY(!shortResult.records.referenceCoverageComplete);
	QVERIFY(shortResult.records.knownSet);
	QCOMPARE(shortResult.records.knownSet->version, qint32{8});
}

void TestBellaPmr::nulAndNullText()
{
	for (const bool unicode : {false, true})
	{
		Record record;
		record.filename = QByteArray("a\0\xff", 3);
		record.project = QByteArray("P\0\xff", 3);
		QByteArray bytes = unicode ? pmrBytes(8, false, {}) : pmrBytes(8, false, {record});
		if (unicode)
			appendSet(bytes, 16, false, {record});
		const auto report = parseBytes(bytes);
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->entries.front().filename, QStringLiteral("a"));
		QCOMPARE(report.records.knownSet->entries.front().project, QStringLiteral("P"));
	}
	for (const bool nullProject : {false, true})
	{
		Record record;
		record.project.clear();
		record.nullProject = nullProject;
		const auto report = parseBytes(pmrBytes(8, false, {record}));
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QVERIFY(report.records.knownSet->entries.front().project.isEmpty());
		QVERIFY(report.records.knownSet->entries.front().masterMobId);
	}
	for (const bool nullFilename : {false, true})
	{
		Record bad;
		bad.filename.clear();
		bad.nullFilename = nullFilename;
		const auto base = parseBytes(pmrBytes(8, false, {Record{}, bad}));
		QVERIFY(base.reading == Read::NoUsableSet);
		QVERIFY(!base.records.canUsePmrMetadata);
		QVERIFY(!base.records.referenceCoverageComplete);
		QVERIFY(!base.records.knownSet);
		QVERIFY(!base.diagnostics.isEmpty());
		QByteArray fallback = pmrBytes(8, false, {Record{}});
		appendSet(fallback, 16, false, {bad});
		const auto report = parseBytes(fallback);
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(!report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->version, qint32{8});
	}
}

void TestBellaPmr::wholeSetFailureDiscardsAllItsEntries()
{
	Record older;
	older.filename = "older.mxf";
	Record first;
	first.filename = "preferred-first.mxf";
	Record damaged;
	damaged.filename = QByteArray::fromHex("ff");
	QByteArray bytes = pmrBytes(8, false, {older});
	appendSet(bytes, 16, false, {first, damaged});
	const auto report = parseBytes(bytes);
	QVERIFY(report.records.canUsePmrMetadata);
	QVERIFY(!report.records.referenceCoverageComplete);
	QVERIFY(report.records.knownSet);
	QCOMPARE(report.records.knownSet->version, qint32{8});
	QCOMPARE(report.records.knownSet->entries.size(), qsizetype{1});
	QCOMPARE(report.records.knownSet->entries.front().filename, QStringLiteral("older.mxf"));

	Record badBase;
	badBase.nullFilename = true;
	QByteArray neither = pmrBytes(8, false, {older, badBase});
	appendSet(neither, 16, false, {first, damaged});
	const auto noSet = parseBytes(neither);
	QVERIFY(!noSet.records.canUsePmrMetadata);
	QVERIFY(!noSet.records.referenceCoverageComplete);
	QVERIFY(!noSet.records.knownSet);
	QVERIFY(!noSet.diagnostics.isEmpty());
}

void TestBellaPmr::safeBaseFramingCanReachPreferredSet()
{
	Record unusable;
	unusable.nullFilename = true; // Known bounds despite unusable older text.
	Record preferred;
	preferred.filename = "preferred.mxf";
	QByteArray bytes = pmrBytes(8, false, {unusable, Record{}});
	appendSet(bytes, 16, false, {preferred});
	const auto report = parseBytes(bytes);
	QVERIFY(report.reading == Read::Completed);
	QVERIFY(report.records.canUsePmrMetadata);
	QVERIFY(report.records.referenceCoverageComplete);
	QVERIFY(report.records.knownSet);
	QCOMPARE(report.records.knownSet->version, qint32{16});
	QCOMPARE(report.records.knownSet->entries.size(), qsizetype{1});
	QCOMPARE(report.records.knownSet->entries.front().filename, QStringLiteral("preferred.mxf"));
}

void TestBellaPmr::brokenBaseFramingCannotFindAnAppendedSet()
{
	QByteArray bytes = pmrBytes(8, false, {Record{}});
	appendSet(bytes, 16, false, {Record{}});
	// Base header (12) plus file ID (32) places its counted-name word here.
	qToLittleEndian<quint16>(65534, bytes.data() + 44);
	const auto report = parseBytes(bytes);
	QVERIFY(report.reading == Read::Malformed);
	QVERIFY(!report.records.canUsePmrMetadata);
	QVERIFY(!report.records.referenceCoverageComplete);
	QVERIFY(!report.records.knownSet);
	QVERIFY(!report.diagnostics.isEmpty());
}

void TestBellaPmr::everyBaseTruncationAndImpossibleCount()
{
	const QByteArray complete = pmrBytes(8, false, {Record{}, Record{}});
	for (qsizetype size = 0; size < complete.size(); ++size)
	{
		const auto report = parseBytes(complete.first(size));
		QVERIFY2(!report.records.canUsePmrMetadata, qPrintable(QStringLiteral("truncation at %1").arg(size)));
		QVERIFY2(!report.records.referenceCoverageComplete, qPrintable(QStringLiteral("truncation at %1").arg(size)));
		QVERIFY(!report.records.knownSet);
	}
	QByteArray hugeCount;
	appendNumber<quint32>(hugeCount, 0x7a9, false);
	appendNumber<qint32>(hugeCount, 8, false);
	appendNumber<quint32>(hugeCount, 0xffffffff, false);
	const auto report = parseBytes(hugeCount);
	QVERIFY(report.reading == Read::Malformed);
	QVERIFY(!report.records.knownSet);
	QVERIFY(!report.records.canUsePmrMetadata);
	QVERIFY(!report.records.referenceCoverageComplete);

	QByteArray shortText;
	appendNumber<quint32>(shortText, 0x7a9, false);
	appendNumber<qint32>(shortText, 8, false);
	appendNumber<quint32>(shortText, 1, false);
	shortText.append(storedId(8, false, false));
	appendNumber<quint16>(shortText, 20, false);
	shortText.append("ab", 2); // Authored: declared 20 bytes, only two available.
	const auto shortReport = parseBytes(shortText);
	QVERIFY(shortReport.reading == Read::Malformed);
	QVERIFY(!shortReport.records.knownSet);
	QVERIFY(!shortReport.records.canUsePmrMetadata);
	QVERIFY(!shortReport.records.referenceCoverageComplete);
}

void TestBellaPmr::everyTruncatedKnownUnicodeSetFallsBack()
{
	Record base;
	base.filename = "base.mxf";
	QByteArray complete = pmrBytes(8, false, {base});
	const qsizetype baseEnd = complete.size();
	Record preferred;
	preferred.filename = "preferred.mxf";
	appendSet(complete, 16, false, {preferred, preferred});
	// Start after a complete recognized v16 version/count header. Classification
	// of an incomplete unknown extension word is deliberately not assumed here.
	for (qsizetype size = baseEnd + 8; size < complete.size(); ++size)
	{
		const auto report = parseBytes(complete.first(size));
		QVERIFY2(report.reading == Read::Completed, qPrintable(QStringLiteral("v16 truncation at %1").arg(size)));
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(!report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->version, qint32{8});
		QCOMPARE(report.records.knownSet->entries.size(), qsizetype{1});
		QCOMPARE(report.records.knownSet->entries.front().filename, QStringLiteral("base.mxf"));
		QVERIFY(!report.diagnostics.isEmpty());
	}
}

void TestBellaPmr::unsupportedExtensionsAndTailsKeepReferenceOnlyWholeSets()
{
	Record base;
	base.filename = "base.mxf";
	QByteArray unknown = pmrBytes(8, false, {base});
	appendNumber<qint32>(unknown, 17, false);
	appendNumber<quint32>(unknown, 0, false);
	const auto extension = parseBytes(unknown);
	QVERIFY(extension.reading == Read::Unsupported);
	QVERIFY(!extension.records.canUsePmrMetadata);
	QVERIFY(!extension.records.referenceCoverageComplete);
	QVERIFY(extension.records.knownSet);
	QCOMPARE(extension.records.knownSet->version, qint32{8});
	QCOMPARE(extension.records.knownSet->entries.size(), qsizetype{1});
	QCOMPARE(extension.records.knownSet->entries.front().filename, QStringLiteral("base.mxf"));
	QVERIFY(!extension.diagnostics.isEmpty());

	Record preferred;
	preferred.filename = "preferred.mxf";
	QByteArray trailing = pmrBytes(8, false, {base});
	appendSet(trailing, 16, false, {preferred});
	trailing.append("unknown", 7);
	const auto tail = parseBytes(trailing);
	QVERIFY(tail.reading == Read::Unsupported);
	QVERIFY(!tail.records.canUsePmrMetadata);
	QVERIFY(!tail.records.referenceCoverageComplete);
	QVERIFY(tail.records.knownSet);
	QCOMPARE(tail.records.knownSet->version, qint32{16});
	QCOMPARE(tail.records.knownSet->entries.size(), qsizetype{1});
	QCOMPARE(tail.records.knownSet->entries.front().filename, QStringLiteral("preferred.mxf"));
	QVERIFY(!tail.diagnostics.isEmpty());

	Record malformed;
	malformed.filename = QByteArray::fromHex("ff");
	const std::array<QVector<Record>, 2> unusablePreferred{{{malformed}, {}}};
	for (const auto &records : unusablePreferred)
	{
		QByteArray fallback = pmrBytes(8, false, {base});
		appendSet(fallback, 16, false, records);
		fallback.append("unknown", 7);
		const auto report = parseBytes(fallback);
		QVERIFY(report.reading == Read::Unsupported);
		QVERIFY(!report.records.canUsePmrMetadata);
		QVERIFY(!report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->version, qint32{8});
		QCOMPARE(report.records.knownSet->entries.size(), qsizetype{1});
		QCOMPARE(report.records.knownSet->entries.front().filename, QStringLiteral("base.mxf"));
		QCOMPARE(report.records.knownSet->entries.front().fileMobId.bytes().toByteArray(), expectedFileId(8));
		QVERIFY(!report.diagnostics.isEmpty());
	}
}

void TestBellaPmr::diagnosticsSeparateFileContextFromReasons()
{
	Record base;
	base.filename = "base.mxf";
	Record malformed;
	malformed.filename = QByteArray::fromHex("ff");
	QByteArray bytes = pmrBytes(8, false, {base});
	appendSet(bytes, 16, false, {malformed});

	QTemporaryDir firstDirectory;
	QTemporaryDir secondDirectory;
	QVERIFY(firstDirectory.isValid());
	QVERIFY(secondDirectory.isValid());
	const QStringList paths{
		firstDirectory.filePath(QStringLiteral("first source.pmr")),
		secondDirectory.filePath(QStringLiteral("second source.pmr")),
	};
	QVector<Bella::PmrReport> reports;
	const std::atomic_bool stopRequested{false};
	for (const auto &path : paths)
	{
		QFile source(path);
		QVERIFY(source.open(QIODevice::WriteOnly));
		QCOMPARE(source.write(bytes), qint64(bytes.size()));
		QVERIFY(source.flush());
		source.close();
		reports.append(Bella::parsePmr(path, stopRequested));
		const auto &report = reports.back();
		QCOMPARE(report.source.path, path);
		QVERIFY(report.reading == Read::Completed);
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(!report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->version, qint32{8});
		QCOMPARE(report.records.knownSet->entries.front().filename, QStringLiteral("base.mxf"));
		QVERIFY(report.diagnostics.size() >= qsizetype{2});
		for (const auto &reason : report.diagnostics)
		{
			QVERIFY(!reason.isEmpty());
			QVERIFY(!reason.contains(firstDirectory.path()));
			QVERIFY(!reason.contains(secondDirectory.path()));
			// A report consumer has both components needed for the full warning.
			// This does not invoke or claim to test future GUI/Console delivery.
			const QString warning = QStringLiteral("%1: %2").arg(report.source.path, reason);
			QVERIFY(warning.startsWith(path + QStringLiteral(": ")));
			QCOMPARE(warning.count(path), qsizetype{1});
			QCOMPARE(warning.sliced(path.size() + 2), reason);
		}
	}
	QCOMPARE(reports[0].diagnostics, reports[1].diagnostics);
}

void TestBellaPmr::emptySetsHaveIndependentReferenceCoverage()
{
	for (const bool hasEmptyExtension : {false, true})
	{
		QByteArray bytes = pmrBytes(8, false, {});
		if (hasEmptyExtension)
			appendSet(bytes, 16, false, {});
		const auto report = parseBytes(bytes);
		QVERIFY(report.reading == Read::NoUsableSet);
		QVERIFY(!report.records.canUsePmrMetadata);
		QVERIFY(report.records.referenceCoverageComplete);
		QVERIFY(!report.records.knownSet || report.records.knownSet->entries.isEmpty());
	}
	QByteArray fallback = pmrBytes(8, false, {Record{}});
	appendSet(fallback, 16, false, {});
	const auto older = parseBytes(fallback);
	QVERIFY(older.reading == Read::Completed);
	QVERIFY(older.records.canUsePmrMetadata);
	QVERIFY(older.records.referenceCoverageComplete);
	QVERIFY(older.records.knownSet);
	QCOMPARE(older.records.knownSet->version, qint32{8});

	QByteArray preferred = pmrBytes(8, false, {});
	appendSet(preferred, 16, false, {Record{}});
	const auto unicode = parseBytes(preferred);
	QVERIFY(unicode.records.canUsePmrMetadata);
	QVERIFY(unicode.records.referenceCoverageComplete);
	QVERIFY(unicode.records.knownSet);
	QCOMPARE(unicode.records.knownSet->version, qint32{16});
}

void TestBellaPmr::unsupportedBaseAndBadMagic()
{
	for (const qint32 version : {qint32{9}, qint32{15}, qint32{16}, qint32{17}})
	{
		const auto report = parseBytes(pmrBytes(version, false, {}));
		QVERIFY(report.reading == Read::Unsupported);
		QVERIFY(!report.records.canUsePmrMetadata);
		QVERIFY(!report.records.referenceCoverageComplete);
		QVERIFY(!report.records.knownSet);
	}
	QByteArray badMagic = pmrBytes(8, false, {Record{}});
	badMagic[0] = 0;
	const auto report = parseBytes(badMagic);
	QVERIFY(report.reading == Read::Malformed);
	QVERIFY(!report.records.canUsePmrMetadata);
	QVERIFY(!report.records.referenceCoverageComplete);
	QVERIFY(!report.records.knownSet);
}

void TestBellaPmr::completeIdsAndIgnoredModificationWord()
{
	for (const qint32 version : {qint32{2}, qint32{8}})
	{
		Record zero;
		zero.fileId = QByteArray(version <= 7 ? 8 : 32, '\0');
		zero.masterId = zero.fileId;
		const auto report = parseBytes(pmrBytes(version, false, {zero}));
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		const auto &entry = report.records.knownSet->entries.front();
		QVERIFY(entry.masterMobId);
		const QByteArray expected = version <= 7
			? QByteArray::fromHex("060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80")
			: QByteArray(32, '\0');
		QCOMPARE(entry.fileMobId.bytes().toByteArray(), expected);
		QCOMPARE(entry.masterMobId->bytes().toByteArray(), expected);
	}
	for (const quint32 modification : {quint32{0}, quint32{0x12345678}, quint32{0xffffffff}})
	{
		Record record;
		record.modification = modification;
		const auto report = parseBytes(pmrBytes(8, false, {record, record}));
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->entries.size(), qsizetype{2});
		QCOMPARE(report.records.knownSet->entries.front().fileMobId.bytes().toByteArray(), expectedFileId(8));
		QCOMPARE(report.records.knownSet->entries.back().filename, QStringLiteral("take.mxf"));
	}
}

void TestBellaPmr::nativeTextCapacitiesAreNotWireLimits()
{
	Record longText;
	longText.filename = QByteArray(4096, 'n');
	longText.project = QByteArray(4096, 'p');
	for (const bool unicode : {false, true})
	{
		QByteArray bytes = unicode ? pmrBytes(8, false, {}) : pmrBytes(8, false, {longText});
		if (unicode)
			appendSet(bytes, 16, false, {longText});
		const auto report = parseBytes(bytes);
		QVERIFY(report.records.canUsePmrMetadata);
		QVERIFY(report.records.referenceCoverageComplete);
		QVERIFY(report.records.knownSet);
		QCOMPARE(report.records.knownSet->entries.front().filename, QString(4096, QLatin1Char('n')));
		QCOMPARE(report.records.knownSet->entries.front().project, QString(4096, QLatin1Char('p')));
	}
}

void TestBellaPmr::stampComparisonUsesAvailableFacts()
{
	struct Example
	{
		const char *name;
		Bella::PmrStamp before;
		Bella::PmrStamp after;
		Check expected;
	};
	const std::vector<Example> examples{
		{"both equal", stamp(100, 1000), stamp(100, 1000), Check::Verified},
		{"zero size and epoch are available", stamp(0, 0), stamp(0, 0), Check::Verified},
		{"size changed", stamp(100, 1000), stamp(101, 1000), Check::Changed},
		{"date changed by one millisecond", stamp(100, 1000), stamp(100, 1001), Check::Changed},
		{"only equal sizes", stamp(100, std::nullopt), stamp(100, std::nullopt), Check::Unverified},
		{"only equal dates", stamp(std::nullopt, 1000), stamp(std::nullopt, 1000), Check::Unverified},
		{"missing before facts", stamp(std::nullopt, std::nullopt), stamp(100, 1000), Check::Unverified},
		{"missing after facts", stamp(100, 1000), stamp(std::nullopt, std::nullopt), Check::Unverified},
		{"all missing", stamp(std::nullopt, std::nullopt), stamp(std::nullopt, std::nullopt), Check::Unverified},
		{"size difference despite missing dates", stamp(100, std::nullopt), stamp(101, std::nullopt), Check::Changed},
		{"date difference despite missing sizes", stamp(std::nullopt, 1000), stamp(std::nullopt, 1001), Check::Changed},
		{"negative size is unavailable", stamp(-1, 1000), stamp(100, 1000), Check::Unverified},
		{"different negative sizes are unavailable", stamp(-1, 1000), stamp(-2, 1000), Check::Unverified},
		{"known date change wins over negative size", stamp(-1, 1000), stamp(100, 1001), Check::Changed},
	};
	for (const auto &example : examples)
		QVERIFY2(Bella::Detail::comparePmrStamps(example.before, example.after) == example.expected, example.name);
}

void TestBellaPmr::preCancelledAndIoFailures()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString missing = directory.filePath(QStringLiteral("missing.pmr"));
	const std::atomic_bool stopped{true};
	const auto cancelled = Bella::parsePmr(missing, stopped);
	QVERIFY(cancelled.reading == Read::Cancelled);
	QVERIFY(cancelled.sizeDateCheck == Check::NotChecked);
	QVERIFY(!cancelled.records.knownSet);
	QVERIFY(!cancelled.records.canUsePmrMetadata);
	QVERIFY(!cancelled.records.referenceCoverageComplete);
	QCOMPARE(cancelled.source.path, missing);

	const std::atomic_bool running{false};
	const auto absent = Bella::parsePmr(missing, running);
	QVERIFY(absent.reading == Read::IoError);
	QVERIFY(!absent.records.knownSet);
	QVERIFY(!absent.records.canUsePmrMetadata);
	QVERIFY(!absent.records.referenceCoverageComplete);
	QVERIFY(!absent.diagnostics.isEmpty());
	QCOMPARE(absent.source.path, missing);
}

void TestBellaPmr::reportsOwnTheirResults()
{
	Record record;
	record.filename = QByteArray::fromHex("c39f2e6d7866");
	record.project = QByteArray::fromHex("a7");
	QByteArray source = pmrBytes(8, false, {});
	appendSet(source, 16, false, {record});
	auto original = parseBytes(source);
	QVERIFY(original.records.canUsePmrMetadata);
	QVERIFY(original.records.referenceCoverageComplete);
	QVERIFY(original.records.knownSet);
	QVERIFY(!QFile::exists(original.source.path));
	source.fill('x');
	const auto copied = original;
	auto moved = std::move(original);
	moved.records.knownSet.reset();
	QVERIFY(copied.records.knownSet);
	QCOMPARE(copied.records.knownSet->entries.front().filename, QString::fromUtf8("ß.mxf"));
	QCOMPARE(copied.records.knownSet->entries.front().project, QString::fromUtf8("ß"));
	QCOMPARE(copied.records.knownSet->entries.front().fileMobId.bytes().toByteArray(), expectedFileId(16));
}

void TestBellaPmr::genuineLittleEndianFixtures_data()
{
	QTest::addColumn<QString>("relativePath");
	QTest::addColumn<int>("count");
	QTest::addColumn<QString>("filename");
	QTest::addColumn<QString>("project");
	QTest::addColumn<QByteArray>("fileId");
	QTest::addColumn<QByteArray>("masterId");
	QTest::addColumn<QByteArray>("fingerprint");
	// These pinned expectations come from genuine little-endian PMR bytes.
	// They do not turn the authored opposite-endian cases above into specimens.
	QTest::newRow("genuine-tone-LE") << QStringLiteral("msmFMID.pmr") << 1
		<< QStringLiteral("TONE_100A01.EA7D504A.611740.mxf") << QStringLiteral("block 1729")
		<< QByteArray::fromHex("060a2b340101010501010f10130000004a507dea741106907a361e6a605d3613")
		<< QByteArray::fromHex("060a2b340101010501010f1013000000d2467dea7411069091901e6a605d3613")
		<< QByteArray("be2a925654ac00bc25146223b748dc879e49c6a0cb5a2c6645c29f1ffdae1d8f");
	QTest::newRow("genuine-corpus-LE") << QStringLiteral("corpus_headers/msmFMID.pmr") << 435
		<< QStringLiteral("V01.E690E707_DA3A4DA3A4B46V.mxf") << QString::fromUtf8("zT?ßt_1080i_50")
		<< QByteArray::fromHex("060a2b340101010501010f1013000000464b3ada51120690e4aac22704e88b34")
		<< QByteArray::fromHex("060a2b340101010501010f10130000003f4b3ada511206904523c22704e88b34")
		<< QByteArray("44aa350f0677e7fd9155a733e82a628b961cfd69111492348e478e1ebc18ca38");
	QTest::newRow("genuine-round3-LE") << QStringLiteral("corpus_headers/msmFMID_round3.pmr") << 360
		<< QStringLiteral("A01.E69CB57D_CE619CE61917BA.mxf") << QString::fromUtf8("zTeßt_720p_23.976")
		<< QByteArray::fromHex("060a2b340101010501010f10130000007b9161ce601206909ffb46fc807fda72")
		<< QByteArray::fromHex("060a2b340101010501010f10130000006a9161ce60120690aab246fc807fda72")
		<< QByteArray("1133f437da53b90d3332bedfac8ab791335d03a84c6b775a5181d32ede8f9f3e");
	QTest::newRow("genuine-legacy-supporting-LE") << QStringLiteral("omf/avid_supporting/msmFMID.pmr") << 80
		<< QStringLiteral("BLACK_720x576x1_DV420.omf") << QString{}
		<< QByteArray::fromHex("060a2b340101010101010f00130000005f489d3ab16ff300060e2b347f7f2a80")
		<< QByteArray::fromHex("060a2b340101010101010f00130000005f489d3ab06ff300060e2b347f7f2a80")
		<< QByteArray("44a2068496b885c6dfb5767408dbe9c31a2a110a911d8d97868a92f20427b0db");
	QTest::newRow("genuine-legacy-audio-LE") << QStringLiteral("omf/mc2026_audio/msmFMID.pmr") << 2
		<< QStringLiteral("TONE_100A01.6A972974.039700.wav") << QString::fromUtf8("zTeßt_PAL_25p")
		<< QByteArray::fromHex("060a2b340101010101010f00130000007429976a70397047060e2b347f7f2a80")
		<< QByteArray::fromHex("060a2b340101010101010f00130000007429976a4e397047060e2b347f7f2a80")
		<< QByteArray("e919894a99ad9c237e24a117de2b85772da1cadacef14d4b0b2f463efa37a3d1");
}

void TestBellaPmr::genuineLittleEndianFixtures()
{
	QFETCH(QString, relativePath);
	QFETCH(int, count);
	QFETCH(QString, filename);
	QFETCH(QString, project);
	QFETCH(QByteArray, fileId);
	QFETCH(QByteArray, masterId);
	QFETCH(QByteArray, fingerprint);
	const QString path = QString::fromUtf8(FIXTURES_DIR) + QLatin1Char('/') + relativePath;
	const std::atomic_bool stopRequested{false};
	const auto report = Bella::parsePmr(path, stopRequested);
	QVERIFY(report.reading == Read::Completed);
	QVERIFY(report.sizeDateCheck == Check::Verified);
	QVERIFY(report.records.canUsePmrMetadata);
	QVERIFY(report.records.referenceCoverageComplete);
	QVERIFY(report.records.knownSet);
	QCOMPARE(report.records.knownSet->version, qint32{16});
	QCOMPARE(report.records.knownSet->entries.size(), qsizetype(count));
	const auto &entry = report.records.knownSet->entries.front();
	QCOMPARE(entry.filename, filename);
	QCOMPARE(entry.project, project);
	QCOMPARE(entry.fileMobId.bytes().toByteArray(), fileId);
	QVERIFY(entry.masterMobId);
	QCOMPARE(entry.masterMobId->bytes().toByteArray(), masterId);
	QCOMPARE(recordFingerprint(*report.records.knownSet), fingerprint);
	for (const auto &item : report.records.knownSet->entries)
	{
		QVERIFY(!item.filename.isEmpty());
		QCOMPARE(item.fileMobId.bytes().size(), qsizetype{32});
		QVERIFY(item.masterMobId);
		QCOMPARE(item.masterMobId->bytes().size(), qsizetype{32});
	}
}

QTEST_APPLESS_MAIN(TestBellaPmr)
#include "tst_bellapmr.moc"
