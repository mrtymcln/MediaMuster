// Table row notifications, display values and bin-derived metadata ownership.

#include "avbbinloader.h"
#include "mediaengineadapter.h"
#include "mediaengine/projection.h"
#include "mediaengine/scanmodel.h"
#include "mediaengine/scancoordinator.h"
#include "enumutil.h"
#include "mediafile.h"
#include "mediacsv.h"
#include "mediatablemodel.h"
#include "testmediafile.h"
#include "mobid.h"

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

	AvbBin bin(const QString &name = QStringLiteral("Edited clip"),
			   const QString &originalBin = QStringLiteral("Original rushes"),
			   const QString &uid = QStringLiteral("0000000100000002"))
	{
		AvbBin value;
		value.usable = true;
		value.coverageComplete = true;
		value.filePath = QStringLiteral("/current/renamed-bin.avb");
		value.displayName = QStringLiteral("renamed-bin");
		AvbComposition mob;
		mob.mobId = masterId();
		mob.name = name;
		mob.mobType = AvbComposition::masterMobType;
		mob.originalBinName = originalBin;
		mob.originalBinUid = uid;
		const auto snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Avb, value.filePath, {}, SourceReadState::Complete});
		auto source = QSharedPointer<MediaEngine::ParsedSource>::create();
		source->snapshot = snapshot;
		value.sourceGraph = source;
		const auto observation = [&](const QString &property, const QString &text, const QString &owner)
		{
			MetadataObservation result;
			result.snapshot = snapshot;
			result.property = property;
			result.objectIdentity = owner;
			result.value = text;
			result.rawValue = text.toUtf8();
			result.readState = PropertyReadState::Present;
			return result;
		};
		if (!name.isEmpty())
			mob.nameObservations.append(observation(QStringLiteral("Component.name"), name, QStringLiteral("7")));
		if (!originalBin.isEmpty())
			mob.originalBinObservations.append(observation(QStringLiteral("BinRef.name_utf8"), originalBin, QStringLiteral("12")));
		value.compositions.append(mob);
		return value;
	}

	AvbBin evidenceBin(const QString &name = QStringLiteral("Edited clip"))
	{
		auto value = bin(name);
		auto snapshot = QSharedPointer<SourceSnapshot>::create();
		snapshot->source = MetadataSource::Avb;
		snapshot->path = value.filePath;
		auto source = QSharedPointer<MediaEngine::ParsedSource>::create();
		source->snapshot = snapshot;
		value.sourceGraph = source;
		value.compositions[0].nameObservations.clear();
		value.compositions[0].originalBinObservations.clear();
		auto add = [&](const QString &property, const QString &text, quint64 object, bool eligible)
		{
			MetadataObservation observation;
			observation.snapshot = snapshot;
			observation.property = property;
			observation.objectIdentity = QString::number(object);
			observation.value = text;
			observation.rawValue = text.toUtf8();
			observation.readState = PropertyReadState::Present;
			observation.eligible = eligible;
			return observation;
		};
		value.compositions[0].nameObservations.append(add(QStringLiteral("Component.name"), name, 7, true));
		value.compositions[0].originalBinObservations.append(add(QStringLiteral("BinRef.name"), QStringLiteral("Legacy rushes"), 12, false));
		value.compositions[0].originalBinObservations.append(add(QStringLiteral("BinRef.name_utf8"), value.compositions[0].originalBinName, 12, true));
		return value;
	}

	MediaFile row(const QString &path = QStringLiteral("/media/clip.mxf"))
	{
		MediaFile file;
		file.mediaFilePath = path;
		file.masterMobId = masterId();
		return file;
	}

	void record(MediaFile &file, MediaProperty field, MetadataSource source, const QString &property, const QVariant &value)
	{
		auto snapshot = QSharedPointer<SourceSnapshot>::create();
		snapshot->source = source;
		snapshot->path = source == MetadataSource::Mdb ? QStringLiteral("/media/msmMMOB.mdb") : file.mediaFilePath;
		snapshot->readState = SourceReadState::Complete;
		MetadataObservation observation;
		observation.snapshot = snapshot;
		observation.property = property;
		observation.objectIdentity = QStringLiteral("object:101");
		observation.value = value;
		observation.rawValue = value.metaType().id() == QMetaType::QString ? QVariant(value.toString().toUtf8()) : value;
		observation.readState = PropertyReadState::Present;
		file.evidence.observe(field, observation);
	}

	MediaFile mediaEngineRow()
	{
		auto file = row();
		file.mediaEngineScan = QSharedPointer<MediaEngine::ScanResult>::create();
		record(file, MediaProperty::MasterMobId, MetadataSource::Mdb, QStringLiteral("OMFI:MOBJ:MobID"), masterId());
		record(file, MediaProperty::Type, MetadataSource::Mdb, QStringLiteral("OMFI:MOBJ:UsageCode"), int(MediaFile::Type::Precompute));
		record(file, MediaProperty::DatabaseStatus, MetadataSource::Filesystem, QStringLiteral("PMR membership"), int(MediaFile::DbStatus::NoReference));
		MediaEngine::selectMetadata(file.evidence);
		return file;
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
	void physical_row_ids_survive_moves_and_separate_copies();
	void omf_gate_preserves_optional_column_indexes();

	// The Location column shows the whole path; View ▸ Resize Columns to
	// Fit (Cmd+T) is what makes it readable, so nothing here may quietly
	// shorten the value.
	void location_cell_shows_the_full_path();

	// The attribution setters own the flag+label pairing; a half-set
	// state (label without flag, or stale flags after a transition)
	// must be impossible. Labels asserted as raw literals on purpose —
	// the rename tripwire.
	void status_words_come_from_one_table();
	void unknown_classification_displays_without_guessing();
	void precomputes_gate_preserves_rows_and_existing_indexes();
	void clip_duration_is_separate_and_gated();
	void effect_columns_only_display_precompute_details();
	void precompute_categories_and_unknown_effects_display_consistently();

	// Bin-derived fallbacks, conflict handling and row refresh notifications.
	void fills_missing_owned_metadata_by_exact_identity();
	void avb_effect_changes_refresh_visible_cells();
	void mediaengine_selections_refresh_nonempty_cells_and_sources();
	void mediaengine_avb_changes_preserve_physical_rows_and_evidence();
	void mediaengine_avb_conflicts_clear_previously_selected_cells();
	void metadata_does_not_cross_byte_swapped_identities_data();
	void metadata_does_not_cross_byte_swapped_identities();
	void preserves_scanner_metadata_and_ignores_source_names();
	void conflicts_are_independent_and_retractable();
	void same_bin_name_with_different_uid_is_ambiguous();
	void readable_partial_metadata_is_distinct_from_invalid_bins();
	void rescans_and_removals_preserve_provenance();
	void avb_evidence_survives_removal_and_reactivation();
	void field_priorities_retain_avb_alternatives();
	void multiple_master_associations_do_not_choose_first();

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

void TestMediaTableModel::physical_row_ids_survive_moves_and_separate_copies()
{
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
	auto originalRow = row(original);
	originalRow.compression = QStringLiteral("Avid DNx HQX");
	originalRow.bitDepth = QStringLiteral("10-bit");
	originalRow.fileMobId = masterId();
	auto snapshot = QSharedPointer<SourceSnapshot>::create();
	snapshot->source = MetadataSource::Mxf;
	snapshot->path = original;
	const auto observe = [&](MediaProperty property, const QVariant &value, const SourceSnapshotRef &receipt)
	{
		MetadataObservation item;
		item.snapshot = receipt;
		item.property = QStringLiteral("Authored field");
		item.value = value;
		item.readState = PropertyReadState::Present;
		originalRow.evidence.observe(property, item);
	};
	observe(MediaProperty::Compression, originalRow.compression, snapshot);
	observe(MediaProperty::BitDepth, originalRow.bitDepth, snapshot);
	observe(MediaProperty::FileMobId, originalRow.fileMobId, snapshot);
	auto filesystem = QSharedPointer<SourceSnapshot>::create();
	filesystem->source = MetadataSource::Filesystem;
	filesystem->path = original;
	observe(MediaProperty::Location, original, filesystem);
	MediaEngine::selectMetadata(originalRow.evidence);
	auto receipt = QSharedPointer<MediaEngine::ScanResult>::create();
	MediaEngine::MediaFile retained;
	retained.path = original;
	receipt->files.append(retained);
	originalRow.mediaEngineScan = receipt;
	model.setMediaFiles(TestMediaFile::seeded({originalRow}));
	const KelpieId originalId = model.allFiles().first().kelpieId;
	QVERIFY(originalId != 0);
	model.applyTransfer(original, copy, true);
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(model.allFiles()[0].kelpieId, originalId);
	QVERIFY(model.allFiles()[1].kelpieId != originalId);
	QCOMPARE(model.allFiles()[0].masterMobId, model.allFiles()[1].masterMobId);
	QCOMPARE(model.allFiles()[0].evidence.selected(MediaProperty::Location).value.toString(), original);
	QCOMPARE(model.allFiles()[1].dbStatus, MediaFile::DbStatus::NoDatabase);
	QVERIFY(!put("future.pmr").isEmpty()); // Presence cannot prove membership without parsing this database.
	model.applyTransfer(original, moved, false);
	QCOMPARE(model.allFiles()[0].dbStatus, MediaFile::DbStatus::DbUnreadable);
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(model.allFiles()[0].kelpieId, originalId);
	QCOMPARE(model.allFiles()[0].scanStamp.path, moved);
	QCOMPARE(model.allFiles()[0].mediaFilePath, moved);
	for (const auto &file : model.allFiles())
	{
		QCOMPARE(file.evidence.selected(MediaProperty::Compression).value.toString(), file.compression);
		QCOMPARE(file.evidence.selected(MediaProperty::BitDepth).value.toString(), file.bitDepth);
		QCOMPARE(file.evidence.selected(MediaProperty::FileMobId).value.toString(), file.fileMobId);
		QCOMPARE(file.evidence.selected(MediaProperty::Location).value.toString(), file.mediaFilePath);
		QCOMPARE(file.evidence.selected(MediaProperty::DatabaseStatus).value.toInt(), int(file.dbStatus));
		QCOMPARE(file.evidence.observations(MediaProperty::DatabaseStatus).size(), 1);
		QCOMPARE(file.evidence.observations(MediaProperty::DatabaseStatus).first().basis, EvidenceBasis::Derived);
		QCOMPARE(file.mediaEngineScan, originalRow.mediaEngineScan);
		QCOMPARE(file.mediaEngineScan->files.first().path, original); // Original scan receipt was not rewritten.
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
	model.setMediaFiles(TestMediaFile::seeded({file}));
	for (bool omf : {true, false, true})
		for (bool clip : {true, false})
			for (bool precompute : {true, false})
			{
				model.setOmfScanEnabled(omf);
				model.setClipDurationEnabled(clip);
				model.setPrecomputesEnabled(precompute);
				QCOMPARE(model.columnCount(), 19 + int(omf) + int(clip) + 4 * int(precompute));
				if (omf)
				{
					QCOMPARE(model.headerData(model.omfScanColumn(), Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("OmfScan"));
					QCOMPARE(model.data(model.index(0, model.omfScanColumn()), Qt::DisplayRole).toString(), QStringLiteral("true"));
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
// Project cell, its tooltip, the tab and the CSV all read that table. This
// pins the table: every status has a label, the two couldn't-check states
// share the one "No Database" label but explain themselves differently, and
// "No project" is a separate fact (an empty project) with its own sentence.
void TestMediaTableModel::status_words_come_from_one_table()
{
	using DbStatus = MediaFile::DbStatus;
	for (DbStatus s : {DbStatus::Listed, DbStatus::NoReference, DbStatus::NoDatabase, DbStatus::DbUnreadable})
	{
		const auto text = MediaFile::dbStatusText(s);
		QVERIFY2(!text.label.isEmpty(), "every status has a label (it is the CSV value)");
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

void TestMediaTableModel::clip_duration_is_separate_and_gated()
{
	MediaTableModel model;
	MediaFile file = row();
	file.duration = {19, {25, 1}, {25, 1}, MediaDuration::Source::Descriptor};
	file.timecodeBase = 25;
	file.clipDurations = {{1, {344, {25, 1}, {25, 1}, MediaDuration::Source::ClipReference}, false},
						  {2, {250, {25, 1}, {25, 1}, MediaDuration::Source::ClipReference}, false}};
	model.setMediaFiles(TestMediaFile::seeded({file}));
	const int base = model.columnCount();
	QCOMPARE(model.headerData(int(MediaTableModel::Column::Duration), Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("Duration"));
	model.setClipDurationEnabled(true);
	QCOMPARE(model.columnCount(), base + 1);
	QCOMPARE(model.headerData(model.clipDurationColumn(), Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("Clip Duration"));
	QPersistentModelIndex clip(model.index(0, model.clipDurationColumn()));
	QCOMPARE(clip.data().toString(), QStringLiteral("Track 1: 00:00:13:19; Track 2: 00:00:10:00"));
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Duration)).data().toString(), QStringLiteral("00:00:00:19"));
	model.setPrecomputesEnabled(true);
	QCOMPARE(clip.column(), model.clipDurationColumn());
	QCOMPARE(clip.data().toString(), file.clipDurationDisplay());
	model.setPrecomputesEnabled(false);
	QCOMPARE(clip.column(), model.clipDurationColumn());
	model.setClipDurationEnabled(false);
	QCOMPARE(model.columnCount(), base);
	QVERIFY(!clip.isValid());
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
		QStringLiteral("Date Created"), QStringLiteral("Filename"), QStringLiteral("Source Filename"),
		QStringLiteral("Location")};
	const QPersistentModelIndex row(model.index(1, int(MediaTableModel::Column::Location)));
	const QString path = row.data().toString();
	QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
	QSignalSpy inserted(&model, &QAbstractItemModel::columnsInserted);
	QSignalSpy removed(&model, &QAbstractItemModel::columnsRemoved);
	QVERIFY(!model.precomputesEnabled());
	QCOMPARE(model.columnCount(), 19);
	for (int column = 0; column < baseHeaders.size(); ++column)
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), baseHeaders[column]);
	QVERIFY(!model.index(0, int(MediaTableModel::Column::Effect)).isValid());
	QVERIFY(!model.headerData(int(MediaTableModel::Column::Effect), Qt::Horizontal, Qt::DisplayRole).isValid());
	model.setPrecomputesEnabled(true);
	QCOMPARE(model.columnCount(), 23);
	for (int column = 0; column < baseHeaders.size(); ++column)
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), baseHeaders[column]);
	QCOMPARE(inserted.size(), 1);
	QCOMPARE(inserted.first().at(1).toInt(), 19);
	QCOMPARE(inserted.first().at(2).toInt(), 22);
	QVERIFY(row.isValid());
	QCOMPARE(row.data().toString(), path);
	const QPersistentModelIndex effect(model.index(1, int(MediaTableModel::Column::Effect)));
	model.setPrecomputesEnabled(true);
	QCOMPARE(inserted.size(), 1);
	model.setPrecomputesEnabled(false);
	model.setPrecomputesEnabled(false);
	QCOMPARE(model.columnCount(), 19);
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

void TestMediaTableModel::fills_missing_owned_metadata_by_exact_identity()
{
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({row(), row(QStringLiteral("/media/second.mxf"))}));
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	model.setAvbBins({bin()});
	QCOMPARE(changed.size(), 1);
	QCOMPARE(changed.first().at(0).value<QModelIndex>(), model.index(0, int(MediaTableModel::Column::ClipName)));
	QCOMPARE(changed.first().at(1).value<QModelIndex>(), model.index(1, model.columnCount() - 1));
	for (const MediaFile &file : model.allFiles())
	{
		QCOMPARE(file.clipName, QStringLiteral("Edited clip"));
		QCOMPARE(file.clipNameSource, MediaFile::ClipNameSource::Avb);
		QCOMPARE(file.originalBin, QStringLiteral("Original rushes"));
		QVERIFY(file.originalBinFromAvb);
	}
	model.setAvbBins({});
	for (const MediaFile &file : model.allFiles())
	{
		QVERIFY(file.clipName.isEmpty());
		QCOMPARE(file.clipNameSource, MediaFile::ClipNameSource::None);
		QVERIFY(file.originalBin.isEmpty());
		QVERIFY(!file.originalBinFromAvb);
	}
}

void TestMediaTableModel::avb_effect_changes_refresh_visible_cells()
{
	auto file = row();
	file.type = MediaFile::Type::Precompute;
	file.mediaEngineScan = QSharedPointer<MediaEngine::ScanResult>::create();
	MetadataObservation type;
	type.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{MetadataSource::Mxf, file.mediaFilePath, {}, SourceReadState::Complete});
	type.readState = PropertyReadState::Present;
	type.value = int(file.type);
	file.evidence.observe(MediaProperty::Type, type);
	record(file, MediaProperty::MasterMobId, MetadataSource::Mxf, QStringLiteral("GenericPackage.PackageUID"), file.masterMobId);
	MediaEngine::selectMetadata(file.evidence);
	MediaTableModel model;
	model.setPrecomputesEnabled(true);
	model.setMediaFiles(TestMediaFile::seeded({file}));
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	model.setAvbBins({evidenceBin(QStringLiteral("Sequence,3D_Warp+1"))});
	const int effectColumn = int(MediaTableModel::Column::Effect);
	QCOMPARE(model.index(0, effectColumn).data().toString(), QStringLiteral("3D Warp"));
	QVERIFY(!changed.isEmpty());
	QVERIFY(changed.last().at(1).value<QModelIndex>().column() >= effectColumn);
	model.setAvbBins({});
	QVERIFY(model.fileAt(0).effect.isEmpty());
	QVERIFY(!model.fileAt(0).evidence.observations(MediaProperty::Effect).isEmpty());
	QVERIFY(!model.fileAt(0).evidence.observations(MediaProperty::Effect).first().eligible);
}

void TestMediaTableModel::mediaengine_selections_refresh_nonempty_cells_and_sources()
{
	auto file = mediaEngineRow();
	record(file, MediaProperty::ClipName, MetadataSource::Mdb, QStringLiteral("OMFI:MOBJ:Name"), QStringLiteral("MDB clip"));
	record(file, MediaProperty::ClipName, MetadataSource::Mxf, QStringLiteral("GenericPackage.Name"), QStringLiteral("Header clip"));
	record(file, MediaProperty::OriginalBin, MetadataSource::Mxf, QStringLiteral("_ORG_BIN"), QStringLiteral("Header bin"));
	record(file, MediaProperty::OriginalBin, MetadataSource::Mdb, QStringLiteral("_ORG_BIN"), QStringLiteral("MDB bin"));
	MediaEngine::selectMetadata(file.evidence);
	file.clipName = QStringLiteral("Stale nonempty name");
	file.clipNameSource = MediaFile::ClipNameSource::Mdb;
	file.originalBin = QStringLiteral("Stale nonempty bin");
	file.originalBinFromAvb = true;
	file.compression = QStringLiteral("Stale compression without evidence");
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({file}));
	model.setAvbBins({evidenceBin()});
	const auto &current = model.fileAt(0);
	QCOMPARE(current.clipName, QStringLiteral("Header clip"));
	QCOMPARE(current.originalBin, QStringLiteral("MDB bin"));
	QCOMPARE(current.clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QVERIFY(!current.originalBinFromAvb);
	QVERIFY(current.compression.isEmpty());
	QCOMPARE(current.clipName, current.evidence.selected(MediaProperty::ClipName).value.toString());
	QCOMPARE(current.originalBin, current.evidence.selected(MediaProperty::OriginalBin).value.toString());
	QCOMPARE(model.index(0, int(MediaTableModel::Column::ClipName)).data().toString(), current.clipName);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::OriginalBin)).data().toString(), current.originalBin);
	QVERIFY(MediaCsv::rowLine(current).startsWith(QStringLiteral("\"Header clip\",\"No project\",\"MDB bin\",")));
	QCOMPARE(current.evidence.observations(MediaProperty::ClipName).size(), 3);
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Header clip"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("MDB bin"));
}

void TestMediaTableModel::mediaengine_avb_changes_preserve_physical_rows_and_evidence()
{
	auto file = mediaEngineRow();
	file.kelpieId = 91;
	file.mediaFilePath = QStringLiteral("/moved/OMFI MediaFiles/clip.omf");
	file.fileName = QStringLiteral("clip.omf");
	file.mediaFolderName = QStringLiteral("OMFI MediaFiles");
	file.volumePath = QStringLiteral("/moved");
	file.volumeName = QStringLiteral("WORK");
	file.omfEra = true;
	file.sizeBytes = 987654;
	file.created = QDateTime::fromSecsSinceEpoch(100, Qt::UTC);
	file.modified = QDateTime::fromSecsSinceEpoch(200, Qt::UTC);
	file.isQuarantined = true;
	file.isNonPortable = true;
	file.sampleRateEncoding = QByteArray::fromHex("400ebb80000000000000");
	file.scanStamp.path = QStringLiteral("/original/clip.omf");
	file.scanStamp.volumeIdentifier = QStringLiteral("original-volume");
	file.scanStamp.modified = QDateTime::fromSecsSinceEpoch(50, Qt::UTC);
	file.scanStamp.mobId = QStringLiteral("original-file-id");
	file.scanStamp.masterMobIds = {masterId()};
	MediaTableModel model;
	model.setPrecomputesEnabled(true);
	model.setMediaFiles(TestMediaFile::seeded({file}));
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	const auto first = evidenceBin(QStringLiteral("Sequence,3D_Warp+1"));
	const auto second = evidenceBin(QStringLiteral("Renamed render"));
	model.setAvbBins({first});
	QCOMPARE(model.fileAt(0).clipNameSource, MediaFile::ClipNameSource::Avb);
	QVERIFY(model.fileAt(0).originalBinFromAvb);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Effect)).data().toString(), QStringLiteral("3D Warp"));
	QVERIFY(MediaCsv::rowLine(model.fileAt(0), {true, false}).contains(QStringLiteral("\"3D Warp\"")));
	QCOMPARE(changed.size(), 1);
	model.setAvbBins({first});
	QCOMPARE(changed.size(), 1);
	model.setAvbBins({second});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Renamed render"));
	QCOMPARE(model.fileAt(0).effect, QStringLiteral("Renamed render"));
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Effect)).data().toString(), QStringLiteral("Renamed render"));
	QCOMPARE(model.index(0, int(MediaTableModel::Column::EffectCategory)).data().toString(), QStringLiteral("unknown"));
	model.setAvbBins({});
	QCOMPARE(changed.size(), 3);
	const auto &current = model.fileAt(0);
	QVERIFY(current.clipName.isEmpty());
	QVERIFY(current.originalBin.isEmpty());
	QCOMPARE(current.clipNameSource, MediaFile::ClipNameSource::None);
	QVERIFY(!current.originalBinFromAvb);
	QVERIFY(MediaCsv::rowLine(current).startsWith(QStringLiteral("\"\",\"No project\",\"\",")));
	QCOMPARE(current.kelpieId, file.kelpieId);
	QCOMPARE(current.mediaEngineScan, file.mediaEngineScan);
	QCOMPARE(current.mediaFilePath, file.mediaFilePath);
	QCOMPARE(current.fileName, file.fileName);
	QCOMPARE(current.mediaFolderName, file.mediaFolderName);
	QCOMPARE(current.volumePath, file.volumePath);
	QCOMPARE(current.volumeName, file.volumeName);
	QCOMPARE(current.omfEra, file.omfEra);
	QCOMPARE(current.sizeBytes, file.sizeBytes);
	QCOMPARE(current.created, file.created);
	QCOMPARE(current.modified, file.modified);
	QCOMPARE(current.isQuarantined, file.isQuarantined);
	QCOMPARE(current.isNonPortable, file.isNonPortable);
	QCOMPARE(current.sampleRateEncoding, file.sampleRateEncoding);
	QCOMPARE(current.scanStamp.path, file.scanStamp.path);
	QCOMPARE(current.scanStamp.volumeIdentifier, file.scanStamp.volumeIdentifier);
	QCOMPARE(current.scanStamp.modified, file.scanStamp.modified);
	QCOMPARE(current.scanStamp.mobId, file.scanStamp.mobId);
	QCOMPARE(current.scanStamp.masterMobIds, file.scanStamp.masterMobIds);
	QCOMPARE(current.mediaEngineAvbSources.size(), 2);
	QCOMPARE(current.mediaEngineAvbSources[0], first.sourceGraph);
	QCOMPARE(current.mediaEngineAvbSources[1], second.sourceGraph);
	const auto &names = current.evidence.observations(MediaProperty::ClipName);
	QCOMPARE(names.size(), 2);
	QCOMPARE(names[0].rawValue.toByteArray(), first.compositions[0].name.toUtf8());
	QCOMPARE(names[1].rawValue.toByteArray(), second.compositions[0].name.toUtf8());
	QVERIFY(!names[0].eligible);
	QVERIFY(!names[1].eligible);
	auto unchanged = current;
	QVERIFY(!applyResolvedMetadata(unchanged));
}

void TestMediaTableModel::mediaengine_avb_conflicts_clear_previously_selected_cells()
{
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({mediaEngineRow()}));
	const auto first = evidenceBin(QStringLiteral("First edit"));
	auto second = evidenceBin(QStringLiteral("Second edit"));
	second.compositions[0].originalBinUid = QStringLiteral("0000000100000003");
	model.setAvbBins({first});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("First edit"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
	model.setAvbBins({first, second});
	const auto &current = model.fileAt(0);
	QVERIFY(current.clipName.isEmpty());
	QVERIFY(current.originalBin.isEmpty());
	QCOMPARE(current.clipNameSource, MediaFile::ClipNameSource::None);
	QVERIFY(!current.originalBinFromAvb);
	for (const auto property : {MediaProperty::ClipName, MediaProperty::OriginalBin})
	{
		QCOMPARE(current.evidence.selected(property).selectedObservation, -1);
		QCOMPARE(current.evidence.selected(property).agreement, PropertyAgreement::Conflicting);
	}
	QVERIFY(model.index(0, int(MediaTableModel::Column::ClipName)).data().toString().isEmpty());
	QVERIFY(model.index(0, int(MediaTableModel::Column::OriginalBin)).data().toString().isEmpty());
	QVERIFY(MediaCsv::rowLine(current).startsWith(QStringLiteral("\"\",\"No project\",\"\",")));
	QCOMPARE(current.evidence.observations(MediaProperty::ClipName).size(), 2);
	QCOMPARE(current.evidence.observations(MediaProperty::OriginalBin).size(), 4);
	model.setAvbBins({first});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("First edit"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
}

void TestMediaTableModel::metadata_does_not_cross_byte_swapped_identities_data()
{
	QTest::addColumn<QString>("firstId");
	QTest::addColumn<QString>("secondId");
	QTest::newRow("OMF")
		<< QStringLiteral("060a2b3401010101.01010f0013000000.1122334455667788.060e2b347f7f2a80")
		<< QStringLiteral("060a2b3401010101.01010f0013000000.4433221166558877.060e2b347f7f2a80");
	QTest::newRow("MXF")
		<< QStringLiteral("060a2b3401010105.01010f1013000000.1122334455667788.99aabbccddeeff00")
		<< QStringLiteral("060a2b3401010105.01010f1013000000.4433221166558877.99aabbccddeeff00");
}

void TestMediaTableModel::metadata_does_not_cross_byte_swapped_identities()
{
	QFETCH(QString, firstId);
	QFETCH(QString, secondId);
	MediaFile first = row(QStringLiteral("/media/first"));
	first.masterMobId = firstId;
	MediaFile second = row(QStringLiteral("/media/second"));
	second.masterMobId = secondId;
	AvbBin firstBin = bin(QStringLiteral("First clip"), QStringLiteral("First bin"));
	firstBin.compositions[0].mobId = firstId;
	AvbBin secondBin = bin(QStringLiteral("Second clip"), QStringLiteral("Second bin"));
	secondBin.compositions[0].mobId = secondId;
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({first, second}));
	model.setAvbBins({firstBin});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("First clip"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("First bin"));
	QCOMPARE(model.fileAt(0).clipNameSource, MediaFile::ClipNameSource::Avb);
	QVERIFY(model.fileAt(1).clipName.isEmpty());
	QVERIFY(model.fileAt(1).originalBin.isEmpty());
	QCOMPARE(model.fileAt(1).clipNameSource, MediaFile::ClipNameSource::None);
	QVERIFY(!model.fileAt(1).originalBinFromAvb);
	model.setAvbBins({firstBin, secondBin});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("First clip"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("First bin"));
	QCOMPARE(model.fileAt(1).clipName, QStringLiteral("Second clip"));
	QCOMPARE(model.fileAt(1).originalBin, QStringLiteral("Second bin"));
}

void TestMediaTableModel::preserves_scanner_metadata_and_ignores_source_names()
{
	MediaTableModel model;
	MediaFile known = row();
	known.clipName = QStringLiteral("Header clip");
	known.clipNameSource = MediaFile::ClipNameSource::MaterialPackage;
	known.originalBin = QStringLiteral("Recorded bin");
	model.setMediaFiles(TestMediaFile::seeded({known}));
	model.setAvbBins({bin()});
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, known.clipName);
	QCOMPARE(model.fileAt(0).clipNameSource, known.clipNameSource);
	QCOMPARE(model.fileAt(0).originalBin, known.originalBin);
	QVERIFY(!model.fileAt(0).originalBinFromAvb);

	AvbBin source = bin();
	source.compositions[0].mobType = 3;
	model.setMediaFiles(TestMediaFile::seeded({row()}));
	model.setAvbBins({source});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	MediaFile noMaster = row();
	noMaster.fileMobId = noMaster.masterMobId;
	noMaster.masterMobId.clear();
	model.setMediaFiles(TestMediaFile::seeded({noMaster}));
	model.setAvbBins({bin()});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
}

void TestMediaTableModel::conflicts_are_independent_and_retractable()
{
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({row()}));
	model.setAvbBins({bin(), bin(QStringLiteral("Another edit"))});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
	model.setAvbBins({bin()});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QStringLiteral("Different bin"))});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	QVERIFY(!model.fileAt(0).originalBinFromAvb);
}

void TestMediaTableModel::same_bin_name_with_different_uid_is_ambiguous()
{
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({row()}));
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QStringLiteral("Original rushes"),
								 QStringLiteral("0000000100000003"))});
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	// A missing display name cannot erase evidence of a different owning bin.
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QString(),
								 QStringLiteral("0000000100000003"))});
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
}

void TestMediaTableModel::readable_partial_metadata_is_distinct_from_invalid_bins()
{
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({row()}));
	AvbBin invalid = bin(QStringLiteral("Unusable name"), QStringLiteral("Unusable bin"));
	invalid.usable = false;
	invalid.coverageComplete = false;
	invalid.error = QStringLiteral("Malformed document");
	model.setAvbBins({invalid});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());

	AvbBin partial = bin();
	partial.coverageComplete = false;
	partial.warnings.append(QStringLiteral("A separate dependency is unresolved"));
	model.setAvbBins({partial, invalid});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
	QCOMPARE(model.fileAt(0).clipNameSource, MediaFile::ClipNameSource::Avb);
	QVERIFY(model.fileAt(0).originalBinFromAvb);
	model.setAvbBins({invalid});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
}

void TestMediaTableModel::rescans_and_removals_preserve_provenance()
{
	MediaTableModel model;
	model.setAvbBins({bin()});
	model.setMediaFiles(TestMediaFile::seeded({row(), row(QStringLiteral("/media/new.mxf"))}));
	QCOMPARE(model.fileAt(1).originalBin, QStringLiteral("Original rushes"));
	model.removeFilesByPath({QStringLiteral("/media/clip.mxf")});
	model.setAvbBins({});
	QCOMPARE(model.rowCount(), 1);
	QVERIFY(model.fileAt(0).originalBin.isEmpty());

	model.setAvbBins({bin()});
	MediaFile refreshed = row();
	refreshed.clipName = QStringLiteral("Database clip");
	refreshed.clipNameSource = MediaFile::ClipNameSource::Mdb;
	refreshed.originalBin = QStringLiteral("Database bin");
	model.setMediaFiles(TestMediaFile::seeded({refreshed}));
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, refreshed.clipName);
	QCOMPARE(model.fileAt(0).clipNameSource, MediaFile::ClipNameSource::Mdb);
	QCOMPARE(model.fileAt(0).originalBin, refreshed.originalBin);
}

void TestMediaTableModel::avb_evidence_survives_removal_and_reactivation()
{
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({row()}));
	const auto loaded = evidenceBin();
	model.setAvbBins({loaded});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
	const auto &facts = model.fileAt(0).evidence.observations(MediaProperty::OriginalBin);
	QCOMPARE(facts.size(), 2);
	QCOMPARE(facts[0].property, QStringLiteral("BinRef.name"));
	QCOMPARE(facts[0].objectIdentity, QStringLiteral("12"));
	QCOMPARE(facts[0].rawValue.toByteArray(), QByteArray("Legacy rushes"));
	QVERIFY(!facts[0].eligible);
	QVERIFY(facts[1].eligible);
	QCOMPARE(model.fileAt(0).mediaEngineAvbSources.size(), 1);
	QCOMPARE(model.fileAt(0).mediaEngineAvbSources[0], loaded.sourceGraph);
	model.setAvbBins({});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	QCOMPARE(model.fileAt(0).mediaEngineAvbSources[0], loaded.sourceGraph);
	for (const auto &fact : model.fileAt(0).evidence.observations(MediaProperty::OriginalBin))
		QVERIFY(!fact.eligible);
	model.setAvbBins({loaded});
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
	QCOMPARE(model.fileAt(0).evidence.observations(MediaProperty::OriginalBin).size(), 2);
	QVERIFY(!model.fileAt(0).evidence.observations(MediaProperty::OriginalBin)[0].eligible);
	QVERIFY(model.fileAt(0).evidence.observations(MediaProperty::OriginalBin)[1].eligible);
}

void TestMediaTableModel::field_priorities_retain_avb_alternatives()
{
	auto file = row();
	auto record = [&](MediaProperty property, MetadataSource kind, const QString &text)
	{
		auto snapshot = QSharedPointer<SourceSnapshot>::create();
		snapshot->source = kind;
		MetadataObservation observation;
		observation.snapshot = snapshot;
		observation.property = QStringLiteral("Recorded fixture field");
		observation.value = text;
		observation.readState = PropertyReadState::Present;
		file.evidence.observe(property, observation);
	};
	record(MediaProperty::ClipName, MetadataSource::Mdb, QStringLiteral("MDB clip"));
	record(MediaProperty::ClipName, MetadataSource::Mxf, QStringLiteral("Header clip"));
	record(MediaProperty::OriginalBin, MetadataSource::Mxf, QStringLiteral("Header bin"));
	record(MediaProperty::OriginalBin, MetadataSource::Mdb, QStringLiteral("MDB bin"));
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({file}));
	model.setAvbBins({evidenceBin()});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Header clip"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("MDB bin"));
	QCOMPARE(model.fileAt(0).clipNameSource, MediaFile::ClipNameSource::MaterialPackage);
	QVERIFY(!model.fileAt(0).originalBinFromAvb);
	QCOMPARE(model.fileAt(0).evidence.selected(MediaProperty::ClipName).value.toString(), QStringLiteral("Header clip"));
	QCOMPARE(model.fileAt(0).evidence.selected(MediaProperty::OriginalBin).value.toString(), QStringLiteral("MDB bin"));
	QCOMPARE(model.fileAt(0).evidence.selected(MediaProperty::ClipName).agreement, PropertyAgreement::Conflicting);
	QCOMPARE(model.fileAt(0).evidence.observations(MediaProperty::ClipName).size(), 3);
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Header clip"));
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("MDB bin"));
}

void TestMediaTableModel::multiple_master_associations_do_not_choose_first()
{
	const auto first = evidenceBin(QStringLiteral("First edit"));
	auto second = evidenceBin(QStringLiteral("Second edit"));
	second.compositions[0].mobId = QStringLiteral("second exact identity");
	auto file = row();
	file.masterMobIds = {first.compositions[0].mobId, second.compositions[0].mobId};
	MediaTableModel model;
	model.setMediaFiles(TestMediaFile::seeded({file}));
	model.setAvbBins({first, second});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QCOMPARE(model.fileAt(0).originalBin, QStringLiteral("Original rushes"));
	QCOMPARE(model.fileAt(0).evidence.observations(MediaProperty::ClipName).size(), 2);
	QCOMPARE(model.fileAt(0).evidence.selected(MediaProperty::ClipName).selectedObservation, -1);
	model.setAvbBins({second});
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Second edit"));
}

QTEST_GUILESS_MAIN(TestMediaTableModel)
#include "tst_mediatablemodel.moc"
