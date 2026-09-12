#include "opjournal.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>
#include <QSet>
#include <algorithm>

namespace
{
constexpr int schema = 3;
QJsonObject itemJson(const OpItem &i)
{
	return {{"src", i.src},
			{"name", i.name},
			{"folder", i.folder},
			{"omf", i.omfEra},
			{"bytes", QString::number(i.bytes)},
			{"modifiedMs", QString::number(i.modifiedMs)},
			{"maintenance", i.maintenance},
			{"policy", i.policy},
			{"mob", i.mobId},
			{"master", i.masterMobId},
			{"clip", i.clipName},
			{"rename", i.renameDst},
			{"group", i.groupKey},
			{"expectedFileId", i.expectedFileId},
			{"expectedVolumeId", i.expectedVolumeId},
			{"expectedModified", QString::number(i.expectedModified)},
			{"undoAction", i.undoAction},
			{"undoEntryId", i.undoEntryId},
			{"trashReceipt", i.trashReceipt}};
}
OpItem itemFromJson(const QJsonObject &v)
{
	OpItem i;
	i.src = v["src"].toString();
	i.name = v["name"].toString();
	i.folder = v["folder"].toString();
	i.modifiedMs = v["modifiedMs"].toString("-1").toLongLong();
	i.maintenance = v["maintenance"].toBool();
	i.omfEra = v["omf"].toBool();
	i.bytes = v["bytes"].toString().toLongLong();
	i.policy = v["policy"].toString();
	i.mobId = v["mob"].toString();
	i.masterMobId = v["master"].toString();
	i.clipName = v["clip"].toString();
	i.renameDst = v["rename"].toString();
	i.groupKey = v["group"].toString();
	i.expectedFileId = v["expectedFileId"].toString();
	i.expectedVolumeId = v["expectedVolumeId"].toString();
	i.expectedModified = v["expectedModified"].toString().toLongLong();
	i.undoAction = v["undoAction"].toString();
	i.undoEntryId = v["undoEntryId"].toInt(-1);
	i.trashReceipt = v["trashReceipt"].toString();
	return i;
}
bool inside(const QString &path, const QString &root)
{
	return path == root || path.startsWith(root.endsWith('/') ? root : root + '/');
}
} // namespace

QString OpJournal::stepName(Step s)
{
	switch (s)
	{
	case Step::Planned:
		return "planned";
	case Step::Copying:
		return "copying";
	case Step::CopyReady:
		return "copy-ready";
	case Step::Verified:
		return "verified";
	case Step::Publishing:
		return "publishing";
	case Step::Published:
		return "published";
	case Step::Relocating:
		return "relocating";
	case Step::RemovingSource:
		return "removing-source";
	case Step::SourceRemoved:
		return "source-removed";
	case Step::Done:
		return "done";
	case Step::NoEffect:
		return "no-effect";
	case Step::SourceRetained:
		return "source-retained";
	case Step::Skipped:
		return "skipped";
	case Step::Cancelled:
		return "cancelled";
	case Step::Failed:
		return "failed";
	case Step::NeedsAttention:
		return "needs-attention";
	}
	return {};
}
bool OpJournal::Entry::complete() const
{
	return step == Step::Done || step == Step::SourceRemoved || step == Step::Skipped ||
		step == Step::NoEffect;
}
QJsonObject OpJournal::Entry::json() const
{
	return {{"record", "item"},
			{"id", id},
			{"item", itemJson(item)},
			{"original", originalSource},
			{"originalVolume", originalVolume.toJson()},
			{"originalRelative", originalRelativePath},
			{"dst", dst},
			{"temp", temp},
			{"algorithm", "XXH3-64"},
				{"hash", hash},
				{"mechanism", mechanism},
				{"retirement", retirement},
				{"trashProvider", trashProvider},
				{"trashReceipt", trashReceipt},
				{"verificationRequested", verificationRequested},
				{"explicitSkip", explicitSkip},
				{"sourceRemoved", sourceRemoved},
				{"attempts", attempts},
				{"undoEntryId", undoEntryId},
				{"undoAction", undoAction},
			{"copyDurable", copyDurable},
			{"metadataComplete", metadataComplete},
			{"error", error},
			{"source", source.json()},
			{"landed", landed.json()},
			{"step", stepName(step)},
			{"artifacts", QJsonArray::fromStringList(artifacts)}};
}
std::optional<OpJournal::Entry> OpJournal::Entry::fromJson(const QJsonObject &v)
{
	Entry e;
	// This beta deliberately retains schema 3 while replacing the record contract.
	// Missing fields are incompatible, never silently interpreted as new defaults.
	for (const auto *key : {"mechanism", "retirement", "trashProvider", "trashReceipt", "undoAction"})
		if (!v[key].isString())
			return {};
	for (const auto *key : {"verificationRequested", "explicitSkip", "sourceRemoved"})
		if (!v[key].isBool())
			return {};
	if (!v["undoEntryId"].isDouble() || !v["attempts"].isDouble() ||
		v["attempts"].toInt(-1) < 0 || !v["item"].isObject())
		return {};
	const auto item = v["item"].toObject();
	for (const auto *key : {"expectedFileId", "expectedVolumeId", "expectedModified", "undoAction", "trashReceipt"})
		if (!item[key].isString())
			return {};
	if (!item["undoEntryId"].isDouble())
		return {};
	bool known = false;
	for (int n = 0; n <= int(Step::NeedsAttention); ++n)
		if (stepName(Step(n)) == v["step"].toString())
		{
			e.step = Step(n);
			known = true;
			break;
		}
	if (!known || v["algorithm"].toString() != "XXH3-64")
		return {};
	e.id = v["id"].toInt(-1);
	e.item = itemFromJson(v["item"].toObject());
	e.originalSource = v["original"].toString();
	e.originalVolume = VolumeIdentity::fromJson(v["originalVolume"].toObject());
	e.originalRelativePath = v["originalRelative"].toString();
	e.dst = v["dst"].toString();
	e.temp = v["temp"].toString();
	e.hash = v["hash"].toString();
	e.mechanism = v["mechanism"].toString();
	if (!e.mechanism.isEmpty() && e.mechanism != "copy" && e.mechanism != "relocate" &&
		e.mechanism != "systemTrash")
		return {};
	e.retirement = v["retirement"].toString();
	e.trashProvider = v["trashProvider"].toString();
	e.trashReceipt = v["trashReceipt"].toString();
	e.verificationRequested = v["verificationRequested"].toBool();
	e.explicitSkip = v["explicitSkip"].toBool();
	e.sourceRemoved = v["sourceRemoved"].toBool();
	e.attempts = v["attempts"].toInt();
	e.undoEntryId = v["undoEntryId"].toInt(-1);
	e.undoAction = v["undoAction"].toString();
	if (!e.undoAction.isEmpty() && e.undoAction != "restoreMove" && e.undoAction != "discardCopy" &&
		e.undoAction != "restoreTrash" && e.undoAction != "restoreRelocate")
		return {};
	e.copyDurable = v["copyDurable"].toBool();
	e.metadataComplete = v["metadataComplete"].toBool();
	e.error = v["error"].toString();
	e.source = OpStamp::fromJson(v["source"].toObject());
	e.landed = OpStamp::fromJson(v["landed"].toObject());
	if (e.step == Step::NoEffect && (!e.source.unchanged(e.landed) ||
		!QDir::isAbsolutePath(e.dst) || e.sourceRemoved || e.explicitSkip ||
		!e.mechanism.isEmpty() || !e.retirement.isEmpty()))
		return {}; // A no-effect result needs identity evidence, not an inferred Skip.
	for (const auto &a : v["artifacts"].toArray())
		e.artifacts.append(a.toString());
	if (e.id < 0 || !QDir::isAbsolutePath(e.item.src))
		return {};
	return e;
}
QString OpJournal::canonicalPath(const QString &path)
{
	if (path.isEmpty())
		return {};
	QFileInfo fi(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
	if (fi.isSymLink())
		return fi.absoluteFilePath(); // engine refuses the leaf link
	const auto parent = fi.absolutePath();
	const auto resolved = QFileInfo(parent).canonicalFilePath();
	if (!resolved.isEmpty())
		return QDir(resolved).filePath(fi.fileName());
	if (parent == fi.absoluteFilePath())
		return fi.absoluteFilePath();
	return QDir(canonicalPath(parent)).filePath(fi.fileName());
}
QString OpJournal::standardJournalDir()
{
	const auto overridePath = qEnvironmentVariable("MEDIAMUSTER_JOURNAL_DIR");
	auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (base.isEmpty())
		base = QDir::homePath() + "/.mediamuster";
	// Keep the existing location so older recovery records remain visible.
	return canonicalPath(overridePath.isEmpty() ? base + "/journal" : overridePath);
}
bool OpJournal::standardDirWritable()
{
	const auto d = standardJournalDir();
	return QDir().mkpath(d) && QFileInfo(d).isWritable();
}
std::unique_ptr<QLockFile> OpJournal::acquire(const QString &directory, QString &error)
{
	const auto dir = directory.isEmpty() ? standardJournalDir() : canonicalPath(directory);
	if (OpFile::makeDirectory(dir, error) != NativeFile::SyncResult::Ok)
		return {};
	auto lock = std::make_unique<QLockFile>(dir + "/engine.lock");
	lock->setStaleLockTime(0);
	if (!lock->tryLock(0))
	{
		error = "Another file operation or recovery owns the journal. Try again after it finishes.";
		return {};
	}
	return lock;
}
bool OpJournal::append(const QJsonObject &value)
{
	if (!m_healthy)
		return false;
	const auto line = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
	if (m_file.write(line) != line.size() ||
		NativeFile::syncFile(m_file) != NativeFile::SyncResult::Ok)
	{
		m_healthy = false;
		m_error = "The operation journal could not be saved. Further changes have stopped; files "
				  "and recovery records were retained.";
		return false;
	}
	return true;
}
bool OpJournal::create(const OpRequest &request, const QString &directory, QString &error)
{
	const auto dir = directory.isEmpty() ? standardJournalDir() : canonicalPath(directory);
	if (OpFile::makeDirectory(dir, error) != NativeFile::SyncResult::Ok)
		return false;
	m_record.request = request;
	m_record.started = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	m_file.setFileName(dir + "/operation-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
					   ".jsonl");
	if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
	{
		error = m_file.errorString();
		return false;
	}
	m_record.path = path();
	m_healthy = true;
	QJsonArray items, volumes;
	QSet<QString> seen;
	auto addVolume = [&](QString p)
	{
		while (!QFileInfo::exists(p) && QFileInfo(p).absolutePath() != p)
			p = QFileInfo(p).absolutePath();
		const auto v = VolumeIdentity::capture(p);
		if (seen.contains(v.rootPath))
			return;
		seen.insert(v.rootPath);
		m_record.volumes.append(v);
		volumes.append(v.toJson());
	};
	for (int n = 0; n < request.items.size(); ++n)
	{
		Entry e;
		e.id = n;
		e.item = request.items[n];
		e.originalSource = e.item.src;
		e.source = OpFile::inspect(e.item.src);
		if (!e.item.expectedFileId.isEmpty())
		{
			e.source.fileId = e.item.expectedFileId;
			e.source.volumeId = e.item.expectedVolumeId;
			e.source.size = e.item.bytes;
			e.source.modified = e.item.expectedModified;
		}
		e.verificationRequested = request.verifyCopies;
		e.explicitSkip = e.item.policy == "skip";
		e.undoAction = e.item.undoAction;
		e.undoEntryId = e.item.undoEntryId;
		e.trashReceipt = e.item.trashReceipt;
		e.originalVolume = VolumeIdentity::capture(e.item.src);
		e.originalRelativePath = QDir(e.originalVolume.rootPath).relativeFilePath(e.item.src);
		m_record.entries.append(e);
		items.append(e.json());
		addVolume(e.item.src);
		if (!e.item.renameDst.isEmpty())
			addVolume(QFileInfo(e.item.renameDst).absolutePath());
	}
	if (!request.destRoot.isEmpty())
		addVolume(request.destRoot);
	if (!request.diagnosticTrashRoot.isEmpty())
		addVolume(request.diagnosticTrashRoot);
	QString syncError;
	const bool ok = append({{"record", "begin"},
							{"schema", schema},
							{"kind", opKindName(request.kind)},
							{"dest", request.destRoot},
							{"preserve", request.preserve},
							{"verifyCopies", request.verifyCopies},
							{"copyThenRemove", request.copyThenRemove},
							{"undoOf", request.undoOf},
							{"copiesComplete", false},
							{"undoPath", QString()},
							{"started", m_record.started},
							{"diagnosticTrashRoot", request.diagnosticTrashRoot},
							{"volumes", volumes},
							{"items", items}}) &&
					NativeFile::syncDirectory(dir, &syncError) == NativeFile::SyncResult::Ok;
	if (!ok)
	{
		m_healthy = false;
		error = m_error.isEmpty()
					? QStringLiteral("Cannot persist the journal directory.\n") + syncError
					: m_error;
	}
	return ok;
}
bool OpJournal::resume(const Record &record, QString &error)
{
	if (record.corrupt)
	{
		error = "Journal contains an invalid record; it was preserved for inspection.";
		return false;
	}
	m_record = record;
	m_file.setFileName(record.path);
	if (!m_file.open(QIODevice::ReadWrite))
	{
		error = m_file.errorString();
		return false;
	}
	// Only an incomplete final line may be truncated; malformed complete lines
	// stop recovery. Previously durable records are never removed.
	if (record.torn && !m_file.resize(record.validBytes))
	{
		error = m_file.errorString();
		return false;
	}
	m_file.seek(m_file.size());
	m_healthy = true;
	QJsonArray entries, volumes;
	for (const auto &e : record.entries)
		entries.append(e.json());
	for (const auto &v : record.volumes)
		volumes.append(v.toJson());
	if (!append({{"record", "resolved"},
				 {"entries", entries},
				 {"volumes", volumes},
				 {"dest", record.request.destRoot},
				 {"diagnosticTrashRoot", record.request.diagnosticTrashRoot},
				 {"folders", QJsonArray::fromStringList(record.changedFolders)}}))
	{
		error = m_error;
		return false;
	}
	return true;
}
bool OpJournal::save(const Entry &entry)
{
	const bool added = entry.id == m_record.entries.size() && entry.step == Step::Planned;
	if (entry.id < 0 || (!added && entry.id >= m_record.entries.size()))
	{
		stop("Invalid journal item; operation stopped.");
		return false;
	}
	if (!append(entry.json()))
		return false;
	if (added)
		m_record.entries.append(entry);
	else
		m_record.entries[entry.id] = entry;
	return true;
}
bool OpJournal::touchFolder(const QString &folder)
{
	if (m_record.changedFolders.contains(folder))
		return true;
	if (!append({{"record", "folder"}, {"path", folder}}))
		return false;
	m_record.changedFolders.append(folder);
	return true;
}
bool OpJournal::finish(bool cancelled)
{
	if (!append({{"record", "stop"}, {"cancelled", cancelled}}))
		return false;
	m_record.stopped = true;
	return true;
}
bool OpJournal::markCopiesComplete()
{
	if (m_record.copiesComplete)
		return true;
	if (!append({{"record", "copies-complete"}}))
		return false;
	m_record.copiesComplete = true;
	return true;
}
bool OpJournal::claimUndo(const QString &undoPath)
{
	if (!QDir::isAbsolutePath(undoPath) || m_record.request.kind == OpKind::Undo ||
		undoPath == m_record.path ||
		(!m_record.undoPath.isEmpty() && m_record.undoPath != undoPath))
	{
		stop("This job is already claimed by another Undo, or its Undo reference is invalid.");
		return false;
	}
	if (m_record.undoPath == undoPath)
		return true;
	if (!append({{"record", "undo-claim"}, {"path", undoPath}}))
		return false;
	m_record.undoPath = undoPath;
	return true;
}

std::optional<OpJournal::Record> OpJournal::readOne(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	Record rec;
	rec.path = path;
	bool began = false;
	while (!file.atEnd())
	{
		const QByteArray line = file.readLine();
		if (!line.endsWith('\n'))
		{
			rec.torn = true;
			break;
		}
		QJsonParseError parse{};
		const auto doc = QJsonDocument::fromJson(line, &parse);
		if (parse.error != QJsonParseError::NoError || !doc.isObject())
		{
			rec.corrupt = true;
			break;
		}
		const auto v = doc.object();
		const auto type = v["record"].toString();
		if (!began)
		{
			if (type != "begin" || v["schema"].toInt() != schema)
				return {};
			if (!v["verifyCopies"].isBool() || !v["copyThenRemove"].isBool() ||
				!v["undoOf"].isString() || !v["copiesComplete"].isBool() ||
				!v["undoPath"].isString())
				return {}; // Incompatible beta record; no migration or default guessing.
			const auto kind = opKindFromName(v["kind"].toString());
			if (!kind)
				return {};
			rec.request.kind = *kind;
			rec.request.verifyCopies = v["verifyCopies"].toBool();
			rec.request.copyThenRemove = v["copyThenRemove"].toBool();
			rec.request.undoOf = v["undoOf"].toString();
			rec.copiesComplete = v["copiesComplete"].toBool();
			rec.undoPath = v["undoPath"].toString();
			if ((rec.request.kind == OpKind::Undo && !QDir::isAbsolutePath(rec.request.undoOf)) ||
				(rec.request.kind != OpKind::Undo && !rec.request.undoOf.isEmpty()) ||
				(!rec.undoPath.isEmpty() && !QDir::isAbsolutePath(rec.undoPath)))
				return {};
			rec.request.destRoot = v["dest"].toString();
			rec.request.preserve = v["preserve"].toBool();
			rec.request.diagnosticTrashRoot = v["diagnosticTrashRoot"].toString();
			rec.started = v["started"].toString();
			for (const auto &a : v["volumes"].toArray())
				rec.volumes.append(VolumeIdentity::fromJson(a.toObject()));
			for (const auto &a : v["items"].toArray())
			{
				const auto entry = Entry::fromJson(a.toObject());
				if (!entry || entry->id != rec.entries.size())
				{
					rec.corrupt = true;
					break;
				}
				rec.entries.append(*entry);
				rec.request.items.append(entry->item);
			}
			began = true;
		}
		else if (type == "resolved")
		{
			QVector<Entry> entries;
			for (const auto &a : v["entries"].toArray())
			{
				const auto entry = Entry::fromJson(a.toObject());
				if (!entry || entry->id != entries.size())
				{
					rec.corrupt = true;
					break;
				}
				entries.append(*entry);
			}
			if (entries.size() != rec.entries.size())
			{
				rec.corrupt = true;
				break;
			}
			rec.entries = entries;
			rec.volumes.clear();
			for (const auto &a : v["volumes"].toArray())
				rec.volumes.append(VolumeIdentity::fromJson(a.toObject()));
			rec.request.destRoot = v["dest"].toString();
			rec.request.diagnosticTrashRoot = v["diagnosticTrashRoot"].toString();
			rec.changedFolders.clear();
			for (const auto &a : v["folders"].toArray())
				rec.changedFolders.append(a.toString());
		}
		else if (type == "item")
		{
			const auto entry = Entry::fromJson(v);
			if (!entry || entry->id > rec.entries.size() ||
				(entry->id == rec.entries.size() && entry->step != Step::Planned))
			{
				rec.corrupt = true;
				break;
			}
			if (entry->id == rec.entries.size())
				rec.entries.append(*entry);
			else
				rec.entries[entry->id] = *entry;
		}
		else if (type == "folder")
			rec.changedFolders.append(v["path"].toString());
		else if (type == "stop")
			rec.stopped = true;
		else if (type == "copies-complete")
			rec.copiesComplete = true;
		else if (type == "undo-claim")
		{
			const auto inverse = v["path"].toString();
			if (!QDir::isAbsolutePath(inverse) || rec.request.kind == OpKind::Undo ||
				(!rec.undoPath.isEmpty() && rec.undoPath != inverse))
			{
				rec.corrupt = true;
				break;
			}
			rec.undoPath = inverse;
		}
		else if (type == "dismiss")
			rec.dismissed = true;
		else
		{
			rec.corrupt = true;
			break;
		}
		rec.validBytes = file.pos();
	}
	for (int n = 0; n < rec.request.items.size() && n < rec.entries.size(); ++n)
		rec.request.items[n] = rec.entries[n].item;
	return began ? std::optional<Record>(rec) : std::nullopt;
}
QVector<OpJournal::Record> OpJournal::scan(const QString &directory)
{
	QDir dir(directory.isEmpty() ? standardJournalDir() : directory);
	QVector<Record> out;
	for (const auto &name : dir.entryList({"operation-*.jsonl"}, QDir::Files, QDir::Name))
		if (auto r = readOne(dir.filePath(name)))
			out.append(*r);
	std::sort(out.begin(), out.end(), [](const Record &a, const Record &b)
			  { return a.started == b.started ? a.path < b.path : a.started < b.started; });
	return out;
}
QVector<OpJournal::Record> OpJournal::interrupted(const QString &directory)
{
	QVector<Record> out;
	const auto records = scan(directory);
	QSet<QString> claimed;
	for (const auto &record : records)
		if (!record.corrupt && record.request.kind == OpKind::Undo)
			claimed.insert(record.request.undoOf);
	for (const auto &record : records)
	{
		if (record.corrupt || record.dismissed || !record.undoPath.isEmpty() || claimed.contains(record.path))
			continue;
		if (std::any_of(record.entries.cbegin(), record.entries.cend(),
						[](const Entry &entry) { return !entry.complete(); }))
			out.append(record);
	}
	return out;
}
std::optional<OpJournal::Record> OpJournal::latestUndoable(const QString &directory)
{
	const auto records = scan(directory);
	QSet<QString> claimed;
	for (const auto &record : records)
		if (!record.corrupt && record.request.kind == OpKind::Undo)
			claimed.insert(record.request.undoOf);
	for (auto it = records.crbegin(); it != records.crend(); ++it)
	{
		if (it->corrupt)
			continue;
		if (it->request.kind == OpKind::Undo)
			return {}; // One level of Undo; completing it does not expose older jobs.
		if (!it->undoPath.isEmpty() || claimed.contains(it->path))
			return {};
		for (const auto &entry : it->entries)
		{
			if (entry.item.maintenance || entry.step == Step::NoEffect)
				continue;
			if (entry.step == Step::Done || entry.step == Step::SourceRemoved ||
				entry.step == Step::Published || entry.step == Step::SourceRetained ||
				(entry.mechanism == "copy" && entry.landed.valid() &&
				 (entry.step == Step::RemovingSource || entry.step == Step::Publishing ||
				  entry.step == Step::NeedsAttention)) ||
				(entry.mechanism == "relocate" && entry.source.valid() &&
				 (entry.step == Step::Relocating || entry.step == Step::NeedsAttention)) ||
				(entry.mechanism == "systemTrash" && entry.step == Step::NeedsAttention &&
				 !entry.trashReceipt.isEmpty() && entry.landed.valid()))
				// Ambiguous final appends are candidates for the Undo planner's live
				// reconciliation, not a claim that a filesystem mutation succeeded.
				return *it;
		}
	}
	return {};
}
QStringList OpJournal::unreadableRecords(const QString &directory)
{
	QDir dir(directory.isEmpty() ? standardJournalDir() : directory);
	QStringList out;
	for (const auto &name : dir.entryList({"*.jsonl"}, QDir::Files, QDir::Name))
		if (!name.startsWith("operation-") || !readOne(dir.filePath(name)))
			out.append(dir.filePath(name));
	return out;
}
bool OpJournal::dismiss(const QString &path, QString &error)
{
	auto lock = acquire(QFileInfo(path).absolutePath(), error);
	if (!lock)
		return false;
	auto rec = readOne(path);
	if (!rec)
	{
		error = "Cannot read recovery record.";
		return false;
	}
	if (rec->dismissed)
		return true;
	// Abandonment ends future work; it neither resolves storage nor removes
	// completed effects, retained originals, partials or their recovery evidence.
	OpJournal j;
	if (!j.resume(*rec, error))
		return false;
	if (!j.append({{"record", "dismiss"}}))
	{
		error = j.error();
		return false;
	}
	return true;
}
bool OpJournal::resolve(Record &rec, QString &error, const QVector<VolumeIdentity> &overrideVolumes)
{
	QVector<VolumeIdentity> mounted = overrideVolumes;
	if (mounted.isEmpty())
	{
		for (const auto &v : QStorageInfo::mountedVolumes())
			if (v.isValid() && v.isReady())
				mounted.append(VolumeIdentity::capture(v.rootPath()));
		// Direct UNC shares need not be enumerated as mapped Windows drives.
		// Their recorded endpoint can still be checked at its original path.
		for (const auto &old : rec.volumes)
		{
			const auto current = VolumeIdentity::capture(old.rootPath);
			if (!current.rootPath.isEmpty())
				mounted.append(current);
		}
	}
	QHash<QString, QString> roots;
	for (const auto &old : rec.volumes)
	{
		QString found;
		for (const auto &now : mounted)
		{
			if (old.matches(now))
			{
				if (!found.isEmpty() && found != now.rootPath)
				{
					error = "The recorded volume has more than one possible mount; files were "
							"retained.";
					return false;
				}
				found = now.rootPath;
			}
		}
		if (found.isEmpty())
		{
			error = QStringLiteral("Cannot establish the recorded volume at %1. Reconnect the "
								   "original storage; recovery records are retained.")
						.arg(old.rootPath);
			return false;
		}
		roots.insert(old.rootPath, found);
	}
	auto rewrite = [&](QString p)
	{
		QString owner;
		for (auto it = roots.cbegin(); it != roots.cend(); ++it)
			if (inside(p, it.key()) && it.key().size() > owner.size())
				owner = it.key();
		return owner.isEmpty() ? p : QDir(roots[owner]).filePath(QDir(owner).relativeFilePath(p));
	};
	rec.request.destRoot = rewrite(rec.request.destRoot);
	rec.request.diagnosticTrashRoot = rewrite(rec.request.diagnosticTrashRoot);
	for (auto &e : rec.entries)
	{
		e.item.src = rewrite(e.item.src);
		e.item.renameDst = rewrite(e.item.renameDst);
		e.dst = rewrite(e.dst);
		e.temp = rewrite(e.temp);
		e.retirement = rewrite(e.retirement);
		for (auto &p : e.artifacts)
			p = rewrite(p);
		// A positively resolved volume can change its OS device number after
		// remount. Rebind only when file ID, length and mtime still agree.
		auto rebind = [](OpStamp &old, const QString &path)
		{
			const auto now = OpFile::inspect(path);
			if (old.valid() && now.valid() && old.fileId == now.fileId && old.size == now.size &&
				old.modified == now.modified)
				old.volumeId = now.volumeId;
		};
		rebind(e.source, e.item.src);
		if (!e.retirement.isEmpty())
			rebind(e.source, e.retirement);
		if (e.mechanism == "relocate" || e.mechanism == "systemTrash")
			rebind(e.source, e.dst);
		rebind(e.landed, OpFile::occupied(e.temp) ? e.temp : e.dst);
		if (!e.item.expectedFileId.isEmpty() && e.source.fileId == e.item.expectedFileId)
			e.item.expectedVolumeId = e.source.volumeId;
	}
	for (auto &folder : rec.changedFolders)
		folder = rewrite(folder);
	for (auto &v : rec.volumes)
		v.rootPath = roots.value(v.rootPath, v.rootPath);
	for (int n = 0; n < rec.request.items.size() && n < rec.entries.size(); ++n)
		rec.request.items[n] = rec.entries[n].item;
	return true;
}
