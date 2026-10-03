// MediaFilterProxy search behaviour across Unicode normalisation forms.
// macOS volumes often hand back decomposed (NFD) filenames — 'é' stored as
// 'e' + combining acute — which render identically to composed (NFC)
// keyboard input but are different code points. Search must treat the two
// forms as the same text while keeping accents themselves significant.
// Raw escape sequences throughout so source-file encoding can't drift what
// the assertions actually test.

#include "enumutil.h"
#include "mediafile.h"
#include "mediafilterproxy.h"
#include "mediatablemodel.h"
#include "mobid.h"
#include "testavb.h"

#include <QTest>

#include <algorithm>
#include <array>
#include <initializer_list>

namespace
{
	BinFileReferences fullIds(std::initializer_list<QString> ids)
	{
		return {QSet<QString>(ids), {}};
	}

	MediaFile rowNamed(const QString &clipName)
	{
		MediaFile f;
		f.clipName = clipName;
		f.fileName = clipName + QStringLiteral(".mxf");
		f.mediaFilePath = QStringLiteral("/vol/") + f.fileName;
		return f;
	}

	// "café" composed: one precomposed é (U+00E9).
	const QString kCafeNfc = QStringLiteral("café");
	// "café" decomposed: 'e' + combining acute (U+0301) — what APFS/SMB
	// paths frequently contain.
	const QString kCafeNfd = QStringLiteral("café");
} // namespace

class TestMediaFilterProxy : public QObject
{
	Q_OBJECT
private slots:
	void unicode_search_normalises_and_folds_data();
	void unicode_search_normalises_and_folds();
	void plain_ascii_never_matches_accents();

	// Sorting. The Size column sorts on exact byte counts, not the
	// rounded MB display string — two files that both show "850.0 MB"
	// still order by their real sizes, and no double rounding sits
	// between the user and the answer.
	void size_column_sorts_on_exact_bytes();
	void sample_rate_column_sorts_numerically();
	void bit_depth_column_sorts_numbers_before_labels();
	void duration_column_sorts_displayed_timecode();

	// Search covers the path (2026-08-18). The Location column shows the
	// full path, so the search box has to match it — and because the
	// filename and the Avid folder are both substrings of the path, they
	// keep matching without a pass of their own.
	void search_matches_the_path_shown_in_the_location_column();
	void unknown_classification_does_not_match_known_filters();
	void quarantined_filter_uses_scanner_flag();
	void three_state_classification_sort_is_consistent();
	void type_sorting_survives_precompute_gate_changes();
	void effect_selection_intersects_volume_and_existing_filters();
	void precomputes_gate_resets_filters_and_hidden_search();
	void effect_columns_sort_displayed_values();
	void precompute_hierarchy_filters_intersect_and_unknown_is_selectable();
	void precompute_tree_unites_branches_and_preserves_complete_paths();
	void precompute_tree_empty_and_unknown_are_not_wildcards();
	void bin_filter_excludes_master_only_relatives();
	void bin_filter_compares_full_ids();
	void bin_filter_falls_back_only_for_legacy_identities();
	void bin_filter_rejects_malformed_file_ids();
	void bin_subtraction_only_uses_file_identity_data();
	void bin_subtraction_only_uses_file_identity();
	void bin_ordered_add_can_restore_a_row();
	void bin_leading_subtract_uses_all_media_rows();
	void bin_empty_operand_leaves_filter_unchanged();
	void bin_expression_intersects_search_and_survives_model_refresh();
	void unchanged_bin_criteria_do_not_refilter_rows();
};

void TestMediaFilterProxy::unicode_search_normalises_and_folds_data()
{
	QVERIFY(kCafeNfc != kCafeNfd);
	QTest::addColumn<QString>("name");
	QTest::addColumn<QString>("search");
	QTest::newRow("nfc-search-finds-nfd-row") << kCafeNfd << kCafeNfc;
	QTest::newRow("nfd-search-finds-nfc-row") << kCafeNfc << kCafeNfd;
	QTest::newRow("case-fold-and-normalise") << QStringLiteral("CAFÉ REEL 7") << kCafeNfc;
}

void TestMediaFilterProxy::unicode_search_normalises_and_folds()
{
	QFETCH(QString, name);
	QFETCH(QString, search);
	MediaTableModel model;
	model.setMediaFiles({rowNamed(name)});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	proxy.setSearchText(search);
	QCOMPARE(proxy.rowCount(), 1);
}

void TestMediaFilterProxy::plain_ascii_never_matches_accents()
{
	// Accents stay significant; only the FORM is insensitive. This must
	// hold for both storage forms — before the fix an NFD row matched the
	// bare-ASCII needle ("cafe" is literally a prefix of "cafe" + accent)
	// while an NFC row didn't, so results depended on which volume a file
	// came from.
	MediaTableModel model;
	model.setMediaFiles({rowNamed(kCafeNfc), rowNamed(kCafeNfd)});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	proxy.setSearchText(QStringLiteral("cafe"));
	QCOMPARE(proxy.rowCount(), 0);

	// Sanity: the accented needle still finds both rows.
	proxy.setSearchText(kCafeNfc);
	QCOMPARE(proxy.rowCount(), 2);
}

void TestMediaFilterProxy::search_matches_the_path_shown_in_the_location_column()
{
	MediaTableModel model;
	MediaFile a = rowNamed(QStringLiteral("Scene 1"));
	a.mediaFilePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/8646/V01.abc.mxf");
	a.fileName = QStringLiteral("V01.abc.mxf");
	a.mediaFolderName = QStringLiteral("8646");
	a.volumeName = QStringLiteral("EDIT");
	MediaFile b = rowNamed(QStringLiteral("Scene 2"));
	b.mediaFilePath = QStringLiteral("/Volumes/BACKUP/Avid MediaFiles/MXF/1/V02.def.mxf");
	b.fileName = QStringLiteral("V02.def.mxf");
	b.mediaFolderName = QStringLiteral("1");
	b.volumeName = QStringLiteral("BACKUP");
	model.setMediaFiles({a, b});

	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	// A folder name, a filename fragment and a whole path segment all live
	// inside mediaFilePath, so one match pass covers them.
	proxy.setSearchText(QStringLiteral("8646"));
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText(QStringLiteral("V02.def"));
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText(QStringLiteral("Avid MediaFiles/MXF/1/"));
	QCOMPARE(proxy.rowCount(), 1);

	// The volume name is matched in its own right: a Windows path
	// ("E:/...") need not contain the label the user knows it by.
	proxy.setSearchText(QStringLiteral("BACKUP"));
	QCOMPARE(proxy.rowCount(), 1);
}

void TestMediaFilterProxy::size_column_sorts_on_exact_bytes()
{
	const auto sized = [](const QString &name, qint64 bytes)
	{
		MediaFile f = rowNamed(name);
		f.sizeBytes = bytes;
		return f;
	};

	// The two 850.0-rounding neighbours differ by 400 bytes: the display
	// string cannot tell them apart, the sort must.
	MediaTableModel model;
	model.setMediaFiles({sized(QStringLiteral("big"), 1'100'000'000),
						 sized(QStringLiteral("mid_hi"), 850'000'400),
						 sized(QStringLiteral("mid_lo"), 850'000'000),
						 sized(QStringLiteral("empty"), 0)});

	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int sizeCol = Enum::to_underlying(MediaTableModel::Column::SizeMB);
	const int nameCol = Enum::to_underlying(MediaTableModel::Column::ClipName);
	proxy.sort(sizeCol, Qt::AscendingOrder);
	QCOMPARE(proxy.rowCount(), 4);

	QStringList order;
	for (int r = 0; r < proxy.rowCount(); ++r)
		order << proxy.data(proxy.index(r, nameCol)).toString();
	QCOMPARE(order, (QStringList{QStringLiteral("empty"), QStringLiteral("mid_lo"),
								 QStringLiteral("mid_hi"), QStringLiteral("big")}));

	// The two neighbours really do render identically — proof the order
	// above cannot have come from the display string.
	QCOMPARE(model.fileAt(1).sizeMBDisplay(), model.fileAt(2).sizeMBDisplay());
}

void TestMediaFilterProxy::sample_rate_column_sorts_numerically()
{
	QVector<MediaFile> files;
	for (const int rate : {192000, 48000, 0, 96000, 44100})
	{
		auto file = rowNamed(QString::number(rate));
		file.kind = MediaFile::Kind::Audio;
		file.sampleRate = rate;
		files.append(file);
	}
	MediaTableModel model;
	model.setMediaFiles(files);
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int column = int(MediaTableModel::Column::SampleRate);
	QVector<int> expected{0, 44100, 48000, 96000, 192000};
	for (const auto direction : {Qt::AscendingOrder, Qt::DescendingOrder})
	{
		proxy.sort(column, direction);
		QVector<int> actual;
		for (int row = 0; row < proxy.rowCount(); ++row)
			actual.append(model.fileAt(proxy.mapToSource(proxy.index(row, column)).row()).sampleRate);
		QCOMPARE(actual, expected);
		std::reverse(expected.begin(), expected.end());
	}
}

void TestMediaFilterProxy::bit_depth_column_sorts_numbers_before_labels()
{
	QVector<MediaFile> files;
	for (const auto *depth : {"Float", "24-bit", "Unknown", "8-bit", "32-bit", "", "16-bit", "10-bit"})
	{
		auto file = rowNamed(QString::number(files.size()));
		file.bitDepth = QString::fromLatin1(depth);
		files.append(file);
	}
	MediaTableModel model;
	model.setMediaFiles(files);
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int column = int(MediaTableModel::Column::BitDepth);
	QStringList expected{"", "8-bit", "10-bit", "16-bit", "24-bit", "32-bit", "Float", "Unknown"};
	for (const auto direction : {Qt::AscendingOrder, Qt::DescendingOrder})
	{
		proxy.sort(column, direction);
		QStringList actual;
		for (int row = 0; row < proxy.rowCount(); ++row)
			actual.append(proxy.index(row, column).data().toString());
		QCOMPARE(actual, expected);
		std::reverse(expected.begin(), expected.end());
	}
}

void TestMediaFilterProxy::unknown_classification_does_not_match_known_filters()
{
	MediaFile unknown = rowNamed(QStringLiteral("unread"));
	MediaFile video = rowNamed(QStringLiteral("picture"));
	video.kind = MediaFile::Kind::Video;
	video.type = MediaFile::Type::Media;
	MediaFile audio = rowNamed(QStringLiteral("sound"));
	audio.kind = MediaFile::Kind::Audio;
	audio.type = MediaFile::Type::Precompute;
	MediaTableModel model;
	model.setMediaFiles({unknown, video, audio});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int name = int(MediaTableModel::Column::ClipName);
	QCOMPARE(proxy.rowCount(), 3);
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Video);
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.index(0, name).data().toString(), QStringLiteral("picture"));
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Audio);
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.index(0, name).data().toString(), QStringLiteral("sound"));
	proxy.setPrecomputesEnabled(true);
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Precompute);
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.index(0, name).data().toString(), QStringLiteral("sound"));
	proxy.setFilterMode(MediaFilterProxy::FilterMode::All);
	proxy.setSearchText(QStringLiteral("unread"));
	QCOMPARE(proxy.rowCount(), 1); // unresolved files remain accessible
}

void TestMediaFilterProxy::quarantined_filter_uses_scanner_flag()
{
	// Deliberately disagree with the paths: the scanner owns classification,
	// and the filter must consume its flag without guessing from folder names.
	MediaFile flagged = rowNamed(QStringLiteral("flagged"));
	flagged.isQuarantined = true;
	flagged.mediaFolderName = QStringLiteral("1");
	flagged.mediaFilePath = QStringLiteral("/vol/Avid MediaFiles/MXF/1/flagged.mxf");
	MediaFile namedOnly = rowNamed(QStringLiteral("folder name only"));
	namedOnly.mediaFolderName = QStringLiteral("Quarantined Files");
	namedOnly.mediaFilePath = QStringLiteral("/vol/Avid MediaFiles/MXF/Quarantined Files/named.mxf");
	MediaTableModel model;
	model.setMediaFiles({flagged, namedOnly});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int name = Enum::to_underlying(MediaTableModel::Column::ClipName);
	QCOMPARE(proxy.rowCount(), 2);

	proxy.setFilterMode(MediaFilterProxy::FilterMode::Quarantined);
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.index(0, name).data().toString(), flagged.clipName);

	proxy.setFilterMode(MediaFilterProxy::FilterMode::All);
	QCOMPARE(proxy.rowCount(), 2);
	QCOMPARE(proxy.index(0, name).data().toString(), flagged.clipName);
	QCOMPARE(proxy.index(1, name).data().toString(), namedOnly.clipName);
}

void TestMediaFilterProxy::duration_column_sorts_displayed_timecode()
{
	using Kind = MediaFile::Kind;
	// Expected ascending order compares the displayed fields numerically.
	// Equal fields use the existing Audio, Video, Unknown order, regardless
	// of rate or whether the displayed separator is a colon or semicolon.
	const struct
	{
		const char *name;
		qint64 frames;
		int base;
		bool drop;
		Kind kind;
		const char *display;
		const char *frameRate = "";
	} cases[] = {
		{"blank frames", 0, 25, false, Kind::Audio, ""},
		{"blank rate", 250, 0, false, Kind::Video, ""},
		{"negative frames", -25, 25, false, Kind::Unknown, ""},
		{"30 frames/s", 10, 30, false, Kind::Video, "00:00:00:10"},
		{"25 frames/s", 11, 25, false, Kind::Video, "00:00:00:11"},
		{"equal audio", 12, 24, false, Kind::Audio, "00:00:00:12"},
		{"equal video", 12, 60, false, Kind::Video, "00:00:00:12"},
		{"equal unknown", 12, 25, false, Kind::Unknown, "00:00:00:12"},
		// Raw FF order, not FF/base: 12 at 24 frames/s precedes 20 at 60 frames/s.
		{"60 frames/s", 20, 60, false, Kind::Video, "00:00:00:20"},
		{"two digit frame field", 99, 120, false, Kind::Video, "00:00:00:99"},
		{"three digit frame field", 100, 120, false, Kind::Video, "00:00:00:100"},
		{"frameRate fallback", 25, 0, false, Kind::Video, "00:00:01:00", "25"},
		{"60 DF before drop", 3599, 60, true, Kind::Video, "00;00;59;59"},
		{"60 NDF at drop", 3603, 60, false, Kind::Video, "00:01:00:03"},
		{"60 DF at drop", 3600, 60, true, Kind::Audio, "00;01;00;04"},
		{"60 NDF equal fields", 3604, 60, false, Kind::Video, "00:01:00:04"},
		// The original mixed DF/NDF regression: the larger raw frame count
		// displays a shorter timecode and must sort first.
		{"30 NDF before ten minutes", 17990, 30, false, Kind::Video, "00:09:59:20"},
		{"30 DF ten minutes", 17982, 30, true, Kind::Video, "00;10;00;00"},
		{"99 hours", 8'999'999, 25, false, Kind::Video, "99:59:59:24"},
		{"100 hours", 9'000'000, 25, false, Kind::Video, "100:00:00:00"},
	};
	QVector<MediaFile> rows;
	QStringList ascending;
	for (const auto &c : cases)
	{
		MediaFile row = rowNamed(QString::fromLatin1(c.name));
		const int durationBase = c.base > 0 ? c.base : QString::fromLatin1(c.frameRate).toInt();
		row.duration = {c.frames, {durationBase, 1}, {durationBase, 1}, MediaDuration::Source::Descriptor};
		row.timecodeBase = c.base;
		row.dropFrame = c.drop;
		row.kind = c.kind;
		row.frameRate = QString::fromLatin1(c.frameRate);
		QCOMPARE(row.durationDisplay(), QString::fromLatin1(c.display));
		rows.append(row);
		ascending.append(row.clipName);
	}
	std::reverse(rows.begin(), rows.end());
	MediaTableModel model;
	model.setMediaFiles(rows);
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	for (const auto direction : {Qt::AscendingOrder, Qt::DescendingOrder})
	{
		proxy.sort(int(MediaTableModel::Column::Duration), direction);
		QStringList actual;
		for (int i = 0; i < proxy.rowCount(); ++i)
			actual.append(proxy.index(i, int(MediaTableModel::Column::ClipName)).data().toString());
		QStringList expected = ascending;
		if (direction == Qt::DescendingOrder)
			std::reverse(expected.begin(), expected.end());
		QCOMPARE(actual, expected);
	}
}

void TestMediaFilterProxy::three_state_classification_sort_is_consistent()
{
	MediaFile audio = rowNamed(QStringLiteral("audio"));
	audio.kind = MediaFile::Kind::Audio;
	audio.type = MediaFile::Type::Media;
	MediaFile video = rowNamed(QStringLiteral("video"));
	video.kind = MediaFile::Kind::Video;
	video.type = MediaFile::Type::Precompute;
	MediaFile unknown = rowNamed(QStringLiteral("unknown"));
	std::array<MediaFile, 3> rows{audio, video, unknown};
	for (auto &row : rows)
	{
		row.duration = {250, {25, 1}, {25, 1}, MediaDuration::Source::Descriptor};
		row.timecodeBase = 25;
	}
	std::array<int, 3> order{0, 1, 2};
	do
	{
		MediaTableModel model;
		model.setMediaFiles({rows[order[0]], rows[order[1]], rows[order[2]]});
		MediaFilterProxy proxy;
		proxy.setSourceModel(&model);
		proxy.setPrecomputesEnabled(true);
		for (const auto column : {MediaTableModel::Column::Kind, MediaTableModel::Column::Type,
								  MediaTableModel::Column::Duration})
		{
			for (const auto direction : {Qt::AscendingOrder, Qt::DescendingOrder})
			{
				proxy.sort(int(column), direction);
				QStringList names;
				for (int i = 0; i < proxy.rowCount(); ++i)
					names << proxy.index(i, int(MediaTableModel::Column::ClipName)).data().toString();
				QStringList expected{QStringLiteral("audio"), QStringLiteral("video"), QStringLiteral("unknown")};
				if (direction == Qt::DescendingOrder)
					std::reverse(expected.begin(), expected.end());
				QCOMPARE(names, expected);
			}
		}
	} while (std::next_permutation(order.begin(), order.end()));
}

void TestMediaFilterProxy::type_sorting_survives_precompute_gate_changes()
{
	MediaFile render = rowNamed(QStringLiteral("render"));
	render.type = MediaFile::Type::Precompute;
	MediaFile media = rowNamed(QStringLiteral("ordinary"));
	media.type = MediaFile::Type::Media;
	MediaTableModel model;
	model.setMediaFiles({render, media});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const int typeColumn = int(MediaTableModel::Column::Type);
	proxy.sort(typeColumn);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
	model.setPrecomputesEnabled(true);
	proxy.setPrecomputesEnabled(true);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
	model.setPrecomputesEnabled(false);
	proxy.setPrecomputesEnabled(false);
	QCOMPARE(proxy.sortColumn(), typeColumn);
	QVERIFY(proxy.index(0, typeColumn).isValid());
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
	proxy.sort(typeColumn, Qt::DescendingOrder);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 0);
	QCOMPARE(proxy.rowCount(), 2); // Toggling detail columns never removes media rows.
}

void TestMediaFilterProxy::effect_selection_intersects_volume_and_existing_filters()
{
	MediaFile title = rowNamed(QStringLiteral("picture"));
	title.type = MediaFile::Type::Precompute;
	title.kind = MediaFile::Kind::Video;
	title.effect = QStringLiteral("Title");
	title.volumeName = QStringLiteral("EDIT");
	title.volumePath = QStringLiteral("/Volumes/EDIT");
	title.project = QStringLiteral("Project A");
	title.fileMobId = MobId::format(TestAvb::Source);
	MediaFile custom = title;
	custom.clipName = QStringLiteral("sound");
	custom.fileName = QStringLiteral("sound.mxf");
	custom.mediaFilePath = QStringLiteral("/vol/sound.mxf");
	custom.kind = MediaFile::Kind::Audio;
	custom.effect = QStringLiteral("Custom, exact name");
	custom.fileMobId = MobId::format(TestAvb::Other);
	MediaFile otherVolume = title;
	otherVolume.volumePath = QStringLiteral("/Volumes/EDIT 2"); // same displayed label
	MediaFile otherProject = title;
	otherProject.project = QStringLiteral("Project B");
	MediaFile ordinary = title;
	ordinary.type = MediaFile::Type::Media; // stale details must not imply a render
	MediaFile unknown = title;
	unknown.type = MediaFile::Type::Unknown;
	MediaTableModel model;
	model.setMediaFiles({title, custom, otherVolume, otherProject, ordinary, unknown});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setPrecomputesEnabled(true);
	proxy.setPrecomputeTreeFilter({true, {{{}, {}, QStringLiteral("Title")}, {{}, {}, QStringLiteral("Custom, exact name")}}});
	QCOMPARE(proxy.rowCount(), 4); // OR across names, proven precomputes only
	proxy.setPrecomputeVolumeFilter(title.volumePath);
	QCOMPARE(proxy.rowCount(), 3);
	proxy.setProjectFilter({title.project});
	QCOMPARE(proxy.rowCount(), 2);
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Audio);
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({title.fileMobId})}}});
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({custom.fileMobId})}}});
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText(QStringLiteral("picture"));
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setSearchText(QStringLiteral("sound"));
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText({});
	proxy.setBinFilter({});
	proxy.setFilterMode(MediaFilterProxy::FilterMode::All);
	proxy.setProjectFilter({});
	proxy.setPrecomputeTreeFilter({true, {{{}, {}, QStringLiteral("title")}}});
	QCOMPARE(proxy.rowCount(), 0); // names are exact, not fuzzy or case folded
	proxy.setPrecomputeTreeFilter({});
	QCOMPARE(proxy.rowCount(), 3); // volume alone still means precomputes
	proxy.setPrecomputeVolumeFilter({});
	QCOMPARE(proxy.rowCount(), 6); // neither selection means no precompute filter
}

void TestMediaFilterProxy::precomputes_gate_resets_filters_and_hidden_search()
{
	MediaFile render = rowNamed(QStringLiteral("render"));
	render.type = MediaFile::Type::Precompute;
	render.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	render.effect = QStringLiteral("Exclusive token");
	render.effectCategory = QStringLiteral("Exclusive category");
	render.effectSequence = QStringLiteral("Exclusive sequence");
	render.volumePath = QStringLiteral("/Volumes/EDIT");
	MediaFile media = rowNamed(QStringLiteral("ordinary"));
	media.type = MediaFile::Type::Media;
	media.effect = render.effect;
	MediaTableModel model;
	model.setMediaFiles({render, media});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	struct GateCase
	{
		PrecomputeFilter filter;
		int expectedRows;
	};
	const GateCase cases[] = {
		{{true, {{{}, {}, render.effect}}}, 1}, // A named selection.
		{{true, {}}, 0},						// An active empty selection means no matches.
	};
	QVERIFY(!proxy.precomputesEnabled());
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Precompute);
	QCOMPARE(proxy.rowCount(), 2); // Hidden mode cannot activate programmatically.
	for (const auto &test : cases)
	{
		proxy.setPrecomputeTreeFilter(test.filter);
		proxy.setPrecomputeVolumeFilter(render.volumePath);
		QVERIFY(!proxy.precomputeTreeFilter().active);
		QVERIFY(proxy.precomputeTreeFilter().paths.isEmpty());
		QVERIFY(proxy.precomputeVolumeFilter().isEmpty());
		QCOMPARE(proxy.rowCount(), 2);
	}
	for (const auto &text : {render.precomputeCategoryDisplay(), render.effect, render.effectCategory, render.effectSequence})
	{
		proxy.setSearchText(text);
		QCOMPARE(proxy.rowCount(), 0);
		proxy.setPrecomputesEnabled(true);
		QCOMPARE(proxy.rowCount(), 1);
		proxy.setPrecomputesEnabled(false);
		QCOMPARE(proxy.rowCount(), 0);
	}
	proxy.setSearchText({});
	proxy.setPrecomputesEnabled(true);
	for (const auto &test : cases)
	{
		proxy.setFilterMode(MediaFilterProxy::FilterMode::Precompute);
		QCOMPARE(proxy.rowCount(), 1);
		proxy.setPrecomputeTreeFilter(test.filter);
		QVERIFY(proxy.precomputeTreeFilter().active);
		QCOMPARE(proxy.precomputeTreeFilter().paths.size(), test.filter.paths.size());
		proxy.setPrecomputeVolumeFilter(render.volumePath);
		QCOMPARE(proxy.rowCount(), test.expectedRows);
		proxy.setPrecomputesEnabled(false);
		QCOMPARE(proxy.rowCount(), 2);
		QVERIFY(!proxy.precomputeTreeFilter().active);
		QVERIFY(proxy.precomputeTreeFilter().paths.isEmpty());
		QVERIFY(proxy.precomputeVolumeFilter().isEmpty());
		proxy.setFilterMode(MediaFilterProxy::FilterMode::Precompute);
		QCOMPARE(proxy.rowCount(), 2); // Hidden mode remains inactive.
		proxy.setPrecomputesEnabled(true);
		QCOMPARE(proxy.rowCount(), 2); // Mode and selections stay reset when re-enabled.
	}

	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Titles and Matte Keys"), QStringLiteral("Title"), QStringLiteral("Title")}}});
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setPrecomputeTreeFilter({true, {{}}});
	QCOMPARE(proxy.rowCount(), 1); // A root wildcard includes only proven precomputes.
	proxy.setPrecomputeTreeFilter({true, {{{}, QStringLiteral("Title"), {}}}});
	QVERIFY(proxy.precomputeTreeFilter().active);
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setPrecomputeTreeFilter({true, {{}}});
	proxy.setPrecomputeTreeFilter({});
	QCOMPARE(proxy.rowCount(), 2); // Inactive and active-empty are different states.
	proxy.setPrecomputeTreeFilter({false, {{QStringLiteral("Rendered Effects"), {}, {}}}});
	QVERIFY(proxy.precomputeTreeFilter().paths.isEmpty()); // No stale inactive paths.
	proxy.setPrecomputesEnabled(false);
	proxy.setSearchText(QStringLiteral("render"));
	QCOMPARE(proxy.rowCount(), 1); // Existing visible Clip Name search remains.
}

void TestMediaFilterProxy::effect_columns_sort_displayed_values()
{
	MediaFile alpha = rowNamed(QStringLiteral("alpha"));
	alpha.type = MediaFile::Type::Precompute;
	alpha.effect = alpha.effectCategory = alpha.effectSequence = QStringLiteral("Alpha");
	MediaFile zulu = alpha;
	zulu.clipName = QStringLiteral("zulu");
	zulu.effect = zulu.effectCategory = zulu.effectSequence = QStringLiteral("Zulu");
	MediaFile ordinary = zulu;
	ordinary.clipName = QStringLiteral("ordinary");
	ordinary.type = MediaFile::Type::Media;
	MediaTableModel model;
	model.setMediaFiles({zulu, alpha, ordinary});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	model.setPrecomputesEnabled(true);
	proxy.setPrecomputesEnabled(true);
	for (auto column : {MediaTableModel::Column::Effect, MediaTableModel::Column::EffectCategory, MediaTableModel::Column::EffectSequence})
	{
		proxy.sort(int(column));
		QStringList names;
		for (int i = 0; i < proxy.rowCount(); ++i)
			names << proxy.index(i, int(MediaTableModel::Column::ClipName)).data().toString();
		QCOMPARE(names, (QStringList{QStringLiteral("ordinary"), QStringLiteral("alpha"), QStringLiteral("zulu")}));
	}
	model.setPrecomputesEnabled(false);
	proxy.setPrecomputesEnabled(false);
	QCOMPARE(proxy.columnCount(), 19);
	QCOMPARE(proxy.rowCount(), 3);
}

void TestMediaFilterProxy::precompute_hierarchy_filters_intersect_and_unknown_is_selectable()
{
	MediaFile warp = rowNamed(QStringLiteral("warp"));
	warp.type = MediaFile::Type::Precompute;
	warp.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	warp.effect = QStringLiteral("3D Warp");
	warp.effectCategory = QStringLiteral("Blend");
	warp.volumePath = QStringLiteral("/Volumes/EDIT");
	MediaFile title = warp;
	title.precomputeCategory = MediaFile::PrecomputeCategory::TitlesAndMatteKeys;
	title.effect = title.effectCategory = QStringLiteral("Title");
	MediaFile unresolved = warp;
	unresolved.effect.clear();
	unresolved.effectCategory.clear();
	MediaFile uncertain = unresolved;
	uncertain.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;
	MediaFile otherVolume = warp;
	otherVolume.volumePath = QStringLiteral("/Volumes/OTHER");
	MediaFile ordinary = warp;
	ordinary.type = MediaFile::Type::Media;
	MediaTableModel model;
	model.setMediaFiles({warp, title, unresolved, uncertain, otherVolume, ordinary});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), {}}}});
	QVERIFY(!proxy.precomputeTreeFilter().active);
	proxy.setPrecomputesEnabled(true);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Rendered Effects"), {}, {}}, {QStringLiteral("Titles and Matte Keys"), {}, {}}}});
	QCOMPARE(proxy.rowCount(), 4);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), {}}, {QStringLiteral("Rendered Effects"), QStringLiteral("unknown"), {}}}});
	QCOMPARE(proxy.rowCount(), 3);
	proxy.setPrecomputeVolumeFilter(warp.volumePath);
	QCOMPARE(proxy.rowCount(), 2);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")}}});
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Rendered Effects"), QStringLiteral("unknown"), QStringLiteral("unknown")}}});
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("unknown"), QStringLiteral("unknown"), QStringLiteral("unknown")}}});
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Titles and Matte Keys"), QStringLiteral("unknown"), QStringLiteral("unknown")}}});
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setPrecomputesEnabled(false);
	QCOMPARE(proxy.rowCount(), 6);
	QVERIFY(!proxy.precomputeTreeFilter().active);
	proxy.setPrecomputesEnabled(true);
	proxy.setSearchText(QStringLiteral("Titles and Matte Keys"));
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setPrecomputesEnabled(false);
	QCOMPARE(proxy.rowCount(), 0);
}

void TestMediaFilterProxy::precompute_tree_unites_branches_and_preserves_complete_paths()
{
	MediaFile warp = rowNamed(QStringLiteral("warp"));
	warp.type = MediaFile::Type::Precompute;
	warp.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	warp.effectCategory = QStringLiteral("Blend");
	warp.effect = QStringLiteral("3D Warp");
	warp.volumePath = QStringLiteral("/Volumes/EDIT");
	warp.project = QStringLiteral("Project A");
	warp.fileMobId = MobId::format(TestAvb::Master);
	warp.kind = MediaFile::Kind::Video;
	MediaFile sameNameOtherCategory = warp;
	sameNameOtherCategory.effectCategory = QStringLiteral("Image");
	MediaFile title = warp;
	title.precomputeCategory = MediaFile::PrecomputeCategory::TitlesAndMatteKeys;
	title.effectCategory = title.effect = QStringLiteral("Title");
	title.project = QStringLiteral("Project B");
	title.fileMobId = MobId::format(TestAvb::Source);
	MediaFile matte = title;
	matte.effect = QStringLiteral("Matte Key");
	matte.project = warp.project;
	matte.fileMobId = MobId::format(TestAvb::Other);
	matte.kind = MediaFile::Kind::Audio;
	MediaFile sameNameOtherSubtype = warp;
	sameNameOtherSubtype.precomputeCategory = title.precomputeCategory;
	sameNameOtherSubtype.volumePath = QStringLiteral("/Volumes/OTHER");
	MediaFile ordinary = warp;
	ordinary.type = MediaFile::Type::Media;
	MediaFile unknownSubtype = warp;
	unknownSubtype.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;
	MediaTableModel model;
	model.setMediaFiles({warp, sameNameOtherCategory, title, matte, sameNameOtherSubtype, ordinary, unknownSubtype});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setPrecomputesEnabled(true);
	const PrecomputeFilterPath warpPath{QStringLiteral("Rendered Effects"), QStringLiteral("Blend"), QStringLiteral("3D Warp")};
	proxy.setPrecomputeTreeFilter({true, {warpPath}});
	QCOMPARE(proxy.rowCount(), 1); // same effect text in another branch does not match
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Titles and Matte Keys"), {}, {}}, warpPath}});
	QCOMPARE(proxy.rowCount(), 4); // all title/matte branches OR this rendered effect
	proxy.setPrecomputeVolumeFilter(warp.volumePath);
	QCOMPARE(proxy.rowCount(), 3);
	proxy.setProjectFilter({warp.project});
	QCOMPARE(proxy.rowCount(), 2);
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Video);
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({matte.fileMobId})}}});
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setFilterMode(MediaFilterProxy::FilterMode::All);
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText(QStringLiteral("unrelated"));
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setSearchText(QStringLiteral("Matte Key"));
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setPrecomputeVolumeFilter(QStringLiteral("/Volumes/NOT SCANNED"));
	QCOMPARE(proxy.rowCount(), 0);
	QCOMPARE(proxy.precomputeTreeFilter().paths.size(), 2); // volume never erases choices
}

void TestMediaFilterProxy::precompute_tree_empty_and_unknown_are_not_wildcards()
{
	MediaFile render = rowNamed(QStringLiteral("render"));
	render.type = MediaFile::Type::Precompute;
	render.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	render.volumePath = QStringLiteral("/Volumes/EDIT");
	MediaFile unknownSubtype = render;
	unknownSubtype.precomputeCategory = MediaFile::PrecomputeCategory::Unknown;
	unknownSubtype.effectCategory = QStringLiteral("Blend");
	unknownSubtype.effect = QStringLiteral("3D Warp");
	MediaFile ordinary = render;
	ordinary.type = MediaFile::Type::Media;
	MediaFile unknownType = render;
	unknownType.type = MediaFile::Type::Unknown;
	MediaTableModel model;
	model.setMediaFiles({render, unknownSubtype, ordinary, unknownType});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setPrecomputesEnabled(true);
	proxy.setPrecomputeTreeFilter({true, {}});
	QCOMPARE(proxy.rowCount(), 0);
	QVERIFY(proxy.precomputeTreeFilter().active);
	proxy.setPrecomputeVolumeFilter(render.volumePath);
	QCOMPARE(proxy.rowCount(), 0); // no ticks remains no matches, even on a chosen volume
	proxy.setPrecomputeTreeFilter({true, {{}}});
	QCOMPARE(proxy.rowCount(), 2); // root means proven precomputes only
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("unknown"), {}, {}}}});
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Rendered Effects"), QStringLiteral("unknown"), QStringLiteral("unknown")}}});
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 0);
	proxy.setPrecomputeTreeFilter({true, {{QStringLiteral("Rendered Effects"), QStringLiteral("Unknown"), {}}}});
	QCOMPARE(proxy.rowCount(), 0); // display values, including unknown, are exact
	proxy.setPrecomputeTreeFilter({});
	QCOMPARE(proxy.rowCount(), 2); // the independent volume still requires a precompute
	proxy.setPrecomputeVolumeFilter({});
	QCOMPARE(proxy.rowCount(), 4);
}

void TestMediaFilterProxy::bin_filter_excludes_master_only_relatives()
{
	MediaFile video = rowNamed(QStringLiteral("video"));
	video.fileMobId = MobId::format(TestAvb::Source);
	video.masterMobId = MobId::format(TestAvb::Master);
	MediaFile audio = rowNamed(QStringLiteral("audio"));
	audio.fileMobId = MobId::format(TestAvb::Other);
	audio.masterMobId = video.masterMobId;
	MediaTableModel model;
	model.setMediaFiles({video, audio});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	const auto videoKey = MobId::format(TestAvb::Source);
	const auto audioKey = MobId::format(TestAvb::Other);
	const auto masterKey = MobId::format(TestAvb::Master);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({videoKey})}}});
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 0);
	// Even an MSML carrying this ID matches file IDs, never row master IDs.
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({masterKey})}}});
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({videoKey})},
						 {BinFilter::Operation::Add, {}, fullIds({masterKey})}}});
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({videoKey})},
						 {BinFilter::Operation::Add, {}, fullIds({audioKey})}}});
	QCOMPARE(proxy.rowCount(), 2);
}

void TestMediaFilterProxy::bin_filter_compares_full_ids()
{
	const BinFilter filter{{{BinFilter::Operation::Intersect, {}, fullIds({MobId::format(TestAvb::Source)})}}};
	QVERIFY(filter.matches(MobId::format(TestAvb::Source)));
	QVERIFY(filter.matches(QString::fromLatin1(TestAvb::Source.toHex().toUpper())));
	QVERIFY(!filter.matches(MobId::swapMaterialByteOrder(MobId::format(TestAvb::Source))));
	// Every byte matters when both sides have a modern full identity.
	for (int i = 0; i < MobId::kRawSize; ++i)
	{
		auto different = TestAvb::Source;
		different[i] = char(uchar(different[i]) ^ 1);
		QVERIFY(!filter.matches(MobId::format(different)));
	}
}

void TestMediaFilterProxy::bin_filter_falls_back_only_for_legacy_identities()
{
	const QString modern = MobId::format(TestAvb::Source);
	const QString old = QStringLiteral("060a2b3401010101.01010f0013000000.98badcfe32107654.060e2b347f7f2a80");
	const QString wrongOld = QStringLiteral("060a2b3401010101.01010f0013000000.1122334455667788.060e2b347f7f2a80");
	const auto shortId = BinFileId::fromLegacyWords(0xfedcba98, 0x54761032);
	QCOMPARE(shortId.legacyKey, QStringLiteral("98badcfe32107654"));
	QVERIFY(shortId.fullId.isEmpty());
	BinFileReferences legacyRefs;
	legacyRefs.add(shortId);
	const BinFilter oldBin{{{BinFilter::Operation::Intersect, {}, legacyRefs}}};
	QVERIFY(oldBin.matches(modern)); // modern file, old bin
	QVERIFY(oldBin.matches(old));	 // old file, old bin
	QVERIFY(!oldBin.matches(wrongOld));
	QVERIFY(!oldBin.matches(MobId::format(TestAvb::Other)));
	const BinFilter modernBin{{{BinFilter::Operation::Intersect, {}, fullIds({modern})}}};
	QVERIFY(modernBin.matches(old)); // old file, modern bin
	QVERIFY(!modernBin.matches(wrongOld));
	// Both prefix and suffix identify an OMF wrapper. A shared prefix alone
	// cannot turn a different modern ID into a legacy fallback.
	const QString modernWithOldPrefix = QStringLiteral("060a2b3401010101.01010f0013000000.98badcfe32107654.0123456789abcdef");
	QVERIFY(!modernBin.matches(modernWithOldPrefix));
	BinFileReferences wrappedRefs;
	wrappedRefs.add(BinFileId::fromMobId(old));
	QVERIFY(wrappedRefs.fullIds.contains(old));
	QCOMPARE(wrappedRefs.legacyKeys, QSet<QString>{shortId.legacyKey});
	const BinFilter wrappedBin{{{BinFilter::Operation::Intersect, {}, wrappedRefs}}};
	QVERIFY(wrappedBin.matches(modern)); // typed wrapper still carries a legacy ID
	// A mixed selection retains both kinds; adding legacy support must not
	// shorten unrelated full IDs in the same operand.
	legacyRefs.unite(fullIds({MobId::format(TestAvb::Other)}));
	const BinFilter mixed{{{BinFilter::Operation::Intersect, {}, legacyRefs}}};
	QVERIFY(mixed.matches(modern));
	QVERIFY(mixed.matches(MobId::format(TestAvb::Other)));
	auto different = TestAvb::Other;
	different[24] = char(uchar(different[24]) ^ 1);
	QVERIFY(!mixed.matches(MobId::format(different)));
}

void TestMediaFilterProxy::bin_filter_rejects_malformed_file_ids()
{
	const BinFileReferences refs{{MobId::format(QByteArray(32, '\0'))}, {QStringLiteral("0000000000000000")}};
	const BinFilter filter{{{BinFilter::Operation::Intersect, {}, refs}}};
	for (const auto &id : {QString{}, QStringLiteral("F"), QString(64, 'g'),
						   QString(32, '0'), QStringLiteral("omf:") + QString(24, '0')})
		QVERIFY(!filter.matches(id));
	QVERIFY(!filter.matches(MobId::format(QByteArray(32, '\0'))));
	QVERIFY(!filter.matches(QStringLiteral("060a2b3401010101.01010f0013000000.0000000000000000.060e2b347f7f2a80")));
	auto badDots = MobId::format(QByteArray(32, '\0'));
	badDots[0] = '.';
	badDots[16] = '0';
	QVERIFY(!filter.matches(badDots));
}

void TestMediaFilterProxy::bin_subtraction_only_uses_file_identity_data()
{
	QTest::addColumn<QString>("subtract");
	QTest::addColumn<int>("expected");
	QTest::newRow("file") << MobId::format(TestAvb::Source) << 0;
	QTest::newRow("master") << MobId::format(TestAvb::Master) << 1;
}

void TestMediaFilterProxy::bin_subtraction_only_uses_file_identity()
{
	QFETCH(QString, subtract);
	QFETCH(int, expected);
	MediaFile row = rowNamed(QStringLiteral("same media"));
	row.fileMobId = MobId::format(TestAvb::Source);
	row.masterMobId = MobId::format(TestAvb::Master);
	MediaTableModel model;
	model.setMediaFiles({row});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({row.fileMobId})},
						 {BinFilter::Operation::Subtract, {}, fullIds({subtract})}}});
	QCOMPARE(proxy.rowCount(), expected);
}

void TestMediaFilterProxy::bin_ordered_add_can_restore_a_row()
{
	MediaFile row = rowNamed(QStringLiteral("same media"));
	row.fileMobId = MobId::format(TestAvb::Source);
	row.masterMobId = MobId::format(TestAvb::Master);
	const auto refs = fullIds({row.fileMobId});
	MediaTableModel model;
	model.setMediaFiles({row, rowNamed(QStringLiteral("unrelated"))});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, refs},
						 {BinFilter::Operation::Subtract, {}, refs},
						 {BinFilter::Operation::Add, {}, refs}}});
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 0);
	proxy.setBinFilter({{{BinFilter::Operation::Add, {}, refs}}});
	QCOMPARE(proxy.rowCount(), 1); // leading Add starts with its own matches
}

void TestMediaFilterProxy::bin_leading_subtract_uses_all_media_rows()
{
	MediaFile hit = rowNamed(QStringLiteral("hit"));
	hit.fileMobId = MobId::format(TestAvb::Source);
	MediaFile outside = rowNamed(QStringLiteral("outside all bins"));
	outside.fileMobId = MobId::format(TestAvb::Other);
	MediaFile unknown = rowNamed(QStringLiteral("no identity"));
	MediaTableModel model;
	model.setMediaFiles({hit, outside, unknown});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setBinFilter({{{BinFilter::Operation::Subtract, {}, fullIds({hit.fileMobId, QString{}})}}});
	QCOMPARE(proxy.rowCount(), 2);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
	QCOMPARE(proxy.mapToSource(proxy.index(1, 0)).row(), 2);
}

void TestMediaFilterProxy::bin_empty_operand_leaves_filter_unchanged()
{
	MediaFile hit = rowNamed(QStringLiteral("hit"));
	hit.fileMobId = MobId::format(TestAvb::Source);
	MediaTableModel model;
	model.setMediaFiles({hit, rowNamed(QStringLiteral("outside"))});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	for (const auto op : {BinFilter::Operation::Intersect, BinFilter::Operation::Subtract, BinFilter::Operation::Add})
	{
		proxy.setBinFilter({{{op, {}, {}}}});
		QCOMPARE(proxy.rowCount(), 2);
		proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({hit.fileMobId})}, {op, {}, {}}}});
		QCOMPARE(proxy.rowCount(), 1);
	}
	proxy.setBinFilter({});
	QCOMPARE(proxy.rowCount(), 2);
}

void TestMediaFilterProxy::bin_expression_intersects_search_and_survives_model_refresh()
{
	MediaFile first = rowNamed(QStringLiteral("first"));
	first.fileMobId = MobId::format(TestAvb::Source);
	MediaFile second = rowNamed(QStringLiteral("second"));
	second.fileMobId = first.fileMobId;
	MediaTableModel model;
	model.setMediaFiles({first, second});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);
	proxy.setBinFilter({{{BinFilter::Operation::Intersect, {}, fullIds({first.fileMobId})}}});
	proxy.setSearchText(QStringLiteral("second"));
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
	model.setMediaFiles({rowNamed(QStringLiteral("second unrelated")), second});
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 1);
	proxy.setBinFilter({});
	QCOMPARE(proxy.rowCount(), 2);
}

void TestMediaFilterProxy::unchanged_bin_criteria_do_not_refilter_rows()
{
	class CountingProxy : public MediaFilterProxy
	{
	public:
		mutable int rowChecks = 0;

	protected:
		bool filterAcceptsRow(int row, const QModelIndex &parent) const override
		{
			++rowChecks;
			return MediaFilterProxy::filterAcceptsRow(row, parent);
		}
	};
	MediaFile first = rowNamed(QStringLiteral("first"));
	first.fileMobId = MobId::format(TestAvb::Source);
	MediaFile second = rowNamed(QStringLiteral("second"));
	second.fileMobId = MobId::format(TestAvb::Other);
	MediaTableModel model;
	model.setMediaFiles({first, second});
	CountingProxy proxy;
	proxy.setSourceModel(&model);
	QCOMPARE(proxy.rowCount(), 2);
	proxy.rowChecks = 0;
	proxy.setBinFilter({});
	QCOMPARE(proxy.rowCount(), 2);
	QCOMPARE(proxy.rowChecks, 0);

	BinFilter filter{{{BinFilter::Operation::Intersect, {QStringLiteral("First")}, fullIds({first.fileMobId})},
					  {BinFilter::Operation::Add, {QStringLiteral("Second")}, fullIds({second.fileMobId})}}};
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 2);
	QVERIFY(proxy.rowChecks > 0);
	proxy.rowChecks = 0;
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 2);
	QCOMPARE(proxy.rowChecks, 0);
	filter.steps[0].binDisplayNames = {QStringLiteral("Renamed")};
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 2);
	QCOMPARE(proxy.rowChecks, 0);

	// Order, operations, full IDs, legacy keys and step removal each matter.
	std::swap(filter.steps[0], filter.steps[1]);
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 0);
	QVERIFY(proxy.rowChecks > 0);
	proxy.rowChecks = 0;
	filter.steps[1].op = BinFilter::Operation::Add;
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 2);
	QVERIFY(proxy.rowChecks > 0);
	proxy.rowChecks = 0;
	filter.steps[1].mediaFileIds = fullIds({second.fileMobId});
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 1);
	QVERIFY(proxy.rowChecks > 0);
	proxy.rowChecks = 0;
	filter.steps[1].mediaFileIds.add(BinFileId::fromLegacyWords(0xfedcba98, 0x54761032));
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 2);
	QVERIFY(proxy.rowChecks > 0);
	proxy.rowChecks = 0;
	filter.steps.removeLast();
	proxy.setBinFilter(filter);
	QCOMPARE(proxy.rowCount(), 1);
	QVERIFY(proxy.rowChecks > 0);
}

QTEST_GUILESS_MAIN(TestMediaFilterProxy)
#include "tst_mediafilterproxy.moc"
