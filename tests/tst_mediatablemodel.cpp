// Table row notifications, display values, metadata preparation and transfer safety.

#include "mediaengineadapter.h"
#include "mediaengine/projection.h"
#include "mediaengine/scanmodel.h"
#include "mediaengine/scancoordinator.h"
#include "enumutil.h"
#include "mediafile.h"
#include "mediacsv.h"
#include "mediatablemodel.h"
#include "opscanreceipt.h"
#include "testmediafile.h"
#include "mobid.h"
#include "volumeidentity.h"

#include <QAbstractItemModelTester>
#include <QDateTime>
#include <QPersistentModelIndex>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <initializer_list>

namespace
{
	QString masterId()
	{
		return MobId::format(QByteArray::fromHex(
			"060a2b340101010501010f1013000000443322116655887799aabbccddeeff00"));
	}

	MediaFile row(const QString &path = QStringLiteral("/media/clip.mxf"))
	{
		MediaFile file;
		file.mediaFilePath = path;
		file.masterMobId = masterId();
		return file;
	}

	void record(MediaEvidence &evidence, const QString &path, MediaProperty field, MetadataSource source, const QString &property, const QVariant &value)
	{
		auto snapshot = QSharedPointer<SourceSnapshot>::create();
		snapshot->source = source;
		snapshot->path = source == MetadataSource::Mdb ? QStringLiteral("/media/msmMMOB.mdb") : path;
		snapshot->readState = SourceReadState::Complete;
		MetadataObservation observation;
		observation.snapshot = snapshot;
		observation.property = property;
		observation.objectIdentity = QStringLiteral("object:101");
		observation.value = value;
		observation.rawValue = value.metaType().id() == QMetaType::QString ? QVariant(value.toString().toUtf8()) : value;
		observation.readState = PropertyReadState::Present;
		evidence.observe(field, observation);
	}

	MediaEvidence mediaEngineEvidence(const QString &path)
	{
		MediaEvidence evidence;
		record(evidence, path, MediaProperty::MasterMobId, MetadataSource::Mdb, QStringLiteral("OMFI:MOBJ:MobID"), masterId());
		record(evidence, path, MediaProperty::Type, MetadataSource::Mdb, QStringLiteral("OMFI:MOBJ:UsageCode"), int(MediaFile::Type::Precompute));
		record(evidence, path, MediaProperty::DatabaseStatus, MetadataSource::Filesystem, QStringLiteral("PMR membership"), int(MediaFile::DbStatus::NoReference));
		return evidence;
	}
}

class TestMediaTableModel : public QObject
{
	Q_OBJECT
private slots:
	void row_removal_preserves_rows_and_notifications_data();
	void row_removal_preserves_rows_and_notifications();

	// An unknown creation date must display blank — never silently
	// substituted with another timestamp (the modified-time fallback was
	// removed 2026-07: an unknown coerced to a different fact is a wrong
	// value wearing a confident face).
	void unknown_created_date_displays_blank();
	void physical_row_ids_survive_moves_and_separate_copies_data();
	void physical_row_ids_survive_moves_and_separate_copies();
	void omf_gate_preserves_optional_column_indexes();

	// The Location column shows the whole path; View ▸ Resize Columns to
	// Fit (Cmd+T) is what makes it readable, so nothing here may quietly
	// shorten the value.
	void location_cell_shows_the_full_path();
	void source_path_cell_preserves_recorded_value_data();
	void source_path_cell_preserves_recorded_value();

	// The attribution setters own the flag+label pairing; a half-set
	// state (label without flag, or stale flags after a transition)
	// must be impossible. Labels asserted as raw literals on purpose —
	// the rename tripwire.
	void status_words_come_from_one_table();
	void database_columns_show_the_compact_result_data();
	void database_columns_show_the_compact_result();
	void unknown_classification_displays_without_guessing();
	void precomputes_gate_preserves_rows_and_existing_indexes();
	void effect_columns_only_display_precompute_details();
	void precompute_categories_and_unknown_effects_display_consistently();

	// Metadata preparation boundaries; the ownership migration retains these checks.
	void prepared_metadata_reaches_table_cells();
	void selected_master_and_source_status_reach_table();
	void prepared_scan_load_releases_history_and_preserves_results();

private:
	/// Build `n` MediaFiles with sequential filePaths, nothing else.
	static QVector<MediaFile> makeRows(int n);
	/// filePaths from the model in current order.
	static QStringList pathsOf(const MediaTableModel &m);
};

QVector<MediaFile> TestMediaTableModel::makeRows(int n)
{
	QVector<MediaFile> v;
	v.reserve(n);
	for (int i = 0; i < n; ++i)
	{
		MediaFile mf;
		mf.mediaFilePath = QStringLiteral("/fake/row%1.mxf").arg(i);
		v.push_back(std::move(mf));
	}
	return v;
}

QStringList TestMediaTableModel::pathsOf(const MediaTableModel &m)
{
	QStringList paths;
	const auto &all = m.allFiles();
	paths.reserve(all.size());
	for (const auto &mf : all)
		paths << mf.mediaFilePath;
	return paths;
}

void TestMediaTableModel::row_removal_preserves_rows_and_notifications_data()
{
	QTest::addColumn<int>("initialCount");
	QTest::addColumn<QStringList>("removePaths");
	QTest::addColumn<QList<int>>("ranges"); // Consecutive first/last pairs, in notification order.
	QTest::addColumn<QStringList>("remainingPaths");
	auto paths = [](std::initializer_list<int> rows)
	{
		QStringList result;
		for (int row : rows)
			result.append(QStringLiteral("/fake/row%1.mxf").arg(row));
		return result;
	};
	QTest::newRow("empty_paths_emits_nothing")
		<< 5 << QStringList{} << QList<int>{} << paths({0, 1, 2, 3, 4});
	QTest::newRow("empty_model_emits_nothing")
		<< 0 << paths({0}) << QList<int>{} << QStringList{};
	QTest::newRow("unknown_paths_emit_nothing")
		<< 3 << QStringList{QStringLiteral("/nope/missing.mxf")} << QList<int>{} << paths({0, 1, 2});
	QTest::newRow("single_row_emits_one_range")
		<< 5 << paths({2}) << QList<int>{2, 2} << paths({0, 1, 3, 4});
	QTest::newRow("contiguous_block_emits_one_range")
		<< 5 << paths({1, 2, 3}) << QList<int>{1, 3} << paths({0, 4});
	QTest::newRow("leading_block_emits_one_range")
		<< 5 << paths({0, 1}) << QList<int>{0, 1} << paths({2, 3, 4});
	QTest::newRow("trailing_block_emits_one_range")
		<< 5 << paths({3, 4}) << QList<int>{3, 4} << paths({0, 1, 2});
	QTest::newRow("remove_all_emits_one_range")
		<< 4 << paths({0, 1, 2, 3}) << QList<int>{0, 3} << QStringList{};
	QTest::newRow("two_separated_rows_emit_two_ranges")
		<< 5 << paths({1, 3}) << QList<int>{3, 3, 1, 1} << paths({0, 2, 4});
	QTest::newRow("three_blocks_emit_three_ranges")
		<< 10 << paths({1, 2, 5, 7, 8}) << QList<int>{7, 8, 5, 5, 1, 2} << paths({0, 3, 4, 6, 9});
}

void TestMediaTableModel::row_removal_preserves_rows_and_notifications()
{
	QFETCH(int, initialCount);
	QFETCH(QStringList, removePaths);
	QFETCH(QList<int>, ranges);
	QFETCH(QStringList, remainingPaths);
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	model.setMediaFiles(TestMediaFile::seeded(makeRows(initialCount)));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);
	QSignalSpy doneSpy(&model, &QAbstractItemModel::rowsRemoved);

	model.removeFilesByPath(QSet<QString>(removePaths.cbegin(), removePaths.cend()));

	QCOMPARE(aboutSpy.size(), ranges.size() / 2);
	QCOMPARE(doneSpy.size(), ranges.size() / 2);
	for (qsizetype i = 0; i < ranges.size() / 2; ++i)
	{
		QCOMPARE(aboutSpy.at(i).at(1).toInt(), ranges.at(2 * i));
		QCOMPARE(aboutSpy.at(i).at(2).toInt(), ranges.at(2 * i + 1));
		QCOMPARE(doneSpy.at(i).at(1).toInt(), ranges.at(2 * i));
		QCOMPARE(doneSpy.at(i).at(2).toInt(), ranges.at(2 * i + 1));
	}
	QCOMPARE(model.rowCount(), remainingPaths.size());
	QCOMPARE(pathsOf(model), remainingPaths);
}

void TestMediaTableModel::location_cell_shows_the_full_path()
{
	MediaFile f;
	f.fileName = QStringLiteral("V01.abc.mxf");
	f.volumeName = QStringLiteral("EDIT");
	f.mediaFolderName = QStringLiteral("8646");
	f.mediaFilePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/8646/V01.abc.mxf");

	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({f}));
	const QModelIndex idx =
		model.index(0, Enum::to_underlying(MediaTableModel::Column::Location));

	QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), f.mediaFilePath);
	QCOMPARE(model.headerData(Enum::to_underlying(MediaTableModel::Column::Location),
							  Qt::Horizontal, Qt::DisplayRole)
				 .toString(),
			 QStringLiteral("Location"));
}

void TestMediaTableModel::source_path_cell_preserves_recorded_value_data()
{
	QTest::addColumn<QString>("sourcePath");
	QTest::newRow("posix") << QStringLiteral("/Recorded Imports/Camera A/interview.mov");
	QTest::newRow("windows") << QStringLiteral("C:\\Recorded Imports\\Camera A\\interview.mov");
	QTest::newRow("url") << QStringLiteral("file:///Recorded%20Imports/Camera%20A/interview.mov");
	QTest::newRow("unavailable") << QString{};
}

void TestMediaTableModel::source_path_cell_preserves_recorded_value()
{
	QFETCH(QString, sourcePath);
	auto file = row();
	file.fileName = QStringLiteral("managed.mxf");
	file.sourceFilePath = sourcePath;
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({file}));
	const int column = int(MediaTableModel::Column::SourcePath);
	const QPersistentModelIndex source(model.index(0, column));
	for (const bool precomputes : {false, true, false})
	{
		model.setPrecomputesEnabled(precomputes);
		QVERIFY(source.isValid());
		QCOMPARE(source.column(), column);
		QCOMPARE(source.data().toString(), sourcePath);
		QCOMPARE(source.data(Qt::ToolTipRole).toString(), sourcePath);
		QCOMPARE(source.data(Qt::UserRole).toString(), sourcePath);
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("Source Path"));
		QCOMPARE(model.index(0, int(MediaTableModel::Column::FileName)).data().toString(), file.fileName);
		QCOMPARE(model.index(0, int(MediaTableModel::Column::Location)).data().toString(), file.mediaFilePath);
	}
}

void TestMediaTableModel::physical_row_ids_survive_moves_and_separate_copies_data()
{
	QTest::addColumn<bool>("headerSkipped");
	QTest::newRow("header-backed") << false;
	QTest::newRow("database-backed-skipped-header") << true;
}

void TestMediaTableModel::physical_row_ids_survive_moves_and_separate_copies()
{
	QFETCH(bool, headerSkipped);
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto put = [&](const QString &name)
	{
		const QString path = temp.filePath(name);
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write("same bytes") != 10)
			return QString{};
		return path;
	};
	const QString original = put("original.mxf");
	const QString copy = put("copy.mxf");
	const QString moved = put("moved.mxf");
	QVERIFY(!original.isEmpty() && !copy.isEmpty() && !moved.isEmpty());
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	// Authored transfer controls retain the same selected fields through either receipt route.
	auto originalRow = row(original);
	originalRow.compression = QStringLiteral("Avid DNx HQX");
	originalRow.bitDepth = QStringLiteral("10-bit");
	originalRow.sourceFilePath = QStringLiteral("file:///Recorded%20Imports/original.mov");
	originalRow.fileMobId = masterId();
	originalRow.scanStamp = {original, VolumeIdentity::capture(original).identifier(),
		QFileInfo(original).lastModified(), originalRow.fileMobId, originalRow.masterMobId};
	QWeakPointer<MediaEngine::ScanResult> oldScan;
	QWeakPointer<const SourceSnapshot> oldSource;
	{
		MediaEvidence evidence;
		auto snapshot = QSharedPointer<SourceSnapshot>::create();
		snapshot->source = headerSkipped ? MetadataSource::Mdb : MetadataSource::Mxf;
		snapshot->path = headerSkipped ? temp.filePath("index.mdb") : original;
		oldSource = snapshot;
		const auto observe = [&](MediaProperty property, const QVariant &value, const SourceSnapshotRef &receipt)
		{
			MetadataObservation item;
			item.snapshot = receipt;
			item.property = QStringLiteral("Authored field");
			item.value = value;
			item.readState = PropertyReadState::Present;
			evidence.observe(property, item);
		};
		observe(MediaProperty::Compression, originalRow.compression, snapshot);
		observe(MediaProperty::BitDepth, originalRow.bitDepth, snapshot);
		observe(MediaProperty::SourcePath, originalRow.sourceFilePath, snapshot);
		observe(MediaProperty::FileMobId, originalRow.fileMobId, snapshot);
		observe(MediaProperty::MasterMobId, originalRow.masterMobId, snapshot);
		auto filesystem = QSharedPointer<SourceSnapshot>::create();
		filesystem->source = MetadataSource::Filesystem;
		filesystem->path = original;
		observe(MediaProperty::Location, original, filesystem);
		observe(MediaProperty::DatabaseStatus, int(MediaFile::DbStatus::Listed), filesystem);
		MediaEngine::selectMetadata(evidence);
		auto scan = QSharedPointer<MediaEngine::ScanResult>::create();
		oldScan = scan;
		MediaEngine::SourceReceipt mediaSource;
		mediaSource.outcome = headerSkipped ? MediaEngine::ParsedSource::Outcome::NotRead
			: MediaEngine::ParsedSource::Outcome::Complete;
		mediaSource.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, original, originalRow.scanStamp.modified,
			headerSkipped ? SourceReadState::NotRead : SourceReadState::Complete});
		scan->sources.append(mediaSource);
		const ScanRowLookup lookup(*scan);
		applyResolvedMetadata(originalRow, evidence);
		originalRow.metadataStatus = makeMediaMetadataStatus(evidence, lookup.headerOutcome(original));
		originalRow.operationScanReceipt = makeOpScanReceipt(evidence, originalRow.scanStamp, lookup);
	}
	QVERIFY(oldScan.isNull());
	QVERIFY(oldSource.isNull());
	QVERIFY(originalRow.operationScanReceipt);
	const auto checks = *originalRow.operationScanReceipt;
	QCOMPARE(checks.headerIdentity.fileMobId, headerSkipped ? QString{} : originalRow.fileMobId);
	QCOMPARE(checks.headerIdentity.masterMobIds, headerSkipped ? QStringList{} : QStringList{masterId()});
	QCOMPARE(checks.databaseFileMobIdToVerify, headerSkipped ? originalRow.fileMobId : QString{});
	model.setMediaFiles({originalRow});
	const KelpieId originalId = model.allFiles().first().kelpieId;
	QVERIFY(originalId != 0);
	const QPersistentModelIndex noDatabase(model.index(0, int(MediaTableModel::Column::NoDatabase)));
	const QPersistentModelIndex noReference(model.index(0, int(MediaTableModel::Column::NoReference)));
	QCOMPARE(noDatabase.data().toString(), QStringLiteral("false"));
	QCOMPARE(noReference.data().toString(), QStringLiteral("false"));
	model.applyTransfer(original, copy, true);
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(model.allFiles()[0].kelpieId, originalId);
	QVERIFY(model.allFiles()[1].kelpieId != originalId);
	QCOMPARE(model.allFiles()[0].masterMobId, model.allFiles()[1].masterMobId);
	QCOMPARE(model.allFiles()[0].mediaFilePath, original);
	QCOMPARE(model.allFiles()[1].dbStatus, MediaFile::DbStatus::NoDatabase);
	QCOMPARE(model.index(1, int(MediaTableModel::Column::NoDatabase)).data().toString(), QStringLiteral("true"));
	QCOMPARE(model.index(1, int(MediaTableModel::Column::NoReference)).data().toString(), QStringLiteral("false"));
	QVERIFY(!put("future.pmr").isEmpty()); // Presence cannot prove membership without parsing this database.
	model.applyTransfer(original, moved, false);
	QCOMPARE(model.allFiles()[0].dbStatus, MediaFile::DbStatus::DbUnreadable);
	QVERIFY(noDatabase.isValid());
	QVERIFY(noReference.isValid());
	QCOMPARE(noDatabase.data().toString(), QStringLiteral("true"));
	QCOMPARE(noReference.data().toString(), QStringLiteral("false"));
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(model.allFiles()[0].kelpieId, originalId);
	QCOMPARE(model.allFiles()[0].scanStamp.path, moved);
	QCOMPARE(model.allFiles()[0].mediaFilePath, moved);
	for (const auto &file : model.allFiles())
	{
		QCOMPARE(file.compression, originalRow.compression);
		QCOMPARE(file.bitDepth, originalRow.bitDepth);
		QCOMPARE(file.sourceFilePath, originalRow.sourceFilePath);
		QCOMPARE(file.fileMobId, originalRow.fileMobId);
		QCOMPARE(file.masterMobId, originalRow.masterMobId);
		QCOMPARE(file.scanStamp.masterMobId, originalRow.masterMobId);
		for (const auto property : {MediaProperty::Compression, MediaProperty::BitDepth,
			MediaProperty::FileMobId, MediaProperty::MasterMobId, MediaProperty::SourcePath})
		{
			const auto &status = file.metadataStatus.field(property);
			const auto &before = originalRow.metadataStatus.field(property);
			QCOMPARE(status.readState, before.readState);
			QCOMPARE(status.agreement, before.agreement);
			QVERIFY(status.selectedSource == before.selectedSource);
			QVERIFY(status.selectedBasis == before.selectedBasis);
		}
		const auto &location = file.metadataStatus.field(MediaProperty::Location);
		QVERIFY(location.hasSelectedValue);
		QVERIFY(location.selectedSource == MetadataSource::Filesystem);
		const auto &membership = file.metadataStatus.field(MediaProperty::DatabaseStatus);
		QVERIFY(membership.hasSelectedValue);
		QVERIFY(membership.selectedBasis == EvidenceBasis::Derived);
		QCOMPARE(file.metadataStatus.headerOutcome, originalRow.metadataStatus.headerOutcome);
		QVERIFY(oldScan.isNull());
		QVERIFY(oldSource.isNull());
		QVERIFY(file.operationScanReceipt);
		QCOMPARE(file.operationScanReceipt->headerIdentity.fileMobId, checks.headerIdentity.fileMobId);
		QCOMPARE(file.operationScanReceipt->headerIdentity.masterMobIds, checks.headerIdentity.masterMobIds);
		QCOMPARE(file.operationScanReceipt->headerIdentity.unavailableReason, checks.headerIdentity.unavailableReason);
		QCOMPARE(file.operationScanReceipt->databaseFileMobIdToVerify, checks.databaseFileMobIdToVerify);
		QCOMPARE(file.scanStamp.path, file.mediaFilePath);
		QCOMPARE(file.scanStamp.modified, file.modified);
		QCOMPARE(file.scanStamp.volumeIdentifier, VolumeIdentity::capture(file.mediaFilePath).identifier());
		// A later operation uses the frozen checks after all temporary owners expired.
		const auto item = opItemFromMediaFile(file);
		QVERIFY(item.headerIdentity);
		QCOMPARE(item.headerIdentity->fileMobId, checks.headerIdentity.fileMobId);
		QCOMPARE(item.headerIdentity->masterMobIds, checks.headerIdentity.masterMobIds);
		QCOMPARE(item.headerIdentity->unavailableReason, checks.headerIdentity.unavailableReason);
		QCOMPARE(item.databaseFileMobIdToVerify, checks.databaseFileMobIdToVerify);
		QCOMPARE(item.scanPath, file.mediaFilePath);
		QCOMPARE(item.scanVolumeIdentifier, file.scanStamp.volumeIdentifier);
		QCOMPARE(item.modifiedMs, file.modified.toMSecsSinceEpoch());
	}
	model.setMediaFiles(TestMediaFile::seeded({row(copy)}));
	QCOMPARE(model.allFiles()[0].kelpieId, KelpieId(1));
}

void TestMediaTableModel::omf_gate_preserves_optional_column_indexes()
{
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	MediaFile file = row();
	file.omfEra = true;
	file.duration = {19, {25, 1}, {25, 1}, MediaDuration::Source::Descriptor};
	file.timecodeBase = 25;
	model.setMediaFiles(TestMediaFile::seeded({file}));
	const QPersistentModelIndex duration(model.index(0, int(MediaTableModel::Column::Duration)));
	for (bool omf : {true, false, true})
	{
		model.setOmfScanEnabled(omf);
		const QPersistentModelIndex family = omf ? model.index(0, model.omfScanColumn()) : QModelIndex{};
		for (bool precompute : {true, false, true})
		{
			model.setPrecomputesEnabled(precompute);
			QCOMPARE(model.columnCount(), 21 + int(omf) + 4 * int(precompute));
			QVERIFY(duration.isValid());
			QCOMPARE(duration.column(), int(MediaTableModel::Column::Duration));
			QCOMPARE(duration.data().toString(), QStringLiteral("00:00:00:19"));
			if (omf)
			{
				QVERIFY(family.isValid());
				QCOMPARE(family.column(), model.omfScanColumn());
				QCOMPARE(model.headerData(family.column(), Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("OmfScan"));
				QCOMPARE(family.data().toString(), QStringLiteral("true"));
			}
		}
	}
}

void TestMediaTableModel::unknown_created_date_displays_blank()
{
	MediaFile withDate;
	withDate.mediaFilePath = QStringLiteral("/vol/a.mxf");
	withDate.created = QDateTime(QDate(2026, 7, 20), QTime(12, 30));

	MediaFile withoutDate;
	withoutDate.mediaFilePath = QStringLiteral("/vol/b.mxf");
	// created left invalid — a filesystem that records no birth time.

	MediaTableModel m;
	m.setMediaFiles(TestMediaFile::seeded({withDate, withoutDate}));
	const int col = int(MediaTableModel::Column::Created);

	QCOMPARE(m.index(0, col).data(Qt::DisplayRole).toString(),
			 QStringLiteral("2026-07-20 12:30"));
	QVERIFY(m.index(1, col).data(Qt::DisplayRole).toString().isEmpty());

	// The sort role hands back the raw (possibly invalid) datetime;
	// invalid values sort before valid ones, so blanks group together.
	QVERIFY(!m.index(1, col).data(Qt::UserRole).toDateTime().isValid());
}

// The database status is one enum value and its words live in one table; the
// Project tooltip and filter tabs share those explanations. This
// pins the table: every status has a label, the two couldn't-check states
// share the one "No Database" label but explain themselves differently, and
// "No project" is a separate fact (an empty project) with its own sentence.
void TestMediaTableModel::status_words_come_from_one_table()
{
	using DbStatus = MediaFile::DbStatus;
	for (DbStatus s : {DbStatus::Listed, DbStatus::NoReference, DbStatus::NoDatabase, DbStatus::DbUnreadable})
	{
		const auto text = MediaFile::dbStatusText(s);
		QVERIFY2(!text.label.isEmpty(), "every status has a label for its explanation");
		QCOMPARE(text.why.isEmpty(), s == DbStatus::Listed); // only Listed needs no explanation
	}
	QCOMPARE(MediaFile::dbStatusText(DbStatus::NoDatabase).label,
			 MediaFile::dbStatusText(DbStatus::DbUnreadable).label); // one tab
	QVERIFY(MediaFile::dbStatusText(DbStatus::NoDatabase).why !=
			MediaFile::dbStatusText(DbStatus::DbUnreadable).why); // two reasons
	QCOMPARE(MediaFile::dbStatusText(DbStatus::NoReference).label, QStringLiteral("No Reference"));

	MediaFile f;
	QCOMPARE(f.dbStatus, DbStatus::Listed);
	QVERIFY(!f.isNoDatabase());
	f.dbStatus = DbStatus::DbUnreadable;
	QVERIFY(f.isNoDatabase());
	f.dbStatus = DbStatus::NoDatabase;
	QVERIFY(f.isNoDatabase());

	// The project is independent of the status: empty means "No project"
	// wherever it is shown, and a named project never reads as a state.
	QVERIFY(f.hasNoProject());
	QCOMPARE(f.projectDisplay(), QStringLiteral("No project"));
	f.project = QStringLiteral("Tëst");
	QVERIFY(!f.hasNoProject());
	QCOMPARE(f.projectDisplay(), QStringLiteral("Tëst"));

	// The model shows projectDisplay() and explains the row in its tooltip.
	MediaTableModel m;
	MediaFile unlisted;
	unlisted.dbStatus = DbStatus::NoReference;
	m.setMediaFiles(TestMediaFile::seeded({unlisted}));
	const int col = int(MediaTableModel::Column::Project);
	QCOMPARE(m.index(0, col).data(Qt::DisplayRole).toString(), QStringLiteral("No project"));
	const QString tip = m.index(0, col).data(Qt::ToolTipRole).toString();
	QVERIFY(tip.contains(MediaFile::noProjectWhy()));
	QVERIFY(tip.contains(MediaFile::dbStatusText(DbStatus::NoReference).why));
}

void TestMediaTableModel::database_columns_show_the_compact_result_data()
{
	QTest::addColumn<MediaFile::DbStatus>("status");
	QTest::addColumn<bool>("noDatabase");
	QTest::addColumn<bool>("noReference");
	QTest::newRow("listed") << MediaFile::DbStatus::Listed << false << false;
	QTest::newRow("checked-missing-reference") << MediaFile::DbStatus::NoReference << false << true;
	QTest::newRow("no-local-databases") << MediaFile::DbStatus::NoDatabase << true << false;
	QTest::newRow("unresolved-local-checks") << MediaFile::DbStatus::DbUnreadable << true << false;
}

void TestMediaTableModel::database_columns_show_the_compact_result()
{
	QFETCH(MediaFile::DbStatus, status);
	QFETCH(bool, noDatabase);
	QFETCH(bool, noReference);
	MediaFile file = row();
	file.dbStatus = status;
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	model.setMediaFiles({file});
	const int databaseColumn = int(MediaTableModel::Column::NoDatabase);
	const int referenceColumn = int(MediaTableModel::Column::NoReference);
	QCOMPARE(databaseColumn, int(MediaTableModel::Column::Location) + 1);
	QCOMPARE(referenceColumn, databaseColumn + 1);
	QCOMPARE(int(MediaTableModel::Column::FileMobId), referenceColumn + 1);
	const QPersistentModelIndex database(model.index(0, databaseColumn));
	const QPersistentModelIndex reference(model.index(0, referenceColumn));
	for (const bool precomputes : {false, true, false})
	{
		model.setPrecomputesEnabled(precomputes);
		for (const bool omf : {false, true, false})
		{
			model.setOmfScanEnabled(omf);
			QVERIFY(database.isValid());
			QVERIFY(reference.isValid());
			QCOMPARE(database.data().toString(), noDatabase ? QStringLiteral("true") : QStringLiteral("false"));
			QCOMPARE(reference.data().toString(), noReference ? QStringLiteral("true") : QStringLiteral("false"));
			QCOMPARE(database.data(Qt::UserRole).metaType().id(), int(QMetaType::Bool));
			QCOMPARE(reference.data(Qt::UserRole).metaType().id(), int(QMetaType::Bool));
			QCOMPARE(database.data(Qt::UserRole).toBool(), noDatabase);
			QCOMPARE(reference.data(Qt::UserRole).toBool(), noReference);
			QCOMPARE(model.headerData(databaseColumn, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("No Database"));
			QCOMPARE(model.headerData(referenceColumn, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("No Reference"));
			const QString databaseTip = database.data(Qt::ToolTipRole).toString();
			const QString referenceTip = reference.data(Qt::ToolTipRole).toString();
			QVERIFY(!databaseTip.isEmpty());
			QVERIFY(!referenceTip.isEmpty());
			if (status != MediaFile::DbStatus::Listed)
			{
				QVERIFY(databaseTip.contains(file.dbStatusText().why));
				QVERIFY(referenceTip.contains(file.dbStatusText().why));
			}
			if (noDatabase)
				QVERIFY(referenceTip.contains(QStringLiteral("absence was not established")));
		}
	}
}

void TestMediaTableModel::unknown_classification_displays_without_guessing()
{
	// Known numeric values are stable for existing consumers.
	static_assert(int(MediaFile::Kind::Video) == 0 && int(MediaFile::Kind::Audio) == 1);
	static_assert(int(MediaFile::Type::Media) == 0 && int(MediaFile::Type::Precompute) == 1);
	MediaFile unknown;
	QCOMPARE(unknown.kind, MediaFile::Kind::Unknown);
	QCOMPARE(unknown.type, MediaFile::Type::Unknown);

	MediaFile video;
	video.kind = MediaFile::Kind::Video;
	video.type = MediaFile::Type::Media;
	MediaFile audio;
	audio.kind = MediaFile::Kind::Audio;
	audio.type = MediaFile::Type::Precompute;
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({unknown, video, audio}));
	const int kind = int(MediaTableModel::Column::Kind);
	const int type = int(MediaTableModel::Column::Type);
	QCOMPARE(model.index(0, kind).data().toString(), QString{});
	QCOMPARE(model.index(0, type).data().toString(), QStringLiteral("\u2014"));
	QVERIFY(!model.index(0, kind).data(Qt::ToolTipRole).toString().isEmpty());
	QVERIFY(!model.index(0, type).data(Qt::ToolTipRole).toString().isEmpty());
	QCOMPARE(model.index(1, kind).data().toString(), QStringLiteral("Video"));
	QCOMPARE(model.index(1, type).data().toString(), QStringLiteral("Media"));
	QCOMPARE(model.index(2, kind).data().toString(), QStringLiteral("Audio"));
	QCOMPARE(model.index(2, type).data().toString(), QStringLiteral("Precompute"));
}

void TestMediaTableModel::precomputes_gate_preserves_rows_and_existing_indexes()
{
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	model.setMediaFiles(TestMediaFile::seeded(makeRows(2)));
	const QStringList baseHeaders{
		QStringLiteral("Clip Name"), QStringLiteral("Project"), QStringLiteral("Bin"),
		QStringLiteral("Kind"), QStringLiteral("Duration"), QStringLiteral("Size (MB)"),
		QStringLiteral("Compression"), QStringLiteral("Resolution"), QStringLiteral("Frame Rate"),
		QStringLiteral("Sample Rate"), QStringLiteral("Bit Depth"), QStringLiteral("Type"),
		QStringLiteral("Date Created"), QStringLiteral("Filename"), QStringLiteral("Source Path"),
		QStringLiteral("Location"), QStringLiteral("No Database"), QStringLiteral("No Reference"),
		QStringLiteral("FileMobId"), QStringLiteral("MasterMobId"), QStringLiteral("KelpieId")};
	const QPersistentModelIndex row(model.index(1, int(MediaTableModel::Column::Location)));
	const QString path = row.data().toString();
	QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
	QSignalSpy inserted(&model, &QAbstractItemModel::columnsInserted);
	QSignalSpy removed(&model, &QAbstractItemModel::columnsRemoved);
	QVERIFY(!model.precomputesEnabled());
	QCOMPARE(model.columnCount(), 21);
	for (int column = 0; column < baseHeaders.size(); ++column)
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), baseHeaders[column]);
	QVERIFY(!model.index(0, int(MediaTableModel::Column::Effect)).isValid());
	QVERIFY(!model.headerData(int(MediaTableModel::Column::Effect), Qt::Horizontal, Qt::DisplayRole).isValid());
	model.setPrecomputesEnabled(true);
	QCOMPARE(model.columnCount(), 25);
	for (int column = 0; column < baseHeaders.size(); ++column)
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), baseHeaders[column]);
	QCOMPARE(inserted.size(), 1);
	QCOMPARE(inserted.first().at(1).toInt(), 21);
	QCOMPARE(inserted.first().at(2).toInt(), 24);
	QVERIFY(row.isValid());
	QCOMPARE(row.data().toString(), path);
	const QPersistentModelIndex effect(model.index(1, int(MediaTableModel::Column::Effect)));
	model.setPrecomputesEnabled(true);
	QCOMPARE(inserted.size(), 1);
	model.setPrecomputesEnabled(false);
	model.setPrecomputesEnabled(false);
	QCOMPARE(model.columnCount(), 21);
	QCOMPARE(removed.size(), 1);
	QCOMPARE(reset.size(), 0);
	QCOMPARE(model.rowCount(), 2);
	QVERIFY(row.isValid());
	QCOMPARE(row.data().toString(), path);
	QVERIFY(!effect.isValid());
}

void TestMediaTableModel::effect_columns_only_display_precompute_details()
{
	MediaFile precompute;
	precompute.type = MediaFile::Type::Precompute;
	precompute.clipName = QStringLiteral("Sequence,3D_Warp+1");
	MediaFile media = precompute;
	media.type = MediaFile::Type::Media;
	MediaFile unknown = precompute;
	unknown.type = MediaFile::Type::Unknown;
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({precompute, media, unknown}));
	model.setPrecomputesEnabled(true);
	const QStringList headers{QStringLiteral("Precompute Category"), QStringLiteral("Effect Category"), QStringLiteral("Effect"), QStringLiteral("Effect Sequence")};
	const QStringList values{QStringLiteral("unknown"), QStringLiteral("Blend"), QStringLiteral("3D Warp"), QStringLiteral("Sequence")};
	for (int i = 0; i < values.size(); ++i)
	{
		const int column = int(MediaTableModel::Column::PrecomputeCategory) + i;
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), headers[i]);
		QCOMPARE(model.index(0, column).data().toString(), values[i]);
		QVERIFY(model.index(1, column).data().toString().isEmpty());
		QVERIFY(model.index(2, column).data().toString().isEmpty());
	}
	model.setPrecomputesEnabled(false);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Type)).data().toString(), QStringLiteral("Precompute"));
}

void TestMediaTableModel::precompute_categories_and_unknown_effects_display_consistently()
{
	MediaFile rendered, title, unknown;
	rendered.type = title.type = unknown.type = MediaFile::Type::Precompute;
	rendered.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	title.precomputeCategory = MediaFile::PrecomputeCategory::TitlesAndMatteKeys;
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({rendered, title, unknown, MediaFile{}}));
	model.setPrecomputesEnabled(true);
	const int category = int(MediaTableModel::Column::PrecomputeCategory);
	QCOMPARE(model.index(0, category).data().toString(), QStringLiteral("Rendered Effects"));
	QCOMPARE(model.index(1, category).data().toString(), QStringLiteral("Titles and Matte Keys"));
	QCOMPARE(model.index(2, category).data().toString(), QStringLiteral("unknown"));
	for (auto column : {MediaTableModel::Column::Effect, MediaTableModel::Column::EffectCategory})
		for (int row = 0; row < 3; ++row)
			QCOMPARE(model.index(row, int(column)).data().toString(), QStringLiteral("unknown"));
	QVERIFY(model.index(3, category).data().toString().isEmpty());
}

void TestMediaTableModel::prepared_metadata_reaches_table_cells()
{
	auto file = row();
	auto evidence = mediaEngineEvidence(file.mediaFilePath);
	record(evidence, file.mediaFilePath, MediaProperty::ClipName, MetadataSource::Mxf, QStringLiteral("GenericPackage.Name"), QStringLiteral("Sequence,3D_Warp+1"));
	record(evidence, file.mediaFilePath, MediaProperty::OriginalBin, MetadataSource::Mdb, QStringLiteral("_ORG_BIN"), QStringLiteral("MDB bin"));
	MediaEngine::selectMetadata(evidence);
	file.type = MediaFile::Type::Media;
	file.dbStatus = MediaFile::DbStatus::NoDatabase;
	file.clipName = QStringLiteral("Stale clip");
	file.originalBin = QStringLiteral("Stale bin");
	file.effect = QStringLiteral("Stale effect");
	file.effectCategory = QStringLiteral("Stale category");
	file.effectSequence = QStringLiteral("Stale sequence");
	// Preparation happens before insertion; the model displays the supplied result.
	applyResolvedMetadata(file, evidence);
	file.metadataStatus = makeMediaMetadataStatus(evidence);
	MediaTableModel model;
	model.setPrecomputesEnabled(true);
	model.setMediaFiles({file});
	const auto &current = model.fileAt(0);
	QCOMPARE(current.type, MediaFile::Type::Precompute);
	QCOMPARE(current.dbStatus, MediaFile::DbStatus::NoReference);
	QCOMPARE(current.clipName, QStringLiteral("Sequence,3D_Warp+1"));
	QCOMPARE(current.originalBin, QStringLiteral("MDB bin"));
	QCOMPARE(current.effect, QStringLiteral("3D Warp"));
	QCOMPARE(current.effectCategory, QStringLiteral("Blend"));
	QCOMPARE(current.effectSequence, QStringLiteral("Sequence"));
	QCOMPARE(model.index(0, int(MediaTableModel::Column::ClipName)).data().toString(), current.clipName);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::OriginalBin)).data().toString(), current.originalBin);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Type)).data().toString(), QStringLiteral("Precompute"));
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Effect)).data().toString(), current.effect);
	QVERIFY(current.metadataStatus.field(MediaProperty::ClipName).selectedSource == MetadataSource::Mxf);
	QVERIFY(current.metadataStatus.field(MediaProperty::OriginalBin).selectedSource == MetadataSource::Mdb);
	for (const auto property : {MediaProperty::ClipName, MediaProperty::OriginalBin})
		QVERIFY(!current.metadataStatus.field(property).presentSources.contains(MetadataSource::Avb));
}

void TestMediaTableModel::selected_master_and_source_status_reach_table()
{
	auto file = row();
	MediaEvidence evidence;
	const QString headerMaster = masterId();
	QString databaseMaster = headerMaster;
	databaseMaster[databaseMaster.size() - 1] = QLatin1Char('1');
	record(evidence, file.mediaFilePath, MediaProperty::MasterMobId, MetadataSource::Mdb,
		QStringLiteral("OMFI:MOBJ:MobID"), databaseMaster);
	record(evidence, file.mediaFilePath, MediaProperty::MasterMobId, MetadataSource::Mxf,
		QStringLiteral("MaterialPackage.PackageUID"), headerMaster);
	MediaEngine::selectMetadata(evidence);
	applyResolvedMetadata(file, evidence);
	file.metadataStatus = makeMediaMetadataStatus(evidence);
	evidence = {};
	MediaTableModel model;
	model.setMediaFiles({file});
	const auto &current = model.fileAt(0);
	QCOMPARE(current.masterMobId, headerMaster);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::MasterMobId)).data().toString(), headerMaster);
	const auto &status = current.metadataStatus.field(MediaProperty::MasterMobId);
	QVERIFY(status.captured && status.hasSelectedValue);
	QCOMPARE(status.readState, PropertyReadState::Present);
	QVERIFY(status.selectedSource == MetadataSource::Mxf);
	QVERIFY(status.selectedBasis == EvidenceBasis::Recorded);
	QVERIFY(status.selectedFreshness == SourceFreshness::Unknown);
	QVERIFY(status.presentSources.contains(MetadataSource::Mxf));
	QVERIFY(status.presentSources.contains(MetadataSource::Mdb));
	QVERIFY(MediaCsv::rowLine(current).contains(headerMaster));
	QVERIFY(!MediaCsv::rowLine(current).contains(databaseMaster));
}

void TestMediaTableModel::prepared_scan_load_releases_history_and_preserves_results()
{
	MediaTableModel model;
	model.setPrecomputesEnabled(true);
	QWeakPointer<MediaEngine::ScanResult> oldScan;
	QWeakPointer<const SourceSnapshot> oldSource;
	MediaFile file;
	{
		auto scan = QSharedPointer<MediaEngine::ScanResult>::create();
		oldScan = scan;
		MediaEngine::MediaFile engineFile;
		engineFile.kelpieId = 91;
		engineFile.path = QStringLiteral("/media/clip.mxf");
		engineFile.stamp.path = engineFile.path;
		engineFile.evidence = mediaEngineEvidence(engineFile.path);
		record(engineFile.evidence, engineFile.path, MediaProperty::ClipName, MetadataSource::Mxf,
			QStringLiteral("GenericPackage.Name"), QStringLiteral("Header clip"));
		record(engineFile.evidence, engineFile.path, MediaProperty::OriginalBin, MetadataSource::Mdb,
			QStringLiteral("_ORG_BIN"), QStringLiteral("MDB bin"));
		MediaEngine::selectMetadata(engineFile.evidence);
		oldSource = engineFile.evidence.observations(MediaProperty::ClipName).first().snapshot;
		scan->files.append(engineFile);
		file = mediaEngineMediaFile(scan->files.first(), ScanRowLookup(*scan), QStringLiteral("/media"), QStringLiteral("Test"));
		// An authored label proves insertion displays the prepared row without reselection.
		file.clipName = QStringLiteral("Prepared clip label");
		model.setMediaFiles({file});
	}
	QVERIFY(oldScan.isNull());
	QVERIFY(oldSource.isNull());
	QCOMPARE(model.rowCount(), 1);
	const auto expectedCsv = MediaCsv::rowLine(file);
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	const auto &current = model.fileAt(0);
	QCOMPARE(current.kelpieId, KelpieId(91));
	QCOMPARE(MediaCsv::rowLine(current), expectedCsv);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::ClipName)).data().toString(), QStringLiteral("Prepared clip label"));
	QCOMPARE(current.originalBin, QStringLiteral("MDB bin"));
	for (const auto property : {MediaProperty::ClipName, MediaProperty::OriginalBin})
	{
		const auto &status = current.metadataStatus.field(property);
		const auto &previous = file.metadataStatus.field(property);
		QVERIFY(status.captured);
		QVERIFY(status.hasSelectedValue);
		QCOMPARE(status.readState, PropertyReadState::Present);
		QCOMPARE(status.agreement, previous.agreement);
		QVERIFY(status.selectedSource == previous.selectedSource);
		QVERIFY(status.selectedBasis == previous.selectedBasis);
		QVERIFY(status.selectedFreshness == previous.selectedFreshness);
	}
	QVERIFY(current.operationScanReceipt);
	QVERIFY(opItemFromMediaFile(current).headerIdentity);
	QCOMPARE(changed.size(), 0);
}

QTEST_GUILESS_MAIN(TestMediaTableModel)
#include "tst_mediatablemodel.moc"
