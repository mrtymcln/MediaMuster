#include "opjournal.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

#include <optional>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <io.h>
#endif

namespace
{
	// Bump this only when the on-disk shape changes in a way old readers
	// can't cope with. Readers branch on it; never reuse a number.
	//
	// Adding Kind::Copy did NOT need a bump: an older build reads kind:"copy"
	// as Kind::Move (kindFromName's fallback) and reverses it with
	// reverseMoveLike, which on a Copy record restores the parked original and
	// deletes nothing it shouldn't. Degraded, not dangerous.
	constexpr int kSchema = 1;

	// Push a just-written line down to the drive. Plain fsync — not macOS's
	// F_FULLFSYNC — is deliberate: fsync survives an app crash and a
	// drive-acknowledged power loss, which covers the realistic failure modes for
	// this tool. F_FULLFSYNC additionally survives a power cut *during* the drive's
	// own cache-to-platter flush, but it measured ~4.8 ms/call on this hardware
	// (~48 s of pure sync on a 5,000-file rebalance, at two syncs per file) — a
	// cost far out of proportion to that residual risk on a workstation or Nexis.
	bool syncFile(QFile &f)
	{
		if (!f.flush())
			return false;
		const int fd = f.handle();
		if (fd == -1)
			return false;
#if defined(Q_OS_UNIX)
		return ::fsync(fd) == 0;
#elif defined(Q_OS_WIN)
		return ::_commit(fd) == 0;
#else
		return true;
#endif
	}

	// Syncing a file's bytes doesn't persist the directory entry that names
	// it, so a power cut right after create could lose the whole journal. One
	// sync of the parent dir after create closes that window — on Unix.
	// Windows is knowingly uncovered: _commit can't take a directory, and
	// NTFS's own metadata journaling narrows the same window, so the residual
	// risk is accepted rather than half-fixed. (If that ever changes, the
	// upgrade path is CreateFileW with FILE_FLAG_BACKUP_SEMANTICS +
	// FlushFileBuffers.)
	void syncDir(const QString &dirPath)
	{
#if defined(Q_OS_UNIX)
		const int fd = ::open(QFile::encodeName(dirPath).constData(), O_RDONLY);
		if (fd != -1)
		{
			::fsync(fd);
			::close(fd);
		}
#else
		Q_UNUSED(dirPath);
#endif
	}
} // namespace

// MARK: - Construction

OpJournal::OpJournal(Kind kind, const QJsonObject &meta, const QString &dir)
{
	const QString base = dir.isEmpty() ? standardOplogDir() : dir;
	if (!QDir().mkpath(base))
		return;

	// Timestamp orders the files chronologically; the uuid tail keeps
	// two runs started in the same millisecond from colliding.
	const QString stamp =
		QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz"));
	const QString tag = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
	m_path = base + QStringLiteral("/oplog-%1-%2.jsonl").arg(stamp, tag);

	m_file = std::make_unique<QFile>(m_path);
	if (!m_file->open(QIODevice::WriteOnly))
	{
		m_file.reset();
		m_path.clear();
		return;
	}

	writeLine(
		{{QStringLiteral("schema"), kSchema},
		 {QStringLiteral("rec"), QStringLiteral("begin")},
		 {QStringLiteral("kind"), kindName(kind)},
		 {QStringLiteral("started"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
		 {QStringLiteral("app"), QCoreApplication::applicationVersion()},
		 {QStringLiteral("pid"), QCoreApplication::applicationPid()},
		 {QStringLiteral("host"), QSysInfo::machineHostName()},
		 {QStringLiteral("meta"), meta}});

	// Make the journal's existence durable, not just its first line's
	// bytes, so recovery can find it after a power cut.
	syncDir(base);
}

OpJournal::~OpJournal() = default;

bool OpJournal::isOpen() const
{
	return m_file && m_file->isOpen();
}

// MARK: - Per-op records

int OpJournal::planOp(const QString &src, const QString &dst, qint64 bytes, const QString &parked)
{
	const int id = m_nextId++;

	QJsonObject o{{QStringLiteral("rec"), QStringLiteral("op")},
				  {QStringLiteral("id"), id},
				  {QStringLiteral("src"), src},
				  {QStringLiteral("dst"), dst}};
	if (bytes > 0)
		o.insert(QStringLiteral("bytes"), QJsonValue(bytes));
	if (!parked.isEmpty())
		o.insert(QStringLiteral("parked"), parked);

	writeLine(o);
	return id;
}

void OpJournal::markDone(int id, const QString &finalPath)
{
	QJsonObject o{{QStringLiteral("rec"), QStringLiteral("done")}, {QStringLiteral("id"), id}};
	if (!finalPath.isEmpty())
		o.insert(QStringLiteral("final"), finalPath);
	writeLine(o);
}

void OpJournal::markFailed(int id, const QString &error, bool rollbackIncomplete)
{
	QJsonObject o{{QStringLiteral("rec"), QStringLiteral("fail")},
				  {QStringLiteral("id"), id},
				  {QStringLiteral("err"), error}};
	// Adding `dirty` did NOT need a schema bump: an old reader ignores the
	// field and treats this as a plain fail — it just won't retry the
	// rollback, which is the pre-fix behaviour. Degraded, not dangerous.
	if (rollbackIncomplete)
	{
		o.insert(QStringLiteral("dirty"), true);
		m_hasDirty = true;
	}
	writeLine(o);
}

void OpJournal::markSkipped(int id)
{
	writeLine({{QStringLiteral("rec"), QStringLiteral("skip")}, {QStringLiteral("id"), id}});
}

void OpJournal::finish(int succeeded, int failed, int skipped, bool cancelled)
{
	if (!isOpen())
		return;

	QJsonObject o{
		{QStringLiteral("rec"), QStringLiteral("end")},
		{QStringLiteral("ok"), succeeded},
		{QStringLiteral("fail"), failed},
		{QStringLiteral("skip"), skipped},
		{QStringLiteral("ended"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
	if (cancelled)
		o.insert(QStringLiteral("cancelled"), true);
	// Forensics only — the read side computes dirtiness from the fail lines,
	// which survive a crash between the last op and this end line.
	if (m_hasDirty)
		o.insert(QStringLiteral("dirty"), true);
	writeLine(o);

	m_finished = true;
	m_file->close();
}

void OpJournal::prune()
{
	// Only delete a concluded journal; deleting an open one would lose a
	// run we might still need to roll back. A dirty journal is never
	// pruned: it's the only record of where a stranded original is parked,
	// and next-launch recovery reads it to finish the rollback.
	//
	// Degraded overrides dirty: with lines missing, the on-disk file no
	// longer tells the truth — its end line (or the dirty marker itself)
	// may never have landed, and recovery reading it as an interrupted run
	// would "roll back" moves that actually finished. A degraded journal is
	// worse than none; the caller's critical log is the surviving record.
	if (m_finished && (!m_hasDirty || m_degraded) && !m_path.isEmpty())
		QFile::remove(m_path);
}

bool OpJournal::markRecovered(const QString &journalPath, int reversed, int failed)
{
	QFile f(journalPath);
	if (!f.open(QIODevice::Append))
		return false;

	const QJsonObject o{
		{QStringLiteral("rec"), QStringLiteral("recovered")},
		{QStringLiteral("reversed"), reversed},
		{QStringLiteral("failed"), failed},
		{QStringLiteral("ts"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
	f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
	f.write("\n", 1);
	syncFile(f);
	f.close();
	return true;
}

void OpJournal::writeLine(const QJsonObject &obj)
{
	if (!m_file || !m_file->isOpen())
		return;
	// A WAL line that never reached the disk protects nothing. Track the
	// first failure (full disk, dying drive) as permanent degradation: the
	// callers warn the user once, and prune() disposes of the file rather
	// than leaving a half-told story for recovery to misread.
	const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
	bool ok = m_file->write(line) == line.size();
	ok = m_file->write("\n", 1) == 1 && ok;
	ok = syncFile(*m_file) && ok;
	if (!ok)
		m_degraded = true;
}

// MARK: - Locations

QString OpJournal::standardOplogDir()
{
	// Escape hatch for tests, and for moving the journal off a full system
	// disk. Points straight at the oplog dir, no /oplog suffix.
	const QString override = qEnvironmentVariable("MEDIAMUSTER_OPLOG_DIR");
	if (!override.isEmpty())
		return override;

	QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	// AppDataLocation can come back empty on a misconfigured box; fall
	// back to the dotfolder so we never silently lose the journal.
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.mediamuster");
	return base + QStringLiteral("/oplog");
}

bool OpJournal::standardDirWritable()
{
	const QString dir = standardOplogDir();
	if (!QDir().mkpath(dir))
		return false;
	QFile probe(dir + QStringLiteral("/.write-probe-") +
				QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
	if (!probe.open(QIODevice::WriteOnly))
		return false;
	const bool ok = probe.write("x", 1) == 1;
	probe.close();
	QFile::remove(probe.fileName());
	return ok;
}

// MARK: - Kind <-> string

QString OpJournal::openFailedText(Kind k)
{
	return QStringLiteral("Couldn't open the operations journal — this %1 runs "
						  "without crash recovery.")
		.arg(kindName(k));
}

QString OpJournal::degradedText()
{
	return QStringLiteral("The operations journal stopped accepting writes (disk "
						  "full?). The run continues, but crash recovery can't "
						  "protect files from here on.");
}

QString OpJournal::kindName(Kind k)
{
	switch (k)
	{
	case Kind::Move:
		return QStringLiteral("move");
	case Kind::Rebalance:
		return QStringLiteral("rebalance");
	case Kind::Delete:
		return QStringLiteral("delete");
	case Kind::Copy:
		return QStringLiteral("copy");
	}
	return QStringLiteral("move");
}

OpJournal::Kind OpJournal::kindFromName(const QString &s, bool *ok)
{
	if (ok)
		*ok = true;
	if (s == QStringLiteral("move"))
		return Kind::Move;
	if (s == QStringLiteral("rebalance"))
		return Kind::Rebalance;
	if (s == QStringLiteral("delete"))
		return Kind::Delete;
	if (s == QStringLiteral("copy"))
		return Kind::Copy;
	if (ok)
		*ok = false;
	return Kind::Move;
}

// MARK: - Recovery (read side)

std::optional<OpJournal::Record> OpJournal::readOne(const QString &journalPath)
{
	QFile f(journalPath);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return std::nullopt;

	Record rec;
	rec.path = journalPath;

	// op id to its index in rec.ops, so outcome lines can find their op.
	QHash<int, int> idToIdx;

	while (!f.atEnd())
	{
		const QByteArray line = f.readLine().trimmed();
		if (line.isEmpty())
			continue;

		// A torn final line (crash mid-write) is expected, not fatal.
		const QJsonDocument doc = QJsonDocument::fromJson(line);
		if (!doc.isObject())
			continue;
		const QJsonObject o = doc.object();
		const QString r = o.value(QStringLiteral("rec")).toString();

		if (r == QStringLiteral("begin"))
		{
			rec.kind = kindFromName(o.value(QStringLiteral("kind")).toString());
			rec.meta = o.value(QStringLiteral("meta")).toObject();
			rec.started = o.value(QStringLiteral("started")).toString();
			rec.pid = o.value(QStringLiteral("pid")).toInteger(0);
			rec.host = o.value(QStringLiteral("host")).toString();
		}
		else if (r == QStringLiteral("op"))
		{
			Entry e;
			e.id = o.value(QStringLiteral("id")).toInt(-1);
			e.src = o.value(QStringLiteral("src")).toString();
			e.dst = o.value(QStringLiteral("dst")).toString();
			e.bytes = o.value(QStringLiteral("bytes")).toInteger(0);
			e.parked = o.value(QStringLiteral("parked")).toString();
			idToIdx.insert(e.id, rec.ops.size());
			rec.ops.append(e);
		}
		else if (r == QStringLiteral("done"))
		{
			const int idx = idToIdx.value(o.value(QStringLiteral("id")).toInt(-1), -1);
			if (idx >= 0)
			{
				rec.ops[idx].completed = true;
				rec.ops[idx].finalPath = o.value(QStringLiteral("final")).toString();
			}
		}
		else if (r == QStringLiteral("fail"))
		{
			const int idx = idToIdx.value(o.value(QStringLiteral("id")).toInt(-1), -1);
			if (idx >= 0)
			{
				rec.ops[idx].failed = true;
				if (o.value(QStringLiteral("dirty")).toBool(false))
				{
					rec.ops[idx].rollbackIncomplete = true;
					rec.dirty = true;
				}
			}
		}
		else if (r == QStringLiteral("skip"))
		{
			const int idx = idToIdx.value(o.value(QStringLiteral("id")).toInt(-1), -1);
			if (idx >= 0)
				rec.ops[idx].skipped = true;
		}
		else if (r == QStringLiteral("end"))
		{
			rec.complete = true;
			rec.cancelled = o.value(QStringLiteral("cancelled")).toBool(false);
		}
		else if (r == QStringLiteral("recovered"))
		{
			rec.recovered = true;
		}
	}

	return rec;
}

QVector<OpJournal::Record> OpJournal::scan(const QString &dir)
{
	const QString base = dir.isEmpty() ? standardOplogDir() : dir;
	QVector<Record> out;

	QDir d(base);
	if (!d.exists())
		return out;

	// Name sort == chronological, since the filename leads with a
	// zero-padded UTC timestamp.
	const QStringList files =
		d.entryList({QStringLiteral("oplog-*.jsonl")}, QDir::Files, QDir::Name);

	for (const QString &name : files)
	{
		// A file we can't open (or that vanished mid-sweep) simply isn't part
		// of the recovery set; skip it rather than fail the whole scan.
		if (const auto rec = readOne(d.filePath(name)))
			out.append(*rec);
	}

	return out;
}