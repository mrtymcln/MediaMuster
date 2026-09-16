#include "fileoperationcontroller.h"
#include "formatutil.h"
#include "opjournal.h"
#include "progressdialog.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace
{
	// Only a button click chooses Cancel. QDialog's ordinary reject path
	// (Escape or window close) leaves the job unresolved and changes nothing.
	class InterruptedJobDialog : public QDialog
	{
	public:
		enum class Choice
		{
			Unresolved,
			Resume,
			Cancel
		};
		explicit InterruptedJobDialog(const OperationRecovery::Resumable &job, QWidget *parent)
			: QDialog(parent)
		{
			setObjectName(QStringLiteral("interruptedJobDialog"));
			setWindowTitle(tr("Interrupted job"));
			auto *layout = new QVBoxLayout(this);
			auto *headline = new QLabel(tr("The previous job was interrupted."), this);
			headline->setTextFormat(Qt::PlainText);
			layout->addWidget(headline);
			auto *details =
				new QLabel(tr("%1 of %2 files finished.\n\n"
							  "Resume continues that job. Cancel abandons its unfinished work "
							  "and keeps the completed results.")
							   .arg(Format::count(job.finished), Format::count(job.total)),
						   this);
			details->setTextFormat(Qt::PlainText);
			details->setWordWrap(true);
			details->setMinimumWidth(380);
			layout->addWidget(details);
			auto *buttons = new QDialogButtonBox(this);
			auto *resume = buttons->addButton(tr("Resume"), QDialogButtonBox::AcceptRole);
			auto *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::ActionRole);
			resume->setObjectName(QStringLiteral("resumeInterruptedJobButton"));
			cancel->setObjectName(QStringLiteral("cancelInterruptedJobButton"));
			connect(resume, &QPushButton::clicked, this,
					[this]
					{
						choice = Choice::Resume;
						accept();
					});
			connect(cancel, &QPushButton::clicked, this,
					[this]
					{
						choice = Choice::Cancel;
						accept();
					});
			layout->addWidget(buttons);
		}
		Choice choice = Choice::Unresolved;
	};

	class RestoreOriginalsDialog : public QDialog
	{
	public:
		explicit RestoreOriginalsDialog(const QVector<OperationRecovery::Restorable> &jobs, QWidget *parent)
			: QDialog(parent)
		{
			setObjectName(QStringLiteral("restoreOriginalsDialog"));
			setWindowTitle(tr("Restore interrupted originals"));
			auto *layout = new QVBoxLayout(this);
			auto *description = new QLabel(
				tr("An interrupted move left these originals in temporary folders. "
				   "Restore puts them back without replacing files already there. "
				   "Completed destination copies are kept."), this);
			description->setTextFormat(Qt::PlainText);
			description->setWordWrap(true);
			layout->addWidget(description);
			auto *jobsList = new QComboBox(this);
			jobsList->setObjectName(QStringLiteral("restoreOriginalsJob"));
			for (qsizetype n = 0; n < jobs.size(); ++n)
				jobsList->addItem(tr("Job %1 — %2 original(s)").arg(n + 1).arg(jobs[n].originals.size()));
			jobsList->setVisible(jobs.size() > 1);
			layout->addWidget(jobsList);
			auto *paths = new QPlainTextEdit(this);
			paths->setObjectName(QStringLiteral("restoreOriginalPaths"));
			paths->setReadOnly(true);
			paths->setLineWrapMode(QPlainTextEdit::NoWrap);
			paths->setMinimumSize(560, 180);
			layout->addWidget(paths);
			const auto showJob = [this, jobs, paths](int index)
			{
				selectedJob = index;
				QStringList locations;
				const auto &job = jobs[index];
				for (qsizetype n = 0; n < job.originals.size(); ++n)
					locations.append(tr("Kept at: %1\nRestore to: %2")
						.arg(job.retainedPaths.value(n), job.originals[n]));
				paths->setPlainText(locations.join(QStringLiteral("\n\n")));
			};
			connect(jobsList, &QComboBox::currentIndexChanged, this, showJob);
			showJob(0);
			auto *buttons = new QDialogButtonBox(this);
			auto *restore = buttons->addButton(tr("Restore"), QDialogButtonBox::AcceptRole);
			auto *close = buttons->addButton(tr("Close"), QDialogButtonBox::RejectRole);
			restore->setObjectName(QStringLiteral("restoreOriginalsButton"));
			close->setObjectName(QStringLiteral("closeRestoreOriginalsButton"));
			close->setDefault(true);
			connect(restore, &QPushButton::clicked, this, &QDialog::accept);
			connect(close, &QPushButton::clicked, this, &QDialog::reject);
			layout->addWidget(buttons);
		}
		int selectedJob = 0;
	};

	OperationRecovery::Summary operationHistory()
	{
		OperationRecovery::Summary result;
		result.resumable = OperationRecovery::pending();
		result.restorable = OperationRecovery::restorable();
		result.undoCandidate = OpJournal::latestUndoable();
		return result;
	}

} // namespace

FileOperationController::FileOperationController(QWidget *window)
	: QObject(window), m_window(window), m_fileOps(new OpManager(this)),
	  m_resumeAct(new QAction(tr("Resume Interrupted Operation..."), this)),
	  m_restoreOriginalsAct(new QAction(tr("Restore Interrupted Originals..."), this)),
	  m_undoAct(new QAction(tr("&Undo"), this)),
	  m_verifyCopiesAct(new QAction(tr("Verify copies"), this)),
	  m_enableUndoAct(new QAction(tr("Enable Undo"), this))
{
	m_undoAct->setObjectName(QStringLiteral("undoFileOperationAction"));
	m_restoreOriginalsAct->setObjectName(QStringLiteral("restoreInterruptedOriginalsAction"));
	m_verifyCopiesAct->setObjectName(QStringLiteral("verifyCopiesDebugAction"));
	m_enableUndoAct->setObjectName(QStringLiteral("enableUndoDebugAction"));
	m_verifyCopiesAct->setCheckable(true);
	m_enableUndoAct->setCheckable(true);
	connect(m_resumeAct, &QAction::triggered, this, &FileOperationController::offerResume);
	connect(m_restoreOriginalsAct, &QAction::triggered, this,
			&FileOperationController::offerRestoreOriginals);
	connect(m_undoAct, &QAction::triggered, this, &FileOperationController::undoLastOperation);
	connect(m_enableUndoAct, &QAction::toggled, this,
			[this](bool enabled)
			{
				m_fileOps->setUndoEnabled(enabled);
				updateUndoAction();
			});
	connect(
		m_fileOps, &OpManager::operationProgress, this,
		[this](const QString &name, int current, int total, double pct)
		{
			progressDialog()->setItemProgress(current, total, pct);
			progressDialog()->setDetail(name);
		},
		Qt::QueuedConnection);
	connect(
		m_fileOps, &OpManager::operationLog, this, [this](QtMsgType level, const QString &message)
		{ emit logMessage(level, QStringLiteral("ops"), message); }, Qt::QueuedConnection);
	connect(
		m_fileOps, &OpManager::operationResult, this,
		[this](const OpResult &result)
		{
			QString state;
			switch (result.state)
			{
			case OpResult::State::Completed:
				state = tr("Completed");
				break;
			case OpResult::State::SourceRetained:
				state = m_restoringOriginals ? tr("Original restored") : tr("Copied; source retained");
				break;
			case OpResult::State::NoEffect:
				state = tr("Already at destination");
				break;
			case OpResult::State::Skipped:
				state = tr("Skipped");
				break;
			case OpResult::State::Cancelled:
				state = tr("Cancelled");
				break;
			case OpResult::State::Failed:
				state = tr("Failed");
				break;
			case OpResult::State::NeedsAttention:
				state = tr("Needs attention");
				break;
			}
			const bool problem = result.state == OpResult::State::Failed ||
								 result.state == OpResult::State::NeedsAttention;
			emit logMessage(problem ? QtWarningMsg : QtInfoMsg, QStringLiteral("ops"),
							result.name + ": " + state +
								(result.message.isEmpty() ? QString() : " — " + result.message));
			if (result.sourceRemoved && m_pruneSourceRowsAfterOperation)
				m_removedSourcePaths.insert(result.source);
			if (m_restoringOriginals && result.state == OpResult::State::SourceRetained &&
				!result.sourceRemoved)
				m_restoredOriginalPaths.insert(result.source);
		},
		Qt::QueuedConnection);
	connect(
		m_fileOps, &OpManager::operationFinished, this,
		[this](int, int)
		{
			if (m_progressDialog)
				m_progressDialog->finish();
			setActivity(Activity::Idle);
			if (m_pruneSourceRowsAfterOperation && !m_removedSourcePaths.isEmpty())
				emit sourcesRemoved(m_removedSourcePaths);
			m_pruneSourceRowsAfterOperation = false;
			m_removedSourcePaths.clear();
			m_restoringOriginals = false;
			const auto restored = m_restoredOriginalPaths;
			m_restoredOriginalPaths.clear();
			if (!restored.isEmpty())
				emit originalsRestored(restored);
			refreshHistory();
		},
		Qt::QueuedConnection);
	connect(m_fileOps, &OpManager::mediaMusterTrashUsed, this,
			&FileOperationController::mediaMusterTrashUsed, Qt::QueuedConnection);
	connect(m_fileOps, &OpManager::trashFallbackRequested, this,
			&FileOperationController::showTrashFallback, Qt::QueuedConnection);
	connect(m_fileOps, &OpManager::trashFallbackFinished, this,
			&FileOperationController::closeTrashFallback, Qt::QueuedConnection);
	m_fileOps->setTrashFallbackHandlerAvailable(true);
	updateResumeAction();
}

FileOperationController::~FileOperationController()
{
	m_fileOps->setTrashFallbackHandlerAvailable(false);
	m_fileOps->cancel();
	// The dialog belongs to the window/progress sheet, so it would otherwise
	// outlive this controller when the controller is destroyed independently.
	delete m_trashFallbackDialog.data();
}

void FileOperationController::showTrashFallback(quint64 requestId,
												const QVector<OpTrashFallbackItem> &items)
{
	if (!m_fileOps->isTrashFallbackPending(requestId))
		return;
	if (m_trashFallbackDialog)
		closeTrashFallback(m_trashFallbackRequest);
	auto *parent = m_progressDialog && m_progressDialog->isVisible()
					   ? static_cast<QWidget *>(m_progressDialog)
					   : m_window;
	auto *dialog = new QMessageBox(parent);
	m_trashFallbackDialog = dialog;
	m_trashFallbackRequest = requestId;
	dialog->setObjectName(QStringLiteral("trashFallbackDialog"));
	dialog->setIcon(QMessageBox::Information);
	dialog->setWindowTitle(tr("MediaMuster Trash"));
	dialog->setTextFormat(Qt::PlainText);
	dialog->setText(tr("Move these files to MediaMuster Trash?"));
	dialog->setInformativeText(tr("The system trash couldn’t accept these files. Keep them in MediaMuster Trash until you decide."));
	QStringList details;
	for (const auto &item : items)
		details.append(tr("File: %1\nReason: %2\nMediaMuster Trash: %3")
						   .arg(item.source, item.reason, item.destination));
	dialog->setDetailedText(details.join(QStringLiteral("\n\n")));
	auto *cancel = dialog->addButton(tr("Cancel"), QMessageBox::RejectRole);
	auto *move = dialog->addButton(tr("Move"), QMessageBox::AcceptRole);
	cancel->setObjectName(QStringLiteral("cancelTrashFallbackButton"));
	move->setObjectName(QStringLiteral("acceptTrashFallbackButton"));
	dialog->setDefaultButton(cancel);
	dialog->setEscapeButton(cancel);
	connect(dialog, &QDialog::finished, this, [this, dialog, move, requestId]
			{
		m_fileOps->respondTrashFallback(requestId, dialog->clickedButton() == move);
		if (m_trashFallbackDialog == dialog)
		{
			m_trashFallbackDialog = nullptr;
			m_trashFallbackRequest = 0;
		}
		dialog->deleteLater(); });
	dialog->open();
}

void FileOperationController::closeTrashFallback(quint64 requestId)
{
	if (m_trashFallbackDialog && m_trashFallbackRequest == requestId)
		m_trashFallbackDialog->reject();
}

void FileOperationController::setUndoSeparator(QAction *separator)
{
	m_undoSeparator = separator;
	updateUndoAction();
}

void FileOperationController::setActivity(Activity activity)
{
	if (m_activity == activity)
		return;
	m_activity = activity;
	updateResumeAction();
	emit activityChanged(activity);
}

bool FileOperationController::resolveBeforeRebalance()
{
	if (m_activity != Activity::RebalanceDialog)
		return false;
	setActivity(Activity::Idle);
	const bool resolved = resolvePreviousJob();
	if (isIdle())
		setActivity(Activity::RebalanceDialog);
	return resolved;
}

void FileOperationController::endRebalanceDialog()
{
	if (m_activity == Activity::RebalanceDialog)
		setActivity(Activity::Idle);
}

ProgressDialog *FileOperationController::progressDialog()
{
	if (!m_progressDialog)
	{
		m_progressDialog = new ProgressDialog(m_window);
		connect(m_progressDialog, &ProgressDialog::cancelRequested, this,
				[this]
				{
					m_fileOps->cancel();
					emit logMessage(QtWarningMsg, QStringLiteral("ops"), tr("Cancel requested"));
				});
	}
	return m_progressDialog;
}

void FileOperationController::runStartupRecovery()
{
	setActivity(Activity::Recovering);
	const quint64 generation = ++m_historyGeneration;
	m_historyLoading = true;
	// Journal cleanup and recovery run off the UI thread, each under the
	// operation lock. Cleanup never resolves or modifies media paths.
	auto *watcher = new QFutureWatcher<OperationRecovery::Summary>(this);
	connect(watcher, &QFutureWatcher<OperationRecovery::Summary>::finished, this,
			[this, watcher, generation]
			{
				const OperationRecovery::Summary summary = watcher->result();
				watcher->deleteLater();
				if (generation != m_historyGeneration)
					return;
				m_historyLoading = false;
				setActivity(Activity::Idle);
				onRecoveryDone(summary);
			});
	watcher->setFuture(QtConcurrent::run([]
										 {
		QString cleanupError;
		const bool cleaned = OpJournal::prune({}, cleanupError);
		auto summary = OperationRecovery::run();
		if (!cleaned)
			summary.notes.prepend("Journal cleanup: " + cleanupError);
		return summary; }));
}

void FileOperationController::onRecoveryDone(const OperationRecovery::Summary &summary)
{
	// Report unresolved recovery evidence, then offer the unfinished job.
	for (const QString &note : summary.notes)
		emit logMessage(summary.hadTrouble() ? QtWarningMsg : QtInfoMsg, QStringLiteral("app"),
						note);

	if (summary.hadTrouble())
		QMessageBox::warning(m_window, tr("Some files need a look"), summary.message());

	// The launch sweep already worked this out on the pool thread; no need
	// to re-read the journal folder here.
	applyOperationHistory(summary);

	// Anything left to finish? Ask now; the File menu item stays live for
	// later if the dialog is closed without choosing either action.
	if (!m_restorable.isEmpty())
		offerRestoreOriginals();
	else if (!m_resumable.isEmpty())
		offerResume();
}

bool FileOperationController::confirmCrashProtection()
{
	if (OpJournal::standardDirWritable())
		return true;
	QMessageBox::warning(m_window, tr("Operation cannot start"),
						 tr("MediaMuster cannot write its operation journal. Check free space and "
							"permissions on the system disk, then try again."));
	return false;
}

void FileOperationController::updateUndoAction()
{
	if (!m_undoAct)
		return;
	const bool enabled = m_enableUndoAct && m_enableUndoAct->isChecked();
	m_undoAct->setVisible(enabled);
	if (m_undoSeparator)
		m_undoSeparator->setVisible(enabled);
	m_undoAct->setShortcut(enabled ? QKeySequence(QKeySequence::Undo) : QKeySequence());
	m_undoAct->setEnabled(enabled && !m_historyLoading && !m_undoCandidate.path.isEmpty() &&
						  isIdle());
	m_undoAct->setText(m_undoCandidate.label.isEmpty() ? tr("&Undo") : m_undoCandidate.label);
}

void FileOperationController::undoLastOperation()
{
	if (!m_enableUndoAct->isChecked() || !isIdle())
		return;
	// Resolve the forward remainder before selecting its completed effects
	// for Undo. Choosing Resume starts only that old job, never this Undo.
	if (!resolvePreviousJob() || m_undoCandidate.path.isEmpty())
		return;

	QString plainLabel = m_undoCandidate.label;
	plainLabel.remove(QLatin1Char('&'));
	QMessageBox confirm(m_window);
	confirm.setIcon(QMessageBox::Question);
	confirm.setWindowTitle(tr("Undo"));
	confirm.setText(tr("%1?").arg(plainLabel));
	confirm.setInformativeText(
		tr("Only completed changes will be reversed. Files will be "
		   "restored to their original locations; copies being removed go to Trash. "
		   "Changed files and occupied original locations will be reported."));
	auto *goBtn = confirm.addButton(plainLabel, QMessageBox::AcceptRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.exec();
	if (confirm.clickedButton() != goBtn)
		return;

	OpRequest request;
	request.kind = OpKind::Undo;
	request.undoJournalPath = m_undoCandidate.path;
	if (dispatchRequest(std::move(request)))
		emit logMessage(QtInfoMsg, QStringLiteral("ops"),
						tr("Undoing the last operation. Rescan afterwards to refresh the table."));
}

bool FileOperationController::dispatchRequest(OpRequest request)
{
	if (!isIdle() || m_fileOps->isRunning())
		return false;
	const bool resuming = !request.resumeJournalPath.isEmpty();
	const bool restoring = !request.restoreJournalPath.isEmpty();
	if (request.items.isEmpty() && !resuming && !restoring && request.kind != OpKind::Undo)
		return false;
	if (!resuming && !restoring && !resolvePreviousJob())
		return false;
	if (request.kind == OpKind::Undo && !resuming && !restoring && !m_enableUndoAct->isChecked())
		return false;
	if (!confirmCrashProtection())
		return false;

	// Capture only new-job choices. Resume uses the policy saved in its journal.
	if (!resuming && !restoring && (request.kind == OpKind::Copy || request.kind == OpKind::Move))
		request.verifyCopies = m_verifyCopiesAct->isChecked();
	m_pruneSourceRowsAfterOperation =
		!restoring && (request.kind == OpKind::Move || request.kind == OpKind::Delete);
	m_restoringOriginals = restoring;
	m_restoredOriginalPaths.clear();
	m_removedSourcePaths.clear();
	++m_historyGeneration; // A previous asynchronous read cannot repopulate stale actions.
	m_historyLoading = false;
	m_undoCandidate = {};
	setActivity(Activity::FileOperation);
	progressDialog()->begin();
	m_fileOps->execute(std::move(request));
	return true;
}

void FileOperationController::updateResumeAction()
{
	if (m_resumeAct)
		m_resumeAct->setEnabled(!m_historyLoading && !m_resumable.isEmpty() && isIdle());
	if (m_restoreOriginalsAct)
		m_restoreOriginalsAct->setEnabled(!m_historyLoading && !m_restorable.isEmpty() && isIdle());
	updateUndoAction();
}

void FileOperationController::applyOperationHistory(const OperationRecovery::Summary &history)
{
	m_resumable = history.resumable;
	m_restorable = history.restorable;
	m_undoCandidate = {};
	if (history.undoCandidate)
	{
		m_undoCandidate.path = history.undoCandidate->path;
		switch (history.undoCandidate->request.kind)
		{
		case OpKind::Copy:
			m_undoCandidate.label = tr("&Undo Copy");
			break;
		case OpKind::Move:
			m_undoCandidate.label = tr("&Undo Move");
			break;
		case OpKind::Delete:
			m_undoCandidate.label = tr("&Undo Delete");
			break;
		case OpKind::Rename:
			m_undoCandidate.label = tr("&Undo Rebalance");
			break;
		case OpKind::Undo:
			m_undoCandidate = {};
			break;
		}
	}
	updateResumeAction();
}

void FileOperationController::refreshHistory()
{
	const quint64 generation = ++m_historyGeneration;
	m_historyLoading = true;
	m_undoCandidate = {};
	updateResumeAction();
	auto *watcher = new QFutureWatcher<OperationRecovery::Summary>(this);
	connect(watcher, &QFutureWatcher<OperationRecovery::Summary>::finished, this,
			[this, watcher, generation]
			{
				const auto history = watcher->result();
				watcher->deleteLater();
				if (generation != m_historyGeneration)
					return;
				m_historyLoading = false;
				applyOperationHistory(history);
			});
	watcher->setFuture(QtConcurrent::run(operationHistory));
}

void FileOperationController::readOperationHistoryForGate()
{
	// Read journal files on a pool thread. Paint/timer events continue while
	// the dispatch boundary waits, but a second user action cannot race it.
	// Pending discovery deliberately does not probe disconnected media.
	++m_historyGeneration;
	QFutureWatcher<OperationRecovery::Summary> watcher;
	QEventLoop loop;
	connect(&watcher, &QFutureWatcher<OperationRecovery::Summary>::finished, &loop,
			&QEventLoop::quit);
	watcher.setFuture(QtConcurrent::run(operationHistory));
	if (!watcher.isFinished())
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	m_historyLoading = false;
	applyOperationHistory(watcher.result());
}

bool FileOperationController::resolvePreviousJob()
{
	if (!isIdle() || m_fileOps->isRunning() || m_operationGateActive)
		return false;
	QScopedValueRollback<bool> guard(m_operationGateActive, true);
	readOperationHistoryForGate();
	while (!m_resumable.isEmpty())
	{
		const OperationRecovery::Resumable job = m_resumable.first();
		InterruptedJobDialog dialog(job, m_window);
		dialog.exec();
		if (dialog.choice == InterruptedJobDialog::Choice::Resume)
		{
			resumeOperation(job);
			return false;
		}
		if (dialog.choice != InterruptedJobDialog::Choice::Cancel)
			return false;
		QString error;
		if (!OpJournal::dismiss(job.journalPath, error))
		{
			emit logMessage(QtWarningMsg, QStringLiteral("ops"), error);
			QMessageBox::warning(m_window, tr("Job could not be cancelled"), error);
			refreshHistory();
			return false;
		}
		emit logMessage(
			QtInfoMsg, QStringLiteral("ops"),
			tr("Cancelled the unfinished part of the previous job. Completed results were kept."));
		readOperationHistoryForGate();
	}
	return true;
}

void FileOperationController::offerResume()
{
	// The same dialog and explicit abandonment rules apply at launch,
	// from the File menu, and before any new operation.
	resolvePreviousJob();
}

void FileOperationController::offerRestoreOriginals()
{
	if (!isIdle() || m_fileOps->isRunning() || m_operationGateActive)
		return;
	QScopedValueRollback<bool> guard(m_operationGateActive, true);
	readOperationHistoryForGate();
	if (m_restorable.isEmpty())
		return;
	const auto jobs = m_restorable;
	RestoreOriginalsDialog dialog(jobs, m_window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	OpRequest request;
	request.restoreJournalPath = jobs[dialog.selectedJob].journalPath;
	if (dispatchRequest(std::move(request)))
		emit logMessage(QtInfoMsg, QStringLiteral("ops"), tr("Restoring interrupted originals."));
}

bool FileOperationController::resumeOperation(const OperationRecovery::Resumable &job)
{
	OpRequest request;
	request.kind = job.kind;
	request.destRoot = job.dest;
	request.preserve = job.preserve;
	request.items = job.remaining;
	request.resumeJournalPath = job.journalPath;
	emit logMessage(QtInfoMsg, QStringLiteral("ops"), tr("Resuming the previous job."));
	return dispatchRequest(std::move(request));
}
