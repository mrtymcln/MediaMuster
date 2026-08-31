#pragma once

#include "fileidentity.h"
#include "opcopier.h"
#include "opjournal.h"
#include "oprequest.h"

#include <QSet>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <functional>
#include <optional>

// MARK: - OpSink
//
// How the runner talks to the world. The QObject facade (OpManager)
// implements this by emitting the app's signals; tests implement it
// with plain structs. The runner itself is deliberately not a QObject —
// no signals, no event loop, no thread affinity — so the whole safety
// machine can be driven synchronously in a test and, later, run N-up in
// a worker pool without redesign.
class OpSink
{
public:
	virtual ~OpSink() = default;

	/// One file's progress: `current`/`total` are file counts, `pct` is
	/// 0–100 within the current file (0 where there is no byte progress
	/// to report — renames, deletes).
	virtual void progress(const QString &name, int current, int total, double pct) = 0;

	/// One file concluded. `skipped=true` rides with ok=true (no error,
	/// just opted out). `path` is the SOURCE path — consumers key row
	/// pruning on it.
	virtual void itemDone(const QString &name, const QString &path, bool ok,
						  const QString &error, bool skipped) = 0;

	virtual void log(QtMsgType level, const QString &message) = 0;

	/// A run routed files into the per-volume MediaMuster Trash (deletes
	/// on volumes without a usable OS trash, and replaced originals).
	virtual void trashUsed(const QString &trashFolderPath, int fileCount) = 0;
};

// MARK: - Folder database reset

/// Delete `folderPath`'s Avid databases (msmMMOB.mdb / msmFMID.pmr) so
/// Media Composer rebuilds them at next launch; failures are warned
/// through `sink`. The one implementation of the reset both the forward
/// Rename machine and its undo perform — see the ordering rationale at
/// the definition.
void resetAvidDatabases(const QString &folderPath, OpSink &sink);

// MARK: - OpRunner
//
// The state machines: one run() call executes one OpRequest start to
// finish on the calling thread, writing every step ahead into an
// OpJournal and reporting through the OpSink. The safety order inside
// every machine is the same four beats, and their sequence is the whole
// point:
//
//   1. IDENTIFY  — capture the file's identity and check it against
//                  what the scan claimed. Wrong file = refuse, explain,
//                  move on. We never operate on a guess.
//   2. WRITE IT  — the journal line describing the step, fsync'd, BEFORE
//      DOWN        the step. A crash at any instant leaves a record
//                  that recovery can act on.
//   3. ACT       — park aside, copy/rename/trash, verify, apply the
//                  durability barrier.
//   4. RE-CHECK  — verify the source again after the bytes moved (a
//                  same-size swap during a long copy is real), capture
//                  the landed file's identity for the journal, and only
//                  then dispose of anything (replaced originals go to
//                  the trash, never a hard delete; a Move's source is
//                  removed only after its copy is verified AND durable).
//
// Copy and Move share ONE loop here (v1 kept two mirrored copies of it
// with a "change both together" comment — that class of bug ends now);
// the differences (park tag, rename-first, durability level, source
// disposal) are explicit branches inside the shared sequence.
class OpRunner
{
public:
	/// `cancel` is polled at file boundaries and inside the byte loops.
	/// Cancel means STOP AND KEEP: work already landed stays landed, the
	/// journal closes clean (and remains the undo candidate).
	OpRunner(OpSink &sink, const std::atomic<bool> &cancel);

	struct Totals
	{
		int succeeded = 0;
		int failed = 0;
		int skipped = 0;
	};

	/// Execute one request. `journalDir` overrides the journal location
	/// (tests); empty means the standard journal dir.
	Totals run(const OpRequest &request, const QString &journalDir = QString());

	// MARK: - Path helpers (public: dialog previews and tests pin them)

	/// Where an item lands under destRoot. preserve=true mirrors Avid's
	/// `Avid MediaFiles/MXF/<n>/<filename>` layout; false flattens.
	static QString buildDestPath(const QString &fileName, const QString &mxfFolder,
								 const QString &destRoot, bool preserve);

	/// First free `name (2)`-style sibling of destPath (Windows/Chrome
	/// convention — see the naming catalogue above the definition);
	/// `std::nullopt` if all
	/// 999 slots are taken (at that point something is clearly wrong
	/// with the folder).
	static std::optional<QString> generateRenamePath(const QString &destPath);

	/// Provably the same volume, gating every rename fast path (Move,
	/// Undo-of-move). NOT an optimisation: Qt's QFile::rename silently
	/// falls back to an unverified copy-then-DELETE across volumes,
	/// which would bypass the checksum, the durability barrier and the
	/// trash rule in one line (review finding 5). "Don't know" fails
	/// safe into the verified copy leg.
	static bool sameVolumeForRename(const QString &src, const QString &dstPath);

	/// The fingerprint of every distinct volume a request touches —
	/// journaled in the plan so recovery and undo can re-find a drive
	/// that came back under a different name, and refuse an impostor.
	static QVector<VolumeIdentity> volumesFor(const OpRequest &request);

	// MARK: - Rename hook

	/// Called once per folder whose contents a Rename run changed, after
	/// that folder's first successful rename — AFTER the engine's own
	/// Avid-database reset for that folder (which is built in, so undo
	/// of a rename run resets them too). Rebalance wires this for its
	/// summary counting; unset means no extra action.
	std::function<void(const QString &folderPath)> onRenameFolderTouched;

private:
	enum class ConflictAction
	{
		Proceed,
		Skip,
		Fail
	};

	Totals runCopyMove(const OpRequest &req, const QString &journalDir);
	Totals runDelete(const OpRequest &req, const QString &journalDir);
	Totals runRename(const OpRequest &req, const QString &journalDir);

	ConflictAction resolveConflict(const OpItem &it, QString &dstPath,
								   const QSet<QString> &claimed);
	bool claimDestination(const OpItem &it, QString &dstPath, QSet<QString> &claimed);

	/// Beat 1: capture the file's real identity and cross-check it
	/// against the scan's claims (size, Avid mob ids). nullopt = refused
	/// (the item-done message has already been emitted, naming the clip
	/// and the first difference).
	std::optional<FileIdentity> captureAndCheckSource(const OpItem &it);

	void warnJournalDegradedOnce(const OpJournal &journal, bool &warned);
	/// Writes the dirty journal line for a park that could not be restored,
	/// tells the user, and DISARMS the park — the journal line must be the
	/// last word on this item's disk state, so no destructor retry may
	/// change it afterwards.
	void flagStrandedPark(JournalOpGuard &lop, class ParkedFile &park, const OpItem &it);

	/// The display name for messages: the clip name the editor knows
	/// when the scan recorded one, else the file name.
	static QString displayName(const OpItem &it);

	OpSink &m_sink;
	const std::atomic<bool> &m_cancel;
	OpCopier m_copier;
};
