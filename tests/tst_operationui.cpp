#include "mainwindow.h"
#include "featureflags.h"
#include "mediacsv.h"
#include "managemediadialog.h"
#include "opjournal.h"
#include "progressdialog.h"
#include "rebalancedialog.h"
#include "rebalancer.h"
#include "formatutil.h"
#include "binfilterdialog.h"
#include "testavb.h"
#include "mobid.h"
#include "canon/scanmodel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QComboBox>
#include <QRadioButton>
#include <QSemaphore>
#include <QTreeWidget>
#include <QtConcurrent>
#include <QDir>
#include <QEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <atomic>
#include <memory>

namespace
{
	bool put(const QString &path, const QByteArray &data)
	{
		if (!QDir().mkpath(QFileInfo(path).absolutePath()))
			return false;
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
	}
	RebalancePlan smallRebalancePlan(const QString &root)
	{
		RebalancePlan plan;
		plan.mxfRootPath = root;
		plan.volumeLabel = QStringLiteral("Test media");
		for (int n = 1; n <= 3; ++n)
		{
			FolderState folder;
			folder.id = {{}, n};
			folder.mediaFolderName = folder.id.display();
			folder.count = n == 1 ? 3 : n == 2 ? 1
											   : 0;
			folder.filesOut = n == 1 ? 3 : 0;
			folder.filesIn = n == 2 ? 2 : n == 3 ? 1
												 : 0;
			folder.isNew = n == 3;
			plan.folders.append(folder);
		}
		plan.newFolders.append(NumberedMxfFolder{{}, 3});
		for (int n = 0; n < 3; ++n)
		{
			RebalanceMove op;
			op.srcPath = root + QStringLiteral("/1/clip-%1.mxf").arg(n);
			op.dest = {{}, n == 1 ? 3 : 2};
			plan.ops.append(op);
		}
		return plan;
	}
	QString folderCountCaption(const RebalanceDialog &dialog, const QString &mediaFolderName)
	{
		for (const auto *card : dialog.findChildren<QFrame *>(QStringLiteral("folderCard")))
			if (card->accessibleName() == mediaFolderName)
				return card->accessibleDescription();
		return QStringLiteral("Missing folder card: ") + mediaFolderName;
	}
	QStringList appMessages(const QPlainTextEdit *console)
	{
		const QString prefix = QStringLiteral("app: ");
		QStringList messages;
		for (const QString &line : console->toPlainText().split('\n'))
		{
			const int start = line.indexOf(prefix);
			if (start >= 0)
				messages.append(line.mid(start + prefix.size()));
		}
		return messages;
	}
	struct Sink : OpSink
	{
		std::atomic<bool> *cancelAfterResult = nullptr;
		void progress(const QString &, int, int, double) override {}
		void log(QtMsgType, const QString &) override {}
		void trashUsed(const QString &, int) override {}
		void result(const OpResult &value) override
		{
			if (cancelAfterResult && value.state == OpResult::State::Completed)
				cancelAfterResult->store(true);
		}
	};
	// Holds background work deterministically while the dialog receives edits.
	struct BlockPool
	{
		QThreadPool *pool = QThreadPool::globalInstance();
		int previousMaximum = pool->maxThreadCount();
		QSemaphore entered;
		QSemaphore releaseWorker;
		QFuture<void> future;
		BlockPool()
		{
			pool->waitForDone();
			pool->setMaxThreadCount(1);
			future = QtConcurrent::run([this]
									   { entered.release(); releaseWorker.acquire(); });
		}
		~BlockPool()
		{
			releaseWorker.release();
			future.waitForFinished();
			pool->setMaxThreadCount(previousMaximum);
		}
	};

}

class TestOperationUi : public QObject
{
	Q_OBJECT
private slots:
	void scan_results_keep_reconciliation_issues();
	void scan_start_releases_previous_session();
	void scan_failure_closes_progress_and_restores_actions();
	void initTestCase();
	void init();
	void cleanup();
	void cleanupTestCase();
	void project_sidebar_uses_whole_inventory_totals();
	void removed_sources_refresh_status_and_project_filters();
	void undo_restores_inventory_data();
	void undo_restores_inventory();
	void feature_flags_configure_startup_and_text_undo_works();
	void menu_availability_tracks_locations_selection_and_activity();
	void select_relatives_counts_all_visible_matches_data();
	void select_relatives_counts_all_visible_matches();
	void select_relatives_counts_master_ids_even_when_names_match();
	void select_relatives_preserves_hidden_selections_without_using_them_as_seeds();
	void invert_selection_preserves_hidden_selections_data();
	void invert_selection_preserves_hidden_selections();
	void text_editing_shortcuts_remain_native();
	void table_widths_change_only_on_request_and_reset_each_session();
	void precompute_gate_hides_controls_and_clears_filters();
	void optional_columns_match_csv_and_preserve_retained_sort();
	void bin_metadata_lifecycle_matches_table_and_csv();
	void omf_gate_controls_scans_and_removes_legacy_rows();
	void added_locations_require_managed_media_structure();
	void volume_refresh_clears_selection_and_retains_unique_manual_paths();
	void startup_prunes_expired_journals_with_undo_disabled();
	void unfinished_business_is_the_single_file_recovery_command();
	void unfinished_business_merges_jobs_and_updates_choices();
	void unfinished_business_gate_restore_does_not_start_waiting_job();
	void unfinished_business_gate_stop_only_dismisses_selected_job();
	void interrupted_dialog_escape_leaves_job_pending();
	void interrupted_dialog_close_leaves_job_pending();
	void interrupted_dialog_stop_keeps_completed_effects();
	void interrupted_dialog_resume_starts_only_old_job();
	void interrupted_undo_resumes_with_debug_flag_off();
	void restore_action_survives_dismissal_later_jobs_and_close();
	void restore_originals_keeps_completed_copy_and_refreshes_rows();
	void resume_restoration_refreshes_rows();
	void blocked_restore_remains_available();
	void restore_can_select_another_retained_job();
	void startup_offers_retained_originals();
	void restore_originals_respects_busy_gate();
	void observed_removals_prune_rows_even_when_job_needs_attention();
	void completed_results_preserve_details_and_refresh_signals();
	void rebalance_live_counts_follow_confirmed_results();
	void rebalance_finished_recounts_uncertain_moves_and_absent_folders();
	void rebalance_finished_marks_unavailable_root();
	void rebalance_engine_results_and_final_counts_data();
	void rebalance_engine_results_and_final_counts();
	void rebalance_dialog_blocks_other_operation_entrypoints();
	void scan_activity_blocks_operations_even_if_button_state_changes();
	void rebalance_resume_keeps_running_job_activity();
	void same_session_refresh_and_stale_result_guard();
	void progress_cancel_is_acknowledged_once();
	void trash_fallback_choice_data();
	void trash_fallback_choice();
	void trash_fallback_cancel_closes_dialog();
	void trash_fallback_without_handler_returns_without_waiting();
	void trash_fallback_destruction_unblocks_without_gui_events();
	void trash_fallback_controller_destruction_closes_dialog();
	void preview_background_checks_discard_superseded_results();
	void preview_policy_changes_refresh_space_and_same_file_is_no_effect();

private:
	OpRequest request(const QString &name = QStringLiteral("old"), int count = 1);
	QString makeInterrupted(OpRequest request, bool completeFirst = false);
	QString makeRetainedOriginal(const QString &name = QStringLiteral("retained"));
	void clickInterrupted(const QString &button);
	void clickRestoreOriginals(const QString &button, const OperationRecovery::Restorable &job);
	QString path(const QString &relative) const { return m_root + '/' + relative; }
	QByteArray m_previousJournalDir;
	bool m_hadJournalDir = false;
	std::unique_ptr<QTemporaryDir> m_temp;
	QString m_root;
};

void TestOperationUi::scan_results_keep_reconciliation_issues()
{
	QTemporaryDir temporary;
	QVERIFY(temporary.isValid());
	const QString folder = temporary.path() + QStringLiteral("/Avid MediaFiles/MXF/1");
	QVERIFY(put(folder + QStringLiteral("/unknown.mxf"), QByteArrayLiteral("unreadable test header")));
	QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/msmFMID.pmr"), folder + QStringLiteral("/index.pmr")));
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy issues(window.m_scanner, &MediaScanner::scanIssuesFinished);
	window.startScanWithPaths({temporary.path()});
	QVERIFY(issues.wait(10000));
	QTRY_COMPARE(window.m_model->rowCount(), 1);
	QVERIFY(!qvariant_cast<QVector<ScanIssue>>(issues.first().first()).isEmpty());
	QCOMPARE(window.m_model->scanIssues().size(), qvariant_cast<QVector<ScanIssue>>(issues.first().first()).size());
}

void TestOperationUi::scan_start_releases_previous_session()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	auto previous = QSharedPointer<Canon::ScanResult>::create();
	const QWeakPointer<Canon::ScanResult> receipt(previous);
	MediaFile row;
	row.mediaFilePath = path("previous.mxf");
	row.canonScan = previous;
	window.m_model->setMediaFiles({row});
	row.canonScan.clear();
	previous.clear();
	QVERIFY(!receipt.isNull());
	QSignalSpy finished(window.m_scanner, &MediaScanner::scanFinished);
	window.startScanWithPaths({m_root});
	QCOMPARE(window.m_model->rowCount(), 0);
	QVERIFY(receipt.isNull());
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
	QTRY_VERIFY(window.m_operations->isIdle());
}

void TestOperationUi::scan_failure_closes_progress_and_restores_actions()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_operations->setActivity(FileOperationController::Activity::Scanning);
	window.progressDialog()->begin();
	QVERIFY(!window.m_exportAction->isEnabled());
	bool explained = false;
	QTimer dismiss;
	dismiss.setInterval(1);
	connect(&dismiss, &QTimer::timeout, [&]
	{
		for (QWidget *widget : QApplication::topLevelWidgets())
			if (auto *message = qobject_cast<QMessageBox *>(widget))
				if (message->text() == QStringLiteral("Archive allocation failed"))
				{
					explained = true;
					message->accept();
				}
	});
	dismiss.start();
	// Exercise the worker/UI failure notification, without inventing a bad media format.
	emit window.m_scanner->scanFailed(QStringLiteral("Archive allocation failed"));
	QTRY_VERIFY(explained);
	QVERIFY(window.m_operations->isIdle());
	QVERIFY(!window.progressDialog()->isVisible());
	QCOMPARE(window.m_statusScanTime->text(), QStringLiteral("Scan failed"));
}

void TestOperationUi::project_sidebar_uses_whole_inventory_totals()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QVector<MediaFile> files(4);
	for (int i = 0; i < files.size(); ++i)
	{
		files[i].mediaFilePath = QStringLiteral("/media/%1.mxf").arg(i);
		files[i].sizeBytes = 100;
	}
	for (int i : {0, 1, 2})
		files[i].project = QStringLiteral("Project A");
	files[0].kind = MediaFile::Kind::Video;
	files[1].kind = MediaFile::Kind::Audio;
	files[1].dbStatus = MediaFile::DbStatus::NoReference;
	files[2].dbStatus = MediaFile::DbStatus::NoDatabase;
	window.m_model->setMediaFiles(files);
	window.m_proxy->setFilterMode(MediaFilterProxy::FilterMode::Audio);
	window.rebuildProjectList();
	QCOMPARE(window.m_proxy->rowCount(), 1);
	QCOMPARE(window.m_projectList->count(), 2);
	QCOMPARE(window.m_projectList->item(0)->text(), QStringLiteral("No project"));
	QCOMPARE(window.m_projectList->item(0)->toolTip(),
			 QStringLiteral("1 files, 100 B\n\n") + MediaFile::noProjectWhy());
	QCOMPARE(window.m_projectList->item(1)->text(), QStringLiteral("Project A"));
	QCOMPARE(window.m_projectList->item(1)->toolTip(), QStringLiteral("3 files, 300 B"));

	window.m_projectList->item(1)->setSelected(true);
	emit window.m_operations->sourcesRemoved({files[0].mediaFilePath});
	QVERIFY(window.m_projectList->item(1)->isSelected());
	QCOMPARE(window.m_projectList->item(1)->toolTip(), QStringLiteral("2 files, 200 B"));
	QCOMPARE(window.m_proxy->rowCount(), 1);

	emit window.m_operations->sourcesRemoved({files[1].mediaFilePath, files[2].mediaFilePath, files[3].mediaFilePath});
	QCOMPARE(window.m_projectList->count(), 0);
}

void TestOperationUi::removed_sources_refresh_status_and_project_filters()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QVector<MediaFile> files(3);
	for (int i = 0; i < files.size(); ++i)
	{
		files[i].mediaFilePath = path(QStringLiteral("%1.mxf").arg(i));
		files[i].clipName = QString::number(i);
		files[i].project = i < 2 ? QStringLiteral("Project A") : QStringLiteral("Project B");
		files[i].sizeBytes = 100 * (1 << i);
	}
	window.onScanFinished(files);
	window.m_projectList->item(0)->setSelected(true);
	window.m_tableView->selectAll();
	QTRY_COMPARE(window.m_statusFiles->text(), QStringLiteral("2 files (filtered from 3)"));
	QCOMPARE(window.m_statusSize->text(), QStringLiteral("300 B"));
	QCOMPARE(window.m_statusSelected->text(), QStringLiteral("2 selected"));
	QTRY_COMPARE(window.m_statusSelSize->text(), QStringLiteral("300 B selected"));

	// Removing only part of the selected project retains its filter and selection.
	emit window.m_operations->sourcesRemoved({files[0].mediaFilePath});
	QTRY_COMPARE(window.m_statusFiles->text(), QStringLiteral("1 files (filtered from 2)"));
	QCOMPARE(window.m_statusSize->text(), QStringLiteral("200 B"));
	QCOMPARE(window.m_statusSelected->text(), QStringLiteral("1 selected"));
	QTRY_COMPARE(window.m_statusSelSize->text(), QStringLiteral("200 B selected"));
	QCOMPARE(window.m_projectList->selectedItems().size(), 1);
	QCOMPARE(window.m_projectList->selectedItems().first()->text(), QStringLiteral("Project A"));
	QCOMPARE(window.selectedFiles().size(), 1);
	QCOMPARE(window.selectedFiles().first().mediaFilePath, files[1].mediaFilePath);
	QVERIFY(!window.m_chipsBar->isHidden());

	// Removing its last file clears the dead project filter and reveals Project B.
	emit window.m_operations->sourcesRemoved({files[1].mediaFilePath});
	QTRY_COMPARE(window.m_statusFiles->text(), QStringLiteral("1 files"));
	QCOMPARE(window.m_statusSize->text(), QStringLiteral("400 B"));
	QCOMPARE(window.m_projectList->count(), 1);
	QCOMPARE(window.m_projectList->item(0)->text(), QStringLiteral("Project B"));
	QVERIFY(window.m_projectList->selectedItems().isEmpty());
	QVERIFY(window.m_chipsBar->isHidden());
	QVERIFY(window.m_statusSelected->isHidden());
	QVERIFY(window.m_statusSelSize->isHidden());
	QVERIFY(window.selectedFiles().isEmpty());
	QCOMPARE(window.fileAtProxyRow(0).mediaFilePath, files[2].mediaFilePath);

	emit window.m_operations->sourcesRemoved({files[2].mediaFilePath});
	QTRY_COMPARE(window.m_statusFiles->text(), QStringLiteral("0 files"));
	QCOMPARE(window.m_statusSize->text(), QStringLiteral("0 B"));
	QCOMPARE(window.m_projectList->count(), 0);
}

void TestOperationUi::undo_restores_inventory_data()
{
	QTest::addColumn<OpKind>("kind");
	QTest::addColumn<bool>("keepOtherLocation");
	QTest::addColumn<bool>("blockUndo");
	QTest::newRow("move-last-file") << OpKind::Move << false << false;
	QTest::newRow("move-with-other-location") << OpKind::Move << true << false;
	QTest::newRow("delete-with-other-location") << OpKind::Delete << true << false;
	QTest::newRow("failed-undo-does-not-restore-row") << OpKind::Move << true << true;
}

void TestOperationUi::undo_restores_inventory()
{
	QFETCH(OpKind, kind);
	QFETCH(bool, keepOtherLocation);
	QFETCH(bool, blockUndo);
	const QString sourceRoot = path("source");
	const QString source = sourceRoot + "/Avid MediaFiles/MXF/1/tone.mxf";
	QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath()));
	QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/TONE_100A01.EA7D504A.611740.mxf"), source));
	QStringList roots{sourceRoot};
	const QString other = path("unaffected/Avid MediaFiles/MXF/1/other.mxf");
	if (keepOtherLocation)
	{
		QVERIFY(put(other, QByteArray(1024, 'u')));
		roots.append(path("unaffected"));
	}
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	auto *operations = window.m_operations;
	// Keep disposable Delete fixtures in the test's private Trash without a popup.
	disconnect(operations, &FileOperationController::mediaMusterTrashUsed,
			   &window, &MainWindow::showMediaMusterTrashDialog);
	operations->setUndoEnabled(true);
	QSignalSpy scanned(window.m_scanner, &MediaScanner::scanFinished);
	QSignalSpy finished(operations->manager(), &OpManager::operationFinished);
	QSignalSpy restored(operations, &FileOperationController::originalsRestored);
	window.startScanWithPaths(roots);
	QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 1, 15000);
	QTRY_VERIFY(operations->isIdle());
	QCOMPARE(window.m_model->rowCount(), keepOtherLocation ? 2 : 1);
	MediaFile original;
	for (const auto &file : window.m_model->allFiles())
		if (file.mediaFilePath == source)
			original = file;
	QCOMPARE(original.mediaFilePath, source);
	QVERIFY(!original.project.isEmpty());
	window.doUpdateStatusBar();
	const QString originalCount = window.m_statusFiles->text();
	const QString originalSize = window.m_statusSize->text();

	OpRequest forward;
	forward.kind = kind;
	forward.destRoot = path("destination");
	forward.diagnosticTrashRoot = path("trash");
	forward.items = OpManager::itemsFromMediaFiles({original}, {});
	QVERIFY(QDir().mkpath(forward.destRoot));
	QVERIFY(operations->dispatchRequest(forward));
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_VERIFY(operations->isIdle() && !operations->m_historyLoading);
	QVERIFY(!QFileInfo::exists(source));
	QCOMPARE(window.m_model->rowCount(), (keepOtherLocation ? 1 : 0) + (kind == OpKind::Move ? 1 : 0));
	QCOMPARE(window.m_projectList->findItems(original.project, Qt::MatchExactly).isEmpty(), kind != OpKind::Move);
	QCOMPARE(restored.count(), 0);
	if (blockUndo)
		QVERIFY(put(source, QByteArray("different file occupying the original location")));

	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoJournalPath = operations->m_undoCandidate.journalPath;
	undo.diagnosticTrashRoot = path("trash");
	QVERIFY(!undo.undoJournalPath.isEmpty());
	QVERIFY(operations->dispatchRequest(undo));
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 15000);
	QVERIFY(QFileInfo::exists(source));
	if (blockUndo)
	{
		QTRY_VERIFY(operations->isIdle() && !operations->m_historyLoading);
		QCOMPARE(finished.last().at(0).toInt(), 0);
		QVERIFY(finished.last().at(1).toInt() > 0);
		QCOMPARE(restored.count(), 0);
		QCOMPARE(scanned.count(), 1);
		QCOMPARE(window.m_model->rowCount(), kind == OpKind::Move ? 2 : 1);
		QCOMPARE(window.m_projectList->findItems(original.project, Qt::MatchExactly).isEmpty(), kind != OpKind::Move);
		return;
	}
	QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 1000);
	QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 2, 15000);
	QTRY_VERIFY(operations->isIdle() && !operations->m_historyLoading);
	QCOMPARE(window.m_model->rowCount(), keepOtherLocation ? 2 : 1);
	QSet<QString> displayed;
	for (const auto &file : window.m_model->allFiles())
		displayed.insert(file.mediaFilePath);
	QCOMPARE(displayed, keepOtherLocation ? QSet<QString>({source, other}) : QSet<QString>({source}));
	QCOMPARE(window.m_projectList->findItems(original.project, Qt::MatchExactly).size(), 1);
	QTRY_COMPARE(window.m_statusFiles->text(), originalCount);
	QCOMPARE(window.m_statusSize->text(), originalSize);
}

void TestOperationUi::initTestCase()
{
	m_hadJournalDir = qEnvironmentVariableIsSet("MEDIAMUSTER_JOURNAL_DIR");
	m_previousJournalDir = qgetenv("MEDIAMUSTER_JOURNAL_DIR");
}
void TestOperationUi::init()
{
	m_temp = std::make_unique<QTemporaryDir>();
	QVERIFY(m_temp->isValid());
	m_root = OpJournal::canonicalPath(m_temp->path());
	qputenv("MEDIAMUSTER_JOURNAL_DIR", path("journals").toUtf8());
}
void TestOperationUi::cleanup()
{
	QThreadPool::globalInstance()->waitForDone();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	m_temp.reset();
}
void TestOperationUi::cleanupTestCase()
{
	if (m_hadJournalDir)
		qputenv("MEDIAMUSTER_JOURNAL_DIR", m_previousJournalDir);
	else
		qunsetenv("MEDIAMUSTER_JOURNAL_DIR");
}
OpRequest TestOperationUi::request(const QString &name, int count)
{
	OpRequest result;
	result.destRoot = path(name + "/destination");
	QDir().mkpath(result.destRoot);
	for (int i = 0; i < count; ++i)
	{
		OpItem item;
		item.name = QStringLiteral("clip-%1.bin").arg(i);
		item.src = path(name + "/source/" + item.name);
		item.bytes = 64 * 1024;
		if (!put(item.src, QByteArray(int(item.bytes), char('a' + i))))
			qFatal("Cannot write disposable UI fixture");
		result.items.append(item);
	}
	return result;
}
QString TestOperationUi::makeInterrupted(OpRequest request, bool completeFirst)
{
	if (completeFirst)
	{
		std::atomic<bool> cancel{false};
		Sink sink;
		sink.cancelAfterResult = &cancel;
		OpRunner runner(sink, cancel);
		const auto totals = runner.run(request, path("journals"));
		if (totals.succeeded != 1)
			qFatal("Cannot prepare interrupted UI fixture");
		const auto pending = OperationRecovery::pending(path("journals"));
		if (pending.size() != 1)
			qFatal("Interrupted UI fixture was not resumable");
		return pending.first().journalPath;
	}
	OpJournal journal;
	QString error;
	if (!journal.create(request, path("journals"), error) || !journal.finish(true))
		qFatal("Cannot prepare interrupted journal: %s", qPrintable(error));
	return journal.path();
}
void TestOperationUi::clickInterrupted(const QString &button)
{
	auto *timer = new QTimer(this);
	timer->setInterval(1);
	connect(timer, &QTimer::timeout, this, [timer, button]
			{
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) return; // The asynchronous journal read may still be running.
		timer->stop();
		timer->deleteLater();
		QTimer::singleShot(5000, dialog, &QDialog::reject); // Also closes if an assertion fails.
		QCOMPARE(dialog->objectName(), QStringLiteral("unfinishedBusinessDialog"));
		auto *heading = dialog->findChild<QLabel *>(QStringLiteral("unfinishedBusinessHeading"));
		auto *jobs = dialog->findChild<QComboBox *>(QStringLiteral("unfinishedBusinessJob"));
		QVERIFY(heading && jobs);
		QCOMPARE(heading->text(), QStringLiteral("Resume the interrupted job?"));
		auto *summary = dialog->findChild<QLabel *>(QStringLiteral("unfinishedBusinessSummary"));
		QVERIFY(summary && !summary->text().isEmpty());
		auto *resume = dialog->findChild<QPushButton *>(QStringLiteral("resumeInterruptedJobButton"));
		auto *stop = dialog->findChild<QPushButton *>(QStringLiteral("stopInterruptedJobButton"));
		QVERIFY(resume && stop);
		QCOMPARE(resume->text(), QStringLiteral("Resume"));
		QCOMPARE(stop->text(), QStringLiteral("Stop"));
		for (auto *button : dialog->findChildren<QPushButton *>())
			QVERIFY(button->text().remove('&') != QStringLiteral("Cancel"));
		if (button == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
		else if (button == "close") dialog->close();
		else
		{
			auto *target = dialog->findChild<QPushButton *>(button);
			QVERIFY(target);
			target->click();
		} });
	timer->start();
}

QString TestOperationUi::makeRetainedOriginal(const QString &name)
{
	// A move interrupted after relocating its
	// original. No destination evidence is needed to restore that original.
	auto old = request(name);
	old.kind = OpKind::Move;
	old.copyThenRemove = true;
	const auto mediaPath = path(name + "/Avid MediaFiles/MXF/1/clip-0.mxf");
	if (!QDir().mkpath(QFileInfo(mediaPath).absolutePath()) ||
		!QFile::rename(old.items[0].src, mediaPath))
		qFatal("Cannot rename UI fixture");
	old.items[0].src = mediaPath;
	old.items[0].name = QStringLiteral("clip-0.mxf");
	OpJournal journal;
	QString error;
	if (!journal.create(old, path("journals"), error))
		qFatal("Cannot create retained-original journal: %s", qPrintable(error));
	auto entry = journal.record().entries.first();
	entry.dst = old.destRoot + '/' + entry.item.name;
	entry.retirement = QFileInfo(entry.item.src).absolutePath() + "/.mediamuster-retire-" +
					   QUuid::createUuid().toString(QUuid::WithoutBraces) + "/payload.retired";
	if (!QFile::copy(entry.item.src, entry.dst) ||
		!QDir().mkpath(QFileInfo(entry.retirement).absolutePath()) ||
		!QFile::rename(entry.item.src, entry.retirement))
		qFatal("Cannot prepare retained-original payload");
	entry.mechanism = QStringLiteral("copy");
	entry.landed = OpFile::inspect(entry.dst);
	entry.copyDurable = true;
	entry.metadataComplete = true;
	entry.step = OpJournal::Step::SourceRetained;
	if (!journal.save(entry) || !journal.markCopiesComplete() || !journal.finish(true))
		qFatal("Cannot save retained-original journal");
	return journal.path();
}

void TestOperationUi::clickRestoreOriginals(const QString &button,
											const OperationRecovery::Restorable &job)
{
	auto *timer = new QTimer(this);
	timer->setInterval(1);
	connect(timer, &QTimer::timeout, this, [timer, button, job]
			{
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) return;
		timer->stop();
		timer->deleteLater();
		QTimer::singleShot(5000, dialog, &QDialog::reject); // Also closes if an assertion fails.
		QCOMPARE(dialog->objectName(), QStringLiteral("unfinishedBusinessDialog"));
		auto *jobs = dialog->findChild<QComboBox *>(QStringLiteral("unfinishedBusinessJob"));
		QVERIFY(jobs);
		const int selectedJob = jobs->findData(job.journalPath);
		QVERIFY(selectedJob >= 0);
		jobs->setCurrentIndex(selectedJob);
		auto *paths = dialog->findChild<QPlainTextEdit *>(QStringLiteral("restoreOriginalPaths"));
		QVERIFY(paths && paths->isReadOnly());
		for (const auto &path : job.originals)
			QVERIFY(paths->toPlainText().contains(QDir::toNativeSeparators(path)));
		for (const auto &path : job.retainedPaths)
			QVERIFY(paths->toPlainText().contains(QDir::toNativeSeparators(path)));
		if (button == "enter")
		{
			QTest::keyClick(dialog, Qt::Key_Return);
			QVERIFY(dialog->isVisible());
			QTest::keyClick(dialog, Qt::Key_Escape);
		}
		else if (button == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
		else if (button == "close") dialog->close();
		else
		{
			auto *target = dialog->findChild<QPushButton *>(button);
			QVERIFY(target);
			target->click();
		} });
	timer->start();
}

void TestOperationUi::volume_refresh_clears_selection_and_retains_unique_manual_paths()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.rebuildVolumeList({});
	const QString folder = path("manual/Avid MediaFiles");
	QVERIFY(QDir().mkpath(folder + "/MXF/1"));
	window.addVolumePath(folder);
	QCOMPARE(window.m_volumeList->count(), 1);
	QVERIFY(window.m_volumeList->item(0)->isSelected());
	window.addVolumePath(folder + "/.");
	QCOMPARE(window.m_volumeList->count(), 1);
	QCOMPARE(window.m_manualVolumes.size(), 1);
	window.rebuildVolumeList({});
	QCOMPARE(window.m_volumeList->count(), 1);
	QVERIFY(window.m_volumeList->selectedItems().isEmpty());
	VolumeInfo volume;
	volume.name = QStringLiteral("Detected media");
	volume.path = folder;
	volume.hasAvidMedia = true;
	window.rebuildVolumeList({volume});
	QCOMPARE(window.m_volumeList->count(), 1);
	QVERIFY(window.m_volumeList->selectedItems().isEmpty());
	window.m_volumeList->item(0)->setSelected(true);
	window.rebuildVolumeList({volume});
	QVERIFY(window.m_volumeList->selectedItems().isEmpty());
}

void TestOperationUi::added_locations_require_managed_media_structure()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	const QString copiedRoot = path("Desktop/Backup/Avid MediaFiles");
	const QString legacyRoot = path("Desktop/Legacy/OMFI MediaFiles");
	QVERIFY(QDir().mkpath(copiedRoot + "/MXF/1"));
	QVERIFY(QDir().mkpath(legacyRoot));
	QVERIFY(put(path("loose/msmFMID.pmr"), "not an admission rule"));
	QVERIFY(QDir().mkpath(path("standalone/MXF/1")));
	QVERIFY(QDir().mkpath(path("ume/Avid MediaFiles/UME/1")));
	const int originalCount = window.m_volumeList->count();
	for (const QString &rejected : {path("loose"), path("standalone/MXF"),
									path("ume/Avid MediaFiles/UME/1"), path("Desktop")})
	{
		window.addVolumePath(rejected);
		QCOMPARE(window.m_volumeList->count(), originalCount);
		QVERIFY(!window.m_manualVolumes.contains(rejected));
	}
	window.addVolumePath(copiedRoot);
	window.addVolumePath(legacyRoot);
	QCOMPARE(window.m_volumeList->count(), originalCount + 2);
	QVERIFY(window.m_manualVolumes.contains(copiedRoot));
	QVERIFY(window.m_manualVolumes.contains(legacyRoot));
	QCOMPARE(window.m_omfScanEnabled, FeatureFlags::kOmfScan); // Adding a location preserves the build setting.
	window.addVolumePath(copiedRoot);
	QCOMPARE(window.m_volumeList->count(), originalCount + 2);
}

void TestOperationUi::feature_flags_configure_startup_and_text_undo_works()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.show();
	auto *debugMenu = window.findChild<QMenu *>(QStringLiteral("debugMenu"));
	QCOMPARE(debugMenu != nullptr, FeatureFlags::kDebugMenu);
	for (const auto *name : {"enableOmfDebugAction", "enablePrecomputesDebugAction", "enableUndoDebugAction"})
		QVERIFY(!window.findChild<QAction *>(QString::fromLatin1(name)));
	QCOMPARE(window.m_omfScanEnabled, FeatureFlags::kOmfScan);
	QCOMPARE(window.m_precomputesEnabled, FeatureFlags::kPrecomputeFilter);
	QCOMPARE(window.m_model->precomputesEnabled(), FeatureFlags::kPrecomputeFilter);
	QCOMPARE(window.m_proxy->precomputesEnabled(), FeatureFlags::kPrecomputeFilter);
	QCOMPARE(window.m_precomputeFilterAction->isVisible(), FeatureFlags::kPrecomputeFilter);
	QCOMPARE(window.m_operations->m_undoEnabled, FeatureFlags::kUndo);
	QCOMPARE(window.m_operations->m_undoAction->isVisible(), FeatureFlags::kUndo);
	QCOMPARE(window.m_operations->m_undoAction->shortcut(),
			 FeatureFlags::kUndo ? QKeySequence(QKeySequence::Undo) : QKeySequence());
	window.m_searchField->setFocus();
	QTest::keyClicks(window.m_searchField, "typed search");
	QVERIFY(window.m_searchField->isUndoAvailable());
	QTest::keySequence(window.m_searchField, QKeySequence::Undo);
	QVERIFY(window.m_searchField->text().isEmpty());
}

void TestOperationUi::menu_availability_tracks_locations_selection_and_activity()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.setPrecomputesEnabled(false);
	window.setOmfScanEnabled(false);
	window.m_operations->setUndoEnabled(false);
	auto action = [&window](const char *name)
	{
		return window.findChild<QAction *>(QString::fromLatin1(name));
	};
	auto *scanSelected = action("scanSelectedAction");
	auto *scanAll = action("scanAllAction");
	auto *manage = action("manageMediaAction");
	auto *rebalance = action("rebalanceAction");
	auto *exportCsv = action("exportCsvAction");
	auto *reveal = action("revealInFinderAction");
	auto *relatives = action("selectRelativesAction");
	auto *inverse = action("selectInverseAction");
	for (auto *command : {scanSelected, scanAll, manage, rebalance, exportCsv, reveal, relatives, inverse})
	{
		QVERIFY(command);
		QVERIFY(!command->isEnabled());
	}
	QVERIFY(!window.m_precomputeFilterAction->isVisible());
	QVERIFY(!window.m_precomputeFilterAction->isEnabled());
	window.setPrecomputesEnabled(true);
	QVERIFY(window.m_precomputeFilterAction->isVisible());
	QVERIFY(!window.m_precomputeFilterAction->isEnabled());
	QVERIFY(!window.m_precomputeFilterButton->isEnabled());
	window.setPrecomputesEnabled(false);

	auto *location = new QListWidgetItem(QStringLiteral("Media volume"));
	location->setData(Qt::UserRole, path("volume"));
	window.m_volumeList->addItem(location);
	QVERIFY(scanAll->isEnabled());
	QVERIFY(window.m_scanAllButton->isEnabled());
	QVERIFY(!scanSelected->isEnabled());
	location->setSelected(true);
	QVERIFY(scanSelected->isEnabled());
	QVERIFY(window.m_scanButton->isEnabled());

	MediaFile file;
	file.mediaFilePath = path("volume/clip.mxf");
	file.fileName = QStringLiteral("clip.mxf");
	file.masterMobId = QStringLiteral("master-clip");
	window.onScanFinished({file});
	QVERIFY(rebalance->isEnabled());
	QVERIFY(window.m_rebalanceButton->isEnabled());
	QVERIFY(exportCsv->isEnabled());
	QVERIFY(window.m_exportCsvButton->isEnabled());
	QVERIFY(inverse->isEnabled());
	QVERIFY(!manage->isEnabled());
	QVERIFY(!reveal->isEnabled());
	QVERIFY(!relatives->isEnabled());

	window.m_tableView->selectRow(0);
	QVERIFY(manage->isEnabled());
	QVERIFY(window.m_manageMediaButton->isEnabled());
	QVERIFY(reveal->isEnabled());
	QVERIFY(relatives->isEnabled());
	window.setPrecomputesEnabled(true);
	QVERIFY(window.m_precomputeFilterAction->isEnabled());
	QVERIFY(window.m_precomputeFilterButton->isEnabled());

	window.m_operations->setActivity(FileOperationController::Activity::Scanning);
	for (auto *command : {scanSelected, scanAll, manage, rebalance, exportCsv, window.m_precomputeFilterAction})
		QVERIFY(!command->isEnabled());
	for (auto *button : {window.m_scanButton, window.m_scanAllButton, window.m_manageMediaButton,
						 window.m_rebalanceButton, window.m_exportCsvButton, window.m_precomputeFilterButton})
		QVERIFY(!button->isEnabled());
	window.m_operations->setActivity(FileOperationController::Activity::Idle);
	QVERIFY(manage->isEnabled());
	QVERIFY(exportCsv->isEnabled());

	// Remembered selections hidden by a filter cannot feed commands.
	window.onSearchChanged(QStringLiteral("no matching media"));
	QCOMPARE(window.m_proxy->rowCount(), 0);
	QVERIFY(!manage->isEnabled());
	QVERIFY(!reveal->isEnabled());
	QVERIFY(!relatives->isEnabled());
	QVERIFY(!inverse->isEnabled());
	QVERIFY(!exportCsv->isEnabled());
	QVERIFY(!window.m_exportCsvButton->isEnabled());
	QVERIFY(rebalance->isEnabled()); // Rebalance uses the whole inventory.
	window.onSearchChanged({});
	QVERIFY(manage->isEnabled());
	QVERIFY(exportCsv->isEnabled());

	window.m_tableView->clearSelection();
	QVERIFY(!manage->isEnabled());
	QVERIFY(!window.m_manageMediaButton->isEnabled());
	QVERIFY(!reveal->isEnabled());
	QVERIFY(!relatives->isEnabled());

	// Inventory changes must also clear commands seeded by an old selection.
	window.m_tableView->selectRow(0);
	window.m_model->removeFilesByPath({file.mediaFilePath});
	QCOMPARE(window.m_proxy->rowCount(), 0);
	for (auto *command : {manage, rebalance, exportCsv, reveal, relatives, inverse, window.m_precomputeFilterAction})
		QVERIFY(!command->isEnabled());
	window.onScanFinished({file});
	window.m_tableView->selectRow(0);
	QVERIFY(manage->isEnabled());
	QVERIFY(relatives->isEnabled());
	window.m_model->setMediaFiles({});
	QCOMPARE(window.m_proxy->rowCount(), 0);
	for (auto *command : {manage, rebalance, exportCsv, reveal, relatives, inverse, window.m_precomputeFilterAction})
		QVERIFY(!command->isEnabled());

	window.m_volumeList->clearSelection();
	QVERIFY(!scanSelected->isEnabled());
	QVERIFY(!window.m_scanButton->isEnabled());
	QVERIFY(scanAll->isEnabled());
	window.m_volumeList->clear();
	QVERIFY(!scanAll->isEnabled());
	QVERIFY(!window.m_scanAllButton->isEnabled());
}

void TestOperationUi::select_relatives_counts_all_visible_matches_data()
{
	QTest::addColumn<int>("total");
	QTest::addColumn<int>("initiallySelected");
	QTest::addColumn<bool>("hideLastRelative");
	QTest::addColumn<QString>("expectedMessage");
	QTest::newRow("solo-file") << 1 << 1 << false
							   << QStringLiteral("Selected 1 file across 1 master clip.");
	QTest::newRow("audio-pair") << 2 << 1 << false
								<< QStringLiteral("Selected 2 files across 1 master clip.");
	QTest::newRow("trio") << 3 << 1 << false
						  << QStringLiteral("Selected 3 files across 1 master clip.");
	QTest::newRow("partly-selected-trio") << 3 << 2 << false
										  << QStringLiteral("Selected 3 files across 1 master clip.");
	QTest::newRow("filtered-relative") << 2 << 1 << true
									   << QStringLiteral("Selected 1 file across 1 master clip.");
}

void TestOperationUi::select_relatives_counts_all_visible_matches()
{
	QFETCH(int, total);
	QFETCH(int, initiallySelected);
	QFETCH(bool, hideLastRelative);
	QFETCH(QString, expectedMessage);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QVector<MediaFile> files(total + 1);
	for (int i = 0; i < files.size(); ++i)
	{
		auto &file = files[i];
		file.fileName = QStringLiteral("audio-%1.mxf").arg(i);
		file.mediaFilePath = path(file.fileName);
		file.clipName = QStringLiteral("Interview");
		file.fileMobId = QStringLiteral("file-%1").arg(i);
		file.masterMobId = i < total ? QStringLiteral("master-a") : QStringLiteral("unrelated-master");
		file.kind = MediaFile::Kind::Audio;
	}
	// The unrelated file deliberately shares the clip name, but not the master ID.
	if (hideLastRelative)
		files[total - 1].kind = MediaFile::Kind::Video;
	window.onScanFinished(files);
	if (hideLastRelative)
		window.m_proxy->setFilterMode(MediaFilterProxy::FilterMode::Audio);
	const int visibleRelatives = total - (hideLastRelative ? 1 : 0);
	QCOMPARE(window.m_proxy->rowCount(), visibleRelatives + 1);
	for (int row = 0; row < initiallySelected; ++row)
	{
		const auto index = window.m_proxy->mapFromSource(window.m_model->index(row, 0));
		QVERIFY(index.isValid());
		window.m_tableView->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}
	auto *command = window.findChild<QAction *>(QStringLiteral("selectRelativesAction"));
	QVERIFY(command && command->isEnabled());
	window.m_console->clear();
	command->trigger();
	QCOMPARE(appMessages(window.m_console), QStringList{expectedMessage});
	QSet<QString> expectedPaths;
	for (int row = 0; row < visibleRelatives; ++row)
		expectedPaths.insert(files[row].mediaFilePath);
	const auto selectedPaths = [&window]
	{
		QSet<QString> paths;
		for (const auto &file : window.selectedFiles())
			paths.insert(file.mediaFilePath);
		return paths;
	};
	QCOMPARE(selectedPaths(), expectedPaths);
	QCOMPARE(window.m_proxy->rowCount(), visibleRelatives + 1);

	// Repeating the command reports the same totals, including existing selections.
	window.m_console->clear();
	command->trigger();
	QCOMPARE(appMessages(window.m_console), QStringList{expectedMessage});
	QCOMPARE(selectedPaths(), expectedPaths);
	if (hideLastRelative)
	{
		QVERIFY(!window.m_proxy->mapFromSource(window.m_model->index(total - 1, 0)).isValid());
		window.m_proxy->setFilterMode(MediaFilterProxy::FilterMode::All);
		QCOMPARE(window.m_proxy->rowCount(), total + 1);
		QCOMPARE(selectedPaths(), expectedPaths);
	}
}

void TestOperationUi::select_relatives_counts_master_ids_even_when_names_match()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QVector<MediaFile> files(6);
	for (int i = 0; i < files.size(); ++i)
	{
		auto &file = files[i];
		file.fileName = QStringLiteral("channel-%1.mxf").arg(i);
		file.mediaFilePath = path(file.fileName);
		file.clipName = QStringLiteral("Same clip name");
		file.fileMobId = QStringLiteral("file-%1").arg(i);
		file.masterMobId = i < 2   ? QStringLiteral("master-a")
						   : i < 5 ? QStringLiteral("master-b")
								   : QStringLiteral("unrelated-master");
		file.kind = MediaFile::Kind::Audio;
	}
	window.onScanFinished(files);
	for (int row : {0, 2})
	{
		const auto index = window.m_proxy->mapFromSource(window.m_model->index(row, 0));
		QVERIFY(index.isValid());
		window.m_tableView->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}
	window.m_console->clear();
	window.onSelectRelatives();
	const QStringList expectedMessages{QStringLiteral("Selected 5 files across 2 master clips.")};
	QCOMPARE(appMessages(window.m_console), expectedMessages);
	QSet<QString> selectedPaths;
	for (const auto &file : window.selectedFiles())
		selectedPaths.insert(file.mediaFilePath);
	QSet<QString> expectedPaths;
	for (int row = 0; row < 5; ++row)
		expectedPaths.insert(files[row].mediaFilePath);
	QCOMPARE(selectedPaths, expectedPaths);

	window.m_console->clear();
	window.onSelectRelatives();
	QCOMPARE(appMessages(window.m_console), expectedMessages);
	QCOMPARE(window.selectedFiles().size(), 5);
}

void TestOperationUi::select_relatives_preserves_hidden_selections_without_using_them_as_seeds()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	const QStringList names{
		QStringLiteral("visible-seed-a.mxf"), QStringLiteral("visible-relative-a.mxf"),
		QStringLiteral("hidden-seed-b.mxf"), QStringLiteral("visible-relative-b.mxf")};
	QVector<MediaFile> files(names.size());
	for (int i = 0; i < files.size(); ++i)
	{
		auto &file = files[i];
		file.fileName = names[i];
		file.mediaFilePath = path(file.fileName);
		file.clipName = i < 2 ? QStringLiteral("Interview") : QStringLiteral("Cutaway");
		file.fileMobId = QStringLiteral("file-%1").arg(i);
		file.masterMobId = i < 2 ? QStringLiteral("master-a") : QStringLiteral("master-b");
		file.kind = MediaFile::Kind::Audio;
	}
	window.onScanFinished(files);
	for (int row : {0, 2})
		window.m_tableView->selectionModel()->select(
			window.m_proxy->mapFromSource(window.m_model->index(row, 0)),
			QItemSelectionModel::Select | QItemSelectionModel::Rows);
	const auto selectedPaths = [&window]
	{
		QSet<QString> paths;
		for (const auto &file : window.selectedFiles())
			paths.insert(file.mediaFilePath);
		return paths;
	};
	QCOMPARE(selectedPaths(), QSet<QString>({files[0].mediaFilePath, files[2].mediaFilePath}));

	// Hide B's selected seed, but leave its unselected relative visible.
	window.onSearchChanged(QStringLiteral("visible-"));
	QCOMPARE(window.m_proxy->rowCount(), 3);
	QCOMPARE(selectedPaths(), QSet<QString>{files[0].mediaFilePath});
	window.m_console->clear();
	window.onSelectRelatives();
	QCOMPARE(appMessages(window.m_console),
			 QStringList{QStringLiteral("Selected 2 files across 1 master clip.")});
	QCOMPARE(selectedPaths(), QSet<QString>({files[0].mediaFilePath, files[1].mediaFilePath}));
	QCOMPARE(window.m_proxy->rowCount(), 3);

	// B's earlier selection returns, but its relative was never selected.
	window.onSearchChanged({});
	QCOMPARE(window.m_proxy->rowCount(), 4);
	QCOMPARE(selectedPaths(), QSet<QString>({files[0].mediaFilePath, files[1].mediaFilePath, files[2].mediaFilePath}));
}

void TestOperationUi::invert_selection_preserves_hidden_selections_data()
{
	QTest::addColumn<int>("selectedVisibleCount");
	QTest::newRow("none-visible-selected") << 0;
	QTest::newRow("some-visible-selected") << 1;
	QTest::newRow("all-visible-selected") << 2;
}

void TestOperationUi::invert_selection_preserves_hidden_selections()
{
	QFETCH(int, selectedVisibleCount);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QVector<MediaFile> files(3);
	for (int row = 0; row < files.size(); ++row)
	{
		auto &file = files[row];
		file.fileName = row < 2 ? QStringLiteral("visible-%1.mxf").arg(row) : QStringLiteral("hidden.mxf");
		file.mediaFilePath = path(file.fileName);
		file.kind = MediaFile::Kind::Audio;
	}
	window.onScanFinished(files);
	QSet<QString> initialPaths{files[2].mediaFilePath};
	for (int row = 0; row < files.size(); ++row)
	{
		if (row < 2 && row >= selectedVisibleCount)
			continue;
		window.m_tableView->selectionModel()->select(
			window.m_proxy->mapFromSource(window.m_model->index(row, 0)),
			QItemSelectionModel::Select | QItemSelectionModel::Rows);
		initialPaths.insert(files[row].mediaFilePath);
	}
	const auto selectedPaths = [&window]
	{
		QSet<QString> paths;
		for (const auto &file : window.selectedFiles())
			paths.insert(file.mediaFilePath);
		return paths;
	};
	QCOMPARE(selectedPaths(), initialPaths);

	window.onSearchChanged(QStringLiteral("visible-"));
	QCOMPARE(window.m_proxy->rowCount(), 2);
	window.onInvertSelection();
	QSet<QString> invertedPaths;
	for (int row = selectedVisibleCount; row < 2; ++row)
		invertedPaths.insert(files[row].mediaFilePath);
	QCOMPARE(selectedPaths(), invertedPaths);
	window.onSearchChanged({});
	invertedPaths.insert(files[2].mediaFilePath);
	QCOMPARE(selectedPaths(), invertedPaths);

	// Inverting twice restores the visible selection without losing the hidden one.
	window.onSearchChanged(QStringLiteral("visible-"));
	window.onInvertSelection();
	window.onSearchChanged({});
	QCOMPARE(selectedPaths(), initialPaths);
}

void TestOperationUi::text_editing_shortcuts_remain_native()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.show();
	window.activateWindow();
	window.m_searchField->setFocus();
	QTRY_COMPARE(QApplication::focusWidget(), window.m_searchField);
	window.m_searchField->setText(QStringLiteral("native text"));
	QTest::keySequence(window.m_searchField, QKeySequence::SelectAll);
	QCOMPARE(window.m_searchField->selectedText(), QStringLiteral("native text"));
	QTest::keySequence(window.m_searchField, QKeySequence::Copy);
	QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("native text"));
	QTest::keySequence(window.m_searchField, QKeySequence::Cut);
	QVERIFY(window.m_searchField->text().isEmpty());
	QTest::keySequence(window.m_searchField, QKeySequence::Paste);
	QCOMPARE(window.m_searchField->text(), QStringLiteral("native text"));
}

void TestOperationUi::table_widths_change_only_on_request_and_reset_each_session()
{
	using Column = MediaTableModel::Column;
	const int clipColumn = static_cast<int>(Column::ClipName);
	const int fileColumn = static_cast<int>(Column::FileName);
	int initialFilePosition = -1;
	{
		MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
		auto *table = window.m_tableView;
		auto *header = table->horizontalHeader();
		initialFilePosition = header->visualIndex(fileColumn);
		QCOMPARE(table->columnWidth(clipColumn), 250);
		QCOMPARE(table->columnWidth(static_cast<int>(Column::Location)), 250);
		table->setColumnWidth(clipColumn, 333);
		header->moveSection(header->visualIndex(fileColumn), 1);

		MediaFile file;
		file.mediaFilePath = path("clip.mxf");
		file.clipName = QString(100, QLatin1Char('W'));
		window.onScanFinished({file});
		QCOMPARE(table->columnWidth(clipColumn), 333);
		QCOMPARE(header->visualIndex(fileColumn), 1);

		QAction *fit = nullptr;
		for (auto *action : window.findChildren<QAction *>())
			if (action->text() == QStringLiteral("&Resize Columns to Fit"))
				fit = action;
		QVERIFY(fit);
		fit->trigger();
		const int fittedWidth = table->columnWidth(clipColumn);
		QVERIFY(fittedWidth > 333);
		file.clipName = QStringLiteral("Short");
		window.onScanFinished({file});
		QCOMPARE(table->columnWidth(clipColumn), fittedWidth);
	}
	MainWindow freshWindow(nullptr, MainWindow::StartupMode::UiOnly);
	QCOMPARE(freshWindow.m_tableView->columnWidth(clipColumn), 250);
	QCOMPARE(freshWindow.m_tableView->horizontalHeader()->visualIndex(fileColumn), initialFilePosition);
}

void TestOperationUi::precompute_gate_hides_controls_and_clears_filters()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.setPrecomputesEnabled(false);
	window.setOmfScanEnabled(false);
	window.m_operations->setUndoEnabled(false);
	using Column = MediaTableModel::Column;
	const int typeColumn = static_cast<int>(Column::Type);
	const int precomputeTab = 3;
	const auto checkClipDurationPosition = [&]
	{
		QCOMPARE(window.m_model->clipDurationEnabled(), FeatureFlags::kClipDuration);
		if (FeatureFlags::kClipDuration)
		{
			auto *header = window.m_tableView->horizontalHeader();
			QCOMPARE(header->visualIndex(window.m_model->clipDurationColumn()),
					 header->visualIndex(static_cast<int>(Column::Duration)) + 1);
		}
	};
	checkClipDurationPosition();
	MediaFile media;
	media.mediaFilePath = path("media.mxf");
	media.fileName = QStringLiteral("media.mxf");
	media.type = MediaFile::Type::Media;
	media.project = QStringLiteral("Source project");
	media.volumePath = path("volume");
	MediaFile precompute = media;
	precompute.mediaFilePath = path("render.mxf");
	precompute.fileName = QStringLiteral("render.mxf");
	precompute.type = MediaFile::Type::Precompute;
	precompute.project = QStringLiteral("Render project");
	precompute.effect = QStringLiteral("Resize");
	precompute.effectCategory = QStringLiteral("Image");
	precompute.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	window.onScanFinished({media, precompute});
	QVERIFY(!window.m_precomputesEnabled);
	QVERIFY(!window.m_model->precomputesEnabled());
	QVERIFY(!window.m_proxy->precomputesEnabled());
	QVERIFY(!window.m_tableView->isColumnHidden(typeColumn));
	QCOMPARE(window.m_model->columnCount(), static_cast<int>(Column::PrecomputeCategory) + int(FeatureFlags::kClipDuration));
	QVERIFY(window.m_precomputeFilterButton->isHidden());
	QVERIFY(!window.m_precomputeFilterAction->isVisible());
	QVERIFY(!window.m_precomputeFilterAction->isEnabled());
	QVERIFY(!window.m_filterTabs->isTabVisible(precomputeTab));
	QCOMPARE(window.m_proxy->rowCount(), 2); // Rendered media remains manageable.
	window.onFilterChanged(precomputeTab);
	QCOMPARE(window.m_proxy->rowCount(), 2);
	window.m_showAllFilterTabs = true;
	window.updateFilterCounts();
	QVERIFY(!window.m_filterTabs->isTabVisible(precomputeTab));
	bool openedPicker = false;
	QTimer::singleShot(0, &window, [&openedPicker]
					   {
		if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
		{
			openedPicker = true;
			dialog->reject();
		} });
	window.onFilterPrecomputes(); // Disabled entry point must not open a dialog.
	QCoreApplication::processEvents();
	QVERIFY(!openedPicker);

	window.setPrecomputesEnabled(true);
	QVERIFY(window.m_precomputesEnabled);
	QVERIFY(!window.m_tableView->isColumnHidden(typeColumn));
	QCOMPARE(window.m_model->columnCount(), static_cast<int>(Column::Count_) + int(FeatureFlags::kClipDuration));
	checkClipDurationPosition();
	int detailPosition = window.m_tableView->horizontalHeader()->visualIndex(typeColumn) + 1;
	for (const auto column : {Column::PrecomputeCategory, Column::EffectCategory, Column::Effect, Column::EffectSequence})
		QCOMPARE(window.m_tableView->horizontalHeader()->visualIndex(static_cast<int>(column)), detailPosition++);
	QVERIFY(!window.m_precomputeFilterButton->isHidden());
	QVERIFY(window.m_precomputeFilterAction->isVisible());
	QVERIFY(window.m_precomputeFilterAction->isEnabled());
	QVERIFY(window.m_filterTabs->isTabVisible(precomputeTab));
	window.m_filterTabs->setCurrentIndex(precomputeTab);
	QCOMPARE(window.m_proxy->rowCount(), 1);
	window.m_proxy->setPrecomputeTreeFilter({true, {{precompute.precomputeCategoryDisplay(), precompute.effectCategory, precompute.effect}}});
	window.m_proxy->setPrecomputeVolumeFilter(precompute.volumePath);
	window.m_tableView->sortByColumn(typeColumn, Qt::DescendingOrder);
	window.setPrecomputesEnabled(false);
	QVERIFY(!window.m_precomputesEnabled);
	QVERIFY(!window.m_tableView->isColumnHidden(typeColumn));
	QVERIFY(!window.m_filterTabs->isTabVisible(precomputeTab));
	QCOMPARE(window.m_filterTabs->currentIndex(), 0);
	QCOMPARE(window.m_proxy->sortColumn(), typeColumn);
	QVERIFY(!window.m_proxy->precomputeTreeFilter().active);
	QVERIFY(window.m_proxy->precomputeVolumeFilter().isEmpty());
	QCOMPARE(window.m_proxy->rowCount(), 2);
	QCOMPARE(window.m_model->rowCount(), 2);
	QCOMPARE(window.m_model->fileAt(1).type, MediaFile::Type::Precompute);
	QCOMPARE(window.m_model->fileAt(1).effect, precompute.effect);
	QVERIFY(window.m_precomputeFilterButton->isHidden());
	QVERIFY(!window.m_precomputeFilterAction->isVisible());
	QVERIFY(!window.m_precomputeFilterAction->isEnabled());

	// Sorting by any disappearing detail column also returns to Clip Name.
	for (const auto column : {Column::PrecomputeCategory, Column::EffectCategory, Column::Effect, Column::EffectSequence})
	{
		window.setPrecomputesEnabled(true);
		window.m_tableView->sortByColumn(static_cast<int>(column), Qt::DescendingOrder);
		window.setPrecomputesEnabled(false);
		QCOMPARE(window.m_proxy->sortColumn(), static_cast<int>(Column::ClipName));
		QCOMPARE(window.m_proxy->rowCount(), 2);
	}
	// A rescan clears both the visible project selection and its predicate.
	const auto projects = window.m_projectList->findItems(media.project, Qt::MatchExactly);
	QCOMPARE(projects.size(), 1);
	projects.first()->setSelected(true);
	QCOMPARE(window.m_proxy->rowCount(), 1);
	window.onScanFinished({media, precompute});
	QVERIFY(window.m_projectList->selectedItems().isEmpty());
	QCOMPARE(window.m_proxy->rowCount(), 2);
}

void TestOperationUi::optional_columns_match_csv_and_preserve_retained_sort()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	MediaFile file;
	file.clipName = QStringLiteral("Clip");
	window.m_model->setMediaFiles({file});
	// Exercise both gates in one window, including removing and reinserting columns.
	for (const bool clip : {true, false, true})
	{
		window.setClipDurationEnabled(clip);
		for (const bool precomputes : {false, true, false})
		{
			window.setPrecomputesEnabled(precomputes);
			QStringList headings;
			auto *header = window.m_tableView->horizontalHeader();
			for (int visual = 0; visual < header->count(); ++visual)
			{
				const int logical = header->logicalIndex(visual);
				QVERIFY(!window.m_tableView->isColumnHidden(logical));
				headings.append(window.m_model->headerData(logical, Qt::Horizontal, Qt::DisplayRole).toString());
			}
			headings.removeAll(QStringLiteral("MobId"));
			headings.removeAll(QStringLiteral("MasterMobId"));
			headings.removeAll(QStringLiteral("KelpieId"));
			headings.removeAll(QStringLiteral("OmfScan"));
			headings.append({QStringLiteral("Database Status"), QStringLiteral("MobId"), QStringLiteral("MasterMobId"), QStringLiteral("KelpieId"), QStringLiteral("OmfScan")});
			QCOMPARE(MediaCsv::headerLine({precomputes, clip}), headings.join(',') + '\n');
		}
	}
	window.setPrecomputesEnabled(true);
	window.m_tableView->sortByColumn(window.m_model->clipDurationColumn(), Qt::DescendingOrder);
	window.setPrecomputesEnabled(false);
	QCOMPARE(window.m_proxy->sortColumn(), window.m_model->clipDurationColumn());
	QCOMPARE(window.m_proxy->sortOrder(), Qt::DescendingOrder);
	window.setClipDurationEnabled(false);
	QCOMPARE(window.m_proxy->sortColumn(), static_cast<int>(MediaTableModel::Column::ClipName));
}

void TestOperationUi::bin_metadata_lifecycle_matches_table_and_csv()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	MediaFile missing;
	missing.mediaFilePath = tmp.filePath("missing.mxf");
	missing.masterMobId = MobId::format(TestAvb::Master);
	MediaFile known = missing;
	known.mediaFilePath = tmp.filePath("known.mxf");
	known.clipName = QStringLiteral("Database clip");
	known.clipNameSource = MediaFile::ClipNameSource::Mdb;
	known.originalBin = QStringLiteral("Database bin");
	window.onScanFinished({missing, known});
	window.onFilterByBins();
	auto *dialog = window.m_binFilterDialog;
	QVERIFY(dialog);
	auto *list = dialog->findChild<QListWidget *>(QStringLiteral("BinList"));
	QVERIFY(list);
	QSignalSpy published(dialog, &BinFilterDialog::binsChanged);
	const auto makeBin = [&](const QString &name)
	{
		TestAvb::Document d;
		d.objects = {{"ABIN", TestAvb::bin(false, {2})},
					 {"CMPO", TestAvb::composition(false, TestAvb::Master, name.toUtf8(), 3)},
					 {"ATTR", TestAvb::attributes(false, 4)},
					 {"MCBR", TestAvb::binReference(false, (name + " bin").toUtf8(), {})}};
		return TestAvb::write(tmp.filePath(name + ".avb"), d.bytes());
	};
	const auto check = [&](const QString &expectedName, const QString &expectedBin)
	{
		QCOMPARE(window.m_model->fileAt(0).clipName, expectedName);
		QCOMPARE(window.m_model->fileAt(0).originalBin, expectedBin);
		QCOMPARE(window.m_model->fileAt(1).clipName, known.clipName);
		QCOMPARE(window.m_model->fileAt(1).originalBin, known.originalBin);
		QCOMPARE(window.m_model->fileAt(1).clipNameSource, MediaFile::ClipNameSource::Mdb);
		QVERIFY(!window.m_model->fileAt(1).originalBinFromAvb);
		QCOMPARE(window.m_proxy->rowCount(), 2);
		window.m_tableView->selectAll();
		const auto selected = window.selectedFiles();
		QCOMPARE(selected.size(), 2);
		QVector<MediaFile> visible;
		for (int row = 0; row < window.m_proxy->rowCount(); ++row)
			visible.append(window.fileAtProxyRow(row));
		for (const auto &snapshot : {visible, selected})
		{
			const auto exportPath = tmp.filePath("names.csv");
			QVERIFY(MediaCsv::write(exportPath, snapshot));
			QFile csv(exportPath);
			QVERIFY(csv.open(QIODevice::ReadOnly));
			csv.readLine(); // UTF-8 BOM and column headings.
			for (const auto &file : snapshot)
			{
				int proxyRow = 0;
				while (proxyRow < 2 && window.fileAtProxyRow(proxyRow).mediaFilePath != file.mediaFilePath)
					++proxyRow;
				QVERIFY(proxyRow < 2);
				const auto displayedName = window.m_proxy->index(proxyRow, int(MediaTableModel::Column::ClipName)).data().toString();
				const auto displayedBin = window.m_proxy->index(proxyRow, int(MediaTableModel::Column::OriginalBin)).data().toString();
				// Fixture names contain no CSV-special characters; escaping has separate coverage.
				const auto prefix = QStringLiteral("\"%1\",\"No project\",\"%2\",").arg(displayedName, displayedBin).toUtf8();
				QVERIFY(csv.readLine().startsWith(prefix));
			}
			QVERIFY(csv.atEnd());
		}
	};
	dialog->addBinFromFile(makeBin(QStringLiteral("First")));
	QTRY_COMPARE(published.count(), 1);
	check(QStringLiteral("First"), QStringLiteral("First bin"));
	dialog->addBinFromFile(makeBin(QStringLiteral("Second")));
	QTRY_COMPARE(published.count(), 2);
	check({}, {}); // Both fields conflict, while the database row stays authoritative.
	list->item(1)->setCheckState(Qt::Unchecked);
	check({}, {}); // Ticking controls filter operands, not metadata ownership.
	list->clearSelection();
	list->item(0)->setSelected(true);
	QVERIFY(QMetaObject::invokeMethod(dialog, "onRemoveSelectedBinsClicked", Qt::DirectConnection));
	check(QStringLiteral("Second"), QStringLiteral("Second bin"));
	window.onScanFinished({missing, known});
	check(QStringLiteral("Second"), QStringLiteral("Second bin"));
	MediaFile refreshed = missing;
	refreshed.clipName = QStringLiteral("Fresh database clip");
	refreshed.clipNameSource = MediaFile::ClipNameSource::Mdb;
	refreshed.originalBin = QStringLiteral("Fresh database bin");
	window.onScanFinished({refreshed, known});
	check(refreshed.clipName, refreshed.originalBin);
	list->item(0)->setSelected(true);
	QVERIFY(QMetaObject::invokeMethod(dialog, "onRemoveSelectedBinsClicked", Qt::DirectConnection));
	check(refreshed.clipName, refreshed.originalBin);
	window.onScanFinished({missing, known});
	check({}, {});
}

void TestOperationUi::omf_gate_controls_scans_and_removes_legacy_rows()
{
	const QString root = path("scan");
	const QString mxfPath = root + QStringLiteral("/Avid MediaFiles/MXF/1/clip.mxf");
	const QString omfPath = root + QStringLiteral("/OMFI MediaFiles/legacy.omf");
	QVERIFY(put(mxfPath, QByteArray(4096, '\0')));
	QVERIFY(put(omfPath, QByteArray(4096, '\0')));
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.setPrecomputesEnabled(false);
	window.setOmfScanEnabled(false);
	window.m_operations->setUndoEnabled(false);
	QSignalSpy finished(window.m_scanner, &MediaScanner::scanFinished);
	window.startScanWithPaths({root});
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_VERIFY(window.m_operations->isIdle());
	QCOMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).mediaFilePath, mxfPath);
	QVERIFY(!window.m_model->fileAt(0).omfEra);
	window.setOmfScanEnabled(true);
	window.startScanWithPaths({root});
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 15000);
	QTRY_VERIFY(window.m_operations->isIdle());
	QCOMPARE(window.m_model->rowCount(), 2);
	QSet<QString> scanned;
	for (const auto &file : window.m_model->allFiles())
		scanned.insert(file.mediaFilePath);
	QCOMPARE(scanned, QSet<QString>({mxfPath, omfPath}));
	window.setOmfScanEnabled(false);
	QCOMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).mediaFilePath, mxfPath);
	QVERIFY(QFileInfo::exists(omfPath));
	window.startScanWithPaths({root});
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 15000);
	QTRY_VERIFY(window.m_operations->isIdle());
	QCOMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).mediaFilePath, mxfPath);

	// This part authors compatibility rows to exercise UI filtering alone.
	// Legacy metadata and suffixes must both be removed when the gate closes.
	window.setOmfScanEnabled(true);
	MediaFile modern;
	modern.mediaFilePath = mxfPath;
	modern.fileName = QStringLiteral("clip.mxf");
	modern.type = MediaFile::Type::Precompute;
	modern.project = QStringLiteral("Retained project");
	MediaFile otherModern = modern;
	otherModern.mediaFilePath = path("another.mxf");
	otherModern.fileName = QStringLiteral("another.mxf");
	otherModern.project = QStringLiteral("Another project");
	MediaFile legacyMetadata = modern;
	legacyMetadata.mediaFilePath = path("legacy.mxf");
	legacyMetadata.omfEra = true;
	legacyMetadata.project = QStringLiteral("Legacy project");
	MediaFile legacySuffix = modern;
	legacySuffix.mediaFilePath = path("legacy.omf");
	legacySuffix.fileName = QStringLiteral("legacy.omf");
	legacySuffix.project = legacyMetadata.project;
	window.onScanFinished({modern, otherModern, legacyMetadata, legacySuffix});
	for (const auto &project : {modern.project, legacyMetadata.project})
	{
		const auto matches = window.m_projectList->findItems(project, Qt::MatchExactly);
		QCOMPARE(matches.size(), 1);
		matches.first()->setSelected(true);
	}
	QCOMPARE(window.m_proxy->rowCount(), 3);
	window.setOmfScanEnabled(false);
	QCOMPARE(window.m_model->rowCount(), 2);
	QCOMPARE(window.m_model->fileAt(0).mediaFilePath, mxfPath);
	QCOMPARE(window.m_model->fileAt(0).type, MediaFile::Type::Precompute);
	QCOMPARE(window.m_projectList->count(), 2);
	QVERIFY(window.m_projectList->findItems(legacyMetadata.project, Qt::MatchExactly).isEmpty());
	QCOMPARE(window.m_projectList->selectedItems().size(), 1);
	QCOMPARE(window.m_projectList->selectedItems().first()->text(), modern.project);
	QCOMPARE(window.m_proxy->rowCount(), 1);
	QCOMPARE(window.fileAtProxyRow(0).mediaFilePath, modern.mediaFilePath);
	window.onScanFinished({modern, otherModern});
	QVERIFY(window.m_projectList->selectedItems().isEmpty());
	QCOMPARE(window.m_proxy->rowCount(), 2);
}

void TestOperationUi::startup_prunes_expired_journals_with_undo_disabled()
{
	QStringList journals;
	const auto expired = QDateTime::currentDateTimeUtc().addDays(-31);
	for (int n = 0; n < 2; ++n)
	{
		std::atomic<bool> cancel{false};
		Sink sink;
		OpRunner runner(sink, cancel);
		const auto totals = runner.run(request(QStringLiteral("completed-%1").arg(n)), path("journals"));
		QCOMPARE(totals.succeeded, 1);
		const auto candidate = OpJournal::latestUndoable();
		QVERIFY(candidate);
		journals.append(candidate->path);
		QFile file(candidate->path);
		QVERIFY(file.open(QIODevice::ReadWrite));
		QVERIFY(file.setFileTime(expired, QFileDevice::FileModificationTime));
		QTest::qWait(2); // Keep the jobs' recorded start times distinct.
	}
	QVERIFY(journals[0] != journals[1]);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.setPrecomputesEnabled(false);
	window.setOmfScanEnabled(false);
	window.m_operations->setUndoEnabled(false);
	QVERIFY(!window.m_operations->m_undoEnabled);
	window.m_operations->runStartupRecovery();
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_operations->m_historyLoading, 10000);
	QVERIFY(!QFileInfo::exists(journals[0]));
	QVERIFY(QFileInfo::exists(journals[1]));
	QCOMPARE(window.m_operations->m_undoCandidate.journalPath, journals[1]);
	QVERIFY(!window.m_operations->m_undoAction->isVisible());
	for (int n = 0; n < 2; ++n)
	{
		QVERIFY(QFileInfo::exists(path(QStringLiteral("completed-%1/source/clip-0.bin").arg(n))));
		QVERIFY(QFileInfo::exists(path(QStringLiteral("completed-%1/destination/clip-0.bin").arg(n))));
	}
}

void TestOperationUi::unfinished_business_is_the_single_file_recovery_command()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	auto *recovery = window.m_operations->recoveryAction();
	QCOMPARE(recovery->objectName(), QStringLiteral("unfinishedBusinessAction"));
	QCOMPARE(recovery->text(), QStringLiteral("Unfinished Business…"));
	QVERIFY(!recovery->isEnabled());

	QMenu *fileMenu = nullptr;
	for (auto *action : window.menuBar()->actions())
		if (action->text().remove('&') == QStringLiteral("File"))
			fileMenu = action->menu();
	QVERIFY(fileMenu);
	QCOMPARE(fileMenu->actions().count(recovery), 1);
	for (auto *action : fileMenu->actions())
	{
		QVERIFY(!action->text().startsWith(QStringLiteral("Resume Interrupted")));
		QVERIFY(!action->text().startsWith(QStringLiteral("Restore Interrupted")));
	}

	makeInterrupted(request());
	window.m_operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_operations->m_historyLoading, 15000);
	QVERIFY(recovery->isEnabled());
}

void TestOperationUi::unfinished_business_merges_jobs_and_updates_choices()
{
	const auto bothPath = makeRetainedOriginal("both");
	auto deleteRequest = request("delete");
	deleteRequest.kind = OpKind::Delete;
	const auto resumePath = makeInterrupted(deleteRequest);
	const auto restorePath = makeRetainedOriginal("restore");
	QString error;
	QVERIFY(OpJournal::dismiss(restorePath, error));
	QWidget window;
	FileOperationController operations(&window);
	operations.refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations.m_historyLoading, 15000);
	QCOMPARE(operations.m_resumable.size(), 2);
	QCOMPARE(operations.m_restorable.size(), 2);
	QSignalSpy finished(operations.manager(), &OpManager::operationFinished);
	bool inspected = false;
	QTimer inspect;
	inspect.setInterval(1);
	connect(&inspect, &QTimer::timeout, &window, [&]
			{
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) return;
		inspect.stop();
		QTimer::singleShot(5000, dialog, &QDialog::reject); // Also closes if an assertion fails.
		QCOMPARE(dialog->objectName(), QStringLiteral("unfinishedBusinessDialog"));
		auto *heading = dialog->findChild<QLabel *>(QStringLiteral("unfinishedBusinessHeading"));
		auto *summary = dialog->findChild<QLabel *>(QStringLiteral("unfinishedBusinessSummary"));
		QVERIFY(heading && summary);
		QCOMPARE(heading->text(), QStringLiteral("Resume the interrupted job?"));
		auto *jobs = dialog->findChild<QComboBox *>(QStringLiteral("unfinishedBusinessJob"));
		auto *resume = dialog->findChild<QPushButton *>(QStringLiteral("resumeInterruptedJobButton"));
		auto *restore = dialog->findChild<QPushButton *>(QStringLiteral("restoreOriginalsButton"));
		auto *stop = dialog->findChild<QPushButton *>(QStringLiteral("stopInterruptedJobButton"));
		QVERIFY(jobs && resume && restore && stop);
		QCOMPARE(resume->text(), QStringLiteral("Resume"));
		QCOMPARE(stop->text(), QStringLiteral("Stop"));
		for (auto *button : dialog->findChildren<QPushButton *>())
			QVERIFY(button->text().remove('&') != QStringLiteral("Cancel"));
		QCOMPARE(jobs->count(), 3); // The job with both choices appears only once.
		QSet<QString> listed;
		for (int i = 0; i < jobs->count(); ++i) listed.insert(jobs->itemData(i).toString());
		QCOMPARE(listed, QSet<QString>({bothPath, resumePath, restorePath}));
		for (const auto *button : {resume, restore, stop})
		{
			QVERIFY(!button->isDefault());
			QVERIFY(!button->autoDefault());
		}

		jobs->setCurrentIndex(jobs->findData(bothPath));
		QVERIFY(resume->isVisible() && resume->isEnabled());
		QVERIFY(restore->isVisible() && restore->isEnabled());
		QVERIFY(stop->isVisible() && stop->isEnabled());
		for (auto *button : {resume, restore, stop})
		{
			button->setFocus();
			QTest::keyClick(button, Qt::Key_Return);
			QVERIFY(dialog->isVisible());
			QCOMPARE(finished.count(), 0);
		}
		jobs->setCurrentIndex(jobs->findData(resumePath));
		QVERIFY(jobs->currentText().startsWith(QStringLiteral("Delete")));
		QVERIFY(summary->text().startsWith(QStringLiteral("Job: Delete\n")));
		QVERIFY(resume->isVisible() && resume->isEnabled());
		QVERIFY(!restore->isVisible() || !restore->isEnabled());
		QVERIFY(stop->isVisible() && stop->isEnabled());
		jobs->setCurrentIndex(jobs->findData(restorePath));
		QVERIFY(!resume->isVisible() || !resume->isEnabled());
		QVERIFY(restore->isVisible() && restore->isEnabled());
		QVERIFY(!stop->isVisible() || !stop->isEnabled());
		inspected = true;
		dialog->close(); });
	inspect.start();
	operations.recoveryAction()->trigger();
	QVERIFY(inspected);
	QCOMPARE(finished.count(), 0);
	QCOMPARE(OperationRecovery::pending().size(), 2);
	QCOMPARE(OperationRecovery::restorable().size(), 2);
}

void TestOperationUi::unfinished_business_gate_restore_does_not_start_waiting_job()
{
	const auto dismissedPath = makeRetainedOriginal("dismissed");
	QString error;
	QVERIFY(OpJournal::dismiss(dismissedPath, error));
	const auto restorePath = makeRetainedOriginal("pending");
	const auto record = OpJournal::readOne(restorePath);
	QVERIFY(record);
	const auto original = record->entries.first();
	QWidget window;
	FileOperationController operations(&window);
	QSignalSpy finished(operations.manager(), &OpManager::operationFinished);
	bool selectedPending = false;
	QTimer choose;
	choose.setInterval(1);
	connect(&choose, &QTimer::timeout, &window, [&]
			{
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) return;
		choose.stop();
		QTimer::singleShot(0, dialog, &QDialog::reject);
		QCOMPARE(dialog->objectName(), QStringLiteral("unfinishedBusinessDialog"));
		auto *jobs = dialog->findChild<QComboBox *>(QStringLiteral("unfinishedBusinessJob"));
		auto *restore = dialog->findChild<QPushButton *>(QStringLiteral("restoreOriginalsButton"));
		QVERIFY(jobs && restore);
		// The previous-job gate selects its unfinished job even when another
		// dismissed job also has originals waiting to be restored.
		QCOMPARE(jobs->currentData().toString(), restorePath);
		selectedPending = true;
		restore->click(); });
	choose.start();
	QVERIFY(!operations.dispatchRequest(request("attempted")));
	QVERIFY(selectedPending);
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(operations.isIdle() && !operations.m_historyLoading, 15000);
	QVERIFY(QFileInfo::exists(original.item.src));
	QVERIFY(!QFileInfo::exists(original.retirement));
	QVERIFY(QFileInfo::exists(original.dst));
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
	QCOMPARE(OpJournal::scan().size(), 2);
	QVERIFY(OpJournal::readOne(dismissedPath)->dismissed);
	QCOMPARE(operations.m_restorable.size(), 1);
	QCOMPARE(operations.m_restorable.first().journalPath, dismissedPath);
}

void TestOperationUi::unfinished_business_gate_stop_only_dismisses_selected_job()
{
	const auto firstPath = makeInterrupted(request("first"));
	const auto secondPath = makeInterrupted(request("second"));
	QWidget window;
	FileOperationController operations(&window);
	int dialogsSeen = 0;
	QTimer choose;
	choose.setInterval(1);
	connect(&choose, &QTimer::timeout, &window, [&]
			{
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) return;
		QTimer::singleShot(0, dialog, &QDialog::reject);
		QCOMPARE(dialog->objectName(), QStringLiteral("unfinishedBusinessDialog"));
		auto *jobs = dialog->findChild<QComboBox *>(QStringLiteral("unfinishedBusinessJob"));
		QVERIFY(jobs);
		++dialogsSeen;
		if (dialogsSeen == 1)
		{
			const int secondIndex = jobs->findData(secondPath);
			QVERIFY(secondIndex >= 0);
			jobs->setCurrentIndex(secondIndex);
			auto *stop = dialog->findChild<QPushButton *>(QStringLiteral("stopInterruptedJobButton"));
			QVERIFY(stop);
			stop->click();
			return;
		}
		choose.stop();
		QCOMPARE(dialogsSeen, 2);
		QCOMPARE(jobs->count(), 1);
		QCOMPARE(jobs->currentData().toString(), firstPath);
		QVERIFY(!OpJournal::readOne(firstPath)->dismissed);
		QVERIFY(OpJournal::readOne(secondPath)->dismissed);
		dialog->close(); });
	choose.start();
	QVERIFY(!operations.dispatchRequest(request("attempted")));
	QCOMPARE(dialogsSeen, 2);
	QVERIFY(!operations.manager()->isRunning());
	QVERIFY(!OpJournal::readOne(firstPath)->dismissed);
	QVERIFY(OpJournal::readOne(secondPath)->dismissed);
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
	QCOMPARE(OpJournal::scan().size(), 2);
}

void TestOperationUi::interrupted_dialog_escape_leaves_job_pending()
{
	const auto old = request();
	const auto journalPath = makeInterrupted(old);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	clickInterrupted("escape");
	QVERIFY(!window.m_operations->dispatchRequest(request("attempted")));
	const auto record = OpJournal::readOne(journalPath);
	QVERIFY(record);
	QVERIFY(!record->dismissed);
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
	QCOMPARE(OpJournal::scan().size(), 1);
}
void TestOperationUi::interrupted_dialog_close_leaves_job_pending()
{
	const auto journalPath = makeInterrupted(request());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	clickInterrupted("close");
	QVERIFY(!window.m_operations->resolvePreviousJob());
	QVERIFY(!OpJournal::readOne(journalPath)->dismissed);
}
void TestOperationUi::interrupted_dialog_stop_keeps_completed_effects()
{
	const auto old = request("old", 2);
	const auto journalPath = makeInterrupted(old, true);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	clickInterrupted("stopInterruptedJobButton");
	QVERIFY(window.m_operations->resolvePreviousJob());
	const auto record = OpJournal::readOne(journalPath);
	QVERIFY(record && record->dismissed);
	QVERIFY(QFileInfo::exists(path("old/destination/clip-0.bin")));
	QVERIFY(QFileInfo::exists(old.items[1].src));
	QVERIFY(!QFileInfo::exists(path("old/destination/clip-1.bin")));
	QVERIFY(OperationRecovery::pending().isEmpty());
	QVERIFY(OpJournal::latestUndoable());
	QCOMPARE(OpJournal::latestUndoable()->path, journalPath);
}
void TestOperationUi::interrupted_dialog_resume_starts_only_old_job()
{
	auto old = request();
	const auto journalPath = makeInterrupted(old);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy finished(window.m_operations->m_operationManager, &OpManager::operationFinished);
	clickInterrupted("resumeInterruptedJobButton");
	QVERIFY(!window.m_operations->dispatchRequest(request("attempted")));
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(QFileInfo::exists(path("old/destination/clip-0.bin")));
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
	QCOMPARE(OpJournal::scan().size(), 1);
	const auto record = OpJournal::readOne(journalPath);
	QVERIFY(record);
	QVERIFY(record->entries.first().complete());
}
void TestOperationUi::interrupted_undo_resumes_with_debug_flag_off()
{
	auto original = request("original", 2);
	original.diagnosticTrashRoot = path("undo-trash");
	std::atomic<bool> cancel{false};
	Sink sink;
	OpRunner forward(sink, cancel);
	QCOMPARE(forward.run(original, path("journals")).succeeded, 2);
	const auto saved = OpJournal::latestUndoable();
	QVERIFY(saved);
	OpRequest inverse;
	inverse.kind = OpKind::Undo;
	inverse.undoEnabled = true;
	inverse.undoJournalPath = saved->path;
	sink.cancelAfterResult = &cancel;
	OpRunner undo(sink, cancel);
	QCOMPARE(undo.run(inverse, path("journals")).succeeded, 1);
	const auto pending = OperationRecovery::pending();
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending.first().kind, OpKind::Undo);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_operations->setUndoEnabled(false);
	QVERIFY(!window.m_operations->m_undoEnabled);
	MediaFile copied;
	copied.mediaFilePath = path("original/destination/clip-1.bin");
	copied.fileName = QStringLiteral("clip-1.bin");
	copied.project = QStringLiteral("Copied project");
	copied.sizeBytes = QFileInfo(copied.mediaFilePath).size();
	window.onScanFinished({copied});
	QCOMPARE(window.m_projectList->count(), 1);
	QSignalSpy restored(window.m_operations, &FileOperationController::originalsRestored);
	QSignalSpy finished(window.m_operations->m_operationManager, &OpManager::operationFinished);
	clickInterrupted("resumeInterruptedJobButton");
	QVERIFY(!window.m_operations->dispatchRequest(request("attempted")));
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(OperationRecovery::pending().isEmpty());
	QVERIFY(!QFileInfo::exists(path("original/destination/clip-0.bin")));
	QVERIFY(!QFileInfo::exists(path("original/destination/clip-1.bin")));
	QVERIFY(QFileInfo::exists(original.items[0].src));
	QVERIFY(QFileInfo::exists(original.items[1].src));
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
	QTRY_COMPARE(window.m_model->rowCount(), 0);
	QCOMPARE(window.m_projectList->count(), 0);
	QTRY_COMPARE(window.m_statusFiles->text(), QStringLiteral("0 files"));
	QTRY_COMPARE(window.m_statusSize->text(), QStringLiteral("0 B"));
	QCOMPARE(restored.count(), 0); // Undo Copy only removes a copy from the inventory.
}
void TestOperationUi::restore_action_survives_dismissal_later_jobs_and_close()
{
	const auto oldPath = makeRetainedOriginal();
	QString error;
	QVERIFY(OpJournal::dismiss(oldPath, error));
	std::atomic<bool> cancel{false};
	Sink sink;
	OpRunner later(sink, cancel);
	QCOMPARE(later.run(request("later"), path("journals")).succeeded, 1);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.setPrecomputesEnabled(false);
	window.setOmfScanEnabled(false);
	window.m_operations->setUndoEnabled(false);
	auto *operations = window.m_operations;
	operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations->m_historyLoading, 15000);
	QVERIFY(!operations->m_undoEnabled);
	QVERIFY(!operations->undoAction()->isVisible());
	QVERIFY(operations->recoveryAction()->isEnabled());
	QCOMPARE(operations->m_restorable.size(), 1);
	const auto job = operations->m_restorable.first();
	QCOMPARE(job.journalPath, oldPath);
	QSignalSpy finished(operations->manager(), &OpManager::operationFinished);
	for (const auto &close : {"escape", "close", "enter"})
	{
		clickRestoreOriginals(QString::fromLatin1(close), job);
		operations->recoveryAction()->trigger();
		QVERIFY(operations->recoveryAction()->isEnabled());
		QVERIFY(!QFileInfo::exists(job.originals.first()));
		QVERIFY(QFileInfo::exists(job.retainedPaths.first()));
		QVERIFY(OpJournal::readOne(oldPath)->dismissed);
	}
	QCOMPARE(finished.count(), 0);
	QCOMPARE(OpJournal::scan().size(), 2);
}

void TestOperationUi::restore_originals_keeps_completed_copy_and_refreshes_rows()
{
	const auto oldPath = makeRetainedOriginal();
	const auto old = OpJournal::readOne(oldPath);
	QVERIFY(old);
	QString error;
	QVERIFY(OpJournal::dismiss(oldPath, error));
	// An unrelated unfinished job must not intercept a source-only restore.
	const auto laterPath = makeInterrupted(request("later"));
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	MediaFile untouched;
	untouched.mediaFilePath = path("unaffected/Avid MediaFiles/MXF/1/other.mxf");
	untouched.volumePath = path("unaffected");
	untouched.volumeName = QStringLiteral("unaffected");
	QVERIFY(put(untouched.mediaFilePath, QByteArray(1024, 'u')));
	MediaFile sibling;
	sibling.mediaFilePath = path("retained/Avid MediaFiles/MXF/1/sibling.mxf");
	sibling.volumePath = path("retained");
	sibling.volumeName = QStringLiteral("retained");
	QVERIFY(put(sibling.mediaFilePath, QByteArray(1024, 's')));
	window.m_model->setMediaFiles({untouched, sibling});
	auto *operations = window.m_operations;
	operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations->m_historyLoading, 15000);
	QSignalSpy restored(operations, &FileOperationController::originalsRestored);
	QSignalSpy finished(operations->manager(), &OpManager::operationFinished);
	clickRestoreOriginals("restoreOriginalsButton", operations->m_restorable.first());
	operations->recoveryAction()->trigger();
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(operations->isIdle() && !operations->m_historyLoading, 15000);
	QVERIFY(QFileInfo::exists(old->entries.first().item.src));
	QVERIFY(!QFileInfo::exists(old->entries.first().retirement));
	QVERIFY(QFileInfo::exists(old->entries.first().dst));
	QVERIFY(operations->m_restorable.isEmpty());
	QVERIFY(operations->recoveryAction()->isEnabled()); // The unrelated job can still resume.
	QCOMPARE(OpJournal::readOne(oldPath)->entries.first().step, OpJournal::Step::SourceRestored);
	QVERIFY(!OpJournal::readOne(laterPath)->dismissed);
	QVERIFY(!QFileInfo::exists(path("later/destination/clip-0.bin")));
	QSet<QString> displayed;
	const auto files = window.m_model->allFiles();
	QCOMPARE(files.size(), 3); // The represented restored tree is scanned only once.
	for (const auto &file : files)
	{
		displayed.insert(file.mediaFilePath);
		const auto &previous = file.mediaFilePath == untouched.mediaFilePath ? untouched : sibling;
		QCOMPARE(file.volumeName, previous.volumeName);
		QCOMPARE(file.volumePath, previous.volumePath);
	}
	QCOMPARE(displayed, QSet<QString>({untouched.mediaFilePath, sibling.mediaFilePath, old->entries.first().item.src}));
}

void TestOperationUi::resume_restoration_refreshes_rows()
{
	const auto journalPath = makeRetainedOriginal();
	const auto record = OpJournal::readOne(journalPath);
	QVERIFY(record);
	OpJournal journal;
	QString error;
	QVERIFY(journal.resume(*record, error));
	auto entry = record->entries.first();
	entry.step = OpJournal::Step::RestoringSource;
	QVERIFY(journal.save(entry));

	// The original was absent during the current scan. Resuming the saved
	// restoration intent must refresh the table just like explicit Restore.
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	auto *operations = window.m_operations;
	operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations->m_historyLoading, 15000);
	QVERIFY(operations->recoveryAction()->isEnabled());
	QSignalSpy restored(operations, &FileOperationController::originalsRestored);
	QSignalSpy results(operations->manager(), &OpManager::operationResult);
	clickInterrupted("resumeInterruptedJobButton");
	operations->recoveryAction()->trigger();
	QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(operations->isIdle() && !operations->m_historyLoading, 15000);
	QCOMPARE(results.count(), 1);
	QCOMPARE(qvariant_cast<OpResult>(results.first().first()).state, OpResult::State::OriginalRestored);
	QVERIFY(QFileInfo::exists(entry.item.src));
	QVERIFY(QFileInfo::exists(entry.dst));
	QVERIFY(!QFileInfo::exists(entry.retirement));
	QCOMPARE(OpJournal::readOne(journalPath)->entries.first().step, OpJournal::Step::SourceRestored);
	const auto files = window.m_model->allFiles();
	QCOMPARE(files.size(), 1);
	QCOMPARE(files.first().mediaFilePath, entry.item.src);
}

void TestOperationUi::blocked_restore_remains_available()
{
	const auto oldPath = makeRetainedOriginal();
	const auto old = OpJournal::readOne(oldPath);
	QVERIFY(old);
	const auto original = old->entries.first().item.src;
	QVERIFY(put(original, QByteArray("new file in the original location")));
	QString error;
	QVERIFY(OpJournal::dismiss(oldPath, error));
	QWidget window;
	FileOperationController operations(&window);
	operations.refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations.m_historyLoading, 15000);
	QSignalSpy finished(operations.manager(), &OpManager::operationFinished);
	QSignalSpy restored(&operations, &FileOperationController::originalsRestored);
	clickRestoreOriginals("restoreOriginalsButton", operations.m_restorable.first());
	operations.recoveryAction()->trigger();
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(!operations.m_historyLoading && operations.isIdle(), 15000);
	QCOMPARE(restored.count(), 0);
	QVERIFY(operations.recoveryAction()->isEnabled());
	QVERIFY(QFileInfo::exists(old->entries.first().retirement));
	QFile occupant(original);
	QVERIFY(occupant.open(QIODevice::ReadOnly));
	QCOMPARE(occupant.readAll(), QByteArray("new file in the original location"));
	clickRestoreOriginals("close", operations.m_restorable.first());
	operations.offerRecovery();
	QVERIFY(operations.recoveryAction()->isEnabled());
}

void TestOperationUi::restore_can_select_another_retained_job()
{
	QString error;
	QVERIFY(OpJournal::dismiss(makeRetainedOriginal("first"), error));
	QVERIFY(OpJournal::dismiss(makeRetainedOriginal("second"), error));
	QWidget window;
	FileOperationController operations(&window);
	operations.refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations.m_historyLoading, 15000);
	QCOMPARE(operations.m_restorable.size(), 2);
	const auto jobs = operations.m_restorable;
	QVERIFY(put(jobs[0].originals.first(), QByteArray("occupied")));
	QSignalSpy finished(operations.manager(), &OpManager::operationFinished);
	clickRestoreOriginals("restoreOriginalsButton", jobs[1]);
	operations.recoveryAction()->trigger();
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(!operations.m_historyLoading && operations.isIdle(), 15000);
	QCOMPARE(operations.m_restorable.size(), 1);
	QCOMPARE(operations.m_restorable.first().journalPath, jobs[0].journalPath);
	QVERIFY(QFileInfo::exists(jobs[0].retainedPaths.first()));
	QVERIFY(QFileInfo::exists(jobs[1].originals.first()));
	QVERIFY(!QFileInfo::exists(jobs[1].retainedPaths.first()));
}

void TestOperationUi::startup_offers_retained_originals()
{
	const auto oldPath = makeRetainedOriginal();
	QString error;
	QVERIFY(OpJournal::dismiss(oldPath, error));
	QWidget window;
	FileOperationController operations(&window);
	OperationRecovery::Summary summary;
	summary.restorable = OperationRecovery::restorable();
	QCOMPARE(summary.restorable.size(), 1);
	clickRestoreOriginals("close", summary.restorable.first());
	operations.onRecoveryDone(summary);
	QVERIFY(operations.recoveryAction()->isEnabled());
	QVERIFY(!operations.manager()->isRunning());
}

void TestOperationUi::restore_originals_respects_busy_gate()
{
	const auto oldPath = makeRetainedOriginal();
	QWidget window;
	FileOperationController operations(&window);
	operations.refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations.m_historyLoading, 15000);
	for (auto activity : {FileOperationController::Activity::Scanning,
						  FileOperationController::Activity::Recovering,
						  FileOperationController::Activity::RebalanceDialog,
						  FileOperationController::Activity::FileOperation})
	{
		operations.setActivity(activity);
		QVERIFY(!operations.recoveryAction()->isEnabled());
		operations.offerRecovery();
		OpRequest restore;
		restore.restoreJournalPath = oldPath;
		QVERIFY(!operations.dispatchRequest(restore));
		QVERIFY(!operations.manager()->isRunning());
	}
	operations.setActivity(FileOperationController::Activity::Idle);
	QVERIFY(operations.recoveryAction()->isEnabled());
}

void TestOperationUi::observed_removals_prune_rows_even_when_job_needs_attention()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	MediaFile retired, removed, retained;
	retired.mediaFilePath = path("retired.mxf");
	removed.mediaFilePath = path("removed.mxf");
	retained.mediaFilePath = path("retained.mxf");
	window.m_model->setMediaFiles({retired, removed, retained});
	window.m_operations->m_pruneSourceRowsAfterOperation = true;
	OpResult retirement;
	retirement.state = OpResult::State::SourceRetained;
	retirement.source = retired.mediaFilePath;
	retirement.sourceRemoved = true;
	emit window.m_operations->m_operationManager->operationResult(retirement);
	OpResult removal;
	removal.state = OpResult::State::NeedsAttention;
	removal.source = removed.mediaFilePath;
	removal.sourceRemoved = true;
	emit window.m_operations->m_operationManager->operationResult(removal);
	OpResult kept;
	kept.state = OpResult::State::NeedsAttention;
	kept.source = retained.mediaFilePath;
	emit window.m_operations->m_operationManager->operationResult(kept);
	emit window.m_operations->m_operationManager->operationFinished(0, 3);
	QTRY_COMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).mediaFilePath, retained.mediaFilePath);
}
void TestOperationUi::completed_results_preserve_details_and_refresh_signals()
{
	QWidget window;
	FileOperationController operations(&window);
	QSignalSpy messages(&operations, &FileOperationController::logMessage);
	QSignalSpy removed(&operations, &FileOperationController::sourcesRemoved);
	QSignalSpy restored(&operations, &FileOperationController::originalsRestored);
	operations.m_pruneSourceRowsAfterOperation = true;

	OpResult plain;
	plain.state = OpResult::State::Completed;
	plain.name = QStringLiteral("moved.mxf");
	plain.source = path(plain.name);
	plain.sourceRemoved = true;
	emit operations.manager()->operationResult(plain);

	OpResult detailed;
	detailed.state = OpResult::State::Completed;
	detailed.name = QStringLiteral("copied.mxf");
	detailed.message = QStringLiteral("Copy finished; the storage did not confirm the full durability request.");
	emit operations.manager()->operationResult(detailed);

	OpResult returned;
	returned.state = OpResult::State::Completed;
	returned.name = QStringLiteral("returned.mxf");
	returned.restoredOriginalPath = path(returned.name);
	emit operations.manager()->operationResult(returned);
	emit operations.manager()->operationFinished(3, 0);

	QTRY_COMPARE(restored.count(), 1);
	QCOMPARE(removed.count(), 1);
	QCOMPARE(removed.first().first().value<QSet<QString>>(), QSet<QString>{plain.source});
	QCOMPARE(restored.first().first().value<QSet<QString>>(), QSet<QString>{returned.restoredOriginalPath});
	QCOMPARE(messages.count(), 1);
	QCOMPARE(messages.first().at(2).toString(), detailed.name + QStringLiteral(": ") + detailed.message);
}

void TestOperationUi::rebalance_live_counts_follow_confirmed_results()
{
	const auto plan = smallRebalancePlan(path("Avid MediaFiles/MXF"));
	RebalanceDialog dialog({}, {}, {});
	dialog.m_currentPlan = plan;
	dialog.renderPlan();
	dialog.primeLiveState();
	dialog.m_running = true;
	const auto originalCounts = dialog.m_runningCount;
	dialog.onProgress(3, 3, QStringLiteral("Starting last file"));
	QVERIFY(dialog.m_runningCount == originalCounts);

	OpResult outcome;
	outcome.source = plan.ops[0].srcPath;
	outcome.destination = plan.mxfRootPath + QStringLiteral("/2/clip-0.mxf");
	for (const auto state : {OpResult::State::Completed, OpResult::State::NoEffect,
							 OpResult::State::Failed, OpResult::State::Skipped,
							 OpResult::State::Cancelled, OpResult::State::NeedsAttention})
	{
		outcome.state = state;
		dialog.onOperationResult(outcome);
		QVERIFY(dialog.m_runningCount == originalCounts);
	}

	// The grouped runner can emit a different order than the preview's ops.
	outcome.source = plan.ops[2].srcPath;
	outcome.destination = plan.mxfRootPath + QStringLiteral("/2/clip-2.mxf");
	outcome.state = OpResult::State::Completed;
	outcome.sourceRemoved = true;
	dialog.onOperationResult(outcome);
	const NumberedMxfFolder source{{}, 1}, existingDestination{{}, 2}, newDestination{{}, 3};
	QCOMPARE(dialog.m_runningCount.value(source), 2);
	QCOMPARE(dialog.m_runningCount.value(existingDestination), 2);
	QCOMPARE(dialog.m_runningCount.value(newDestination), 0);
	dialog.onOperationResult(outcome); // A repeated receipt must not move the count twice.
	QCOMPARE(dialog.m_runningCount.value(source), 2);
	QCOMPARE(dialog.m_runningCount.value(existingDestination), 2);

	// The rename succeeded, but saving its final journal entry failed.
	outcome.source = plan.ops[1].srcPath;
	outcome.destination = plan.mxfRootPath + QStringLiteral("/3/clip-1.mxf");
	outcome.state = OpResult::State::NeedsAttention;
	dialog.onOperationResult(outcome);
	QCOMPARE(dialog.m_runningCount.value(source), 1);
	QCOMPARE(dialog.m_runningCount.value(existingDestination), 2);
	QCOMPARE(dialog.m_runningCount.value(newDestination), 1);
	dialog.finishDisplay(1, {{source, {1, true}}, {existingDestination, {2, true}}, {newDestination, {1, true}}});
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("1")), QStringLiteral("1"));
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("2")), QStringLiteral("2"));
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("3")), QStringLiteral("1"));
}

void TestOperationUi::rebalance_finished_recounts_uncertain_moves_and_absent_folders()
{
	const auto plan = smallRebalancePlan(path("Avid MediaFiles/MXF"));
	for (const auto &op : plan.ops)
		QVERIFY(put(op.srcPath, "media"));
	QVERIFY(put(plan.mxfRootPath + QStringLiteral("/2/existing.mxf"), "media"));
	QVERIFY(put(plan.mxfRootPath + QStringLiteral("/1/msmMMOB.mdb"), "database"));
	QVERIFY(put(plan.mxfRootPath + QStringLiteral("/2/.DS_Store"), "metadata"));
	RebalanceDialog dialog({}, {}, {});
	dialog.m_currentPlan = plan;
	dialog.renderPlan();
	dialog.primeLiveState();
	dialog.m_running = true;
	const auto originalCounts = dialog.m_runningCount;

	OpResult uncertain;
	uncertain.state = OpResult::State::NeedsAttention;
	uncertain.source = plan.ops[0].srcPath;
	uncertain.destination = plan.mxfRootPath + QStringLiteral("/2/clip-0.mxf");
	QVERIFY(QFile::rename(uncertain.source, uncertain.destination));
	// A post-rename sync failure has no confirmed sourceRemoved flag.
	dialog.onOperationResult(uncertain);
	QVERIFY(dialog.m_runningCount == originalCounts);
	dialog.onFinished(0, 1, false);
	QTRY_COMPARE(dialog.m_rebalanceButton->text(), QStringLiteral("Close"));
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("1")), QStringLiteral("2"));
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("2")), QStringLiteral("2"));
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("3")), QStringLiteral("Not created"));
	QVERIFY(dialog.m_statsLine->text().contains(QStringLiteral("<b>2</b> folders affected")));
	QVERIFY(dialog.m_statsLine->text().contains(QStringLiteral("<b>0</b> new folders")));
	QVERIFY(!QFileInfo::exists(plan.mxfRootPath + QStringLiteral("/3")));
	QVERIFY(dialog.m_progressLabel->text().contains(QStringLiteral("1 failed")));
}

void TestOperationUi::rebalance_finished_marks_unavailable_root()
{
	const auto plan = smallRebalancePlan(path("Avid MediaFiles/MXF"));
	QVERIFY(put(plan.ops[0].srcPath, "media"));
	RebalanceDialog dialog({}, {}, {});
	dialog.m_currentPlan = plan;
	dialog.renderPlan();
	dialog.primeLiveState();
	dialog.m_running = true;
	QDir mediaRoot(QFileInfo(plan.mxfRootPath).absolutePath());
	QVERIFY(mediaRoot.rename(QStringLiteral("MXF"), QStringLiteral("MXF-offline")));
	dialog.onFinished(0, 1, false);
	QTRY_COMPARE(dialog.m_rebalanceButton->text(), QStringLiteral("Close"));
	for (const auto &folder : plan.folders)
		QCOMPARE(folderCountCaption(dialog, folder.mediaFolderName), QStringLiteral("Unavailable"));
	QVERIFY(dialog.m_statsLine->text().contains(QStringLiteral("Unknown")));
	QVERIFY(!dialog.m_statsLine->text().contains(QStringLiteral("<b>-1</b>")));
}

void TestOperationUi::rebalance_engine_results_and_final_counts_data()
{
	QTest::addColumn<bool>("firstDestinationOccupied");
	QTest::addColumn<bool>("viaAlias");
	QTest::newRow("all-moved") << false << false;
	QTest::newRow("first-move-skipped") << true << false;
#ifdef Q_OS_UNIX
	QTest::newRow("ancestor-directory-alias") << false << true;
#endif
}

void TestOperationUi::rebalance_engine_results_and_final_counts()
{
	QFETCH(bool, firstDestinationOccupied);
	QFETCH(bool, viaAlias);
	const QString realRoot = path("media/Avid MediaFiles/MXF");
	auto plan = smallRebalancePlan(realRoot);
	for (int i = 0; i < plan.ops.size(); ++i)
	{
		const auto contents = QByteArray::number(i) + "-media";
		plan.ops[i].sizeBytes = contents.size();
		QVERIFY(put(plan.ops[i].srcPath, contents));
	}
	QVERIFY(put(plan.mxfRootPath + QStringLiteral("/2/existing.mxf"), "existing media"));
	if (viaAlias)
	{
		const QString alias = path("media-alias");
		QVERIFY(QFile::link(path("media"), alias));
		plan.mxfRootPath = alias + QStringLiteral("/Avid MediaFiles/MXF");
		for (auto &op : plan.ops)
			op.srcPath = plan.mxfRootPath + QStringLiteral("/1/") + QFileInfo(op.srcPath).fileName();
	}
	RebalanceDialog dialog({{plan.volumeLabel, plan.mxfRootPath}}, {}, plan.volumeLabel);
	QTRY_VERIFY(!dialog.m_planWatcher.isRunning());
	QTRY_COMPARE(dialog.m_currentPlan.mxfRootPath, plan.mxfRootPath); // onPlanReady has delivered.
	dialog.m_currentPlan = plan;
	dialog.renderPlan();
	dialog.primeLiveState();
	dialog.m_running = true;
	if (firstDestinationOccupied)
		QVERIFY(put(plan.mxfRootPath + QStringLiteral("/2/clip-0.mxf"), "conflicting media"));

	QSignalSpy results(dialog.m_rebalancer, &Rebalancer::operationResult);
	QSignalSpy finished(dialog.m_rebalancer, &Rebalancer::finished);
	QSignalSpy messages(&dialog, &RebalanceDialog::logMessage);
	dialog.m_rebalancer->executeAsync(plan);
	QTRY_COMPARE_WITH_TIMEOUT(dialog.m_rebalanceButton->text(), QStringLiteral("Close"), 15000);
	const int moved = firstDestinationOccupied ? 2 : 3;
	QCOMPARE(finished.count(), 1);
	QCOMPARE(finished.first().at(0).toInt(), moved);
	QCOMPARE(finished.first().at(1).toInt(), 0);
	QCOMPARE(results.count(), 3);
	for (int i = 0; i < plan.ops.size(); ++i)
	{
		const bool skipped = firstDestinationOccupied && i == 0;
		const auto result = results[i].first().value<OpResult>();
		QCOMPARE(result.state, skipped ? OpResult::State::Skipped : OpResult::State::Completed);
		QCOMPARE(result.sourceRemoved, !skipped);
		const QString fileName = QFileInfo(plan.ops[i].srcPath).fileName();
		const QString sourcePath = realRoot + QStringLiteral("/1/") + fileName;
		QCOMPARE(QFileInfo::exists(sourcePath), skipped);
		QFile destination(realRoot + '/' + plan.ops[i].dest.display() + '/' + fileName);
		QVERIFY(destination.open(QIODevice::ReadOnly));
		QCOMPARE(destination.readAll(), skipped ? QByteArray("conflicting media") : QByteArray::number(i) + "-media");
		if (skipped)
		{
			QFile source(sourcePath);
			QVERIFY(source.open(QIODevice::ReadOnly));
			QCOMPARE(source.readAll(), QByteArray("0-media"));
		}
	}
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("1")), firstDestinationOccupied ? QStringLiteral("1") : QStringLiteral("0"));
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("2")), QStringLiteral("3"));
	QCOMPARE(folderCountCaption(dialog, QStringLiteral("3")), QStringLiteral("1"));
	QCOMPARE(dialog.m_progressBar->value(), moved); // Real result forwarding supplied the confirmed-move count.
	QVERIFY(dialog.m_statsLine->text().contains(QStringLiteral("<b>3</b> folders affected")));
	QVERIFY(dialog.m_statsLine->text().contains(QStringLiteral("<b>1</b> new folder")));
	QVERIFY(dialog.m_progressLabel->text().contains(QStringLiteral("%1 moved, 0 failed").arg(moved)));
	if (firstDestinationOccupied)
	{
		bool reportedSkip = false;
		for (const auto &message : messages)
			reportedSkip |= message.at(1).toString().contains(QStringLiteral("Rebalance group skipped"));
		QVERIFY(reportedSkip);
	}
}

void TestOperationUi::rebalance_dialog_blocks_other_operation_entrypoints()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_operations->setUndoEnabled(true);
	window.m_operations->m_undoCandidate.journalPath = path("earlier-journal.jsonl");
	OperationRecovery::Resumable interrupted;
	interrupted.journalPath = path("interrupted-journal.jsonl");
	window.m_operations->m_resumable = {interrupted};
	window.m_operations->setActivity(FileOperationController::Activity::RebalanceDialog);
	window.m_operations->updateRecoveryAction();
	QVERIFY(!window.m_operations->m_undoAction->isEnabled());
	QVERIFY(!window.m_operations->m_recoveryAction->isEnabled());
	QVERIFY(!window.m_operations->dispatchRequest(request("competing")));
	QVERIFY(!window.m_operations->resolvePreviousJob());
	window.m_operations->undoLastOperation();
	QVERIFY(!window.m_operations->m_operationManager->isRunning());
	QVERIFY(OpJournal::scan().isEmpty());
	QVERIFY(!QFileInfo::exists(path("competing/destination/clip-0.bin")));
}
void TestOperationUi::scan_activity_blocks_operations_even_if_button_state_changes()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_operations->setActivity(FileOperationController::Activity::Scanning);
	window.m_scanButton->setEnabled(true); // Widget presentation is no longer the safety state.
	QVERIFY(!window.m_operations->dispatchRequest(request("blocked")));
	QVERIFY(!window.m_operations->resolvePreviousJob());
	QVERIFY(!window.m_operations->manager()->isRunning());
	window.m_operations->setActivity(FileOperationController::Activity::Idle);
	QVERIFY(window.m_operations->isIdle());
}

void TestOperationUi::rebalance_resume_keeps_running_job_activity()
{
	makeInterrupted(request("old"));
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	auto *operations = window.m_operations;
	operations->setActivity(FileOperationController::Activity::RebalanceDialog);
	QSignalSpy finished(operations->manager(), &OpManager::operationFinished);
	clickInterrupted("resumeInterruptedJobButton");
	QVERIFY(!operations->resolveBeforeRebalance());
	QCOMPARE(operations->activity(), FileOperationController::Activity::FileOperation);
	operations->endRebalanceDialog();
	QCOMPARE(operations->activity(), FileOperationController::Activity::FileOperation);
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_VERIFY(operations->isIdle());
}

void TestOperationUi::same_session_refresh_and_stale_result_guard()
{
	const auto journalPath = makeInterrupted(request());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_operations->m_historyLoading, 15000);
	QVERIFY(window.m_operations->m_recoveryAction->isEnabled());
	QCOMPARE(window.m_operations->m_resumable.first().journalPath, journalPath);
	window.m_operations->refreshHistory();
	++window.m_operations->m_historyGeneration; // A newer dispatch invalidates the pending read.
	window.m_operations->m_resumable.clear();
	window.m_operations->m_historyLoading = false;
	QThreadPool::globalInstance()->waitForDone();
	QCoreApplication::processEvents();
	QVERIFY(window.m_operations->m_resumable.isEmpty());
}
void TestOperationUi::preview_background_checks_discard_superseded_results()
{
	const auto fixture = request("preview");
	const auto &source = fixture.items.first();
	MediaFile file;
	file.mediaFilePath = source.src;
	file.fileName = source.name;
	file.sizeBytes = source.bytes;
	const QString oldDestination = fixture.destRoot;
	const QString latestDestination = path("latest");
	QVERIFY(QDir().mkpath(latestDestination));
	QVERIFY(put(oldDestination + '/' + source.name, "occupied"));
	ManageMediaDialog dialog({file});
	{
		BlockPool block;
		QVERIFY(block.entered.tryAcquire(1, 10000));
		dialog.m_destPath->setText(oldDestination);
		QVERIFY(dialog.m_checkingDest);
		QVERIFY(!dialog.m_executeButton->isEnabled());
		dialog.m_destPath->setText(latestDestination);
		QVERIFY(dialog.m_checkingDest);
		QVERIFY(!dialog.m_executeButton->isEnabled());
	}
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QThreadPool::globalInstance()->waitForDone();
	QCoreApplication::processEvents();
	QVERIFY(dialog.m_perFileConflictCombos.isEmpty());
	QCOMPARE(dialog.m_previewTree->topLevelItem(0)->text(1), latestDestination + '/' + source.name);
	QCOMPARE(dialog.m_assessment.requiredCopyBytes, source.bytes);
	QVERIFY(dialog.m_executeButton->isEnabled());
	{
		BlockPool block;
		QVERIFY(block.entered.tryAcquire(1, 10000));
		dialog.m_destPath->setText(oldDestination);
		QVERIFY(dialog.m_checkingDest);
		dialog.m_radioDelete->setChecked(true);
		QVERIFY(!dialog.m_checkingDest);
		QVERIFY(dialog.m_executeButton->isEnabled());
	}
	QThreadPool::globalInstance()->waitForDone();
	QCoreApplication::processEvents();
	QCOMPARE(dialog.operation(), ManageMediaDialog::Operation::Delete);
	QCOMPARE(dialog.m_previewTree->columnCount(), 1);
	QVERIFY(dialog.m_perFileConflictCombos.isEmpty());
}

void TestOperationUi::preview_policy_changes_refresh_space_and_same_file_is_no_effect()
{
	const auto fixture = request("preview");
	const auto &source = fixture.items.first();
	MediaFile file;
	file.mediaFilePath = source.src;
	file.fileName = source.name;
	file.sizeBytes = source.bytes;
	QVERIFY(put(fixture.destRoot + '/' + source.name, "occupied"));
	ManageMediaDialog dialog({file});
	dialog.m_destPath->setText(fixture.destRoot);
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.requiredCopyBytes, source.bytes);
	QCOMPARE(dialog.m_perFileConflictCombos.size(), 1);
	auto *policy = dialog.m_perFileConflictCombos.value(source.src);
	policy->setCurrentIndex(policy->findData(int(ConflictPolicy::Skip)));
	QVERIFY(dialog.m_checkingDest);
	QVERIFY(!dialog.m_executeButton->isEnabled());
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.requiredCopyBytes, qint64(0));
	policy->setCurrentIndex(policy->findData(int(ConflictPolicy::KeepBoth)));
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.requiredCopyBytes, source.bytes);
	dialog.m_destPath->setText(QFileInfo(source.src).absolutePath());
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.requiredCopyBytes, qint64(0));
	QVERIFY(dialog.m_perFileConflictCombos.isEmpty());
	QCOMPARE(dialog.m_previewTree->topLevelItem(0)->toolTip(1), QStringLiteral("Already at destination; no change needed."));
}

void TestOperationUi::progress_cancel_is_acknowledged_once()
{
	ProgressDialog dialog;
	QSignalSpy cancelled(&dialog, &ProgressDialog::cancelRequested);
	dialog.begin();
	dialog.setItemProgress(1, 1, 100);
	auto *bar = dialog.findChild<QProgressBar *>();
	QVERIFY(bar && bar->value() < bar->maximum());
	dialog.setItemProgress(3, 4, 0); // two files, copying plus removal
	bool showsProgress = false;
	for (const auto *label : dialog.findChildren<QLabel *>())
	{
		showsProgress |= label->text() == "50%";
		QVERIFY(!label->text().contains("of 4"));
	}
	QVERIFY(showsProgress);
	QTest::keyClick(&dialog, Qt::Key_Escape);
	QCOMPARE(cancelled.size(), 1);
	QVERIFY(dialog.isVisible());
	auto *button = dialog.findChild<QPushButton *>();
	QVERIFY(button && !button->isEnabled());
	QCOMPARE(button->text(), QStringLiteral("Cancelling..."));
	dialog.close();
	QCOMPARE(cancelled.size(), 1);
	dialog.finish();
	QVERIFY(!dialog.isVisible());
}

void TestOperationUi::trash_fallback_choice_data()
{
	QTest::addColumn<QString>("choice");
	QTest::addColumn<bool>("expectedAcceptance");
	QTest::newRow("move") << QStringLiteral("acceptTrashFallbackButton") << true;
	QTest::newRow("cancel") << QStringLiteral("cancelTrashFallbackButton") << false;
	QTest::newRow("escape") << QStringLiteral("escape") << false;
	QTest::newRow("window-close") << QStringLiteral("close") << false;
}

void TestOperationUi::trash_fallback_choice()
{
	QFETCH(QString, choice);
	QFETCH(bool, expectedAcceptance);
	std::atomic<bool> completed{false};
	std::atomic<bool> accepted{false};
	QWidget window;
	FileOperationController controller(&window);
	window.show();
	const QVector<OpTrashFallbackItem> items{
		{path("source/<clip>.mxf"), path("MediaMuster_Trash/<clip>.mxf"), QStringLiteral("Too large <b>for the bin</b>")},
		{path("source/second.mxf"), path("MediaMuster_Trash/second.mxf"), QStringLiteral("Bin unavailable")}};
	auto *manager = controller.manager();
	QSignalSpy requests(manager, &OpManager::trashFallbackRequested);
	manager->m_job.start([manager, items, &completed, &accepted]
						 {
		accepted.store(manager->confirmTrashFallback(items));
		completed.store(true); });
	QTRY_VERIFY_WITH_TIMEOUT(controller.m_trashFallbackDialog, 5000);
	auto *dialog = controller.m_trashFallbackDialog.data();
	QVERIFY(dialog->isVisible());
#ifndef Q_OS_MACOS
	// QMessageBox deliberately ignores window titles on macOS; the headline,
	// explanatory text and choices below are still required on every platform.
	QCOMPARE(dialog->windowTitle(), QStringLiteral("MediaMuster Trash"));
#endif
	QCOMPARE(dialog->textFormat(), Qt::PlainText);
	QCOMPARE(dialog->text(), QStringLiteral("Move these files to MediaMuster Trash?"));
	QCOMPARE(dialog->informativeText(), QStringLiteral("The system trash couldn’t accept these files. Keep them in MediaMuster Trash until you decide."));
	QVERIFY(dialog->detailedText().contains(items[0].source));
	QVERIFY(dialog->detailedText().contains(items[0].reason));
	QVERIFY(dialog->detailedText().contains(items[1].destination));
	auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("cancelTrashFallbackButton"));
	QVERIFY(cancel);
	QCOMPARE(dialog->defaultButton(), cancel);
	QCOMPARE(dialog->escapeButton(), cancel);
	QCOMPARE(requests.size(), 1);
	const auto requestId = controller.m_trashFallbackRequest;
	// A stale reply must not resolve the current prompt.
	manager->respondTrashFallback(requestId + 1, true);
	QVERIFY(manager->isTrashFallbackPending(requestId));
	QVERIFY(!completed.load());
	if (choice == "escape")
		QTest::keyClick(dialog, Qt::Key_Escape);
	else if (choice == "close")
		dialog->close();
	else
	{
		auto *button = dialog->findChild<QPushButton *>(choice);
		QVERIFY(button);
		button->click();
	}
	QTRY_VERIFY_WITH_TIMEOUT(completed.load(), 5000);
	QCOMPARE(accepted.load(), expectedAcceptance);
	QVERIFY(!manager->isTrashFallbackPending(requestId));
	QVERIFY(!controller.m_trashFallbackDialog);
}

void TestOperationUi::trash_fallback_cancel_closes_dialog()
{
	std::atomic<bool> completed{false};
	std::atomic<bool> accepted{true};
	QWidget window;
	FileOperationController controller(&window);
	auto *manager = controller.manager();
	manager->m_job.start([manager, &completed, &accepted]
						 {
		accepted.store(manager->confirmTrashFallback({{"source", "trash", "Unavailable"}}));
		completed.store(true); });
	QTRY_VERIFY_WITH_TIMEOUT(controller.m_trashFallbackDialog, 5000);
	const auto requestId = controller.m_trashFallbackRequest;
	manager->cancel();
	manager->respondTrashFallback(requestId, true); // Acceptance after cancel must be ignored.
	QTRY_VERIFY_WITH_TIMEOUT(completed.load(), 5000);
	QTRY_VERIFY_WITH_TIMEOUT(!controller.m_trashFallbackDialog, 5000);
	QVERIFY(!accepted.load());
	// A request already queued when cancellation happened must not reopen it.
	controller.showTrashFallback(requestId, {{"source", "trash", "Unavailable"}});
	QVERIFY(!controller.m_trashFallbackDialog);
}

void TestOperationUi::trash_fallback_without_handler_returns_without_waiting()
{
	std::atomic<bool> completed{false};
	std::atomic<bool> accepted{true};
	OpManager manager;
	QSignalSpy requests(&manager, &OpManager::trashFallbackRequested);
	manager.m_job.start([&]
						{
		accepted.store(manager.confirmTrashFallback({{"source", "trash", "Unavailable"}}));
		completed.store(true); });
	QTRY_VERIFY_WITH_TIMEOUT(completed.load(), 5000);
	QVERIFY(!accepted.load());
	QCOMPARE(requests.size(), 0);
}

void TestOperationUi::trash_fallback_destruction_unblocks_without_gui_events()
{
	std::atomic<bool> completed{false};
	std::atomic<bool> accepted{true};
	QSemaphore requested;
	auto manager = std::make_unique<OpManager>();
	manager->setTrashFallbackHandlerAvailable(true);
	connect(manager.get(), &OpManager::trashFallbackRequested, this, [&requested]
			{ requested.release(); }, Qt::DirectConnection);
	auto *workerManager = manager.get();
	manager->m_job.start([workerManager, &completed, &accepted]
						 {
		accepted.store(workerManager->confirmTrashFallback({{"source", "trash", "Unavailable"}}));
		completed.store(true); });
	QVERIFY(requested.tryAcquire(1, 5000));
	QElapsedTimer timer;
	timer.start();
	manager.reset(); // No event processing: the destructor must release and join directly.
	QVERIFY(timer.elapsed() < 2000);
	QVERIFY(completed.load());
	QVERIFY(!accepted.load());
}

void TestOperationUi::trash_fallback_controller_destruction_closes_dialog()
{
	std::atomic<bool> completed{false};
	std::atomic<bool> accepted{true};
	QWidget window;
	auto controller = std::make_unique<FileOperationController>(&window);
	auto *manager = controller->manager();
	manager->m_job.start([manager, &completed, &accepted]
						 {
		accepted.store(manager->confirmTrashFallback({{"source", "trash", "Unavailable"}}));
		completed.store(true); });
	QTRY_VERIFY_WITH_TIMEOUT(controller->m_trashFallbackDialog, 5000);
	QPointer<QMessageBox> dialog = controller->m_trashFallbackDialog;
	controller.reset();
	QVERIFY(dialog.isNull());
	QVERIFY(completed.load());
	QVERIFY(!accepted.load());
}

QTEST_MAIN(TestOperationUi)
#include "tst_operationui.moc"
