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

#include <QTest>

#include <algorithm>
#include <array>

namespace
{
	MediaFile rowNamed(const QString &clipName)
	{
		MediaFile f;
		f.clipName = clipName;
		f.fileName = clipName + QStringLiteral(".mxf");
		f.filePath = QStringLiteral("/vol/") + f.fileName;
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
	void nfc_search_finds_nfd_row();
	void nfd_search_finds_nfc_row();
	void folding_and_normalisation_compose();
	void plain_ascii_never_matches_accents();

	// Sorting. The Size column sorts on exact byte counts, not the
	// rounded MB display string — two files that both show "850.0 MB"
	// still order by their real sizes, and no double rounding sits
	// between the user and the answer.
	void size_column_sorts_on_exact_bytes();

	// Search covers the path (2026-08-18). The Location column shows the
	// full path, so the search box has to match it — and because the
	// filename and the Avid folder are both substrings of the path, they
	// keep matching without a pass of their own.
	void search_matches_the_path_shown_in_the_location_column();
	void unknown_classification_does_not_match_known_filters();
	void three_state_classification_sort_is_consistent();
	void effect_selection_intersects_volume_and_existing_filters();
	void effect_gate_clears_selection_and_hidden_search();
	void effect_columns_sort_displayed_values();
};

void TestMediaFilterProxy::nfc_search_finds_nfd_row()
{
	MediaTableModel model;
	model.setMediaFiles({rowNamed(kCafeNfd)});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	QVERIFY(kCafeNfc != kCafeNfd); // the two forms really are different code points
	proxy.setSearchText(kCafeNfc); // composed keyboard input
	QCOMPARE(proxy.rowCount(), 1); // used to be 0 — the invisible-file bug
}

void TestMediaFilterProxy::nfd_search_finds_nfc_row()
{
	MediaTableModel model;
	model.setMediaFiles({rowNamed(kCafeNfc)});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	proxy.setSearchText(kCafeNfd); // e.g. pasted from an NFD path
	QCOMPARE(proxy.rowCount(), 1);
}

void TestMediaFilterProxy::folding_and_normalisation_compose()
{
	// Uppercase decomposed row, lowercase composed needle: both the case
	// fold and the normalisation have to apply for this to hit.
	MediaTableModel model;
	model.setMediaFiles({rowNamed(QStringLiteral("CAFÉ REEL 7"))});
	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	proxy.setSearchText(kCafeNfc);
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
	a.filePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/8646/V01.abc.mxf");
	a.fileName = QStringLiteral("V01.abc.mxf");
	a.mxfFolder = QStringLiteral("8646");
	a.volumeName = QStringLiteral("EDIT");
	MediaFile b = rowNamed(QStringLiteral("Scene 2"));
	b.filePath = QStringLiteral("/Volumes/BACKUP/Avid MediaFiles/MXF/1/V02.def.mxf");
	b.fileName = QStringLiteral("V02.def.mxf");
	b.mxfFolder = QStringLiteral("1");
	b.volumeName = QStringLiteral("BACKUP");
	model.setMediaFiles({a, b});

	MediaFilterProxy proxy;
	proxy.setSourceModel(&model);

	// A folder name, a filename fragment and a whole path segment all live
	// inside filePath, so one match pass covers them.
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
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Precompute);
	QCOMPARE(proxy.rowCount(), 1);
	QCOMPARE(proxy.index(0, name).data().toString(), QStringLiteral("sound"));
	proxy.setFilterMode(MediaFilterProxy::FilterMode::All);
	proxy.setSearchText(QStringLiteral("unread"));
	QCOMPARE(proxy.rowCount(), 1); // unresolved files remain accessible
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
		row.durationFrames = 250;
		row.timecodeBase = 25;
	}
	std::array<int, 3> order{0, 1, 2};
	do
	{
		MediaTableModel model;
		model.setMediaFiles({rows[order[0]], rows[order[1]], rows[order[2]]});
		MediaFilterProxy proxy;
		proxy.setSourceModel(&model);
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

void TestMediaFilterProxy::effect_selection_intersects_volume_and_existing_filters()
{
	MediaFile title = rowNamed(QStringLiteral("picture"));
	title.type = MediaFile::Type::Precompute;
	title.kind = MediaFile::Kind::Video;
	title.effect = QStringLiteral("Title");
	title.volumeName = QStringLiteral("EDIT");
	title.volumePath = QStringLiteral("/Volumes/EDIT");
	title.project = QStringLiteral("Project A");
	title.masterMobId = QStringLiteral("title-master");
	MediaFile custom = title;
	custom.clipName = QStringLiteral("sound");
	custom.fileName = QStringLiteral("sound.mxf");
	custom.filePath = QStringLiteral("/vol/sound.mxf");
	custom.kind = MediaFile::Kind::Audio;
	custom.effect = QStringLiteral("Custom, exact name");
	custom.masterMobId = QStringLiteral("custom-master");
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
	proxy.setEffectDetailsEnabled(true);
	proxy.setEffectFilter({QStringLiteral("Title"), QStringLiteral("Custom, exact name")});
	QCOMPARE(proxy.rowCount(), 4); // OR across names, proven precomputes only
	proxy.setEffectVolumeFilter(title.volumePath);
	QCOMPARE(proxy.rowCount(), 3);
	proxy.setProjectFilter({title.project});
	QCOMPARE(proxy.rowCount(), 2);
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Audio);
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setBinFilterMobs(true, {title.masterMobId});
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setBinFilterMobs(true, {custom.masterMobId});
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText(QStringLiteral("picture"));
	QCOMPARE(proxy.rowCount(), 0);
	proxy.setSearchText(QStringLiteral("sound"));
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setSearchText({});
	proxy.setBinFilterMobs(false, {});
	proxy.setFilterMode(MediaFilterProxy::FilterMode::All);
	proxy.setProjectFilter({});
	proxy.setEffectFilter({QStringLiteral("title")});
	QCOMPARE(proxy.rowCount(), 0); // names are exact, not fuzzy or case folded
	proxy.setEffectFilter({});
	QCOMPARE(proxy.rowCount(), 3); // volume alone still means precomputes
	proxy.setEffectVolumeFilter({});
	QCOMPARE(proxy.rowCount(), 6); // neither selection means no effect filter
}

void TestMediaFilterProxy::effect_gate_clears_selection_and_hidden_search()
{
	MediaFile render = rowNamed(QStringLiteral("render"));
	render.type = MediaFile::Type::Precompute;
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
	QVERIFY(!proxy.effectDetailsEnabled());
	proxy.setEffectFilter({render.effect});
	proxy.setEffectVolumeFilter(render.volumePath);
	QVERIFY(proxy.effectFilter().isEmpty());
	QVERIFY(proxy.effectVolumeFilter().isEmpty());
	QCOMPARE(proxy.rowCount(), 2);
	for (const auto &text : {render.effect, render.effectCategory, render.effectSequence})
	{
		proxy.setSearchText(text);
		QCOMPARE(proxy.rowCount(), 0);
		proxy.setEffectDetailsEnabled(true);
		QCOMPARE(proxy.rowCount(), 1);
		proxy.setEffectDetailsEnabled(false);
		QCOMPARE(proxy.rowCount(), 0);
	}
	proxy.setSearchText({});
	proxy.setEffectDetailsEnabled(true);
	proxy.setEffectFilter({render.effect, render.effect, QString()});
	QCOMPARE(proxy.effectFilter(), QStringList{render.effect});
	proxy.setEffectVolumeFilter(render.volumePath);
	QCOMPARE(proxy.rowCount(), 1);
	proxy.setEffectDetailsEnabled(false);
	QCOMPARE(proxy.rowCount(), 2);
	QVERIFY(proxy.effectFilter().isEmpty());
	QVERIFY(proxy.effectVolumeFilter().isEmpty());
	proxy.setFilterMode(MediaFilterProxy::FilterMode::Precompute);
	QCOMPARE(proxy.rowCount(), 1); // classification does not depend on the gate
	proxy.setFilterMode(MediaFilterProxy::FilterMode::All);
	proxy.setEffectDetailsEnabled(true);
	QCOMPARE(proxy.rowCount(), 2); // no hidden selection comes back
	proxy.setEffectDetailsEnabled(false);
	proxy.setSearchText(QStringLiteral("render"));
	QCOMPARE(proxy.rowCount(), 1); // existing visible Clip Name search remains
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
	model.setEffectDetailsEnabled(true);
	proxy.setEffectDetailsEnabled(true);
	for (auto column : {MediaTableModel::Column::Effect, MediaTableModel::Column::EffectCategory, MediaTableModel::Column::EffectSequence})
	{
		proxy.sort(int(column));
		QStringList names;
		for (int i = 0; i < proxy.rowCount(); ++i)
			names << proxy.index(i, int(MediaTableModel::Column::ClipName)).data().toString();
		QCOMPARE(names, (QStringList{QStringLiteral("ordinary"), QStringLiteral("alpha"), QStringLiteral("zulu")}));
	}
	model.setEffectDetailsEnabled(false);
	proxy.setEffectDetailsEnabled(false);
	QCOMPARE(proxy.columnCount(), 15);
	QCOMPARE(proxy.rowCount(), 3);
}

QTEST_GUILESS_MAIN(TestMediaFilterProxy)
#include "tst_mediafilterproxy.moc"
