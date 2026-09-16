#include "mainwindow.h"
#include "managemediadialog.h"
#include "opjournal.h"
#include "progressdialog.h"

#include <QAction>
#include <QApplication>
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
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
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
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
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
		future = QtConcurrent::run([this] { entered.release(); releaseWorker.acquire(); });
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
	void initTestCase();
	void init();
	void cleanup();
	void cleanupTestCase();
	void debug_flags_default_off_and_text_undo_works();
	void startup_prunes_expired_journals_with_undo_disabled();
	void interrupted_dialog_escape_does_not_abandon();
	void interrupted_dialog_close_does_not_abandon();
	void interrupted_dialog_cancel_keeps_completed_effects();
	void interrupted_dialog_resume_starts_only_old_job();
	void interrupted_undo_resumes_with_debug_flag_off();
	void restore_action_survives_dismissal_later_jobs_and_close();
	void restore_originals_keeps_completed_copy_and_refreshes_rows();
	void blocked_restore_remains_available();
	void restore_can_select_another_retained_job();
	void startup_offers_retained_originals();
	void restore_originals_respects_busy_gate();
	void observed_removals_prune_rows_even_when_job_needs_attention();
	void rebalance_dialog_blocks_other_operation_entrypoints();
	void scan_activity_blocks_operations_even_if_button_state_changes();
	void rebalance_resume_keeps_running_job_activity();
	void verification_is_saved_per_job();
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
	void clickRestoreOriginals(const QString &button, const OperationRecovery::Restorable &job,
							  int selectedJob = 0);
	QString path(const QString &relative) const { return m_root + '/' + relative; }
	QByteArray m_previousJournalDir;
	bool m_hadJournalDir = false;
	std::unique_ptr<QTemporaryDir> m_temp;
	QString m_root;
};

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
	if (m_hadJournalDir) qputenv("MEDIAMUSTER_JOURNAL_DIR", m_previousJournalDir);
	else qunsetenv("MEDIAMUSTER_JOURNAL_DIR");
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
		if (totals.succeeded != 1) qFatal("Cannot prepare interrupted UI fixture");
		const auto pending = OperationRecovery::pending(path("journals"));
		if (pending.size() != 1) qFatal("Interrupted UI fixture was not resumable");
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
		QCOMPARE(dialog->objectName(), QStringLiteral("interruptedJobDialog"));
		bool foundHeadline = false;
		for (auto *label : dialog->findChildren<QLabel *>())
			foundHeadline |= label->text() == "The previous job was interrupted.";
		QVERIFY(foundHeadline);
		if (button == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
		else if (button == "close") dialog->close();
		else
		{
			auto *target = dialog->findChild<QPushButton *>(button);
			QVERIFY(target);
			target->click();
		}
	});
	timer->start();
}

QString TestOperationUi::makeRetainedOriginal(const QString &name)
{
	// A legacy journal whose move was interrupted after relocating its
	// original. No destination evidence is needed to restore that original.
	auto old = request(name);
	old.kind = OpKind::Move;
	old.copyThenRemove = true;
	const auto mediaPath = path(name + "/Avid MediaFiles/MXF/1/clip-0.mxf");
	if (!QDir().mkpath(QFileInfo(mediaPath).absolutePath()) ||
		!QFile::rename(old.items[0].src, mediaPath)) qFatal("Cannot rename UI fixture");
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
										 const OperationRecovery::Restorable &job, int selectedJob)
{
	auto *timer = new QTimer(this);
	timer->setInterval(1);
	connect(timer, &QTimer::timeout, this, [timer, button, job, selectedJob]
	{
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog) return;
		timer->stop();
		timer->deleteLater();
		QCOMPARE(dialog->objectName(), QStringLiteral("restoreOriginalsDialog"));
		auto *jobs = dialog->findChild<QComboBox *>(QStringLiteral("restoreOriginalsJob"));
		QVERIFY(jobs);
		jobs->setCurrentIndex(selectedJob);
		auto *paths = dialog->findChild<QPlainTextEdit *>(QStringLiteral("restoreOriginalPaths"));
		QVERIFY(paths && paths->isReadOnly());
		for (const auto &path : job.originals) QVERIFY(paths->toPlainText().contains(path));
		for (const auto &path : job.retainedPaths) QVERIFY(paths->toPlainText().contains(path));
		QCOMPARE(dialog->findChildren<QPushButton *>().size(), 2);
		if (button == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
		else if (button == "close") dialog->close();
		else
		{
			auto *target = dialog->findChild<QPushButton *>(button);
			QVERIFY(target);
			target->click();
		}
	});
	timer->start();
}

void TestOperationUi::debug_flags_default_off_and_text_undo_works()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.show();
	QVERIFY(window.m_operations->m_verifyCopiesAct->isCheckable());
	QVERIFY(!window.m_operations->m_verifyCopiesAct->isChecked());
	QVERIFY(window.m_operations->m_enableUndoAct->isCheckable());
	QVERIFY(!window.m_operations->m_enableUndoAct->isChecked());
	QVERIFY(!window.m_operations->m_undoAct->isVisible());
	QVERIFY(window.m_operations->m_undoAct->shortcut().isEmpty());
	window.m_searchField->setFocus();
	QTest::keyClicks(window.m_searchField, "typed search");
	QVERIFY(window.m_searchField->isUndoAvailable());
	QTest::keySequence(window.m_searchField, QKeySequence::Undo);
	QVERIFY(window.m_searchField->text().isEmpty());
	window.m_operations->m_enableUndoAct->setChecked(true);
	QVERIFY(window.m_operations->m_undoAct->isVisible());
	QCOMPARE(window.m_operations->m_undoAct->shortcut(), QKeySequence(QKeySequence::Undo));
	window.m_operations->m_enableUndoAct->setChecked(false);
	QVERIFY(!window.m_operations->m_undoAct->isVisible());
	QVERIFY(window.m_operations->m_undoAct->shortcut().isEmpty());
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
	QVERIFY(!window.m_operations->m_enableUndoAct->isChecked());
	window.m_operations->runStartupRecovery();
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_operations->m_historyLoading, 10000);
	QVERIFY(!QFileInfo::exists(journals[0]));
	QVERIFY(QFileInfo::exists(journals[1]));
	QCOMPARE(window.m_operations->m_undoCandidate.path, journals[1]);
	QVERIFY(!window.m_operations->m_undoAct->isVisible());
	for (int n = 0; n < 2; ++n)
	{
		QVERIFY(QFileInfo::exists(path(QStringLiteral("completed-%1/source/clip-0.bin").arg(n))));
		QVERIFY(QFileInfo::exists(path(QStringLiteral("completed-%1/destination/clip-0.bin").arg(n))));
	}
}

void TestOperationUi::interrupted_dialog_escape_does_not_abandon()
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
void TestOperationUi::interrupted_dialog_close_does_not_abandon()
{
	const auto journalPath = makeInterrupted(request());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	clickInterrupted("close");
	QVERIFY(!window.m_operations->resolvePreviousJob());
	QVERIFY(!OpJournal::readOne(journalPath)->dismissed);
}
void TestOperationUi::interrupted_dialog_cancel_keeps_completed_effects()
{
	const auto old = request("old", 2);
	const auto journalPath = makeInterrupted(old, true);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	clickInterrupted("cancelInterruptedJobButton");
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
	old.verifyCopies = true;
	const auto journalPath = makeInterrupted(old);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy finished(window.m_operations->m_fileOps, &OpManager::operationFinished);
	clickInterrupted("resumeInterruptedJobButton");
	QVERIFY(!window.m_operations->dispatchRequest(request("attempted")));
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(QFileInfo::exists(path("old/destination/clip-0.bin")));
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
	QCOMPARE(OpJournal::scan().size(), 1);
	const auto record = OpJournal::readOne(journalPath);
	QVERIFY(record && record->request.verifyCopies);
	QVERIFY(record->entries.first().complete());
	QVERIFY(!record->entries.first().hash.isEmpty());
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
	QVERIFY(!window.m_operations->m_enableUndoAct->isChecked());
	QSignalSpy finished(window.m_operations->m_fileOps, &OpManager::operationFinished);
	clickInterrupted("resumeInterruptedJobButton");
	QVERIFY(!window.m_operations->dispatchRequest(request("attempted")));
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(OperationRecovery::pending().isEmpty());
	QVERIFY(!QFileInfo::exists(path("original/destination/clip-0.bin")));
	QVERIFY(!QFileInfo::exists(path("original/destination/clip-1.bin")));
	QVERIFY(QFileInfo::exists(original.items[0].src));
	QVERIFY(QFileInfo::exists(original.items[1].src));
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
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
	auto *operations = window.m_operations;
	operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations->m_historyLoading, 15000);
	QVERIFY(!operations->enableUndoAction()->isChecked());
	QVERIFY(!operations->undoAction()->isVisible());
	QVERIFY(operations->restoreOriginalsAction()->isEnabled());
	QCOMPARE(operations->m_restorable.size(), 1);
	const auto job = operations->m_restorable.first();
	QCOMPARE(job.journalPath, oldPath);
	QSignalSpy finished(operations->manager(), &OpManager::operationFinished);
	for (const auto &close : {"closeRestoreOriginalsButton", "escape", "close"})
	{
		clickRestoreOriginals(QString::fromLatin1(close), job);
		operations->restoreOriginalsAction()->trigger();
		QVERIFY(operations->restoreOriginalsAction()->isEnabled());
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
	untouched.filePath = path("unaffected/Avid MediaFiles/MXF/1/other.mxf");
	untouched.volumePath = path("unaffected");
	QVERIFY(put(untouched.filePath, QByteArray(1024, 'u')));
	window.m_model->setMediaFiles({untouched});
	auto *operations = window.m_operations;
	operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!operations->m_historyLoading, 15000);
	QSignalSpy restored(operations, &FileOperationController::originalsRestored);
	QSignalSpy finished(operations->manager(), &OpManager::operationFinished);
	clickRestoreOriginals("restoreOriginalsButton", operations->m_restorable.first());
	operations->restoreOriginalsAction()->trigger();
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(operations->isIdle() && !operations->m_historyLoading, 15000);
	QVERIFY(QFileInfo::exists(old->entries.first().item.src));
	QVERIFY(!QFileInfo::exists(old->entries.first().retirement));
	QVERIFY(QFileInfo::exists(old->entries.first().dst));
	QVERIFY(!operations->restoreOriginalsAction()->isEnabled());
	QCOMPARE(OpJournal::readOne(oldPath)->entries.first().step, OpJournal::Step::SourceRestored);
	QVERIFY(!OpJournal::readOne(laterPath)->dismissed);
	QVERIFY(!QFileInfo::exists(path("later/destination/clip-0.bin")));
	QSet<QString> displayed;
	for (const auto &file : window.m_model->allFiles()) displayed.insert(file.filePath);
	QCOMPARE(displayed, QSet<QString>({untouched.filePath, old->entries.first().item.src}));
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
	operations.restoreOriginalsAction()->trigger();
	QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(!operations.m_historyLoading && operations.isIdle(), 15000);
	QCOMPARE(restored.count(), 0);
	QVERIFY(operations.restoreOriginalsAction()->isEnabled());
	QVERIFY(QFileInfo::exists(old->entries.first().retirement));
	QFile occupant(original);
	QVERIFY(occupant.open(QIODevice::ReadOnly));
	QCOMPARE(occupant.readAll(), QByteArray("new file in the original location"));
	clickRestoreOriginals("closeRestoreOriginalsButton", operations.m_restorable.first());
	operations.offerRestoreOriginals();
	QVERIFY(operations.restoreOriginalsAction()->isEnabled());
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
	clickRestoreOriginals("restoreOriginalsButton", jobs[1], 1);
	operations.restoreOriginalsAction()->trigger();
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
	clickRestoreOriginals("closeRestoreOriginalsButton", summary.restorable.first());
	operations.onRecoveryDone(summary);
	QVERIFY(operations.restoreOriginalsAction()->isEnabled());
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
		QVERIFY(!operations.restoreOriginalsAction()->isEnabled());
		operations.offerRestoreOriginals();
		OpRequest restore;
		restore.restoreJournalPath = oldPath;
		QVERIFY(!operations.dispatchRequest(restore));
		QVERIFY(!operations.manager()->isRunning());
	}
	operations.setActivity(FileOperationController::Activity::Idle);
	QVERIFY(operations.restoreOriginalsAction()->isEnabled());
}

void TestOperationUi::observed_removals_prune_rows_even_when_job_needs_attention()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	MediaFile retired, removed, retained;
	retired.filePath = path("retired.mxf");
	removed.filePath = path("removed.mxf");
	retained.filePath = path("retained.mxf");
	window.m_model->setMediaFiles({retired, removed, retained});
	window.m_operations->m_pruneSourceRowsAfterOperation = true;
	OpResult retirement;
	retirement.state = OpResult::State::SourceRetained;
	retirement.source = retired.filePath;
	retirement.sourceRemoved = true;
	emit window.m_operations->m_fileOps->operationResult(retirement);
	OpResult removal;
	removal.state = OpResult::State::NeedsAttention;
	removal.source = removed.filePath;
	removal.sourceRemoved = true;
	emit window.m_operations->m_fileOps->operationResult(removal);
	OpResult kept;
	kept.state = OpResult::State::NeedsAttention;
	kept.source = retained.filePath;
	emit window.m_operations->m_fileOps->operationResult(kept);
	emit window.m_operations->m_fileOps->operationFinished(0, 3);
	QTRY_COMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).filePath, retained.filePath);
}
void TestOperationUi::rebalance_dialog_blocks_other_operation_entrypoints()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_operations->m_enableUndoAct->setChecked(true);
	window.m_operations->m_undoCandidate.path = path("earlier-journal.jsonl");
	OperationRecovery::Resumable interrupted;
	interrupted.journalPath = path("interrupted-journal.jsonl");
	window.m_operations->m_resumable = {interrupted};
	window.m_operations->setActivity(FileOperationController::Activity::RebalanceDialog);
	window.m_operations->updateResumeAction();
	QVERIFY(!window.m_operations->m_undoAct->isEnabled());
	QVERIFY(!window.m_operations->m_resumeAct->isEnabled());
	QVERIFY(!window.m_operations->dispatchRequest(request("competing")));
	QVERIFY(!window.m_operations->resolvePreviousJob());
	window.m_operations->undoLastOperation();
	QVERIFY(!window.m_operations->m_fileOps->isRunning());
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

void TestOperationUi::verification_is_saved_per_job()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy finished(window.m_operations->m_fileOps, &OpManager::operationFinished);
	QVERIFY(window.m_operations->dispatchRequest(request("unchecked")));
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_operations->m_historyLoading, 15000);
	auto record = OpJournal::scan().first();
	QVERIFY(!record.request.verifyCopies);
	QVERIFY(record.entries.first().hash.isEmpty());
	window.m_operations->m_verifyCopiesAct->setChecked(true);
	QVERIFY(window.m_operations->dispatchRequest(request("checked")));
	// A later toggle cannot change the already accepted request.
	window.m_operations->m_verifyCopiesAct->setChecked(false);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 15000);
	record = OpJournal::scan().last();
	QVERIFY(record.request.verifyCopies);
	QVERIFY(!record.entries.first().hash.isEmpty());
}
void TestOperationUi::same_session_refresh_and_stale_result_guard()
{
	const auto journalPath = makeInterrupted(request());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_operations->refreshHistory();
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_operations->m_historyLoading, 15000);
	QVERIFY(window.m_operations->m_resumeAct->isEnabled());
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
	file.filePath = source.src;
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
		QVERIFY(!dialog.m_btnExecute->isEnabled());
		dialog.m_destPath->setText(latestDestination);
		QVERIFY(dialog.m_checkingDest);
		QVERIFY(!dialog.m_btnExecute->isEnabled());
	}
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QThreadPool::globalInstance()->waitForDone();
	QCoreApplication::processEvents();
	QVERIFY(dialog.m_perFileConflictCombos.isEmpty());
	QCOMPARE(dialog.m_previewTree->topLevelItem(0)->text(1), latestDestination + '/' + source.name);
	QCOMPARE(dialog.m_assessment.temporaryBytes, source.bytes);
	QVERIFY(dialog.m_btnExecute->isEnabled());
	{
		BlockPool block;
		QVERIFY(block.entered.tryAcquire(1, 10000));
		dialog.m_destPath->setText(oldDestination);
		QVERIFY(dialog.m_checkingDest);
		dialog.m_radioDelete->setChecked(true);
		QVERIFY(!dialog.m_checkingDest);
		QVERIFY(dialog.m_btnExecute->isEnabled());
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
	file.filePath = source.src;
	file.fileName = source.name;
	file.sizeBytes = source.bytes;
	QVERIFY(put(fixture.destRoot + '/' + source.name, "occupied"));
	ManageMediaDialog dialog({file});
	dialog.m_destPath->setText(fixture.destRoot);
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.temporaryBytes, source.bytes);
	QCOMPARE(dialog.m_perFileConflictCombos.size(), 1);
	auto *policy = dialog.m_perFileConflictCombos.value(source.src);
	policy->setCurrentIndex(policy->findData(int(ConflictPolicy::Skip)));
	QVERIFY(dialog.m_checkingDest);
	QVERIFY(!dialog.m_btnExecute->isEnabled());
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.temporaryBytes, qint64(0));
	policy->setCurrentIndex(policy->findData(int(ConflictPolicy::KeepBoth)));
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.temporaryBytes, source.bytes);
	dialog.m_destPath->setText(QFileInfo(source.src).absolutePath());
	QTRY_VERIFY_WITH_TIMEOUT(!dialog.m_checkingDest, 15000);
	QCOMPARE(dialog.m_assessment.temporaryBytes, qint64(0));
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
		completed.store(true);
	});
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
		completed.store(true);
	});
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
		completed.store(true);
	});
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
	connect(manager.get(), &OpManager::trashFallbackRequested, this,
			[&requested] { requested.release(); }, Qt::DirectConnection);
	auto *workerManager = manager.get();
	manager->m_job.start([workerManager, &completed, &accepted]
	{
		accepted.store(workerManager->confirmTrashFallback({{"source", "trash", "Unavailable"}}));
		completed.store(true);
	});
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
		completed.store(true);
	});
	QTRY_VERIFY_WITH_TIMEOUT(controller->m_trashFallbackDialog, 5000);
	QPointer<QMessageBox> dialog = controller->m_trashFallbackDialog;
	controller.reset();
	QVERIFY(dialog.isNull());
	QVERIFY(completed.load());
	QVERIFY(!accepted.load());
}

QTEST_MAIN(TestOperationUi)
#include "tst_operationui.moc"
