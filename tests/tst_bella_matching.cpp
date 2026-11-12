// Authored, already-read report values exercise the approved matching contract.
// Expected results are authored independently of readers and selectors.
#include "scanner/matching.h"

#include <QDir>
#include <QtTest>
#include <numeric>

Q_DECLARE_METATYPE(Bella::LocalReference)
Q_DECLARE_METATYPE(Bella::MdbDepthRepresentation)

// MARK: - Test helpers

namespace
{
	const QString folder = QDir::tempPath() + QStringLiteral("/bella-matching/1");
	const QString physical = folder + QStringLiteral("/Take.mxf");
	const QString elsewhere = QDir::tempPath() + QStringLiteral("/bella-matching/other");

	Bella::MobId id(quint8 tail)
	{
		// All IDs share material bytes 16..23. Their complete tails distinguish
		// them, so a shortened eight-byte identity cannot pass these joins.
		auto bytes = QByteArray::fromHex("060a2b340101010501010f10130000000102030405060708090a0b0c0d0e0f00");
		bytes[31] = char(tail);
		return std::get<Bella::MobId>(Bella::normalizeMobId(bytes, Bella::MobIdLayout::Aaf32,
															Bella::ByteOrder::LittleEndian));
	}

	std::array<quint8, 4> tag(QByteArrayView value)
	{
		return {quint8(value[0]), quint8(value[1]), quint8(value[2]), quint8(value[3])};
	}

	std::array<quint8, 16> coding(const char *hex)
	{
		const auto value = QByteArray::fromHex(hex);
		Q_ASSERT(value.size() == 16);
		std::array<quint8, 16> result{};
		for (qsizetype i = 0; i < 16; ++i)
			result[std::size_t(i)] = quint8(value[i]);
		return result;
	}

	Bella::PmrReport pmr(QVector<Bella::PmrEntry> entries, const QString &sourceFolder = folder)
	{
		Bella::PmrReport report;
		report.source.path = sourceFolder + QStringLiteral("/msmFMID.pmr");
		report.reading = entries.isEmpty() ? Bella::PmrReadResult::NoUsableSet : Bella::PmrReadResult::Completed;
		report.sizeDateCheck = Bella::PmrCheckResult::Verified;
		report.records.knownSet = Bella::PmrSet{16, std::move(entries)};
		report.records.canUsePmrMetadata = !report.records.knownSet->entries.isEmpty();
		report.records.referenceCoverageComplete = true;
		return report;
	}

	Bella::PmrReport listedPmr(std::optional<Bella::MobId> master = id(11), const QString &name = QStringLiteral("Take.mxf"))
	{
		return pmr({Bella::PmrEntry{id(1), name, QStringLiteral("PMR project"), master}});
	}

	Bella::MdbFileRecord mdbFile(quint8 fileId = 1)
	{
		Bella::MdbFileRecord file{id(fileId)};
		file.project = QStringLiteral("MDB project");
		file.projectSelection = Bella::MdbProjectSelection::OwnFile;
		file.projectKey = Bella::MdbProjectKey::Pj;
		file.kind = Bella::MdbKind::Video;
		file.descriptorClass = tag("CDCI");
		file.ownDescriptorQualified = true;
		file.length = 250;
		file.sampleRate = Bella::MdbRate{{25, 1}, Bella::MdbRateEncoding::ExactEditRate};
		file.numbering = Bella::MdbNumbering::NotRequired;
		file.resolution = Bella::MdbRaster{{1920, 1080}, Bella::MdbRasterOrigin::Stored};
		file.bitDepth.bits = 10;
		file.bitDepth.representation = Bella::MdbDepthRepresentation::Integer;
		file.bitDepth.rawComponentWidth = 10;
		file.compression.codingState = Bella::MdbCodingState::Present;
		file.compression.codingLabel = coding("060e2b340401010d0401020203060400");
		file.masterLinksComplete = true;
		file.directMasterRecords = {0};
		return file;
	}

	Bella::MdbReport mdb(const QString &sourceFolder = folder)
	{
		Bella::MdbReport report;
		report.path = sourceFolder + QStringLiteral("/msmMMOB.mdb");
		report.reading = Bella::MdbReadResult::Completed;
		report.sizeDateCheck = Bella::MdbCheckResult::Verified;
		report.catalogue.files.append(mdbFile());
		report.catalogue.masters.append({id(11), QStringLiteral("MDB clip"), QStringLiteral("MDB bin"),
										 Bella::MdbType::Media, Bella::MdbPrecomputeCategory::Unknown});
		report.catalogue.fileReferencesComplete = true;
		return report;
	}

	Bella::MxfReport mxf()
	{
		Bella::MxfReport report;
		report.path = physical;
		report.reading = Bella::MxfReadResult::Completed;
		report.sizeDateCheck = Bella::MxfCheckResult::Verified;
		auto &file = report.fileInformation;
		file.fileMobId = id(1);
		file.project = QStringLiteral("MXF project");
		file.projectSelection = Bella::MxfProjectSelection::Preface;
		file.kind = Bella::MxfKind::Video;
		file.descriptorClass = coding("060e2b34025301010d01010101012800");
		file.containerDuration = 500;
		file.sampleRate = Bella::MxfRate{25, 1};
		file.frameRateQualified = true;
		file.numbering = Bella::MxfNumbering::NotRequired;
		file.resolution = Bella::MxfRaster{{720, 576}, Bella::MxfRasterOrigin::Display};
		file.bitDepth.bits = 8;
		file.bitDepth.representation = Bella::MxfDepthRepresentation::Integer;
		file.compression.codingState = Bella::MxfCodingState::Present;
		file.compression.codingLabel = coding("060e2b340401010d0401020203060400");
		report.associatedClipInformation.masterMobId = id(11);
		report.associatedClipInformation.clipName = QStringLiteral("MXF clip");
		report.associatedClipInformation.type = Bella::MxfType::Media;
		return report;
	}

	Bella::OmfReport omf()
	{
		Bella::OmfReport report;
		report.path = physical;
		report.nativeAudioReading = Bella::OmfReadResult::NotPresent;
		report.omfReading = Bella::OmfReadResult::Completed;
		report.sizeDateCheck = Bella::OmfCheckResult::Verified;
		auto &file = report.fileInformation;
		file.fileMobId = id(1);
		file.project = QStringLiteral("OMF project");
		file.projectSelection = Bella::OmfProjectSelection::OwnFile;
		file.kind = Bella::OmfKind::Video;
		file.descriptorClass = tag("CDCI");
		file.length = 500;
		file.sampleRate = Bella::OmfRate{{25, 1}, Bella::OmfRateEncoding::ExactEditRate};
		file.durationClockSource = Bella::OmfDurationClockSource::DescriptorSampleRate;
		file.numbering = Bella::OmfNumbering::NotRequired;
		report.associatedClipInformation.masterMobId = id(11);
		report.associatedClipInformation.clipName = QStringLiteral("OMF clip");
		report.associatedClipInformation.originalBin = QStringLiteral("OMF bin");
		report.associatedClipInformation.type = Bella::OmfType::Media;
		return report;
	}

	QString label(const Bella::FileTiming &timing)
	{
		return timing.duration && timing.display ? Bella::formatDuration(*timing.duration, timing.display->displayRate,
																		 timing.display->nominalBase, timing.display->mode)
												 : QString{};
	}

	bool sameRate(Bella::TimingRate a, Bella::TimingRate b)
	{
		if (!a.numerator || !a.denominator || !b.numerator || !b.denominator)
			return false;
		const auto x = std::gcd(a.numerator, a.denominator), y = std::gcd(b.numerator, b.denominator);
		return a.numerator / x == b.numerator / y && a.denominator / x == b.denominator / y;
	}
}

// MARK: - Test fixture

class TestBellaMatching final : public QObject
{
	Q_OBJECT
private slots:
	void localReference_data();
	void localReference();
	void exactPmrNames_data();
	void exactPmrNames();
	void selectedFieldPriorities();
	void selectedFixedPointBitDepth_data();
	void selectedFixedPointBitDepth();
	void headerNeeded_data();
	void headerNeeded();
	void failedHeaderIsNotRequestedAgain_data();
	void failedHeaderIsNotRequestedAgain();
	void headerIdentityRevisitsScanWideMdb();
	void fullIdsDoNotMatchAnExcerpt();
	void masterSelection_data();
	void masterSelection();
	void unsafeMasterAssociationBlocksHeader_data();
	void unsafeMasterAssociationBlocksHeader();
	void duplicateMasters_data();
	void duplicateMasters();
	void conflictingMdbFrameRates_data();
	void conflictingMdbFrameRates();
	void partialCompressionNamesAgree_data();
	void partialCompressionNamesAgree();
	void sameMasterEditorialFallback_data();
	void sameMasterEditorialFallback();
	void durationIsNotRepairedByAnotherSource_data();
	void durationIsNotRepairedByAnotherSource();
	void headerFallbackAndNativeIndependence();
	void nativeTimingPriority_data();
	void nativeTimingPriority();
	void nativeAndEmbeddedTechnicalFacts_data();
	void nativeAndEmbeddedTechnicalFacts();
	void sourceQualifications_data();
	void sourceQualifications();
	void physicalEligibility();
	void optionalEffects_data();
	void optionalEffects();
	void originalAudioFrequency_data();
	void originalAudioFrequency();
	void ownedContextAndResultLifetime();
};

// MARK: - Reference matching and metadata priority

void TestBellaMatching::localReference_data()
{
	QTest::addColumn<int>("variation");
	QTest::addColumn<Bella::LocalReference>("expected");
	QTest::newRow("complete folder with no database") << 0 << Bella::LocalReference::NoDatabase;
	QTest::newRow("unproven folder completion") << 1 << Bella::LocalReference::Unresolved;
	QTest::newRow("known unread PMR is present") << 2 << Bella::LocalReference::Unresolved;
	QTest::newRow("known unread MDB is present") << 3 << Bella::LocalReference::Unresolved;
	QTest::newRow("covered empty PMR is a completed miss") << 4 << Bella::LocalReference::NoReference;
	QTest::newRow("covered empty MDB is a completed miss") << 5 << Bella::LocalReference::NoReference;
	QTest::newRow("PMR positive wins unread sibling") << 6 << Bella::LocalReference::Listed;
	QTest::newRow("completed PMR miss wins unread sibling") << 7 << Bella::LocalReference::NoReference;
	QTest::newRow("reference-only validated PMR set") << 8 << Bella::LocalReference::Listed;
	QTest::newRow("unverified empty PMR cannot prove a miss") << 9 << Bella::LocalReference::Unresolved;
	QTest::newRow("incomplete nonempty MDB cannot prove a miss") << 10 << Bella::LocalReference::Unresolved;
}
void TestBellaMatching::localReference()
{
	QFETCH(int, variation);
	QFETCH(Bella::LocalReference, expected);
	QVector<Bella::PmrReport> pmrs;
	QVector<Bella::MdbReport> mdbs;
	if (variation == 2 || variation == 3 || variation == 6 || variation == 7)
	{
		if (variation == 2)
		{
			Bella::PmrReport unread;
			unread.source.path = folder + QStringLiteral("/msmFMID.pmr");
			pmrs.append(unread);
		}
		else
		{
			Bella::MdbReport unread;
			unread.path = folder + QStringLiteral("/msmMMOB.mdb");
			mdbs.append(unread);
		}
	}
	if (variation == 4 || variation == 7 || variation == 9)
	{
		auto report = pmr({});
		if (variation == 9)
			report.sizeDateCheck = Bella::PmrCheckResult::Unverified;
		pmrs.append(report);
	}
	if (variation == 5)
	{
		auto report = mdb();
		report.catalogue.files.clear();
		report.catalogue.masters.clear();
		mdbs.append(report);
	}
	if (variation == 6 || variation == 8)
	{
		auto report = listedPmr();
		if (variation == 8)
		{
			report.reading = Bella::PmrReadResult::Unsupported;
			report.records.canUsePmrMetadata = false;
			report.records.referenceCoverageComplete = false;
		}
		pmrs.append(report);
	}
	if (variation == 10)
	{
		auto report = mdb();
		report.catalogue.files[0].fileMobId = id(2);
		report.catalogue.fileReferencesComplete = false;
		mdbs.append(report);
	}
	const auto context = Bella::prepareMatchContext(std::move(pmrs), std::move(mdbs), variation == 1 ? QSet<QString>{} : QSet<QString>{folder});
	const auto header = mxf();
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	QCOMPARE(answer.localReference, expected);
	QVERIFY(!answer.needsHeader); // A supplied attempt is never requested again.
}

void TestBellaMatching::exactPmrNames_data()
{
	QTest::addColumn<QString>("recorded");
	QTest::addColumn<QString>("filename");
	QTest::addColumn<bool>("match");
	QTest::newRow("exact spelling") << QStringLiteral("Take.mxf") << QStringLiteral("Take.mxf") << true;
	QTest::newRow("case is not folded") << QStringLiteral("TAKE.mxf") << QStringLiteral("Take.mxf") << false;
	QTest::newRow("Unicode composition is not normalized") << QString::fromUtf8("caf\xc3\xa9.mxf") << QString::fromUtf8("cafe\xcc\x81.mxf") << false;
	QTest::newRow("filename whitespace is not trimmed") << QStringLiteral(" Take.mxf") << QStringLiteral("Take.mxf") << false;
}
void TestBellaMatching::exactPmrNames()
{
	QFETCH(QString, recorded);
	QFETCH(QString, filename);
	QFETCH(bool, match);
	const auto context = Bella::prepareMatchContext({listedPmr(id(11), recorded)}, {mdb()}, {folder});
	const auto answer = Bella::matchFile(context, folder + '/' + filename, true, {}, false);
	QCOMPARE(bool(answer.metadata.fileMobId), match);
	QCOMPARE(answer.needsHeader, !match);
	if (match)
		QCOMPARE(*answer.metadata.fileMobId, id(1));
	else
		QVERIFY(answer.metadata.project.isEmpty());
}

void TestBellaMatching::selectedFieldPriorities()
{
	auto database = mdb();
	auto &file = database.catalogue.files[0];
	file.length = 1800;
	file.sampleRate = Bella::MdbRate{{2997, 100}, Bella::MdbRateEncoding::ExactEditRate};
	file.numbering = Bella::MdbNumbering::RecordedNdf;
	file.compression.codingLabel = coding("060e2b340401010a0401020271010000");
	file.compression.namingGeometry = Bella::MdbDimensions{1920, 1080};
	file.compression.frameLayout = 0;
	file.compression.horizontalSubsampling = 2;
	file.compression.verticalSubsampling = 1;
	const auto context = Bella::prepareMatchContext({listedPmr()}, {database}, {folder});
	const auto header = mxf();
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	const auto &m = answer.metadata;
	QVERIFY(m.fileMobId);
	QCOMPARE(*m.fileMobId, id(1));
	QVERIFY(m.masterMobId);
	QCOMPARE(*m.masterMobId, id(11));
	QCOMPARE(m.project, QStringLiteral("PMR project"));
	QCOMPARE(m.clipName, QStringLiteral("MDB clip"));
	QCOMPARE(m.originalBin, QStringLiteral("MDB bin"));
	QCOMPARE(m.kind, Bella::MatchKind::Video);
	QCOMPARE(m.type, Bella::MatchType::Media);
	QVERIFY(m.resolution);
	QCOMPARE(m.resolution->width, qint64(1920));
	QCOMPARE(m.resolution->height, qint64(1080));
	QVERIFY(m.frameRate);
	QCOMPARE(m.frameRate->words.numerator, qint32(2997));
	QCOMPARE(m.frameRate->words.denominator, qint32(100));
	QCOMPARE(m.frameRate->meaning, Bella::MatchRateMeaning::AvidExactEditRate);
	QVERIFY(m.exactFrameRate);
	QCOMPARE(m.exactFrameRate->numerator, quint64(30000));
	QCOMPARE(m.exactFrameRate->denominator, quint64(1001));
	QVERIFY(!m.sampleRate);
	QCOMPARE(m.bitDepth.bits, std::optional<quint32>(10));
	QCOMPARE(m.bitDepth.representation, Bella::MatchDepthRepresentation::Integer);
	QCOMPARE(m.interpretedBitDepth, std::optional<quint32>(10));
	QCOMPARE(m.compression.compressionName, QStringLiteral("Apple ProRes 422 HQ"));
	QVERIFY(m.compression.newDnx.isEmpty());
	QVERIFY(m.timing.duration);
	QCOMPARE(m.timing.duration->count, qint64(1800));
	QVERIFY(sameRate(m.timing.duration->rate, {30000, 1001}));
	QCOMPARE(label(m.timing), QStringLiteral("00:01:00:00"));
	QCOMPARE(answer.localReference, Bella::LocalReference::Listed);
	QVERIFY(!answer.needsHeader);
}

void TestBellaMatching::selectedFixedPointBitDepth_data()
{
	QTest::addColumn<Bella::MdbDepthRepresentation>("representation");
	QTest::addColumn<quint32>("expectedBits");
	QTest::newRow("10.6 fixed point") << Bella::MdbDepthRepresentation::Fixed10_6 << quint32(10);
	QTest::newRow("12.4 fixed point") << Bella::MdbDepthRepresentation::Fixed12_4 << quint32(12);
}

void TestBellaMatching::selectedFixedPointBitDepth()
{
	QFETCH(Bella::MdbDepthRepresentation, representation);
	QFETCH(quint32, expectedBits);
	auto database = mdb();
	auto &file = database.catalogue.files[0];
	file.bitDepth.bits.reset();
	file.bitDepth.storageBits = 16;
	file.bitDepth.representation = representation;
	file.bitDepth.rawComponentWidth = qint32(expectedBits);
	file.compression.codingLabel = coding("060e2b340401010d0401020203070200");

	const auto context = Bella::prepareMatchContext({listedPmr()}, {database}, {folder});
	const auto header = mxf();
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	QVERIFY(!answer.metadata.bitDepth.bits);
	QCOMPARE(answer.metadata.bitDepth.storageBits, std::optional<quint32>(16));
	QCOMPARE(answer.metadata.interpretedBitDepth, std::optional<quint32>(expectedBits));
}

void TestBellaMatching::headerNeeded_data()
{
	QTest::addColumn<int>("variation");
	QTest::addColumn<bool>("needed");
	QTest::newRow("missing PMR identification") << 0 << true;
	QTest::newRow("missing MDB file record") << 1 << true;
	QTest::newRow("unsafe required descriptor") << 2 << true;
	QTest::newRow("blank individual technical values") << 3 << false;
	QTest::newRow("missing master") << 4 << false;
}
void TestBellaMatching::headerNeeded()
{
	QFETCH(int, variation);
	QFETCH(bool, needed);
	QVector<Bella::PmrReport> pmrs;
	QVector<Bella::MdbReport> databases;
	if (variation != 0)
		pmrs.append(listedPmr(variation == 4 ? std::optional<Bella::MobId>{} : std::optional<Bella::MobId>(id(11))));
	if (variation != 1)
	{
		auto database = mdb();
		auto &file = database.catalogue.files[0];
		if (variation == 2)
			file.ownDescriptorQualified = false;
		if (variation == 3)
		{
			file.project.clear();
			file.kind = Bella::MdbKind::Unknown;
			file.descriptorClass = tag("TIFD");
			file.length.reset();
			file.sampleRate.reset();
			file.resolution.reset();
			file.bitDepth = {};
			file.compression = {};
			database.catalogue.masters[0].clipName.clear();
		}
		if (variation == 4)
		{
			file.directMasterRecords.clear();
			database.catalogue.masters.clear();
		}
		databases.append(database);
	}
	const auto context = Bella::prepareMatchContext(std::move(pmrs), std::move(databases), {folder});
	const auto answer = Bella::matchFile(context, physical, true, {}, false);
	QCOMPARE(answer.needsHeader, needed);
}

void TestBellaMatching::failedHeaderIsNotRequestedAgain_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("I/O failure is an attempted reply") << 0;
	QTest::newRow("cancelled reader is an attempted reply") << 1;
	QTest::newRow("unsupported header is an attempted reply") << 2;
}
void TestBellaMatching::failedHeaderIsNotRequestedAgain()
{
	QFETCH(int, variation);
	const auto context = Bella::prepareMatchContext({}, {}, {folder});
	QVERIFY(Bella::matchFile(context, physical, true, {}, false).needsHeader);
	Bella::MxfReport header;
	header.path = physical;
	header.reading = variation == 0	  ? Bella::MxfReadResult::IoError
					 : variation == 1 ? Bella::MxfReadResult::Cancelled
									  : Bella::MxfReadResult::Unsupported;
	if (variation == 2)
		header.sizeDateCheck = Bella::MxfCheckResult::Verified;
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	QVERIFY(!answer.needsHeader);
	QVERIFY(!answer.metadata.fileMobId);
	QVERIFY(!answer.metadata.timing.duration);
	QCOMPARE(answer.localReference, Bella::LocalReference::NoDatabase);
}

void TestBellaMatching::headerIdentityRevisitsScanWideMdb()
{
	const auto context = Bella::prepareMatchContext({}, {mdb(elsewhere)}, {folder, elsewhere});
	const auto before = Bella::matchFile(context, physical, true, {}, false);
	QVERIFY(before.needsHeader);
	QVERIFY(!before.metadata.fileMobId);
	const auto header = mxf();
	const auto after = Bella::matchFile(context, physical, true, std::cref(header), false);
	QVERIFY(after.metadata.fileMobId);
	QCOMPARE(*after.metadata.fileMobId, id(1));
	QCOMPARE(after.metadata.clipName, QStringLiteral("MDB clip"));
	QCOMPARE(after.metadata.project, QStringLiteral("MDB project"));
	QVERIFY(after.metadata.timing.duration);
	QCOMPARE(after.metadata.timing.duration->count, qint64(250));
	QCOMPARE(after.localReference, Bella::LocalReference::NoDatabase);
	QVERIFY(!after.needsHeader);
}

void TestBellaMatching::fullIdsDoNotMatchAnExcerpt()
{
	auto database = mdb();
	database.catalogue.files[0].fileMobId = id(2);
	const auto context = Bella::prepareMatchContext({listedPmr()}, {database}, {folder});
	const auto answer = Bella::matchFile(context, physical, true, {}, false);
	QVERIFY(answer.metadata.fileMobId);
	QCOMPARE(*answer.metadata.fileMobId, id(1));
	QVERIFY(answer.needsHeader);
	QCOMPARE(answer.metadata.kind, Bella::MatchKind::Unknown);
	QVERIFY(!answer.metadata.timing.duration);
}

// MARK: - Master identity selection

void TestBellaMatching::masterSelection_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("explicit PMR master resolves") << 0;
	QTest::newRow("unresolved explicit master is retained") << 1;
	QTest::newRow("no explicit master uses unique complete direct link") << 2;
	QTest::newRow("incomplete direct links do not establish a master") << 3;
	QTest::newRow("two distinct direct masters are ambiguous") << 4;
	QTest::newRow("explicit master does not require rediscovering incomplete links") << 5;
}
void TestBellaMatching::masterSelection()
{
	QFETCH(int, variation);
	auto database = mdb();
	if (variation == 3 || variation == 5)
		database.catalogue.files[0].masterLinksComplete = false;
	if (variation == 4)
	{
		database.catalogue.masters.append({id(12), QStringLiteral("Other master"), {}, Bella::MdbType::Media, Bella::MdbPrecomputeCategory::Unknown});
		database.catalogue.files[0].directMasterRecords = {0, 1};
	}
	const auto explicitId = variation == 1					   ? std::optional<Bella::MobId>(id(99))
							: variation == 0 || variation == 5 ? std::optional<Bella::MobId>(id(11))
															   : std::optional<Bella::MobId>{};
	const auto context = Bella::prepareMatchContext({listedPmr(explicitId)}, {database}, {folder});
	const auto answer = Bella::matchFile(context, physical, true, {}, false);
	QVERIFY(!answer.needsHeader); // Master/name absence is not an opening reason.
	if (variation == 1)
	{
		QVERIFY(answer.metadata.masterMobId);
		QCOMPARE(*answer.metadata.masterMobId, id(99));
		QVERIFY(answer.metadata.clipName.isEmpty());
	}
	else if (variation == 3 || variation == 4)
	{
		QVERIFY(!answer.metadata.masterMobId);
		QVERIFY(answer.metadata.clipName.isEmpty());
	}
	else
	{
		QVERIFY(answer.metadata.masterMobId);
		QCOMPARE(*answer.metadata.masterMobId, id(11));
		QCOMPARE(answer.metadata.clipName, QStringLiteral("MDB clip"));
	}
}

void TestBellaMatching::unsafeMasterAssociationBlocksHeader_data()
{
	QTest::addColumn<bool>("ambiguous");
	QTest::newRow("incomplete MDB association is not an empty field") << false;
	QTest::newRow("ambiguous MDB association is not an empty field") << true;
}
void TestBellaMatching::unsafeMasterAssociationBlocksHeader()
{
	QFETCH(bool, ambiguous);
	auto database = mdb();
	if (ambiguous)
	{
		database.catalogue.masters.append({id(12), QStringLiteral("Other master"), {}, Bella::MdbType::Media, Bella::MdbPrecomputeCategory::Unknown});
		database.catalogue.files[0].directMasterRecords = {0, 1};
	}
	else
		database.catalogue.files[0].masterLinksComplete = false;
	const auto context = Bella::prepareMatchContext({listedPmr({})}, {database}, {folder});
	const auto header = mxf(); // Same file, with one independently linked header master.
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	QVERIFY(answer.metadata.fileMobId);
	QCOMPARE(*answer.metadata.fileMobId, id(1));
	QVERIFY(!answer.metadata.masterMobId);
	QVERIFY(answer.metadata.clipName.isEmpty());
	QCOMPARE(answer.metadata.type, Bella::MatchType::Unknown);
	QVERIFY(!answer.needsHeader);
}

void TestBellaMatching::duplicateMasters_data()
{
	QTest::addColumn<bool>("conflict");
	QTest::addColumn<bool>("reverse");
	QTest::newRow("agreeing duplicates") << false << false;
	QTest::newRow("conflicting names cannot be replaced by header") << true << false;
	QTest::newRow("reversed completion order has same conflict") << true << true;
}
void TestBellaMatching::duplicateMasters()
{
	QFETCH(bool, conflict);
	QFETCH(bool, reverse);
	auto first = mdb(), second = mdb(elsewhere);
	if (conflict)
		second.catalogue.masters[0].clipName = QStringLiteral("Different MDB name");
	QVector<Bella::MdbReport> databases = reverse ? QVector<Bella::MdbReport>{second, first} : QVector<Bella::MdbReport>{first, second};
	const auto context = Bella::prepareMatchContext({listedPmr()}, std::move(databases), {folder, elsewhere});
	const auto header = mxf();
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	QVERIFY(answer.metadata.masterMobId);
	QCOMPARE(*answer.metadata.masterMobId, id(11));
	QCOMPARE(answer.metadata.clipName, conflict ? QString{} : QStringLiteral("MDB clip"));
	QCOMPARE(answer.metadata.originalBin, QStringLiteral("MDB bin"));
	QCOMPARE(answer.metadata.type, Bella::MatchType::Media);
}

void TestBellaMatching::conflictingMdbFrameRates_data()
{
	QTest::addColumn<bool>("reverse");
	QTest::newRow("25 then 30") << false;
	QTest::newRow("30 then 25") << true;
}
void TestBellaMatching::conflictingMdbFrameRates()
{
	QFETCH(bool, reverse);
	auto first = mdb(), second = mdb(elsewhere);
	second.catalogue.files[0].sampleRate = Bella::MdbRate{{30, 1}, Bella::MdbRateEncoding::ExactEditRate};
	QVector<Bella::MdbReport> databases = reverse ? QVector<Bella::MdbReport>{second, first} : QVector<Bella::MdbReport>{first, second};
	const auto context = Bella::prepareMatchContext({listedPmr()}, std::move(databases), {folder, elsewhere});
	const auto header = mxf();
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	QVERIFY(!answer.metadata.frameRate); // A lower-priority 25 fps header cannot erase the conflict.
	QVERIFY(!answer.metadata.exactFrameRate);
	QCOMPARE(answer.metadata.project, QStringLiteral("PMR project"));
	QCOMPARE(answer.metadata.clipName, QStringLiteral("MDB clip"));
	QCOMPARE(answer.metadata.kind, Bella::MatchKind::Video);
	QVERIFY(answer.metadata.resolution);
	QCOMPARE(answer.metadata.resolution->width, qint64(1920));
	QCOMPARE(answer.metadata.bitDepth.bits, std::optional<quint32>(10));
	QVERIFY(!answer.needsHeader);
}

void TestBellaMatching::partialCompressionNamesAgree_data()
{
	QTest::addColumn<bool>("reverse");
	QTest::newRow("complete name then qualified aliases") << false;
	QTest::newRow("qualified aliases then complete name") << true;
}
void TestBellaMatching::partialCompressionNamesAgree()
{
	QFETCH(bool, reverse);
	auto complete = mdb();
	auto &file = complete.catalogue.files[0];
	file.sampleRate = Bella::MdbRate{{24000, 1001}, Bella::MdbRateEncoding::ExactEditRate};
	file.compression.codingLabel = coding("060e2b340401010a0401020271010000");
	file.compression.namingGeometry = Bella::MdbDimensions{1920, 1080};
	file.compression.frameLayout = 0;
	file.compression.horizontalSubsampling = 2;
	file.compression.verticalSubsampling = 1;
	auto aliases = complete;
	aliases.path = elsewhere + QStringLiteral("/msmMMOB.mdb");
	// Existing naming leaves main Compression unavailable for these conflicting
	// same-source facts, while independently established DNx aliases survive.
	aliases.catalogue.files[0].compression.audioEncoding = Bella::MdbAudioEncoding::IeeeFloat;
	QVector<Bella::MdbReport> databases = reverse ? QVector<Bella::MdbReport>{aliases, complete}
												  : QVector<Bella::MdbReport>{complete, aliases};
	const auto context = Bella::prepareMatchContext({listedPmr()}, std::move(databases), {folder, elsewhere});
	const auto answer = Bella::matchFile(context, physical, true, {}, false);
	QCOMPARE(answer.metadata.compression.compressionName, QStringLiteral("DNx HQX [DNxHD 175x]"));
	QCOMPARE(answer.metadata.compression.newDnx, QStringLiteral("DNx HQX"));
	QCOMPARE(answer.metadata.compression.oldDnx, QStringLiteral("DNxHD HQX"));
	QCOMPARE(answer.metadata.compression.reallyOldDnx, QStringLiteral("DNxHD 175x"));
	QVERIFY(!answer.needsHeader);
}

// MARK: - Editorial and technical fallback

void TestBellaMatching::sameMasterEditorialFallback_data()
{
	QTest::addColumn<bool>("sameMaster");
	QTest::newRow("same selected master may fill fields") << true;
	QTest::newRow("different header master cannot fill fields") << false;
}
void TestBellaMatching::sameMasterEditorialFallback()
{
	QFETCH(bool, sameMaster);
	auto database = mdb();
	auto &master = database.catalogue.masters[0];
	master.clipName.clear();
	master.originalBin.clear();
	master.type = Bella::MdbType::Unknown;
	const auto target = folder + QStringLiteral("/Take.omf");
	const auto context = Bella::prepareMatchContext({listedPmr(id(11), QStringLiteral("Take.omf"))}, {database}, {folder});
	auto header = omf();
	header.path = target;
	header.associatedClipInformation.masterMobId = id(sameMaster ? 11 : 12);
	const auto answer = Bella::matchFile(context, target, true, std::cref(header), false);
	QVERIFY(answer.metadata.masterMobId);
	QCOMPARE(*answer.metadata.masterMobId, id(11));
	QCOMPARE(answer.metadata.clipName, sameMaster ? QStringLiteral("OMF clip") : QString{});
	QCOMPARE(answer.metadata.originalBin, sameMaster ? QStringLiteral("OMF bin") : QString{});
	QCOMPARE(answer.metadata.type, sameMaster ? Bella::MatchType::Media : Bella::MatchType::Unknown);
}

void TestBellaMatching::durationIsNotRepairedByAnotherSource_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("missing MDB Length") << 0;
	QTest::newRow("missing MDB numbering") << 1;
	QTest::newRow("header Frame Rate does not repair missing MDB clock") << 2;
	QTest::newRow("known MDB zero is not missing") << 3;
}
void TestBellaMatching::durationIsNotRepairedByAnotherSource()
{
	QFETCH(int, variation);
	auto database = mdb();
	auto &file = database.catalogue.files[0];
	if (variation == 0)
		file.length.reset();
	if (variation == 1)
		file.numbering = Bella::MdbNumbering::Unavailable;
	if (variation == 2)
		file.sampleRate.reset();
	if (variation == 3)
		file.length = 0;
	const auto context = Bella::prepareMatchContext({listedPmr()}, {database}, {folder});
	const auto header = mxf();
	const auto answer = Bella::matchFile(context, physical, true, std::cref(header), false);
	QVERIFY(!answer.needsHeader);
	if (variation == 0 || variation == 2)
		QVERIFY(!answer.metadata.timing.duration);
	if (variation == 1)
	{
		QVERIFY(answer.metadata.timing.duration);
		QCOMPARE(answer.metadata.timing.duration->count, qint64(250));
		QVERIFY(!answer.metadata.timing.display);
	}
	if (variation == 2)
	{
		QVERIFY(answer.metadata.frameRate);
		QCOMPARE(answer.metadata.frameRate->words.numerator, qint32(25));
		QCOMPARE(answer.metadata.frameRate->meaning, Bella::MatchRateMeaning::Literal);
		QVERIFY(answer.metadata.exactFrameRate);
		QCOMPARE(answer.metadata.exactFrameRate->numerator, quint64(25));
		QCOMPARE(answer.metadata.exactFrameRate->denominator, quint64(1));
	}
	if (variation == 3)
	{
		QVERIFY(answer.metadata.timing.duration);
		QCOMPARE(answer.metadata.timing.duration->count, qint64(0));
		QCOMPARE(label(answer.metadata.timing), QStringLiteral("00:00:00:00"));
	}
}

void TestBellaMatching::headerFallbackAndNativeIndependence()
{
	const auto empty = Bella::prepareMatchContext({}, {}, {folder});
	const auto header = mxf();
	const auto answer = Bella::matchFile(empty, physical, true, std::cref(header), false);
	QVERIFY(answer.metadata.fileMobId);
	QCOMPARE(*answer.metadata.fileMobId, id(1));
	QVERIFY(answer.metadata.timing.duration);
	QCOMPARE(answer.metadata.timing.duration->count, qint64(500));
	QCOMPARE(label(answer.metadata.timing), QStringLiteral("00:00:20:00"));
	QCOMPARE(answer.metadata.clipName, QStringLiteral("MXF clip"));
	QVERIFY(!answer.needsHeader);
	Bella::OmfReport native;
	native.path = folder + QStringLiteral("/Take.wav");
	native.nativeAudioReading = Bella::OmfReadResult::Completed;
	native.omfReading = Bella::OmfReadResult::Malformed;
	native.sizeDateCheck = Bella::OmfCheckResult::Verified;
	native.nativeAudioInformation.kind = Bella::OmfKind::Audio;
	native.nativeAudioInformation.sampleFrameCount = 2880002;
	native.nativeAudioInformation.samplingRate = Bella::OmfNativeRate{48000, 1};
	native.nativeAudioInformation.bitDepth = 24;
	native.nativeAudioInformation.encoding = Bella::OmfAudioEncoding::Pcm;
	const auto tone = Bella::matchFile(empty, native.path, true, std::cref(native), false);
	QVERIFY(!tone.metadata.fileMobId);
	QVERIFY(!tone.metadata.masterMobId);
	QVERIFY(!tone.needsHeader);
	QCOMPARE(tone.metadata.kind, Bella::MatchKind::Audio);
	QCOMPARE(tone.metadata.compression.compressionName, QStringLiteral("PCM"));
	QVERIFY(tone.metadata.sampleRate);
	const auto rate = std::get<Bella::MatchRateWords>(*tone.metadata.sampleRate);
	QCOMPARE(rate.numerator, qint32(48000));
	QCOMPARE(rate.denominator, qint32(1));
	QVERIFY(tone.metadata.timing.duration);
	QCOMPARE(tone.metadata.timing.duration->count, qint64(2880002));
	QVERIFY(!tone.metadata.timing.display);
	QVERIFY(!tone.metadata.frameRate);
}

void TestBellaMatching::nativeTimingPriority_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("native duration wins without borrowing OMF display") << 0;
	QTest::newRow("unavailable native duration uses whole OMF timing") << 1;
	QTest::newRow("usable MDB timing wins over both header sources") << 2;
}
void TestBellaMatching::nativeTimingPriority()
{
	QFETCH(int, variation);
	const auto target = folder + QStringLiteral("/Take.wav");
	auto header = omf();
	header.path = target;
	header.nativeAudioReading = Bella::OmfReadResult::Completed;
	header.nativeAudioInformation.kind = Bella::OmfKind::Audio;
	header.nativeAudioInformation.sampleFrameCount = 2880002;
	header.nativeAudioInformation.samplingRate = Bella::OmfNativeRate{48000, 1};
	if (variation == 1)
		header.nativeAudioInformation.sampleFrameCount.reset();
	const auto context = variation == 2
							 ? Bella::prepareMatchContext({listedPmr(id(11), QStringLiteral("Take.wav"))}, {mdb()}, {folder})
							 : Bella::prepareMatchContext({}, {}, {folder});
	const auto answer = Bella::matchFile(context, target, true, std::cref(header), false);
	QVERIFY(!answer.needsHeader);
	QVERIFY(answer.metadata.timing.duration);
	const auto &timing = answer.metadata.timing;
	if (variation == 0)
	{
		QCOMPARE(timing.duration->count, qint64(2880002));
		QVERIFY(sameRate(timing.duration->rate, {48000, 1}));
		QVERIFY(!timing.display);
		QVERIFY(label(timing).isEmpty());
	}
	else
	{
		QCOMPARE(timing.duration->count, qint64(variation == 1 ? 500 : 250));
		QVERIFY(sameRate(timing.duration->rate, {25, 1}));
		QVERIFY(timing.display);
		QCOMPARE(label(timing), variation == 1 ? QStringLiteral("00:00:20:00") : QStringLiteral("00:00:10:00"));
	}
}

void TestBellaMatching::nativeAndEmbeddedTechnicalFacts_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("equal WAVE integer and native fraction") << 0;
	QTest::newRow("equal unreduced OMF and native fractions") << 1;
	QTest::newRow("equal AIFF80 and native fraction") << 2;
	QTest::newRow("different audio facts are withheld independently") << 3;
	QTest::newRow("missing native facts preserve embedded values") << 4;
}
void TestBellaMatching::nativeAndEmbeddedTechnicalFacts()
{
	QFETCH(int, variation);
	const auto target = folder + QStringLiteral("/Take.wav");
	auto header = omf();
	header.path = target;
	auto &file = header.fileInformation;
	file.kind = Bella::OmfKind::Audio;
	file.descriptorClass = tag("PCMA");
	file.length = 480000;
	file.sampleRate = Bella::OmfRate{{48000, 1}, Bella::OmfRateEncoding::ExactEditRate};
	file.audioEditRate = Bella::OmfRate{{25, 1}, Bella::OmfRateEncoding::ExactEditRate};
	file.audioFrequency = Bella::OmfAudioSourceValue(quint32(48000));
	file.bitDepth.bits = 24;
	file.bitDepth.representation = Bella::OmfDepthRepresentation::Integer;
	file.compression.codingState = Bella::OmfCodingState::Absent;
	file.compression.audioEncoding = Bella::OmfAudioEncoding::Pcm;
	const std::array<quint8, 10> frequency{0x40, 0x0e, 0xbb, 0x80, 0, 0, 0, 0, 0, 0};
	if (variation == 0)
		file.descriptorClass = tag("WAVD");
	if (variation == 1)
		file.audioFrequency = Bella::OmfAudioSourceValue(Bella::OmfRateWords{96000, 2});
	if (variation == 2)
	{
		file.descriptorClass = tag("AIFD");
		file.audioFrequency = Bella::OmfAudioSourceValue(frequency);
	}
	header.nativeAudioReading = Bella::OmfReadResult::Completed;
	auto &native = header.nativeAudioInformation;
	native.kind = Bella::OmfKind::Audio;
	native.sampleFrameCount = 2880002;
	native.samplingRate = Bella::OmfNativeRate{48000, 1};
	native.bitDepth = 24;
	native.encoding = Bella::OmfAudioEncoding::Pcm;
	if (variation == 3)
	{
		native.samplingRate = Bella::OmfNativeRate{44100, 1};
		native.bitDepth = 32;
		native.encoding = Bella::OmfAudioEncoding::IeeeFloat;
	}
	if (variation == 4)
		native = {};
	const auto context = Bella::prepareMatchContext({}, {}, {folder});
	const auto answer = Bella::matchFile(context, target, true, std::cref(header), false);
	const auto &metadata = answer.metadata;
	QVERIFY(metadata.fileMobId);
	QCOMPARE(*metadata.fileMobId, id(1));
	QCOMPARE(metadata.clipName, QStringLiteral("OMF clip"));
	QVERIFY(!answer.needsHeader);
	QCOMPARE(metadata.kind, Bella::MatchKind::Audio);
	if (variation == 3)
	{
		QVERIFY(!metadata.sampleRate);
		QVERIFY(!metadata.exactSampleRate);
		QVERIFY(!metadata.bitDepth.bits);
		QCOMPARE(metadata.bitDepth.representation, Bella::MatchDepthRepresentation::Unknown);
		QVERIFY(!metadata.interpretedBitDepth);
		QVERIFY(metadata.compression.compressionName.isEmpty());
	}
	else
	{
		QVERIFY(metadata.sampleRate);
		if (variation == 1)
		{
			const auto rate = std::get<Bella::MatchRateWords>(*metadata.sampleRate);
			QCOMPARE(rate.numerator, qint32(96000));
			QCOMPARE(rate.denominator, qint32(2));
		}
		else if (variation == 2)
		{
			const auto bytes = std::get<std::array<quint8, 10>>(*metadata.sampleRate);
			QCOMPARE(bytes, frequency);
		}
		else
			QCOMPARE(std::get<quint32>(*metadata.sampleRate), quint32(48000));
		QCOMPARE(metadata.bitDepth.bits, std::optional<quint32>(24));
		QCOMPARE(metadata.bitDepth.representation, Bella::MatchDepthRepresentation::Integer);
		QCOMPARE(metadata.compression.compressionName, QStringLiteral("PCM"));
	}
}

// MARK: - Source qualification and owned results

void TestBellaMatching::sourceQualifications_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("Changed PMR is excluded") << 0;
	QTest::newRow("Changed MDB permits already-approved header need") << 1;
	QTest::newRow("Changed physical header cannot be overridden") << 2;
	QTest::newRow("Unverified PMR metadata is retained") << 3;
	QTest::newRow("Unverified MDB metadata is retained") << 4;
	QTest::newRow("Unverified header metadata is retained") << 5;
	QTest::newRow("completed facts from Cancelled PMR survive") << 6;
	QTest::newRow("completed facts from Cancelled MDB survive") << 7;
}
void TestBellaMatching::sourceQualifications()
{
	QFETCH(int, variation);
	auto index = listedPmr();
	auto database = mdb();
	auto header = mxf();
	if (variation == 0)
	{
		index.reading = Bella::PmrReadResult::Changed;
		index.sizeDateCheck = Bella::PmrCheckResult::Changed;
		index.records = {};
	}
	if (variation == 1)
	{
		database.reading = Bella::MdbReadResult::Changed;
		database.sizeDateCheck = Bella::MdbCheckResult::Changed;
		database.catalogue = {};
	}
	if (variation == 2)
	{
		header.reading = Bella::MxfReadResult::Changed;
		header.sizeDateCheck = Bella::MxfCheckResult::Changed;
		header.fileInformation = {};
		header.associatedClipInformation = {};
	}
	if (variation == 3)
		index.sizeDateCheck = Bella::PmrCheckResult::Unverified;
	if (variation == 4)
		database.sizeDateCheck = Bella::MdbCheckResult::Unverified;
	if (variation == 5)
		header.sizeDateCheck = Bella::MxfCheckResult::Unverified;
	if (variation == 6)
		index.reading = Bella::PmrReadResult::Cancelled;
	if (variation == 7)
	{
		database.reading = Bella::MdbReadResult::Cancelled;
		database.catalogue.fileReferencesComplete = false;
	}
	const auto context = Bella::prepareMatchContext(variation == 5 ? QVector<Bella::PmrReport>{} : QVector<Bella::PmrReport>{index},
													variation == 5 ? QVector<Bella::MdbReport>{} : QVector<Bella::MdbReport>{database}, {folder});
	const auto answer = Bella::matchFile(context, physical, true,
										 variation == 2 || variation == 5 ? Bella::HeaderInput(std::cref(header)) : Bella::HeaderInput{}, false);
	if (variation == 0)
	{
		QVERIFY(!answer.metadata.fileMobId);
		QVERIFY(answer.needsHeader);
	}
	else if (variation == 1)
	{
		QVERIFY(answer.metadata.fileMobId);
		QVERIFY(answer.needsHeader);
		QVERIFY(!answer.metadata.timing.duration);
	}
	else if (variation == 2)
	{
		QVERIFY(!answer.metadata.fileMobId);
		QVERIFY(answer.metadata.project.isEmpty());
		QVERIFY(!answer.needsHeader);
		QCOMPARE(answer.localReference, Bella::LocalReference::Unresolved);
	}
	else
	{
		QVERIFY(answer.metadata.fileMobId);
		QCOMPARE(*answer.metadata.fileMobId, id(1));
		QVERIFY(!answer.needsHeader);
		QCOMPARE(answer.metadata.clipName, variation == 5 ? QStringLiteral("MXF clip") : QStringLiteral("MDB clip"));
	}
}

void TestBellaMatching::physicalEligibility()
{
	const auto context = Bella::prepareMatchContext({listedPmr()}, {mdb()}, {folder});
	const auto header = mxf();
	const auto answer = Bella::matchFile(context, physical, false, std::cref(header), false);
	QVERIFY(!answer.metadata.fileMobId);
	QVERIFY(!answer.metadata.masterMobId);
	QVERIFY(answer.metadata.clipName.isEmpty());
	QVERIFY(!answer.metadata.timing.duration);
	QCOMPARE(answer.localReference, Bella::LocalReference::Unresolved);
	QVERIFY(!answer.needsHeader);
	const auto empty = Bella::prepareMatchContext({}, {}, {folder});
	QCOMPARE(Bella::matchFile(empty, physical, false, {}, false).localReference, Bella::LocalReference::NoDatabase);
}

void TestBellaMatching::optionalEffects_data()
{
	QTest::addColumn<bool>("details");
	QTest::addColumn<bool>("precompute");
	QTest::newRow("optional work disabled") << false << true;
	QTest::newRow("confirmed Precompute with optional details") << true << true;
	QTest::newRow("effect-like Media name is not a Precompute") << true << false;
}
void TestBellaMatching::optionalEffects()
{
	QFETCH(bool, details);
	QFETCH(bool, precompute);
	auto database = mdb();
	auto &master = database.catalogue.masters[0];
	master.clipName = QStringLiteral("Sequence,Resize+2");
	master.type = precompute ? Bella::MdbType::Precompute : Bella::MdbType::Media;
	master.precomputeCategory = details && precompute ? Bella::MdbPrecomputeCategory::TitlesAndMatteKeys : Bella::MdbPrecomputeCategory::Unknown;
	const auto context = Bella::prepareMatchContext({listedPmr()}, {database}, {folder});
	const auto answer = Bella::matchFile(context, physical, true, {}, details);
	QCOMPARE(answer.metadata.effects.effectDisplayText, details && precompute ? QStringLiteral("Resize") : QString{});
	QCOMPARE(answer.metadata.effects.effectCategoryText, details && precompute ? QStringLiteral("Image") : QString{});
	QCOMPARE(answer.metadata.precomputeCategory, details && precompute ? Bella::MatchPrecomputeCategory::TitlesAndMatteKeys : Bella::MatchPrecomputeCategory::Unknown);
}

void TestBellaMatching::originalAudioFrequency_data()
{
	QTest::addColumn<int>("variation");
	QTest::newRow("original fraction") << 0;
	QTest::newRow("full unsigned32 WAVE frequency") << 1;
	QTest::newRow("original AIFF80 bytes") << 2;
}
void TestBellaMatching::originalAudioFrequency()
{
	QFETCH(int, variation);
	auto database = mdb();
	auto &file = database.catalogue.files[0];
	file.kind = Bella::MdbKind::Audio;
	file.descriptorClass = tag(variation == 0 ? "PCMA" : variation == 1 ? "WAVD"
																		: "AIFD");
	file.sampleRate = Bella::MdbRate{{48000, 1}, Bella::MdbRateEncoding::ExactEditRate};
	file.audioEditRate = Bella::MdbRate{{25, 1}, Bella::MdbRateEncoding::ExactEditRate};
	file.resolution.reset();
	file.bitDepth.rawComponentWidth.reset();
	file.bitDepth.bits = 24;
	if (variation == 1)
		file.bitDepth.storageBits = 32;
	file.compression.codingState = Bella::MdbCodingState::Absent;
	file.compression.audioEncoding = Bella::MdbAudioEncoding::Pcm;
	const std::array<quint8, 10> frequency{0x40, 0x0e, 0xbb, 0x80, 0, 0, 0, 0, 0, 0};
	if (variation == 0)
		file.audioFrequency = Bella::MdbAudioSourceValue(Bella::MdbRateWords{441001, 10});
	if (variation == 1)
		file.audioFrequency = Bella::MdbAudioSourceValue(quint32(0xffffffffu));
	if (variation == 2)
		file.audioFrequency = Bella::MdbAudioSourceValue(frequency);
	const auto context = Bella::prepareMatchContext({listedPmr()}, {database}, {folder});
	const auto answer = Bella::matchFile(context, physical, true, {}, false);
	QVERIFY(answer.metadata.sampleRate);
	QVERIFY(answer.metadata.exactSampleRate);
	const auto exactRate = *answer.metadata.exactSampleRate;
	QVERIFY(!answer.metadata.frameRate);
	if (variation == 0)
	{
		const auto rate = std::get<Bella::MatchRateWords>(*answer.metadata.sampleRate);
		QCOMPARE(rate.numerator, qint32(441001));
		QCOMPARE(rate.denominator, qint32(10));
		QVERIFY(sameRate(exactRate, {441001, 10}));
	}
	if (variation == 1)
	{
		QCOMPARE(std::get<quint32>(*answer.metadata.sampleRate), quint32(0xffffffffu));
		QVERIFY(sameRate(exactRate, {0xffffffffu, 1}));
	}
	if (variation == 2)
	{
		const auto bytes = std::get<std::array<quint8, 10>>(*answer.metadata.sampleRate);
		QCOMPARE(bytes, frequency);
		QVERIFY(sameRate(exactRate, {48000, 1}));
	}
	QCOMPARE(answer.metadata.bitDepth.bits, std::optional<quint32>(24));
	QCOMPARE(answer.metadata.bitDepth.storageBits, variation == 1 ? std::optional<quint32>(32) : std::optional<quint32>{});
}

void TestBellaMatching::ownedContextAndResultLifetime()
{
	Bella::MatchResult answer;
	{
		auto index = listedPmr();
		auto database = mdb();
		index.diagnostics = {QStringLiteral("Original PMR note.")};
		database.problemNotes = {QStringLiteral("Original MDB note.")};
		QVector<Bella::PmrReport> pmrs{index};
		QVector<Bella::MdbReport> databases{database};
		const auto context = Bella::prepareMatchContext(pmrs, databases, {folder});
		pmrs[0].records.knownSet->entries[0].project = QStringLiteral("Changed external project");
		databases[0].catalogue.masters[0].clipName = QStringLiteral("Changed external name");
		const auto copied = context;
		auto header = mxf();
		answer = Bella::matchFile(copied, physical, true, std::cref(header), false);
		header.associatedClipInformation.clipName = QStringLiteral("Changed header name");
		QCOMPARE(context.pmrSources()[0].source.path, folder + QStringLiteral("/msmFMID.pmr"));
		QCOMPARE(context.pmrSources()[0].diagnostics, QStringList{QStringLiteral("Original PMR note.")});
		QCOMPARE(context.mdbSources()[0].problemNotes, QStringList{QStringLiteral("Original MDB note.")});
		QCOMPARE(context.mdbSources()[0].catalogue.masters[0].clipName, QStringLiteral("MDB clip"));
	}
	QVERIFY(answer.metadata.fileMobId);
	QCOMPARE(*answer.metadata.fileMobId, id(1));
	QCOMPARE(answer.metadata.project, QStringLiteral("PMR project"));
	QCOMPARE(answer.metadata.clipName, QStringLiteral("MDB clip"));
	QCOMPARE(answer.metadata.originalBin, QStringLiteral("MDB bin"));
	QCOMPARE(answer.metadata.compression.compressionName, QStringLiteral("Apple ProRes 422 HQ"));
	QVERIFY(answer.metadata.timing.duration);
	QCOMPARE(answer.metadata.timing.duration->count, qint64(250));
}

QTEST_GUILESS_MAIN(TestBellaMatching)
#include "tst_bella_matching.moc"
