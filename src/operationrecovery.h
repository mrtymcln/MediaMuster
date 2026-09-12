#pragma once
#include "opjournal.h"

class OperationRecovery
{
  public:
	struct Resumable
	{
		QString journalPath;
		OpKind kind = OpKind::Copy;
		QString dest;
		bool preserve = false;
		bool verifyCopies = false;
		bool copiesComplete = false;
		QString started;
		int total = 0;
		int finished = 0;
		QVector<OpItem> remaining;
	};
	struct Summary
	{
		int opsFlagged = 0;
		QStringList notes;
		QVector<Resumable> resumable;
		std::optional<OpJournal::Record> undoCandidate;
		bool hadTrouble() const
		{
			return opsFlagged > 0;
		}
		QString message() const
		{
			return notes.join('\n');
		}
	};
	static Summary run(const QString &directory = {}, const QVector<VolumeIdentity> &mounted = {});
	static QVector<Resumable> pending(const QString &directory = {});
	static std::optional<Resumable> resumableFrom(const OpJournal::Record &record);
};
