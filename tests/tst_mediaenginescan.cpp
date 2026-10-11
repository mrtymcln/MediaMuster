#include "mediaengine/scancoordinator.h"
#include "mediaengine/scanengine.h"
#include "mediaengine/mdbreader.h"
#include "mediaengine/projection.h"
#include "mediaengine/metadataselectionpolicy.h"
#include "mediaengineadapter.h"
#include "testutil.h"
#include "testmediaenginebento.h"
#include <QtEndian>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <QDir>
#include <QFile>
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
	QByteArray pmrRecord(const QByteArray &filename, const QByteArray &identity, const QByteArray &project)
	{
		QByteArray bytes = identity;
		appendNumber<quint16>(bytes, quint16(filename.size()));
		bytes.append(filename);
		appendNumber<quint16>(bytes, quint16(project.size()));
		bytes.append(project);
		bytes.append(toneMasterId);
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
		bool contradictoryFileIdentity = false)
	{
		using TestMediaEngineBento::number;
		TestMediaEngineBento::TypedBento writer;
		writer.head(1);
		writer.referenceArray(1, "OMFI:ObjectSpine", members, 1);
		for (const auto &object : QList<QPair<quint32, QByteArray>>{{101, "MOBJ"}, {201, "WAVD"}, {301, "MOBJ"}, {401, "TRAK"}, {501, "SCLP"}})
			writer.add(object.first, "OMFI:ObjID", "omfi:ObjectTag", object.second, true);
		writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", toneFileId);
		writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		writer.add(101, "OMFI:CPNT:EditRate", "omfi:Rational", number<qint32>(25) + number<qint32>(1));
		const QByteArray format = number<quint16>(1) + number<quint16>(1) + number<quint32>(48000) +
								  number<quint32>(144000) + number<quint16>(3) + number(bitDepth);
		const QByteArray wave = QByteArray("WAVEfmt ") + number<quint32>(format.size()) + format;
		writer.add(201, "OMFI:WAVD:Summary", "omfi:DataValue", QByteArray("RIFF") + number<quint32>(wave.size()) + wave);
		writer.add(201, "OMFI:MDFL:SampleRate", "omfi:Rational", number<qint32>(48000) + number<qint32>(1));
		writer.add(201, "OMFI:MDFL:Length", "omfi:Length32", number<qint32>(96000));
		writer.add(301, "OMFI:MOBJ:MobID", "omfi:UID", toneMasterId);
		writer.add(301, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", number<quint32>(7), true);
		writer.add(301, "OMFI:CPNT:Name", "omfi:String", QByteArray("Database clip\0", 14));
		writer.add(301, "OMFI:TRKG:Tracks", "omfi:ObjRefArray", number<quint16>(1) + writer.reference(401, 1));
		writer.add(401, "OMFI:TRAK:LabelNumber", "omfi:UInt16", number<quint16>(1), true);
		writer.add(401, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(501, 1));
		writer.add(501, "OMFI:SCLP:SourceID", "omfi:UID", toneFileId);
		writer.add(501, "OMFI:CLIP:Length", "omfi:Length32", number<quint32>(50));
		writer.add(501, "OMFI:CPNT:EditRate", "omfi:Rational", number<qint32>(25) + number<qint32>(1));
		if (contradictoryFileIdentity)
		{
			QByteArray alternative = toneFileId;
			alternative[31] = char(0x14);
			writer.add(601, "OMFI:ObjID", "omfi:ObjectTag", "MOBJ", true);
			writer.add(601, "OMFI:MOBJ:MobID", "omfi:UID", toneFileId);
			writer.add(601, "OMFI:MOBJ:MobID", "omfi:UID", alternative);
			writer.add(601, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
		}
		return writer.build();
	}
	const MediaEngine::SourceReceipt *mediaSource(const MediaEngine::ScanResult &scan, const QString &path)
	{
		for (const auto &source : scan.sources)
			if (source.snapshot && source.snapshot->path == path)
				return &source;
		return nullptr;
	}
}

class TestMediaEngineScan : public QObject
{
	Q_OBJECT
private slots:
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.files.front().kelpieId != 0);
		QVERIFY(mediaSource(scan, path)->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		const auto &evidence = scan.files.front().evidence;
		QVERIFY(!evidence.selected(MediaProperty::ClipName).value.isValid());
		QCOMPARE(evidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("PCM"));
		QCOMPARE(evidence.selected(MediaProperty::FileMobId).value.toString(),
			MediaEngine::canonicalDatabaseId(toneFileId));
	}

	void complete_database_does_not_open_media_header()
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
		callbacks.progress = [&](int, int, const QString &source)
		{ decisions.append(source); };
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
		QCOMPARE(scan.files.size(), 1);
		QVERIFY(scan.reconciliationComplete);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QVERIFY2(header->outcome == MediaEngine::ParsedSource::Outcome::NotRead, qPrintable(header->readReason));
		QCOMPARE(header->snapshot->readState, SourceReadState::NotRead);
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
		QVERIFY(!evidence.selected(MediaProperty::OriginalBin).value.isValid());
		QVERIFY(!evidence.selected(MediaProperty::SourceFilename).value.isValid());
		for (const auto &value : evidence.observations(MediaProperty::Compression))
			if (value.readState == PropertyReadState::Present)
			{
				QCOMPARE(value.snapshot->source, MetadataSource::Mdb);
				QCOMPARE(value.freshness, SourceFreshness::Unknown);
			}
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
		const auto *header = mediaSource(scan, path);
		QVERIFY(header);
		QVERIFY(header->outcome != MediaEngine::ParsedSource::Outcome::NotRead);
		QVERIFY2(header->readReason.contains(QStringLiteral("Bit Depth")), qPrintable(header->readReason));
		const auto &evidence = scan.files.front().evidence;
		const auto depth = evidence.selected(MediaProperty::BitDepth);
		QVERIFY(depth.value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::BitDepth)[depth.selectedObservation].snapshot->source, MetadataSource::Mxf);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
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
	void clip_duration_agreement_ignores_graph_numbering_and_track_order()
	{
		MediaEvidence evidence;
		const auto track = [](quint32 trackId, quint64 object, qint64 units)
		{
			return QVariantMap{{QStringLiteral("MasterMobId"), QStringLiteral("master")}, {QStringLiteral("TrackId"), trackId}, {QStringLiteral("DurationObject"), object}, {QStringLiteral("DurationProperty"), QStringLiteral("OMFI:SEQU:Sequence")}, {QStringLiteral("DurationComponents"), QVariantList{QVariantMap{{QStringLiteral("Object"), object + 1}}}}, {QStringLiteral("DropFrame"), false}, {QStringLiteral("Duration"), MediaEngine::durationValue({units, {25, 1}, {25, 1}, MediaDuration::Source::ClipReference})}};
		};
		MetadataObservation observation;
		observation.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mdb, QStringLiteral("first.mdb"), {}, SourceReadState::Complete});
		observation.readState = PropertyReadState::Present;
		observation.value = QVariantList{track(1, 11, 250), track(2, 12, 300)};
		evidence.observe(MediaProperty::ClipDuration, observation);
		observation.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mdb, QStringLiteral("second.mdb"), {}, SourceReadState::Complete});
		observation.value = QVariantList{track(2, 102, 300), track(1, 101, 250)};
		evidence.observe(MediaProperty::ClipDuration, observation);
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::ClipDuration).agreement, PropertyAgreement::Agreeing);
		QVERIFY(evidence.selected(MediaProperty::ClipDuration).value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::ClipDuration).size(), 2);
		for (const auto &contradiction : {QStringLiteral("length"), QStringLiteral("clock"), QStringLiteral("drop frame"), QStringLiteral("track identity")})
		{
			auto variant = evidence;
			auto changed = track(1, 201, contradiction == QLatin1String("length") ? 251 : 250);
			if (contradiction == QLatin1String("clock"))
				changed[QStringLiteral("Duration")] = MediaEngine::durationValue({250, {25, 1}, {24, 1}, MediaDuration::Source::ClipReference});
			if (contradiction == QLatin1String("drop frame"))
				changed[QStringLiteral("DropFrame")] = true;
			if (contradiction == QLatin1String("track identity"))
				changed[QStringLiteral("TrackId")] = 9;
			observation.value = QVariantList{changed, track(2, 202, 300)};
			variant.observe(MediaProperty::ClipDuration, observation);
			MediaEngine::selectMetadata(variant);
			QVERIFY2(!variant.selected(MediaProperty::ClipDuration).value.isValid(), qPrintable(contradiction));
			QCOMPARE(variant.selected(MediaProperty::ClipDuration).agreement, PropertyAgreement::Conflicting);
		}
	}
	void clip_duration_coalesces_duplicate_facts_without_discarding_evidence()
	{
		const QVariantMap first{{QStringLiteral("MasterMobId"), QStringLiteral("master")}, {QStringLiteral("TrackId"), 1}, {QStringLiteral("DurationObject"), quint64(11)}, {QStringLiteral("DropFrame"), false}, {QStringLiteral("Duration"), MediaEngine::durationValue({250, {25, 1}, {25, 1}, MediaDuration::Source::ClipReference})}};
		auto duplicate = first;
		duplicate[QStringLiteral("DurationObject")] = quint64(22);
		duplicate[QStringLiteral("Duration")] = MediaEngine::durationValue({250, {50, 2}, {50, 2}, MediaDuration::Source::ClipReference});
		MetadataObservation observation;
		observation.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mdb, QStringLiteral("first.mdb"), {}, SourceReadState::Complete});
		observation.readState = PropertyReadState::Present;
		observation.value = QVariantList{first, duplicate};
		MediaEvidence evidence;
		evidence.observe(MediaProperty::ClipDuration, observation);
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::ClipDuration).value.toList().size(), 1);
		QCOMPARE(evidence.observations(MediaProperty::ClipDuration).first().value.toList(), (QVariantList{first, duplicate}));
		// Another database may encode this same fact only once.
		observation.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mdb, QStringLiteral("second.mdb"), {}, SourceReadState::Complete});
		observation.value = QVariantList{duplicate};
		evidence.observe(MediaProperty::ClipDuration, observation);
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::ClipDuration).agreement, PropertyAgreement::Agreeing);
		QCOMPARE(evidence.selected(MediaProperty::ClipDuration).value.toList().size(), 1);
		QCOMPARE(evidence.observations(MediaProperty::ClipDuration).size(), 2);
		for (const auto &difference : {QStringLiteral("master"), QStringLiteral("track"), QStringLiteral("timing")})
		{
			auto other = duplicate;
			if (difference == QLatin1String("master"))
				other[QStringLiteral("MasterMobId")] = QStringLiteral("another master");
			else if (difference == QLatin1String("track"))
				other[QStringLiteral("TrackId")] = 2;
			else
				other[QStringLiteral("Duration")] = MediaEngine::durationValue({251, {25, 1}, {25, 1}, MediaDuration::Source::ClipReference});
			MediaEvidence distinct;
			observation.value = QVariantList{first, other};
			distinct.observe(MediaProperty::ClipDuration, observation);
			MediaEngine::selectMetadata(distinct);
			if (difference == QLatin1String("timing"))
			{
				QVERIFY(!distinct.selected(MediaProperty::ClipDuration).value.isValid());
				QCOMPARE(distinct.selected(MediaProperty::ClipDuration).agreement, PropertyAgreement::Conflicting);
			}
			else
				QCOMPARE(distinct.selected(MediaProperty::ClipDuration).value.toList().size(), 2);
			QCOMPARE(distinct.observations(MediaProperty::ClipDuration).first().value.toList(), (QVariantList{first, other}));
		}
	}
	void copies_own_rows_and_share_source_receipts()
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
		const auto scan = QSharedPointer<MediaEngine::ScanResult>::create(MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation));
		QCOMPARE(scan->files.size(), 2);
		QVERIFY(scan->reconciliationComplete);
		QVERIFY(scan->files[0].kelpieId != scan->files[1].kelpieId);
		QVERIFY(scan->files[0].kelpieId != 0);
		QCOMPARE(scan->sources.size(), 2);
		const auto first = mediaEngineMediaFile(scan->files[0], scan);
		const auto second = mediaEngineMediaFile(scan->files[1], scan);
		QVERIFY(!first.fileMobId.isEmpty());
		QCOMPARE(first.fileMobId, second.fileMobId);
		QVERIFY(first.mediaFilePath != second.mediaFilePath);
		QCOMPARE(first.mediaEngineScan, second.mediaEngineScan);
		QCOMPARE(first.scanStamp.mobId, first.fileMobId);
		const auto &stored = first.mediaEngineScan->sources.front();
		QCOMPARE(stored.outcome, MediaEngine::ParsedSource::Outcome::Complete);
		QCOMPARE(stored.snapshot->readState, SourceReadState::Complete);
		QVERIFY(std::any_of(first.evidence.observations(MediaProperty::FileMobId).cbegin(),
			first.evidence.observations(MediaProperty::FileMobId).cend(),
			[&](const MetadataObservation &observation) { return observation.snapshot == stored.snapshot; }));
		QVERIFY(!first.evidence.observations(MediaProperty::FileMobId).isEmpty());
		QCOMPARE(first.kind, MediaFile::Kind::Audio);
		QCOMPARE(first.dbStatus, MediaFile::DbStatus::NoDatabase);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
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
	void pmr_unique_normalized_name_is_a_fallback()
	{
		QTemporaryDir temporary;
		const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
		QVERIFY(tryWriteFile(folder + QStringLiteral("/take.mxf"), "not an MXF header"));
		QVERIFY(tryWriteFile(folder + QStringLiteral("/index.pmr"), pmr({pmrRecord("TAKE.MXF", toneFileId, "normalized match")})));
		const MediaEngine::Cancellation cancellation;
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
		QCOMPARE(scan.files.size(), 1);
		const auto &evidence = scan.files.front().evidence;
		QVERIFY(!evidence.selected(MediaProperty::FileMobId).value.isValid());
		QVERIFY(!evidence.selected(MediaProperty::Project).value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::Project).size(), 2);
		for (const auto &value : evidence.observations(MediaProperty::Project))
			QVERIFY(!value.eligible);
		QVERIFY(std::any_of(scan.reconciliationIssues.cbegin(), scan.reconciliationIssues.cend(), [](const ScanIssue &issue)
							{ return issue.kind == ScanIssue::Kind::MetadataConflict && issue.explanation.contains(QStringLiteral("multiple compatible file identities")); }));
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation);
		QCOMPARE(scan.files.size(), 2);
		for (const auto &file : scan.files)
		{
			QVERIFY(!file.evidence.selected(MediaProperty::FileMobId).value.isValid());
			QVERIFY(!file.evidence.selected(MediaProperty::Project).value.isValid());
			QVERIFY(!file.evidence.observations(MediaProperty::Project).isEmpty());
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
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
	void central_policy_keeps_associations_and_duration_clocks_distinct()
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
		add(MediaProperty::MasterMobId, MetadataSource::Mxf, QStringList{QStringLiteral("master-a"), QStringLiteral("master-b")});
		add(MediaProperty::MasterMobId, MetadataSource::Mdb, QStringLiteral("wrong-owner"), false);
		add(MediaProperty::FileDuration, MetadataSource::Mxf,
			MediaEngine::durationValue({100, {25, 1}, {}, MediaDuration::Source::Descriptor}));
		add(MediaProperty::FileDuration, MetadataSource::Mdb,
			MediaEngine::durationValue({100, {50, 2}, {25, 1}, MediaDuration::Source::FileTrack}));
		// This alternative clock belongs to a different duration; it cannot fill ours.
		add(MediaProperty::FileDuration, MetadataSource::Mdb,
			MediaEngine::durationValue({200, {25, 1}, {24, 1}, MediaDuration::Source::FileTrack}));
		MediaEngine::selectMetadata(evidence);
		QCOMPARE(evidence.selected(MediaProperty::MasterMobId).value.toStringList(),
			(QStringList{QStringLiteral("master-a"), QStringLiteral("master-b")}));
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
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
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
		MediaEngine::ScanCallbacks callbacks;
		callbacks.discovering = [&](const QString &folder) { discovered.append(folder); };
		callbacks.progress = [&](int, int, const QString &current)
		{
			QVERIFY(!discovered.isEmpty());
			if (current == path)
				cancellation.cancel();
		};
		const auto scan = MediaEngine::ScanEngine{}.scan({{temporary.path()}, false}, cancellation, callbacks);
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
		const auto known = mediaEngineMediaFile(file, scan, QStringLiteral("/selected-volume"), QStringLiteral("NEXIS workspace"));
		QCOMPARE(known.volumePath, QStringLiteral("/selected-volume"));
		QCOMPARE(known.volumeName, QStringLiteral("NEXIS workspace"));
		QCOMPARE(known.kelpieId, KelpieId(42));
		QCOMPARE(known.scanStamp.volumeIdentifier, file.volumeIdentifier);
		const auto unknown = mediaEngineMediaFile(file, scan);
		QVERIFY(unknown.volumePath.isEmpty());
		QVERIFY(unknown.volumeName.isEmpty());
		QCOMPARE(unknown.mediaFilePath, file.path);
	}
};

QTEST_GUILESS_MAIN(TestMediaEngineScan)
#include "tst_mediaenginescan.moc"
