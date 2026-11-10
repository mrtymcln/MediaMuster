#pragma once
#include "operation_journal.h"

// MARK: - Operation recovery

class OperationRecovery
{
public:
	struct Restorable
	{
		QString journalPath;
		QStringList originals;
		QStringList retainedPaths;
	};
	struct Resumable
	{
		QString journalPath;
		OperationKind kind = OperationKind::Copy;
		QString dest;
		QString started;
		int total = 0;
		int finished = 0;
	};
	struct Summary
	{
		int opsFlagged = 0;
		QStringList notes;
		QVector<Resumable> resumable;
		QVector<Restorable> restorable;
		std::optional<OperationJournal::Record> undoCandidate;
		bool hadTrouble() const
		{
			return opsFlagged > 0;
		}
		QString message() const
		{
			return notes.join('\n');
		}
	};
	// Prune and recover under one journal lock, sharing the loaded history.
	// MARK: - Recovery and history queries

	static Summary run(const QString &directory = {}, const QVector<VolumeIdentity> &mounted = {});
	// Read journal history once without recovery, pruning or media access.
	static Summary readSummary(const QString &directory = {});
	static std::optional<Resumable> resumableFrom(const OperationJournal::Record &record);

private:
	static QVector<Restorable> restorableFrom(const QVector<OperationJournal::Record> &records);
};
