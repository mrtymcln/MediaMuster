// Table row notifications, display values and bin-derived metadata ownership.

#include "avbparser.h"
#include "enumutil.h"
#include "mediafile.h"
#include "mediatablemodel.h"
#include "mobid.h"

#include <QDateTime>
#include <QPersistentModelIndex>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
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
		value.valid = true;
		value.complete = true;
		value.filePath = QStringLiteral("/current/renamed-bin.avb");
		value.displayName = QStringLiteral("renamed-bin");
		AvbMob mob;
		mob.mobId = masterId();
		mob.name = name;
		mob.mobType = AvbMob::masterMobType;
		mob.originalBin = originalBin;
		mob.originalBinUid = uid;
		value.mobs.append(mob);
		return value;
	}

	MediaFile row(const QString &path = QStringLiteral("/media/clip.mxf"))
	{
		MediaFile file;
		file.filePath = path;
		file.masterMobId = MobId::toPmrForm(masterId());
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
	void effect_gate_preserves_rows_and_existing_indexes();
	void effect_columns_only_display_precompute_details();
	void precompute_categories_and_unknown_effects_display_consistently();

	// Bin-derived fallbacks, conflict handling and row refresh notifications.
	void fills_missing_owned_metadata_in_both_identity_forms();
	void preserves_scanner_metadata_and_ignores_source_names();
	void conflicts_are_independent_and_retractable();
	void same_bin_name_with_different_uid_is_ambiguous();
	void incomplete_bins_cannot_supply_metadata();
	void rescans_and_removals_preserve_provenance();

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
		mf.filePath = QStringLiteral("/fake/row%1.mxf").arg(i);
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
		paths << mf.filePath;
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
	MediaFile f;
	f.fileName = QStringLiteral("V01.abc.mxf");
	f.volumeName = QStringLiteral("EDIT");
	f.mediaFolderName = QStringLiteral("8646");
	f.filePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/8646/V01.abc.mxf");

	MediaTableModel model;
	model.setMediaFiles({f});
	const QModelIndex idx =
		model.index(0, Enum::to_underlying(MediaTableModel::Column::Location));

	QCOMPARE(model.data(idx, Qt::DisplayRole).toString(), f.filePath);
	QCOMPARE(model.headerData(Enum::to_underlying(MediaTableModel::Column::Location),
							  Qt::Horizontal, Qt::DisplayRole)
				 .toString(),
			 QStringLiteral("Location"));
}

void TestMediaTableModel::unknown_created_date_displays_blank()
{
	MediaFile withDate;
	withDate.filePath = QStringLiteral("/vol/a.mxf");
	withDate.created = QDateTime(QDate(2026, 7, 20), QTime(12, 30));

	MediaFile withoutDate;
	withoutDate.filePath = QStringLiteral("/vol/b.mxf");
	// created left invalid — a filesystem that records no birth time.

	MediaTableModel m;
	m.setMediaFiles({withDate, withoutDate});
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
	m.setMediaFiles({unlisted});
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
	QVERIFY(!unknown.needsHeaderRead);
	QVERIFY(!unknown.databaseMetadataCurrent);

	MediaFile video;
	video.kind = MediaFile::Kind::Video;
	video.type = MediaFile::Type::Media;
	MediaFile audio;
	audio.kind = MediaFile::Kind::Audio;
	audio.type = MediaFile::Type::Precompute;
	MediaTableModel model;
	model.setMediaFiles({unknown, video, audio});
	const int kind = int(MediaTableModel::Column::Kind);
	const int type = int(MediaTableModel::Column::Type);
	QCOMPARE(model.index(0, kind).data().toString(), QStringLiteral("\u2014"));
	QCOMPARE(model.index(0, type).data().toString(), QStringLiteral("\u2014"));
	QVERIFY(!model.index(0, kind).data(Qt::ToolTipRole).toString().isEmpty());
	QVERIFY(!model.index(0, type).data(Qt::ToolTipRole).toString().isEmpty());
	QCOMPARE(model.index(1, kind).data().toString(), QStringLiteral("Video"));
	QCOMPARE(model.index(1, type).data().toString(), QStringLiteral("Media"));
	QCOMPARE(model.index(2, kind).data().toString(), QStringLiteral("Audio"));
	QCOMPARE(model.index(2, type).data().toString(), QStringLiteral("Precompute"));
}

void TestMediaTableModel::effect_gate_preserves_rows_and_existing_indexes()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(2));
	const QPersistentModelIndex row(model.index(1, int(MediaTableModel::Column::Location)));
	const QString path = row.data().toString();
	QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
	QSignalSpy inserted(&model, &QAbstractItemModel::columnsInserted);
	QSignalSpy removed(&model, &QAbstractItemModel::columnsRemoved);
	QVERIFY(!model.effectDetailsEnabled());
	QCOMPARE(model.columnCount(), 15);
	QVERIFY(!model.index(0, int(MediaTableModel::Column::Effect)).isValid());
	QVERIFY(!model.headerData(int(MediaTableModel::Column::Effect), Qt::Horizontal, Qt::DisplayRole).isValid());
	model.setEffectDetailsEnabled(true);
	QCOMPARE(model.columnCount(), 19);
	QCOMPARE(inserted.size(), 1);
	QCOMPARE(inserted.first().at(1).toInt(), 15);
	QCOMPARE(inserted.first().at(2).toInt(), 18);
	QVERIFY(row.isValid());
	QCOMPARE(row.data().toString(), path);
	const QPersistentModelIndex effect(model.index(1, int(MediaTableModel::Column::Effect)));
	model.setEffectDetailsEnabled(true);
	QCOMPARE(inserted.size(), 1);
	model.setEffectDetailsEnabled(false);
	model.setEffectDetailsEnabled(false);
	QCOMPARE(model.columnCount(), 15);
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
	precompute.effect = QStringLiteral("Custom, exact name");
	precompute.effectCategory = QStringLiteral("Category");
	precompute.effectSequence = QStringLiteral("Sequence");
	MediaFile media = precompute;
	media.type = MediaFile::Type::Media;
	MediaFile unknown = precompute;
	unknown.type = MediaFile::Type::Unknown;
	MediaTableModel model;
	model.setMediaFiles({precompute, media, unknown});
	model.setEffectDetailsEnabled(true);
	const QStringList headers{QStringLiteral("Precompute Category"), QStringLiteral("Effect Category"), QStringLiteral("Effect"), QStringLiteral("Effect Sequence")};
	const QStringList values{QStringLiteral("unknown"), precompute.effectCategory, precompute.effect, precompute.effectSequence};
	for (int i = 0; i < values.size(); ++i)
	{
		const int column = int(MediaTableModel::Column::PrecomputeCategory) + i;
		QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(), headers[i]);
		QCOMPARE(model.index(0, column).data().toString(), values[i]);
		QVERIFY(model.index(1, column).data().toString().isEmpty());
		QVERIFY(model.index(2, column).data().toString().isEmpty());
	}
	model.setEffectDetailsEnabled(false);
	QCOMPARE(model.index(0, int(MediaTableModel::Column::Type)).data().toString(), QStringLiteral("Precompute"));
}

void TestMediaTableModel::precompute_categories_and_unknown_effects_display_consistently()
{
	MediaFile rendered, title, unknown;
	rendered.type = title.type = unknown.type = MediaFile::Type::Precompute;
	rendered.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	title.precomputeCategory = MediaFile::PrecomputeCategory::TitlesAndMatteKeys;
	MediaTableModel model;
	model.setMediaFiles({rendered, title, unknown, MediaFile{}});
	model.setEffectDetailsEnabled(true);
	const int category = int(MediaTableModel::Column::PrecomputeCategory);
	QCOMPARE(model.index(0, category).data().toString(), QStringLiteral("Rendered Effects"));
	QCOMPARE(model.index(1, category).data().toString(), QStringLiteral("Titles and Matte Keys"));
	QCOMPARE(model.index(2, category).data().toString(), QStringLiteral("unknown"));
	for (auto column : {MediaTableModel::Column::Effect, MediaTableModel::Column::EffectCategory})
		for (int row = 0; row < 3; ++row)
			QCOMPARE(model.index(row, int(column)).data().toString(), QStringLiteral("unknown"));
	QVERIFY(model.index(3, category).data().toString().isEmpty());
}

void TestMediaTableModel::fills_missing_owned_metadata_in_both_identity_forms()
{
	MediaTableModel model;
	MediaFile little = row();
	little.masterMobId = masterId();
	model.setMediaFiles({little, row(QStringLiteral("/media/second.mxf"))});
	QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
	model.setAvbBins({bin()});
	QCOMPARE(changed.size(), 1);
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

void TestMediaTableModel::preserves_scanner_metadata_and_ignores_source_names()
{
	MediaTableModel model;
	MediaFile known = row();
	known.clipName = QStringLiteral("Header clip");
	known.clipNameSource = MediaFile::ClipNameSource::MaterialPackage;
	known.originalBin = QStringLiteral("Recorded bin");
	model.setMediaFiles({known});
	model.setAvbBins({bin()});
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, known.clipName);
	QCOMPARE(model.fileAt(0).clipNameSource, known.clipNameSource);
	QCOMPARE(model.fileAt(0).originalBin, known.originalBin);
	QVERIFY(!model.fileAt(0).originalBinFromAvb);

	AvbBin source = bin();
	source.mobs[0].mobType = 3;
	model.setMediaFiles({row()});
	model.setAvbBins({source});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	MediaFile noMaster = row();
	noMaster.mobId = noMaster.masterMobId;
	noMaster.masterMobId.clear();
	model.setMediaFiles({noMaster});
	model.setAvbBins({bin()});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
}

void TestMediaTableModel::conflicts_are_independent_and_retractable()
{
	MediaTableModel model;
	model.setMediaFiles({row()});
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
	model.setMediaFiles({row()});
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QStringLiteral("Original rushes"),
								 QStringLiteral("0000000100000003"))});
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
	QCOMPARE(model.fileAt(0).clipName, QStringLiteral("Edited clip"));
	// A missing display name cannot erase evidence of a different owning bin.
	model.setAvbBins({bin(), bin(QStringLiteral("Edited clip"), QString(),
								 QStringLiteral("0000000100000003"))});
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
}

void TestMediaTableModel::incomplete_bins_cannot_supply_metadata()
{
	MediaTableModel model;
	model.setMediaFiles({row()});
	AvbBin unsupported = bin();
	unsupported.complete = false;
	AvbBin invalid = bin();
	invalid.valid = false;
	model.setAvbBins({unsupported, invalid});
	QVERIFY(model.fileAt(0).clipName.isEmpty());
	QVERIFY(model.fileAt(0).originalBin.isEmpty());
}

void TestMediaTableModel::rescans_and_removals_preserve_provenance()
{
	MediaTableModel model;
	model.setAvbBins({bin()});
	model.setMediaFiles({row(), row(QStringLiteral("/media/new.mxf"))});
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
	model.setMediaFiles({refreshed});
	model.setAvbBins({});
	QCOMPARE(model.fileAt(0).clipName, refreshed.clipName);
	QCOMPARE(model.fileAt(0).clipNameSource, MediaFile::ClipNameSource::Mdb);
	QCOMPARE(model.fileAt(0).originalBin, refreshed.originalBin);
}

QTEST_GUILESS_MAIN(TestMediaTableModel)
#include "tst_mediatablemodel.moc"
