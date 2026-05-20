#pragma once

#include "backgroundjob.h"
#include "mediafile.h"
#include <QHash>
#include <QObject>
#include <optional>

// MARK: - MediaManager

/// Runs Copy, Move and Delete jobs against the user's selection. All
/// three operations share the same shape: kick off on a worker thread
/// via BackgroundJob, emit progress as we go, then a final summary.
///
/// Copy uses an APFS clonefile fast path where source and destination
/// sit on the same APFS volume — bytes don't move, the new inode just
/// shares the same blocks. Anything else falls back to a buffered
/// read/write loop with a xxHash3 verify pass.
///
/// Move tries a same-volume QFile::rename first; cross-volume falls
/// back to copy-then-delete with a rollback if the source delete fails.
///
/// Delete uses the OS trash where available and the
/// `_MediaMuster_Trash` safety where it isn't.
class MediaManager : public QObject
{
	Q_OBJECT
public:
	/// What to do when a destination file already exists. Driven from
	/// the per-file dropdown in ManageMediaDialog; files not in the
	/// map fall through to Replace.
	enum class ConflictPolicy : int
	{
		KeepBoth = 0,
		Skip = 1,
		Replace = 2
	};

	/// Unary + on a strongly-typed enum — lets us write
	/// `policies.value(path, +ConflictPolicy::Replace)` without a
	/// static_cast<int> at every call site.
	friend constexpr int operator+(ConflictPolicy p) noexcept
	{
		return static_cast<int>(p);
	}

	explicit MediaManager(QObject *parent = nullptr);

	~MediaManager() override = default;

	// MARK: - Job entry points

	/// `preserveStructure=true` mirrors Avid's
	/// `Avid MediaFiles/MXF/<n>/<filename>` layout under destRoot;
	/// false flattens everything directly into destRoot.
	void executeCopy(const QVector<MediaFile> &files, const QString &destRoot,
	                 bool preserveStructure = true,
	                 const QHash<QString, ConflictPolicy> &conflictPolicies = {});

	void executeMove(const QVector<MediaFile> &files, const QString &destRoot,
	                 bool preserveStructure = true,
	                 const QHash<QString, ConflictPolicy> &conflictPolicies = {});

	/// Tries the OS trash first; falls back to a per-volume
	/// `_MediaMuster_Trash` folder where it isn't available.
	void executeDelete(const QVector<MediaFile> &files);

	void cancel()
	{
		m_job.cancel();
	}

signals:

	// MARK: - Progress signals

	/// pct is 0-100 for the file currently copying; for Move/Delete
	/// it's a coarse 0 or 100.
	void operationProgress(const QString &fileName, int current, int total, double pct);

	/// `skipped=true` means policy was Skip; success is also true
	/// (no error, just opted out).
	///
	/// filePath enables O(1) row pruning by consumers.
	void operationItemDone(const QString &fileName, const QString &filePath,
	                       bool success, const QString &error,
	                       bool skipped = false);

	/// Followed by BackgroundJob::finished — callers can wait on either.
	void operationFinished(int succeeded, int failed);

	/// `level`: 0=info, 1=warn, 3=success.
	void operationLog(int level, const QString &message);

	/// Fired when a delete falls back to per-volume trash.
	void mediaMusterTrashUsed(const QString &trashFolderPath, int fileCount);

public:
	// MARK: - Path helpers (public for testability)

	/// Returns empty string if all 999 `.Copy.NN` slots are taken —
	/// at that point something is clearly wrong with the folder.
	static QString generateRenamePath(const QString &destPath);

	static QString buildDestPath(const MediaFile &mf, const QString &destRoot,
	                             bool preserve);

private:
	// MARK: - Internal state machine

	/// Tells the caller whether to carry on with the copy/move,
	/// count it as a skip, or treat it as a hard failure. The
	/// helper has already emitted operationItemDone in the Skip and
	/// Fail cases.
	enum class ConflictAction : int
	{
		Proceed,
		Skip,
		Fail
	};

	void doCopy(const QVector<MediaFile> &files, const QString &dest,
	            bool preserve, const QHash<QString, ConflictPolicy> &policies);
	void doMove(const QVector<MediaFile> &files, const QString &dest,
	            bool preserve, const QHash<QString, ConflictPolicy> &policies);
	void doDelete(const QVector<MediaFile> &files);

	/// Looks up the policy for mf, acts on it for Skip/KeepBoth, and
	/// (for KeepBoth) mutates dstPath to the renamed `.Copy.NN`
	/// target. Replace falls through to Proceed and lets the caller
	/// overwrite the destination itself.
	ConflictAction resolveConflict(const MediaFile &mf, QString &dstPath,
	                               const QHash<QString, ConflictPolicy> &policies);

	/// Buffered copy with optional xxHash3 verify. Returns false on
	/// any error or on cancel; the item-done signal has already
	/// fired in the failure paths.
	bool copyFileWithProgress(const QString &src, const QString &dst, const QString &name,
	                          int current, int total);

	/// xxHash3 hash for the verify pass. nullopt on open error or
	/// cancel — caller treats either as a verify failure and removes
	/// the destination.
	std::optional<quint64> hashFile(const QString &path);

	BackgroundJob m_job{this};
};