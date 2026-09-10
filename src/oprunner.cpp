#include "oprunner.h"
#include "conventions.h"
#include "mobid.h"
#include "mxfparser.h"
#include "pathkey.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUuid>
#include <stdexcept>

namespace
{
using Step = OpJournal::Step;
using State = OpResult::State;
using Sync = NativeFile::SyncResult;
Sync syncFolders(const QStringList &folders, QString &error,
				 const NativeFile::DirectorySync &sync = NativeFile::syncDirectory)
{
	QStringList warnings;
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
			warnings.append(detail);
	}
	error = warnings.join('\n');
	return warnings.isEmpty() ? Sync::Ok : Sync::OkDegraded;
}
const QString directoryWarning = QStringLiteral(
	"Copy verified; original retained. This storage does not support confirming folder "
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
	return {s, label(e.item), e.item.src, e.dst, message, removed};
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

QString OpRunner::buildDestPath(const QString &name, const QString &folder, const QString &root,
								bool preserve, bool omf)
{
	if (preserve && omf)
		return Conventions::omfRootUnder(root) + '/' + name;
	if (preserve)
		return Conventions::mxfRootUnder(root) + '/' + folder + '/' + name;
	return root + '/' + name;
}
std::optional<QString> OpRunner::generateRenamePath(const QString &path)
{
	const QFileInfo fi(path);
	for (int n = 2; n <= 999; ++n)
	{
		const auto candidate = fi.absolutePath() + '/' + fi.completeBaseName() +
							   QStringLiteral(" (%1)").arg(n) +
							   (fi.suffix().isEmpty() ? QString() : '.' + fi.suffix());
		if (!OpFile::occupied(candidate))
			return candidate;
	}
	return {};
}
bool OpRunner::sameVolumeForRename(const QString &src, const QString &dst)
{
	const QStorageInfo a(src), b(QFileInfo(dst).absolutePath());
	return a.isValid() && a.isReady() && b.isValid() && b.isReady() && !a.device().isEmpty() &&
		   a.device() == b.device();
}
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

// Recovery never deletes a source or blindly removes an artifact. It can
// recognise a completed relocation by the recorded object's identity. Copies
// require both the verified checkpoint and fresh readback before reconciliation.
bool OpRunner::reconcile(OpJournal &j, OpJournal::Entry &e, QString &error)
{
	if (e.complete())
		return true;
	if (e.step == Step::Planned || e.step == Step::Failed || e.step == Step::Cancelled ||
		e.step == Step::Copying)
	{
		keepArtifact(e, e.temp);
		if (e.source.valid() && !e.source.unchanged(OpFile::inspect(e.item.src)))
		{
			error = QStringLiteral("The original file cannot be confirmed at %1; its recovery "
								   "record was retained.")
						.arg(e.item.src);
			return false;
		}
		e.step = Step::Planned;
		return j.save(e);
	}
	if (e.step == Step::Relocating || (e.step == Step::NeedsAttention && e.hash.isEmpty()))
	{
		const auto src = OpFile::inspect(e.item.src), dst = OpFile::inspect(e.dst);
		if (e.source.unchanged(dst) && !OpFile::occupied(e.item.src))
		{
			QString syncError;
			if (syncFolders({QFileInfo(e.dst).absolutePath(),
							 QFileInfo(e.item.src).absolutePath()}, syncError) != Sync::Ok)
			{
				error = "A relocated file exists, but its folder updates could not be confirmed.\n" +
						syncError;
				return false;
			}
			e.landed = dst;
			e.step = Step::Done;
			e.error.clear();
			return j.save(e);
		}
		if (e.source.unchanged(src) && !OpFile::occupied(e.dst))
		{
			e.step = Step::Planned;
			return j.save(e);
		}
		error = QStringLiteral(
					"Relocation needs inspection: %1 → %2. Both locations were left untouched.")
					.arg(e.item.src, e.dst);
		return false;
	}
	// A verified but unpublished file can be retained and recopied. It does
	// not authorize later removal of a possibly changed source.
	if ((e.step == Step::Verified || e.step == Step::Publishing) && OpFile::occupied(e.temp))
	{
		if (!e.source.unchanged(OpFile::inspect(e.item.src)))
		{
			error = "The source changed after interruption; files were retained.";
			return false;
		}
		keepArtifact(e, e.temp);
		e.step = Step::Planned;
		return j.save(e);
	}
	if (!e.hash.isEmpty() && e.landed.unchanged(OpFile::inspect(e.dst)))
	{
		QString why;
		auto dst = OpFile::open(e.dst, false, why);
		std::atomic<bool> cancel{false};
		if (dst)
		{
			const auto hash = OpCopier::hash(*dst, cancel);
			if (hash.outcome == OpCopier::Outcome::Succeeded && hash.hash == e.hash &&
				dst->stillAt(e.dst, e.landed))
			{
				const bool moved = j.record().request.kind == OpKind::Move;
				if (moved && !OpFile::occupied(e.item.src))
				{
					error = "The source is missing without a recorded removal intent. The "
							"destination and journal were retained.";
					return false;
				}
				QString syncError;
				if (NativeFile::syncDirectory(QFileInfo(e.dst).absolutePath(), &syncError) != Sync::Ok ||
					(moved && NativeFile::syncDirectory(QFileInfo(e.item.src).absolutePath(),
														 &syncError) != Sync::Ok))
				{
					error = "The verified destination exists, but folder durability remains "
							"unconfirmed.\n" + syncError;
					return false;
				}
				e.step = moved && OpFile::occupied(e.item.src) ? Step::SourceRetained : Step::Done;
				if (e.step == Step::SourceRetained)
					e.error = "Copied and verified; source retained after interruption.";
				return j.save(e);
			}
		}
	}
	error = QStringLiteral("Cannot establish the interrupted operation's result. Inspect %1 and "
						   "%2; the journal is retained.")
				.arg(e.item.src, e.dst);
	return false;
}

OpResult OpRunner::transfer(OpJournal &j, OpJournal::Entry &e, OpKind kind, OpFile &source,
							int index, int total, bool directoryDurable)
{
	QString error;
	const QString tempDir = QFileInfo(e.dst).absolutePath() + "/.mediamuster-" + unique();
	e.temp = tempDir + "/payload.partial";
	e.hash.clear();
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
		m_sink.progress((verifying ? QStringLiteral("Verifying ") : QStringLiteral("Copying ")) +
							label(e.item),
						index, total, size ? 100.0 * bytes / size : 100.0);
		checkpoint(verifying ? "readback-chunk" : "copy-chunk", e);
	};
	OpCopier copier;
	auto copied = copier.copy(source, *destination, m_cancel, progress,
							  [&] { checkpoint("before-readback", e); });
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
		return abandon(copied.outcome == OpCopier::Outcome::Cancelled ? State::Cancelled
																	  : State::Failed,
					   copied.error);
	e.hash = copied.hash;
	e.copyDurable = copied.durable && directoryDurable;
	e.metadataComplete = copied.metadataComplete;
	e.error = copied.error;
	if (!copied.durable)
		e.error += " Destination readback passed; the storage did not confirm the full durability "
				   "request.";
	if (!directoryDurable)
		e.error += '\n' + directoryWarning;
	e.landed = destination->stamp();
	if (!save(j, e, Step::Verified))
		return result(e, State::NeedsAttention,
					  "Journal failure; verified temporary file retained at " + e.temp);
	if (m_cancel.load())
		return abandon(State::Cancelled, "Cancelled before publication.");
	if (!source.stillAt(e.item.src, e.source) || !destination->stillAt(e.temp, e.landed))
		return abandon(State::Failed, "A file changed before publication; source retained.");
	const auto originalDestination =
		buildDestPath(e.item.name, e.item.folder, j.record().request.destRoot,
					  j.record().request.preserve, e.item.omfEra);
	for (int attempts = 0; attempts < 999; ++attempts)
	{
		if (!save(j, e, Step::Publishing))
			return result(e, State::NeedsAttention,
						  "Journal failure; verified temporary file retained at " + e.temp);
		if (fail("publish"))
			return abandon(State::Failed, "Injected publication failure.");
		if (!destination->stillAt(e.temp, e.landed))
			return abandon(State::Failed, "The temporary file changed before publication.");
		const auto moved = destination->relocate(e.temp, e.dst, error);
		if (moved == OpFile::Relocation::Exists)
		{
			if (e.item.policy != "keepboth")
				return abandon(State::Skipped, "Skipped: the destination became occupied.");
			const auto next = generateRenamePath(originalDestination);
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
		if (kind == OpKind::Move)
		{
			e.error = (m_cancel.load() ? QStringLiteral("Copied and verified; source retained after cancellation.")
									 : QStringLiteral("Copied and verified; source retained. Source removal "
													  "after a byte copy is not enabled in this phase.")) +
					  (e.error.isEmpty() ? QString() : '\n' + e.error);
			if (!save(j, e, Step::SourceRetained))
				return result(e, State::NeedsAttention, "Journal failure; both files retained.");
			return result(e, State::SourceRetained, e.error);
		}
		if (!save(j, e, Step::Done))
			return result(e, State::NeedsAttention,
						  "The operation finished on disk, but the journal could not confirm "
						  "completion. Destination: " +
							  e.dst);
		return result(e, State::Completed, e.error);
	}
	return abandon(State::Failed, "Too many destination conflicts.");
}

OpResult OpRunner::execute(OpJournal &j, OpJournal::Entry &e, OpKind kind, int index, int total)
{
	QString error;
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
	const bool trash = kind == OpKind::Delete || e.item.maintenance;
	const auto trashFolder = j.record().request.diagnosticTrashRoot.isEmpty()
								 ? trashRoot(e.item.src)
								 : j.record().request.diagnosticTrashRoot;
	if (trash)
		e.dst = trashFolder + "/" + QFileInfo(j.path()).completeBaseName() + "/" +
				QString::number(e.id) + "/" + e.item.name;
	else if (kind == OpKind::Rename)
		e.dst = e.item.renameDst;
	else
		e.dst = buildDestPath(e.item.name, e.item.folder, j.record().request.destRoot,
							  j.record().request.preserve, e.item.omfEra);
	e.dst = OpJournal::canonicalPath(e.dst);
	const auto originalDestination = e.dst;
	if (!save(j, e, Step::Planned))
		return result(e, State::NeedsAttention, "Journal failure; source retained.");
	const auto destinationSync = OpFile::makeDirectory(QFileInfo(e.dst).absolutePath(), error,
														 hooks.directorySync);
	if (destinationSync == Sync::Failed)
	{
		e.error = error;
		save(j, e, Step::Failed);
		return result(e, State::Failed, error);
	}
	bool directoryDurable = destinationSync == Sync::Ok;
	if (e.source.sameObject(OpFile::inspect(e.dst)))
	{
		save(j, e, Step::Skipped);
		return result(e, State::Skipped, "Skipped: source and destination are the same file.");
	}
	if (OpFile::occupied(e.dst))
	{
		if (kind == OpKind::Rename)
		{
			e.error = "Rebalance stopped: a destination became occupied after the group check. "
					  "Rescan and replan.";
			save(j, e, Step::Failed);
			return result(e, State::Failed, e.error);
		}
		if (kind == OpKind::Rename || e.item.policy != "keepboth")
		{
			save(j, e, Step::Skipped);
			return result(e, State::Skipped, "Skipped: destination occupied.");
		}
		const auto next = generateRenamePath(e.dst);
		if (!next)
		{
			e.error = "All Keep Both names are occupied.";
			save(j, e, Step::Failed);
			return result(e, State::Failed, e.error);
		}
		e.dst = *next;
	}
	bool canRelocate = (trash || kind == OpKind::Rename || kind == OpKind::Move) &&
		!hooks.forceCopy && sameVolumeForRename(e.item.src, e.dst);
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
						const auto next = generateRenamePath(originalDestination);
						if (next)
						{
							e.dst = *next;
							continue;
						}
					}
					e.error = "Skipped: destination became occupied.";
					if (!save(j, e, Step::Skipped))
						return result(e, State::NeedsAttention,
									  "Journal failure; source retained.");
					return result(e, State::Skipped, e.error);
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
	if (trash || kind == OpKind::Rename)
	{
		e.error = "Safe same-filesystem relocation is unavailable. The source was retained." +
				  (error.isEmpty() ? QString() : '\n' + error);
		save(j, e, Step::Failed);
		return result(e, State::Failed, e.error);
	}
	return transfer(j, e, kind, *source, index, total, directoryDurable);
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
	const auto lockDirectory = input.resumeJournalPath.isEmpty()
								   ? directory
								   : QFileInfo(input.resumeJournalPath).absolutePath();
	auto lock = OpJournal::acquire(lockDirectory, error);
	if (!lock)
	{
		m_sink.log(QtCriticalMsg, error);
		totals.failed = 1;
		return totals;
	}
	OpRequest request = input;
	OpJournal journal;
	try
	{
		if (!request.resumeJournalPath.isEmpty())
		{
			const auto saved = OpJournal::readOne(request.resumeJournalPath);
			if (!saved)
				throw std::runtime_error("Cannot read the requested operation journal.");
			auto rec = *saved;
			if (!OpJournal::resolve(rec, error) || !journal.resume(rec, error))
				throw std::runtime_error(error.toStdString());
			request = rec.request;
			for (auto e : rec.entries)
				if (!e.complete() && e.step != Step::Planned)
				{
					if (!reconcile(journal, e, error))
						throw std::runtime_error(error.toStdString());
				}
		}
		else
		{
			if (request.kind == OpKind::Undo)
				throw std::runtime_error(
					"Undo remains gated; it must be implemented with the new engine.");
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
					const auto key = PathKey::normalise(buildDestPath(
						i.name, i.folder, request.destRoot, request.preserve, i.omfEra));
					if (i.policy.isEmpty() && batchDestinations.contains(key))
						i.policy = "keepboth";
					batchDestinations.insert(key);
				}
			}
			if (!journal.create(request, directory, error))
				throw std::runtime_error(error.toStdString());
		}
		auto entries = journal.record().entries;
		QString group;
		QSet<QString> touched;
		int mediaIndex = 0, mediaTotal = 0;
		for (const auto &e : entries)
			if (!e.item.maintenance)
				++mediaTotal;
		for (int n = 0; n < entries.size(); ++n)
		{
			auto e = journal.record().entries[n];
			if (!e.item.maintenance)
				++mediaIndex;
			if (e.complete())
				continue;
			if (!journal.healthy())
				break;
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
				if (!reconcile(journal, e, error))
				{
					m_sink.result(result(e, State::NeedsAttention, error));
					++totals.needsAttention;
					break;
				}
				if (e.complete())
					continue;
			}
			m_sink.progress(label(e.item), mediaIndex, mediaTotal, 0);
			if (request.kind == OpKind::Rename && !e.item.maintenance)
			{
				if (!journal.touchFolder(QFileInfo(e.item.src).absolutePath()) ||
					!journal.touchFolder(QFileInfo(e.item.renameDst).absolutePath()))
					throw std::runtime_error(journal.error().toStdString());
			}
			const auto outcome = execute(journal, e, request.kind, mediaIndex, mediaTotal);
			if (!e.item.maintenance)
				m_sink.result(outcome);
			else if (outcome.state != State::Completed)
				m_sink.log(QtCriticalMsg, "Avid database relocation stopped: " + outcome.message);
			if (outcome.state == State::Completed)
			{
				if (!e.item.maintenance)
					++totals.succeeded;
				if (request.kind == OpKind::Delete && !e.item.maintenance)
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
				break;
			}
		}
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
	m_sink.log(totals.failed || totals.needsAttention ? QtWarningMsg : QtInfoMsg,
			   QStringLiteral("%1: %2 completed, %3 source retained, %4 skipped, %5 failed, %6 "
							  "need attention%7. Journal: %8")
				   .arg(opKindName(request.kind))
				   .arg(totals.succeeded)
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
