// Exercises MediaTableModel::removeFilesByPath; the algorithm's
// trick is collapsing contiguous deletions into a single
// begin/endRemoveRows pair, so the assertions count signal
// emissions in addition to the surviving rows.

#include "enumutil.h"
#include "mediafile.h"
#include "mediatablemodel.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QTest>

class TestMediaTableModel : public QObject
{
	Q_OBJECT
private slots:
	void empty_paths_emits_nothing();
	void empty_model_emits_nothing();
	void unknown_paths_emit_nothing();
	void single_row_emits_one_range();
	void contiguous_block_emits_one_range();
	void leading_block_emits_one_range();
	void trailing_block_emits_one_range();
	void remove_all_emits_one_range();
	void two_separated_rows_emit_two_ranges();
	void three_blocks_emit_three_ranges();

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

void TestMediaTableModel::empty_paths_emits_nothing()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(5));

	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);
	QSignalSpy doneSpy(&model, &QAbstractItemModel::rowsRemoved);

	model.removeFilesByPath({});

	QCOMPARE(aboutSpy.size(), 0);
	QCOMPARE(doneSpy.size(), 0);
	QCOMPARE(model.rowCount(), 5);
}

void TestMediaTableModel::empty_model_emits_nothing()
{
	MediaTableModel model;
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	model.removeFilesByPath({QStringLiteral("/fake/row0.mxf")});

	QCOMPARE(aboutSpy.size(), 0);
	QCOMPARE(model.rowCount(), 0);
}

void TestMediaTableModel::unknown_paths_emit_nothing()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(3));

	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);
	model.removeFilesByPath({QStringLiteral("/nope/missing.mxf")});

	QCOMPARE(aboutSpy.size(), 0);
	QCOMPARE(model.rowCount(), 3);
}

void TestMediaTableModel::single_row_emits_one_range()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(5));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	model.removeFilesByPath({QStringLiteral("/fake/row2.mxf")});

	QCOMPARE(aboutSpy.size(), 1);
	const auto args = aboutSpy.takeFirst();
	QCOMPARE(args.at(1).toInt(), 2); // first
	QCOMPARE(args.at(2).toInt(), 2); // last
	QCOMPARE(pathsOf(model),
			 (QStringList{"/fake/row0.mxf", "/fake/row1.mxf", "/fake/row3.mxf", "/fake/row4.mxf"}));
}

void TestMediaTableModel::contiguous_block_emits_one_range()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(5));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	// rows 1,2,3 are contiguous, so a single (1,3) range.
	model.removeFilesByPath({QStringLiteral("/fake/row1.mxf"), QStringLiteral("/fake/row2.mxf"),
							 QStringLiteral("/fake/row3.mxf")});

	QCOMPARE(aboutSpy.size(), 1);
	const auto args = aboutSpy.takeFirst();
	QCOMPARE(args.at(1).toInt(), 1);
	QCOMPARE(args.at(2).toInt(), 3);
	QCOMPARE(pathsOf(model), (QStringList{"/fake/row0.mxf", "/fake/row4.mxf"}));
}

void TestMediaTableModel::leading_block_emits_one_range()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(5));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	// Rows 0,1: exercises the post-loop fall-through path.
	model.removeFilesByPath({QStringLiteral("/fake/row0.mxf"), QStringLiteral("/fake/row1.mxf")});

	QCOMPARE(aboutSpy.size(), 1);
	const auto args = aboutSpy.takeFirst();
	QCOMPARE(args.at(1).toInt(), 0);
	QCOMPARE(args.at(2).toInt(), 1);
	QCOMPARE(pathsOf(model), (QStringList{"/fake/row2.mxf", "/fake/row3.mxf", "/fake/row4.mxf"}));
}

void TestMediaTableModel::trailing_block_emits_one_range()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(5));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	model.removeFilesByPath({QStringLiteral("/fake/row3.mxf"), QStringLiteral("/fake/row4.mxf")});

	QCOMPARE(aboutSpy.size(), 1);
	const auto args = aboutSpy.takeFirst();
	QCOMPARE(args.at(1).toInt(), 3);
	QCOMPARE(args.at(2).toInt(), 4);
	QCOMPARE(pathsOf(model), (QStringList{"/fake/row0.mxf", "/fake/row1.mxf", "/fake/row2.mxf"}));
}

void TestMediaTableModel::remove_all_emits_one_range()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(4));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	QSet<QString> all;
	for (const auto &mf : model.allFiles())
		all.insert(mf.filePath);
	model.removeFilesByPath(all);

	QCOMPARE(aboutSpy.size(), 1);
	const auto args = aboutSpy.takeFirst();
	QCOMPARE(args.at(1).toInt(), 0);
	QCOMPARE(args.at(2).toInt(), 3);
	QCOMPARE(model.rowCount(), 0);
}

void TestMediaTableModel::two_separated_rows_emit_two_ranges()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(5));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	// Rows 1 and 3 are not adjacent, so two separate ranges.
	// removeFilesByPath walks back-to-front, so signals arrive
	// for index 3 first, then index 1.
	model.removeFilesByPath({QStringLiteral("/fake/row1.mxf"), QStringLiteral("/fake/row3.mxf")});

	QCOMPARE(aboutSpy.size(), 2);
	const auto first = aboutSpy.takeFirst();
	QCOMPARE(first.at(1).toInt(), 3);
	QCOMPARE(first.at(2).toInt(), 3);
	const auto second = aboutSpy.takeFirst();
	QCOMPARE(second.at(1).toInt(), 1);
	QCOMPARE(second.at(2).toInt(), 1);
	QCOMPARE(pathsOf(model), (QStringList{"/fake/row0.mxf", "/fake/row2.mxf", "/fake/row4.mxf"}));
}

void TestMediaTableModel::three_blocks_emit_three_ranges()
{
	MediaTableModel model;
	model.setMediaFiles(makeRows(10));
	QSignalSpy aboutSpy(&model, &QAbstractItemModel::rowsAboutToBeRemoved);

	// Three disjoint blocks: {1,2}, {5}, {7,8}.
	model.removeFilesByPath({QStringLiteral("/fake/row1.mxf"), QStringLiteral("/fake/row2.mxf"),
							 QStringLiteral("/fake/row5.mxf"), QStringLiteral("/fake/row7.mxf"),
							 QStringLiteral("/fake/row8.mxf")});

	QCOMPARE(aboutSpy.size(), 3);

	const auto a = aboutSpy.takeFirst();
	QCOMPARE(a.at(1).toInt(), 7);
	QCOMPARE(a.at(2).toInt(), 8);
	const auto b = aboutSpy.takeFirst();
	QCOMPARE(b.at(1).toInt(), 5);
	QCOMPARE(b.at(2).toInt(), 5);
	const auto c = aboutSpy.takeFirst();
	QCOMPARE(c.at(1).toInt(), 1);
	QCOMPARE(c.at(2).toInt(), 2);

	QCOMPARE(pathsOf(model), (QStringList{"/fake/row0.mxf", "/fake/row3.mxf", "/fake/row4.mxf",
										  "/fake/row6.mxf", "/fake/row9.mxf"}));
}

void TestMediaTableModel::location_cell_shows_the_full_path()
{
	MediaFile f;
	f.fileName = QStringLiteral("V01.abc.mxf");
	f.volumeName = QStringLiteral("EDIT");
	f.mxfFolder = QStringLiteral("8646");
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

	// The sort role hands back the raw (possibly invalid) datetime; Qt
	// orders invalid before valid, so blanks group together.
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

QTEST_GUILESS_MAIN(TestMediaTableModel)
#include "tst_mediatablemodel.moc"