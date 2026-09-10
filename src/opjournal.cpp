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
			{"group", i.groupKey}};
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
	case Step::Verified:
		return "verified";
	case Step::Publishing:
		return "publishing";
	case Step::Published:
		return "published";
	case Step::Relocating:
		return "relocating";
	case Step::Done:
		return "done";
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
	return step == Step::Done || step == Step::SourceRetained || step == Step::Skipped;
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
	e.copyDurable = v["copyDurable"].toBool();
	e.metadataComplete = v["metadataComplete"].toBool();
	e.error = v["error"].toString();
	e.source = OpStamp::fromJson(v["source"].toObject());
	e.landed = OpStamp::fromJson(v["landed"].toObject());
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
		NativeFile::syncFile(m_file, NativeFile::Durability::Platter) != NativeFile::SyncResult::Ok)
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
	return append({{"record", "stop"}, {"cancelled", cancelled}});
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
			const auto kind = opKindFromName(v["kind"].toString());
			if (!kind || *kind == OpKind::Undo)
				return {};
			rec.request.kind = *kind;
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
		else if (type == "dismiss")
			rec.dismissed = true;
		else
		{
			rec.corrupt = true;
			break;
		}
		rec.validBytes = file.pos();
	}
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
	for (const auto &e : rec->entries)
		if (!e.artifacts.isEmpty() || e.step == Step::NeedsAttention ||
			e.step == Step::Publishing || e.step == Step::Relocating)
		{
			error = "This operation has unresolved files. Its recovery record must be retained.";
			return false;
		}
	OpJournal j;
	if (!j.resume(*rec, error))
		return false;
	return j.append({{"record", "dismiss"}});
}
bool OpJournal::resolve(Record &rec, QString &error, const QVector<VolumeIdentity> &overrideVolumes)
{
	QVector<VolumeIdentity> mounted = overrideVolumes;
	if (mounted.isEmpty())
		for (const auto &v : QStorageInfo::mountedVolumes())
			if (v.isValid() && v.isReady())
				mounted.append(VolumeIdentity::capture(v.rootPath()));
	QHash<QString, QString> roots;
	for (const auto &old : rec.volumes)
	{
		QString found;
		for (const auto &now : mounted)
		{
			const bool strong = old.confidence == VolumeIdentity::Confidence::High &&
								now.confidence == VolumeIdentity::Confidence::High &&
								old.matches(now);
			if (strong)
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
		rebind(e.landed, OpFile::occupied(e.temp) ? e.temp : e.dst);
	}
	for (auto &folder : rec.changedFolders)
		folder = rewrite(folder);
	for (auto &v : rec.volumes)
		v.rootPath = roots.value(v.rootPath, v.rootPath);
	return true;
}
