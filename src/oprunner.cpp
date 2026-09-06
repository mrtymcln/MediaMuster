#include "oprunner.h"

#include "conventions.h"
#include "testpause.h"
#include "formatutil.h"
#include "mobid.h"
#include "parkedfile.h"
#include "pathkey.h"
#include "progressthrottle.h"
#include "trashrouter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStorageInfo>
#include <QUuid>

namespace
{
	// Progress emit cap for the byte loop: ~30 Hz via ProgressThrottle
	// plus a 32 MB byte threshold so large single-file copies still tick
	// visibly.
	constexpr qint64 kProgressIntervalBytes = 32 * 1024 * 1024;

	// Suffixes for ParkedFile. Distinct per operation so a stray temp
	// names the job that left it behind. Defined in Conventions: the
	// scanner must recognise these same names as temp-renamed media so a
	// stranded park never becomes invisible in the table.
	inline constexpr QLatin1String kCopyReplaceTag = Conventions::kCopyReplaceTag;
	inline constexpr QLatin1String kMoveReplaceTag = Conventions::kMoveReplaceTag;

	// Skipped count is suppressed when 0 so Delete (no skip path)
	// doesn't trail a `, 0 skipped`.
	QString formatOperationSummary(const QString &verb, int succeeded, int failed, int skipped = 0)
	{
		QString s = QStringLiteral("%1 complete: %2 succeeded, %3 failed")
						.arg(verb)
						.arg(succeeded)
						.arg(failed);
		if (skipped > 0)
			s += QStringLiteral(", %1 skipped").arg(skipped);
		return s;
	}

	// Move a parked original into the trash and disarm the park.
	//
	// True: *parkedFinal names where it went (empty when nothing was
	// parked) and the park is disarmed. False: the parked file is STILL
	// AT park.path() and the park is still armed — the caller chooses
	// between rolling the whole item back (park.restore()) and flagging
	// a stranded park. Note the disarm mechanics: the file was MOVED,
	// not deleted, so ParkedFile::commit()'s remove is a harmless no-op
	// and only the disarm matters.
	bool trashParkedOriginal(TrashRouter &router, ParkedFile &park, QString *parkedFinal)
	{
		parkedFinal->clear();
		if (park.path().isEmpty() || !QFile::exists(park.path()))
		{
			park.commit();
			return true;
		}
		const TrashRouter::Landing landing = router.trash(park.path());
		if (!landing.ok)
			return false;
		*parkedFinal = landing.finalPath;
		park.commit();
		return true;
	}

} // namespace

// MARK: - Folder database reset

// Avid's per-folder databases (msmMMOB.mdb / msmFMID.pmr) go stale the
// instant a file moves in or out; Avid rebuilds them on next launch.
// Callers delete them the moment a folder's contents change — NOT at
// the end of the run. The ordering is load-bearing: a crash partway
// through leaves a legal folder layout, but databases still sitting
// there intact would parse cleanly and simply not mention the clips
// that already moved — the scanner reads that as "No reference", the
// state a user culls from. Absent databases read as "No database", an
// honest unknown that invites a rescan instead of a delete.
void resetAvidDatabases(const QString &folderPath, OpSink &sink)
{
	for (const char *db : {"/msmMMOB.mdb", "/msmFMID.pmr"})
	{
		QFile f(folderPath + QLatin1String(db));
		if (f.exists() && !f.remove())
			sink.log(QtWarningMsg, QStringLiteral("Couldn't delete %1").arg(f.fileName()));
	}
}

// MARK: - Construction

OpRunner::OpRunner(OpSink &sink, const std::atomic<bool> &cancel)
	: m_sink(sink), m_cancel(cancel)
{
}

// MARK: - Path helpers

QString OpRunner::buildDestPath(const QString &fileName, const QString &mxfFolder,
								const QString &destRoot, bool preserve, bool omfEra)
{
	// OMF-era: a legacy row's "folder" is a flat root — Avid's own "OMFI
	// MediaFiles", or whatever an archive folder added by hand is called —
	// so a preserved copy goes to Avid's placement, <dest>/OMFI MediaFiles/,
	// never under MXF. The scanner's verdict decides, not the folder's name.
	if (preserve && omfEra)
		return Conventions::omfRootUnder(destRoot) + QLatin1Char('/') + fileName;
	if (preserve)
		return Conventions::mxfRootUnder(destRoot) + QLatin1Char('/') + mxfFolder +
			   QLatin1Char('/') + fileName;
	return destRoot + QLatin1Char('/') + fileName;
}

// QStorageInfo's device() is the filesystem's own identity (dev_t on
// POSIX, the volume GUID on Windows); requiring BOTH ends valid, ready
// and equal means "don't know" fails safe. The destination file doesn't
// exist yet, so its PARENT answers for it. See the header for why this
// gate is safety-critical, not an optimisation.
bool OpRunner::sameVolumeForRename(const QString &src, const QString &dstPath)
{
	const QStorageInfo srcVol(QFileInfo(src).absolutePath());
	const QStorageInfo dstVol(QFileInfo(dstPath).absolutePath());
	return srcVol.isValid() && srcVol.isReady() && dstVol.isValid() && dstVol.isReady() &&
		   !srcVol.device().isEmpty() && srcVol.device() == dstVol.device();
}

// Naming style for Keep-Both duplicates: `name (2).mxf`, `name (3).mxf` —
// the Windows/Chrome convention (Marty's pick, 2026-08-30). Two reasons:
// everyone recognises it instantly, and it is deliberately NOT what Media
// Composer does (Avid appends `.Copy.01` in bins), so a user can tell at
// a glance the duplicate came from MediaMuster, not from Avid.
//
// Alternatives considered and parked for a possible revisit (the naming
// question may become moot — see the last one):
//   .Copy.NN            what MC does; shipped in v1; indistinguishable
//                       from Avid's own bin duplicates — replaced.
//   name copy 2         Finder style; spaces read fine but scripts hate them.
//   name.dup01          compact and explicit; less universally recognised.
//   name.B / name.C     camera-roll lettering; only 25 slots.
//   name.<date>         says WHEN the duplicate arrived; needs a counter too.
//   name.from-<volume>  says WHERE it came from; long, label chars risky.
//   name.<hash>         collision-proof, no counter cap; opaque to humans.
//   _Duplicates/ folder original names preserved (Avid DBs key by name);
//                       collects conflicts in one reviewable place.
//   next numbered folder (preserve-structure only) Avid's OWN answer:
//                       same names in different MXF/<n> folders are normal.
//   decide, don't name  the engine reads the Mob ID during its identity
//                       check, so it can PROVE a same-named destination is
//                       the same media — then Keep Both should become
//                       "Skip (identical file already there)", and only a
//                       same-name DIFFERENT-media file (alarming, rare)
//                       needs a loud name. The best long-term direction.
std::optional<QString> OpRunner::generateRenamePath(const QString &destPath)
{
	const QFileInfo fi(destPath);
	const QString dir = fi.absolutePath();
	const QString base = fi.completeBaseName();
	const QString ext = fi.suffix();

	// (2) is the first duplicate — the original implicitly being copy 1,
	// exactly as Windows Explorer and Chrome downloads count.
	for (int n = 2; n <= 999; ++n)
	{
		const QString suffix = QStringLiteral(" (%1)").arg(n);
		const QString candidate =
			ext.isEmpty() ? dir + QLatin1Char('/') + base + suffix
						  : dir + QLatin1Char('/') + base + suffix + QLatin1Char('.') + ext;
		if (!QFile::exists(candidate))
			return candidate;
	}
	return std::nullopt;
}

QVector<VolumeIdentity> OpRunner::volumesFor(const OpRequest &request)
{
	// One capture per distinct FOLDER first (items overwhelmingly share
	// their Avid MXF folders), then dedupe by volume root — so a
	// 10,000-file run costs a handful of captures, not 10,000.
	QSet<QString> folders;
	for (const OpItem &it : request.items)
	{
		folders.insert(QFileInfo(it.src).absolutePath());
		if (!it.renameDst.isEmpty())
			folders.insert(QFileInfo(it.renameDst).absolutePath());
	}
	if (!request.destRoot.isEmpty())
		folders.insert(request.destRoot);

	QSet<QString> roots;
	QVector<VolumeIdentity> out;
	for (const QString &folder : folders)
	{
		const VolumeIdentity v = VolumeIdentity::capture(folder);
		if (v.confidence == VolumeIdentity::Confidence::Low)
			continue;
		if (roots.contains(v.rootPath))
			continue;
		roots.insert(v.rootPath);
		out.append(v);
	}
	return out;
}

// MARK: - Shared per-item helpers

QString OpRunner::displayName(const OpItem &it)
{
	return it.clipName.isEmpty() ? it.name : it.clipName;
}

std::optional<FileIdentity> OpRunner::captureAndCheckSource(const OpItem &it)
{
	// Beat 1 of every machine: never operate on a guess. The scan (or
	// the resumed plan) CLAIMED a size and an Avid identity for this
	// path; the file actually sitting there now must agree, because the
	// dialog can sit open for minutes while a shared volume changes
	// underneath it.
	const FileIdentity id = FileIdentity::capture(it.src);

	if (id.confidence == FileIdentity::Confidence::Low)
	{
		const bool stillThere = QFileInfo::exists(it.src);
		m_sink.itemDone(it.name, it.src, false,
						stillThere
							? QStringLiteral("Couldn't examine '%1' at %2 — the drive may be "
											 "failing or disconnected. Nothing was touched.")
								  .arg(displayName(it), it.src)
							: QStringLiteral("'%1' is no longer at %2 — it may have been moved "
											 "or its drive disconnected. Nothing was touched.")
								  .arg(displayName(it), it.src),
						false);
		return std::nullopt;
	}

	if (it.bytes > 0 && id.size != it.bytes)
	{
		m_sink.itemDone(it.name, it.src, false,
						QStringLiteral("'%1' at %2 is not the file that was scanned — its size "
									   "changed from %3 to %4. Rescan and try again. Nothing "
									   "was touched.")
							.arg(displayName(it), it.src, Format::bytes(it.bytes),
								 Format::bytes(id.size)),
						false);
		return std::nullopt;
	}

	// The content check: the Avid UMID inside the file vs the scan's
	// claims. Match-any — the header's UMID is the MaterialPackage's
	// (usually the master clip's), with a SourcePackage fallback, so
	// either claim can be the one in the header. All-zero UMIDs are
	// Avid's "no id was assigned" and prove nothing either way.
	//
	// CRITICAL: the claims come from the PMR/MDB world, whose byte order
	// for the ID's middle fields DIFFERS from the MXF header's — the same
	// identity renders as two different hex strings (the codebase already
	// owns this split: MobId::toPmrForm is how the scanner joins header
	// UMIDs to database rows). The gate must compare in BOTH dialects;
	// comparing raw strings here refused every healthy database-described
	// file (found on real media, 2026-08-30).
	if (!id.contentUmid.isEmpty() && !MobId::isAllZero(id.contentUmid))
	{
		const QString headerAsPmr = MobId::toPmrForm(id.contentUmid);
		const auto matchesHeader = [&](const QString &claim)
		{ return claim == id.contentUmid || claim == headerAsPmr; };

		const bool haveClaim = (!it.mobId.isEmpty() && !MobId::isAllZero(it.mobId)) ||
							   (!it.masterMobId.isEmpty() && !MobId::isAllZero(it.masterMobId));
		if (haveClaim && !matchesHeader(it.mobId) && !matchesHeader(it.masterMobId))
		{
			m_sink.itemDone(it.name, it.src, false,
							QStringLiteral("'%1' at %2 is not the file that was selected — the "
										   "Avid media ID inside it doesn't match the clip from "
										   "the scan. Rescan and try again. Nothing was "
										   "touched.")
								.arg(displayName(it), it.src),
							false);
			return std::nullopt;
		}
	}

	return id;
}

OpRunner::ConflictAction OpRunner::resolveConflict(const OpItem &it, QString &dstPath,
												   const QSet<QString> &claimed)
{
	if (!QFile::exists(dstPath))
		return ConflictAction::Proceed;

	const std::optional<ConflictPolicy> policy = conflictPolicyFromName(it.policy);
	if (!policy)
	{
		// No policy entry means the dialog never showed this conflict.
		// Two ways in:
		//
		//   1. A file earlier in THIS run created the destination
		//      (flatten duplicates). Expected; proceed and let
		//      claimDestination redirect this one to a " (2)" sibling.
		//
		//   2. A foreign file appeared after the dialog's conflict sweep
		//      — a race on a shared volume, or a case/normalisation
		//      alias of a selected name that string keys can't see.
		//      Replacing would destroy a file the user was never asked
		//      about; skip instead. (CI proved the stakes: on NTFS a
		//      case-variant sailed past every string comparison, and an
		//      old Replace fallback destroyed the first file's bytes.)
		if (claimed.contains(PathKey::normalise(dstPath)))
			return ConflictAction::Proceed;

		m_sink.itemDone(it.name, it.src, true,
						QStringLiteral("Skipped: a file appeared at this destination after the "
									   "preview. Run the operation again to choose Replace or "
									   "Keep Both."),
						true);
		return ConflictAction::Skip;
	}

	if (*policy == ConflictPolicy::Skip)
	{
		m_sink.itemDone(it.name, it.src, true, QStringLiteral("Skipped (already exists)"), true);
		return ConflictAction::Skip;
	}

	if (*policy == ConflictPolicy::KeepBoth)
	{
		const auto renamed = generateRenamePath(dstPath);
		if (!renamed)
		{
			m_sink.itemDone(it.name, it.src, false,
							QStringLiteral("There are already 999 copies! Did somebody mean to "
										   "delete some of these?"),
							false);
			return ConflictAction::Fail;
		}
		dstPath = *renamed;
		m_sink.log(QtInfoMsg,
				   QStringLiteral("Renaming to %1").arg(QFileInfo(*renamed).fileName()));
	}

	// Replace falls through: the machine parks the live destination
	// aside (ParkedFile) before writing, so the slot is cleared without
	// the original ever being unrecoverable.
	return ConflictAction::Proceed;
}

bool OpRunner::claimDestination(const OpItem &it, QString &dstPath, QSet<QString> &claimed)
{
	const QString key = PathKey::normalise(dstPath);
	if (!claimed.contains(key))
	{
		claimed.insert(key);
		return true;
	}

	// A file earlier in this run already took this exact path. Never
	// clobber it; carve out a unique sibling instead. generateRenamePath
	// probes the disk, where that earlier file already sits, so it skips
	// straight past the taken slot.
	const auto renamed = generateRenamePath(dstPath);
	if (!renamed)
	{
		m_sink.itemDone(it.name, it.src, false,
						QStringLiteral("Another selected file already maps to this destination, "
									   "and all duplicate names are taken."),
						false);
		return false;
	}
	m_sink.log(QtInfoMsg,
			   QStringLiteral("Renaming to %1 (another selected file already targets %2)")
				   .arg(QFileInfo(*renamed).fileName(), QFileInfo(dstPath).fileName()));
	dstPath = *renamed;
	claimed.insert(PathKey::normalise(dstPath));
	return true;
}

void OpRunner::warnJournalDegradedOnce(const OpJournal &journal, bool &warned)
{
	if (warned || !journal.degraded())
		return;
	warned = true;
	m_sink.log(QtCriticalMsg, OpJournal::degradedText());
}

void OpRunner::flagStrandedPark(JournalOpGuard &lop, ParkedFile &park, const OpItem &it)
{
	// Two shapes of stranding, and they need different words. The first is
	// our OWN unfinished write refusing to delete (a file another program
	// still holds open, the everyday Windows case): there may be no parked
	// original at all, so there is no temp name to point the user at — what
	// they need is the full path of the fragment sitting under a real media
	// name. The second is the original itself refusing to go home.
	if (park.destinationLeftBehind())
	{
		lop.failedDirty(QStringLiteral("destination write could not be removed"));
		QString msg =
			QStringLiteral("Couldn't delete the unfinished copy of '%1' at %2 — it may be open "
						   "in another application. That file is NOT complete media, but "
						   "anything reading the folder will treat it as if it were.")
				.arg(displayName(it), park.destinationPath());
		if (!park.path().isEmpty())
			msg += QStringLiteral(" The file it was replacing is set aside beside it, named "
								  "'%1'.")
					   .arg(QFileInfo(park.path()).fileName());
		msg += QStringLiteral(" MediaMuster will clear it up on the next launch.");
		m_sink.log(QtCriticalMsg, msg);
	}
	else
	{
		lop.failedDirty(QStringLiteral("restore failed; original still parked"));
		m_sink.log(
			QtCriticalMsg,
			QStringLiteral("Couldn't put the original '%1' back — it's still in the destination "
						   "folder, named '%2'. MediaMuster will finish restoring it automatically "
						   "on the next launch.")
				.arg(it.name, QFileInfo(park.path()).fileName()));
	}

	// The dirty line above is the LAST word on this item's disk state:
	// recovery will read it next launch and act on exactly what it
	// describes. Disarm so the ParkedFile destructor cannot retry renames
	// AFTER the line is final — a retry that succeeded post-journal is how
	// recovery once came to delete a restored original as a "partial"
	// (adversarial review 2026-08-30, finding 2).
	park.disarm();
}

// MARK: - Dispatch

OpRunner::Totals OpRunner::run(const OpRequest &request, const QString &journalDir)
{
	switch (request.kind)
	{
	case OpKind::Copy:
	case OpKind::Move:
		return runCopyMove(request, journalDir);
	case OpKind::Delete:
		return runDelete(request, journalDir);
	case OpKind::Rename:
		return runRename(request, journalDir);
	case OpKind::Undo:
		// Undo runs are built and executed by OpUndo (it reads the
		// original journal and drives the inverse steps itself).
		m_sink.log(QtCriticalMsg,
				   QStringLiteral("Internal error: an undo request reached the runner."));
		return {};
	}
	return {};
}

// MARK: - Copy / Move

OpRunner::Totals OpRunner::runCopyMove(const OpRequest &req, const QString &journalDir)
{
	Totals t;
	const bool isMove = (req.kind == OpKind::Move);
	const int total = req.items.size();

	m_sink.log(QtInfoMsg, QStringLiteral("%1 %2 files to %3")
							  .arg(isMove ? QStringLiteral("Moving") : QStringLiteral("Copying"))
							  .arg(total)
							  .arg(req.destRoot));

	// The write-ahead journal. Copy earns one despite never removing its
	// source: replacing a live destination parks the original aside, and
	// a crash inside that window would otherwise leave the user with
	// neither file and a temp nothing knows about.
	OpJournal journal(req.kind,
					  QJsonObject{{QStringLiteral("destination"), req.destRoot},
								  {QStringLiteral("preserve"), req.preserve}},
					  journalDir);
	if (!journal.isOpen())
		m_sink.log(QtWarningMsg, OpJournal::openFailedText(req.kind));
	// The whole to-do list + volume fingerprints, before the first op.
	journal.writePlan(req.destRoot, req.preserve, req.items, volumesFor(req));

	// Destinations already taken this run, so two same-named selections
	// can't collide on one path. Critical for Move: a silent overwrite
	// loses the first file outright (its source is already gone).
	QSet<QString> claimedDests;

	TrashRouter router(m_sink);
	bool journalDegradedWarned = false;
	bool durabilityNoteLogged = false;

	// Test seam: skip the rename so the cross-volume copy+delete leg is
	// reachable on a single-volume test machine. Never set in production.
	const bool forceCopyLeg = qEnvironmentVariableIsSet("MEDIAMUSTER_FORCE_MOVE_COPY");
	const QLatin1String parkTag = isMove ? kMoveReplaceTag : kCopyReplaceTag;
	const NativeFile::Durability durability =
		isMove ? NativeFile::Durability::Platter : NativeFile::Durability::Disk;

	ProgressThrottle throttle;

	for (int i = 0; i < total && !m_cancel.load(std::memory_order_acquire); ++i)
	{
		const OpItem &it = req.items[i];
		QString dstPath = buildDestPath(it.name, it.folder, req.destRoot, req.preserve,
										it.omfEra); // OMF-era: the item's journaled verdict

		m_sink.progress(it.name, i + 1, total, 0);
		warnJournalDegradedOnce(journal, journalDegradedWarned);
		TestPause::sleepMs(TestPause::kPerItemMs);

		if (const auto action = resolveConflict(it, dstPath, claimedDests);
			action != ConflictAction::Proceed)
		{
			if (action == ConflictAction::Skip)
			{
				++t.skipped;
				// Journal the skip so a resumed run knows this file was
				// concluded, not left undone.
				journal.markSkipped(journal.planOp(it.src, dstPath, it.bytes, QString(), {}));
			}
			else
				++t.failed;
			continue;
		}

		// Disambiguate before the park-aside below, so a redirected
		// dstPath isn't mistaken for a live destination to move aside.
		if (!claimDestination(it, dstPath, claimedDests))
		{
			++t.failed;
			continue;
		}

		// Beat 1: identity.
		const std::optional<FileIdentity> srcId = captureAndCheckSource(it);
		if (!srcId)
		{
			++t.failed;
			continue;
		}

		if (!QDir().mkpath(QFileInfo(dstPath).absolutePath()))
		{
			m_sink.itemDone(it.name, it.src, false,
							QStringLiteral("Couldn't create the destination folder. Nothing "
										   "was touched — check your write permissions."),
							false);
			++t.failed;
			continue;
		}

		// Replace: capture the identity of the file about to be parked,
		// so the journal knows exactly which file was set aside and undo
		// can later restore exactly it.
		FileIdentity parkedOriginalId;
		if (QFile::exists(dstPath))
			parkedOriginalId = FileIdentity::capture(dstPath);

		// Beat 2: the park path reaches the journal BEFORE the rename it
		// describes; recovery needs it to put a replaced file back.
		ParkedFile park(dstPath, parkTag);
		JournalOpGuard lop(&journal, it.src, dstPath, srcId->size, park.path(), *srcId,
						   parkedOriginalId);

		if (!park.park())
		{
			m_sink.itemDone(
				it.name, it.src, false,
				QStringLiteral("Couldn't move the existing destination aside. Nothing changed."),
				false);
			lop.failed(QStringLiteral("park failed"));
			++t.failed;
			continue;
		}

		// MOVE, same volume: pure rename — the fast path. The volume check
		// is NOT an optimisation (adversarial review finding 5):
		// QFile::rename silently falls back to a copy-then-DELETE when the
		// paths cross volumes — an unverified 4 KB-buffered copy whose
		// destination can still be entirely in the page cache when the
		// source is already unlinked. Every guarantee this engine makes
		// (checksum, durability barrier, trash-not-unlink) would be
		// bypassed in one line. Provably same volume → rename is a pure
		// directory-entry swap; anything else → the verified copy leg.
		if (isMove && !forceCopyLeg && sameVolumeForRename(it.src, dstPath) &&
			QFile::rename(it.src, dstPath))
		{
			// The moved file kept its content byte-for-byte (same file
			// object), so its identity is the source's with a fresh
			// filesystem capture at the new path.
			FileIdentity landed = FileIdentity::capture(dstPath, /*readContent=*/false);
			landed.contentUmid = srcId->contentUmid;

			// Beat 4: dispose of the replaced original — to the trash,
			// never a hard delete. If the trash refuses, ROLL THE MOVE
			// BACK (rename home, original back in its slot) rather than
			// leave the replaced file in limbo.
			QString parkedFinal;
			if (!trashParkedOriginal(router, park, &parkedFinal))
			{
				// Roll the move back: the file goes home first, then the
				// original returns to its slot.
				const bool renamedHome = QFile::rename(dstPath, it.src);
				if (renamedHome && park.restore())
				{
					m_sink.itemDone(it.name, it.src, false,
									QStringLiteral("The file that would be replaced couldn't be "
												   "moved to the trash, so this move was rolled "
												   "back. Nothing changed."),
									false);
					lop.failed(QStringLiteral("replaced-original trash failed; rolled back"));
				}
				else if (!renamedHome)
				{
					// The moved file could not go home (something new sits
					// at the source path, or its folder is gone). It stays
					// at the destination: it is the user's ONLY copy of
					// that clip and nothing may displace it — which is
					// exactly what ParkedFile's ownership rule now
					// enforces (review finding 1: the old restore() here
					// unlinked the moved file to make room for the parked
					// original). flagStrandedPark freezes this state into
					// the journal and disarms; recovery sorts it out with
					// the user's file intact.
					flagStrandedPark(lop, park, it);
					m_sink.itemDone(
						it.name, it.src, false,
						QStringLiteral("The file that would be replaced couldn't be moved to "
									   "the trash, and the move couldn't be rolled back "
									   "either. Your file is safe at the destination (%1); "
									   "the file it replaced is set aside next to it. "
									   "MediaMuster will sort this out on the next launch.")
							.arg(dstPath),
						false);
				}
				else
				{
					// The file is home; only the original's rename-back
					// failed. The dirty fail keeps the journal alive;
					// next-launch recovery knows the parked path and
					// finishes the job.
					flagStrandedPark(lop, park, it);
					m_sink.itemDone(it.name, it.src, false,
									QStringLiteral("The file that would be replaced couldn't be "
												   "moved to the trash, and the rollback "
												   "stalled. MediaMuster will finish putting "
												   "things back on the next launch."),
									false);
				}
				++t.failed;
				continue;
			}

			OpJournal::DoneInfo info;
			info.landedId = landed;
			info.parkedFinal = parkedFinal;
			lop.done(info);
			m_sink.itemDone(it.name, it.src, true, {}, false);
			++t.succeeded;
			continue;
		}

		// COPY — and MOVE's cross-volume leg. For a move, the outer
		// `park` above already emptied the destination slot and still
		// holds the replaced original; it must keep holding it until the
		// source is gone. So the copy gets its own inner park over the
		// (now empty) slot, whose only job is discarding a partial
		// write. For a plain copy the outer park plays both roles.
		ParkedFile partial(dstPath, parkTag);
		if (isMove)
			partial.park(); // slot is empty, so this only arms the discard

		ParkedFile &copyPark = isMove ? partial : park;

		qint64 lastEmitBytes = 0;
		const auto onBytes = [&](qint64 copied, qint64 totalBytes)
		{
			if (copied >= totalBytes || throttle.shouldEmit() ||
				(copied - lastEmitBytes) >= kProgressIntervalBytes)
			{
				const double pct = totalBytes > 0 ? (100.0 * copied / totalBytes) : 100.0;
				m_sink.progress(it.name, i + 1, total, pct);
				lastEmitBytes = copied;
			}
		};
		// Progress signal, not a log line: the sheet's detail row is
		// driven by progress, and a console line is not a UI event.
		const auto onVerify = [&]
		{ m_sink.progress(QStringLiteral("Verifying %1").arg(it.name), i + 1, total, 100.0); };

		const OpCopier::Result copyRes =
			m_copier.copy(it.src, dstPath, copyPark, m_cancel, durability, onBytes, onVerify);

		if (copyRes.outcome == OpCopier::Outcome::Cancelled)
		{
			// Stop the run; the in-flight file is neither succeeded nor
			// failed. The copier already discarded its partial write and
			// (for a copy) restored any parked original; a move's outer
			// park still holds the replaced original — put it back.
			if (isMove)
				park.restore();
			if (park.isStranded())
				flagStrandedPark(lop, park, it);
			break;
		}

		if (copyRes.outcome == OpCopier::Outcome::Failed)
		{
			m_sink.itemDone(it.name, it.src, false, copyRes.error, false);
			if (isMove)
				park.restore();
			if (park.isStranded())
				flagStrandedPark(lop, park, it);
			else
				lop.failed(QStringLiteral("copy failed"));
			++t.failed;
			continue;
		}

		// Which of the three copy paths ran, in the console. The clone line
		// has always been here; the native one was collected by the copier
		// and then read by nobody, so the Windows fast path was invisible.
		if (copyRes.usedClone)
			m_sink.log(QtInfoMsg, QStringLiteral("Cloned %1").arg(it.name));
		else if (copyRes.usedNativeCopy)
			m_sink.log(QtInfoMsg, QStringLiteral("Copied %1 (native)").arg(it.name));

		// A network destination couldn't give the full durability
		// barrier: record it honestly, in the journal and once in the log.
		if (copyRes.durabilityDegraded)
		{
			journal.writeNote(
				QStringLiteral("'%1': the destination volume couldn't confirm a full flush to "
							   "disk; relying on the server's write acknowledgement.")
					.arg(it.name));
			if (!durabilityNoteLogged)
			{
				durabilityNoteLogged = true;
				m_sink.log(QtWarningMsg,
						   QStringLiteral("The destination volume can't confirm writes reached "
										  "its physical disks (network storage). Copies are "
										  "verified by checksum; durability rests on the "
										  "server."));
			}
		}

		// Beat 4, first half: the source must STILL be the file we
		// verified at the start — a same-size swap during a long copy is
		// exactly the attack window identity exists to close.
		FileIdentity actualSrc;
		if (FileIdentity::verify(it.src, *srcId, &actualSrc) != FileIdentity::Verdict::Match)
		{
			// Discard the copy we made of who-knows-what and put any
			// replaced original back.
			copyPark.restore();
			if (isMove)
				park.restore();
			m_sink.itemDone(it.name, it.src, false,
							QStringLiteral("'%1' changed while it was being copied — %2. The "
										   "destination has been left unchanged.")
								.arg(displayName(it),
									 FileIdentity::explainDifference(*srcId, actualSrc)),
							false);
			if (park.isStranded())
				flagStrandedPark(lop, park, it);
			else
				lop.failed(QStringLiteral("source identity changed during copy"));
			++t.failed;
			continue;
		}

		// The landed file's identity, for the journal and for undo. The
		// bytes are checksum-verified identical, so the content identity
		// carries over without re-reading the header.
		FileIdentity landed = FileIdentity::capture(dstPath, /*readContent=*/false);
		landed.contentUmid = srcId->contentUmid;

		if (isMove)
		{
			// The copy's BYTES are platter-durable, but the directory
			// ENTRY naming the new file can still be in-memory filesystem
			// metadata (review finding 4): a power cut after the source
			// remove would then leave a volume holding the bytes with no
			// name pointing at them — fewer complete copies from a single
			// fault, at exactly the instant the Platter barrier was bought
			// for. So the destination FOLDER gets its own barrier before
			// anything irreversible happens. A destination that already
			// degraded the file barrier (network storage) may refuse this
			// too — same honest note, the server's semantics govern; a
			// LOCAL destination refusing it is a hard failure: roll back,
			// keep the source.
			if (!NativeFile::syncDirectory(QFileInfo(dstPath).absolutePath()))
			{
				if (copyRes.durabilityDegraded)
				{
					journal.writeNote(
						QStringLiteral("'%1': the destination couldn't confirm its folder "
									   "update either; relying on the server's write "
									   "acknowledgement.")
							.arg(it.name));
				}
				else
				{
					partial.restore(); // discard the new copy (ours)
					park.restore();	   // replaced original back in its slot
					m_sink.itemDone(
						it.name, it.src, false,
						QStringLiteral("The destination couldn't confirm the new file was "
									   "recorded in its folder, so this move was rolled back. "
									   "Nothing changed — check the drive and try again."),
						false);
					if (park.isStranded())
						flagStrandedPark(lop, park, it);
					else
						lop.failed(QStringLiteral("destination directory sync failed"));
					++t.failed;
					continue;
				}
			}

			// Dispose of the replaced original BEFORE removing the
			// source: if the trash refuses, the whole item can still
			// roll back cleanly (restore removes the new copy and puts
			// the original back — the source was never touched).
			QString parkedFinal;
			if (!trashParkedOriginal(router, park, &parkedFinal))
			{
				partial.restore(); // discard the new copy
				park.restore();	   // replaced original back in its slot
				m_sink.itemDone(it.name, it.src, false,
								QStringLiteral("The file that would be replaced couldn't be "
											   "moved to the trash, so this move was rolled "
											   "back. Nothing changed."),
								false);
				if (park.isStranded())
					flagStrandedPark(lop, park, it);
				else
					lop.failed(QStringLiteral("replaced-original trash failed; rolled back"));
				++t.failed;
				continue;
			}

			// THE point of no return for a move — and the whole reason
			// the copy above ran with the Platter barrier: the moment
			// this remove succeeds, the destination is the only copy.
			// Direct removal (not trash) after triple verification is
			// Marty's confirmed decision — moving media off a full
			// volume must actually free its space.
			if (!QFile::remove(it.src))
			{
				// Both copies exist; the replaced original (if any) is
				// already in the trash. Nothing is lost — say exactly
				// what the state is.
				partial.commit(); // keep the verified copy
				// The fail line carries `parkedFinal` because the disposal
				// ALREADY happened: without it, the trash address of a file
				// the engine moved dies with this run, and nothing could
				// ever put that file back.
				lop.failed(QStringLiteral("source remove failed"), parkedFinal);
				QString why =
					QStringLiteral("The new copy at the destination is verified, but the "
								   "original couldn't be removed (it may be open in another "
								   "application). The file now exists in both places.");
				if (!parkedFinal.isEmpty())
					why += QStringLiteral(" The file it replaced is in the trash, at %1.")
							   .arg(parkedFinal);
				m_sink.itemDone(it.name, it.src, false, why, false);
				++t.failed;
				continue;
			}
			partial.commit();

			OpJournal::DoneInfo info;
			info.hash = copyRes.hashHex;
			info.landedId = landed;
			info.parkedFinal = parkedFinal;
			lop.done(info);
			m_sink.itemDone(it.name, it.src, true, {}, false);
			++t.succeeded;
		}
		else
		{
			// Copy: dispose of the replaced original now that the new
			// file is verified. If the trash refuses, roll back — the
			// restore discards the new copy and puts the original back,
			// and the user's source is untouched either way.
			QString parkedFinal;
			if (!trashParkedOriginal(router, park, &parkedFinal))
			{
				park.restore();
				m_sink.itemDone(it.name, it.src, false,
								QStringLiteral("The file that would be replaced couldn't be "
											   "moved to the trash, so this copy was rolled "
											   "back. Nothing changed."),
								false);
				if (park.isStranded())
					flagStrandedPark(lop, park, it);
				else
					lop.failed(QStringLiteral("replaced-original trash failed; rolled back"));
				++t.failed;
				continue;
			}

			OpJournal::DoneInfo info;
			info.hash = copyRes.hashHex;
			info.landedId = landed;
			info.parkedFinal = parkedFinal;
			lop.done(info);
			m_sink.itemDone(it.name, it.src, true, {}, false);
			++t.succeeded;
		}
	}

	// Cancel means stop and keep: close the journal clean so recovery
	// won't undo what already landed. The journal STAYS on disk — it is
	// now the undo candidate (the next operation prunes it).
	journal.finish(t.succeeded, t.failed, t.skipped, m_cancel.load(std::memory_order_acquire));

	if (router.mediaMusterCount() > 0)
		m_sink.trashUsed(router.mediaMusterFolder(), router.mediaMusterCount());

	m_sink.log(t.failed > 0 ? QtWarningMsg : QtInfoMsg,
			   formatOperationSummary(isMove ? QStringLiteral("Move") : QStringLiteral("Copy"),
									  t.succeeded, t.failed, t.skipped));
	return t;
}

// MARK: - Delete

OpRunner::Totals OpRunner::runDelete(const OpRequest &req, const QString &journalDir)
{
	Totals t;
	const int total = req.items.size();

	m_sink.log(QtInfoMsg, QStringLiteral("Deleting %1 files").arg(total));

	OpJournal journal(OpKind::Delete, QJsonObject{}, journalDir);
	if (!journal.isOpen())
		m_sink.log(QtWarningMsg, OpJournal::openFailedText(OpKind::Delete));
	journal.writePlan(QString(), false, req.items, volumesFor(req));

	TrashRouter router(m_sink);
	bool journalDegradedWarned = false;

	for (int i = 0; i < total && !m_cancel.load(std::memory_order_acquire); ++i)
	{
		const OpItem &it = req.items[i];
		m_sink.progress(it.name, i + 1, total, 0);
		warnJournalDegradedOnce(journal, journalDegradedWarned);
		TestPause::sleepMs(TestPause::kPerItemMs);

		// Beat 1: a delete is the easiest place to destroy the wrong
		// file, so it gets the same identity gate as everything else.
		const std::optional<FileIdentity> srcId = captureAndCheckSource(it);
		if (!srcId)
		{
			++t.failed;
			continue;
		}

		// Beat 2: the intent line, before the file moves anywhere.
		JournalOpGuard lop(&journal, it.src, QString(), srcId->size, QString(), *srcId);

		// Beat 3: the trash tiers. Never a hard delete.
		const TrashRouter::Landing landing = router.trash(it.src);
		if (!landing.ok)
		{
			m_sink.itemDone(it.name, it.src, false, landing.error, false);
			lop.failed(QStringLiteral("trash failed"));
			++t.failed;
			continue;
		}

		OpJournal::DoneInfo info;
		info.finalPath = landing.finalPath;
		lop.done(info);
		m_sink.itemDone(it.name, it.src, true, {}, false);
		++t.succeeded;
	}

	journal.finish(t.succeeded, t.failed, /*skipped=*/0,
				   m_cancel.load(std::memory_order_acquire));

	m_sink.log(t.failed > 0 ? QtWarningMsg : QtInfoMsg,
			   formatOperationSummary(QStringLiteral("Delete"), t.succeeded, t.failed));

	if (router.mediaMusterCount() > 0)
	{
		m_sink.log(QtInfoMsg, QStringLiteral("%1 file(s) moved to MediaMuster Trash at %2")
								  .arg(router.mediaMusterCount())
								  .arg(router.mediaMusterFolder()));
		m_sink.trashUsed(router.mediaMusterFolder(), router.mediaMusterCount());
	}

	return t;
}

// MARK: - Rename (Rebalance)

OpRunner::Totals OpRunner::runRename(const OpRequest &req, const QString &journalDir)
{
	Totals t;
	const int total = req.items.size();

	m_sink.log(QtInfoMsg, QStringLiteral("Moving %1 files between folders").arg(total));

	OpJournal journal(OpKind::Rename, QJsonObject{}, journalDir);
	if (!journal.isOpen())
		m_sink.log(QtWarningMsg, OpJournal::openFailedText(OpKind::Rename));
	journal.writePlan(QString(), false, req.items, volumesFor(req));

	bool journalDegradedWarned = false;
	QSet<QString> touchedFolders;
	QString currentGroup;

	const auto touchFolder = [&](const QString &folder)
	{
		if (touchedFolders.contains(folder))
			return;
		touchedFolders.insert(folder);

		// Delete the folder's Avid databases the moment its contents
		// change — NOT at the end of the run; see the ordering rationale
		// at resetAvidDatabases. Living in the ENGINE (not the
		// Rebalance adapter) means an undo of a rename run resets them too.
		resetAvidDatabases(folder, m_sink);

		if (onRenameFolderTouched)
			onRenameFolderTouched(folder);
	};

	for (int i = 0; i < total; ++i)
	{
		const OpItem &it = req.items[i];

		// Cancel only lands BETWEEN groups: a clip's relatives (its
		// video and audio files) move as one or not at all, so a
		// half-moved clip can't dangle across two folders. An item with
		// no group is its own boundary — otherwise a run of ungrouped
		// items ("" == "") would never see the cancel at all.
		if (it.groupKey.isEmpty() || it.groupKey != currentGroup)
		{
			currentGroup = it.groupKey;
			if (m_cancel.load(std::memory_order_acquire))
				break;
		}

		m_sink.progress(it.name, i + 1, total, 0);
		warnJournalDegradedOnce(journal, journalDegradedWarned);
		TestPause::sleepMs(TestPause::kPerItemMs);

		const std::optional<FileIdentity> srcId = captureAndCheckSource(it);
		if (!srcId)
		{
			++t.failed;
			continue;
		}

		// Never clobber: an occupied destination fails the item, loudly.
		if (QFile::exists(it.renameDst))
		{
			m_sink.itemDone(it.name, it.src, false,
							QStringLiteral("Couldn't move %1 — a file already exists at the "
										   "destination. Nothing was touched.")
								.arg(it.name),
							false);
			++t.failed;
			continue;
		}

		JournalOpGuard lop(&journal, it.src, it.renameDst, srcId->size, QString(), *srcId);

		if (!QFile::rename(it.src, it.renameDst))
		{
			m_sink.itemDone(it.name, it.src, false,
							QStringLiteral("Couldn't move %1 — it may be open in another "
										   "application. Nothing was touched.")
								.arg(it.name),
							false);
			lop.failed(QStringLiteral("rename failed"));
			++t.failed;
			continue;
		}

		FileIdentity landed = FileIdentity::capture(it.renameDst, /*readContent=*/false);
		landed.contentUmid = srcId->contentUmid;
		OpJournal::DoneInfo info;
		info.landedId = landed;
		lop.done(info);
		m_sink.itemDone(it.name, it.src, true, {}, false);
		++t.succeeded;

		// Both folders' Avid databases are now stale; the hook (the
		// Rebalance adapter's database reset) runs after the FIRST
		// successful rename touching each folder, matching the v1
		// rebalancer's honest-absence ordering.
		touchFolder(QFileInfo(it.src).absolutePath());
		touchFolder(QFileInfo(it.renameDst).absolutePath());
	}

	journal.finish(t.succeeded, t.failed, /*skipped=*/0,
				   m_cancel.load(std::memory_order_acquire));

	m_sink.log(t.failed > 0 ? QtWarningMsg : QtInfoMsg,
			   formatOperationSummary(QStringLiteral("Rename"), t.succeeded, t.failed));
	return t;
}
