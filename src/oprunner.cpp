#include "oprunner.h"
#include "operationplan.h"
#include "optrash.h"
#include "conventions.h"
#include "mobid.h"
#include "mxfparser.h"
#include "pathkey.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QThread>
#include <stdexcept>
#include <algorithm>

namespace
{
using Step = OpJournal::Step;
using State = OpResult::State;
using Sync = NativeFile::SyncResult;
Sync syncFolders(const QStringList &folders, QString &error,
				 const NativeFile::DirectorySync &sync = NativeFile::syncDirectory)
{
	QStringList warnings;
	bool degraded = false;
	for (const auto &folder : folders)
	{
		QString detail;
		const auto status = sync(folder, &detail);
		if (status == Sync::Failed)
		{
			error = detail;
			return status;
		}
		if (status == Sync::OkDegraded)
		{
			degraded = true;
			warnings.append(detail);
		}
	}
	error = warnings.join('\n');
	return degraded ? Sync::OkDegraded : Sync::Ok;
}
const QString directoryWarning = QStringLiteral(
	"Copy finished; original retained. This storage does not support confirming folder "
	"changes against a crash or power loss.");
QString unique()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString label(const OpItem &i)
{
	return i.clipName.isEmpty() ? i.name : i.clipName;
}
OpResult result(const OpJournal::Entry &e, State s, const QString &message = {},
				bool removed = false)
{
	return {s, label(e.item), e.item.src, e.dst, message, removed || e.sourceRemoved ||
		(!e.retirement.isEmpty() && !OpFile::occupied(e.item.src) &&
		 e.source.unchanged(OpFile::inspect(e.retirement)))};
}
bool leaf(const QString &s)
{
	return !s.isEmpty() && s != "." && s != ".." && !s.contains('/') && !s.contains('\\');
}
QString trashRoot(const QString &source)
{
	// Place Trash beside the Avid media tree. This also works for a user's
	// internal-disk media area, where the system volume root is read-only.
	QDir parent(QFileInfo(source).absolutePath());
	for (;;)
	{
		const auto name = parent.dirName();
		if (name.compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0 ||
			Conventions::isOmfRootName(name))
		{
			parent.cdUp();
			return parent.filePath(Conventions::kMediaMusterTrashDir);
		}
		if (!parent.cdUp())
			break;
	}
	return QFileInfo(source).dir().filePath(Conventions::kMediaMusterTrashDir);
}
bool mediaIdentityMatches(const OpItem &item, OpFile &source, QString &error)
{
	if (!Conventions::hasMxfExtension(item.src))
		return true;
	const bool fileKnown = !item.mobId.isEmpty() && !MobId::isAllZero(item.mobId);
	const bool masterKnown = !item.masterMobId.isEmpty() && !MobId::isAllZero(item.masterMobId);
	if (!fileKnown && !masterKnown)
		return true;
	const auto h = MxfParser::parseHeader(source.io());
	auto matches = [](const QString &expected, const QString &actual)
	{
		return !actual.isEmpty() && !MobId::isAllZero(actual) &&
			   (expected == actual || expected == MobId::toPmrForm(actual));
	};
	if ((fileKnown && !matches(item.mobId, h.fileMobId)) ||
		(masterKnown && (!h.hasMaterialPackage || !matches(item.masterMobId, h.umid))))
	{
		error = "The file's Avid identity is missing or differs from the scan. Rescan before "
				"proceeding.";
		return false;
	}
	return true;
}
void keepArtifact(OpJournal::Entry &e, const QString &path)
{
	if (!path.isEmpty() && OpFile::occupied(path) && !e.artifacts.contains(path))
		e.artifacts.append(path);
}
} // namespace

void OpRunner::checkpoint(const QString &name, const OpJournal::Entry &e)
{
	if (hooks.checkpoint)
		hooks.checkpoint(name, e);
}
bool OpRunner::save(OpJournal &j, OpJournal::Entry &e, Step s)
{
	e.step = s;
	if (fail("journal"))
		j.stop("Injected journal failure; further changes stopped.");
	if (!j.save(e))
		return false;
	checkpoint(OpJournal::stepName(s), e);
	return true;
}

namespace
{
bool copiedDestinationMatches(const OpJournal::Entry &e, QString &error,
	const std::atomic<bool> *cancellation = nullptr)
{
	if (!e.landed.unchanged(OpFile::inspect(e.dst)))
	{
		error = "The completed destination is missing or changed: " + e.dst;
		return false;
	}
	if (!e.verificationRequested)
		return true;
	std::atomic<bool> neverCancel{false};
	const auto &cancel = cancellation ? *cancellation : neverCancel;
	if (cancel.load())
	{
		error = "Cancelled while checking completed work.";
		return false;
	}
	auto dst = OpFile::open(e.dst, false, error);
	if (!dst || e.hash.isEmpty())
		return false;
	const auto checked = OpCopier::hash(*dst, cancel);
	if (checked.outcome != OpCopier::Outcome::Succeeded || checked.hash != e.hash ||
		!dst->stillAt(e.dst, e.landed))
	{
		error = "The completed destination no longer passes its saved checksum check: " + e.dst;
		return false;
	}
	return true;
}
bool removesAfterCopy(const OpJournal::Entry &e, const OpRequest &request)
{
	return (request.kind == OpKind::Move || e.undoAction == "restoreMove") &&
		e.mechanism == "copy";
}
}

// Recovery examines recorded identities and intent. It never performs a new
// deletion; explicit Resume performs remaining filesystem mutations afterwards.
bool OpRunner::reconcile(OpJournal &j, OpJournal::Entry &e, QString &error,
	const std::atomic<bool> *cancellation, const NativeFile::DirectorySync &directorySync)
{
	if (e.complete())
		return true;
	if (e.mechanism == "systemTrash" && e.step != Step::Planned &&
		e.step != Step::Failed && e.step != Step::Cancelled)
	{
		if (!e.trashReceipt.isEmpty() && e.landed.unchanged(OpFile::inspect(e.dst)) &&
			!OpFile::occupied(e.item.src))
		{
			if (syncFolders({QFileInfo(e.item.src).absolutePath(), QFileInfo(e.dst).absolutePath()}, error) != Sync::Ok)
				return false;
			e.sourceRemoved = true;
			e.step = Step::Done;
			return j.save(e);
		}
		error = "The system Trash result was interrupted before a recovery receipt was saved. "
				"Inspect Trash; MediaMuster will not repeat this deletion.";
		return false;
	}
	if (e.step == Step::RemovingSource || !e.retirement.isEmpty())
	{
		if (!j.record().copiesComplete || !copiedDestinationMatches(e, error, cancellation))
			return false;
		const auto src = OpFile::inspect(e.item.src), retired = OpFile::inspect(e.retirement);
		if ((OpFile::occupied(e.item.src) && !e.source.unchanged(src)) ||
			(OpFile::occupied(e.retirement) && !e.source.unchanged(retired)) ||
			(src.valid() && retired.valid()))
		{
			error = "An original or retirement location changed. Both locations were retained.";
			return false;
		}
		if (!src.valid() && !retired.valid())
		{
			if (syncFolders({QFileInfo(e.item.src).absolutePath(),
				QFileInfo(e.retirement).absolutePath()}, error) != Sync::Ok)
				return false;
			e.sourceRemoved = true;
			e.step = Step::SourceRemoved;
			return j.save(e);
		}
		e.step = Step::RemovingSource;
		return j.save(e);
	}
	if (e.mechanism == "relocate" &&
		(e.step == Step::Relocating || e.step == Step::NeedsAttention))
	{
		const auto src = OpFile::inspect(e.item.src), dst = OpFile::inspect(e.dst);
		if (e.source.unchanged(dst) && !OpFile::occupied(e.item.src))
		{
			if (syncFolders({QFileInfo(e.dst).absolutePath(),
				QFileInfo(e.item.src).absolutePath()}, error) != Sync::Ok)
				return false;
			e.landed = dst;
			e.sourceRemoved = true;
			e.step = Step::Done;
			e.error.clear();
			return j.save(e);
		}
		if (e.source.unchanged(src) && !OpFile::occupied(e.dst))
		{
			e.step = Step::Planned;
			return j.save(e);
		}
		error = "The interrupted relocation needs inspection; both locations were retained.";
		return false;
	}
	if (e.step == Step::Planned || e.step == Step::Failed || e.step == Step::Cancelled ||
		e.step == Step::Copying || ((e.step == Step::CopyReady || e.step == Step::Verified ||
		e.step == Step::Publishing) && OpFile::occupied(e.temp)))
	{
		keepArtifact(e, e.temp);
		if (!e.source.unchanged(OpFile::inspect(e.item.src)))
		{
			error = "The original file is missing or changed; its recovery record was retained: " +
				e.item.src;
			return false;
		}
		e.step = Step::Planned;
		return j.save(e);
	}
	if (e.mechanism == "copy" && copiedDestinationMatches(e, error, cancellation))
	{
		if (removesAfterCopy(e, j.record().request) &&
			!e.source.unchanged(OpFile::inspect(e.item.src)))
		{
			error = "The original changed or is missing without a recorded removal intent.";
			return false;
		}
		auto dst = OpFile::openWritableExisting(e.dst, error);
		if (!dst || !dst->stillAt(e.dst, e.landed))
			return false;
		const auto fileSync = dst->sync();
		const auto folderSync = syncFolders({QFileInfo(e.dst).absolutePath()}, error, directorySync);
		if (fileSync == Sync::Failed || folderSync == Sync::Failed)
		{
			error = "The completed copy could not confirm its writes during recovery. " + error;
			return false;
		}
		e.copyDurable = fileSync == Sync::Ok && folderSync == Sync::Ok;
		e.temp.clear();
		e.step = removesAfterCopy(e, j.record().request) ? Step::Published : Step::Done;
		return j.save(e);
	}
	if (error.isEmpty())
		error = "Cannot establish the interrupted result; journal and files were retained.";
	return false;
}

OpResult OpRunner::transfer(OpJournal &j, OpJournal::Entry &e, OpKind kind, OpFile &source,
								int index, int total, bool directoryDurable, bool *retryableCopy)
{
	QString error;
	const QString tempDir = QFileInfo(e.dst).absolutePath() + "/.mediamuster-" + unique();
	e.temp = tempDir + "/payload.partial";
	e.hash.clear();
	e.mechanism = "copy";
	e.verificationRequested = j.record().request.verifyCopies;
	++e.attempts;
	e.landed = {};
	if (!save(j, e, Step::Copying))
		return result(e, State::NeedsAttention, "Journal failure; source retained.");
	const auto tempSync = OpFile::makeDirectory(tempDir, error, hooks.directorySync);
	if (tempSync == Sync::Failed)
	{
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	directoryDurable = directoryDurable && tempSync == Sync::Ok;
	QFile::setPermissions(tempDir,
						  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	auto destination = OpFile::open(e.temp, true, error);
	if (!destination)
	{
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	e.landed = destination->stamp();
	if (!save(j, e, Step::Copying))
		return result(e, State::NeedsAttention,
					  "Journal failure; temporary file retained at " + e.temp);
	auto progress = [&](qint64 bytes, qint64 size, bool verifying)
	{
		m_sink.progress((verifying ? QStringLiteral("Checking copies: ") : QStringLiteral("Copying ")) +
							label(e.item),
						index, total, j.record().request.verifyCopies ?
						(verifying ? 80.0 + (size ? 20.0 * bytes / size : 20.0) :
						 (size ? 80.0 * bytes / size : 80.0)) : (size ? 100.0 * bytes / size : 100.0));
		checkpoint(verifying ? "readback-chunk" : "copy-chunk", e);
	};
	OpCopier copier;
	OpCopier::Result copied;
	const int injectedError = hooks.nativeCopyError ? hooks.nativeCopyError(e) : 0;
	if (injectedError)
	{
		copied.outcome = m_cancel.load() ? OpCopier::Outcome::Cancelled : OpCopier::Outcome::Failed;
		copied.error = QStringLiteral("Injected native copy error %1.").arg(injectedError);
		copied.retryable = copied.outcome == OpCopier::Outcome::Failed &&
			OpCopier::isRetryableNativeError(injectedError);
	}
	else
		copied = copier.copy(source, *destination, m_cancel, progress,
								  [&] { checkpoint("before-readback", e); },
								  j.record().request.verifyCopies);
	auto abandon = [&](State state, const QString &why)
	{
		QString cleanup;
		// Only the protected HANDLE can remove a partial. On platforms without
		// that operation, leaving an isolated partial is the safe fallback.
		if (!fail("cleanup") && destination->stamp().sameObject(OpFile::inspect(e.temp)) &&
			destination->removeProtected(cleanup))
		{
			e.temp.clear();
		}
		else
			keepArtifact(e, e.temp);
		e.error =
			why + (e.temp.isEmpty() ? QString()
									: QStringLiteral(" Temporary file retained: %1").arg(e.temp));
		const auto step = state == State::Cancelled ? Step::Cancelled
						  : state == State::Skipped ? Step::Skipped
													: Step::Failed;
		if (!save(j, e, step))
			state = State::NeedsAttention;
		return result(e, state, e.error);
	};
	if (copied.outcome != OpCopier::Outcome::Succeeded)
	{
		if (retryableCopy)
			*retryableCopy = copied.outcome == OpCopier::Outcome::Failed && copied.retryable;
		return abandon(copied.outcome == OpCopier::Outcome::Cancelled ? State::Cancelled
																	  : State::Failed,
						   copied.error);
	}
	e.hash = copied.hash;
	e.copyDurable = copied.durable && directoryDurable;
	e.metadataComplete = copied.metadataComplete;
	e.error = copied.error;
	if (!copied.durable)
		e.error += " Copy finished; the storage did not confirm the full durability "
				   "request.";
	if (!directoryDurable)
		e.error += '\n' + directoryWarning;
	e.landed = destination->stamp();
	if (!save(j, e, e.verificationRequested ? Step::Verified : Step::CopyReady))
		return result(e, State::NeedsAttention,
					  "Journal failure; temporary file retained at " + e.temp);
	if (m_cancel.load())
		return abandon(State::Cancelled, "Cancelled before publication.");
	if (!source.stillAt(e.item.src, e.source) || !destination->stillAt(e.temp, e.landed))
		return abandon(State::Failed, "A file changed before publication; source retained.");
	const auto originalDestination =
		(e.undoAction == "restoreMove" ? e.item.renameDst :
		 OperationPlan::destinationPath(e.item.name, e.item.folder, j.record().request.destRoot,
					  j.record().request.preserve, e.item.omfEra));
	for (int attempts = 0; attempts < 999; ++attempts)
	{
		if (!save(j, e, Step::Publishing))
			return result(e, State::NeedsAttention,
						  "Journal failure; temporary file retained at " + e.temp);
		if (fail("publish"))
			return abandon(State::Failed, "Injected publication failure.");
		if (!destination->stillAt(e.temp, e.landed))
			return abandon(State::Failed, "The temporary file changed before publication.");
		const auto moved = destination->relocate(e.temp, e.dst, error);
		if (moved == OpFile::Relocation::Exists)
		{
			if (e.item.policy != "keepboth")
				return abandon(e.item.policy == "skip" ? State::Skipped : State::Failed,
					"The destination became occupied; source retained.");
			const auto next = OperationPlan::findKeepBothPath(originalDestination);
			if (!next)
				return abandon(State::Failed, "All Keep Both names are occupied.");
			e.dst = *next;
			continue;
		}
		if (moved != OpFile::Relocation::Moved)
		{
			keepArtifact(e, e.temp);
			e.error = error;
			save(j, e, Step::NeedsAttention);
			return result(e, State::NeedsAttention, error);
		}
		const auto oldTemp = e.temp;
		e.temp.clear();
		QString syncError;
		const auto publishedSync = fail("folder-sync") ? Sync::Failed :
			syncFolders({QFileInfo(e.dst).absolutePath(), QFileInfo(oldTemp).absolutePath()},
						syncError, hooks.directorySync);
		if (publishedSync == Sync::Failed)
		{
			e.copyDurable = false;
			e.error = "The published file's folder update could not be confirmed. Source retained.";
			if (!syncError.isEmpty())
				e.error += '\n' + syncError;
			save(j, e, Step::NeedsAttention);
			return result(e, State::NeedsAttention, e.error);
		}
		if (publishedSync == Sync::OkDegraded)
		{
			e.copyDurable = false;
			if (directoryDurable)
				e.error += '\n' + directoryWarning;
			e.error += '\n' + syncError;
		}
		if (!save(j, e, Step::Published))
			return result(e, State::NeedsAttention,
						  "The copy was published, but the journal failed. Source retained at " +
							  e.item.src);
		if (kind == OpKind::Move || e.undoAction == "restoreMove")
			return result(e, State::SourceRetained,
				"Copy ready; original kept until every required copy finishes.");
		if (!save(j, e, Step::Done))
			return result(e, State::NeedsAttention,
						  "The operation finished on disk, but the journal could not confirm "
						  "completion. Destination: " +
							  e.dst);
		return result(e, State::Completed, e.error);
	}
	return abandon(State::Failed, "Too many destination conflicts.");
}

OpResult OpRunner::execute(OpJournal &j, OpJournal::Entry &e, OpKind kind, int index, int total,
	bool *retryableCopy)
{
	if (retryableCopy)
		*retryableCopy = false;
	QString error;
	if (e.item.policy == "skip")
	{
		e.explicitSkip = true;
		if (!save(j, e, Step::Skipped))
			return result(e, State::NeedsAttention, j.error());
		return result(e, State::Skipped, "Skipped as requested.");
	}
	if (e.undoAction == "restoreTrash")
	{
		e.dst = e.item.renameDst;
		e.mechanism = "relocate";
		if (!save(j, e, Step::Relocating))
			return result(e, State::NeedsAttention, j.error());
		const auto restored = OpTrash::restore(e.item.trashReceipt, e.dst, e.source, m_cancel, e.item.src);
		if (restored.outcome != OpTrash::Outcome::Succeeded)
		{
			if (!restored.path.isEmpty() && restored.path != e.dst && restored.path != e.item.src)
				keepArtifact(e, restored.path);
			e.landed = restored.landed;
			e.trashReceipt = restored.receipt;
			e.error = restored.error;
			save(j, e, Step::NeedsAttention);
			return result(e, State::NeedsAttention, e.error);
		}
		e.landed = restored.landed;
		e.sourceRemoved = true;
		if (!save(j, e, Step::Done))
			return result(e, State::NeedsAttention, j.error());
		return result(e, State::Completed, "Restored from system Trash.", true);
	}
	auto source = OpFile::open(e.item.src, false, error);
	if (!source)
	{
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	const auto current = source->stamp();
	if (!e.source.valid() || !e.source.unchanged(current) ||
		(e.item.bytes >= 0 && e.item.bytes != current.size) ||
		(e.item.modifiedMs >= 0 &&
		 QFileInfo(e.item.src).lastModified().toMSecsSinceEpoch() != e.item.modifiedMs) ||
		!mediaIdentityMatches(e.item, *source, error) || !source->stillAt(e.item.src, current))
	{
		if (error.isEmpty())
			error =
				"The file changed since it was selected or journalled; rescan before proceeding.";
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	e.source = current;
	e.error.clear();
	const bool trash = kind == OpKind::Delete || e.item.maintenance ||
		e.undoAction == "discardCopy";
	if (trash && !e.item.maintenance && !hooks.forceNetworkTrash &&
		j.record().request.diagnosticTrashRoot.isEmpty() &&
		!OpTrash::isNetwork(e.item.src))
	{
		e.mechanism = "systemTrash";
		e.trashProvider = "system";
		if (!save(j, e, Step::Relocating))
			return result(e, State::NeedsAttention, j.error());
		source.reset();
		const auto trashed = OpTrash::move(e.item.src, e.source, m_cancel);
		checkpoint("system-trash-returned", e);
		if (trashed.outcome == OpTrash::Outcome::Succeeded)
		{
			e.dst = trashed.path;
			e.trashReceipt = trashed.receipt;
			e.landed = trashed.landed;
			e.sourceRemoved = true;
			if (!save(j, e, Step::Done))
				return result(e, State::NeedsAttention, "System Trash result needs recovery.");
			return result(e, State::Completed, "Moved to system Trash.", true);
		}
		if (trashed.outcome != OpTrash::Outcome::Unavailable)
		{
			e.dst = trashed.path;
			e.trashReceipt = trashed.receipt;
			e.landed = trashed.landed;
			e.error = trashed.error;
			const bool unchanged = e.source.unchanged(OpFile::inspect(e.item.src));
			const auto state = !unchanged ? State::NeedsAttention :
				(trashed.outcome == OpTrash::Outcome::Cancelled ? State::Cancelled : State::Failed);
			save(j, e, !unchanged ? Step::NeedsAttention :
				(state == State::Cancelled ? Step::Cancelled : Step::Failed));
			return result(e, state, e.error, !unchanged);
		}
		source = OpFile::open(e.item.src, false, error);
		if (!source || !source->stillAt(e.item.src, e.source))
			return result(e, State::NeedsAttention, "Original changed while checking Trash support.");
	}
	if (trash)
		e.trashProvider = "mediamuster";
	const auto trashFolder = j.record().request.diagnosticTrashRoot.isEmpty()
								 ? trashRoot(e.item.src)
								 : j.record().request.diagnosticTrashRoot;
	if (trash)
		e.dst = trashFolder + "/" + QFileInfo(j.path()).completeBaseName() + "/" +
				QString::number(e.id) + "/" + e.item.name;
	else if (kind == OpKind::Rename || !e.undoAction.isEmpty())
		e.dst = e.item.renameDst;
	else
		e.dst = OperationPlan::destinationPath(e.item.name, e.item.folder, j.record().request.destRoot,
							  j.record().request.preserve, e.item.omfEra);
	e.dst = OpJournal::canonicalPath(e.dst);
	const auto originalDestination = e.dst;
	if (!save(j, e, Step::Planned))
		return result(e, State::NeedsAttention, "Journal failure; source retained.");
	if (source->stillAt(e.item.src, e.source) && source->stillAt(e.dst, e.source))
	{
		keepArtifact(e, e.temp);
		e.temp.clear();
		e.hash.clear();
		e.mechanism.clear();
		e.copyDurable = false;
		e.metadataComplete = false;
		e.sourceRemoved = false;
		e.explicitSkip = false;
		e.landed = e.source;
		if (!save(j, e, Step::NoEffect))
			return result(e, State::NeedsAttention, j.error());
		return result(e, State::NoEffect, "Already at the destination; no file changes were needed.");
	}
	const auto destinationSync = OpFile::makeDirectory(QFileInfo(e.dst).absolutePath(), error,
														 hooks.directorySync);
	if (destinationSync == Sync::Failed)
	{
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	bool directoryDurable = destinationSync == Sync::Ok;
	if (OpFile::occupied(e.dst))
	{
		if (kind == OpKind::Rename)
		{
			e.error = "Rebalance stopped: a destination became occupied after the group check. "
					  "Rescan and replan.";
			save(j, e, Step::Failed);
			return result(e, State::Failed, e.error);
		}
		if (e.item.policy != "keepboth")
		{
			e.explicitSkip = e.item.policy == "skip";
			save(j, e, e.explicitSkip ? Step::Skipped : Step::Failed);
			return result(e, e.explicitSkip ? State::Skipped : State::Failed,
				"Destination occupied; source retained.");
		}
		const auto next = OperationPlan::findKeepBothPath(e.dst);
		if (!next)
		{
			e.error = "All Keep Both names are occupied.";
			save(j, e, Step::Failed);
			return result(e, State::Failed, e.error);
		}
		e.dst = *next;
	}
	bool canRelocate = (trash || kind == OpKind::Rename || e.undoAction == "restoreRelocate" ||
		(kind == OpKind::Move && !j.record().request.copyThenRemove)) &&
		(!hooks.forceCopy || trash || e.undoAction == "restoreRelocate") &&
		OperationPlan::sameVolumeForRename(e.item.src, e.dst);
	if (canRelocate)
	{
		const auto relocationSync = syncFolders(
			{QFileInfo(e.item.src).absolutePath(), QFileInfo(e.dst).absolutePath()},
			error, hooks.directorySync);
		if (relocationSync == Sync::Failed)
		{
			e.error = "Cannot prepare relocation; the source was retained.\n" + error;
			save(j, e, Step::Failed);
			return result(e, State::Failed, e.error);
		}
		canRelocate = directoryDurable && relocationSync == Sync::Ok;
	}
	if (canRelocate)
	{
		e.mechanism = "relocate";
		e.hash.clear();
		e.landed = {};
		e.copyDurable = false;
		e.metadataComplete = false;
		for (int attempts = 0; attempts < 999; ++attempts)
		{
			if (!save(j, e, Step::Relocating))
				return result(e, State::NeedsAttention, "Journal failure; relocation stopped.");
			if (!source->stillAt(e.item.src, e.source))
			{
				e.error = "The source changed before relocation; rescan before proceeding.";
				save(j, e, Step::Failed);
				return result(e, State::Failed, e.error);
			}
			if (fail("relocate"))
				error = "Injected relocation failure.";
			else
			{
				const auto moved = source->relocate(e.item.src, e.dst, error);
				if (moved == OpFile::Relocation::Exists)
				{
					if (kind == OpKind::Rename)
					{
						e.error = "Rebalance stopped: a destination became occupied. Completed "
								  "moves and remaining files are recorded.";
						save(j, e, Step::Failed);
						return result(e, State::Failed, e.error);
					}
					if (kind == OpKind::Move && e.item.policy == "keepboth")
					{
						const auto next = OperationPlan::findKeepBothPath(originalDestination);
						if (next)
						{
							e.dst = *next;
							continue;
						}
					}
					e.error = "Destination became occupied; source retained.";
					if (!save(j, e, Step::Failed))
						return result(e, State::NeedsAttention,
									  "Journal failure; source retained.");
					return result(e, State::Failed, e.error);
				}
				if (moved == OpFile::Relocation::Moved)
				{
					e.landed = source->stamp();
					if (!e.source.unchanged(e.landed))
					{
						e.error = "The relocated file changed during the operation. Inspect " +
								  e.dst + "; its journal is retained.";
						save(j, e, Step::NeedsAttention);
						return result(e, State::NeedsAttention, e.error);
					}
					QString syncError;
					if (fail("folder-sync") ||
						syncFolders({QFileInfo(e.dst).absolutePath(),
									 QFileInfo(e.item.src).absolutePath()}, syncError,
									hooks.directorySync) != Sync::Ok)
					{
						e.error = "File relocated to " + e.dst +
								  ", but folder durability needs recovery confirmation.";
						if (!syncError.isEmpty())
							e.error += '\n' + syncError;
						save(j, e, Step::NeedsAttention);
						return result(e, State::NeedsAttention, e.error);
					}
					e.sourceRemoved = true;
					if (!save(j, e, Step::Done))
						return result(e, State::NeedsAttention,
									  "Relocated to " + e.dst + "; journal completion failed.");
					return result(e, State::Completed,
								  trash ? "Moved to MediaMuster Trash." : QString(), true);
				}
				// A lost reply or a changed source must be reconciled, not retried
				// as a copy that might act on another file at the source pathname.
				if (moved != OpFile::Relocation::CrossVolume)
				{
					e.error = error;
					save(j, e, Step::NeedsAttention);
					return result(e, State::NeedsAttention, error);
				}
			}
			if (fail("relocate"))
			{
				e.error = error;
				save(j, e, Step::Failed);
				return result(e, State::Failed, error);
			}
			break;
		}
	}
	if (trash || kind == OpKind::Rename || e.undoAction == "restoreRelocate" ||
		(kind == OpKind::Move && !j.record().request.copyThenRemove))
	{
		e.error = "Safe same-filesystem relocation is unavailable. The source was retained." +
				  (error.isEmpty() ? QString() : '\n' + error);
		save(j, e, Step::Failed);
		return result(e, State::Failed, e.error);
	}
	return transfer(j, e, kind, *source, index, total, directoryDurable, retryableCopy);
}

OpResult OpRunner::removeOriginal(OpJournal &j, OpJournal::Entry &e, int index, int total)
{
	QString error;
	if (!j.record().copiesComplete || !e.copyDurable || !e.metadataComplete ||
		!copiedDestinationMatches(e, error, &m_cancel))
		return result(e, State::NeedsAttention,
			error.isEmpty() ? "The completed copy is not ready for original removal." : error);
	if (m_cancel.load())
		return result(e, State::SourceRetained, "Cancelled; original retained.");
	m_sink.progress("Removing originals: " + label(e.item), index, total, 0);
	if (e.retirement.isEmpty() || (OpFile::occupied(e.item.src) &&
		!OpFile::occupied(e.retirement) && OpFile::occupied(QFileInfo(e.retirement).absolutePath())))
	{
		if (!e.retirement.isEmpty())
			keepArtifact(e, QFileInfo(e.retirement).absolutePath());
		e.retirement = QFileInfo(e.item.src).absolutePath() +
			"/.mediamuster-retire-" + unique() + "/payload.retired";
		if (!save(j, e, Step::RemovingSource))
			return result(e, State::NeedsAttention, j.error());
	}
	const auto privateDir = QFileInfo(e.retirement).absolutePath();
	const bool atOriginal = OpFile::occupied(e.item.src);
	const bool atRetirement = OpFile::occupied(e.retirement);
	if (!atOriginal && !atRetirement)
	{
		// Recorded removal intent and a completed copy survive a lost final append.
		if (syncFolders({privateDir, QFileInfo(e.item.src).absolutePath()}, error,
			hooks.directorySync) != Sync::Ok)
			return result(e, State::NeedsAttention, "Original removal durability needs recovery. " + error, true);
		e.sourceRemoved = true;
		if (!save(j, e, Step::SourceRemoved))
			return result(e, State::NeedsAttention, j.error());
		return result(e, State::Completed, {}, true);
	}
	if (atOriginal && atRetirement)
		return result(e, State::NeedsAttention, "Both original and retirement paths are occupied.");
	const auto from = atOriginal ? e.item.src : e.retirement;
	auto original = OpFile::open(from, false, error);
	if (!original || !original->stillAt(from, e.source))
		return result(e, State::NeedsAttention, "Original changed before removal: " + from);
	if (atOriginal)
	{
		// A private fresh directory makes removal of the captured object possible
		// without a check-then-unlink race on the original shared pathname.
		if (OpFile::occupied(privateDir) || !QDir().mkdir(privateDir) ||
			!QFile::setPermissions(privateDir, QFileDevice::ReadOwner |
				QFileDevice::WriteOwner | QFileDevice::ExeOwner) ||
			syncFolders({privateDir, QFileInfo(e.item.src).absolutePath()}, error,
				hooks.directorySync) != Sync::Ok)
			return result(e, State::NeedsAttention, "Cannot prepare original removal. " + error);
		checkpoint("before-source-retirement", e);
		if (!original->stillAt(e.item.src, e.source) ||
			original->relocate(e.item.src, e.retirement, error) != OpFile::Relocation::Moved)
			return result(e, State::NeedsAttention, "Original removal needs recovery. " + error);
		if (!save(j, e, Step::RemovingSource))
			return result(e, State::NeedsAttention, j.error());
		checkpoint("source-retired", e);
	}
	if (m_cancel.load())
		return result(e, State::SourceRetained, "Cancelled; original retained at " + e.retirement, true);
	if (fail("remove-original") || !original->removeOriginal(error))
		return result(e, State::NeedsAttention, "Could not confirm original removal. " + error, true);
	checkpoint("source-unlinked", e);
	if (syncFolders({privateDir, QFileInfo(e.item.src).absolutePath()}, error,
		hooks.directorySync) != Sync::Ok)
		return result(e, State::NeedsAttention, "Original removal durability needs recovery. " + error, true);
	e.sourceRemoved = true;
	if (!save(j, e, Step::SourceRemoved))
		return result(e, State::NeedsAttention, j.error());
	return result(e, State::Completed, {}, true);
}

OpRequest OpRunner::planUndo(OpJournal::Record &forward, const OpRequest &input)
{
	if (!input.undoEnabled)
		throw std::runtime_error("Enable Undo in the Debug menu before starting an Undo.");
	if (!forward.undoPath.isEmpty() || forward.request.kind == OpKind::Undo || forward.corrupt)
		throw std::runtime_error("This job cannot start another Undo.");
	QString error;
	if (!OpJournal::resolve(forward, error))
		throw std::runtime_error(error.toStdString());
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoOf = forward.path;
	undo.verifyCopies = forward.request.verifyCopies;
	undo.diagnosticTrashRoot = input.diagnosticTrashRoot.isEmpty() ?
		forward.request.diagnosticTrashRoot : input.diagnosticTrashRoot;
	QVector<OpItem> discards;
	auto add = [&](const OpJournal::Entry &e, const QString &src, const QString &dst,
		const OpStamp &stamp, const QString &action)
	{
		if (!stamp.valid())
			throw std::runtime_error("An Undo object has no saved identity.");
		OpItem item;
		item.src = src;
		item.renameDst = dst;
		item.name = QFileInfo(src).fileName();
		item.bytes = stamp.size;
		item.expectedFileId = stamp.fileId;
		item.expectedVolumeId = stamp.volumeId;
		item.expectedModified = stamp.modified;
		item.undoAction = action;
		item.undoEntryId = e.id;
		item.trashReceipt = e.trashReceipt;
		if (action == "discardCopy")
			discards.append(item);
		else
			undo.items.append(item);
	};
	for (const auto &e : forward.entries)
	{
		if (e.item.maintenance || e.step == Step::Skipped || e.step == Step::NoEffect)
			continue;
		if (e.mechanism == "systemTrash")
		{
			if (!e.trashReceipt.isEmpty() && e.landed.unchanged(OpFile::inspect(e.dst)) &&
				!OpFile::occupied(e.item.src))
				add(e, e.dst, e.item.src, e.landed, "restoreTrash");
			else if (e.step == Step::Relocating || e.step == Step::NeedsAttention)
				throw std::runtime_error("An interrupted system Trash action needs inspection before Undo.");
			continue;
		}
		if (e.mechanism == "relocate")
		{
			if (e.source.unchanged(OpFile::inspect(e.dst)) && !OpFile::occupied(e.item.src))
				add(e, e.dst, e.item.src, e.source, "restoreRelocate");
			else if (e.step == Step::Done || e.step == Step::NeedsAttention || e.step == Step::Relocating)
			{
				if (!e.source.unchanged(OpFile::inspect(e.item.src)) || OpFile::occupied(e.dst))
					throw std::runtime_error("A relocated file changed or its original location is occupied.");
			}
			continue;
		}
		if (e.mechanism != "copy" || !e.landed.valid() ||
			(e.step != Step::Published && e.step != Step::Publishing && e.step != Step::Done &&
			 e.step != Step::SourceRetained && e.step != Step::RemovingSource &&
			 e.step != Step::SourceRemoved && e.step != Step::NeedsAttention))
			continue;
		if (!copiedDestinationMatches(e, error, &m_cancel))
		{
			if ((e.step == Step::Publishing || e.step == Step::NeedsAttention) &&
				e.landed.unchanged(OpFile::inspect(e.temp)))
				continue; // An unpublished artifact is retained, not invented as a completed copy.
			throw std::runtime_error(error.toStdString());
		}
		if (forward.request.kind == OpKind::Copy)
			add(e, e.dst, {}, e.landed, "discardCopy");
		else if (e.source.unchanged(OpFile::inspect(e.item.src)))
			add(e, e.dst, {}, e.landed, "discardCopy");
		else if (!e.retirement.isEmpty() && e.source.unchanged(OpFile::inspect(e.retirement)) &&
			!OpFile::occupied(e.item.src))
		{
			add(e, e.retirement, e.item.src, e.source, "restoreRelocate");
			add(e, e.dst, {}, e.landed, "discardCopy");
		}
		else if (!OpFile::occupied(e.item.src) && (e.sourceRemoved || e.step == Step::RemovingSource))
			add(e, e.dst, e.item.src, e.landed, "restoreMove");
		else
			throw std::runtime_error("An original changed or disappeared without a removal record; Undo stopped.");
	}
	undo.items += discards; // Restore originals before discarding any redundant results.
	if (undo.items.isEmpty())
		throw std::runtime_error("This job has no completed work that can be undone.");
	return undo;
}

bool OpRunner::retireDatabases(OpJournal &journal, const QSet<QString> &folders, QString &error)
{
	for (const auto &folder : folders)
		for (const auto &name : {QStringLiteral("msmMMOB.mdb"), QStringLiteral("msmFMID.pmr")})
		{
			const auto path = folder + '/' + name;
			if (!OpFile::occupied(path))
				continue;
			OpJournal::Entry entry;
			for (const auto &saved : journal.record().entries)
				if (saved.item.maintenance && saved.item.src == path && !saved.complete())
				{
					entry = saved;
					break;
				}
			if (entry.id < 0)
			{
				entry.id = journal.record().entries.size();
				entry.item.src = path;
				entry.item.name = name;
				entry.item.bytes = QFileInfo(path).size();
				entry.item.maintenance = true;
				entry.originalSource = path;
				entry.source = OpFile::inspect(path);
				entry.originalVolume = VolumeIdentity::capture(path);
				entry.originalRelativePath =
					QDir(entry.originalVolume.rootPath).relativeFilePath(path);
				if (!save(journal, entry, Step::Planned))
				{
					error = journal.error();
					return false;
				}
			}
			if (!journal.touchFolder(folder))
			{
				error = journal.error();
				return false;
			}
			const auto outcome = execute(journal, entry, OpKind::Delete, 0, 0);
			if (outcome.state != State::Completed)
			{
				error = "Avid database retirement stopped: " + outcome.message;
				return false;
			}
		}
	return true;
}

OpRunner::Totals OpRunner::run(const OpRequest &input, const QString &directory)
{
	Totals totals;
	QString error;
	QHash<QString, int> trashCounts;
	const auto ownerPath = input.resumeJournalPath.isEmpty() ? input.undoJournalPath : input.resumeJournalPath;
	const auto lockDirectory = ownerPath.isEmpty() ? directory : QFileInfo(ownerPath).absolutePath();
	auto lock = OpJournal::acquire(lockDirectory, error);
	if (!lock)
	{
		m_sink.log(QtCriticalMsg, error);
		totals.failed = 1;
		return totals;
	}
	OpRequest request = input;
	OpJournal journal;
	QSet<int> failedReconciliation;
	try
	{
		if (!request.resumeJournalPath.isEmpty())
		{
			const auto saved = OpJournal::readOne(request.resumeJournalPath);
			if (!saved)
				throw std::runtime_error("Cannot read the requested operation journal.");
			auto rec = *saved;
			if (rec.dismissed || !rec.undoPath.isEmpty())
				throw std::runtime_error("This job was abandoned or has already started Undo.");
			for (const auto &other : OpJournal::scan(lockDirectory))
				if (other.request.undoOf == rec.path)
					throw std::runtime_error("This job has already started Undo.");
			if (!OpJournal::resolve(rec, error) || !journal.resume(rec, error))
				throw std::runtime_error(error.toStdString());
			request = rec.request;
			if (!request.undoOf.isEmpty())
			{
				auto original = OpJournal::readOne(request.undoOf);
				if (!original || (!original->undoPath.isEmpty() && original->undoPath != journal.path()))
					throw std::runtime_error("Cannot confirm which job owns this Undo.");
				OpJournal claimed;
				if (!claimed.resume(*original, error) || !claimed.claimUndo(journal.path()))
					throw std::runtime_error("Cannot save ownership of the interrupted Undo.");
			}
			for (auto e : rec.entries)
				if (!e.complete() && e.step != Step::Planned)
				{
					if (!reconcile(journal, e, error, &m_cancel, hooks.directorySync))
					{
						const bool beforePublication = e.step == Step::Failed || e.step == Step::Cancelled ||
							e.step == Step::Copying || e.step == Step::CopyReady || e.step == Step::Verified;
						if ((request.kind != OpKind::Copy && request.kind != OpKind::Move) ||
							!beforePublication || !journal.healthy() || m_cancel.load())
							throw std::runtime_error(error.toStdString());
						e.error = error;
						if (!save(journal, e, Step::Failed))
							throw std::runtime_error(journal.error().toStdString());
						failedReconciliation.insert(e.id);
						++totals.failed;
						m_sink.result(result(e, State::Failed, error));
					}
				}
		}
		else
		{
			for (const auto &pending : OpJournal::interrupted(lockDirectory))
				if (request.kind != OpKind::Undo || pending.path != request.undoJournalPath)
					throw std::runtime_error("The previous job was interrupted. Resume or cancel it first.");
			std::optional<OpJournal::Record> undoOriginal;
			if (request.kind == OpKind::Undo)
			{
				undoOriginal = OpJournal::latestUndoable(lockDirectory);
				if (!undoOriginal || undoOriginal->path != request.undoJournalPath)
					throw std::runtime_error("Only the most recent eligible job can be undone.");
				request = planUndo(*undoOriginal, input);
			}
			if ((request.kind == OpKind::Copy || request.kind == OpKind::Move) &&
				!QDir::isAbsolutePath(request.destRoot))
				throw std::runtime_error("Choose an absolute destination folder before starting.");
			request.destRoot = OpJournal::canonicalPath(request.destRoot);
			request.diagnosticTrashRoot = OpJournal::canonicalPath(request.diagnosticTrashRoot);
			QSet<QString> batchDestinations;
			for (auto &i : request.items)
			{
				if (!QDir::isAbsolutePath(i.src) ||
					(request.kind == OpKind::Rename && !QDir::isAbsolutePath(i.renameDst)))
					throw std::runtime_error("The source and relocation paths must be absolute.");
				i.src = OpJournal::canonicalPath(i.src);
				i.renameDst = OpJournal::canonicalPath(i.renameDst);
				if (i.name.isEmpty())
					i.name = QFileInfo(i.src).fileName();
				if (!leaf(i.name) || (!i.folder.isEmpty() && !leaf(i.folder)) ||
					(!i.policy.isEmpty() && i.policy != "keepboth" && i.policy != "skip"))
					throw std::runtime_error(
						"Unsupported name, folder or conflict policy. Replace is not supported.");
				if (request.kind == OpKind::Copy || request.kind == OpKind::Move)
				{
					const auto key = PathKey::normalise(OperationPlan::destinationPath(
						i.name, i.folder, request.destRoot, request.preserve, i.omfEra));
					if (i.policy.isEmpty() && batchDestinations.contains(key))
						i.policy = "keepboth";
					batchDestinations.insert(key);
				}
			}
			if (request.kind == OpKind::Move)
			{
				auto canRelocate = [&](const QString &source, const QString &destination)
				{
					QString parent = QFileInfo(destination).absolutePath();
					while (!QFileInfo::exists(parent) && QFileInfo(parent).absolutePath() != parent)
						parent = QFileInfo(parent).absolutePath();
					return OperationPlan::sameVolumeForRename(source, destination) &&
						hooks.directorySync(QFileInfo(source).absolutePath(), &error) != Sync::OkDegraded &&
						hooks.directorySync(parent, &error) != Sync::OkDegraded;
				};
				request.copyThenRemove = OperationPlan::assessCopyMove(
					request, canRelocate, hooks.forceCopy).copyThenRemove;
			}

			if (!journal.create(request, lockDirectory, error))
				throw std::runtime_error(error.toStdString());
			if (undoOriginal)
			{
				OpJournal claimed;
				if (!claimed.resume(*undoOriginal, error) || !claimed.claimUndo(journal.path()))
					throw std::runtime_error("Undo was saved, but original ownership needs recovery.");
			}
		}
		auto entries = journal.record().entries;
		bool undoRebalance = false;
		if (request.kind == OpKind::Undo)
		{
			const auto forward = OpJournal::readOne(request.undoOf);
			undoRebalance = forward && forward->request.kind == OpKind::Rename;
		}
		QString group;
		QSet<QString> touched;
		int mediaIndex = 0, mediaTotal = 0;
		QSet<int> deferred;
		QVector<int> discards;
		for (const auto &e : entries)
			if (!e.item.maintenance)
				++mediaTotal;
		const bool twoStage = request.copyThenRemove || std::any_of(entries.cbegin(), entries.cend(),
			[](const auto &e) { return e.undoAction == "restoreMove"; });
		const int workTotal = twoStage ? mediaTotal * 2 : mediaTotal;
		for (int n = 0; n < entries.size(); ++n)
		{
			auto e = journal.record().entries[n];
			if (!e.item.maintenance)
				++mediaIndex;
			if (e.complete() || failedReconciliation.contains(e.id))
				continue;
			if (!journal.healthy())
				break;
			if (e.undoAction == "discardCopy")
			{
				discards.append(n);
				continue;
			}
			if (removesAfterCopy(e, request) && (e.step == Step::Published ||
				e.step == Step::SourceRetained || e.step == Step::RemovingSource))
			{
				deferred.insert(n);
				continue;
			}
			const bool startsGroup = e.item.groupKey.isEmpty() || e.item.groupKey != group;
			if (startsGroup)
			{
				if (m_cancel.load())
				{
					totals.cancelled = true;
					break;
				}
				group = e.item.groupKey;
				if (request.kind == OpKind::Rename && !e.item.maintenance)
				{
					bool conflict = false;
					QSet<QString> destinations;
					QHash<QString, int> incoming;
					int end = n;
					while (end < entries.size() && entries[end].item.groupKey == group)
					{
						const auto &candidate = entries[end];
						const auto key = PathKey::normalise(candidate.item.renameDst);
						if (!candidate.complete() && (OpFile::occupied(candidate.item.renameDst) ||
													  destinations.contains(key)))
							conflict = true;
						destinations.insert(key);
						++end;
						if (!candidate.complete() &&
							Conventions::countsAsEssenceName(candidate.item.name))
							++incoming[QFileInfo(candidate.item.renameDst).absolutePath()];
						if (group.isEmpty())
							break;
					}
					bool full = false;
					for (auto it = incoming.cbegin(); it != incoming.cend(); ++it)
					{
						int count = 0;
						for (const auto &name : QDir(it.key()).entryList(
								 QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot))
							if (Conventions::countsAsEssenceName(name))
								++count;
						if (count + it.value() > Conventions::kFolderTarget)
							full = true;
					}
					if (conflict || full)
					{
						for (int k = n; k < end; ++k)
							if (!entries[k].complete())
							{
								auto skipped = entries[k];
								if (!save(journal, skipped, Step::Skipped))
									throw std::runtime_error(journal.error().toStdString());
								++totals.skipped;
								m_sink.result(result(
									skipped, State::Skipped,
									full ? "Rebalance group skipped: a destination folder no "
										   "longer has room below 5,000 files. Rescan and replan."
										 : "Rebalance group skipped: a destination is occupied. "
										   "Rescan and replan."));
							}
						n = end - 1;
						continue;
					}
				}
			}
			if (startsGroup && request.kind == OpKind::Rename && !e.item.maintenance)
			{
				QSet<QString> folders;
				for (int k = n; k < entries.size(); ++k)
				{
					if (k > n && (group.isEmpty() || entries[k].item.groupKey != group))
						break;
					if (journal.record().entries[k].complete())
						continue;
					folders << QFileInfo(entries[k].item.src).absolutePath()
							<< QFileInfo(entries[k].item.renameDst).absolutePath();
				}
				// Check the whole group before retiring any Avid database or media.
				for (const auto &folder : folders)
					if (OpFile::makeDirectory(folder, error, hooks.directorySync) != Sync::Ok)
						throw std::runtime_error(("Rebalance unavailable; source files retained.\n" +
												  error).toStdString());
				if (syncFolders(folders.values(), error, hooks.directorySync) != Sync::Ok)
					throw std::runtime_error(("Rebalance unavailable; source files retained.\n" +
											  error).toStdString());
				if (!retireDatabases(journal, folders, error))
					throw std::runtime_error(error.toStdString());
			}

			if (e.step != Step::Planned)
			{
				if (!reconcile(journal, e, error, &m_cancel, hooks.directorySync))
				{
					m_sink.result(result(e, State::NeedsAttention, error));
					++totals.needsAttention;
					break;
				}
				if (e.complete())
					continue;
			}
			m_sink.progress(label(e.item), mediaIndex, workTotal, 0);
			if (undoRebalance && e.undoAction == "restoreRelocate" && !e.item.maintenance)
			{
				QSet<QString> folders{QFileInfo(e.item.src).absolutePath(),
					QFileInfo(e.item.renameDst).absolutePath()};
				if (!retireDatabases(journal, folders, error))
					throw std::runtime_error(error.toStdString());
			}
			if (request.kind == OpKind::Rename && !e.item.maintenance)
			{
				if (!journal.touchFolder(QFileInfo(e.item.src).absolutePath()) ||
					!journal.touchFolder(QFileInfo(e.item.renameDst).absolutePath()))
					throw std::runtime_error(journal.error().toStdString());
			}
			bool retryableCopy = false;
			auto outcome = execute(journal, e, request.kind, mediaIndex, workTotal, &retryableCopy);
			for (int retry = 0; retry < 2 && retryableCopy && outcome.state == State::Failed &&
				e.step == Step::Failed && journal.healthy() && !m_cancel.load() &&
				(request.kind == OpKind::Copy || request.kind == OpKind::Move); ++retry)
			{
				if (!reconcile(journal, e, error, &m_cancel, hooks.directorySync))
				{
					outcome = result(e, State::NeedsAttention, error);
					break;
				}
				m_sink.log(QtInfoMsg, "Retrying " + label(e.item));
				checkpoint("before-copy-retry", e);
				for (int remaining = 250 * (retry + 1); remaining > 0 && !m_cancel.load(); remaining -= 25)
					QThread::msleep(25);
				if (m_cancel.load())
				{
					e.error = "Cancelled before retrying the copy.";
					const auto state = save(journal, e, Step::Cancelled) ? State::Cancelled : State::NeedsAttention;
					outcome = result(e, state, e.error);
					break;
				}
				outcome = execute(journal, e, request.kind, mediaIndex, workTotal, &retryableCopy);
			}
			if (outcome.state == State::SourceRetained && removesAfterCopy(e, request))
			{
				deferred.insert(n);
				if (m_cancel.load())
				{
					totals.cancelled = true;
					break;
				}
				continue;
			}
			if (!e.item.maintenance)
				m_sink.result(outcome);
			else if (outcome.state != State::Completed)
				m_sink.log(QtCriticalMsg, "Avid database relocation stopped: " + outcome.message);
			if (outcome.state == State::Completed)
			{
				if (!e.item.maintenance)
					++totals.succeeded;
				if (request.kind == OpKind::Delete && !e.item.maintenance && e.trashProvider == "mediamuster")
				{
					QDir trash(QFileInfo(e.dst).absolutePath());
					trash.cdUp();
					trash.cdUp();
					++trashCounts[trash.path()];
				}
				if (request.kind == OpKind::Rename && !e.item.maintenance)
					for (const auto &folder :
						 {QFileInfo(e.item.src).absolutePath(), QFileInfo(e.dst).absolutePath()})
						if (!touched.contains(folder))
						{
							touched.insert(folder);
							if (onRenameFolderTouched)
								onRenameFolderTouched(folder);
						}
			}
			else if (outcome.state == State::NoEffect)
				++totals.unchanged;
			else if (outcome.state == State::SourceRetained)
				++totals.retained;
			else if (outcome.state == State::Skipped)
				++totals.skipped;
			else if (outcome.state == State::Cancelled)
			{
				totals.cancelled = true;
				break;
			}
			else
			{
				if (outcome.state == State::NeedsAttention)
					++totals.needsAttention;
				else
					++totals.failed;
				if (outcome.state == State::NeedsAttention || request.kind == OpKind::Rename ||
					request.kind == OpKind::Undo || !journal.healthy())
					break;
			}
		}
		bool ready = journal.healthy() && !totals.failed && !totals.needsAttention &&
			!totals.cancelled && !m_cancel.load();
		for (const auto &e : journal.record().entries)
		{
			if (e.item.maintenance || e.undoAction == "discardCopy" || e.step == Step::NoEffect)
				continue;
			if (e.step == Step::Skipped)
			{
				if (!e.explicitSkip && request.kind == OpKind::Move)
					ready = false;
				continue;
			}
			if (e.complete())
			{
				if ((request.kind == OpKind::Move || request.kind == OpKind::Undo) &&
					e.mechanism == "copy" && !copiedDestinationMatches(e, error, &m_cancel))
					ready = false;
				if (request.kind == OpKind::Undo && e.undoAction.startsWith("restore") &&
					!e.landed.unchanged(OpFile::inspect(e.dst)))
					ready = false;
				continue;
			}
			if (!removesAfterCopy(e, request) ||
				(e.step != Step::Published && e.step != Step::SourceRetained && e.step != Step::RemovingSource) ||
				!e.copyDurable || !e.metadataComplete || !copiedDestinationMatches(e, error, &m_cancel))
				ready = false;
			if (e.retirement.isEmpty() && !e.source.unchanged(OpFile::inspect(e.item.src)))
				ready = false;
		}
		if (ready && !deferred.isEmpty() && !journal.record().copiesComplete)
		{
			if (!journal.markCopiesComplete())
				throw std::runtime_error(journal.error().toStdString());
			checkpoint("copies-complete", journal.record().entries[*deferred.cbegin()]);
		}
		for (int n = 0; n < journal.record().entries.size(); ++n)
		{
			if (!deferred.contains(n))
				continue;
			auto e = journal.record().entries[n];
			auto outcome = ready && !m_cancel.load() ? removeOriginal(journal, e, mediaTotal + n + 1, workTotal) :
				result(e, State::SourceRetained,
					"Original retained because the job's required copies have not all completed safely." +
					(e.error.isEmpty() ? QString() : '\n' + e.error));
			m_sink.result(outcome);
			if (outcome.state == State::Completed)
				++totals.succeeded;
			else if (outcome.state == State::SourceRetained)
				++totals.retained;
			else
			{
				++totals.needsAttention;
				ready = false;
			}
		}
		// Undo of copied-only portions comes last, after all restoration work.
		// Re-read its forward evidence: a redundant Move copy is discardable only
		// while the original or the inverse's restored replacement still exists.
		std::optional<OpJournal::Record> undoOriginal;
		if (!discards.isEmpty())
		{
			undoOriginal = OpJournal::readOne(request.undoOf);
			if (!undoOriginal || !OpJournal::resolve(*undoOriginal, error))
				throw std::runtime_error("Cannot confirm original locations before finishing Undo.");
		}
		for (const auto n : discards)
		{
			if (!ready || m_cancel.load() || !journal.healthy())
				break;
			auto e = journal.record().entries[n];
			if (undoOriginal->request.kind == OpKind::Move)
			{
				bool restored = false;
				for (const auto &inverse : journal.record().entries)
					if (inverse.undoEntryId == e.undoEntryId && inverse.undoAction.startsWith("restore") &&
						inverse.complete() && inverse.landed.unchanged(OpFile::inspect(inverse.dst)))
						restored = true;
				if (!restored && e.undoEntryId >= 0 && e.undoEntryId < undoOriginal->entries.size())
				{
					const auto &original = undoOriginal->entries[e.undoEntryId];
					restored = original.source.unchanged(OpFile::inspect(original.item.src));
				}
				if (!restored)
					throw std::runtime_error("A restored original changed; its remaining copy was retained.");
			}
			if (e.step != Step::Planned && !reconcile(journal, e, error, &m_cancel, hooks.directorySync))
			{
				++totals.needsAttention;
				m_sink.result(result(e, State::NeedsAttention, error));
				break;
			}
			if (e.complete())
				continue;
			const auto outcome = execute(journal, e, OpKind::Undo, n + 1, mediaTotal);
			m_sink.result(outcome);
			if (outcome.state == State::Completed)
				++totals.succeeded;
			else if (outcome.state == State::NoEffect)
				++totals.unchanged;
			else
			{
				++totals.needsAttention;
				break;
			}
		}
		totals.cancelled = totals.cancelled || m_cancel.load();
		if (!journal.finish(totals.cancelled || m_cancel.load()))
		{
			++totals.needsAttention;
			m_sink.log(QtCriticalMsg, journal.error());
		}
	}
	catch (const std::exception &e)
	{
		++totals.needsAttention;
		m_sink.log(QtCriticalMsg,
				   QStringLiteral("Operation stopped: %1. Journal and files retained at %2.")
					   .arg(QString::fromUtf8(e.what()), journal.path()));
	}
	totals.cancelled = totals.cancelled || m_cancel.load();
	m_sink.log(totals.failed || totals.needsAttention ? QtWarningMsg : QtInfoMsg,
		QStringLiteral("%1: %2 completed, %3 unchanged, %4 source retained, %5 skipped, %6 failed, %7 "
			"need attention%8. Journal: %9")
			.arg(opKindName(request.kind))
			.arg(totals.succeeded)
			.arg(totals.unchanged)
			.arg(totals.retained)
			.arg(totals.skipped)
			.arg(totals.failed)
			.arg(totals.needsAttention)
			.arg(totals.cancelled ? QStringLiteral("; cancelled") : QString())
			.arg(journal.path()));
	for (auto it = trashCounts.cbegin(); it != trashCounts.cend(); ++it)
		m_sink.trashUsed(it.key(), it.value());
	return totals;
}
