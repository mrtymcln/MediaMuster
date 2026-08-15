// Exercises MediaTableModel::removeFilesByPath; the algorithm's
// trick is collapsing contiguous deletions into a single
// begin/endRemoveRows pair, so the assertions count signal
// emissions in addition to the surviving rows.

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

	// The attribution setters own the flag+label pairing; a half-set
	// state (label without flag, or stale flags after a transition)
	// must be impossible. Labels asserted as raw literals on purpose —
	// the rename tripwire.
	void attribution_setters_pair_flags_with_labels();

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

void TestMediaTableModel::attribution_setters_pair_flags_with_labels()
{
	MediaFile f;

	f.markNoReference();
	QVERIFY(f.isNoReference);
	QVERIFY(!f.isNoProject);
	QVERIFY(!f.isNoDatabase());
	QCOMPARE(f.project, QStringLiteral("No reference"));

	f.markNoDatabase(MediaFile::DbIssue::Unreadable);
	QVERIFY(f.isNoDatabase());
	QVERIFY(!f.isNoReference);
	QCOMPARE(f.project, QStringLiteral("No database"));

	// 'No project' outranks both miss states — the setter clears them.
	f.markNoProject();
	QVERIFY(f.isNoProject);
	QVERIFY(!f.isNoReference);
	QVERIFY(!f.isNoDatabase());
	QCOMPARE(f.project, QStringLiteral("No project"));
}

QTEST_GUILESS_MAIN(TestMediaTableModel)
#include "tst_mediatablemodel.moc"