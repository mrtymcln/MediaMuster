#include "oldscanner/mediaengine/scancoordinator.h"
#include "oldscanner/mediaengine/scanengine.h"
#include "oldscanner/mediaengine/mdbreader.h"
#include "oldscanner/mediaengine/mxfreader.h"
#include "oldscanner/mediaengine/projection.h"
#include "oldscanner/mediaengine/metadataselectionpolicy.h"
#include "oldscanner/mediaengineadapter.h"
#include "mediametadatastatus.h"
#include "opscanreceipt.h"
#include "testutil.h"
#include "testmediaenginebento.h"
#include <QtEndian>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QTest>

namespace
{
	const QByteArray toneFileId = QByteArray::fromHex("060a2b340101010501010f10130000004a507dea741106907a361e6a605d3613");
	const QByteArray toneMasterId = QByteArray::fromHex("060a2b340101010501010f1013000000d2467dea7411069091901e6a605d3613");
	template <typename T>
	void appendNumber(QByteArray &bytes, T number)
	{
		char encoded[sizeof(T)];
		qToLittleEndian(number, encoded);
		bytes.append(encoded, sizeof(T));
	}
	QByteArray pmrRecord(const QByteArray &filename, const QByteArray &identity, const QByteArray &project,
		const QByteArray &masterIdentity = toneMasterId)
	{
		QByteArray bytes = identity;
		appendNumber<quint16>(bytes, quint16(filename.size()));
		bytes.append(filename);
		appendNumber<quint16>(bytes, quint16(project.size()));
		bytes.append(project);
		bytes.append(masterIdentity);
		appendNumber<quint32>(bytes, 0);
		return bytes;
	}
	QByteArray pmr(const QList<QByteArray> &records)
	{
		QByteArray bytes;
		appendNumber<quint32>(bytes, 0x7a9);
		appendNumber<qint32>(bytes, 8);
		appendNumber<quint32>(bytes, quint32(records.size()));
		for (const auto &record : records)
			bytes.append(record);
		return bytes;
	}
	QByteArray audioDatabase(quint16 bitDepth = 24, const QVector<quint32> &members = {101, 301},
		bool contradictoryFileIdentity = false, const QByteArray &masterIdentity = toneMasterId,
		const QList<QByteArray> &sourcePaths = {QByteArray("/Rushes/Database clip.wav")},
		const QList<QByteArray> &originalBins = {QByteArray("Database bin")},
		const QByteArray &fileIdentity = toneFileId)
	{
		using TestMediaEngineBento::number;
		TestMediaEngineBento::TypedBento writer;
		writer.head(1);
		writer.referenceArray(1, "OMFI:ObjectSpine", members, 1);
		for (const auto &object : QList<QPair<quint32, QByteArray>>{{101, "MOBJ"}, {201, "WAVD"}, {301, "MOBJ"}, {401, "TRAK"}, {501, "SCLP"}})
			writer.add(object.first, "OMFI:ObjID", "omfi:ObjectTag", object.second, true);
		writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", fileIdentity);
		writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		writer.add(101, "OMFI:CPNT:EditRate", "omfi:Rational", number<qint32>(25) + number<qint32>(1));
		const QByteArray format = number<quint16>(1) + number<quint16>(1) + number<quint32>(48000) +
								  number<quint32>(144000) + number<quint16>(3) + number(bitDepth);
		const QByteArray wave = QByteArray("WAVEfmt ") + number<quint32>(format.size()) + format;
		writer.add(201, "OMFI:WAVD:Summary", "omfi:DataValue", QByteArray("RIFF") + number<quint32>(wave.size()) + wave);
		writer.add(201, "OMFI:MDFL:SampleRate", "omfi:Rational", number<qint32>(48000) + number<qint32>(1));
		writer.add(201, "OMFI:MDFL:Length", "omfi:Length32", number<qint32>(96000));
		writer.add(301, "OMFI:MOBJ:MobID", "omfi:UID", masterIdentity);
		writer.add(301, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", number<quint32>(7), true);
		writer.add(301, "OMFI:CPNT:Name", "omfi:String", QByteArray("Database clip\0", 14));
		writer.add(301, "OMFI:TRKG:Tracks", "omfi:ObjRefArray", number<quint16>(1) + writer.reference(401, 1));
		writer.add(401, "OMFI:TRAK:LabelNumber", "omfi:UInt16", number<quint16>(1), true);
		writer.add(401, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(501, 1));
		writer.add(501, "OMFI:SCLP:SourceID", "omfi:UID", fileIdentity);
		if (!sourcePaths.isEmpty())
		{
			writer.add(701, "OMFI:ObjID", "omfi:ObjectTag", "ATTR", true);
			writer.add(101, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(701, 1));
			QVector<quint32> attributes;
			quint32 handle = 702;
			for (const auto &path : sourcePaths)
			{
				attributes.append(handle);
				writer.add(handle, "OMFI:ObjID", "omfi:ObjectTag", "ATTB", true);
				writer.add(handle, "OMFI:ATTB:Name", "omfi:String", QByteArray("UNC Path\0", 9));
				writer.add(handle, "OMFI:ATTB:Kind", "omfi:AttrKind", number<quint16>(2));
				writer.add(handle, "OMFI:ATTB:StringAttribute", "omfi:String", path + '\0');
				++handle;
			}
			writer.referenceArray(701, "OMFI:ATTR:AttrRefs", attributes, 1);
		}
		if (!originalBins.isEmpty())
		{
			writer.add(801, "OMFI:ObjID", "omfi:ObjectTag", "ATTR", true);
			writer.add(301, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(801, 1));
			QVector<quint32> attributes;
			quint32 handle = 802;
			for (const auto &bin : originalBins)
			{
				const quint32 binHandle = handle + 200;
				attributes.append(handle);
				writer.add(handle, "OMFI:ObjID", "omfi:ObjectTag", "ATTB", true);
				writer.add(handle, "OMFI:ATTB:Name", "omfi:String", QByteArray("_ORG_BIN\0", 9));
				writer.add(handle, "OMFI:ATTB:Kind", "omfi:AttrKind", number<quint16>(3));
				writer.add(handle, "OMFI:ATTB:ObjAttribute", "omfi:ObjRef", writer.reference(binHandle, 1));
				writer.add(binHandle, "OMFI:ObjID", "omfi:ObjectTag", "MCBR", true);
				writer.add(binHandle, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", bin + '\0');
				++handle;
			}
			writer.referenceArray(801, "OMFI:ATTR:AttrRefs", attributes, 1);
		}
		if (contradictoryFileIdentity)
		{
			QByteArray alternative = fileIdentity;
			alternative[31] = char(0x14);
			writer.add(601, "OMFI:ObjID", "omfi:ObjectTag", "MOBJ", true);
			writer.add(601, "OMFI:MOBJ:MobID", "omfi:UID", fileIdentity);
			writer.add(601, "OMFI:MOBJ:MobID", "omfi:UID", alternative);
			writer.add(601, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		}
		return writer.build();
	}
	TestMediaEngineBento::TypedBento fileReferenceDatabase(const QByteArray &identity = toneFileId,
		const QByteArray &descriptorClass = "WAVD")
	{
		TestMediaEngineBento::TypedBento writer;
		writer.head(1);
		writer.referenceArray(1, "OMFI:ObjectSpine", {101}, 1);
		writer.add(101, "OMFI:ObjID", "omfi:ObjectTag", "MOBJ", true);
		writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", identity);
		writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		writer.add(201, "OMFI:ObjID", "omfi:ObjectTag", descriptorClass, true);
		return writer;
	}
	const MediaEngine::SourceReceipt *mediaSource(const MediaEngine::ScanResult &scan, const QString &path)
	{
		for (const auto &source : scan.sources)
			if (source.snapshot && source.snapshot->path == path)
				return &source;
		return nullptr;
	}
	void observeFact(MediaEngine::ProjectedFile &facts, MediaProperty property,
		const SourceSnapshotRef &source, const QVariant &value)
	{
		MetadataObservation observation;
		observation.snapshot = source;
		observation.property = mediaPropertyName(property);
		observation.objectIdentity = QStringLiteral("authored grouped facts");
		observation.value = value;
		observation.readState = PropertyReadState::Present;
		facts.evidence.observe(property, std::move(observation));
	}
}

class TestMediaEngineScan : public QObject
{
	Q_OBJECT
private slots:
	void local_database_reference_results_data()
	{
		using Outcome = MediaEngine::ParsedSource::Outcome;
		using Status = MediaFile::DbStatus;
		QTest::addColumn<QByteArray>("databaseBytes");
		QTest::addColumn<QByteArray>("pmrBytes");
		QTest::addColumn<bool>("genuineHeader");
		QTest::addColumn<int>("expectedOutcome");
		QTest::addColumn<int>("expectedStatus");
		const auto add = [&](const char *name, const QByteArray &bytes, bool header,
			Status status, int outcome = int(Outcome::Complete), const QByteArray &localPmr = {})
		{
			QTest::newRow(name) << bytes << localPmr << header << outcome << int(status);
		};
		const auto matching = fileReferenceDatabase().build();
		add("same-folder-mdb-alone-lists-file", matching, true, Status::Listed);
		add("pmr-miss-does-not-hide-mdb-reference", matching, true, Status::Listed,
			int(Outcome::Complete), pmr({}));
		auto partial = fileReferenceDatabase();
		// An unrelated named UTF-8 value fails without damaging this file reference.
		partial.add(901, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", QByteArray::fromHex("ff00"));
		add("qualified-reference-survives-unrelated-partial-read", partial.build(), true,
			Status::Listed, int(Outcome::Incomplete));
		add("master-only-is-not-a-physical-reference", audioDatabase(24, {301}), true, Status::NoReference);
		QByteArray otherId = toneFileId;
		otherId[31] ^= 1;
		add("complete-known-file-catalogue-miss", fileReferenceDatabase(otherId).build(), true, Status::NoReference);
		TestMediaEngineBento::TypedBento empty;
		empty.head(1);
		empty.referenceArray(1, "OMFI:ObjectSpine", {}, 1);
		add("empty-qualified-catalogue", empty.build(), true, Status::NoReference);
		add("empty-qualified-catalogue-needs-no-physical-id", empty.build(), false, Status::NoReference);
		TestMediaEngineBento::TypedBento unknownSchema;
		unknownSchema.add(101, "Vendor:Unknown", "vendor:Type", "retained");
		add("parsed-complete-does-not-establish-reference-coverage", unknownSchema.build(), true, Status::DbUnreadable);
		add("unreadable-mdb-without-physical-id", "Unreadable database", false, Status::DbUnreadable, -1);
		add("nonempty-catalogue-cannot-check-unknown-physical-id", matching, false, Status::DbUnreadable);
		add("unknown-descriptor-does-not-prove-absence", fileReferenceDatabase(toneFileId, "ZZZZ").build(), true, Status::DbUnreadable);
		auto conflicting = fileReferenceDatabase();
		conflicting.add(101, "OMFI:MOBJ:MobID", "omfi:UID", otherId);
		add("ambiguous-owner-does-not-prove-reference-or-absence", conflicting.build(), true, Status::DbUnreadable);
		auto unresolved = fileReferenceDatabase();
		unresolved.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", unresolved.reference(999, 1));
		add("unresolved-owner-does-not-prove-absence", unresolved.build(), true, Status::DbUnreadable);
		auto brokenRoot = fileReferenceDatabase();
		brokenRoot.referenceArray(1, "OMFI:ObjectSpine", {999}, 1);
		add("broken-root-does-not-prove-absence", brokenRoot.build(), true, Status::DbUnreadable);
	}
	void local_database_reference_results()
	{
		QFETCH(QByteArray, databaseBytes);
		QFETCH(QByteArray, pmrBytes);
		QFETCH(bool, genuineHeader);
		QFETCH(int, expectedOutcome);
		QFETCH(int, expectedStatus);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		const QString database = folder + QStringLiteral("/metadata.mdb");
		QVERIFY(QDir().mkpath(folder));
		if (genuineHeader)
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		else
			QVERIFY(tryWriteFile(path, "Physical identity is unavailable"));
		QVERIFY(tryWriteFile(database, databaseBytes));
		if (!pmrBytes.isEmpty())
			QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmrBytes));
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate) { ++reads[candidate.path]; };
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.reconciliationComplete);
		const auto *source = mediaSource(scan, database);
		QVERIFY(source);
		if (expectedOutcome >= 0)
			QCOMPARE(int(source->outcome), expectedOutcome);
		else
			QVERIFY(source->outcome != MediaEngine::ParsedSource::Outcome::Complete);
		QCOMPARE(reads.value(path), 1);
		QCOMPARE(reads.value(database), 1);
		for (const auto count : reads)
			QCOMPARE(count, 1);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QCOMPARE(header->outcome, genuineHeader ? MediaEngine::ParsedSource::Outcome::Complete
			: MediaEngine::ParsedSource::Outcome::Unsupported);
		const auto &file = scan.files.front();
		QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), expectedStatus);
		QCOMPARE(file.stamp.fileMobId, genuineHeader ? MediaEngine::canonicalDatabaseId(toneFileId) : QString{});
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan));
		QCOMPARE(int(row.dbStatus), expectedStatus);
		QVERIFY(row.operationScanReceipt);
		QCOMPARE(row.operationScanReceipt->headerIdentity.fileMobId,
			genuineHeader ? MediaEngine::canonicalDatabaseId(toneFileId) : QString{});
		QVERIFY(row.operationScanReceipt->databaseFileMobIdToVerify.isEmpty());
	}

	void completed_local_reference_check_wins_over_failed_sibling_data()
	{
		QTest::addColumn<bool>("checkMdb");
		QTest::addColumn<bool>("listed");
		QTest::newRow("pmr-miss-and-failed-mdb") << false << false;
		QTest::newRow("pmr-hit-and-failed-mdb") << false << true;
		QTest::newRow("mdb-miss-and-failed-pmr") << true << false;
		QTest::newRow("mdb-hit-and-failed-pmr") << true << true;
	}
	void completed_local_reference_check_wins_over_failed_sibling()
	{
		QFETCH(bool, checkMdb);
		QFETCH(bool, listed);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(QDir().mkpath(folder));
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		QByteArray otherId = toneFileId;
		otherId[31] ^= 1;
		const QString good = folder + (checkMdb ? QStringLiteral("/metadata.mdb") : QStringLiteral("/index.pmr"));
		const QString bad = folder + (checkMdb ? QStringLiteral("/broken.pmr") : QStringLiteral("/broken.mdb"));
		QVERIFY(tryWriteFile(good, checkMdb ? fileReferenceDatabase(listed ? toneFileId : otherId).build()
			: pmr(listed ? QList<QByteArray>{pmrRecord("take.mxf", toneFileId, "Project")} : QList<QByteArray>{})));
		QVERIFY(tryWriteFile(bad, "Cannot interpret this database"));
		QStringList warnings;
		int headerReads = 0;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.warning = [&](const QString &message) { warnings.append(message); };
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			if (candidate.path == path)
				++headerReads;
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QCOMPARE(headerReads, 1);
		QVERIFY(std::any_of(warnings.cbegin(), warnings.cend(), [&](const QString &message) { return message.contains(bad); }));
		const auto expected = listed ? MediaFile::DbStatus::Listed : MediaFile::DbStatus::NoReference;
		QCOMPARE(scan.files.front().evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(expected));
		QCOMPARE(mediaEngineMediaFile(scan.files.front(), ScanRowLookup(scan)).dbStatus, expected);
	}

	void another_folders_database_enriches_metadata_without_local_membership()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString first = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString second = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/2");
		const QString path = first + QStringLiteral("/take.mxf");
		const QString database = second + QStringLiteral("/metadata.mdb");
		QVERIFY(QDir().mkpath(first));
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		QVERIFY(tryWriteFile(database, audioDatabase()));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		const auto &file = scan.files.front();
		QCOMPARE(file.evidence.selected(MediaProperty::SourcePath).value.toString(), QStringLiteral("/Rushes/Database clip.wav"));
		const auto sourcePath = file.evidence.selected(MediaProperty::SourcePath);
		QVERIFY(sourcePath.selectedObservation >= 0);
		QCOMPARE(file.evidence.observations(MediaProperty::SourcePath)[sourcePath.selectedObservation].snapshot->path, database);
		QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::NoDatabase));
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan));
		QCOMPARE(row.dbStatus, MediaFile::DbStatus::NoDatabase);
		QCOMPARE(row.sourceFilePath, QStringLiteral("/Rushes/Database clip.wav"));
	}

	void partial_pmr_reference_does_not_supply_partial_metadata_data()
	{
		QTest::addColumn<QByteArray>("records");
		QTest::addColumn<int>("expectedStatus");
		const auto complete = pmrRecord("take.mxf", toneFileId, "Partial metadata must stay excluded");
		QTest::newRow("validated-record-before-truncated-record")
			<< pmr({complete, toneFileId.first(4)}) << int(MediaFile::DbStatus::Listed);
		QTest::newRow("validated-id-and-filename-before-project-truncation")
			<< pmr({complete.first(toneFileId.size() + 2 + QByteArray("take.mxf").size())}) << int(MediaFile::DbStatus::Listed);
		QTest::newRow("unreadable-filename-cannot-establish-reference")
			<< pmr({complete.first(toneFileId.size() + 3)}) << int(MediaFile::DbStatus::DbUnreadable);
		QByteArray otherId = toneFileId;
		otherId[31] ^= 1;
		QTest::newRow("conflicting-validated-records-stay-unresolved")
			<< pmr({complete, pmrRecord("take.mxf", otherId, "Partial metadata must stay excluded"), toneFileId.first(4)})
			<< int(MediaFile::DbStatus::DbUnreadable);
	}
	void partial_pmr_reference_does_not_supply_partial_metadata()
	{
		QFETCH(QByteArray, records);
		QFETCH(int, expectedStatus);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		const QString database = folder + QStringLiteral("/index.pmr");
		QVERIFY(tryWriteFile(path, "Physical identity is unavailable"));
		QVERIFY(tryWriteFile(database, records));
		int headerReads = 0;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			if (candidate.path == path)
				++headerReads;
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		const auto *source = mediaSource(scan, database);
		QVERIFY(source);
		QCOMPARE(source->outcome, MediaEngine::ParsedSource::Outcome::Incomplete);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QCOMPARE(header->outcome, MediaEngine::ParsedSource::Outcome::Unsupported);
		QCOMPARE(headerReads, 1);
		const auto &file = scan.files.front();
		QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), expectedStatus);
		QVERIFY(!file.evidence.selected(MediaProperty::FileMobId).value.isValid());
		QVERIFY(!file.evidence.selected(MediaProperty::Project).value.isValid());
		for (const auto property : {MediaProperty::Filename, MediaProperty::FileMobId, MediaProperty::MasterMobId, MediaProperty::Project})
			for (const auto &observation : file.evidence.observations(property))
				if (observation.snapshot && observation.snapshot->source == MetadataSource::Pmr)
					QVERIFY(!observation.eligible);
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan));
		QCOMPARE(int(row.dbStatus), expectedStatus);
		QVERIFY(row.operationScanReceipt);
		QVERIFY(row.operationScanReceipt->headerIdentity.fileMobId.isEmpty());
		QVERIFY(row.operationScanReceipt->databaseFileMobIdToVerify.isEmpty());
	}

	void changed_sources_cannot_establish_local_database_membership_data()
	{
		QTest::addColumn<bool>("changePhysicalFile");
		QTest::newRow("changed-database") << false;
		QTest::newRow("changed-physical-file") << true;
	}
	void changed_sources_cannot_establish_local_database_membership()
	{
		QFETCH(bool, changePhysicalFile);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		const QString database = folder + QStringLiteral("/metadata.mdb");
		QVERIFY(QDir().mkpath(folder));
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		QVERIFY(tryWriteFile(database, audioDatabase()));
		const QString changedPath = changePhysicalFile ? path : database;
		const auto originalTime = QFileInfo(changedPath).lastModified();
		bool changed = false;
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			++reads[candidate.path];
			if (candidate.path != path)
				return;
			QFile file(changedPath);
			changed = file.open(QIODevice::ReadWrite) &&
				file.setFileTime(originalTime.addSecs(10), QFileDevice::FileModificationTime);
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(changed);
		QCOMPARE(scan.files.size(), 1);
		QCOMPARE(reads.value(path), 1);
		QCOMPARE(reads.value(database), 1);
		QVERIFY(std::any_of(scan.reconciliationIssues.cbegin(), scan.reconciliationIssues.cend(), [&](const ScanIssue &issue)
			{ return issue.kind == ScanIssue::Kind::SourceChanged && issue.expectedPath == changedPath; }));
		const auto &file = scan.files.front();
		QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::DbUnreadable));
		QCOMPARE(mediaEngineMediaFile(file, ScanRowLookup(scan)).dbStatus, MediaFile::DbStatus::DbUnreadable);
		for (const auto &observation : file.evidence.observations(MediaProperty::FileMobId))
			if (observation.snapshot && observation.snapshot->path == changedPath)
				QVERIFY(!observation.eligible);
	}

	void conflicting_active_database_identity_cannot_disappear_during_matching()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(tryWriteFile(path, "Unreadable header"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
			pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"),
			audioDatabase(24, {101, 301, 601}, true)));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(mediaSource(scan, path)->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		const auto &evidence = scan.files.front().evidence;
		const auto identity = evidence.selected(MediaProperty::FileMobId);
		QCOMPARE(identity.agreement, PropertyAgreement::Conflicting);
		QVERIFY(!identity.value.isValid());
		QVERIFY(!evidence.selected(MediaProperty::Compression).value.isValid());
		QSet<QString> databaseClaims;
		for (const auto &claim : evidence.observations(MediaProperty::FileMobId))
			if (claim.snapshot->source == MetadataSource::Mdb && claim.eligible &&
				claim.readState == PropertyReadState::Present)
				databaseClaims.insert(claim.value.toString());
		QCOMPARE(databaseClaims.size(), 2);
	}

	void damaged_database_contents_list_reads_header_and_keeps_physical_row()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(tryWriteFile(path, "Unreadable header"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
			pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase(24, {101, 999})));
		const MediaEngine::Cancellation cancellation;
		QStringList warnings;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.warning = [&](const QString &message) { warnings.append(message); };
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.files.front().kelpieId != 0);
		QVERIFY(mediaSource(scan, path)->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		const auto &evidence = scan.files.front().evidence;
		QVERIFY(!evidence.selected(MediaProperty::Compression).value.isValid());
		QCOMPARE(evidence.selected(MediaProperty::Project).value.toString(), QStringLiteral("Project"));
		QVERIFY(std::any_of(warnings.cbegin(), warnings.cend(), [](const QString &message)
			{ return message.contains(QStringLiteral("ObjectSpine")); }));
		const auto *database = mediaSource(scan, folder + QStringLiteral("/metadata.mdb"));
		QVERIFY(database);
		// Check the actual unreadable contents-list relationship through the reader.
		QFile input(database->snapshot->path);
		QVERIFY(input.open(QIODevice::ReadOnly));
		const auto parsed = MediaEngine::MdbReader{}.read(input, {database->snapshot, cancellation});
		QCOMPARE(database->outcome, parsed.outcome);
		QCOMPARE(database->snapshot->readState, parsed.snapshot->readState);
		QVERIFY(!parsed.objects.isEmpty());
		QVERIFY(std::any_of(parsed.relationships.cbegin(), parsed.relationships.cend(),
			[](const MediaEngine::Relationship &link)
			{ return link.locator.name == QLatin1String("OMFI:ObjectSpine") && link.target == 0; }));
	}

	void unlisted_database_master_cannot_supply_clip_name()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(tryWriteFile(path, "Unreadable header: the physical file must still keep its row."));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
			pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		// The master is readable in the database, but its contents list omits it.
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase(24, {101})));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.files.front().kelpieId != 0);
		QVERIFY(mediaSource(scan, path)->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		const auto &evidence = scan.files.front().evidence;
		QVERIFY(!evidence.selected(MediaProperty::ClipName).value.isValid());
		QCOMPARE(evidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("PCM"));
		QCOMPARE(evidence.selected(MediaProperty::FileMobId).value.toString(),
			MediaEngine::canonicalDatabaseId(toneFileId));
	}

	void complete_database_without_master_track_lengths_does_not_open_media_header()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(tryWriteFile(path, "This would fail if a media reader opened it."));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase()));
		const MediaEngine::Cancellation cancellation;
		QStringList decisions;
		MediaEngine::ScanCallbacks callbacks;
		QHash<QString, int> reads;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate) { ++reads[candidate.path]; };
		callbacks.progress = [&](int, int, const QString &source)
		{ decisions.append(source); };
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.reconciliationComplete);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QVERIFY2(header->outcome == MediaEngine::ParsedSource::Outcome::NotRead, qPrintable(header->readReason));
		QCOMPARE(header->snapshot->readState, SourceReadState::NotRead);
		QCOMPARE(reads.value(path), 0);
		QCOMPARE(reads.value(folder + QStringLiteral("/metadata.mdb")), 1);
		QCOMPARE(reads.size(), 2);
		QCOMPARE(decisions.last(), path);
		const auto &evidence = scan.files.front().evidence;
		QCOMPARE(evidence.readStatus(MediaProperty::Compression, header->snapshot).state, PropertyReadState::NotRead);
		QCOMPARE(evidence.readStatus(MediaProperty::Compression, header->snapshot).reason, PropertyReadReason::SourceNotRead);
		for (const auto &source : scan.sources)
			if (source.snapshot->source == MetadataSource::Pmr)
			{
				const auto absent = evidence.readStatus(MediaProperty::Compression, source.snapshot);
				QCOMPARE(absent.state, PropertyReadState::Absent);
				QCOMPARE(absent.reason, PropertyReadReason::NotStoredByFormat);
				QCOMPARE(evidence.readStatus(MediaProperty::Modified, source.snapshot).reason, PropertyReadReason::UnsupportedInterpretation);
			}
		QCOMPARE(evidence.selected(MediaProperty::ClipName).value.toString(), QStringLiteral("Database clip"));
		QCOMPARE(evidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("PCM"));
		QCOMPARE(evidence.selected(MediaProperty::FileMobId).value.toString(),
			MediaEngine::canonicalDatabaseId(toneFileId));
		QCOMPARE(evidence.selected(MediaProperty::MasterMobId).value.toString(),
			MediaEngine::canonicalDatabaseId(toneMasterId));
		const auto duration = MediaEngine::mediaDuration(evidence.selected(MediaProperty::FileDuration).value);
		QCOMPARE(duration.units, qint64(96000));
		QCOMPARE(duration.rate.numerator, 48000);
		QCOMPARE(duration.rate.denominator, 1);
		QCOMPARE(duration.displayRate.numerator, 25);
		QCOMPARE(duration.displayRate.denominator, 1);
		QCOMPARE(evidence.selected(MediaProperty::OriginalBin).value.toString(), QStringLiteral("Database bin"));
		QCOMPARE(evidence.selected(MediaProperty::SourcePath).value.toString(), QStringLiteral("/Rushes/Database clip.wav"));
		QCOMPARE(evidence.selected(MediaProperty::Project).value.toString(), QStringLiteral("Project"));
		const auto row = mediaEngineMediaFile(scan.files.front(), ScanRowLookup(scan), temporary.path(), QStringLiteral("Volume"), false);
		QCOMPARE(row.kind, MediaFile::Kind::Audio);
		QVERIFY(row.resolution.isEmpty());
		const auto &resolution = row.metadataStatus.field(MediaProperty::Resolution);
		QCOMPARE(resolution.applicability, PropertyApplicability::NotApplicable);
		QVERIFY(!resolution.hasSelectedValue);
		for (const auto &value : evidence.observations(MediaProperty::Compression))
			if (value.readState == PropertyReadState::Present)
			{
				QCOMPARE(value.snapshot->source, MetadataSource::Mdb);
				QCOMPARE(value.freshness, SourceFreshness::Unknown);
			}
	}

	void source_path_controls_header_fallback_data()
	{
		QTest::addColumn<QList<QByteArray>>("recordedPaths");
		QTest::addColumn<bool>("readHeader");
		QTest::addColumn<QString>("selectedPath");
		const QByteArray native("C:\\Rushes\\Interview.mov");
		const QByteArray url("file:///Rushes/Interview%2520take.mov");
		QTest::newRow("native-path") << QList<QByteArray>{native} << false << QString::fromUtf8(native);
		QTest::newRow("recorded-url") << QList<QByteArray>{url} << false << QString::fromUtf8(url);
		QTest::newRow("absent-path") << QList<QByteArray>{} << true << QString{};
		QTest::newRow("empty-path") << QList<QByteArray>{QByteArray{}} << true << QString{};
		QTest::newRow("conflicting-paths-with-same-basename")
			<< QList<QByteArray>{QByteArray("/Rushes/Interview.mov"), QByteArray("/Archive/Interview.mov")}
			<< true << QString{};
	}

	void source_path_controls_header_fallback()
	{
		QFETCH(QList<QByteArray>, recordedPaths);
		QFETCH(bool, readHeader);
		QFETCH(QString, selectedPath);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(QDir().mkpath(folder));
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
			pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"),
			audioDatabase(24, {101, 301}, false, toneMasterId, recordedPaths)));
		int headerReads = 0;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			if (candidate.path == path)
				++headerReads;
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.reconciliationComplete);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QCOMPARE(headerReads, readHeader ? 1 : 0);
		QCOMPARE(header->outcome == MediaEngine::ParsedSource::Outcome::NotRead, !readHeader);
		if (readHeader)
			QCOMPARE(header->readReason,
				QStringLiteral("Header read: required table metadata missing or conflicting: Source Path"));
		const auto &file = scan.files.front();
		QCOMPARE(file.evidence.selected(MediaProperty::SourcePath).value.toString(), selectedPath);
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan), temporary.path(), QStringLiteral("Volume"), false);
		QCOMPARE(row.sourceFilePath, selectedPath);
		QCOMPARE(row.mediaFilePath, path);
		QCOMPARE(row.kind, MediaFile::Kind::Audio);
		QVERIFY(row.resolution.isEmpty());
		QCOMPARE(row.metadataStatus.field(MediaProperty::Resolution).applicability, PropertyApplicability::NotApplicable);
		const auto &status = row.metadataStatus.field(MediaProperty::SourcePath);
		if (recordedPaths.size() > 1)
		{
			QCOMPARE(status.agreement, PropertyAgreement::Conflicting);
			QCOMPARE(status.readState, PropertyReadState::Present);
			QVERIFY(!status.hasSelectedValue);
		}
		if (!selectedPath.isEmpty())
		{
			QCOMPARE(status.readState, PropertyReadState::Present);
			QVERIFY(status.selectedSource);
			QCOMPARE(*status.selectedSource, MetadataSource::Mdb);
		}
	}
	void original_bin_controls_header_fallback_data()
	{
		QTest::addColumn<bool>("legacyFamily");
		QTest::addColumn<bool>("includeOmf");
		QTest::addColumn<QList<QByteArray>>("recordedBins");
		QTest::addColumn<bool>("readHeader");
		QTest::addColumn<QString>("selectedBin");
		QTest::addColumn<bool>("completeSibling");
		QTest::addColumn<bool>("missingSourcePath");
		const QString unicode = QStringLiteral("資料 originals");
		for (const bool legacy : {false, true})
		{
			const QByteArray prefix = legacy ? QByteArray("omf-") : QByteArray("mxf-");
			QTest::newRow((prefix + "present-bin").constData())
				<< legacy << legacy << QList<QByteArray>{unicode.toUtf8()} << false << unicode << false << false;
			QTest::newRow((prefix + "absent-bin").constData())
				<< legacy << legacy << QList<QByteArray>{} << legacy << QString{} << false << false;
			QTest::newRow((prefix + "empty-bin").constData())
				<< legacy << legacy << QList<QByteArray>{QByteArray{}} << legacy << QString{} << false << false;
			QTest::newRow((prefix + "unreadable-bin").constData())
				<< legacy << legacy << QList<QByteArray>{QByteArray::fromHex("c328")} << legacy << QString{} << true << false;
			QTest::newRow((prefix + "conflicting-bins").constData())
				<< legacy << legacy << QList<QByteArray>{QByteArray("First bin"), QByteArray("Second bin")}
				<< true << QString{} << false << false;
		}
		QTest::newRow("mxf-absent-bin-and-source-path")
			<< false << false << QList<QByteArray>{} << true << QString{} << false << true;
		QTest::newRow("mxf-absent-bin-with-omf-scanning-enabled")
			<< false << true << QList<QByteArray>{} << false << QString{} << false << false;
	}
	void original_bin_controls_header_fallback()
	{
		QFETCH(bool, legacyFamily);
		QFETCH(bool, includeOmf);
		QFETCH(QList<QByteArray>, recordedBins);
		QFETCH(bool, readHeader);
		QFETCH(QString, selectedBin);
		QFETCH(bool, completeSibling);
		QFETCH(bool, missingSourcePath);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + (legacyFamily ? QStringLiteral("/OMFI MediaFiles")
			: QStringLiteral("/Avid MediaFiles/MXF/1"));
		const QByteArray filename = legacyFamily ? QByteArray("take.wav") : QByteArray("take.mxf");
		const QString path = folder + QLatin1Char('/') + QString::fromLatin1(filename);
		QVERIFY(QDir().mkpath(folder));
		if (legacyFamily)
		{
			// A complete native WAVE is handled by the legacy reader but has no
			// embedded OMF bin metadata. Genuine OMF recovery is checked below.
			using TestMediaEngineBento::number;
			const QByteArray format = number<quint16>(1) + number<quint16>(1) + number<quint32>(48000) +
				number<quint32>(144000) + number<quint16>(3) + number<quint16>(24);
			const QByteArray body = QByteArray("WAVEfmt ") + number<quint32>(format.size()) + format +
				QByteArray("data") + number<quint32>(0);
			QVERIFY(tryWriteFile(path, QByteArray("RIFF") + number<quint32>(body.size()) + body));
		}
		else
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
			pmr({pmrRecord(filename, toneFileId, "Project")})));
		const QList<QByteArray> sourcePaths = missingSourcePath ? QList<QByteArray>{}
			: QList<QByteArray>{QByteArray("/Rushes/Database clip.wav")};
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"),
			audioDatabase(24, {101, 301}, false, toneMasterId, sourcePaths, recordedBins)));
		if (completeSibling)
		{
			// Keep a complete matching source available, so the unreadable bin
			// exercises the field requirement rather than the whole-source gate.
			QVERIFY(tryWriteFile(folder + QStringLiteral("/complete.mdb"),
				audioDatabase(24, {101, 301}, false, toneMasterId, sourcePaths, {})));
		}
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{ ++reads[candidate.path]; };
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{legacyFamily ? folder : QFileInfo(folder).absolutePath()}, includeOmf}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.parsingComplete);
		QVERIFY(scan.reconciliationComplete);
		QCOMPARE(reads.value(folder + QStringLiteral("/index.pmr")), 1);
		QCOMPARE(reads.value(folder + QStringLiteral("/metadata.mdb")), 1);
		QCOMPARE(reads.value(folder + QStringLiteral("/complete.mdb")), completeSibling ? 1 : 0);
		QCOMPARE(reads.value(path), readHeader ? 1 : 0);
		QCOMPARE(reads.size(), 2 + int(completeSibling) + int(readHeader));
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QCOMPARE(header->outcome == MediaEngine::ParsedSource::Outcome::NotRead, !readHeader);
		if (readHeader)
			QCOMPARE(header->readReason,
				missingSourcePath ? QStringLiteral("Header read: required table metadata missing or conflicting: Source Path")
					: QStringLiteral("Header read: required table metadata missing or conflicting: Original Bin"));
		const auto &file = scan.files.front();
		QCOMPARE(file.stamp.fileMobId, MediaEngine::canonicalDatabaseId(toneFileId));
		QCOMPARE(file.stamp.masterMobId, MediaEngine::canonicalDatabaseId(toneMasterId));
		QCOMPARE(file.evidence.selected(MediaProperty::OriginalBin).value.toString(), selectedBin);
		QCOMPARE(file.evidence.selected(MediaProperty::SourcePath).value.toString(),
			missingSourcePath ? QString{} : QStringLiteral("/Rushes/Database clip.wav"));
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan), temporary.path(), QStringLiteral("Volume"), false);
		QCOMPARE(row.originalBin, selectedBin);
		QCOMPARE(row.masterMobId, file.stamp.masterMobId);
		QCOMPARE(row.kind, MediaFile::Kind::Audio);
		QVERIFY(row.resolution.isEmpty());
		QCOMPARE(row.metadataStatus.field(MediaProperty::Resolution).applicability, PropertyApplicability::NotApplicable);
		const auto &status = row.metadataStatus.field(MediaProperty::OriginalBin);
		QVERIFY(status.captured);
		QCOMPARE(status.hasSelectedValue, !selectedBin.isEmpty());
		if (!selectedBin.isEmpty())
		{
			QCOMPARE(status.readState, PropertyReadState::Present);
			QVERIFY(status.selectedSource);
			QCOMPARE(*status.selectedSource, MetadataSource::Mdb);
		}
		else
		{
			// Neither this MXF header nor the native WAVE carries bin metadata.
			// An unread header and unestablished field coverage stay distinct.
			const auto coverage = file.evidence.readStatus(MediaProperty::OriginalBin, header->snapshot);
			QCOMPARE(coverage.state, PropertyReadState::NotRead);
			QCOMPARE(coverage.reason, readHeader ? PropertyReadReason::CoverageNotEstablished
				: PropertyReadReason::SourceNotRead);
			QVERIFY(status.readState != PropertyReadState::Absent);
			QVERIFY(!status.selectedSource);
		}
		if (recordedBins.size() > 1)
		{
			QCOMPARE(status.agreement, PropertyAgreement::Conflicting);
			QCOMPARE(status.readState, PropertyReadState::Present);
		}
	}
	void missing_database_bin_reads_genuine_omf_header()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/OMFI MediaFiles");
		const QString path = folder + QStringLiteral("/take.omf");
		QVERIFY(QDir().mkpath(folder));
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/omf/avid_supporting/BLACK_720x243x2_UNCOMP.omf"), path));
		const MediaEngine::Cancellation cancellation;
		const auto headerOnly = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/OMFI MediaFiles")}, true}, cancellation);
		QCOMPARE(headerOnly.files.size(), 1);
		const auto &baseline = headerOnly.files.front();
		QCOMPARE(baseline.evidence.selected(MediaProperty::OriginalBin).value.toString(),
			QStringLiteral("Uncompressed media slides"));
		QString fileId = baseline.stamp.fileMobId;
		QString masterId = baseline.stamp.masterMobId;
		fileId.remove(QLatin1Char('.'));
		masterId.remove(QLatin1Char('.'));
		const QByteArray fileIdentity = QByteArray::fromHex(fileId.toLatin1());
		const QByteArray masterIdentity = QByteArray::fromHex(masterId.toLatin1());
		QCOMPARE(fileIdentity.size(), 32);
		QCOMPARE(masterIdentity.size(), 32);
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
			pmr({pmrRecord("take.omf", fileIdentity, "Project", masterIdentity)})));
		// The authored matching database supplies the other required values;
		// the real header must be requested specifically for the missing bin.
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"),
			audioDatabase(24, {101, 301}, false, masterIdentity,
				{QByteArray("/Rushes/Black720x486.PICT")}, {}, fileIdentity)));
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{ ++reads[candidate.path]; };
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/OMFI MediaFiles")}, true}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.reconciliationComplete);
		QCOMPARE(reads.size(), 3);
		for (const auto count : reads)
			QCOMPARE(count, 1);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QCOMPARE(header->outcome, MediaEngine::ParsedSource::Outcome::Complete);
		QCOMPARE(header->readReason,
			QStringLiteral("Header read: required table metadata missing or conflicting: Original Bin"));
		const auto &file = scan.files.front();
		QCOMPARE(file.stamp.fileMobId, baseline.stamp.fileMobId);
		QCOMPARE(file.stamp.masterMobId, baseline.stamp.masterMobId);
		const auto bin = file.evidence.selected(MediaProperty::OriginalBin);
		QCOMPARE(bin.value.toString(), QStringLiteral("Uncompressed media slides"));
		QVERIFY(bin.selectedObservation >= 0);
		const auto &claim = file.evidence.observations(MediaProperty::OriginalBin)[bin.selectedObservation];
		QCOMPARE(claim.snapshot->source, MetadataSource::Omf);
		QCOMPARE(claim.property, QStringLiteral("OMFI:MCBR:MC:binName"));
		QCOMPARE(claim.objectIdentity, QStringLiteral("object:68001"));
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan), temporary.path(), QStringLiteral("Volume"), false);
		QCOMPARE(row.originalBin, QStringLiteral("Uncompressed media slides"));
		QCOMPARE(row.masterMobId, baseline.stamp.masterMobId);
		QCOMPARE(row.kind, MediaFile::Kind::Video);
		QVERIFY(!row.resolution.isEmpty());
		QCOMPARE(row.metadataStatus.field(MediaProperty::Resolution).applicability, PropertyApplicability::Applicable);
		const auto &status = row.metadataStatus.field(MediaProperty::OriginalBin);
		QVERIFY(status.captured);
		QVERIFY(status.hasSelectedValue);
		QCOMPARE(status.readState, PropertyReadState::Present);
		QVERIFY(status.selectedSource);
		QCOMPARE(*status.selectedSource, MetadataSource::Omf);
	}
	void genuine_legacy_audio_compact_resolution_data()
	{
		QTest::addColumn<QString>("filename");
		QTest::newRow("avid-wave-with-omf-metadata") << QStringLiteral("TONE_100A01.6A972974.039700.wav");
		QTest::newRow("avid-aiff-with-omf-metadata") << QStringLiteral("TONE_100A01.6A972997.0C53E0.aif");
	}
	void genuine_legacy_audio_compact_resolution()
	{
		QFETCH(QString, filename);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/OMFI MediaFiles");
		const QString path = folder + QLatin1Char('/') + filename;
		QVERIFY(QDir().mkpath(folder));
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/") + filename, path));
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate) { ++reads[candidate.path]; };
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/OMFI MediaFiles")}, true}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.reconciliationComplete);
		QCOMPARE(reads.size(), 1);
		QCOMPARE(reads.value(path), 1);
		const auto &file = scan.files.front();
		const auto kind = file.evidence.selected(MediaProperty::Kind);
		QCOMPARE(kind.value.toInt(), int(MediaFile::Kind::Audio));
		QVERIFY(kind.agreement != PropertyAgreement::Conflicting);
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan), temporary.path(), QStringLiteral("Volume"), false);
		QCOMPARE(row.kind, MediaFile::Kind::Audio);
		QVERIFY(row.resolution.isEmpty());
		const auto &resolution = row.metadataStatus.field(MediaProperty::Resolution);
		QCOMPARE(resolution.applicability, PropertyApplicability::NotApplicable);
		QVERIFY(!resolution.hasSelectedValue && !resolution.selectedSource);
		QCOMPARE(resolution.readState, file.evidence.selected(MediaProperty::Resolution).readState);
		QCOMPARE(resolution.readReason, file.evidence.selected(MediaProperty::Resolution).readReason);
	}
	void incomplete_or_conflicting_table_metadata_reads_header_data()
	{
		QTest::addColumn<bool>("conflicting");
		QTest::newRow("missing bit depth") << false;
		QTest::newRow("conflicting bit depth") << true;
	}
	void incomplete_or_conflicting_table_metadata_reads_header()
	{
		QFETCH(bool, conflicting);
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR) + QStringLiteral("/TONE_100A01.EA7D504A.611740.mxf"), path));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase(conflicting ? 24 : 0)));
		if (conflicting)
			QVERIFY(tryWriteFile(folder + QStringLiteral("/other.mdb"), audioDatabase(16)));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QVERIFY(header->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		QVERIFY2(header->readReason.contains(QStringLiteral("Bit Depth")), qPrintable(header->readReason));
		const auto &evidence = scan.files.front().evidence;
		const auto depth = evidence.selected(MediaProperty::BitDepth);
		QVERIFY(depth.value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::BitDepth)[depth.selectedObservation].snapshot->source, MetadataSource::Mxf);
	}
	void ambiguous_database_master_reads_header_and_selects_its_association()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR) + QStringLiteral("/TONE_100A01.EA7D504A.611740.mxf"), path));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase()));
		QByteArray otherMaster = toneMasterId;
		otherMaster[31] = char(0x14);
		QVERIFY(tryWriteFile(folder + QStringLiteral("/other.mdb"), audioDatabase(24, {101, 301}, false, otherMaster)));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QVERIFY(header->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		QVERIFY2(header->readReason.contains(QStringLiteral("MasterMobId")), qPrintable(header->readReason));
		const auto &file = scan.files.front();
		const auto selected = file.evidence.selected(MediaProperty::MasterMobId);
		QCOMPARE(selected.value.metaType().id(), QMetaType::QString);
		QCOMPARE(selected.value.toString(), MediaEngine::canonicalDatabaseId(toneMasterId));
		QCOMPARE(selected.agreement, PropertyAgreement::Conflicting);
		QVERIFY(selected.selectedObservation >= 0);
		QCOMPARE(file.evidence.observations(MediaProperty::MasterMobId)[selected.selectedObservation].snapshot->source,
			MetadataSource::Mxf);
		QCOMPARE(file.stamp.masterMobId, selected.value.toString());
	}
	void master_only_database_details_do_not_promote_the_file_association()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(tryWriteFile(path, "Unreadable header"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		// The active master is available for lookup, but this MDB has no active
		// descriptor-owning file object that can establish a file association.
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase(24, {301})));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		const auto &file = scan.files.front();
		const auto master = file.evidence.selected(MediaProperty::MasterMobId);
		QCOMPARE(master.value.toString(), MediaEngine::canonicalDatabaseId(toneMasterId));
		QVERIFY(master.selectedObservation >= 0);
		QCOMPARE(file.evidence.observations(MediaProperty::MasterMobId)[master.selectedObservation].snapshot->source,
			MetadataSource::Pmr);
		QCOMPARE(file.stamp.masterMobId, master.value.toString());
		const auto name = file.evidence.selected(MediaProperty::ClipName);
		QCOMPARE(name.value.toString(), QStringLiteral("Database clip"));
		QVERIFY(name.selectedObservation >= 0);
		QCOMPARE(file.evidence.observations(MediaProperty::ClipName)[name.selectedObservation].snapshot->source,
			MetadataSource::Mdb);
	}
	void matching_master_id_does_not_borrow_another_physical_headers_name()
	{
		QFile fixture(QStringLiteral(FIXTURES_DIR) + QStringLiteral("/TONE_100A01.EA7D504A.611740.mxf"));
		QVERIFY(fixture.open(QIODevice::ReadOnly));
		QByteArray unnamedBytes = fixture.readAll();
		QVERIFY(fixture.seek(0));
		const auto receipt = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, fixture.fileName(), {}, SourceReadState::Complete});
		const MediaEngine::Cancellation cancellation;
		const auto parsed = MediaEngine::MxfReader{}.read(fixture, {receipt, cancellation});
		const auto projection = MediaEngine::projectMxf(parsed, cancellation);
		QCOMPARE(projection.files.size(), 1);
		const auto evidence = MediaEngine::selectedProjectedEvidence(projection.files.front(), projection);
		const auto name = evidence.selected(MediaProperty::ClipName);
		QVERIFY(name.selectedObservation >= 0);
		QVERIFY(!name.value.toString().isEmpty());
		const auto &nameClaim = evidence.observations(MediaProperty::ClipName)[name.selectedObservation];
		bool cleared = false;
		for (const auto &object : parsed.objects)
			if (QStringLiteral("object:%1").arg(object.handle) == nameClaim.objectIdentity)
				for (const auto &property : object.properties)
					if (property.locator.name == nameClaim.property)
						for (const auto &range : property.locator.ranges)
						{
							QVERIFY(range.offset >= 0 && range.length > 0);
							QVERIFY(range.offset <= unnamedBytes.size() && range.length <= unnamedBytes.size() - range.offset);
							unnamedBytes.replace(static_cast<qsizetype>(range.offset), static_cast<qsizetype>(range.length),
								QByteArray(static_cast<qsizetype>(range.length), '\0'));
							cleared = true;
						}
		QVERIFY(cleared); // Only this disposable copy's selected master-name bytes are changed.
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString unnamedPath = folder + QStringLiteral("/a-unnamed.mxf");
		const QString namedPath = folder + QStringLiteral("/b-named.mxf");
		QVERIFY(tryWriteFile(unnamedPath, unnamedBytes));
		QVERIFY(QFile::copy(fixture.fileName(), namedPath));
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 2);
		const auto unnamed = std::find_if(scan.files.cbegin(), scan.files.cend(), [&](const auto &file) { return file.path == unnamedPath; });
		const auto named = std::find_if(scan.files.cbegin(), scan.files.cend(), [&](const auto &file) { return file.path == namedPath; });
		QVERIFY(unnamed != scan.files.cend() && named != scan.files.cend());
		QVERIFY(!unnamed->stamp.masterMobId.isEmpty());
		QCOMPARE(unnamed->stamp.masterMobId, named->stamp.masterMobId);
		QCOMPARE(unnamed->stamp.fileMobId, named->stamp.fileMobId);
		QVERIFY(!unnamed->evidence.selected(MediaProperty::ClipName).value.isValid());
		QCOMPARE(named->evidence.selected(MediaProperty::ClipName).value.toString(), name.value.toString());
	}
	void a_database_match_does_not_hide_an_unlisted_sibling()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(tryWriteFile(folder + QStringLiteral("/listed.mxf"), "unopened"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/unlisted.mxf"), "requires a media read"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("listed.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase()));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 2);
		QVERIFY(scan.files[0].kelpieId != scan.files[1].kelpieId);
		QCOMPARE(mediaSource(scan, folder + QStringLiteral("/listed.mxf"))->outcome, MediaEngine::ParsedSource::Outcome::NotRead);
		const auto *unlisted = mediaSource(scan, folder + QStringLiteral("/unlisted.mxf"));
		QVERIFY(unlisted->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		QCOMPARE(unlisted->readReason, QStringLiteral("Header read: no usable database match"));
	}
	void unopened_media_is_rechecked_before_final_selection()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString first = folder + QStringLiteral("/a.mxf"), second = folder + QStringLiteral("/b.mxf");
		QVERIFY(tryWriteFile(first, "first copy"));
		QVERIFY(tryWriteFile(second, "second copy"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("a.mxf", toneFileId, "Project"), pmrRecord("b.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase()));
		const QDateTime modified = QFileInfo(first).lastModified();
		bool changed = false;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.progress = [&](int, int, const QString &source)
		{
			if (source != second)
				return;
			QFile file(first);
			changed = file.open(QIODevice::ReadWrite) && file.setFileTime(modified.addSecs(10), QFileDevice::FileModificationTime);
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(changed);
		QCOMPARE(scan.files.size(), 2);
		QVERIFY(!scan.files[0].evidence.selected(MediaProperty::Compression).value.isValid());
		QCOMPARE(scan.files[1].evidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("PCM"));
		QVERIFY(std::any_of(scan.reconciliationIssues.cbegin(), scan.reconciliationIssues.cend(), [&](const ScanIssue &issue)
							{ return issue.kind == ScanIssue::Kind::SourceChanged && issue.expectedPath == first; }));
	}
	void final_folder_evidence_observes_changes_since_header_decisions()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		for (const auto &name : {QStringLiteral("a.mxf"), QStringLiteral("b.mxf")})
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR) + QStringLiteral("/TONE_100A01.EA7D504A.611740.mxf"), folder + '/' + name));
		std::error_code error;
		const auto nativeFolder = QDir(folder).filesystemPath();
		const auto before = std::filesystem::last_write_time(nativeFolder, error);
		QVERIFY(!error);
		const auto original = QFileInfo(folder).lastModified();
		QDateTime expected;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.finalising = [&]
		{
			// A later matching pass must observe the folder again, rather than
			// retaining its earlier timestamp for the whole scan.
			std::filesystem::last_write_time(nativeFolder, before + std::chrono::seconds(10), error);
			expected = QFileInfo(folder).lastModified();
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(!error);
		QVERIFY(expected.isValid() && expected != original);
		QVERIFY(scan.reconciliationComplete);
		QCOMPARE(scan.files.size(), 2);
		for (const auto &file : scan.files)
		{
			const auto observations = file.evidence.observations(MediaProperty::DatabaseStatus);
			QVERIFY(!observations.isEmpty());
			for (const auto &observation : observations)
			{
				QVERIFY(observation.snapshot);
				QCOMPARE(observation.snapshot->modified, expected);
			}
		}
	}
	void a_database_changed_after_a_skip_triggers_header_fallback()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		const QString first = folder + QStringLiteral("/a.mxf"), second = folder + QStringLiteral("/b.mxf");
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR) + QStringLiteral("/TONE_100A01.EA7D504A.611740.mxf"), first));
		QVERIFY(tryWriteFile(second, "unopened"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("a.mxf", toneFileId, "Project"), pmrRecord("b.mxf", toneFileId, "Project")})));
		const QString database = folder + QStringLiteral("/metadata.mdb");
		QVERIFY(tryWriteFile(database, audioDatabase()));
		const QDateTime modified = QFileInfo(database).lastModified();
		bool changed = false;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.progress = [&](int, int, const QString &source)
		{
			if (source != second)
				return;
			QFile file(database);
			changed = file.open(QIODevice::ReadWrite) && file.setFileTime(modified.addSecs(10), QFileDevice::FileModificationTime);
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(changed);
		QVERIFY(mediaSource(scan, first)->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		const auto &evidence = scan.files.front().evidence;
		const auto codec = evidence.selected(MediaProperty::Compression);
		QVERIFY(codec.value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::Compression)[codec.selectedObservation].snapshot->source, MetadataSource::Mxf);
		for (const auto &observation : evidence.observations(MediaProperty::Compression))
			if (observation.snapshot && observation.snapshot->source == MetadataSource::Mdb)
				QVERIFY(!observation.eligible);
	}
	void approved_priorities_and_empty_text()
	{
		MediaEvidence evidence;
		const auto add = [&](MediaProperty field, MetadataSource source, const QString &value)
		{
			MetadataObservation item;
			item.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{source, {}, {}, SourceReadState::Complete});
			item.readState = PropertyReadState::Present;
			item.value = value;
			evidence.observe(field, item);
		};
		for (const auto field : {MediaProperty::ClipName, MediaProperty::Project, MediaProperty::OriginalBin})
		{
			add(field, MetadataSource::Mdb, QStringLiteral("database"));
			add(field, MetadataSource::Mxf, QStringLiteral("header"));
		}
		add(MediaProperty::Project, MetadataSource::Pmr, QStringLiteral("index"));
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::ClipName).value.toString(), QStringLiteral("header"));
		QCOMPARE(evidence.selected(MediaProperty::Project).value.toString(), QStringLiteral("index"));
		QCOMPARE(evidence.selected(MediaProperty::OriginalBin).value.toString(), QStringLiteral("database"));
		add(MediaProperty::ClipName, MetadataSource::Mxf, QString{});
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::ClipName).value.toString(), QStringLiteral("header"));
		add(MediaProperty::ClipName, MetadataSource::Mxf, QStringLiteral("contradiction"));
		MediaEngine::selectMetadata(evidence);
		QVERIFY(!evidence.selected(MediaProperty::ClipName).value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::ClipName).size(), 4);
	}
	void selected_master_details_exclude_other_master_origins_and_classification()
	{
		const auto header = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("authored.mxf"), {}, SourceReadState::Complete});
		const auto database = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mdb, QStringLiteral("authored.mdb"), {}, SourceReadState::Complete});
		MediaEngine::ProjectedFile file;
		observeFact(file, MediaProperty::MasterMobId, header, QStringLiteral("master-a"));
		observeFact(file, MediaProperty::MasterMobId, database, QStringLiteral("master-b"));
		observeFact(file, MediaProperty::SourcePath, header, QStringLiteral("physical-source.wav"));
		observeFact(file, MediaProperty::DropFrame, header, true);
		observeFact(file, MediaProperty::FileDuration, header,
			MediaEngine::durationValue({48001, {48000, 1}, {25, 1}, MediaDuration::Source::Descriptor}));
		MediaEngine::ProjectedFile chosen;
		chosen.masterMobIds = {QStringLiteral("master-a")};
		observeFact(chosen, MediaProperty::MasterMobId, header, QStringLiteral("master-a"));
		observeFact(chosen, MediaProperty::ClipName, header, QStringLiteral("Selected clip"));
		observeFact(chosen, MediaProperty::OriginalBin, header, QStringLiteral("Selected bin"));
		MediaEngine::ProjectedFile other;
		other.masterMobIds = {QStringLiteral("master-b")};
		observeFact(other, MediaProperty::MasterMobId, database, QStringLiteral("master-b"));
		observeFact(other, MediaProperty::ClipName, database, QStringLiteral("Sequence,Resize+1"));
		observeFact(other, MediaProperty::OriginalBin, database, QStringLiteral("Wrong bin"));
		observeFact(other, MediaProperty::Type, database, int(MediaType::Precompute));
		observeFact(other, MediaProperty::SourcePath, database, QStringLiteral("wrong-source.mov"));
		observeFact(other, MediaProperty::SourceContainer, database, QStringLiteral("Wrong container"));
		observeFact(other, MediaProperty::Imported, database, true);
		observeFact(other, MediaProperty::DropFrame, database, false);
		MediaEngine::Projection projection;
		projection.files.append(file);
		projection.masters = {chosen, other};
		const auto evidence = MediaEngine::selectedProjectedEvidence(file, projection);
		MediaFile row;
		applyResolvedMetadata(row, evidence);
		QCOMPARE(row.masterMobId, QStringLiteral("master-a"));
		QCOMPARE(row.clipName, QStringLiteral("Selected clip"));
		QCOMPARE(evidence.observations(MediaProperty::ClipName).size(), 1);
		QCOMPARE(row.originalBin, QStringLiteral("Selected bin")); // MDB preference cannot admit another master.
		QCOMPARE(row.sourceFilePath, QStringLiteral("physical-source.wav"));
		QVERIFY(!evidence.selected(MediaProperty::SourceContainer).value.isValid());
		QVERIFY(!evidence.selected(MediaProperty::Imported).value.isValid());
		QCOMPARE(row.type, MediaFile::Type::Unknown);
		QVERIFY(row.effect.isEmpty());
		QVERIFY(row.dropFrame);
		QCOMPARE(evidence.observations(MediaProperty::DropFrame).size(), 1);
		QCOMPARE(row.duration.units, qint64(48001));
		QCOMPARE(row.duration.rate.numerator, 48000);
		QCOMPARE(row.duration.displayRate.numerator, 25);
	}
	void same_master_details_keep_per_field_fallback_and_association_claims()
	{
		const auto header = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("authored.mxf"), {}, SourceReadState::Complete});
		const auto database = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mdb, QStringLiteral("authored.mdb"), {}, SourceReadState::Complete});
		MediaEngine::ProjectedFile file;
		observeFact(file, MediaProperty::MasterMobId, header, QStringLiteral("master-a"));
		observeFact(file, MediaProperty::MasterMobId, database, QStringLiteral("master-a"));
		MediaEngine::ProjectedFile fromHeader;
		fromHeader.masterMobIds = {QStringLiteral("master-a")};
		observeFact(fromHeader, MediaProperty::MasterMobId, header, QStringLiteral("master-a"));
		observeFact(fromHeader, MediaProperty::ClipName, header, QStringLiteral("Header clip"));
		observeFact(fromHeader, MediaProperty::Type, header, int(MediaType::Media));
		MediaEngine::ProjectedFile fromDatabase;
		fromDatabase.masterMobIds = {QStringLiteral("master-a")};
		observeFact(fromDatabase, MediaProperty::MasterMobId, database, QStringLiteral("master-a"));
		observeFact(fromDatabase, MediaProperty::ClipName, database, QStringLiteral("Database clip"));
		observeFact(fromDatabase, MediaProperty::OriginalBin, database, QStringLiteral("Database bin"));
		observeFact(fromDatabase, MediaProperty::SourcePath, database, QStringLiteral("recorded-source.wav"));
		observeFact(fromDatabase, MediaProperty::Type, database, int(MediaType::Precompute));
		MediaEngine::Projection projection;
		projection.files.append(file);
		projection.masters = {fromHeader, fromDatabase};
		const auto evidence = MediaEngine::selectedProjectedEvidence(file, projection);
		const auto master = evidence.selected(MediaProperty::MasterMobId);
		QCOMPARE(master.value.toString(), QStringLiteral("master-a"));
		QCOMPARE(evidence.observations(MediaProperty::MasterMobId).size(), 2);
		QCOMPARE(evidence.observations(MediaProperty::MasterMobId)[master.selectedObservation].snapshot->source,
			MetadataSource::Mxf);
		MediaFile row;
		applyResolvedMetadata(row, evidence);
		QCOMPARE(row.clipName, QStringLiteral("Header clip"));
		QCOMPARE(row.originalBin, QStringLiteral("Database bin"));
		QCOMPARE(row.sourceFilePath, QStringLiteral("recorded-source.wav"));
		QCOMPARE(row.type, MediaFile::Type::Media);
		QCOMPARE(evidence.observations(MediaProperty::Type).size(), 2);
	}
	void ambiguous_master_keeps_physical_facts_and_all_header_checks()
	{
		const auto header = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("authored.mxf"), {}, SourceReadState::Complete});
		MediaEngine::ProjectedFile file;
		observeFact(file, MediaProperty::FileMobId, header, QStringLiteral("physical-file"));
		observeFact(file, MediaProperty::MasterMobId, header, QStringLiteral("master-a"));
		observeFact(file, MediaProperty::MasterMobId, header, QStringLiteral("master-b"));
		observeFact(file, MediaProperty::Project, header, QStringLiteral("File project"));
		observeFact(file, MediaProperty::DropFrame, header, true);
		observeFact(file, MediaProperty::FileDuration, header,
			MediaEngine::durationValue({48001, {48000, 1}, {30000, 1001}, MediaDuration::Source::Descriptor}));
		MediaEngine::Projection projection;
		projection.files.append(file);
		for (const auto &identity : {QStringLiteral("master-a"), QStringLiteral("master-b")})
		{
			MediaEngine::ProjectedFile master;
			master.masterMobIds = {identity};
			observeFact(master, MediaProperty::MasterMobId, header, identity);
			observeFact(master, MediaProperty::ClipName, header, identity + QStringLiteral(" clip"));
			observeFact(master, MediaProperty::Type, header, int(MediaType::Media));
			projection.masters.append(master);
		}
		const auto evidence = MediaEngine::selectedProjectedEvidence(file, projection);
		QVERIFY(!evidence.selected(MediaProperty::MasterMobId).value.isValid());
		QCOMPARE(evidence.selected(MediaProperty::MasterMobId).agreement, PropertyAgreement::Conflicting);
		QVERIFY(!evidence.selected(MediaProperty::ClipName).value.isValid());
		QVERIFY(!evidence.selected(MediaProperty::Type).value.isValid());
		QCOMPARE(evidence.selected(MediaProperty::Project).value.toString(), QStringLiteral("File project"));
		QVERIFY(evidence.selected(MediaProperty::DropFrame).value.toBool());
		const auto duration = MediaEngine::mediaDuration(evidence.selected(MediaProperty::FileDuration).value);
		QCOMPARE(duration.units, qint64(48001));
		QCOMPARE(duration.rate.numerator, 48000);
		QCOMPARE(duration.displayRate.denominator, 1001);
		MediaScanStamp stamp;
		stamp.path = header->path;
		stamp.fileMobId = QStringLiteral("physical-file");
		const auto checks = makeOpScanReceipt(evidence, stamp);
		QCOMPARE(checks.headerIdentity.fileMobId, stamp.fileMobId);
		QCOMPARE(checks.headerIdentity.masterMobIds,
			(QStringList{QStringLiteral("master-a"), QStringLiteral("master-b")}));
		QVERIFY(checks.headerIdentity.unavailableReason.isEmpty());
	}
	void compact_status_preserves_unavailable_and_empty_results()
	{
		const MediaMetadataStatus uncaptured;
		QVERIFY(!uncaptured.field(MediaProperty::ClipName).captured);
		const auto unknown = makeMediaMetadataStatus({});
		for (int index = 0; index < static_cast<int>(MediaProperty::Count); ++index)
		{
			const auto &status = unknown.field(static_cast<MediaProperty>(index));
			QVERIFY(status.captured);
			QCOMPARE(status.readState, PropertyReadState::NotRead);
			QCOMPARE(status.readReason, PropertyReadReason::NoAssociatedSource);
			QVERIFY(!status.hasSelectedValue);
			QVERIFY(!status.selectedSource && !status.selectedBasis && !status.selectedFreshness);
		}
		QCOMPARE(unknown.headerOutcome, MediaHeaderReadOutcome::NoReceipt);

		MediaEvidence evidence;
		const auto header = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("authored.mxf"), {}, SourceReadState::Complete});
		const QString owner = QStringLiteral("audio-descriptor");
		evidence.recordReadStatus(MediaProperty::Resolution, header, owner,
			{PropertyReadState::Absent, PropertyReadReason::NotPresentInObject, PropertyApplicability::NotApplicable, {}});
		evidence.recordReadStatus(MediaProperty::Project, header, owner,
			{PropertyReadState::Absent, PropertyReadReason::NotPresentInObject, PropertyApplicability::Applicable, {}});
		evidence.recordReadStatus(MediaProperty::SourcePath, header, owner,
			{PropertyReadState::Unreadable, PropertyReadReason::ValueUnreadable, PropertyApplicability::Applicable, {}});
		const auto observe = [&](MediaProperty property, const QVariant &value)
		{
			MetadataObservation observation;
			observation.snapshot = header;
			observation.objectIdentity = owner;
			observation.readState = PropertyReadState::Present;
			observation.value = value;
			evidence.observe(property, std::move(observation));
		};
		observe(MediaProperty::ClipName, QStringLiteral(""));
		observe(MediaProperty::Kind, int(MediaFile::Kind::Audio));
		observe(MediaProperty::Imported, false);
		observe(MediaProperty::Channels, 0);
		MediaEngine::selectMetadata(evidence);
		const auto captured = makeMediaMetadataStatus(evidence, MediaHeaderReadOutcome::Incomplete);
		QCOMPARE(captured.headerOutcome, MediaHeaderReadOutcome::Incomplete);
		const auto &resolution = captured.field(MediaProperty::Resolution);
		QCOMPARE(resolution.readState, PropertyReadState::Absent);
		QCOMPARE(resolution.applicability, PropertyApplicability::NotApplicable);
		QVERIFY(!resolution.hasSelectedValue);
		const auto &project = captured.field(MediaProperty::Project);
		QCOMPARE(project.readState, PropertyReadState::Absent);
		QCOMPARE(project.readReason, PropertyReadReason::NotPresentInObject);
		QCOMPARE(project.applicability, PropertyApplicability::Applicable);
		const auto &path = captured.field(MediaProperty::SourcePath);
		QCOMPARE(path.readState, PropertyReadState::Unreadable);
		QCOMPARE(path.readReason, PropertyReadReason::ValueUnreadable);
		QVERIFY(!path.hasSelectedValue);
		const auto &empty = captured.field(MediaProperty::ClipName);
		QCOMPARE(empty.readState, PropertyReadState::Present);
		QVERIFY(!empty.hasSelectedValue && !empty.selectedSource);
		QVERIFY(empty.presentSources.contains(MetadataSource::Mxf));
		for (const auto property : {MediaProperty::Imported, MediaProperty::Channels})
		{
			const auto &status = captured.field(property);
			QCOMPARE(status.readState, PropertyReadState::Present);
			QVERIFY(status.hasSelectedValue && status.selectedSource);
			QCOMPARE(*status.selectedSource, MetadataSource::Mxf);
		}

		MediaEvidence unopened;
		unopened.registerSource(QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Omf, QStringLiteral("unopened.omf"), {}, SourceReadState::NotRead}));
		const auto deferred = makeMediaMetadataStatus(unopened, MediaHeaderReadOutcome::NotRead);
		QCOMPARE(deferred.field(MediaProperty::Compression).readState, PropertyReadState::NotRead);
		QCOMPARE(deferred.field(MediaProperty::Compression).readReason, PropertyReadReason::SourceNotRead);
		QVERIFY(!deferred.field(MediaProperty::Compression).selectedSource);
	}

	void compact_status_preserves_conflicts_and_excluded_changes()
	{
		MediaEvidence evidence;
		const auto header = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("header.mxf"), {}, SourceReadState::Complete});
		const auto database = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mdb, QStringLiteral("database.mdb"), {}, SourceReadState::Complete});
		const auto changed = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Pmr, QStringLiteral("changed.pmr"), {}, SourceReadState::Complete});
		const auto observe = [&](MediaProperty property, const SourceSnapshotRef &source, const QVariant &value)
		{
			MetadataObservation observation;
			observation.snapshot = source;
			observation.readState = PropertyReadState::Present;
			observation.value = value;
			evidence.observe(property, std::move(observation));
		};
		observe(MediaProperty::ClipName, header, QStringLiteral("Header clip"));
		observe(MediaProperty::ClipName, database, QStringLiteral("Database clip"));
		observe(MediaProperty::ClipName, changed, QStringLiteral("Excluded clip"));
		observe(MediaProperty::FrameRate, header, MediaEngine::rateValue({25, 1}));
		observe(MediaProperty::FrameRate, header, MediaEngine::rateValue({24, 1}));
		evidence.qualifySource(header, true, SourceFreshness::TimestampConsistent);
		evidence.qualifySource(changed, false, SourceFreshness::Changed);
		MediaEngine::selectMetadata(evidence);
		const auto captured = makeMediaMetadataStatus(evidence);
		const auto &name = captured.field(MediaProperty::ClipName);
		QCOMPARE(name.agreement, PropertyAgreement::Conflicting);
		QVERIFY(name.hasSelectedValue && name.selectedSource && name.selectedBasis && name.selectedFreshness);
		QCOMPARE(*name.selectedSource, MetadataSource::Mxf);
		QCOMPARE(*name.selectedBasis, EvidenceBasis::Recorded);
		QCOMPARE(*name.selectedFreshness, SourceFreshness::TimestampConsistent);
		QVERIFY(name.presentSources.contains(MetadataSource::Mxf));
		QVERIFY(name.presentSources.contains(MetadataSource::Mdb));
		QVERIFY(!name.presentSources.contains(MetadataSource::Pmr));
		QVERIFY(name.timestampConsistentSources.contains(MetadataSource::Mxf));
		QVERIFY(name.unknownFreshnessSources.contains(MetadataSource::Mdb));
		QVERIFY(name.changedSources.contains(MetadataSource::Pmr));
		const auto &rate = captured.field(MediaProperty::FrameRate);
		QCOMPARE(rate.readState, PropertyReadState::Present);
		QCOMPARE(rate.agreement, PropertyAgreement::Conflicting);
		QVERIFY(!rate.hasSelectedValue);
		QVERIFY(!rate.selectedSource && !rate.selectedBasis && !rate.selectedFreshness);
		QVERIFY(rate.presentSources.contains(MetadataSource::Mxf));
	}

	void compact_resolution_applicability_data()
	{
		QTest::addColumn<QString>("condition");
		QTest::addColumn<int>("sourceKind");
		QTest::addColumn<int>("expectedApplicability");
		const auto add = [&](const char *name, PropertyApplicability applicability,
			MetadataSource source = MetadataSource::Mxf)
		{
			QTest::newRow(name) << QString::fromLatin1(name) << int(source) << int(applicability);
		};
		add("mdb-audio", PropertyApplicability::NotApplicable, MetadataSource::Mdb);
		add("mxf-audio", PropertyApplicability::NotApplicable);
		add("omf-audio", PropertyApplicability::NotApplicable, MetadataSource::Omf);
		add("audio-with-unrelated-unknown-context", PropertyApplicability::NotApplicable);
		add("audio-with-excluded-video", PropertyApplicability::NotApplicable);
		add("audio-with-changed-video", PropertyApplicability::NotApplicable);
		add("audio-with-excluded-picture", PropertyApplicability::NotApplicable);
		add("audio-with-changed-picture", PropertyApplicability::NotApplicable);
		add("kind-missing", PropertyApplicability::Unknown);
		add("kind-missing-with-existing-not-applicable", PropertyApplicability::Unknown);
		add("kind-unknown", PropertyApplicability::Unknown);
		add("kind-unreadable", PropertyApplicability::Unknown);
		add("kind-excluded", PropertyApplicability::Unknown);
		add("kind-changed", PropertyApplicability::Unknown);
		add("video-missing-resolution", PropertyApplicability::Unknown);
		add("preferred-audio-disagrees-with-video", PropertyApplicability::Unknown);
		add("disagreement-with-existing-not-applicable", PropertyApplicability::Unknown);
		add("audio-with-picture", PropertyApplicability::Applicable);
		add("audio-with-empty-picture", PropertyApplicability::Applicable);
		add("audio-with-conflicting-picture", PropertyApplicability::Applicable);
		add("audio-with-unreadable-picture", PropertyApplicability::Applicable);
	}

	void compact_resolution_applicability()
	{
		QFETCH(QString, condition);
		QFETCH(int, sourceKind);
		QFETCH(int, expectedApplicability);
		MediaEvidence evidence;
		const auto source = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource(sourceKind), QStringLiteral("authored-media"), {}, SourceReadState::Complete});
		const auto other = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mdb, QStringLiteral("other.mdb"), {}, SourceReadState::Complete});
		const QString owner = QStringLiteral("physical-descriptor");
		const auto observe = [&](MediaProperty property, const SourceSnapshotRef &snapshot,
			const QVariant &value, PropertyReadState state = PropertyReadState::Present)
		{
			MetadataObservation observation;
			observation.snapshot = snapshot;
			observation.objectIdentity = owner;
			observation.property = mediaPropertyName(property);
			observation.readState = state;
			observation.value = value;
			evidence.observe(property, std::move(observation));
		};
		if (!condition.startsWith(QLatin1String("kind-missing")))
			observe(MediaProperty::Kind, source,
				int(condition == QLatin1String("kind-unknown") ? MediaFile::Kind::Unknown
					: condition == QLatin1String("video-missing-resolution") ? MediaFile::Kind::Video : MediaFile::Kind::Audio),
				condition == QLatin1String("kind-unreadable") ? PropertyReadState::Unreadable : PropertyReadState::Present);
		if (condition == QLatin1String("kind-excluded") || condition == QLatin1String("kind-changed"))
			evidence.qualifySource(source, false, condition == QLatin1String("kind-changed")
				? SourceFreshness::Changed : SourceFreshness::Unknown);
		if (condition == QLatin1String("audio-with-unrelated-unknown-context"))
			evidence.registerSource(other, QStringLiteral("unrelated-master"));
		if (condition.contains(QLatin1String("video")) && condition != QLatin1String("video-missing-resolution"))
			observe(MediaProperty::Kind, other, int(MediaFile::Kind::Video));
		if (condition == QLatin1String("disagreement-with-existing-not-applicable"))
		{
			observe(MediaProperty::Kind, other, int(MediaFile::Kind::Video));
			for (const auto &snapshot : {source, other})
				evidence.recordReadStatus(MediaProperty::Resolution, snapshot, owner,
					{PropertyReadState::Absent, PropertyReadReason::NotPresentInObject,
						PropertyApplicability::NotApplicable, {}});
		}
		if (condition == QLatin1String("kind-missing-with-existing-not-applicable"))
			evidence.recordReadStatus(MediaProperty::Resolution, source, owner,
				{PropertyReadState::Absent, PropertyReadReason::NotPresentInObject,
					PropertyApplicability::NotApplicable, {}});
		if (condition == QLatin1String("audio-with-excluded-picture") || condition == QLatin1String("audio-with-changed-picture"))
			observe(MediaProperty::Resolution, other, QStringLiteral("1920x1080"));
		if (condition.contains(QLatin1String("-excluded-")) || condition.contains(QLatin1String("-changed-")))
			evidence.qualifySource(other, false, condition.contains(QLatin1String("-changed-"))
				? SourceFreshness::Changed : SourceFreshness::Unknown);
		if (condition == QLatin1String("audio-with-picture") || condition == QLatin1String("audio-with-conflicting-picture"))
			observe(MediaProperty::Resolution, source, QStringLiteral("1920x1080"));
		if (condition == QLatin1String("audio-with-empty-picture"))
			observe(MediaProperty::Resolution, source, QStringLiteral(""));
		if (condition == QLatin1String("audio-with-conflicting-picture"))
			observe(MediaProperty::Resolution, source, QStringLiteral("1280x720"));
		if (condition == QLatin1String("audio-with-unreadable-picture"))
			evidence.recordReadStatus(MediaProperty::Resolution, source, owner,
				{PropertyReadState::Unreadable, PropertyReadReason::ValueUnreadable, PropertyApplicability::Applicable, {}});
		MediaEngine::selectMetadata(evidence);
		QVector<ResolvedField> before;
		for (int index = 0; index < int(MediaProperty::Count); ++index)
			before.append(evidence.selected(MediaProperty(index)));
		const auto captured = makeMediaMetadataStatus(evidence);
		const auto &resolution = captured.field(MediaProperty::Resolution);
		QCOMPARE(resolution.applicability, PropertyApplicability(expectedApplicability));
		QCOMPARE(resolution.hasSelectedValue, before[int(MediaProperty::Resolution)].value.isValid());
		QCOMPARE(resolution.readState, before[int(MediaProperty::Resolution)].readState);
		QCOMPARE(resolution.readReason, before[int(MediaProperty::Resolution)].readReason);
		if (resolution.applicability == PropertyApplicability::NotApplicable)
		{
			QVERIFY(!resolution.hasSelectedValue && !resolution.selectedSource);
		}
		if (condition == QLatin1String("preferred-audio-disagrees-with-video") ||
			condition == QLatin1String("disagreement-with-existing-not-applicable"))
		{
			QCOMPARE(evidence.selected(MediaProperty::Kind).value.toInt(), int(MediaFile::Kind::Audio));
			QCOMPARE(captured.field(MediaProperty::Kind).agreement, PropertyAgreement::Conflicting);
		}
		if (condition == QLatin1String("audio-with-empty-picture") ||
			condition == QLatin1String("audio-with-conflicting-picture"))
		{
			QCOMPARE(resolution.readState, PropertyReadState::Present);
			QVERIFY(!resolution.hasSelectedValue);
			QVERIFY(resolution.presentSources.contains(MetadataSource(sourceKind)));
		}
		if (condition == QLatin1String("audio-with-unreadable-picture"))
		{
			QCOMPARE(resolution.readState, PropertyReadState::Unreadable);
			QCOMPARE(resolution.readReason, PropertyReadReason::ValueUnreadable);
		}
		// Preparing the saved checklist must not change selected values or the
		// temporary reader/selection results that control additional reads.
		for (int index = 0; index < int(MediaProperty::Count); ++index)
		{
			const auto after = evidence.selected(MediaProperty(index));
			QCOMPARE(after.value, before[index].value);
			QCOMPARE(after.readState, before[index].readState);
			QCOMPARE(after.readReason, before[index].readReason);
			QCOMPARE(after.applicability, before[index].applicability);
			QCOMPARE(after.agreement, before[index].agreement);
			QCOMPARE(after.selectedObservation, before[index].selectedObservation);
		}
	}

	void compact_status_keeps_selected_master_and_principal_clock_origins()
	{
		MediaEvidence evidence;
		const auto header = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("header.mxf"), {}, SourceReadState::Complete});
		const auto database = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mdb, QStringLiteral("database.mdb"), {}, SourceReadState::Complete});
		const auto observe = [&](MediaProperty property, const SourceSnapshotRef &source, const QVariant &value)
		{
			MetadataObservation observation;
			observation.snapshot = source;
			observation.readState = PropertyReadState::Present;
			observation.value = value;
			evidence.observe(property, std::move(observation));
		};
		observe(MediaProperty::MasterMobId, header, QStringLiteral("header-master"));
		observe(MediaProperty::MasterMobId, database, QStringLiteral("database-master"));
		observe(MediaProperty::FileDuration, header,
			MediaEngine::durationValue({2400, {25, 1}, {}, MediaDuration::Source::Descriptor}));
		observe(MediaProperty::FileDuration, database,
			MediaEngine::durationValue({2400, {25, 1}, {25, 1}, MediaDuration::Source::Descriptor}));
		observe(MediaProperty::ClipName, header, QStringLiteral("Sequence,3D_Warp+1"));
		observe(MediaProperty::Type, header, int(MediaFile::Type::Precompute));
		MediaEngine::selectMetadata(evidence);
		const auto captured = makeMediaMetadataStatus(evidence);
		const auto &master = captured.field(MediaProperty::MasterMobId);
		QVERIFY(master.hasSelectedValue);
		QVERIFY(master.selectedSource && master.selectedBasis && master.selectedFreshness);
		QCOMPARE(*master.selectedSource, MetadataSource::Mxf);
		QCOMPARE(*master.selectedBasis, EvidenceBasis::Recorded);
		QCOMPARE(*master.selectedFreshness, SourceFreshness::Unknown);
		QCOMPARE(master.agreement, PropertyAgreement::Conflicting);
		QCOMPARE(evidence.selected(MediaProperty::MasterMobId).value.toString(), QStringLiteral("header-master"));
		QVERIFY(master.presentSources.contains(MetadataSource::Mxf));
		QVERIFY(master.presentSources.contains(MetadataSource::Mdb));
		const auto &duration = captured.field(MediaProperty::FileDuration);
		QVERIFY(duration.hasSelectedValue && duration.selectedSource);
		QCOMPARE(*duration.selectedSource, MetadataSource::Mxf); // Principal length, not the supplemental clock.
		QVERIFY(duration.presentSources.contains(MetadataSource::Mdb));
		QVERIFY(MediaEngine::mediaDuration(evidence.selected(MediaProperty::FileDuration).value).displayRate.valid());
		const auto &effect = captured.field(MediaProperty::Effect);
		QVERIFY(effect.hasSelectedValue && effect.selectedSource && effect.selectedBasis);
		QCOMPARE(*effect.selectedSource, MetadataSource::Mxf);
		QCOMPARE(*effect.selectedBasis, EvidenceBasis::Derived);
	}

	void compact_status_checks_property_and_source_bounds()
	{
		MediaMetadataStatus status;
		QVERIFY_THROWS_EXCEPTION(std::out_of_range, status.field(MediaProperty::Count));
		QVERIFY_THROWS_EXCEPTION(std::out_of_range, status.field(static_cast<MediaProperty>(-1)));
		MetadataSourceSet sources;
		QCOMPARE(sources.bits(), quint8{0});
		sources.add(MetadataSource::Mxf);
		QVERIFY(sources.contains(MetadataSource::Mxf));
		QVERIFY(!sources.contains(MetadataSource::Omf));
		QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sources.add(static_cast<MetadataSource>(255)));
		QVERIFY_THROWS_EXCEPTION(std::invalid_argument, sources.contains(static_cast<MetadataSource>(255)));
	}

	void adapter_operation_checks_survive_releasing_source_history_data()
	{
		QTest::addColumn<QString>("condition");
		QTest::newRow("genuine-header") << QStringLiteral("header");
		QTest::newRow("authored-skipped-header") << QStringLiteral("skipped");
		QTest::newRow("authored-unreadable-header") << QStringLiteral("unreadable");
	}

	void adapter_operation_checks_survive_releasing_source_history()
	{
		QFETCH(QString, condition);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		const bool genuineHeader = condition == QStringLiteral("header");
		const bool headerSkipped = condition == QStringLiteral("skipped");
		if (genuineHeader)
		{
			QVERIFY(QDir().mkpath(folder));
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR) + QStringLiteral("/TONE_100A01.EA7D504A.611740.mxf"), path));
		}
		else
		{
			// Authored database/header controls exercise read decisions, not format ground truth.
			QVERIFY(tryWriteFile(path, "Unreadable test header"));
			QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
			QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase(headerSkipped ? 24 : 0)));
		}
		const MediaEngine::Cancellation cancellation;
		auto scan = QSharedPointer<MediaEngine::ScanResult>::create(
			MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation));
		QCOMPARE(scan->files.size(), 1);
		QVERIFY(scan->reconciliationComplete);
		const auto *header = mediaSource(*scan, path);
		QVERIFY(header && header->snapshot);
		QCOMPARE(header->outcome == MediaEngine::ParsedSource::Outcome::NotRead, headerSkipped);
		const QWeakPointer<MediaEngine::ScanResult> scanOwner(scan);
		const QWeakPointer<const SourceSnapshot> headerOwner(header->snapshot);
		const ScanRowLookup lookup(*scan);
		const auto converted = mediaEngineMediaFile(scan->files.front(), lookup, temporary.path(), QStringLiteral("Test volume"), scan->cancelled);
		QVERIFY(converted.operationScanReceipt);
		const QString fileId = MediaEngine::canonicalDatabaseId(toneFileId);
		const QString master = MediaEngine::canonicalDatabaseId(toneMasterId);
		const auto &checks = *converted.operationScanReceipt;
		QCOMPARE(checks.headerIdentity.fileMobId, genuineHeader ? fileId : QString{});
		QCOMPARE(checks.headerIdentity.masterMobIds, genuineHeader ? QStringList{master} : QStringList{});
		QVERIFY(checks.headerIdentity.unavailableReason.isEmpty());
		QCOMPARE(checks.databaseFileMobIdToVerify, headerSkipped ? fileId : QString{});
		QCOMPARE(converted.metadataStatus.headerOutcome, lookup.headerOutcome(path));
		if (genuineHeader || headerSkipped)
			QCOMPARE(converted.metadataStatus.headerOutcome, headerSkipped ? MediaHeaderReadOutcome::NotRead : MediaHeaderReadOutcome::Complete);
		const auto &identityStatus = converted.metadataStatus.field(MediaProperty::FileMobId);
		QVERIFY(identityStatus.captured && identityStatus.hasSelectedValue);
		QVERIFY(identityStatus.selectedSource);
		QCOMPARE(*identityStatus.selectedSource, genuineHeader ? MetadataSource::Mxf : MetadataSource::Mdb);
		const QString scanPath = converted.scanStamp.path;
		const QString volume = converted.scanStamp.volumeIdentifier;
		const qint64 modifiedMs = converted.scanStamp.modified.toMSecsSinceEpoch();
		scan.clear();
		// Published rows and the temporary lookup never retain the scan or sources.
		QVERIFY(scanOwner.isNull());
		QVERIFY(headerOwner.isNull());
		QCOMPARE(lookup.headerWasSkipped(path), headerSkipped);
		const auto item = opItemFromMediaFile(converted);
		QVERIFY(item.headerIdentity);
		QCOMPARE(item.headerIdentity->fileMobId, genuineHeader ? fileId : QString{});
		QCOMPARE(item.headerIdentity->masterMobIds, genuineHeader ? QStringList{master} : QStringList{});
		QVERIFY(item.headerIdentity->unavailableReason.isEmpty());
		QCOMPARE(item.databaseFileMobIdToVerify, headerSkipped ? fileId : QString{});
		QCOMPARE(item.fileMobId, fileId);
		QCOMPARE(item.scanPath, scanPath);
		QCOMPARE(item.scanVolumeIdentifier, volume);
		QCOMPARE(item.modifiedMs, modifiedMs);
	}

	void copies_keep_physical_rows_after_source_release()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString name = QStringLiteral("TONE_100A01.EA7D504A.611740.mxf");
		for (const auto &folder : {QStringLiteral("1"), QStringLiteral("2")})
		{
			const QString path = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/") + folder;
			QVERIFY(QDir().mkpath(path));
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR) + '/' + name, path + '/' + name));
		}
		const MediaEngine::Cancellation cancellation;
		auto scan = QSharedPointer<MediaEngine::ScanResult>::create(MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation));
		QCOMPARE(scan->files.size(), 2);
		QVERIFY(scan->reconciliationComplete);
		QVERIFY(scan->files[0].kelpieId != scan->files[1].kelpieId);
		QVERIFY(scan->files[0].kelpieId != 0);
		QCOMPARE(scan->sources.size(), 2);
		const ScanRowLookup lookup(*scan);
		const auto first = mediaEngineMediaFile(scan->files[0], lookup);
		const auto second = mediaEngineMediaFile(scan->files[1], lookup);
		QVERIFY(!first.fileMobId.isEmpty());
		QCOMPARE(first.fileMobId, second.fileMobId);
		QVERIFY(first.mediaFilePath != second.mediaFilePath);
		QCOMPARE(first.scanStamp.fileMobId, first.fileMobId);
		// Reader evidence still shares source receipts while the engine result lives.
		const auto &stored = scan->sources.front();
		QCOMPARE(stored.outcome, MediaEngine::ParsedSource::Outcome::Complete);
		QCOMPARE(stored.snapshot->readState, SourceReadState::Complete);
		QVERIFY(std::any_of(scan->files[0].evidence.observations(MediaProperty::FileMobId).cbegin(),
			scan->files[0].evidence.observations(MediaProperty::FileMobId).cend(),
			[&](const MetadataObservation &observation) { return observation.snapshot == stored.snapshot; }));
		QVERIFY(!scan->files[0].evidence.observations(MediaProperty::FileMobId).isEmpty());
		QCOMPARE(first.kind, MediaFile::Kind::Audio);
		QCOMPARE(first.dbStatus, MediaFile::DbStatus::NoDatabase);
		const QWeakPointer<MediaEngine::ScanResult> scanOwner(scan);
		const QWeakPointer<const SourceSnapshot> sourceOwner(stored.snapshot);
		scan.clear();
		QVERIFY(scanOwner.isNull());
		QVERIFY(sourceOwner.isNull());
		QCOMPARE(first.metadataStatus.headerOutcome, MediaHeaderReadOutcome::Complete);
		QCOMPARE(second.metadataStatus.headerOutcome, MediaHeaderReadOutcome::Complete);
		QVERIFY(first.metadataStatus.field(MediaProperty::FileMobId).hasSelectedValue);
		QVERIFY(second.metadataStatus.field(MediaProperty::FileMobId).hasSelectedValue);
	}
	void pmr_exact_name_wins_before_normalized_candidates()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QByteArray otherId = toneFileId;
		otherId[31] ^= 1;
		QVERIFY(tryWriteFile(folder + QStringLiteral("/take.mxf"), "not an MXF header"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("TAKE.MXF", otherId, "wrong case"), pmrRecord("take.mxf", toneFileId, "exact name")})));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		const auto &evidence = scan.files.front().evidence;
		QCOMPARE(evidence.selected(MediaProperty::Project).value.toString(), QStringLiteral("exact name"));
		QCOMPARE(evidence.observations(MediaProperty::Project).size(), 2);
		int eligible = 0;
		for (const auto &value : evidence.observations(MediaProperty::Project))
			eligible += value.eligible;
		QCOMPARE(eligible, 1);
		MediaEvidence reselected = evidence;
		MediaEngine::selectMetadata(reselected);
		QCOMPARE(reselected.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::Listed));
		QVERIFY(!reselected.observations(MediaProperty::DatabaseStatus).isEmpty());
	}
	void pmr_exact_name_controls_completed_reference_miss()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(QDir().mkpath(folder));
		QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		QByteArray otherId = toneFileId;
		otherId[31] ^= 1;
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
			pmr({pmrRecord("take.mxf", otherId, "Exact name belongs to another file"),
				pmrRecord("TAKE.MXF", toneFileId, "Ignored normalized spelling")})));
		int headerReads = 0;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			if (candidate.path == path)
				++headerReads;
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.reconciliationComplete);
		QCOMPARE(headerReads, 1);
		const auto &file = scan.files.front();
		QCOMPARE(file.stamp.fileMobId, MediaEngine::canonicalDatabaseId(toneFileId));
		QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::NoReference));
		QCOMPARE(mediaEngineMediaFile(file, ScanRowLookup(scan)).dbStatus, MediaFile::DbStatus::NoReference);
		for (const auto &observation : file.evidence.observations(MediaProperty::Project))
			if (observation.snapshot && observation.snapshot->source == MetadataSource::Pmr)
				QVERIFY(!observation.eligible);
	}

	void pmr_unique_normalized_name_is_a_fallback()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(tryWriteFile(folder + QStringLiteral("/take.mxf"), "not an MXF header"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("TAKE.MXF", toneFileId, "normalized match")})));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		QCOMPARE(scan.files.front().evidence.selected(MediaProperty::Project).value.toString(), QStringLiteral("normalized match"));
	}
	void ambiguous_pmr_identities_cannot_supply_even_agreeing_metadata()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QByteArray otherId = toneFileId;
		otherId[31] ^= 1;
		QVERIFY(tryWriteFile(folder + QStringLiteral("/take.mxf"), "not an MXF header"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("take.mxf", toneFileId, "same project"), pmrRecord("take.mxf", otherId, "same project")})));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		const auto &evidence = scan.files.front().evidence;
		QVERIFY(!evidence.selected(MediaProperty::FileMobId).value.isValid());
		QVERIFY(!evidence.selected(MediaProperty::Project).value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::Project).size(), 2);
		for (const auto &value : evidence.observations(MediaProperty::Project))
			QVERIFY(!value.eligible);
		QVERIFY(std::any_of(scan.reconciliationIssues.cbegin(), scan.reconciliationIssues.cend(), [](const ScanIssue &issue)
							{ return issue.kind == ScanIssue::Kind::MetadataConflict && issue.explanation.contains(QStringLiteral("multiple compatible file identities")); }));
		QCOMPARE(evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::DbUnreadable));
		QCOMPARE(mediaEngineMediaFile(scan.files.front(), ScanRowLookup(scan)).dbStatus, MediaFile::DbStatus::DbUnreadable);
	}
	void normalized_name_does_not_assign_one_record_to_two_files()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(tryWriteFile(folder + QStringLiteral("/take.mxf"), "first invalid MXF"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/TAKE.mxf"), "second invalid MXF"));
		if (QDir(folder).entryList({QStringLiteral("*.mxf")}, QDir::Files).size() != 2)
			QSKIP("Temporary filesystem does not preserve case-distinct file locations");
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("Take.mxf", toneFileId, "ambiguous location")})));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation);
		QCOMPARE(scan.files.size(), 2);
		for (const auto &file : scan.files)
		{
			QVERIFY(!file.evidence.selected(MediaProperty::FileMobId).value.isValid());
			QVERIFY(!file.evidence.selected(MediaProperty::Project).value.isValid());
			QVERIFY(!file.evidence.observations(MediaProperty::Project).isEmpty());
			QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::DbUnreadable));
			QCOMPARE(mediaEngineMediaFile(file, ScanRowLookup(scan)).dbStatus, MediaFile::DbStatus::DbUnreadable);
		}
	}
	void changed_physical_file_excludes_database_fallbacks()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		const QString name = QStringLiteral("TONE_100A01.EA7D504A.611740.mxf");
		for (const auto &fixture : {name, QStringLiteral("msmFMID.pmr"), QStringLiteral("msmMMOB.mdb")})
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR) + '/' + fixture, folder + '/' + fixture));
		const QString path = folder + '/' + name;
		const QDateTime modified = QFileInfo(path).lastModified();
		bool changed = false;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.progress = [&](int, int, const QString &source)
		{
			if (source != path)
				return;
			QFile file(path);
			changed = file.open(QIODevice::ReadWrite) && file.setFileTime(modified.addSecs(10), QFileDevice::FileModificationTime);
		};
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(changed);
		QCOMPARE(scan.files.size(), 1);
		const auto &evidence = scan.files.front().evidence;
		QVERIFY(!evidence.selected(MediaProperty::FileMobId).value.isValid());
		QVERIFY(!evidence.selected(MediaProperty::Compression).value.isValid());
		bool retainedDatabase = false;
		for (const auto &value : evidence.observations(MediaProperty::Compression))
			if (value.snapshot && value.snapshot->source == MetadataSource::Mdb && value.readState == PropertyReadState::Present)
			{
				retainedDatabase = true;
				QVERIFY(!value.eligible);
			}
		QVERIFY(retainedDatabase);
		QVERIFY(std::any_of(scan.reconciliationIssues.cbegin(), scan.reconciliationIssues.cend(), [](const ScanIssue &issue)
							{ return issue.kind == ScanIssue::Kind::SourceChanged; }));
	}
	void effect_inference_follows_selected_name_without_losing_evidence()
	{
		MediaEvidence evidence;
		MetadataObservation name;
		name.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mxf, QStringLiteral("render.mxf"), {}, SourceReadState::Complete});
		name.property = QStringLiteral("GenericPackage.Name");
		name.readState = PropertyReadState::Present;
		name.value = QStringLiteral("3D_Warp");
		evidence.observe(MediaProperty::ClipName, name);
		auto type = name;
		type.property = QStringLiteral("Avid.UsageCode");
		type.value = int(MediaFile::Type::Precompute);
		evidence.observe(MediaProperty::Type, type);
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::Effect).value.toString(), QStringLiteral("3D Warp"));
		QCOMPARE(evidence.observations(MediaProperty::Effect).front().basis, EvidenceBasis::Derived);
		QCOMPARE(evidence.observations(MediaProperty::Effect).front().snapshot, name.snapshot);
		// A conflicting name invalidates selection. The previous derived effect
		// remains evidence, but it must no longer be a current value.
		name.value = QStringLiteral("unknown private effect");
		evidence.observe(MediaProperty::ClipName, name);
		MediaEngine::selectMetadata(evidence);
		QVERIFY(!evidence.selected(MediaProperty::Effect).value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::Effect).size(), 1);
		QVERIFY(!evidence.observations(MediaProperty::Effect).front().eligible);
	}
	void equivalent_rationals_and_durations_do_not_create_false_conflicts()
	{
		MediaEvidence evidence;
		const auto add = [&](MediaProperty field, const QVariant &value)
		{
			MetadataObservation item;
			item.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mxf, {}, {}, SourceReadState::Complete});
			item.readState = PropertyReadState::Present;
			item.value = value;
			evidence.observe(field, item);
		};
		add(MediaProperty::FrameRate, MediaEngine::rateValue({25, 1}));
		add(MediaProperty::FrameRate, MediaEngine::rateValue({50, 2}));
		add(MediaProperty::FileDuration, MediaEngine::durationValue({100, {25, 1}, {25, 1}, MediaDuration::Source::Descriptor}));
		add(MediaProperty::FileDuration, MediaEngine::durationValue({100, {50, 2}, {50, 2}, MediaDuration::Source::FileTrack}));
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::FrameRate).agreement, PropertyAgreement::Agreeing);
		QCOMPARE(evidence.selected(MediaProperty::FileDuration).agreement, PropertyAgreement::Agreeing);
		auto duration = MediaEngine::mediaDuration(evidence.selected(MediaProperty::FileDuration).value);
		QCOMPARE(duration.units, 100);
		QVERIFY(duration.displayRate.sameRate({25, 1}));
		// The optional display clock may disagree without destroying the
		// independently established file length and its original clock.
		add(MediaProperty::FileDuration, MediaEngine::durationValue({100, {25, 1}, {24, 1}, MediaDuration::Source::Descriptor}));
		MediaEngine::selectMetadata(evidence);
		duration = MediaEngine::mediaDuration(evidence.selected(MediaProperty::FileDuration).value);
		QCOMPARE(duration.units, 100);
		QVERIFY(duration.rate.sameRate({25, 1}));
		QVERIFY(!duration.displayRate.valid());
	}
	void central_policy_selects_one_master_and_keeps_duration_clocks_distinct()
	{
		MediaEvidence evidence;
		const auto add = [&](MediaProperty property, MetadataSource source, const QVariant &value, bool eligible = true)
		{
			MetadataObservation item;
			item.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{source, {}, {}, SourceReadState::Complete});
			item.readState = PropertyReadState::Present;
			item.value = value;
			item.eligible = eligible;
			evidence.observe(property, item);
		};
		add(MediaProperty::MasterMobId, MetadataSource::Pmr, QStringLiteral("master-b"));
		add(MediaProperty::MasterMobId, MetadataSource::Mxf, QStringLiteral("master-a"));
		add(MediaProperty::MasterMobId, MetadataSource::Mdb, QStringLiteral("wrong-owner"), false);
		add(MediaProperty::FileDuration, MetadataSource::Mxf,
			MediaEngine::durationValue({100, {25, 1}, {}, MediaDuration::Source::Descriptor}));
		add(MediaProperty::FileDuration, MetadataSource::Mdb,
			MediaEngine::durationValue({100, {50, 2}, {25, 1}, MediaDuration::Source::FileTrack}));
		// This alternative clock belongs to a different duration; it cannot fill ours.
		add(MediaProperty::FileDuration, MetadataSource::Mdb,
			MediaEngine::durationValue({200, {25, 1}, {24, 1}, MediaDuration::Source::FileTrack}));
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::MasterMobId).value.metaType().id(), QMetaType::QString);
		QCOMPARE(evidence.selected(MediaProperty::MasterMobId).value.toString(), QStringLiteral("master-a"));
		QCOMPARE(evidence.selected(MediaProperty::MasterMobId).agreement, PropertyAgreement::Conflicting);
		QCOMPARE(evidence.observations(MediaProperty::MasterMobId).size(), 3);
		const auto duration = MediaEngine::mediaDuration(evidence.selected(MediaProperty::FileDuration).value);
		QCOMPARE(duration.units, 100);
		QVERIFY(duration.rate.sameRate({25, 1}));
		QVERIFY(duration.displayRate.sameRate({25, 1}));
		for (const auto &policy : MediaEngine::propertyPolicies())
		{
			const auto selected = evidence.selected(policy.property);
			QCOMPARE(selected.rule, mediaPropertyName(policy.property));
		}
	}
	void external_cancellation_is_observed()
	{
		std::atomic_bool flag{false};
		const MediaEngine::Cancellation cancellation(&flag);
		QVERIFY(!cancellation.cancelled());
		flag.store(true);
		const auto scan = MediaEngine::ScanEngine{}.scan({}, cancellation);
		QVERIFY(scan.cancelled);
		QVERIFY(!scan.reconciliationComplete);
		QVERIFY(!scan.parsingComplete);
	}
	void cancellation_before_source_open_data()
	{
		QTest::addColumn<bool>("withDatabases");
		QTest::addColumn<bool>("atReading");
		QTest::newRow("database-progress") << true << false;
		QTest::newRow("header-progress") << false << false;
		QTest::newRow("database-reading") << true << true;
		QTest::newRow("header-reading") << false << true;
	}
	void cancellation_before_source_open()
	{
		QFETCH(bool, withDatabases);
		QFETCH(bool, atReading);
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		QVERIFY(tryWriteFile(path, "Header must remain unopened"));
		if (withDatabases)
		{
			QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"),
				pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
			QVERIFY(tryWriteFile(folder + QStringLiteral("/metadata.mdb"), audioDatabase()));
		}
		MediaEngine::Cancellation cancellation;
		int progressCalls = 0;
		int readingCalls = 0;
		bool finalising = false;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.progress = [&](int, int, const QString &)
		{
			++progressCalls;
			if (!atReading)
				cancellation.cancel();
		};
		callbacks.reading = [&](const MediaEngine::SourceCandidate &)
		{
			++readingCalls;
			cancellation.cancel();
		};
		callbacks.finalising = [&] { finalising = true; };
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QCOMPARE(progressCalls, 1);
		QCOMPARE(readingCalls, atReading ? 1 : 0);
		QVERIFY(!finalising);
		QVERIFY(scan.cancelled);
		QVERIFY(scan.discoveryComplete);
		QVERIFY(!scan.parsingComplete);
		QVERIFY(!scan.reconciliationComplete);
		QCOMPARE(scan.files.size(), 1);
		QCOMPARE(scan.files.front().path, path);
		QCOMPARE(scan.sources.size(), scan.candidates.size());
		for (const auto &source : scan.sources)
		{
			QCOMPARE(source.outcome, MediaEngine::ParsedSource::Outcome::NotRead);
			QCOMPARE(source.snapshot->readState, SourceReadState::NotRead);
			QVERIFY(source.diagnostics.isEmpty());
		}
		QVERIFY(scan.reconciliationIssues.isEmpty());
	}
	void cancellation_after_database_read_keeps_source_receipt()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		const QString database = folder + QStringLiteral("/index.pmr");
		QVERIFY(tryWriteFile(path, "Header must remain unopened"));
		QVERIFY(tryWriteFile(database, pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		MediaEngine::Cancellation cancellation;
		QStringList discovered;
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.discovering = [&](const QString &folder) { discovered.append(folder); };
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			QVERIFY(!cancellation.cancelled());
			++reads[candidate.path];
		};
		callbacks.progress = [&](int, int, const QString &current)
		{
			QVERIFY(!discovered.isEmpty());
			if (current == path)
				cancellation.cancel();
		};
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(scan.cancelled);
		QVERIFY(!scan.reconciliationComplete);
		QCOMPARE(scan.files.size(), 1);
		const auto *parsed = mediaSource(scan, database);
		QVERIFY(parsed);
		QCOMPARE(parsed->outcome, MediaEngine::ParsedSource::Outcome::Complete);
		QCOMPARE(parsed->snapshot->readState, SourceReadState::Complete);
		QVERIFY(!parsed->readReason.isEmpty());
		QCOMPARE(mediaSource(scan, path)->outcome, MediaEngine::ParsedSource::Outcome::NotRead);
		QVERIFY(scan.reconciliationIssues.isEmpty());
		QCOMPARE(reads.size(), 1);
		QCOMPARE(reads.value(database), 1);
		const auto &file = scan.files.front();
		QVERIFY(file.stamp.fileMobId.isEmpty());
		QVERIFY(!file.evidence.selected(MediaProperty::Project).value.isValid());
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan), temporary.path(), QStringLiteral("Test volume"), true);
		QCOMPARE(row.dbStatus, MediaFile::DbStatus::DbUnreadable);
		QVERIFY(row.operationScanReceipt);
		QVERIFY(row.operationScanReceipt->headerIdentity.fileMobId.isEmpty());
		QVERIFY(row.operationScanReceipt->databaseFileMobIdToVerify.isEmpty());
	}
	void cancellation_at_finalising_preserves_completed_header_values()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		for (const auto &name : {QStringLiteral("a.mxf"), QStringLiteral("b.mxf")})
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), folder + '/' + name));
		const MediaEngine::Cancellation completeCancellation;
		const auto complete = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, completeCancellation);
		QVERIFY(complete.reconciliationComplete);
		QCOMPARE(complete.files.size(), 2);
		MediaEngine::Cancellation cancellation;
		QHash<QString, int> reads;
		bool finalising = false;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			QVERIFY(!cancellation.cancelled());
			++reads[candidate.path];
		};
		callbacks.finalising = [&]
		{
			finalising = true;
			cancellation.cancel();
		};
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(finalising);
		QVERIFY(scan.cancelled);
		QVERIFY(scan.discoveryComplete);
		QVERIFY(!scan.reconciliationComplete);
		QCOMPARE(scan.files.size(), complete.files.size());
		QCOMPARE(reads.size(), 2);
		const ScanRowLookup lookup(scan);
		for (qsizetype index = 0; index < scan.files.size(); ++index)
		{
			const auto &file = scan.files[index];
			const auto &expected = complete.files[index];
			QCOMPARE(file.path, expected.path);
			QCOMPARE(file.kelpieId, expected.kelpieId);
			QCOMPARE(reads.value(file.path), 1);
			QVERIFY(file.sizeBytes > 0);
			QCOMPARE(mediaSource(scan, file.path)->outcome, MediaEngine::ParsedSource::Outcome::Complete);
			// Compare usable results, not the temporary observation histories.
			for (const auto &policy : MediaEngine::propertyPolicies())
			{
				if (policy.property == MediaProperty::DatabaseStatus)
					continue;
				const auto selected = file.evidence.selected(policy.property);
				const auto retained = expected.evidence.selected(policy.property);
				QCOMPARE(selected.value, retained.value);
				QCOMPARE(selected.readState, retained.readState);
				QCOMPARE(selected.agreement, retained.agreement);
			}
			QCOMPARE(file.stamp.fileMobId, MediaEngine::canonicalDatabaseId(toneFileId));
			QCOMPARE(file.stamp.masterMobId, MediaEngine::canonicalDatabaseId(toneMasterId));
			const auto row = mediaEngineMediaFile(file, lookup, temporary.path(), QStringLiteral("Test volume"), true);
			QCOMPARE(row.metadataStatus.headerOutcome, MediaHeaderReadOutcome::Complete);
			QVERIFY(row.metadataStatus.field(MediaProperty::FileDuration).hasSelectedValue);
			QVERIFY(row.operationScanReceipt);
			QCOMPARE(row.operationScanReceipt->headerIdentity.fileMobId, file.stamp.fileMobId);
			QCOMPARE(row.operationScanReceipt->headerIdentity.masterMobIds, QStringList{file.stamp.masterMobId});
			QVERIFY(row.operationScanReceipt->headerIdentity.unavailableReason.isEmpty());
			QVERIFY(row.operationScanReceipt->databaseFileMobIdToVerify.isEmpty());
		}
	}
	void cancellation_before_second_media_decision_preserves_only_qualified_rows_data()
	{
		QTest::addColumn<bool>("withDatabases");
		QTest::newRow("completed-genuine-header") << false;
		QTest::newRow("completed-database-match") << true;
	}
	void cancellation_before_second_media_decision_preserves_only_qualified_rows()
	{
		QFETCH(bool, withDatabases);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		const QString first = folder + QStringLiteral("/a.mxf"), second = folder + QStringLiteral("/b.mxf");
		for (const auto &path : {first, second})
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		const QString pmrPath = folder + QStringLiteral("/index.pmr");
		const QString mdbPath = folder + QStringLiteral("/metadata.mdb");
		if (withDatabases)
		{
			QVERIFY(tryWriteFile(pmrPath, pmr({pmrRecord("a.mxf", toneFileId, "Project"), pmrRecord("b.mxf", toneFileId, "Project")})));
			QVERIFY(tryWriteFile(mdbPath, audioDatabase()));
		}
		MediaEngine::Cancellation cancellation;
		QHash<QString, int> reads;
		bool finalising = false;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.progress = [&](int, int, const QString &path)
		{
			if (path == second)
				cancellation.cancel();
		};
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			QVERIFY(!cancellation.cancelled());
			++reads[candidate.path];
		};
		callbacks.finalising = [&] { finalising = true; };
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(scan.cancelled);
		QVERIFY(scan.discoveryComplete);
		QVERIFY(!scan.parsingComplete);
		QVERIFY(!scan.reconciliationComplete);
		QVERIFY(!finalising);
		QCOMPARE(scan.files.size(), 2);
		QCOMPARE(scan.files[0].path, first);
		QCOMPARE(scan.files[1].path, second);
		QVERIFY(scan.files[0].kelpieId != scan.files[1].kelpieId);
		QCOMPARE(reads.value(first), withDatabases ? 0 : 1);
		QCOMPARE(reads.value(second), 0);
		QCOMPARE(reads.size(), withDatabases ? 2 : 1);
		if (withDatabases)
		{
			QCOMPARE(reads.value(pmrPath), 1);
			QCOMPARE(reads.value(mdbPath), 1);
		}
		const auto &qualified = scan.files[0];
		QCOMPARE(qualified.stamp.fileMobId, MediaEngine::canonicalDatabaseId(toneFileId));
		QCOMPARE(qualified.evidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("PCM"));
		QVERIFY(qualified.evidence.selected(MediaProperty::FileDuration).value.isValid());
		if (withDatabases)
		{
			QCOMPARE(qualified.evidence.selected(MediaProperty::ClipName).value.toString(), QStringLiteral("Database clip"));
			QCOMPARE(qualified.evidence.selected(MediaProperty::SourcePath).value.toString(), QStringLiteral("/Rushes/Database clip.wav"));
			QCOMPARE(qualified.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::Listed));
		}
		const ScanRowLookup lookup(scan);
		const auto retained = mediaEngineMediaFile(qualified, lookup, temporary.path(), QStringLiteral("Test volume"), true);
		QVERIFY(retained.operationScanReceipt);
		QCOMPARE(retained.metadataStatus.headerOutcome,
			withDatabases ? MediaHeaderReadOutcome::NotRead : MediaHeaderReadOutcome::Complete);
		QCOMPARE(retained.operationScanReceipt->databaseFileMobIdToVerify,
			withDatabases ? qualified.stamp.fileMobId : QString{});
		QCOMPARE(retained.operationScanReceipt->headerIdentity.fileMobId,
			withDatabases ? QString{} : qualified.stamp.fileMobId);
		const auto &unfinished = scan.files[1];
		QVERIFY(unfinished.sizeBytes > 0);
		QVERIFY(unfinished.stamp.fileMobId.isEmpty());
		QVERIFY(unfinished.stamp.masterMobId.isEmpty());
		// Discovery and a readable database do not verify this unvisited physical row.
		for (const auto property : {MediaProperty::FileMobId, MediaProperty::MasterMobId,
				 MediaProperty::ClipName, MediaProperty::Project, MediaProperty::FileDuration,
				 MediaProperty::Compression, MediaProperty::SourcePath})
			QVERIFY2(!unfinished.evidence.selected(property).value.isValid(), qPrintable(mediaPropertyName(property)));
		const auto untouched = mediaEngineMediaFile(unfinished, lookup, temporary.path(), QStringLiteral("Test volume"), true);
		QCOMPARE(untouched.metadataStatus.headerOutcome, MediaHeaderReadOutcome::NotRead);
		if (withDatabases)
			QCOMPARE(untouched.dbStatus, MediaFile::DbStatus::DbUnreadable);
		else
		{
			QVERIFY(untouched.dbStatus != MediaFile::DbStatus::Listed);
			QVERIFY(untouched.dbStatus != MediaFile::DbStatus::NoReference);
		}
		QCOMPARE(untouched.metadataStatus.field(MediaProperty::FileMobId).readState, PropertyReadState::NotRead);
		QVERIFY(untouched.operationScanReceipt);
		QVERIFY(untouched.operationScanReceipt->headerIdentity.fileMobId.isEmpty());
		QVERIFY(untouched.operationScanReceipt->headerIdentity.masterMobIds.isEmpty());
		QVERIFY(untouched.operationScanReceipt->databaseFileMobIdToVerify.isEmpty());
		for (const auto &issue : scan.reconciliationIssues)
			QVERIFY(!issue.scopeComplete);
	}
	void cancellation_at_finalising_preserves_completed_database_match()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		const QString path = folder + QStringLiteral("/take.mxf");
		const QString pmrPath = folder + QStringLiteral("/index.pmr");
		const QString mdbPath = folder + QStringLiteral("/metadata.mdb");
		QVERIFY(tryWriteFile(path, "A qualified database match leaves this header unopened"));
		QVERIFY(tryWriteFile(pmrPath, pmr({pmrRecord("take.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(mdbPath, audioDatabase()));
		MediaEngine::Cancellation cancellation;
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			QVERIFY(!cancellation.cancelled());
			++reads[candidate.path];
		};
		callbacks.finalising = [&] { cancellation.cancel(); };
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(scan.cancelled);
		QVERIFY(scan.discoveryComplete);
		QVERIFY(!scan.reconciliationComplete);
		QCOMPARE(scan.files.size(), 1);
		QCOMPARE(reads.size(), 2);
		QCOMPARE(reads.value(pmrPath), 1);
		QCOMPARE(reads.value(mdbPath), 1);
		QCOMPARE(reads.value(path), 0);
		QCOMPARE(mediaSource(scan, path)->outcome, MediaEngine::ParsedSource::Outcome::NotRead);
		const auto &file = scan.files.front();
		QCOMPARE(file.stamp.fileMobId, MediaEngine::canonicalDatabaseId(toneFileId));
		QCOMPARE(file.stamp.masterMobId, MediaEngine::canonicalDatabaseId(toneMasterId));
		QCOMPARE(file.evidence.selected(MediaProperty::ClipName).value.toString(), QStringLiteral("Database clip"));
		QCOMPARE(file.evidence.selected(MediaProperty::Project).value.toString(), QStringLiteral("Project"));
		QCOMPARE(file.evidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("PCM"));
		QCOMPARE(file.evidence.selected(MediaProperty::SourcePath).value.toString(), QStringLiteral("/Rushes/Database clip.wav"));
		QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(MediaFile::DbStatus::Listed));
		const auto duration = MediaEngine::mediaDuration(file.evidence.selected(MediaProperty::FileDuration).value);
		QCOMPARE(duration.units, qint64(96000));
		QVERIFY(duration.rate.sameRate({48000, 1}));
		QVERIFY(duration.displayRate.sameRate({25, 1}));
		const auto row = mediaEngineMediaFile(file, ScanRowLookup(scan), temporary.path(), QStringLiteral("Test volume"), true);
		QCOMPARE(row.metadataStatus.headerOutcome, MediaHeaderReadOutcome::NotRead);
		QCOMPARE(row.dbStatus, MediaFile::DbStatus::Listed);
		QVERIFY(row.operationScanReceipt);
		QVERIFY(row.operationScanReceipt->headerIdentity.fileMobId.isEmpty());
		QVERIFY(row.operationScanReceipt->headerIdentity.masterMobIds.isEmpty());
		QCOMPARE(row.operationScanReceipt->databaseFileMobIdToVerify, file.stamp.fileMobId);
	}
	void cancellation_keeps_known_changed_sources_excluded_data()
	{
		QTest::addColumn<bool>("changeDatabase");
		QTest::newRow("physical-file-changed") << false;
		QTest::newRow("mdb-changed") << true;
	}
	void cancellation_keeps_known_changed_sources_excluded()
	{
		QFETCH(bool, changeDatabase);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(QDir().mkpath(folder));
		const QString first = folder + QStringLiteral("/a.mxf"), second = folder + QStringLiteral("/b.mxf");
		for (const auto &path : {first, second})
			QVERIFY(QFile::copy(QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf"), path));
		const QString pmrPath = folder + QStringLiteral("/index.pmr");
		const QString mdbPath = folder + QStringLiteral("/metadata.mdb");
		QVERIFY(tryWriteFile(pmrPath, pmr({pmrRecord("a.mxf", toneFileId, "Project"), pmrRecord("b.mxf", toneFileId, "Project")})));
		QVERIFY(tryWriteFile(mdbPath, audioDatabase()));
		const QString changedPath = changeDatabase ? mdbPath : first;
		const QDateTime originalModified = QFileInfo(changedPath).lastModified();
		bool changed = false;
		MediaEngine::Cancellation cancellation;
		QHash<QString, int> reads;
		MediaEngine::ScanCallbacks callbacks;
		callbacks.progress = [&](int, int, const QString &path)
		{
			if (path == second)
			{
				QFile file(changedPath);
				changed = file.open(QIODevice::ReadWrite) &&
					file.setFileTime(originalModified.addSecs(10), QFileDevice::FileModificationTime);
			}
		};
		callbacks.reading = [&](const MediaEngine::SourceCandidate &candidate)
		{
			QVERIFY(!cancellation.cancelled());
			++reads[candidate.path];
		};
		callbacks.finalising = [&] { cancellation.cancel(); };
		const auto scan = MediaEngine::ScanEngine{}.scan(
			{{temporary.path() + QStringLiteral("/Avid MediaFiles/MXF")}, false}, cancellation, callbacks);
		QVERIFY(changed);
		QVERIFY(scan.cancelled);
		QVERIFY(!scan.reconciliationComplete);
		QCOMPARE(scan.files.size(), 2);
		QCOMPARE(reads.value(pmrPath), 1);
		QCOMPARE(reads.value(mdbPath), 1);
		QCOMPARE(reads.value(first), 1);
		QCOMPARE(reads.value(second), changeDatabase ? 1 : 0);
		QCOMPARE(reads.size(), changeDatabase ? 4 : 3);
		QVERIFY(std::any_of(scan.reconciliationIssues.cbegin(), scan.reconciliationIssues.cend(), [&](const ScanIssue &issue)
		{
			return issue.kind == ScanIssue::Kind::SourceChanged && issue.expectedPath == changedPath;
		}));
		const ScanRowLookup lookup(scan);
		for (const auto &file : scan.files)
		{
			if (!changeDatabase && file.path == first)
			{
				QVERIFY(file.stamp.fileMobId.isEmpty());
				QVERIFY(!file.evidence.selected(MediaProperty::Compression).value.isValid());
				const auto row = mediaEngineMediaFile(file, lookup, temporary.path(), QStringLiteral("Test volume"), true);
				QVERIFY(row.operationScanReceipt);
				QVERIFY(!row.operationScanReceipt->headerIdentity.unavailableReason.isEmpty());
				QVERIFY(row.operationScanReceipt->databaseFileMobIdToVerify.isEmpty());
				continue;
			}
			QCOMPARE(file.stamp.fileMobId, MediaEngine::canonicalDatabaseId(toneFileId));
			QCOMPARE(file.evidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("PCM"));
			if (changeDatabase)
			{
				QVERIFY(file.evidence.selected(MediaProperty::ClipName).value.toString() != QStringLiteral("Database clip"));
				QVERIFY(file.evidence.selected(MediaProperty::SourcePath).value.toString() != QStringLiteral("/Rushes/Database clip.wav"));
				for (int index = 0; index < int(MediaProperty::Count); ++index)
					for (const auto &observation : file.evidence.observations(MediaProperty(index)))
						if (observation.snapshot && observation.snapshot->path == mdbPath)
							QVERIFY(!observation.eligible);
			}
		}
		for (const auto &issue : scan.reconciliationIssues)
			QVERIFY(!issue.scopeComplete);
	}
	void row_conversion_reuses_known_volume_and_does_not_probe_after_cancel()
	{
		const auto scan = QSharedPointer<MediaEngine::ScanResult>::create();
		scan->cancelled = true;
		MediaEngine::MediaFile file;
		file.kelpieId = 42;
		file.path = QStringLiteral("/unavailable/Avid MediaFiles/MXF/1/take.mxf");
		file.volumeIdentifier = QStringLiteral("captured-physical-volume");
		file.stamp.volumeIdentifier = file.volumeIdentifier;
		const ScanRowLookup lookup(*scan);
		const auto known = mediaEngineMediaFile(file, lookup, QStringLiteral("/selected-volume"), QStringLiteral("NEXIS workspace"), scan->cancelled);
		QCOMPARE(known.volumePath, QStringLiteral("/selected-volume"));
		QCOMPARE(known.volumeName, QStringLiteral("NEXIS workspace"));
		QCOMPARE(known.kelpieId, KelpieId(42));
		QCOMPARE(known.scanStamp.volumeIdentifier, file.volumeIdentifier);
		const auto unknown = mediaEngineMediaFile(file, lookup, {}, {}, scan->cancelled);
		QVERIFY(unknown.volumePath.isEmpty());
		QVERIFY(unknown.volumeName.isEmpty());
		QCOMPARE(unknown.mediaFilePath, file.path);
	}
};

QTEST_GUILESS_MAIN(TestMediaEngineScan)
#include "tst_mediaenginescan.moc"
