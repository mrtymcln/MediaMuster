#pragma once

#include "conventions.h"
#include "backgroundjob.h"
#include "mediafile.h"
#include "opmanager.h"
#include "rebalanceplan.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <optional>

// MARK: - Rebalancer

/// Plans and executes redistribution of MXF files between
/// `Avid MediaFiles/MXF/<n>` folders. Avid recommends keeping each
/// folder under 5000 files; past that Media Composer slows down.
///
/// Split in two halves:
///
///   1. computePlan: synchronous pure function over indexed files
///      plus current folder state on disk. Produces a RebalancePlan
///      describing every move, every new folder, every warning.
///      Nothing touched on disk yet.
///
///   2. executeAsync: runs the approved plan through the file-
///      operations engine. It opens with the scratch-file rename check
///      per donor folder (on its own short-lived worker), so a folder
///      that is gone or read-only aborts the run before anything moves;
///      then the plan becomes an OpRequest of Rename items and the
///      engine does the rest — which is the v2 upgrade: every rename is
///      now WRITE-AHEAD JOURNALED, identity-checked, recoverable after a
///      crash from the next launch's sweep, and undoable from Edit ▸
///      Undo. (v1 ran bare QFile::rename with none of that — the one
///      feature outside the safety net.)
///
///      Each folder's stale .mdb / .pmr is still deleted the moment its
///      contents change, via the engine's folder-touched hook, so Avid
///      rebuilds them.
///
/// Relatives stay together. Bucket by masterMobId, order the request
/// group-contiguously, and the engine's Rename machine only honours
/// cancel at group boundaries — never mid-bucket.
///
/// This adapter owns a PRIVATE OpManager rather than sharing
/// MainWindow's: the main window's signal handlers (row pruning, busy
/// state, the modal progress sheet) are wired to ITS engine instance
/// and must not fire for a rebalance, whose dialog has its own progress
/// UI. The modal dialogs remain what prevents two operations running at
/// once, exactly as before.
class Rebalancer : public QObject
{
	Q_OBJECT
public:
	explicit Rebalancer(QObject *parent = nullptr);

	~Rebalancer() override;

	// MARK: - Planning

	static RebalancePlan computePlan(const QString &mxfRoot, const QString &volumeLabel,
									 const QVector<MediaFile> &files);

	static std::optional<FolderName> parseFolderName(const QString &name);

	/// The source folder a RenameOp came from, recomputed from its srcPath
	/// (RenameOp doesn't store the FolderName). nullopt when the parent dir
	/// isn't a conforming Avid folder name.
	static std::optional<FolderName> srcFolderOf(const QString &srcPath);

	// MARK: - Execution

	/// Only one execute is in flight per instance; a second call
	/// cancels and joins the previous pre-flight before starting anew.
	void executeAsync(const RebalancePlan &plan);

	/// Checked at relatives-group boundaries so relatives stay
	/// together; never leave half a master clip's essence split
	/// across folders.
	void cancel()
	{
		m_cancelRequested.store(true, std::memory_order_release);
		m_preflight.cancel();
		m_engine->cancel();
	}

signals:

	// MARK: - Progress signals

	void progress(int current, int total, const QString &detail);
	void log(QtMsgType level, const QString &message);
	void finished(int succeeded, int failed, bool cancelled);

	/// No moves performed and no `finished` will follow; the dialog
	/// treats this as a terminal state on its own.
	void aborted(const QString &reason);

private:
	/// GUI-thread tail of executeAsync: the pre-flight worker hops back
	/// here (queued) to hand the built request to the engine.
	void startEngineRun(OpRequest request);

	/// The private engine instance (see the class comment for why it is
	/// not MainWindow's). Signal adaptation happens once, in the ctor.
	OpManager *m_engine = nullptr;

	/// Folders whose Avid databases this run has reset, for the summary
	/// line. Written from the engine's worker thread via the
	/// folder-touched hook; read on the GUI thread after finished.
	std::atomic<int> m_foldersReset{0};

	std::atomic<bool> m_cancelRequested{false};

	/// Pre-flight only: the scratch-file donor checks and the request
	/// build run here so a dead network mount can't freeze the GUI. The
	/// renames themselves run on the engine's own worker.
	BackgroundJob m_preflight{this};
};
