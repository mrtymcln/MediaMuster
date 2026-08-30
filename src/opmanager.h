#pragma once

#include "backgroundjob.h"
#include "mediafile.h"
#include "oprequest.h"
#include "oprunner.h"

#include <QHash>
#include <QObject>
#include <QVector>

#include <functional>
#include <optional>

// MARK: - OpManager
//
// The engine's front door — the only part of the file-operations engine
// that is a QObject. MainWindow (and the Rebalance adapter) talk to
// this; everything behind it (OpRunner, OpCopier, OpLedger) is plain
// C++ driven synchronously on one worker thread.
//
// The signal and entry-point contract is MediaManager's, kept on
// purpose so the swap is a re-point, not a rewrite:
//
//   - entry points take the selection BY VALUE and return immediately;
//     the caller raises the progress sheet itself.
//   - operationFinished fires EXACTLY once per run, including cancel —
//     it is what un-busies the whole UI.
//   - operationItemDone carries the SOURCE path (row pruning keys on
//     it), and a skip is success=true + skipped=true, or Move/Delete
//     would prune rows for files still on disk.
//   - cancel is stop-and-keep: landed work stays, the summary says
//     cancelled, and the finished ledger becomes the undo candidate.
/// Privately an OpSink: the runner reports through the interface, and
/// the overrides below simply re-emit as this object's signals (only a
/// member can emit its own protected signals). The overrides run on the
/// worker thread; every connection into the GUI is queued, so that is
/// safe by construction.
class OpManager : public QObject, private OpSink
{
	Q_OBJECT
public:
	explicit OpManager(QObject *parent = nullptr);
	~OpManager() override = default;

	// MARK: - Job entry points

	/// `preserveStructure=true` mirrors Avid's
	/// `Avid MediaFiles/MXF/<n>/<filename>` layout under destRoot;
	/// false flattens everything directly into destRoot.
	void executeCopy(QVector<MediaFile> files, const QString &destRoot,
					 bool preserveStructure = false,
					 const QHash<QString, ConflictPolicy> &conflictPolicies = {});

	void executeMove(QVector<MediaFile> files, const QString &destRoot,
					 bool preserveStructure = false,
					 const QHash<QString, ConflictPolicy> &conflictPolicies = {});

	/// Never a hard delete: OS trash preferred, the per-volume
	/// `_MediaMuster_Trash` where it can't be used (see TrashRouter).
	void executeDelete(QVector<MediaFile> files);

	/// Run a fully built request — the resume flow dispatches the
	/// ledger's own plan items through here (no reconstituted
	/// MediaFiles), and the Rebalance adapter dispatches Rename
	/// requests.
	void execute(OpRequest request);

	/// Edit ▸ Undo: reverse the completed run recorded at `ledgerPath`
	/// (found via OpLedger::latestUndoable). Fire-and-forget like every
	/// other entry point; progress/itemDone/operationFinished flow
	/// through the same signals, and the undo writes its own ledger so
	/// a crash mid-undo is recovered at next launch.
	void executeUndo(const QString &ledgerPath);

	void cancel() { m_job.cancel(); }

	// MARK: - Path helpers (public: ManageMediaDialog previews with
	// them, and the tests pin them)

	static QString buildDestPath(const MediaFile &mf, const QString &destRoot, bool preserve);
	static std::optional<QString> generateRenamePath(const QString &destPath);

	/// The engine's entire read of a MediaFile, in one place: path,
	/// name, folder, size, the per-file conflict policy, and the scan's
	/// Avid identity claims (mob ids + clip name) that the runner
	/// cross-checks and the ledger records.
	static QVector<OpItem> itemsFromMediaFiles(const QVector<MediaFile> &files,
											   const QHash<QString, ConflictPolicy> &policies);

	/// Rename runs only: called on the WORKER thread after the first
	/// successful rename touching each folder (the engine's own Avid-
	/// database reset for that folder has already run). The Rebalance
	/// adapter wires its summary counting here; unset means no extra
	/// action. Set before dispatching; not thread-safe to change mid-run.
	std::function<void(const QString &folderPath)> renameFolderTouched;

signals:

	// MARK: - Progress signals (consumed via QueuedConnection)

	/// pct is 0-100 for the file currently copying. A same-volume Move
	/// (pure rename) and Delete have no per-byte progress and emit 0.
	void operationProgress(const QString &fileName, int current, int total, double pct);

	/// `skipped=true` means policy was Skip; success is also true (no
	/// error, just opted out). filePath enables O(1) row pruning by
	/// consumers.
	void operationItemDone(const QString &fileName, const QString &filePath, bool success,
						   const QString &error, bool skipped = false);

	/// Exactly once per run, including cancel.
	void operationFinished(int succeeded, int failed);

	void operationLog(QtMsgType level, const QString &message);

	/// Files landed in a per-volume MediaMuster Trash this run (deletes
	/// on volumes without a usable OS trash, and replaced originals).
	void mediaMusterTrashUsed(const QString &trashFolderPath, int fileCount);

private:
	// MARK: - OpSink (the runner's reporting channel)

	void progress(const QString &name, int current, int total, double pct) override;
	void itemDone(const QString &name, const QString &path, bool ok, const QString &error,
				  bool skipped) override;
	void log(QtMsgType level, const QString &message) override;
	void trashUsed(const QString &folder, int count) override;

	void startRun(OpRequest request);

	/// Must stay the LAST member: BackgroundJob's destructor joins the
	/// worker, and members declared after it would be destroyed first —
	/// out from under a still-running worker.
	BackgroundJob m_job{this};
};
