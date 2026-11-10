#pragma once
#include "operation_copier.h"
#include "operation_journal.h"
#include "operation_trash.h"
#include <QSet>
#include <functional>
#include <memory>
#include <optional>

// MARK: - Operation reporting

class OperationSink
{
public:
	virtual ~OperationSink() = default;
	virtual void progress(const QString &, int, int, double) = 0;
	virtual void log(QtMsgType, const QString &) = 0;
	virtual void trashUsed(const QString &, int) = 0;
	virtual void result(const OperationResult &) = 0;
	// Headless callers retain originals unless they explicitly support consent.
	virtual bool confirmTrashFallback(const QVector<OperationTrashFallbackItem> &) { return false; }
};

// MARK: - Operation execution

class OperationRunner
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
		std::function<void(const QString &, const OperationJournal::Entry &)> checkpoint;
		std::function<bool(const QString &)> fail;
		// Inject an OS copy error before native transfer; zero runs the real copier.
		std::function<int(const OperationJournal::Entry &)> nativeCopyError;
		std::function<OperationTrash::Result(const OperationJournal::Entry &)> nativeTrash;
		FileSync::DirectorySync directorySync = FileSync::syncDirectory;
		bool forceCopy = false;
		bool forceNetworkTrash = false; // Exercise network routing on disposable local fixtures.
	};
	// MARK: - Run control and recovery

	OperationRunner(OperationSink &sink, const std::atomic<bool> &cancel) : m_sink(sink), m_cancel(cancel) {}
	Totals run(const OperationRequest &request, const QString &journalDir = {});
	Hooks hooks;
	static bool reconcile(OperationJournal &journal, OperationJournal::Entry &entry, QString &error,
						  const FileSync::DirectorySync &directorySync = FileSync::syncDirectory);
	// Only recorded private artifacts are eligible; incomplete cleanup stays journalled.
	static bool cleanup(OperationJournal &journal, OperationJournal::Entry &entry, QString &error,
						const Hooks *hooks = nullptr);

// MARK: - Execution helpers

private:
	struct RunState;
	void resumeRunJournal(OperationRequest &request, OperationJournal &journal, RunState &state,
						  const QString &lockDirectory);
	void createRunJournal(const OperationRequest &input, OperationRequest &request, OperationJournal &journal,
						  RunState &state, const QString &lockDirectory);
	int prepareRebalanceGroup(OperationJournal &journal, RunState &state, int index);
	void executePendingItems(OperationJournal &journal, const OperationRequest &request, RunState &state);
	bool removeCopiedOriginals(OperationJournal &journal, const OperationRequest &request, RunState &state);
	static bool canDiscardUndoCopy(const OperationJournal &journal, const OperationJournal::Entry &entry,
								   const std::optional<OperationJournal::Record> &undoOriginal);
	void discardUndoCopies(OperationJournal &journal, const OperationRequest &request, RunState &state, bool ready);
	void confirmAndRunTrashFallbacks(OperationJournal &journal, const OperationRequest &request, RunState &state);
	void finishRun(OperationJournal &journal, RunState &state);
	bool save(OperationJournal &journal, OperationJournal::Entry &entry, OperationJournal::Step step);
	OperationResult restoreSystemTrash(OperationJournal &journal, OperationJournal::Entry &entry);
	// Takes ownership so the protected handle closes before the native Trash call.
	OperationResult moveToSystemTrash(OperationJournal &journal, OperationJournal::Entry &entry,
							   std::unique_ptr<OperationFile> source, QString error);
	// No terminal result leaves the unavailable-relocation decision to execute().
	std::optional<OperationResult> attemptRelocation(OperationJournal &journal, OperationJournal::Entry &entry,
											  OperationKind kind, OperationFile &source, const QString &originalDestination, QString &error);
	OperationResult execute(OperationJournal &journal, OperationJournal::Entry &entry, OperationKind kind, int index,
					 int total, bool *retryableCopy = nullptr);
	/// Retry only eligible native copy failures, preserving journal and cancel gates.
	OperationResult executeWithRetries(OperationJournal &journal, OperationJournal::Entry &entry,
								OperationKind kind, int index, int total, QString &error);
	/// Whole-job barrier shared by Move removal and Undo copy disposal.
	bool copiesReadyForRemoval(const OperationJournal &journal, const OperationRequest &request,
							   const Totals &totals, QString &error) const;
	OperationResult transfer(OperationJournal &journal, OperationJournal::Entry &entry, OperationKind kind, OperationFile &source,
					  int index, int total, bool directoryDurable, bool *retryableCopy);
	OperationResult removeOriginal(OperationJournal &, OperationJournal::Entry &, int index, int total);
	OperationResult restoreOriginal(OperationJournal &, OperationJournal::Entry &);
	Totals restoreOriginals(const OperationRequest &);
	OperationRequest planUndo(OperationJournal::Record &, const OperationRequest &);
	bool retireDatabases(OperationJournal &journal, const QSet<QString> &folders, QString &error);
	void checkpoint(const QString &name, const OperationJournal::Entry &entry);
	bool fail(const QString &name) const
	{
		return hooks.fail && hooks.fail(name);
	}
	// MARK: - Run dependencies

	OperationSink &m_sink;
	const std::atomic<bool> &m_cancel;
};
