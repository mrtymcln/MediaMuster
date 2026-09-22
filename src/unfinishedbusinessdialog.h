#pragma once

#include "operationrecovery.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QPlainTextEdit;

/// Presents a snapshot of recoverable work. Selecting a job or closing the
/// dialog has no effect on disk; the controller performs the chosen action.
class UnfinishedBusinessDialog : public QDialog
{
public:
	enum class Choice
	{
		Close,
		Resume,
		Restore,
		Stop
	};

	UnfinishedBusinessDialog(const QVector<OperationRecovery::Resumable> &resumable,
							 const QVector<OperationRecovery::Restorable> &restorable,
							 const QString &preferredJournalPath, QWidget *parent);

	Choice choice() const noexcept { return m_choice; }
	QString selectedJournalPath() const;

private:
	struct Job
	{
		QString journalPath;
		QString label;
		QString summary;
		QString restoreLocations;
		bool canResume = false;
	};

	static QVector<Job> mergeJobs(const QVector<OperationRecovery::Resumable> &resumable,
								  const QVector<OperationRecovery::Restorable> &restorable);
	void showJob(int index);
	void choose(Choice choice);

	QVector<Job> m_jobs;
	int m_selectedJob = -1;
	Choice m_choice = Choice::Close;

	// Non-owning observers; the QObject parent tree owns every control.
	QLabel *m_summary = nullptr;
	QWidget *m_resumeRow = nullptr;
	QWidget *m_restoreRow = nullptr;
	QWidget *m_stopRow = nullptr;
	QLabel *m_stopHelp = nullptr;
	QPlainTextEdit *m_restorePaths = nullptr;
};
