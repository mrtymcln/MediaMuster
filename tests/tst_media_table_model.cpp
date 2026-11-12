// Table row notifications, Bella display values and transfer safety.

#include "scanner/bella_scanner.h"
#include "support/enum_util.h"
#include "media/media_file_record.h"
#include "media/media_csv.h"
#include "ui/media_table_model.h"
#include "ui/scan_result_p.h"
#include "operations/operation_request.h"
#include "media/mob_id_text.h"
#include "storage/volume_identity.h"

#include <QAbstractItemModelTester>
#include <QDateTime>
#include <QPersistentModelIndex>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <array>
#include <initializer_list>
#include <limits>
#include <utility>

// MARK: - Test helpers

namespace
{
	QString masterId()
	{
		return MobId::format(QByteArray::fromHex(
			"060a2b340101010501010f1013000000443322116655887799aabbccddeeff00"));
	}

	MediaFileRecord row(const QString &path = QStringLiteral("/media/clip.mxf"))
	{
		MediaFileRecord file;
		file.mediaFilePath = path;
		file.masterMobId = masterId();
		return file;
	}

	Bella::MobId aafIdFromHex(const QByteArray &hex)
	{
		return std::get<Bella::MobId>(Bella::normalizeMobId(QByteArray::fromHex(hex),
															Bella::MobIdLayout::Aaf32, Bella::ByteOrder::LittleEndian));
	}

	Bella::MobId selectedId()
	{
		return aafIdFromHex("060a2b340101010501010f1013000000443322116655887799aabbccddeeff00");
	}
}

// MARK: - Test fixture

class TestMediaTableModel : public QObject
{
	Q_OBJECT
private slots:
	void id_allocation_never_wraps_or_reuses_removed_ids()
	{
		KelpieIdAllocator ids;
		QCOMPARE(ids.allocate(), KelpieId(1));
		ids.reserveThrough(10);
		QCOMPARE(ids.allocate(), KelpieId(11));
		ids.reserveThrough(std::numeric_limits<KelpieId>::max() - 1);
		QCOMPARE(ids.allocate(), std::numeric_limits<KelpieId>::max());
		QVERIFY_THROWS_EXCEPTION(KelpieIdExhausted, ids.allocate());
		ids.reset();
		QCOMPARE(ids.allocate(), KelpieId(1));
	}

	void row_removal_preserves_rows_and_notifications_data();
	void row_removal_preserves_rows_and_notifications();

	// A modification time cannot substitute for an unknown creation date.
	void unknown_created_date_displays_blank();
	void physical_row_ids_survive_moves_and_separate_copies_data();
	void physical_row_ids_survive_moves_and_separate_copies();
	void omf_gate_preserves_optional_column_indexes();

	// Column width must not shorten the stored Location value.
	void location_cell_shows_the_full_path();

	void status_words_come_from_one_table();
	void database_columns_show_the_compact_result_data();
	void database_columns_show_the_compact_result();
	void unknown_classification_displays_without_guessing();
	void precomputes_gate_preserves_rows_and_existing_indexes();
	void effect_columns_only_display_precompute_details();
	void precompute_categories_and_unknown_effects_display_consistently();

	// The model displays the values prepared by the scan.
	void prepared_metadata_reaches_table_cells();
	void selected_master_reaches_table();

private:
	/// Build `n` media file records with sequential paths.
	static QVector<MediaFileRecord> makeRows(int n);
	/// filePaths from the model in current order.
	static QStringList pathsOf(const MediaTableModel &m);
};

// MARK: - Test cases

QVector<MediaFileRecord> TestMediaTableModel::makeRows(int n)
{
	QVector<MediaFileRecord> v;
	v.reserve(n);
	for (int i = 0; i < n; ++i)
	{
		MediaFileRecord mf;
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

// MARK: - Row updates and physical identity

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
	model.setMediaFiles(makeRows(initialCount));
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
	MediaFileRecord f;
	f.fileName = QStringLiteral("V01.abc.mxf");
	f.volumeName = QStringLiteral("EDIT");
	f.mediaFolderName = QStringLiteral("8646");
	f.mediaFilePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/8646/V01.abc.mxf");

	MediaTableModel model;
	model.setMediaFiles({f});
	const QModelIndex idx =
		model.index(0, Enum::toUnderlying(MediaTableModel::Column::Location));

	QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), f.mediaFilePath);
	QCOMPARE(model.headerData(Enum::toUnderlying(MediaTableModel::Column::Location),
							  Qt::Horizontal, Qt::DisplayRole)
				 .toString(),
			 QStringLiteral("File Path"));
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
	const QString neighbor = put("neighbor.mxf");
	QVERIFY(!original.isEmpty() && !copy.isEmpty() && !moved.isEmpty() && !neighbor.isEmpty());
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	const auto makeScannedRow = [](const QString &path, const Bella::MobId &fileMobId,
								   const Bella::MobId &masterMobId, const Bella::OperationSafety &safety)
	{
		const QFileInfo info(path);
		Bella::ScanEntry entry;
		entry.path = path;
		entry.volumeIdentifier = VolumeIdentity::capture(path).identifier();
		entry.sizeBytes = info.size();
		entry.modified = info.lastModified();
		entry.localReference = Bella::LocalReference::Listed;
		entry.metadata.fileMobId = fileMobId;
		entry.metadata.masterMobId = masterMobId;
		entry.metadata.compression.compressionName = QStringLiteral("DNx HQX");
		entry.metadata.bitDepth.bits = 10;
		entry.operationSafety = safety;
		return mediaFileRecordFromScanEntry(entry);
	};
	const auto fileIdOrigin = headerSkipped ? Bella::FileIdOrigin::Database : Bella::FileIdOrigin::Header;
	const auto originalRow = makeScannedRow(original, selectedId(), selectedId(), Bella::VerifyFileId{selectedId(), fileIdOrigin});
	const auto neighborRow = makeScannedRow(neighbor,
											aafIdFromHex("060a2b340101010501010f1013000000a1a2a3a4a5a6a7a8a9aaabacadaeafb0"),
											aafIdFromHex("060a2b340101010501010f1013000000b1b2b3b4b5b6b7b8b9babbbcbdbebfc0"),
											Bella::RescanReason::FileChanged);
	QVERIFY(originalRow.operationScanReceipt);
	const auto &originalChecks = *originalRow.operationScanReceipt;
	QCOMPARE(originalChecks.headerIdentity.fileMobId, headerSkipped ? QString{} : originalRow.fileMobId);
	QVERIFY(originalChecks.headerIdentity.masterMobIds.isEmpty());
	QCOMPARE(originalChecks.databaseFileMobIdToVerify, headerSkipped ? originalRow.fileMobId : QString{});
	QVERIFY(neighborRow.fileMobId != originalRow.fileMobId);
	QVERIFY(neighborRow.masterMobId != originalRow.masterMobId);
	QVERIFY(neighborRow.operationScanReceipt);
	QVERIFY(!neighborRow.operationScanReceipt->headerIdentity.unavailableReason.isEmpty());
	model.setMediaFiles({originalRow, neighborRow});
	constexpr int neighborIndex = 1;
	constexpr int copyIndex = 2;
	const KelpieId originalId = model.allFiles().first().kelpieId;
	const KelpieId neighborId = model.fileAt(neighborIndex).kelpieId;
	QVERIFY(originalId != 0);
	const QPersistentModelIndex noDatabase(model.index(0, int(MediaTableModel::Column::NoDatabase)));
	const QPersistentModelIndex noReference(model.index(0, int(MediaTableModel::Column::NoReference)));
	QCOMPARE(noDatabase.data().toString(), QStringLiteral("false"));
	QCOMPARE(noReference.data().toString(), QStringLiteral("false"));
	model.applyTransfer(original, copy, true);
	QCOMPARE(model.rowCount(), 3);
	QCOMPARE(model.allFiles()[0].kelpieId, originalId);
	QVERIFY(model.fileAt(copyIndex).kelpieId != originalId);
	QVERIFY(model.fileAt(copyIndex).kelpieId != neighborId);
	QCOMPARE(model.allFiles()[0].masterMobId, model.fileAt(copyIndex).masterMobId);
	QCOMPARE(model.allFiles()[0].mediaFilePath, original);
	QCOMPARE(model.fileAt(copyIndex).mediaFilePath, copy);
	QCOMPARE(model.fileAt(copyIndex).dbStatus, MediaFileRecord::DbStatus::NoDatabase);
	QCOMPARE(model.index(copyIndex, int(MediaTableModel::Column::NoDatabase)).data().toString(), QStringLiteral("true"));
	QCOMPARE(model.index(copyIndex, int(MediaTableModel::Column::NoReference)).data().toString(), QStringLiteral("false"));
	QVERIFY(!put("future.pmr").isEmpty()); // Presence cannot prove membership without parsing this database.
	model.applyTransfer(original, moved, false);
	QCOMPARE(model.allFiles()[0].dbStatus, MediaFileRecord::DbStatus::DbUnreadable);
	QVERIFY(noDatabase.isValid());
	QVERIFY(noReference.isValid());
	QCOMPARE(noDatabase.data().toString(), QStringLiteral("true"));
	QCOMPARE(noReference.data().toString(), QStringLiteral("false"));
	QCOMPARE(model.rowCount(), 3);
	QCOMPARE(model.allFiles()[0].kelpieId, originalId);
	QCOMPARE(model.allFiles()[0].scanStamp.path, moved);
	QCOMPARE(model.allFiles()[0].mediaFilePath, moved);
	QCOMPARE(model.fileAt(neighborIndex).kelpieId, neighborId);
	QCOMPARE(model.fileAt(neighborIndex).mediaFilePath, neighbor);
	QCOMPARE(model.fileAt(neighborIndex).dbStatus, MediaFileRecord::DbStatus::Listed);
	const std::array expectedRows{&originalRow, &neighborRow, &originalRow};
	for (std::size_t index = 0; index < expectedRows.size(); ++index)
	{
		const auto &file = model.fileAt(static_cast<int>(index));
		const auto &expected = *expectedRows[index];
		const auto &checks = *expected.operationScanReceipt;
		QCOMPARE(file.compression, expected.compression);
		QCOMPARE(file.bitDepthDisplay(), expected.bitDepthDisplay());
		QCOMPARE(file.fileMobId, expected.fileMobId);
		QCOMPARE(file.masterMobId, expected.masterMobId);
		QCOMPARE(file.scanStamp.fileMobId, expected.fileMobId);
		QCOMPARE(file.scanStamp.masterMobId, expected.masterMobId);
		QVERIFY(file.operationScanReceipt);
		QCOMPARE(file.operationScanReceipt->headerIdentity.fileMobId, checks.headerIdentity.fileMobId);
		QCOMPARE(file.operationScanReceipt->headerIdentity.masterMobIds, checks.headerIdentity.masterMobIds);
		QCOMPARE(file.operationScanReceipt->headerIdentity.unavailableReason, checks.headerIdentity.unavailableReason);
		QCOMPARE(file.operationScanReceipt->databaseFileMobIdToVerify, checks.databaseFileMobIdToVerify);
		QCOMPARE(file.scanStamp.path, file.mediaFilePath);
		QCOMPARE(file.scanStamp.modified, file.modified);
		QCOMPARE(file.scanStamp.volumeIdentifier, VolumeIdentity::capture(file.mediaFilePath).identifier());
		// A later operation uses the frozen scan checks at the updated location.
		const auto item = operationItemFromMediaFile(file);
		QVERIFY(item.headerIdentity);
		QCOMPARE(item.fileMobId, expected.fileMobId);
		QCOMPARE(item.masterMobId, expected.masterMobId);
		QCOMPARE(item.headerIdentity->fileMobId, checks.headerIdentity.fileMobId);
		QCOMPARE(item.headerIdentity->masterMobIds, checks.headerIdentity.masterMobIds);
		QCOMPARE(item.headerIdentity->unavailableReason, checks.headerIdentity.unavailableReason);
		QCOMPARE(item.databaseFileMobIdToVerify, checks.databaseFileMobIdToVerify);
		QCOMPARE(item.scanPath, file.mediaFilePath);
		QCOMPARE(item.scanVolumeIdentifier, file.scanStamp.volumeIdentifier);
		QCOMPARE(item.modifiedMs, file.modified.toMSecsSinceEpoch());
	}
	model.setMediaFiles({row(copy)});
	QCOMPARE(model.allFiles()[0].kelpieId, KelpieId(1));
}

// MARK: - Columns and displayed metadata

void TestMediaTableModel::omf_gate_preserves_optional_column_indexes()
{
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	MediaFileRecord file = row();
	file.omfEra = true;
	file.bellaValues.emplace().durationTimecode = {0, 0, 0, 19};
	model.setMediaFiles({file});
	const QPersistentModelIndex duration(model.index(0, int(MediaTableModel::Column::Duration)));
	for (bool omf : {true, false, true})
	{
		model.setOmfScanEnabled(omf);
		const QPersistentModelIndex family = omf ? model.index(0, model.omfScanColumn()) : QModelIndex{};
		for (bool precompute : {true, false, true})
		{
			model.setPrecomputesEnabled(precompute);
			QCOMPARE(model.columnCount(), int(MediaTableModel::Column::PrecomputeCategory) + int(omf) + 3 * int(precompute));
			for (int column = 0; column < model.omfScanColumn(); ++column)
				QCOMPARE(model.columnIndex(model.columnAt(column)), column);
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
	MediaFileRecord withDate;
	withDate.mediaFilePath = QStringLiteral("/vol/a.mxf");
	withDate.created = QDateTime(QDate(2026, 7, 20), QTime(12, 30));

	MediaFileRecord withoutDate;
	withoutDate.mediaFilePath = QStringLiteral("/vol/b.mxf");
	withoutDate.modified = QDateTime(QDate(2026, 7, 21), QTime(9, 45));
	// created left invalid — a filesystem that records no birth time.

	MediaTableModel m;
	m.setMediaFiles({withDate, withoutDate});
	const int col = int(MediaTableModel::Column::Created);

	QCOMPARE(m.index(0, col).data(Qt::DisplayRole).toString(),
			 QStringLiteral("2026-07-20 12:30"));
	QVERIFY(m.index(1, col).data(Qt::DisplayRole).toString().isEmpty());

	const auto sortValue = m.index(1, col).data(MediaTableModel::SortRole);
	QCOMPARE(sortValue.metaType(), QMetaType::fromType<QDateTime>());
	QVERIFY(!sortValue.toDateTime().isValid());
}

// Missing and unreadable databases share a label but keep distinct explanations.
// Project availability remains independent of database membership.
void TestMediaTableModel::status_words_come_from_one_table()
{
	using DbStatus = MediaFileRecord::DbStatus;
	for (DbStatus s : {DbStatus::Listed, DbStatus::NoReference, DbStatus::NoDatabase, DbStatus::DbUnreadable})
	{
		const auto text = MediaFileRecord::dbStatusText(s);
		QVERIFY2(!text.label.isEmpty(), "every status has a label for its explanation");
		QCOMPARE(text.why.isEmpty(), s == DbStatus::Listed); // only Listed needs no explanation
	}
	QCOMPARE(MediaFileRecord::dbStatusText(DbStatus::NoDatabase).label,
			 MediaFileRecord::dbStatusText(DbStatus::DbUnreadable).label); // one tab
	QVERIFY(MediaFileRecord::dbStatusText(DbStatus::NoDatabase).why !=
			MediaFileRecord::dbStatusText(DbStatus::DbUnreadable).why); // two reasons
	QCOMPARE(MediaFileRecord::dbStatusText(DbStatus::NoReference).label, QStringLiteral("No Reference"));

	MediaFileRecord f;
	QCOMPARE(f.dbStatus, DbStatus::Listed);
	QVERIFY(!f.isNoDatabase());
	f.dbStatus = DbStatus::DbUnreadable;
	QVERIFY(f.isNoDatabase());
	f.dbStatus = DbStatus::NoDatabase;
	QVERIFY(f.isNoDatabase());

	QVERIFY(f.hasNoProject());
	QCOMPARE(f.projectDisplay(), QStringLiteral("No project"));
	f.project = QStringLiteral("Tëst");
	QVERIFY(!f.hasNoProject());
	QCOMPARE(f.projectDisplay(), QStringLiteral("Tëst"));

	MediaTableModel m;
	MediaFileRecord unlisted;
	unlisted.dbStatus = DbStatus::NoReference;
	m.setMediaFiles({unlisted});
	const int col = int(MediaTableModel::Column::Project);
	QCOMPARE(m.index(0, col).data(Qt::DisplayRole).toString(), QStringLiteral("No project"));
	const QString tip = m.index(0, col).data(Qt::ToolTipRole).toString();
	QVERIFY(tip.contains(MediaFileRecord::noProjectWhy()));
	QVERIFY(tip.contains(MediaFileRecord::dbStatusText(DbStatus::NoReference).why));
}

void TestMediaTableModel::database_columns_show_the_compact_result_data()
{
	QTest::addColumn<MediaFileRecord::DbStatus>("status");
	QTest::addColumn<bool>("noDatabase");
	QTest::addColumn<bool>("noReference");
	QTest::newRow("listed") << MediaFileRecord::DbStatus::Listed << false << false;
	QTest::newRow("checked-missing-reference") << MediaFileRecord::DbStatus::NoReference << false << true;
	QTest::newRow("no-local-databases") << MediaFileRecord::DbStatus::NoDatabase << true << false;
	QTest::newRow("unresolved-local-checks") << MediaFileRecord::DbStatus::DbUnreadable << true << false;
}

void TestMediaTableModel::database_columns_show_the_compact_result()
{
	QFETCH(MediaFileRecord::DbStatus, status);
	QFETCH(bool, noDatabase);
	QFETCH(bool, noReference);
	MediaFileRecord file = row();
	file.dbStatus = status;
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	model.setMediaFiles({file});
	const int databaseColumn = int(MediaTableModel::Column::NoDatabase);
	const int referenceColumn = int(MediaTableModel::Column::NoReference);
	QCOMPARE(databaseColumn, int(MediaTableModel::Column::KelpieId) + 1);
	QCOMPARE(referenceColumn, databaseColumn + 1);
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
			const auto databaseSortValue = database.data(MediaTableModel::SortRole);
			const auto referenceSortValue = reference.data(MediaTableModel::SortRole);
			QCOMPARE(databaseSortValue.metaType(), QMetaType::fromType<int>());
			QCOMPARE(referenceSortValue.metaType(), QMetaType::fromType<int>());
			QCOMPARE(databaseSortValue.toInt(), int(noDatabase));
			QCOMPARE(referenceSortValue.toInt(), int(noReference));
			QCOMPARE(model.headerData(databaseColumn, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("No Database"));
			QCOMPARE(model.headerData(referenceColumn, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("No Reference"));
			const QString databaseTip = database.data(Qt::ToolTipRole).toString();
			const QString referenceTip = reference.data(Qt::ToolTipRole).toString();
			QVERIFY(!databaseTip.isEmpty());
			QVERIFY(!referenceTip.isEmpty());
			if (status != MediaFileRecord::DbStatus::Listed)
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
	static_assert(int(MediaFileRecord::Kind::Video) == 0 && int(MediaFileRecord::Kind::Audio) == 1);
	static_assert(int(MediaFileRecord::Type::Media) == 0 && int(MediaFileRecord::Type::Precompute) == 1);
	MediaFileRecord unknown;
	QCOMPARE(unknown.kind, MediaFileRecord::Kind::Unknown);
	QCOMPARE(unknown.type, MediaFileRecord::Type::Unknown);

	MediaFileRecord video;
	video.kind = MediaFileRecord::Kind::Video;
	video.type = MediaFileRecord::Type::Media;
	MediaFileRecord audio;
	audio.kind = MediaFileRecord::Kind::Audio;
	audio.type = MediaFileRecord::Type::Precompute;
	MediaTableModel model;
	model.setMediaFiles({unknown, video, audio});
	const int kind = int(MediaTableModel::Column::Kind);
	const int type = int(MediaTableModel::Column::Type);
	QCOMPARE(model.index(0, kind).data().toString(), QStringLiteral("unknown"));
	QCOMPARE(model.index(0, type).data().toString(), QStringLiteral("unknown"));
	QVERIFY(!model.index(0, kind).data(Qt::ToolTipRole).toString().isEmpty());
	QVERIFY(!model.index(0, type).data(Qt::ToolTipRole).toString().isEmpty());
	QCOMPARE(model.index(1, kind).data().toString(), QStringLiteral("Video"));
	QCOMPARE(model.index(1, type).data().toString(), QStringLiteral("Media"));
	QCOMPARE(model.index(2, kind).data().toString(), QStringLiteral("Audio"));
	QCOMPARE(model.index(2, type).data().toString(), QStringLiteral("Precompute"));
}

// MARK: - Precompute presentation

void TestMediaTableModel::precomputes_gate_preserves_rows_and_existing_indexes()
{
	MediaTableModel model;
	QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
	model.setMediaFiles(makeRows(2));
	const QStringList baseHeaders{
		QStringLiteral("Clip Name"), QStringLiteral("Bin"), QStringLiteral("Project"),
		QStringLiteral("Duration"), QStringLiteral("Kind"), QStringLiteral("Type"),
		QStringLiteral("Compression"), QStringLiteral("Resolution"), QStringLiteral("Frame Rate"),
		QStringLiteral("Sample Rate"), QStringLiteral("Bit Depth"), QStringLiteral("Size (MB)"),
		QStringLiteral("Date Created"), QStringLiteral("File Path"), QStringLiteral("FileMobId"),
		QStringLiteral("MasterMobId"), QStringLiteral("KelpieId"), QStringLiteral("No Database"),
		QStringLiteral("No Reference")};
	const QPersistentModelIndex row(model.index(1, int(MediaTableModel::Column::Location)));
	const QString path = row.data().toString();
	QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
	QSignalSpy inserted(&model, &QAbstractItemModel::columnsInserted);
	QSignalSpy removed(&model, &QAbstractItemModel::columnsRemoved);
	QVERIFY(!model.precomputesEnabled());
	QCOMPARE(model.columnCount(), baseHeaders.size());
	for (int column = 0; column < baseHeaders.size(); ++column)
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), baseHeaders[column]);
	QVERIFY(!model.index(0, int(MediaTableModel::Column::Effect)).isValid());
	QVERIFY(!model.headerData(int(MediaTableModel::Column::Effect), Qt::Horizontal, Qt::DisplayRole).isValid());
	model.setPrecomputesEnabled(true);
	QCOMPARE(model.columnCount(), baseHeaders.size() + 3);
	for (int column = 0; column < baseHeaders.size(); ++column)
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), baseHeaders[column]);
	QCOMPARE(inserted.size(), 1);
	QCOMPARE(inserted.first().at(1).toInt(), baseHeaders.size());
	QCOMPARE(inserted.first().at(2).toInt(), baseHeaders.size() + 2);
	QVERIFY(row.isValid());
	QCOMPARE(row.data().toString(), path);
	const QPersistentModelIndex effect(model.index(1, int(MediaTableModel::Column::Effect)));
	model.setPrecomputesEnabled(true);
	QCOMPARE(inserted.size(), 1);
	model.setPrecomputesEnabled(false);
	model.setPrecomputesEnabled(false);
	QCOMPARE(model.columnCount(), baseHeaders.size());
	QCOMPARE(removed.size(), 1);
	QCOMPARE(reset.size(), 0);
	QCOMPARE(model.rowCount(), 2);
	QVERIFY(row.isValid());
	QCOMPARE(row.data().toString(), path);
	QVERIFY(!effect.isValid());
}

void TestMediaTableModel::effect_columns_only_display_precompute_details()
{
	MediaFileRecord precompute;
	precompute.type = MediaFileRecord::Type::Precompute;
	precompute.precomputeCategory = MediaFileRecord::PrecomputeCategory::RenderedEffects;
	precompute.effect = QStringLiteral("3D Warp");
	precompute.effectCategory = QStringLiteral("Blend");
	MediaFileRecord media = precompute;
	media.type = MediaFileRecord::Type::Media;
	MediaFileRecord unknown = precompute;
	unknown.type = MediaFileRecord::Type::Unknown;
	MediaTableModel model;
	model.setMediaFiles({precompute, media, unknown});
	model.setPrecomputesEnabled(true);
	const QStringList headers{QStringLiteral("Precompute Category"), QStringLiteral("Effect Category"), QStringLiteral("Effect")};
	const QStringList values{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")};
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
	MediaFileRecord rendered, title, unknown;
	rendered.type = title.type = unknown.type = MediaFileRecord::Type::Precompute;
	rendered.precomputeCategory = MediaFileRecord::PrecomputeCategory::RenderedEffects;
	title.precomputeCategory = MediaFileRecord::PrecomputeCategory::TitlesAndMatteKeys;
	MediaTableModel model;
	model.setMediaFiles({rendered, title, unknown, MediaFileRecord{}});
	model.setPrecomputesEnabled(true);
	const int category = int(MediaTableModel::Column::PrecomputeCategory);
	QCOMPARE(model.index(0, category).data().toString(), QStringLiteral("Rendered Effects"));
	QCOMPARE(model.index(1, category).data().toString(), QStringLiteral("Titles and Matte Keys"));
	QVERIFY(model.index(2, category).data().toString().isEmpty());
	for (auto column : {MediaTableModel::Column::Effect, MediaTableModel::Column::EffectCategory})
		for (int row = 0; row < 3; ++row)
			QCOMPARE(model.index(row, int(column)).data().toString(), QStringLiteral("unknown"));
	QVERIFY(model.index(3, category).data().toString().isEmpty());
}

// MARK: - Prepared scan handoff

void TestMediaTableModel::prepared_metadata_reaches_table_cells()
{
	Bella::ScanEntry entry;
	entry.kelpieId = 91;
	entry.path = QStringLiteral("/media/clip.mxf");
	entry.localReference = Bella::LocalReference::NoReference;
	entry.metadata.type = Bella::MatchType::Precompute;
	entry.metadata.clipName = QStringLiteral("Sequence,3D_Warp+1");
	entry.metadata.originalBin = QStringLiteral("MDB bin");
	entry.metadata.effects = {QStringLiteral("3D Warp"), QStringLiteral("Blend")};
	const auto file = mediaFileRecordFromScanEntry(entry);
	MediaTableModel model;
	model.setPrecomputesEnabled(true);
	model.setMediaFiles({file});
	const auto &current = model.fileAt(0);
	QCOMPARE(current.kelpieId, entry.kelpieId);
	QCOMPARE(current.type, MediaFileRecord::Type::Precompute);
	QCOMPARE(current.dbStatus, MediaFileRecord::DbStatus::NoReference);
	QCOMPARE(current.clipName, QStringLiteral("Sequence,3D_Warp+1"));
	QCOMPARE(current.originalBin, QStringLiteral("MDB bin"));
	QCOMPARE(current.effect, QStringLiteral("3D Warp"));
	QCOMPARE(current.effectCategory, QStringLiteral("Blend"));
	QCOMPARE(model.index(0, int(MediaTableModel::Column::ClipName)).data().toString(), current.clipName);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::OriginalBin)).data().toString(), current.originalBin);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Type)).data().toString(), QStringLiteral("Precompute"));
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Effect)).data().toString(), current.effect);
}

void TestMediaTableModel::selected_master_reaches_table()
{
	Bella::ScanEntry entry;
	entry.path = QStringLiteral("/media/clip.mxf");
	entry.metadata.masterMobId = selectedId();
	const auto file = mediaFileRecordFromScanEntry(entry);
	MediaTableModel model;
	model.setMediaFiles({file});
	const auto &current = model.fileAt(0);
	QCOMPARE(current.masterMobId, masterId());
	QCOMPARE(model.index(0, int(MediaTableModel::Column::MasterMobId)).data().toString(), masterId());
	QVERIFY(MediaCsv::rowLine(current).contains(masterId()));
}

QTEST_GUILESS_MAIN(TestMediaTableModel)
#include "tst_media_table_model.moc"
