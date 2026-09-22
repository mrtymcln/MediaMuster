#pragma once
#include "opcopier.h"
#include "opjournal.h"
#include "optrash.h"
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
	// Headless callers retain originals unless they explicitly support consent.
	virtual bool confirmTrashFallback(const QVector<OpTrashFallbackItem> &) { return false; }
};

class OpRunner
{
public:
	struct Totals
	{
		int succeeded = 0;
		int unchanged = 0;
		int failed = 0;
		int skipped = 0;
		int retained = 0;
		int needsAttention = 0;
		bool cancelled = false;
	};
	// Tests inject failure and storage behaviour through these hooks.
	struct Hooks
	{
		std::function<void(const QString &, const OpJournal::Entry &)> checkpoint;
		std::function<bool(const QString &)> fail;
		// Inject an OS copy error before native transfer; zero runs the real copier.
		std::function<int(const OpJournal::Entry &)> nativeCopyError;
		std::function<OpTrash::Result(const OpJournal::Entry &)> nativeTrash;
		NativeFile::DirectorySync directorySync = NativeFile::syncDirectory;
		bool forceCopy = false;
		bool forceNetworkTrash = false; // Exercise network routing on disposable local fixtures.
	};
	OpRunner(OpSink &sink, const std::atomic<bool> &cancel) : m_sink(sink), m_cancel(cancel) {}
	Totals run(const OpRequest &request, const QString &journalDir = {});
	Hooks hooks;
	std::function<void(const QString &)> onRenameFolderTouched;
	static bool reconcile(OpJournal &journal, OpJournal::Entry &entry, QString &error,
						  const NativeFile::DirectorySync &directorySync = NativeFile::syncDirectory);
	// Only recorded private artifacts are eligible; incomplete cleanup stays journalled.
	static bool cleanup(OpJournal &journal, OpJournal::Entry &entry, QString &error,
						const Hooks *hooks = nullptr);

private:
	bool save(OpJournal &journal, OpJournal::Entry &entry, OpJournal::Step step);
	OpResult execute(OpJournal &journal, OpJournal::Entry &entry, OpKind kind, int index,
					 int total, bool *retryableCopy = nullptr);
	/// Retry only eligible native copy failures, preserving journal and cancel gates.
	OpResult executeWithRetries(OpJournal &journal, OpJournal::Entry &entry,
								OpKind kind, int index, int total, QString &error);
	/// Whole-job barrier shared by Move removal and Undo copy disposal.
	bool copiesReadyForRemoval(const OpJournal &journal, const OpRequest &request,
							   const Totals &totals, QString &error) const;
	OpResult transfer(OpJournal &journal, OpJournal::Entry &entry, OpKind kind, OpFile &source,
					  int index, int total, bool directoryDurable, bool *retryableCopy);
	OpResult removeOriginal(OpJournal &, OpJournal::Entry &, int index, int total);
	OpResult restoreOriginal(OpJournal &, OpJournal::Entry &);
	Totals restoreOriginals(const OpRequest &, const QString &directory);
	OpRequest planUndo(OpJournal::Record &, const OpRequest &);
	bool retireDatabases(OpJournal &journal, const QSet<QString> &folders, QString &error);
	void checkpoint(const QString &name, const OpJournal::Entry &entry);
	bool fail(const QString &name) const
	{
		return hooks.fail && hooks.fail(name);
	}
	OpSink &m_sink;
	const std::atomic<bool> &m_cancel;
};
