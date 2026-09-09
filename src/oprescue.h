#pragma once
#include "opjournal.h"

class OpRescue
{
  public:
	struct Resumable
	{
		QString journalPath;
		OpKind kind = OpKind::Copy;
		QString dest;
		bool preserve = false;
		QString started;
		int total = 0;
		int finished = 0;
		bool usedMediaMusterTrash = false;
		QVector<OpItem> remaining;
	};
	struct Summary
	{
		int journalsRecovered = 0;
		int opsReversed = 0;
		int opsFlagged = 0;
		QStringList notes;
		QVector<Resumable> resumable;
		bool anything() const
		{
			return !notes.isEmpty() || !resumable.isEmpty();
		}
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
