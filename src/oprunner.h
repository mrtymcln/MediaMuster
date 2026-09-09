#pragma once
#include "opcopier.h"
#include "opjournal.h"
#include <QSet>
#include <functional>

class OpSink
{
  public:
	virtual ~OpSink() = default;
	virtual void progress(const QString &, int, int, double) = 0;
	virtual void log(QtMsgType, const QString &) = 0;
	virtual void trashUsed(const QString &, int) = 0;
	virtual void result(const OpResult &) = 0;
};

class OpRunner
{
  public:
	struct Totals
	{
		int succeeded = 0;
		int failed = 0;
		int skipped = 0;
		int retained = 0;
		int needsAttention = 0;
		bool cancelled = false;
	};
	// Passed explicitly by tests/Debug diagnostics. Production uses no hooks.
	struct Hooks
	{
		std::function<void(const QString &, const OpJournal::Entry &)> checkpoint;
		std::function<bool(const QString &)> fail;
		bool forceCopy = false;
	};
	OpRunner(OpSink &sink, const std::atomic<bool> &cancel) : m_sink(sink), m_cancel(cancel) {}
	Totals run(const OpRequest &request, const QString &journalDir = {});
	Hooks hooks;
	std::function<void(const QString &)> onRenameFolderTouched;
	static QString buildDestPath(const QString &, const QString &, const QString &, bool,
								 bool omfEra = false);
	static std::optional<QString> generateRenamePath(const QString &);
	static bool sameVolumeForRename(const QString &, const QString &);
	static bool reconcile(OpJournal &journal, OpJournal::Entry &entry, QString &error);

  private:
	bool save(OpJournal &journal, OpJournal::Entry &entry, OpJournal::Step step);
	OpResult execute(OpJournal &journal, OpJournal::Entry &entry, OpKind kind, int index,
					 int total);
	OpResult transfer(OpJournal &journal, OpJournal::Entry &entry, OpKind kind, OpFile &source,
					  int index, int total);
	bool retireDatabases(OpJournal &journal, const QSet<QString> &folders, QString &error);
	void checkpoint(const QString &name, const OpJournal::Entry &entry);
	bool fail(const QString &name) const
	{
		return hooks.fail && hooks.fail(name);
	}
	OpSink &m_sink;
	const std::atomic<bool> &m_cancel;
};
