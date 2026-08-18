#pragma once

#include "backgroundjob.h"
#include "mediafile.h"
#include "opjournal.h"
#include "parkedfile.h"
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <optional>

class JournalOp;

// MARK: - MediaManager

/// Runs Copy, Move and Delete jobs against the user's selection. All
/// three operations share the same shape: kick off on a worker thread
/// via BackgroundJob, emit progress as we go, then a final summary.
///
/// Copy uses an APFS clonefile fast path where source and destination
/// sit on the same APFS volume; bytes don't move, the new inode just
/// shares the same blocks. Anything else falls back to a buffered
/// read/write loop with an optional xxHash3 verify pass (on by default).
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
	/// the per-file dropdown in ManageMediaDialog. A file absent from the map
	/// was never shown as a conflict, so if one turns up on disk anyway it is
	/// skipped, never silently replaced (see resolveConflict).
	enum class ConflictPolicy : int
	{
		KeepBoth = 0,
		Skip = 1,
		Replace = 2
	};

	explicit MediaManager(QObject *parent = nullptr);

	~MediaManager() override = default;

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

	/// Tries the OS trash first; falls back to a per-volume
	/// `_MediaMuster_Trash` folder where it isn't available.
	void executeDelete(QVector<MediaFile> files);

	void cancel() { m_job.cancel(); }

signals:

	// MARK: - Progress signals

	/// pct is 0-100 for the file currently copying. A same-volume Move (pure
	/// rename) and Delete have no per-byte progress and emit 0; a cross-volume
	/// Move runs the copy loop, so it reports real percentages.
	void operationProgress(const QString &fileName, int current, int total, double pct);

	/// `skipped=true` means policy was Skip; success is also true
	/// (no error, just opted out).
	///
	/// filePath enables O(1) row pruning by consumers.
	void operationItemDone(const QString &fileName, const QString &filePath, bool success,
						   const QString &error, bool skipped = false);

	/// Followed by BackgroundJob::finished; callers can wait on either.
	void operationFinished(int succeeded, int failed);

	void operationLog(QtMsgType level, const QString &message);

	/// Fired when a delete falls back to per-volume trash.
	void mediaMusterTrashUsed(const QString &trashFolderPath, int fileCount);

public:
	// MARK: - Path helpers (public: ManageMediaDialog previews with them,
	// and the tests pin them)

	/// `std::nullopt` if all 999 `.Copy.NN` slots are taken; at
	/// that point something is clearly wrong with the folder.
	static std::optional<QString> generateRenamePath(const QString &destPath);

	static QString buildDestPath(const MediaFile &mf, const QString &destRoot, bool preserve);

	// MARK: - Journal plan (public for testability)

	/// The on-disk names for ConflictPolicy. Strings, not enum values, so
	/// a journal written today still means the same thing to a future build.
	static QString conflictPolicyName(ConflictPolicy policy);
	static std::optional<ConflictPolicy> conflictPolicyFromName(const QString &name);

	/// The to-do list a run writes to its journal (OpJournal::writePlan):
	/// only the MediaFile fields the engine reads — filePath, fileName,
	/// mxfFolder, sizeBytes — plus each file's conflict policy, if any.
	static QVector<OpJournal::PlanItem> planItems(const QVector<MediaFile> &files,
												  const QHash<QString, ConflictPolicy> &policies);

	/// The inverse, for resuming an interrupted run: rebuild the rows the
	/// engine needs from the journal's plan. The stubs carry exactly the
	/// fields planItems() saved (nothing else is read by the engine); the
	/// policies come back keyed by source path, as executeCopy/Move expect.
	static QVector<MediaFile> filesFromPlan(const QVector<OpJournal::PlanItem> &items);
	static QHash<QString, ConflictPolicy> policiesFromPlan(const QVector<OpJournal::PlanItem> &items);

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

	void doCopy(const QVector<MediaFile> &files, const QString &dest, bool preserve,
				const QHash<QString, ConflictPolicy> &policies);
	void doMove(const QVector<MediaFile> &files, const QString &dest, bool preserve,
				const QHash<QString, ConflictPolicy> &policies);
	void doDelete(const QVector<MediaFile> &files);

	/// Looks up the policy for mf, acts on it for Skip/KeepBoth, and
	/// (for KeepBoth) mutates dstPath to the renamed `.Copy.NN`
	/// target. Replace falls through to Proceed and lets the caller
	/// overwrite the destination itself.
	///
	/// A conflict with NO policy entry is one the dialog never showed the
	/// user, and it must never fall through to Replace. If `claimed` shows a
	/// file earlier in this run took the path, proceed (claimDestination
	/// redirects to a .Copy.NN sibling); otherwise skip. CI proved the
	/// stakes: on NTFS a case-variant of a selected name sails past every
	/// string comparison, and the old Replace fallback destroyed the first
	/// file's bytes.
	ConflictAction resolveConflict(const MediaFile &mf, QString &dstPath,
								   const QHash<QString, ConflictPolicy> &policies,
								   const QSet<QString> &claimed);

	/// Guards against two source files in the *same* run resolving to the
	/// same destination. The per-file conflict check only sees what's
	/// already on disk, so the second of two identically-named selections
	/// (common when flattening folders that reuse names) would otherwise
	/// silently overwrite the first — data loss on Move. Disambiguates the
	/// later file to a `.Copy.NN` sibling instead, mutating `dstPath`.
	/// Returns false (having emitted the failure item-done) only when all
	/// rename slots are taken. `claimed` holds the normalised paths already
	/// taken this run.
	bool claimDestination(const MediaFile &mf, QString &dstPath, QSet<QString> &claimed);

	/// A finished copy's outcome. Distinguishes a genuine failure from a user
	/// cancel so callers never count (or journal) a cancel as an error.
	enum class CopyOutcome : int
	{
		Succeeded,
		Failed,
		Cancelled
	};

	/// Buffered copy with optional xxHash3 verify. For Failed the item-done
	/// signal has already fired; Cancelled fires nothing (the run is stopping
	/// and the summary reports the cancel).
	///
	/// `park` must already be park()ed over `dst` (the caller does it, so the
	/// park path reaches the journal before any rename). On every failure and
	/// cancel path this restores it; on success it commits.
	///
	/// `syncDestination=true` fsyncs the destination before success is
	/// reported. Move requires it — the source is deleted on success, so the
	/// bytes must be on the drive, not the page cache. Copy passes false:
	/// its source survives, and ~5 ms/file of sync buys nothing there.
	CopyOutcome copyFileWithProgress(const QString &src, const QString &dst, const QString &name,
									 int current, int total, ParkedFile &park,
									 bool syncDestination);

	/// Outcome of reading a file back for the verify pass. `ReadFailed` and a
	/// digest mismatch are different problems — a drive that can't be read and
	/// a copy that came out wrong need different words to the user — so they
	/// don't collapse into one "no value" case.
	struct HashResult
	{
		enum class Status
		{
			Ok,
			ReadFailed,
			Cancelled
		};
		Status status = Status::ReadFailed;
		quint64 digest = 0;
	};

	/// xxHash3 hash for the verify pass.
	HashResult hashFile(const QString &path);

	/// A restore attempt left the original stranded at its park path
	/// (ParkedFile::isStranded()). Settle the op as a dirty fail — which
	/// keeps the journal alive for next-launch recovery to finish the
	/// rollback — and tell the user now, because any earlier item-done
	/// message claimed the destination was left unchanged.
	void flagStrandedPark(JournalOp &jop, const ParkedFile &park, const MediaFile &mf);

	/// The journal reported a failed line write mid-run (OpJournal::
	/// degraded()). Warn once per run: the operation keeps going, but the
	/// user must know crash recovery no longer covers it. `warned` is the
	/// caller's once-latch.
	void warnJournalDegradedOnce(const OpJournal &journal, bool &warned);

	/// 4 MB scratch buffer reused across copyFileWithProgress and
	/// hashFile. Safe to share because doCopy/doMove/doDelete are
	/// serial within the single BackgroundJob worker thread.
	QByteArray m_copyBuffer;

	BackgroundJob m_job{this};
};