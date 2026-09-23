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
		return {s, label(e.item), e.item.src, e.dst, message, removed || e.sourceRemoved || (!e.retirement.isEmpty() && !OpFile::occupied(e.item.src) && e.source.unchanged(OpFile::inspect(e.retirement)))};
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
	bool copiedDestinationUnchanged(const OpJournal::Entry &e, QString &error)
	{
		if (!e.landed.unchanged(OpFile::inspect(e.dst)))
		{
			error = "The completed destination is missing or changed: " + e.dst;
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
						 const NativeFile::DirectorySync &directorySync)
{
	if (e.complete())
		return true;
	if (e.step == Step::RestoringSource)
	{
		// A crash after the restoring rename must never become permission to
		// remove the original again. This path does not depend on the copy.
		if (!e.source.unchanged(OpFile::inspect(e.item.src)) || OpFile::occupied(e.retirement))
		{
			error = "Original restoration is pending: " + e.retirement + " -> " + e.item.src;
			return false;
		}
		QStringList folders{QFileInfo(e.item.src).absolutePath()};
		if (OpFile::occupied(QFileInfo(e.retirement).absolutePath()))
			folders.append(QFileInfo(e.retirement).absolutePath());
		if (syncFolders(folders, error, directorySync) != Sync::Ok)
			return false;
		e.sourceRemoved = false;
		e.error.clear();
		e.step = Step::SourceRestored;
		return j.save(e);
	}
	if (e.step == Step::TrashFallback)
	{
		// The native call explicitly refused before moving anything. Recovery
		// can verify this state, but only Resume may ask/perform the fallback.
		auto original = OpFile::open(e.item.src, false, error);
		if (!original || !original->stillAt(e.item.src, e.source))
		{
			error = "The original changed while awaiting a Trash choice; no fallback was attempted. " + error;
			return false;
		}
		return true;
	}
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
		if (!j.record().copiesComplete || !copiedDestinationUnchanged(e, error))
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
							 QFileInfo(e.retirement).absolutePath()},
							error) != Sync::Ok)
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
							 QFileInfo(e.item.src).absolutePath()},
							error) != Sync::Ok)
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
		e.step == Step::Copying || ((e.step == Step::CopyReady || e.step == Step::Publishing) && OpFile::occupied(e.temp)))
	{
		keepArtifact(e, e.temp);
		if (!e.temp.isEmpty())
		{
			const auto directory = QFileInfo(e.temp).absolutePath();
			if (std::none_of(e.cleanup.cbegin(), e.cleanup.cend(), [&](const auto &pending)
							 { return pending.directory == directory; }))
				keepArtifact(e, directory); // Creation may have preceded the identity append.
		}
		if (!e.source.unchanged(OpFile::inspect(e.item.src)))
		{
			error = "The original file is missing or changed; its recovery record was retained: " +
					e.item.src;
			return false;
		}
		e.step = Step::Planned;
		return j.save(e);
	}
	if (e.mechanism == "copy" && copiedDestinationUnchanged(e, error))
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

bool OpRunner::cleanup(OpJournal &j, OpJournal::Entry &e, QString &error, const Hooks *hooks)
{
	const auto sync = hooks ? hooks->directorySync : NativeFile::DirectorySync(NativeFile::syncDirectory);
	const auto point = [&](const QString &name)
	{
		if (hooks && hooks->checkpoint)
			hooks->checkpoint(name, e);
	};
	QStringList errors;
	for (qsizetype n = 0; n < e.cleanup.size();)
	{
		auto &pending = e.cleanup[n];
		QString detail;
		const auto retain = [&](const QString &why)
		{
			const auto path = pending.file.isEmpty() ? pending.directory : pending.file;
			keepArtifact(e, path);
			errors.append("Temporary cleanup pending at " + path + ": " + why);
		};
		if ((!e.retirement.isEmpty() && pending.directory == QFileInfo(e.retirement).absolutePath() &&
			 e.step != Step::SourceRemoved && e.step != Step::SourceRestored && e.step != Step::Done) ||
			(!pending.file.isEmpty() && !pending.removeFile &&
			 (e.step == Step::Publishing || e.step == Step::NeedsAttention)))
		{
			retain("The interrupted file operation must be reconciled before its folder is removed.");
			++n;
			continue;
		}
		if (hooks && hooks->fail && hooks->fail("cleanup"))
		{
			retain("Injected cleanup failure.");
			++n;
			continue;
		}
		if (!OpFile::occupied(pending.directory))
		{
			// The remove may have reached disk before its completion append.
			if (!OpFile::safePath(pending.directory) ||
				sync(QFileInfo(pending.directory).absolutePath(), &detail) != Sync::Ok)
			{
				retain("Cannot confirm the folder removal. " + detail);
				++n;
				continue;
			}
		}
		else
		{
			if (!pending.directoryStamp.sameObject(OpFile::inspectDirectory(pending.directory)))
			{
				retain("The folder identity changed; it was retained.");
				++n;
				continue;
			}
			if (!pending.file.isEmpty() && OpFile::occupied(pending.file))
			{
				const bool unpublished = e.step == Step::Planned || e.step == Step::Copying ||
										 e.step == Step::CopyReady || e.step == Step::Failed ||
										 e.step == Step::Cancelled || e.step == Step::Skipped;
				const bool originalSafe = e.source.unchanged(OpFile::inspect(e.item.src));
				const bool copySafe = e.mechanism == "copy" && e.complete() &&
									  e.landed.unchanged(OpFile::inspect(e.dst));
				auto partial = OpFile::open(pending.file, false, detail);
				if (!partial || (!originalSafe && !copySafe) ||
					!pending.fileStamp.sameObject(partial->stamp()) ||
					(!pending.removeFile && !unpublished))
				{
					retain("The partial file cannot be safely identified as disposable. " + detail);
					++n;
					continue;
				}
				if (!pending.removeFile)
				{
					// Interrupted copying changes length/mtime, but not the created
					// object's identity. Save disposal intent before touching it.
					pending.fileStamp = partial->stamp();
					pending.removeFile = true;
					if (!j.save(e))
					{
						error = j.error();
						return false;
					}
				}
				const QString survivorPath = originalSafe ? e.item.src : e.dst;
				const OpStamp survivorStamp = originalSafe ? e.source : e.landed;
				auto survivor = OpFile::open(survivorPath, false, detail);
				point("before-partial-cleanup");
				if (!survivor || !survivor->stillAt(survivorPath, survivorStamp))
				{
					retain("The surviving original or completed copy changed; the partial was retained.");
					++n;
					continue;
				}
				if (!partial->removePartial(pending.fileStamp, pending.directoryStamp, detail))
				{
					retain(detail);
					++n;
					continue;
				}
				partial.reset();
				point("partial-cleaned");
			}
			if (!pending.file.isEmpty())
			{
				if (sync(pending.directory, &detail) != Sync::Ok)
				{
					retain("Cannot confirm partial cleanup. " + detail);
					++n;
					continue;
				}
				if (e.temp == pending.file)
					e.temp.clear();
				e.artifacts.removeAll(pending.file);
				pending.file.clear();
				pending.fileStamp = {};
				pending.removeFile = false;
				if (!j.save(e))
				{
					error = j.error();
					return false;
				}
			}
			point("before-directory-cleanup");
			if (OpFile::removeEmptyPrivateDirectory(pending.directory, pending.directoryStamp, detail, sync) != Sync::Ok)
			{
				retain(detail);
				++n;
				continue;
			}
			point("directory-cleaned");
		}
		if (e.temp == pending.file)
			e.temp.clear();
		e.artifacts.removeAll(pending.file);
		e.artifacts.removeAll(pending.directory);
		e.cleanup.removeAt(n);
		if (!j.save(e))
		{
			error = j.error();
			return false;
		}
	}
	error = errors.join('\n');
	if (!errors.isEmpty() && !j.save(e))
		error += '\n' + j.error();
	return errors.isEmpty();
}

OpResult OpRunner::transfer(OpJournal &j, OpJournal::Entry &e, OpKind kind, OpFile &source,
							int index, int total, bool directoryDurable, bool *retryableCopy)
{
	QString error;
	const QString tempDir = QFileInfo(e.dst).absolutePath() + "/.mediamuster-stage-" + unique();
	e.temp = tempDir + "/payload.partial";
	e.mechanism = "copy";
	++e.attempts;
	e.landed = {};
	if (!save(j, e, Step::Copying))
		return result(e, State::NeedsAttention, "Journal failure; source retained.");
	OpStamp directoryStamp;
	const auto tempSync = OpFile::makePrivateDirectory(tempDir, directoryStamp, error, hooks.directorySync);
	if (directoryStamp.valid())
	{
		e.cleanup.append({tempDir, directoryStamp, {}, {}, false});
		if (!save(j, e, Step::Copying))
			return result(e, State::NeedsAttention, "Journal failure; temporary folder retained at " + tempDir);
	}
	if (tempSync == Sync::Failed)
	{
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	directoryDurable = directoryDurable && tempSync == Sync::Ok;
	auto destination = OpFile::open(e.temp, true, error);
	if (!destination)
	{
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	e.landed = destination->stamp();
	e.cleanup.last().file = e.temp;
	e.cleanup.last().fileStamp = e.landed;
	if (!save(j, e, Step::Copying))
		return result(e, State::NeedsAttention,
					  "Journal failure; temporary file retained at " + e.temp);
	auto progress = [&](qint64 bytes, qint64 size)
	{
		m_sink.progress(QStringLiteral("Copying ") + label(e.item), index, total,
						size ? 100.0 * bytes / size : 100.0);
		checkpoint("copy-chunk", e);
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
		copied = copier.copy(source, *destination, m_cancel, progress);
	auto abandon = [&](State state, const QString &why)
	{
		e.cleanup.last().fileStamp = destination->stamp();
		e.cleanup.last().removeFile = true;
		e.error = why;
		const auto step = state == State::Cancelled ? Step::Cancelled
						  : state == State::Skipped ? Step::Skipped
													: Step::Failed;
		if (!save(j, e, step))
			return result(e, State::NeedsAttention, "Journal failure; temporary file retained at " + e.temp);
		destination.reset();
		QString cleanupError;
		if (!cleanup(j, e, cleanupError, &hooks))
		{
			e.error += '\n' + cleanupError;
			if (!j.save(e))
				state = State::NeedsAttention;
		}
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
	e.copyDurable = copied.durable && directoryDurable;
	e.metadataComplete = copied.metadataComplete;
	e.error = copied.error;
	if (!copied.durable)
		e.error += " Copy finished; the storage did not confirm the full durability "
				   "request.";
	if (!directoryDurable)
		e.error += '\n' + directoryWarning;
	e.landed = destination->stamp();
	if (!save(j, e, Step::CopyReady))
		return result(e, State::NeedsAttention,
					  "Journal failure; temporary file retained at " + e.temp);
	if (m_cancel.load())
		return abandon(State::Cancelled, "Cancelled before publication.");
	if (!source.stillAt(e.item.src, e.source) || !destination->stillAt(e.temp, e.landed))
		return abandon(State::Failed, "A file changed before publication; source retained.");
	const auto originalDestination =
		(e.undoAction == "restoreMove" ? e.item.renameDst : OperationPlan::destinationPath(e.item.name, e.item.folder, j.record().request.destRoot, j.record().request.preserve, e.item.omfEra));
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
		const auto publishedSync = fail("folder-sync") ? Sync::Failed : syncFolders({QFileInfo(e.dst).absolutePath(), QFileInfo(oldTemp).absolutePath()}, syncError, hooks.directorySync);
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
	if (e.step != Step::TrashFallback)
		e.error.clear();
	const bool trash = kind == OpKind::Delete || e.item.maintenance ||
					   e.undoAction == "discardCopy";
	if (trash && e.step == Step::TrashFallback && !e.trashFallbackApproved)
		return result(e, State::SourceRetained, e.error);
	if (trash && !e.trashFallbackApproved && !e.item.maintenance && !hooks.forceNetworkTrash &&
		j.record().request.diagnosticTrashRoot.isEmpty() &&
		!OpTrash::isNetwork(e.item.src))
	{
		e.mechanism = "systemTrash";
		e.trashProvider = "system";
		if (!save(j, e, Step::Relocating))
			return result(e, State::NeedsAttention, j.error());
		source.reset();
		const auto trashed = hooks.nativeTrash ? hooks.nativeTrash(e) : OpTrash::move(e.item.src, e.source, m_cancel);
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
		if (trashed.outcome != OpTrash::Outcome::Unavailable ||
			!trashed.path.isEmpty() || !trashed.receipt.isEmpty() || trashed.landed.valid())
		{
			e.dst = trashed.path;
			e.trashReceipt = trashed.receipt;
			e.landed = trashed.landed;
			e.error = trashed.error;
			const bool unchanged = e.source.unchanged(OpFile::inspect(e.item.src));
			const bool uncertain = trashed.outcome == OpTrash::Outcome::Failed || !unchanged || !trashed.path.isEmpty() ||
								   !trashed.receipt.isEmpty() || trashed.landed.valid();
			const auto state = uncertain ? State::NeedsAttention : (trashed.outcome == OpTrash::Outcome::Cancelled ? State::Cancelled : State::Failed);
			save(j, e, uncertain ? Step::NeedsAttention : (state == State::Cancelled ? Step::Cancelled : Step::Failed));
			return result(e, state, e.error);
		}
		source = OpFile::open(e.item.src, false, error);
		if (!source || !source->stillAt(e.item.src, e.source))
		{
			e.error = "Original changed while checking bin support. " + error;
			save(j, e, Step::NeedsAttention);
			return result(e, State::NeedsAttention, e.error);
		}
		// This is a confirmed refusal, distinct from an interrupted native move.
		// Persist it before waiting for consent so Resume never repeats the call.
		e.mechanism.clear();
		e.trashProvider.clear();
		e.dst.clear();
		e.trashReceipt.clear();
		e.landed = {};
		e.error = trashed.error;
		if (!save(j, e, Step::TrashFallback))
			return result(e, State::NeedsAttention, "Cannot save the bin refusal; original retained.");
		checkpoint("trash-fallback-pending", e);
		return result(e, State::SourceRetained, e.error);
	}
	if (trash && e.trashFallbackApproved && m_cancel.load())
		return result(e, State::Cancelled, "Cancelled before moving to MediaMuster Trash.");
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
				if (trash && m_cancel.load())
				{
					e.error = "Cancelled before moving to Trash.";
					const auto state = save(j, e, Step::Cancelled) ? State::Cancelled : State::NeedsAttention;
					return result(e, state, e.error);
				}
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
									 QFileInfo(e.item.src).absolutePath()},
									syncError,
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

OpResult OpRunner::restoreOriginal(OpJournal &j, OpJournal::Entry &e)
{
	// Restoring is protective rollback, so a cancellation request must not
	// interrupt it or send this entry back through forward removal on Resume.
	e.sourceRemoved = false;
	const auto retirementDirectory = QFileInfo(e.retirement).absolutePath();
	if (std::none_of(e.cleanup.cbegin(), e.cleanup.cend(), [&](const auto &pending)
					 { return pending.directory == retirementDirectory; }))
		keepArtifact(e, retirementDirectory);
	if (!save(j, e, Step::RestoringSource))
		return result(e, State::NeedsAttention, j.error());
	const auto blocked = [&](const QString &why)
	{
		e.error = "Original restoration pending: " + e.retirement + " -> " + e.item.src + ". " + why;
		// Keep the distinct intent even when restoration is blocked, and
		// surface a journal failure rather than hiding it behind the first error.
		if (!j.save(e))
			return result(e, State::NeedsAttention, e.error + '\n' + j.error());
		return result(e, State::NeedsAttention, e.error);
	};
	if (!OpFile::safePath(e.item.src) || !OpFile::safePath(e.retirement))
		return blocked("A path is no longer safe; files were retained.");
	QString error;
	const bool atSource = OpFile::occupied(e.item.src);
	const bool atRetirement = OpFile::occupied(e.retirement);
	if (atSource)
	{
		if (atRetirement || !e.source.unchanged(OpFile::inspect(e.item.src)))
			return blocked("The original location is occupied; nothing was overwritten.");
	}
	else
	{
		auto original = OpFile::open(e.retirement, false, error);
		if (!original || !original->stillAt(e.retirement, e.source))
			return blocked("The retained original is missing or changed. " + error);
		checkpoint("before-source-restore", e);
		if (fail("restore-original") || !original->stillAt(e.retirement, e.source) ||
			original->relocate(e.retirement, e.item.src, error) != OpFile::Relocation::Moved)
			return blocked("The original could not be returned safely. " + error);
		checkpoint("source-restored-on-disk", e);
	}
	QStringList folders{QFileInfo(e.item.src).absolutePath()};
	if (OpFile::occupied(QFileInfo(e.retirement).absolutePath()))
		folders.append(QFileInfo(e.retirement).absolutePath());
	if (syncFolders(folders, error, hooks.directorySync) != Sync::Ok)
		return blocked("The folder updates still need confirmation. " + error);
	e.error.clear();
	if (!save(j, e, Step::SourceRestored))
		return result(e, State::NeedsAttention, j.error());
	return result(e, State::OriginalRestored, "Original restored to " + e.item.src + ". Completed copies were kept.");
}

OpRunner::Totals OpRunner::restoreOriginals(const OpRequest &request, const QString &directory)
{
	Totals totals;
	QString error;
	const QString journalDirectory = request.restoreJournalPath.isEmpty()
										 ? directory
										 : QFileInfo(request.restoreJournalPath).absolutePath();
	auto lock = OpJournal::acquire(journalDirectory, error);
	OpJournal journal;
	try
	{
		if (!lock)
			throw std::runtime_error(error.toStdString());
		auto saved = OpJournal::readOne(request.restoreJournalPath);
		if (!saved || saved->corrupt)
			throw std::runtime_error("Cannot read the original restoration record.");
		if (!saved->undoPath.isEmpty())
			throw std::runtime_error("An Undo already owns this job's recovery. Resume that Undo first.");
		for (const auto &other : OpJournal::scan(journalDirectory))
			if (!other.corrupt && other.request.undoOf == saved->path)
				throw std::runtime_error("An Undo already owns this job's recovery. Resume that Undo first.");
		if (!OpJournal::resolveRestoration(*saved, error) || !journal.resume(*saved, error))
			throw std::runtime_error(error.toStdString());
		for (auto e : saved->entries)
		{
			if (!e.needsOriginalRestoration())
				continue;
			if (m_cancel.load())
			{
				totals.cancelled = true;
				break;
			}
			m_sink.progress("Restoring original: " + label(e.item), e.id + 1, saved->entries.size(), 0);
			const auto outcome = restoreOriginal(journal, e);
			m_sink.result(outcome);
			if (outcome.state == State::OriginalRestored)
				++totals.succeeded;
			else
				++totals.needsAttention;
			QString cleanupError;
			if (journal.healthy() && e.step == Step::SourceRestored && !cleanup(journal, e, cleanupError, &hooks))
				m_sink.log(QtWarningMsg, cleanupError);
			if (!journal.healthy())
			{
				m_sink.log(QtWarningMsg, journal.error());
				if (outcome.state == State::OriginalRestored)
					++totals.needsAttention;
				break;
			}
		}
		if (journal.healthy() && !journal.finish(totals.cancelled))
			throw std::runtime_error(journal.error().toStdString());
	}
	catch (const std::exception &failure)
	{
		++totals.needsAttention;
		m_sink.log(QtWarningMsg, QString::fromUtf8(failure.what()));
	}
	return totals;
}

OpResult OpRunner::removeOriginal(OpJournal &j, OpJournal::Entry &e, int index, int total)
{
	QString error;
	if (m_cancel.load() && e.needsOriginalRestoration())
		return restoreOriginal(j, e);
	if (!j.record().copiesComplete || !e.copyDurable || !e.metadataComplete ||
		!copiedDestinationUnchanged(e, error))
	{
		if (m_cancel.load() && e.needsOriginalRestoration())
			return restoreOriginal(j, e);
		return result(e, State::NeedsAttention,
					  error.isEmpty() ? "The completed copy is not ready for original removal." : error);
	}
	if (m_cancel.load())
		return e.needsOriginalRestoration() ? restoreOriginal(j, e)
											: result(e, State::SourceRetained, "Cancelled; original retained.");
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
		OpStamp directoryStamp;
		const auto prepared = OpFile::makePrivateDirectory(privateDir, directoryStamp, error, hooks.directorySync);
		if (directoryStamp.valid())
		{
			e.cleanup.append({privateDir, directoryStamp, {}, {}, false});
			if (!save(j, e, Step::RemovingSource))
				return result(e, State::NeedsAttention, j.error());
		}
		if (prepared != Sync::Ok)
			return result(e, State::NeedsAttention, "Cannot prepare original removal. " + error);
		checkpoint("before-source-retirement", e);
		if (m_cancel.load())
		{
			original.reset();
			return restoreOriginal(j, e);
		}
		if (!original->stillAt(e.item.src, e.source) ||
			original->relocate(e.item.src, e.retirement, error) != OpFile::Relocation::Moved)
			return result(e, State::NeedsAttention, "Original removal needs recovery. " + error);
		if (!save(j, e, Step::RemovingSource))
			return result(e, State::NeedsAttention, j.error());
		checkpoint("source-retired", e);
	}
	if (m_cancel.load())
	{
		original.reset();
		return restoreOriginal(j, e);
	}
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
		throw std::runtime_error("Enable undo in the Debug menu before starting an Undo.");
	if (!forward.undoPath.isEmpty() || forward.request.kind == OpKind::Undo || forward.corrupt)
		throw std::runtime_error("This job cannot start another Undo.");
	QString error;
	if (!OpJournal::resolve(forward, error))
		throw std::runtime_error(error.toStdString());
	OpRequest undo;
	undo.kind = OpKind::Undo;
	undo.undoOf = forward.path;
	undo.diagnosticTrashRoot = input.diagnosticTrashRoot.isEmpty() ? forward.request.diagnosticTrashRoot : input.diagnosticTrashRoot;
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
			 e.step != Step::RestoringSource && e.step != Step::SourceRestored &&
			 e.step != Step::SourceRemoved && e.step != Step::NeedsAttention))
			continue;
		if (!copiedDestinationUnchanged(e, error))
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
		else if (!OpFile::occupied(e.item.src) && (e.sourceRemoved || e.step == Step::RemovingSource ||
												   (e.step == Step::RestoringSource && !e.retirement.isEmpty())))
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

OpResult OpRunner::executeWithRetries(OpJournal &journal, OpJournal::Entry &e,
									  OpKind kind, int index, int total, QString &error)
{
	bool retryableCopy = false;
	auto outcome = execute(journal, e, kind, index, total, &retryableCopy);
	for (int retry = 0; retry < 2 && retryableCopy && outcome.state == State::Failed &&
						e.step == Step::Failed && journal.healthy() && !m_cancel.load() &&
						(kind == OpKind::Copy || kind == OpKind::Move);
		 ++retry)
	{
		if (!reconcile(journal, e, error, hooks.directorySync))
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
		outcome = execute(journal, e, kind, index, total, &retryableCopy);
	}
	return outcome;
}

bool OpRunner::copiesReadyForRemoval(const OpJournal &journal, const OpRequest &request,
									 const Totals &totals, QString &error) const
{
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
				e.mechanism == "copy" && !copiedDestinationUnchanged(e, error))
				ready = false;
			if (request.kind == OpKind::Undo && e.undoAction.startsWith("restore") &&
				!e.landed.unchanged(OpFile::inspect(e.dst)))
				ready = false;
			continue;
		}
		if (!removesAfterCopy(e, request) ||
			(e.step != Step::Published && e.step != Step::SourceRetained && e.step != Step::RemovingSource) ||
			!e.copyDurable || !e.metadataComplete || !copiedDestinationUnchanged(e, error))
			ready = false;
		if (e.retirement.isEmpty() && !e.source.unchanged(OpFile::inspect(e.item.src)))
			ready = false;
	}
	return ready;
}

OpRunner::Totals OpRunner::run(const OpRequest &input, const QString &directory)
{
	if (!input.restoreJournalPath.isEmpty())
		return restoreOriginals(input, directory);
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
					if (e.step == Step::RestoringSource)
					{
						const auto restored = restoreOriginal(journal, e);
						m_sink.result(restored);
						if (restored.state != State::OriginalRestored)
							throw std::runtime_error(restored.message.toStdString());
						continue;
					}
					if (!reconcile(journal, e, error, hooks.directorySync))
					{
						const bool beforePublication = e.step == Step::Failed || e.step == Step::Cancelled ||
													   e.step == Step::Copying || e.step == Step::CopyReady;
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
					throw std::runtime_error("The previous job was interrupted. Resume or stop it first.");
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
											 request, canRelocate, hooks.forceCopy)
											 .copyThenRemove;
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
		QVector<int> trashFallbacks;
		for (const auto &e : entries)
			if (!e.item.maintenance)
				++mediaTotal;
		const bool twoStage = request.copyThenRemove || std::any_of(entries.cbegin(), entries.cend(),
																	[](const auto &e)
																	{ return e.undoAction == "restoreMove"; });
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
												  error)
													 .toStdString());
				if (syncFolders(folders.values(), error, hooks.directorySync) != Sync::Ok)
					throw std::runtime_error(("Rebalance unavailable; source files retained.\n" +
											  error)
												 .toStdString());
				if (!retireDatabases(journal, folders, error))
					throw std::runtime_error(error.toStdString());
			}

			if (e.step != Step::Planned)
			{
				if (!reconcile(journal, e, error, hooks.directorySync))
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
			const auto outcome = executeWithRetries(
				journal, e, request.kind, mediaIndex, workTotal, error);
			if (e.step == Step::TrashFallback && outcome.state == State::SourceRetained)
			{
				trashFallbacks.append(n);
				continue;
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
		bool ready = copiesReadyForRemoval(journal, request, totals, error);
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
			const auto outcome = [&]
			{
				if (m_cancel.load() && e.needsOriginalRestoration())
					return restoreOriginal(journal, e);
				if (ready && !m_cancel.load())
					return removeOriginal(journal, e, mediaTotal + n + 1, workTotal);
				return result(e, State::SourceRetained,
							  "Original retained because the job's required copies have not all completed safely." +
								  (e.error.isEmpty() ? QString() : '\n' + e.error));
			}();
			m_sink.result(outcome);
			if (outcome.state == State::Completed)
				++totals.succeeded;
			else if (outcome.state == State::SourceRetained || outcome.state == State::OriginalRestored)
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
		auto canDiscard = [&](const OpJournal::Entry &e)
		{
			if (!undoOriginal || undoOriginal->request.kind != OpKind::Move)
				return true;
			for (const auto &inverse : journal.record().entries)
				if (inverse.undoEntryId == e.undoEntryId && inverse.undoAction.startsWith("restore") &&
					inverse.complete() && inverse.landed.unchanged(OpFile::inspect(inverse.dst)))
					return true;
			if (e.undoEntryId >= 0 && e.undoEntryId < undoOriginal->entries.size())
			{
				const auto &original = undoOriginal->entries[e.undoEntryId];
				return original.source.unchanged(OpFile::inspect(original.item.src));
			}
			return false;
		};
		for (const auto n : discards)
		{
			if (!ready || m_cancel.load() || !journal.healthy())
				break;
			auto e = journal.record().entries[n];
			if (!canDiscard(e))
				throw std::runtime_error("A restored original changed; its remaining copy was retained.");
			if (e.step != Step::Planned && !reconcile(journal, e, error, hooks.directorySync))
			{
				++totals.needsAttention;
				m_sink.result(result(e, State::NeedsAttention, error));
				break;
			}
			if (e.complete())
				continue;
			const auto outcome = execute(journal, e, OpKind::Undo, n + 1, mediaTotal);
			if (e.step == Step::TrashFallback && outcome.state == State::SourceRetained)
			{
				trashFallbacks.append(n);
				continue;
			}
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
		if (!trashFallbacks.isEmpty())
		{
			QVector<OpTrashFallbackItem> choices;
			QSet<int> changedOriginals;
			bool eligible = journal.healthy() && !totals.needsAttention && !totals.failed &&
							!totals.cancelled && !m_cancel.load();
			for (const auto n : trashFallbacks)
			{
				const auto &e = journal.record().entries[n];
				QString checkError;
				auto original = OpFile::open(e.item.src, false, checkError);
				if (!original || !original->stillAt(e.item.src, e.source) || !canDiscard(e))
				{
					eligible = false;
					changedOriginals.insert(n);
				}
				choices.append({e.item.src, trashRoot(e.item.src), e.error});
			}
			const bool approved = eligible && m_sink.confirmTrashFallback(choices);
			if (eligible && !approved)
				totals.cancelled = true;
			if (approved && !m_cancel.load())
			{
				// Persist the whole batch choice before moving its first file.
				// Every subsequent mutation still revalidates its own source.
				for (const auto n : trashFallbacks)
				{
					auto e = journal.record().entries[n];
					e.trashFallbackApproved = true;
					if (!save(journal, e, Step::TrashFallback))
						throw std::runtime_error(journal.error().toStdString());
				}
				checkpoint("trash-fallback-approved", journal.record().entries[trashFallbacks.first()]);
			}
			for (const auto n : trashFallbacks)
			{
				auto e = journal.record().entries[n];
				if (changedOriginals.contains(n))
				{
					e.error = "An original changed while preparing the Trash choice; no fallback was attempted.";
					save(journal, e, Step::NeedsAttention);
					++totals.needsAttention;
					m_sink.result(result(e, State::NeedsAttention, e.error));
					continue;
				}
				if (!approved || m_cancel.load() || !journal.healthy())
				{
					++totals.retained;
					m_sink.result(result(e, State::SourceRetained,
										 "Original retained; the MediaMuster Trash move was not approved or the operation stopped."));
					continue;
				}
				OpResult outcome;
				if (e.undoAction == "discardCopy" && !canDiscard(e))
				{
					e.error = "A restored original changed while awaiting the Trash choice; its remaining copy was retained.";
					save(journal, e, Step::NeedsAttention);
					outcome = result(e, State::NeedsAttention, e.error);
				}
				else
					outcome = execute(journal, e, request.kind, n + 1, mediaTotal);
				m_sink.result(outcome);
				if (outcome.state == State::Completed)
				{
					++totals.succeeded;
					QDir trash(QFileInfo(e.dst).absolutePath());
					trash.cdUp();
					trash.cdUp();
					++trashCounts[trash.path()];
				}
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
					break;
				}
			}
		}
		// Publication/removal/restoration completion must be durable before
		// deleting its working directory. Keep failed cleanup discoverable even
		// for a completed or subsequently dismissed job.
		for (auto e : journal.record().entries)
		{
			if (!journal.healthy())
				break;
			QString cleanupError;
			if (!cleanup(journal, e, cleanupError, &hooks))
				m_sink.log(QtWarningMsg, cleanupError);
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
