#include "fileoperationcontroller.h"
#include "featureflags.h"
#include "opjournal.h"
#include "progressdialog.h"
#include "unfinishedbusinessdialog.h"

#include <QAction>
#include <QDialog>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QtConcurrent>
#include <algorithm>

namespace
{
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
	: QObject(window), m_window(window), m_operationManager(new OpManager(this)),
	  m_recoveryAction(new QAction(tr("Unfinished Business…"), this)),
	  m_undoAction(new QAction(tr("&Undo"), this))
{
	m_undoAction->setObjectName(QStringLiteral("undoFileOperationAction"));
	m_recoveryAction->setObjectName(QStringLiteral("unfinishedBusinessAction"));
	setUndoEnabled(FeatureFlags::kUndo);
	connect(m_recoveryAction, &QAction::triggered, this, &FileOperationController::offerRecovery);
	connect(m_undoAction, &QAction::triggered, this, &FileOperationController::undoLastOperation);
	connect(
		m_operationManager, &OpManager::operationProgress, this,
		[this](const QString &name, int current, int total, double pct)
		{
			progressDialog()->setItemProgress(current, total, pct);
			progressDialog()->setDetail(name);
		},
		Qt::QueuedConnection);
	connect(
		m_operationManager, &OpManager::operationLog, this, [this](QtMsgType level, const QString &message)
		{ emit logMessage(level, QStringLiteral("operations"), message); }, Qt::QueuedConnection);
	connect(m_operationManager, &OpManager::operationResult, this,
			&FileOperationController::onOperationResult, Qt::QueuedConnection);
	connect(m_operationManager, &OpManager::operationFinished, this,
			&FileOperationController::onOperationFinished, Qt::QueuedConnection);
	connect(m_operationManager, &OpManager::mediaMusterTrashUsed, this,
			&FileOperationController::mediaMusterTrashUsed, Qt::QueuedConnection);
	connect(m_operationManager, &OpManager::trashFallbackRequested, this,
			&FileOperationController::showTrashFallback, Qt::QueuedConnection);
	connect(m_operationManager, &OpManager::trashFallbackFinished, this,
			&FileOperationController::closeTrashFallback, Qt::QueuedConnection);
	m_operationManager->setTrashFallbackHandlerAvailable(true);
	updateRecoveryAction();
}

void FileOperationController::onOperationResult(const OpResult &result)
{
	QString state;
	bool appendDetail = true;
	switch (result.state)
	{
	case OpResult::State::Completed:
	case OpResult::State::OriginalRestored:
		state = result.message;
		appendDetail = false;
		break;
	case OpResult::State::SourceRetained:
		state = tr("Source kept");
		break;
	case OpResult::State::NoEffect:
		state = tr("Already at destination.");
		appendDetail = false;
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
	if (!state.isEmpty())
	{
		const bool problem = result.state == OpResult::State::Failed ||
							 result.state == OpResult::State::NeedsAttention;
		const bool stateFirst = result.state == OpResult::State::Skipped ||
								result.state == OpResult::State::Cancelled ||
								result.state == OpResult::State::Failed;
		const bool colonDetail = stateFirst || result.state == OpResult::State::SourceRetained;
		QString message = stateFirst ? state + ": " + result.name : result.name + ": " + state;
		if (appendDetail && !result.message.isEmpty())
			message += (colonDetail ? ": " : " — ") + result.message;
		if ((colonDetail || result.state == OpResult::State::Completed ||
			 result.state == OpResult::State::OriginalRestored) &&
			!message.endsWith(QLatin1Char('.')))
			message += QLatin1Char('.');
		emit logMessage(problem ? QtWarningMsg : QtInfoMsg, QStringLiteral("operations"),
						message);
	}
	if (result.state == OpResult::State::Completed &&
		(m_activeKind == OpKind::Copy || m_activeKind == OpKind::Move) && !result.destination.isEmpty())
		emit transferCompleted(result.source, result.destination, !result.sourceRemoved);
	if (result.sourceRemoved && m_pruneSourceRowsAfterOperation)
		m_removedSourcePaths.insert(result.source);
	if (!result.restoredOriginalPath.isEmpty())
		m_restoredOriginalPaths.insert(result.restoredOriginalPath);
}

void FileOperationController::onOperationFinished()
{
	if (m_progressDialog)
		m_progressDialog->finish();
	setActivity(Activity::Idle);
	if (m_pruneSourceRowsAfterOperation && !m_removedSourcePaths.isEmpty())
		emit sourcesRemoved(m_removedSourcePaths);
	m_pruneSourceRowsAfterOperation = false;
	m_removedSourcePaths.clear();
	const auto restored = m_restoredOriginalPaths;
	m_restoredOriginalPaths.clear();
	if (!restored.isEmpty())
		emit originalsRestored(restored);
	refreshHistory();
}

FileOperationController::~FileOperationController()
{
	m_operationManager->setTrashFallbackHandlerAvailable(false);
	m_operationManager->cancel();
	// The dialog belongs to the window/progress sheet, so it would otherwise
	// outlive this controller when the controller is destroyed independently.
	delete m_trashFallbackDialog.data();
}

void FileOperationController::showTrashFallback(quint64 requestId,
												const QVector<OpTrashFallbackItem> &items)
{
	if (!m_operationManager->isTrashFallbackPending(requestId))
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
				m_operationManager->respondTrashFallback(requestId, dialog->clickedButton() == move);
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

void FileOperationController::setActivity(Activity activity)
{
	if (m_activity == activity)
		return;
	m_activity = activity;
	updateRecoveryAction();
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
					m_operationManager->cancel();
					emit logMessage(QtWarningMsg, QStringLiteral("operations"), tr("Cancel requested"));
				});
	}
	return m_progressDialog;
}

void FileOperationController::runStartupRecovery()
{
	setActivity(Activity::Recovering);
	const quint64 generation = ++m_historyGeneration;
	m_historyLoading = true;
	// Prune journal history, then reconcile interrupted work off the UI thread.
	// Recovery shares one locked history across pruning and the launch checks.
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
										 { return OperationRecovery::run(); }));
}

void FileOperationController::onRecoveryDone(const OperationRecovery::Summary &summary)
{
	// Report unresolved recovery evidence, then offer the unfinished job.
	for (const QString &note : summary.notes)
		emit logMessage(summary.hadTrouble() ? QtWarningMsg : QtInfoMsg, QStringLiteral("operations"),
						note);

	if (summary.hadTrouble())
		QMessageBox::warning(m_window, tr("Some files need a look"), summary.message());

	// Show the launch results; dispatch rechecks history before acting on a choice.
	applyOperationHistory(summary);

	// Offer unfinished work now; closing the dialog leaves it available in the File menu.
	if (!m_restorable.isEmpty() || !m_resumable.isEmpty())
		offerRecovery();
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

void FileOperationController::setUndoEnabled(bool enabled)
{
	m_undoEnabled = enabled;
	m_operationManager->setUndoEnabled(enabled);
	updateUndoAction();
}

void FileOperationController::updateUndoAction()
{
	const bool enabled = m_undoEnabled;
	m_undoAction->setVisible(enabled);
	m_undoAction->setShortcut(enabled ? QKeySequence(QKeySequence::Undo) : QKeySequence());
	m_undoAction->setEnabled(enabled && !m_historyLoading && !m_undoCandidate.journalPath.isEmpty() &&
							 isIdle());
	m_undoAction->setText(m_undoCandidate.undoText.isEmpty() ? tr("&Undo") : m_undoCandidate.undoText);
}

void FileOperationController::undoLastOperation()
{
	if (!m_undoEnabled || !isIdle())
		return;
	// Resolve the forward remainder before selecting its completed effects
	// for Undo. Choosing Resume starts only that old job, never this Undo.
	if (!resolvePreviousJob() || m_undoCandidate.journalPath.isEmpty())
		return;

	QString plainUndoText = m_undoCandidate.undoText;
	plainUndoText.remove(QLatin1Char('&'));
	QMessageBox confirm(m_window);
	confirm.setIcon(QMessageBox::Question);
	confirm.setWindowTitle(QString());
	confirm.setText(m_undoCandidate.confirmationHeading);
	confirm.setInformativeText(m_undoCandidate.confirmationMessage);
	auto *goBtn = confirm.addButton(plainUndoText, QMessageBox::AcceptRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.exec();
	if (confirm.clickedButton() != goBtn)
		return;

	OpRequest request;
	request.kind = OpKind::Undo;
	request.undoJournalPath = m_undoCandidate.journalPath;
	if (dispatchRequest(std::move(request)))
		emit logMessage(QtInfoMsg, QStringLiteral("operations"),
						tr("Undoing the previous operation."));
}

bool FileOperationController::dispatchRequest(OpRequest request)
{
	if (!isIdle() || m_operationManager->isRunning())
		return false;
	const bool resuming = !request.resumeJournalPath.isEmpty();
	const bool restoring = !request.restoreJournalPath.isEmpty();
	if (request.items.isEmpty() && !resuming && !restoring && request.kind != OpKind::Undo)
		return false;
	if (!resuming && !restoring && !resolvePreviousJob())
		return false;
	if (request.kind == OpKind::Undo && !resuming && !restoring && !m_undoEnabled)
		return false;
	if (!confirmCrashProtection())
		return false;

	m_pruneSourceRowsAfterOperation =
		!restoring && (request.kind == OpKind::Move || request.kind == OpKind::Delete || request.kind == OpKind::Undo);
	m_activeKind = request.kind;
	if (resuming)
		if (const auto journal = OpJournal::readOne(request.resumeJournalPath))
			m_activeKind = journal->request.kind;
	m_restoredOriginalPaths.clear();
	m_removedSourcePaths.clear();
	++m_historyGeneration; // A previous asynchronous read cannot repopulate stale actions.
	m_historyLoading = false;
	m_undoCandidate = {};
	setActivity(Activity::FileOperation);
	progressDialog()->begin();
	m_operationManager->execute(std::move(request));
	return true;
}

void FileOperationController::updateRecoveryAction()
{
	m_recoveryAction->setEnabled(!m_historyLoading && isIdle() &&
							  (!m_resumable.isEmpty() || !m_restorable.isEmpty()));
	updateUndoAction();
}

void FileOperationController::applyOperationHistory(const OperationRecovery::Summary &history)
{
	m_resumable = history.resumable;
	m_restorable = history.restorable;
	m_undoCandidate = {};
	if (history.undoCandidate)
	{
		m_undoCandidate.journalPath = history.undoCandidate->path;
		switch (history.undoCandidate->request.kind)
		{
		case OpKind::Copy:
			m_undoCandidate.undoText = tr("&Undo Copy");
			m_undoCandidate.confirmationHeading = tr("Undo the last copy operation?");
			m_undoCandidate.confirmationMessage = tr("The copies will be moved to the trash.");
			break;
		case OpKind::Move:
			m_undoCandidate.undoText = tr("&Undo Move");
			m_undoCandidate.confirmationHeading = tr("Undo the last move operation?");
			m_undoCandidate.confirmationMessage = tr("The files will be returned to their original location.");
			break;
		case OpKind::Delete:
			m_undoCandidate.undoText = tr("&Undo Delete");
			m_undoCandidate.confirmationHeading = tr("Undo the last delete operation?");
			m_undoCandidate.confirmationMessage = tr("The files will be returned to their original location.");
			break;
		case OpKind::Rename:
			m_undoCandidate.undoText = tr("&Undo Rebalance");
			m_undoCandidate.confirmationHeading = tr("Undo the last rebalance?");
			m_undoCandidate.confirmationMessage = tr("All files will be returned to their original Avid MediaFiles folders.");
			break;
		case OpKind::Undo:
			m_undoCandidate = {};
			break;
		}
	}
	updateRecoveryAction();
}

void FileOperationController::refreshHistory()
{
	const quint64 generation = ++m_historyGeneration;
	m_historyLoading = true;
	m_undoCandidate = {};
	updateRecoveryAction();
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
	if (!isIdle() || m_operationManager->isRunning() || m_operationGateActive)
		return false;
	QScopedValueRollback<bool> guard(m_operationGateActive, true);
	readOperationHistoryForGate();
	while (!m_resumable.isEmpty())
	{
		// Resuming an old job must never also start the new
		// request waiting at this gate. Only choosing Stop allows a new request.
		if (showRecoveryDialog(m_resumable.first().journalPath) != RecoveryOutcome::Stopped)
			return false;
	}
	return true;
}

void FileOperationController::offerRecovery()
{
	if (!isIdle() || m_operationManager->isRunning() || m_operationGateActive)
		return;
	QScopedValueRollback<bool> guard(m_operationGateActive, true);
	readOperationHistoryForGate();
	if (!m_resumable.isEmpty() || !m_restorable.isEmpty())
		showRecoveryDialog();
}

FileOperationController::RecoveryOutcome FileOperationController::showRecoveryDialog(
	const QString &preferredJournalPath)
{
	// The modal event loop can deliver history refreshes. Keep this decision
	// bound to the same value snapshot the user saw, rather than mutable indices.
	const auto resumableJobs = m_resumable;
	const auto restorableJobs = m_restorable;
	UnfinishedBusinessDialog dialog(resumableJobs, restorableJobs, preferredJournalPath, m_window);
	if (dialog.selectedJournalPath().isEmpty() || dialog.exec() != QDialog::Accepted)
		return RecoveryOutcome::Closed;

	const auto path = dialog.selectedJournalPath();
	const auto resumable = std::find_if(resumableJobs.cbegin(), resumableJobs.cend(),
										[&](const auto &job)
										{ return job.journalPath == path; });
	const auto restorable = std::find_if(restorableJobs.cbegin(), restorableJobs.cend(),
										 [&](const auto &job)
										 { return job.journalPath == path; });
	switch (dialog.choice())
	{
	case UnfinishedBusinessDialog::Choice::Resume:
		if (resumable != resumableJobs.cend() && resumeOperation(*resumable))
			return RecoveryOutcome::Started;
		break;
	case UnfinishedBusinessDialog::Choice::Restore:
		if (restorable != restorableJobs.cend())
		{
			OpRequest request;
			request.restoreJournalPath = path;
			if (dispatchRequest(std::move(request)))
			{
				emit logMessage(QtInfoMsg, QStringLiteral("operations"), tr("Restoring interrupted originals."));
				return RecoveryOutcome::Started;
			}
		}
		break;
	case UnfinishedBusinessDialog::Choice::Stop:
		if (resumable != resumableJobs.cend())
		{
			QString error;
			if (!OpJournal::dismiss(path, error))
			{
				emit logMessage(QtWarningMsg, QStringLiteral("operations"), error);
				QMessageBox::warning(m_window, tr("Job could not be stopped"), error);
				refreshHistory();
				break;
			}
			emit logMessage(QtInfoMsg, QStringLiteral("operations"),
							tr("Stopped the unfinished job. Completed results were kept."));
			readOperationHistoryForGate();
			return RecoveryOutcome::Stopped;
		}
		break;
	case UnfinishedBusinessDialog::Choice::Close:
		break;
	}
	return RecoveryOutcome::Closed;
}

bool FileOperationController::resumeOperation(const OperationRecovery::Resumable &job)
{
	OpRequest request;
	request.kind = job.kind;
	request.destRoot = job.dest;
	request.preserve = job.preserve;
	request.items = job.remaining;
	request.resumeJournalPath = job.journalPath;
	if (!dispatchRequest(std::move(request)))
		return false;
	emit logMessage(QtInfoMsg, QStringLiteral("operations"), tr("Resuming the unfinished job."));
	return true;
}
