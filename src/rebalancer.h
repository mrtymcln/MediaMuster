#pragma once

#include "avidlimits.h"
#include "backgroundjob.h"
#include "mediafile.h"
#include "rebalanceplan.h"

#include <QObject>
#include <QString>
#include <QVector>
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
///   2. executeAsync: runs the approved plan on a worker thread.
///      Renames files into their target folders, deletes stale
///      per-folder .mdb / .pmr so Avid rebuilds them, and bookends
///      the run with a pre-flight probe rename to abort cleanly if a
///      donor folder is locked by an open Avid.
///
/// Relatives stay together. Bucket by masterMobId
/// and treat each bucket as atomic; cancel fires between buckets,
/// never mid-bucket.
class Rebalancer : public QObject
{
	Q_OBJECT
public:
	/// Pack folders up to (not past) this; one file below Avid's hard
	/// ceiling. Sourced from AvidLimits so the cap and the ceiling can't
	/// drift apart.
	static constexpr int kFolderCap = AvidLimits::kFolderRecommend;

	explicit Rebalancer(QObject *parent = nullptr);

	~Rebalancer() override = default;

	// MARK: - Planning

	static RebalancePlan computePlan(const QString &mxfRoot, const QString &volumeLabel,
									 const QVector<MediaFile> &files);

	static std::optional<FolderId> parseFolderName(const QString &name);

	/// The source folder a MoveOp came from, recomputed from its srcPath
	/// (MoveOp doesn't store the FolderId). nullopt when the parent dir
	/// isn't a conforming Avid folder name.
	static std::optional<FolderId> srcFolderOf(const QString &srcPath);

	// MARK: - Execution

	/// Only one execute is in flight per instance; a second call
	/// cancels and joins the previous worker before starting the new one.
	void executeAsync(const RebalancePlan &plan);

	/// Checked at relatives-group boundaries so relatives stay
	/// together; never leave half a master clip's essence split
	/// across folders.
	void cancel() { m_job.cancel(); }

signals:

	// MARK: - Progress signals

	void progress(int current, int total, const QString &detail);
	void log(QtMsgType level, const QString &message);
	void finished(int succeeded, int failed, bool cancelled);

	/// No moves performed and no `finished` will follow; the dialog
	/// treats this as a terminal state on its own.
	void aborted(const QString &reason);

private:
	void doExecute(const RebalancePlan &plan);

	BackgroundJob m_job{this};
};