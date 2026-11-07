#include "mainwindow.h"
#include "mediacsv.h"
#include "progressdialog.h"

#include <QAction>
#include <QApplication>
#include <QByteArrayView>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

namespace
{
	constexpr int kScanTimeoutMs = 60'000;

	QString putMedia(const QString &base, const QString &name)
	{
		const QString folder = base + QStringLiteral("/Avid MediaFiles/MXF/1");
		if (!QDir().mkpath(folder))
			return {};
		QFile file(folder + '/' + name);
		const QByteArray bytes = QByteArrayLiteral("unreadable test header");
		if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
			return {};
		file.close();
		return QFileInfo(file).canonicalFilePath();
	}

	QSet<QString> physicalPaths(const QVector<MediaFile> &rows)
	{
		QSet<QString> paths;
		for (const auto &row : rows)
			paths.insert(row.mediaFilePath);
		return paths;
	}

	QList<QByteArrayView> splitCsv(QByteArrayView bytes, char separator)
	{
		// Doubled quotes toggle twice. A quoted newline belongs to the same record.
		bool quoted = false;
		qsizetype start = 0;
		QList<QByteArrayView> parts;
		for (qsizetype i = 0; i < bytes.size(); ++i)
		{
			if (bytes[i] == '"')
				quoted = !quoted;
			else if (bytes[i] == separator && !quoted)
			{
				parts.append(bytes.sliced(start, i - start));
				start = i + 1;
			}
		}
		if (quoted)
			return {};
		if (start < bytes.size())
			parts.append(bytes.sliced(start));
		return parts;
	}

	struct ExpectedRow
	{
		QString fileName;
		MediaFile::Kind kind;
		QString duration;
	};
}

class TestBellaPreviewUi : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cancelThenRescan();
	void cancelWhileFinalising();
	void closeWaitsForCleanup();
	void scanEditAndShared();

private:
	void checkTableAndCsv(MainWindow &window, const QString &exportPath,
						  const QList<ExpectedRow> &examples = {});
};

void TestBellaPreviewUi::initTestCase()
{
	qRegisterMetaType<QVector<MediaFile>>();
	QApplication::setQuitOnLastWindowClosed(false);
}

void TestBellaPreviewUi::checkTableAndCsv(MainWindow &window, const QString &exportPath,
										  const QList<ExpectedRow> &examples)
{
	using Column = MediaTableModel::Column;
	const auto &model = *window.m_model;
	QCOMPARE(model.presentation(), MediaPresentation::Bella);
	QCOMPARE(model.columnIndex(Column::SourcePath), -1);
	QCOMPARE(model.columnIndex(Column::EffectSequence), -1);
	const int filePath = model.columnIndex(Column::Location);
	QVERIFY(filePath >= 0 && filePath < model.columnCount());
	QCOMPARE(model.columnAt(filePath), Column::Location);
	QCOMPARE(model.headerData(filePath, Qt::Horizontal, Qt::DisplayRole).toString(),
			 QStringLiteral("File Path"));
	QVERIFY(model.rowCount() > 0);
	QCOMPARE(model.data(model.index(0, filePath), Qt::DisplayRole).toString(),
			 model.fileAt(0).mediaFilePath);

	const MediaCsv::Options options{model.precomputesEnabled(), model.presentation()};
	QVERIFY(MediaCsv::write(exportPath, model.allFiles(), options));
	QFile csv(exportPath);
	QVERIFY(csv.open(QIODevice::ReadOnly));
	const auto bytes = csv.readAll();
	const auto records = splitCsv(bytes, '\n');
	QCOMPARE(records.size(), model.allFiles().size() + 1);
	const auto columns = QString::fromUtf8(records.first()).split(',');
	QVERIFY(columns.contains(QStringLiteral("File Path")));
	QVERIFY(!columns.contains(QStringLiteral("Source Path")));
	QVERIFY(!columns.contains(QStringLiteral("Effect Sequence")));
	QCOMPARE(columns.contains(QStringLiteral("Effect Category")), model.precomputesEnabled());
	const auto csvDuration = columns.indexOf(QStringLiteral("Duration"));
	QVERIFY(csvDuration >= 0);
	const auto &rows = model.allFiles();
	for (const auto &example : examples)
	{
		const auto row = std::find_if(rows.cbegin(), rows.cend(), [&](const MediaFile &item)
									  { return item.fileName == example.fileName; });
		QVERIFY2(row != rows.cend(), qPrintable(example.fileName));
		QCOMPARE(row->kind, example.kind);
		QVERIFY(!row->fileMobId.isEmpty() && row->operationScanReceipt.has_value());
		const auto durationIndex = model.index(int(row - rows.cbegin()), model.columnIndex(Column::Duration));
		QCOMPARE(model.data(durationIndex, Qt::DisplayRole).toString(), example.duration);
		const auto quotedPath = QStringLiteral("\"%1\"").arg(row->mediaFilePath).toUtf8();
		const auto record = std::find_if(records.cbegin() + 1, records.cend(), [&](QByteArrayView value)
										 { return value.contains(quotedPath); });
		QVERIFY2(record != records.cend(), qPrintable(row->mediaFilePath));
		const auto cells = splitCsv(*record, ',');
		QCOMPARE(cells.size(), columns.size());
		QCOMPARE(QString::fromUtf8(cells.at(csvDuration)), QStringLiteral("\"%1\"").arg(example.duration));
	}
}

void TestBellaPreviewUi::cancelThenRescan()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const QString first = temp.filePath(QStringLiteral("first"));
	const QString second = temp.filePath(QStringLiteral("second"));
	QVERIFY(!putMedia(first, QStringLiteral("first.mxf")).isEmpty());
	const QString expected = putMedia(second, QStringLiteral("second.mxf"));
	QVERIFY(!expected.isEmpty());

	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy finished(window.m_scanner, &AppScanner::scanFinished);
	QSignalSpy failed(window.m_scanner, &AppScanner::scanFailed);
	window.scanListedPaths({first});
	QVERIFY(window.m_scanner->isActive());
	QVERIFY(!window.m_operations->isIdle());
	QVERIFY(!window.m_exportAction->isEnabled());
	QVERIFY(!window.m_manageMediaAction->isEnabled());
	QVERIFY(!window.m_rebalanceAction->isEnabled());
	QCOMPARE(window.m_model->rowCount(), 0);
	QVERIFY(window.m_progressDialog);
	// Request Cancel on the GUI thread before it can accept the queued result.
	window.m_progressDialog->requestCancel();
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, kScanTimeoutMs);
	QVERIFY(finished.at(0).at(1).toBool());
	QTRY_VERIFY_WITH_TIMEOUT(window.m_operations->isIdle(), kScanTimeoutMs);
	QVERIFY(!window.m_scanner->isActive());

	window.rescanPaths({second});
	QVERIFY(window.m_scanner->isActive());
	QCOMPARE(window.m_model->rowCount(), 0);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, kScanTimeoutMs);
	QTRY_VERIFY_WITH_TIMEOUT(window.m_operations->isIdle(), kScanTimeoutMs);
	QCOMPARE(failed.size(), 0);
	QVERIFY(!finished.at(1).at(1).toBool());
	QVERIFY(finished.at(1).at(2).toBool());
	QCOMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).mediaFilePath, expected);
	QVERIFY(window.m_model->fileAt(0).isNoDatabase());
	QVERIFY(!window.m_model->fileAt(0).isNoReference());
	QVERIFY(window.m_model->fileAt(0).operationScanReceipt);

	window.m_tableView->selectRow(0);
	QVERIFY(window.m_tableView->selectionModel()->hasSelection());
#ifdef MEDIAMUSTER_BELLA_PREVIEW
	QVERIFY(!window.m_manageMediaAction->isEnabled());
	QVERIFY(!window.m_rebalanceAction->isEnabled());
#else
	QVERIFY(window.m_manageMediaAction->isEnabled());
	QVERIFY(window.m_rebalanceAction->isEnabled());
#endif
	QVERIFY(!window.m_operations->undoAction()->isEnabled());
	QVERIFY(!window.m_operations->recoveryAction()->isEnabled());
	QVERIFY(window.m_exportAction->isEnabled());
	QVERIFY(window.m_binFilterAction->isEnabled());
	checkTableAndCsv(window, temp.filePath(QStringLiteral("preview.csv")));
}

void TestBellaPreviewUi::cancelWhileFinalising()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto expected = putMedia(temp.path(), QStringLiteral("finalising.mxf"));
	QVERIFY(!expected.isEmpty());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy finalising(window.m_scanner, &AppScanner::scanFinalising);
	QSignalSpy finished(window.m_scanner, &AppScanner::scanFinished);
	auto *progress = window.progressDialog();
	connect(window.m_scanner, &AppScanner::scanFinalising,
			progress, &ProgressDialog::requestCancel, Qt::DirectConnection);

	window.scanListedPaths({temp.path()});
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, kScanTimeoutMs);
	QCOMPARE(finalising.size(), 1);
	QVERIFY(finished.front().at(1).toBool());
	QVERIFY(finished.front().at(2).toBool());
	QCOMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).mediaFilePath, expected);
	QVERIFY(window.m_statusScanTime->text().startsWith(QStringLiteral("Scan cancelled")));
	QVERIFY(window.m_operations->isIdle());
	QVERIFY(!window.m_scanner->isActive());
}

void TestBellaPreviewUi::closeWaitsForCleanup()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	QVERIFY(!putMedia(temp.path(), QStringLiteral("close.mxf")).isEmpty());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.show();
	window.scanListedPaths({temp.path()});
	QVERIFY(window.m_scanner->isActive());
	QVERIFY(!window.close());
	QVERIFY(window.isVisible());
	QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), kScanTimeoutMs);
	QVERIFY(!window.m_scanner->isActive());
}

void TestBellaPreviewUi::scanEditAndShared()
{
	if (qEnvironmentVariableIntValue("MEDIAMUSTER_TEST_BELLA_REAL_SCAN") != 1)
		QSKIP("Set MEDIAMUSTER_TEST_BELLA_REAL_SCAN=1 to scan EDIT and Shared read-only.");
	const QString edit = QStringLiteral("/Volumes/EDIT");
	const QString shared = QStringLiteral("/Users/Shared/AvidMediaComposer");
	for (const auto &base : {edit, shared})
		QVERIFY2(QFileInfo(base + QStringLiteral("/Avid MediaFiles/MXF")).isDir(), qPrintable(base));

	QTemporaryDir output;
	QVERIFY(output.isValid());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_detectedVolumePaths = {edit, shared};
	window.m_volumeList->addItems(window.m_detectedVolumePaths);
	window.updateActivityUi();
	QVERIFY(window.m_scanAllAction->isEnabled());
	QSignalSpy finished(window.m_scanner, &AppScanner::scanFinished);
	QSignalSpy failed(window.m_scanner, &AppScanner::scanFailed);
	bool requested = false;
	bool tableWasEmpty = false;
	const auto cancelAtDatabase = connect(window.m_scanner, &AppScanner::scanProgress, &window,
										  [&](const QString &stage, qsizetype, qsizetype, const QString &path)
										  {
											  if (!requested && stage == QStringLiteral("Reading databases") &&
												  (path.endsWith(QStringLiteral(".pmr")) || path.endsWith(QStringLiteral(".mdb"))))
											  {
												  requested = true;
												  tableWasEmpty = window.m_model->rowCount() == 0;
												  window.m_progressDialog->requestCancel();
											  }
										  });
	window.m_scanAllAction->trigger();
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, kScanTimeoutMs);
	QTRY_VERIFY_WITH_TIMEOUT(window.m_operations->isIdle(), kScanTimeoutMs);
	QVERIFY(requested && tableWasEmpty);
	QVERIFY(finished.at(0).at(1).toBool());
	QVERIFY(finished.at(0).at(2).toBool());
	QVERIFY(window.m_statusScanTime->text().startsWith(QStringLiteral("Scan cancelled")));
	const auto cancelledPaths = physicalPaths(window.m_model->allFiles());
	QVERIFY(!cancelledPaths.isEmpty());
	disconnect(cancelAtDatabase);

	QSignalSpy progress(window.m_scanner, &AppScanner::scanProgress);
	window.m_scanAllAction->trigger();
	QCOMPARE(window.m_model->rowCount(), 0);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, kScanTimeoutMs);
	QTRY_VERIFY_WITH_TIMEOUT(window.m_operations->isIdle(), kScanTimeoutMs);
	QCOMPARE(failed.size(), 0);
	QVERIFY(!finished.at(1).at(1).toBool());
	QVERIFY(finished.at(1).at(2).toBool());
	const auto &rows = window.m_model->allFiles();
	QCOMPARE(physicalPaths(rows), cancelledPaths);
	QCOMPARE(rows.size(), cancelledPaths.size());
	QCOMPARE(rows.size(), qsizetype(2413));
	for (const auto &base : {edit, shared})
		QVERIFY(std::any_of(rows.cbegin(), rows.cend(), [&](const MediaFile &row)
							{ return row.mediaFilePath.startsWith(base + '/'); }));
	const auto readCount = [&](const QString &stage)
	{
		// Stage entry has a zero count and an empty path. Count completed jobs.
		return std::count_if(progress.cbegin(), progress.cend(), [&](const auto &event)
							 { return event.at(0).toString() == stage && event.at(1).toLongLong() > 0 &&
									  !event.at(3).toString().isEmpty(); });
	};
	QCOMPARE(readCount(QStringLiteral("Reading databases")), 12);
	QCOMPARE(readCount(QStringLiteral("Reading required headers")), 1);

	QString exportPath = qEnvironmentVariable("MEDIAMUSTER_TEST_BELLA_CSV_PATH");
	if (exportPath.isEmpty())
		exportPath = output.filePath(QStringLiteral("edit-and-shared.csv"));
	else
	{
		const QFileInfo destination(exportPath);
		const QString directory = destination.dir().canonicalPath();
		const QString tempDirectory = QDir(QDir::tempPath()).canonicalPath();
		QVERIFY2(destination.isAbsolute() && !destination.isSymLink() &&
					 (directory == tempDirectory || directory.startsWith(tempDirectory + '/') ||
					  directory == QStringLiteral("/private/tmp") || directory.startsWith(QStringLiteral("/private/tmp/"))),
				 "Use an existing disposable temporary directory for MEDIAMUSTER_TEST_BELLA_CSV_PATH.");
	}
	checkTableAndCsv(window, exportPath, {{QStringLiteral("Untitled Sequence.175B1728V.mxf"), MediaFile::Kind::Video, QStringLiteral("00;10;11;28")}, {QStringLiteral("TONE_100A01.6A972974.039700.wav"), MediaFile::Kind::Audio, QStringLiteral("00:01:00:00")}, {QStringLiteral("TONE_100A01.6A972997.0C53E0.aif"), MediaFile::Kind::Audio, QStringLiteral("00:01:00:00")}});
	qInfo() << "Bella retained" << rows.size() << "physical rows on EDIT and Shared.";
}

QTEST_MAIN(TestBellaPreviewUi)
#include "tst_bellapreviewui.moc"
