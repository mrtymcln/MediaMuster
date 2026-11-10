#include "ui/main_window.h"
#include "media/media_csv.h"
#include "ui/dialogs/progress_dialog.h"
#include "support/diagnostics.h"

#include <QAction>
#include <QApplication>
#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <utility>

// MARK: - Test helpers

namespace
{
	constexpr int kScanTimeoutMs = 60'000;
	constexpr int kWarningLocationCount = 8;
	const QString kFurtherWarnings = QStringLiteral("Got to Help > Reveal Diagnostics for more info.");

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

}

// MARK: - Test fixture

class TestBellaPreviewUi : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cancelThenRescan();
	void cancelWhileFinalising();
	void closeWaitsForCleanup();
	void scannerMessagesAreBoundedAndReset();
	void scannerLogRetainsEveryWarningOnce();

private:
	void checkTableAndCsv(MainWindow &window, const QString &exportPath);
};

// MARK: - UI test setup

void TestBellaPreviewUi::initTestCase()
{
	qRegisterMetaType<QVector<MediaFile>>();
	QApplication::setQuitOnLastWindowClosed(false);
}

// MARK: - Table and export assertions

void TestBellaPreviewUi::checkTableAndCsv(MainWindow &window, const QString &exportPath)
{
	using Column = MediaTableModel::Column;
	const auto &model = *window.m_model;
	const int filePath = model.columnIndex(Column::Location);
	QVERIFY(filePath >= 0 && filePath < model.columnCount());
	QCOMPARE(model.columnAt(filePath), Column::Location);
	QCOMPARE(model.headerData(filePath, Qt::Horizontal, Qt::DisplayRole).toString(),
			 QStringLiteral("File Path"));
	QVERIFY(model.rowCount() > 0);
	QCOMPARE(model.data(model.index(0, filePath), Qt::DisplayRole).toString(),
			 model.fileAt(0).mediaFilePath);

	const MediaCsv::Options options{model.precomputesEnabled()};
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
}

// MARK: - Scan cancellation and close lifecycle

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

// MARK: - Scan diagnostics

void TestBellaPreviewUi::scannerMessagesAreBoundedAndReset()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	BellaAppScanner scanner;
	QVector<LogMessage> messages;
	connect(&scanner, &BellaAppScanner::scanLogBatch, this,
			[&messages](const QVector<LogMessage> &batch)
			{ messages += batch; });
	QSignalSpy finished(&scanner, &BellaAppScanner::scanFinished);
	QSignalSpy failed(&scanner, &BellaAppScanner::scanFailed);
	BellaAppScanner::Options options;
	for (int index = 0; index < kWarningLocationCount; ++index)
	{
		const auto path = temp.filePath(QStringLiteral("empty%1").arg(index));
		QVERIFY(QDir().mkpath(path));
		options.locations.append({BellaAppScanner::Location::Kind::RequiredMediaBase, path});
	}
	scanner.startScan(options);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, kScanTimeoutMs);
	QCOMPARE(failed.size(), 0);
	QVERIFY(!finished.first().at(2).toBool());
	QCOMPARE(messages.size(), 8); // Start, five details, one overflow notice, outcome.
	QCOMPARE(messages.first().message, QStringLiteral("Scan starting: 8 locations."));
	for (int index = 0; index < 5; ++index)
	{
		QCOMPARE(messages.at(index + 1).level, QtWarningMsg);
		QVERIFY(messages.at(index + 1).message.startsWith(options.locations.at(index).path + ": "));
	}
	QCOMPARE(messages.at(6).message, kFurtherWarnings);
	QVERIFY(messages.last().message.startsWith(QStringLiteral("Scan finished with incomplete discovery: 0 files, ")));
	QVERIFY(messages.last().message.endsWith(QStringLiteral(" ms, 8 warnings.")));

	const auto filePath = putMedia(temp.filePath(QStringLiteral("media")), QStringLiteral("unreadable.mxf"));
	QVERIFY(!filePath.isEmpty());
	options.locations = {BellaAppScanner::rescanLocation(temp.filePath(QStringLiteral("media")))};
	messages.clear();
	scanner.startScan(options);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, kScanTimeoutMs);
	QCOMPARE(messages.size(), 3); // A fresh scan has a fresh warning allowance.
	QCOMPARE(messages.first().message, QStringLiteral("Scan starting: 1 location."));
	QVERIFY(messages.at(1).message.startsWith(filePath + ": "));
	QVERIFY(messages.last().message.startsWith(QStringLiteral("Scan complete: 1 file, ")));
	QVERIFY(messages.last().message.endsWith(QStringLiteral(" ms, 1 warning.")));

	messages.clear();
	scanner.startScan(options);
	QVERIFY(scanner.cancelScan());
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, kScanTimeoutMs);
	QVERIFY(finished.last().at(1).toBool());
	QCOMPARE(failed.size(), 0);
	int cancellationMessages = 0;
	for (const auto &message : std::as_const(messages))
		cancellationMessages += message.message.startsWith(QStringLiteral("Scan cancelled:"));
	QCOMPARE(cancellationMessages, 1);
	QVERIFY(messages.last().message.endsWith(QStringLiteral("; results are incomplete.")));
	QVERIFY(!scanner.isActive());
}

void TestBellaPreviewUi::scannerLogRetainsEveryWarningOnce()
{
	QTemporaryDir temp;
	QVERIFY(temp.isValid());
	const auto logs = temp.filePath(QStringLiteral("logs"));
	QStringList locations;
	for (int index = 0; index < kWarningLocationCount; ++index)
	{
		const auto path = temp.filePath(QStringLiteral("empty%1").arg(index));
		QVERIFY(QDir().mkpath(path));
		locations.append(path);
	}
	QStringList arguments{QStringLiteral("--scan-log-smoke"), logs};
	arguments += locations;
	QProcess child;
	child.start(QCoreApplication::applicationFilePath(), arguments);
	QVERIFY2(child.waitForStarted(5000), qPrintable(child.errorString()));
	const bool finished = child.waitForFinished(kScanTimeoutMs);
	if (!finished)
	{
		child.kill();
		child.waitForFinished(3000);
	}
	QVERIFY2(finished, "The scan diagnostic child did not finish.");
	QCOMPARE(child.exitStatus(), QProcess::NormalExit);
	QCOMPARE(child.exitCode(), 0);
	QFile log(logs + QStringLiteral("/mediamuster.log"));
	QVERIFY(log.open(QIODevice::ReadOnly));
	const auto content = log.readAll();
	for (const auto &path : std::as_const(locations))
		QCOMPARE(content.count(path.toUtf8() + ": "), 1);
	QCOMPARE(content.count(kFurtherWarnings.toUtf8()), 1);
	QCOMPARE(content.count("Scan starting:"), 1);
	QCOMPARE(content.count("Scan finished with incomplete discovery:"), 1);
	QVERIFY(content.contains(" ms, 8 warnings."));
	QVERIFY(!content.contains("Reading database:"));
	QVERIFY(!content.contains("Finalising scan:"));
	QVERIFY(!content.contains("Scan total including row formatting:"));
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	const auto arguments = app.arguments();
	if (arguments.size() >= 4 && arguments.at(1) == QStringLiteral("--scan-log-smoke"))
	{
		QCoreApplication::setApplicationName(QStringLiteral("MediaMuster scan diagnostic test"));
		Diagnostics::install(arguments.at(2));
		BellaAppScanner scanner;
		QObject::connect(&scanner, &BellaAppScanner::scanFinished, &app,
						 [&app](const QVector<MediaFile> &, bool cancelled, bool)
						 { app.exit(cancelled ? 1 : 0); });
		QObject::connect(&scanner, &BellaAppScanner::scanFailed, &app,
						 [&app](const QString &)
						 { app.exit(2); });
		QTimer::singleShot(kScanTimeoutMs - 5000, &app, [&app]
						   { app.exit(3); });
		BellaAppScanner::Options options;
		for (qsizetype index = 3; index < arguments.size(); ++index)
			options.locations.append({BellaAppScanner::Location::Kind::RequiredMediaBase, arguments.at(index)});
		scanner.startScan(options);
		return app.exec();
	}
	TestBellaPreviewUi test;
	return QTest::qExec(&test, argc, argv);
}
#include "tst_bella_preview_ui.moc"
