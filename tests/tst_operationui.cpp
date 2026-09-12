#include "mainwindow.h"
#include "opjournal.h"
#include "progressdialog.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
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
	void interrupted_dialog_escape_does_not_abandon();
	void interrupted_dialog_close_does_not_abandon();
	void interrupted_dialog_cancel_keeps_completed_effects();
	void interrupted_dialog_resume_starts_only_old_job();
	void interrupted_undo_resumes_with_debug_flag_off();
	void observed_removals_prune_rows_even_when_job_needs_attention();
	void rebalance_dialog_blocks_other_operation_entrypoints();
	void verification_is_saved_per_job();
	void same_session_refresh_and_stale_result_guard();
	void facade_refuses_second_job_without_cancelling_first();
	void progress_cancel_is_acknowledged_once();
private:
	OpRequest request(const QString &name = QStringLiteral("old"), int count = 1);
	QString makeInterrupted(OpRequest request, bool completeFirst = false);
	void clickInterrupted(const QString &button);
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
		const auto pending = OpRescue::pending(path("journals"));
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

void TestOperationUi::debug_flags_default_off_and_text_undo_works()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.show();
	QVERIFY(window.m_verifyCopiesAct->isCheckable());
	QVERIFY(!window.m_verifyCopiesAct->isChecked());
	QVERIFY(window.m_enableUndoAct->isCheckable());
	QVERIFY(!window.m_enableUndoAct->isChecked());
	QVERIFY(!window.m_undoAct->isVisible());
	QVERIFY(window.m_undoAct->shortcut().isEmpty());
	window.m_searchField->setFocus();
	QTest::keyClicks(window.m_searchField, "typed search");
	QVERIFY(window.m_searchField->isUndoAvailable());
	QTest::keySequence(window.m_searchField, QKeySequence::Undo);
	QVERIFY(window.m_searchField->text().isEmpty());
	window.m_enableUndoAct->setChecked(true);
	QVERIFY(window.m_undoAct->isVisible());
	QCOMPARE(window.m_undoAct->shortcut(), QKeySequence(QKeySequence::Undo));
	window.m_enableUndoAct->setChecked(false);
	QVERIFY(!window.m_undoAct->isVisible());
	QVERIFY(window.m_undoAct->shortcut().isEmpty());
}
void TestOperationUi::interrupted_dialog_escape_does_not_abandon()
{
	const auto old = request();
	const auto journalPath = makeInterrupted(old);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	clickInterrupted("escape");
	QVERIFY(!window.dispatchRequest(request("attempted")));
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
	QVERIFY(!window.resolvePreviousJob());
	QVERIFY(!OpJournal::readOne(journalPath)->dismissed);
}
void TestOperationUi::interrupted_dialog_cancel_keeps_completed_effects()
{
	const auto old = request("old", 2);
	const auto journalPath = makeInterrupted(old, true);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	clickInterrupted("cancelInterruptedJobButton");
	QVERIFY(window.resolvePreviousJob());
	const auto record = OpJournal::readOne(journalPath);
	QVERIFY(record && record->dismissed);
	QVERIFY(QFileInfo::exists(path("old/destination/clip-0.bin")));
	QVERIFY(QFileInfo::exists(old.items[1].src));
	QVERIFY(!QFileInfo::exists(path("old/destination/clip-1.bin")));
	QVERIFY(OpRescue::pending().isEmpty());
	QVERIFY(OpJournal::latestUndoable());
	QCOMPARE(OpJournal::latestUndoable()->path, journalPath);
}
void TestOperationUi::interrupted_dialog_resume_starts_only_old_job()
{
	auto old = request();
	old.verifyCopies = true;
	const auto journalPath = makeInterrupted(old);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy finished(window.m_fileOps, &OpManager::operationFinished);
	clickInterrupted("resumeInterruptedJobButton");
	QVERIFY(!window.dispatchRequest(request("attempted")));
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
	const auto pending = OpRescue::pending();
	QCOMPARE(pending.size(), 1);
	QCOMPARE(pending.first().kind, OpKind::Undo);
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QVERIFY(!window.m_enableUndoAct->isChecked());
	QSignalSpy finished(window.m_fileOps, &OpManager::operationFinished);
	clickInterrupted("resumeInterruptedJobButton");
	QVERIFY(!window.dispatchRequest(request("attempted")));
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(OpRescue::pending().isEmpty());
	QVERIFY(!QFileInfo::exists(path("original/destination/clip-0.bin")));
	QVERIFY(!QFileInfo::exists(path("original/destination/clip-1.bin")));
	QVERIFY(QFileInfo::exists(original.items[0].src));
	QVERIFY(QFileInfo::exists(original.items[1].src));
	QVERIFY(!QFileInfo::exists(path("attempted/destination/clip-0.bin")));
}
void TestOperationUi::observed_removals_prune_rows_even_when_job_needs_attention()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	MediaFile retired, removed, retained;
	retired.filePath = path("retired.mxf");
	removed.filePath = path("removed.mxf");
	retained.filePath = path("retained.mxf");
	window.m_model->setMediaFiles({retired, removed, retained});
	window.m_removeAfterOp = true;
	OpResult retirement;
	retirement.state = OpResult::State::SourceRetained;
	retirement.source = retired.filePath;
	retirement.sourceRemoved = true;
	emit window.m_fileOps->operationResult(retirement);
	OpResult removal;
	removal.state = OpResult::State::NeedsAttention;
	removal.source = removed.filePath;
	removal.sourceRemoved = true;
	emit window.m_fileOps->operationResult(removal);
	OpResult kept;
	kept.state = OpResult::State::NeedsAttention;
	kept.source = retained.filePath;
	emit window.m_fileOps->operationResult(kept);
	emit window.m_fileOps->operationFinished(0, 3);
	QTRY_COMPARE(window.m_model->rowCount(), 1);
	QCOMPARE(window.m_model->fileAt(0).filePath, retained.filePath);
}
void TestOperationUi::rebalance_dialog_blocks_other_operation_entrypoints()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.m_enableUndoAct->setChecked(true);
	window.m_undoCandidate.path = path("earlier-journal.jsonl");
	OpRescue::Resumable interrupted;
	interrupted.journalPath = path("interrupted-journal.jsonl");
	window.m_resumable = {interrupted};
	window.m_rebalanceDialogActive = true;
	window.updateResumeAction();
	QVERIFY(!window.m_undoAct->isEnabled());
	QVERIFY(!window.m_resumeAct->isEnabled());
	QVERIFY(!window.dispatchRequest(request("competing")));
	QVERIFY(!window.resolvePreviousJob());
	window.onUndoLastOperation();
	QVERIFY(!window.m_fileOps->isRunning());
	QVERIFY(OpJournal::scan().isEmpty());
	QVERIFY(!QFileInfo::exists(path("competing/destination/clip-0.bin")));
}
void TestOperationUi::verification_is_saved_per_job()
{
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	QSignalSpy finished(window.m_fileOps, &OpManager::operationFinished);
	QVERIFY(window.dispatchRequest(request("unchecked")));
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_historyLoading, 15000);
	auto record = OpJournal::scan().first();
	QVERIFY(!record.request.verifyCopies);
	QVERIFY(record.entries.first().hash.isEmpty());
	window.m_verifyCopiesAct->setChecked(true);
	QVERIFY(window.dispatchRequest(request("checked")));
	// A later toggle cannot change the already accepted request.
	window.m_verifyCopiesAct->setChecked(false);
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 15000);
	record = OpJournal::scan().last();
	QVERIFY(record.request.verifyCopies);
	QVERIFY(!record.entries.first().hash.isEmpty());
}
void TestOperationUi::same_session_refresh_and_stale_result_guard()
{
	const auto journalPath = makeInterrupted(request());
	MainWindow window(nullptr, MainWindow::StartupMode::UiOnly);
	window.refreshResumable();
	QTRY_VERIFY_WITH_TIMEOUT(!window.m_historyLoading, 15000);
	QVERIFY(window.m_resumeAct->isEnabled());
	QCOMPARE(window.m_resumable.first().journalPath, journalPath);
	window.refreshResumable();
	++window.m_historyGeneration; // A newer dispatch invalidates the pending read.
	window.m_resumable.clear();
	window.m_historyLoading = false;
	QThreadPool::globalInstance()->waitForDone();
	QCoreApplication::processEvents();
	QVERIFY(window.m_resumable.isEmpty());
}
void TestOperationUi::facade_refuses_second_job_without_cancelling_first()
{
	OpManager manager;
	QSignalSpy finished(&manager, &OpManager::operationFinished);
	const auto first = request("first");
	manager.execute(first);
	QVERIFY(manager.isRunning());
	manager.execute(request("second"));
	QVERIFY(manager.isRunning());
	QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
	QVERIFY(!manager.isRunning());
	QVERIFY(QFileInfo::exists(path("first/destination/clip-0.bin")));
	QVERIFY(!QFileInfo::exists(path("second/destination/clip-0.bin")));
	QString error;
	auto lock = OpJournal::acquire(path("journals"), error);
	QVERIFY2(lock, qPrintable(error));
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

QTEST_MAIN(TestOperationUi)
#include "tst_operationui.moc"
