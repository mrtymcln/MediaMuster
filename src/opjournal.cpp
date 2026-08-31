#include "opjournal.h"

#include "nativefile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

#include <atomic>

namespace
{
	// Bump only when the on-disk shape changes in a way readers can't
	// cope with. The reader REFUSES anything that isn't this exact value:
	// no backwards compatibility during the beta (Marty's decision), and
	// forwards a newer build's journals are simply not ours to interpret.
	constexpr int kSchema = 2;

	// Push a just-written line down to the drive. The Disk barrier
	// (fsync/_commit) is the deliberate choice for journal LINES — it
	// survives an app crash and a drive-acknowledged power loss, which
	// covers the realistic failure modes at a per-line cost the run can
	// afford. The MEDIA files get the harder Platter barrier at the one
	// instant that matters (see the copier); the journal doesn't need it:
	// losing the very last line to a heroic power cut leaves an
	// interrupted-looking run whose recovery pass reads the DISK for
	// evidence anyway.
	bool syncLine(QFile &f)
	{
		return NativeFile::syncFile(f, NativeFile::Durability::Disk) !=
			   NativeFile::SyncResult::Failed;
	}

	QString nowIso()
	{
		return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
	}
} // namespace

// MARK: - Construction

OpJournal::OpJournal(OpKind kind, const QJsonObject &meta, const QString &dir,
				   const QString &sparePath)
{
	const QString base = dir.isEmpty() ? standardJournalDir() : dir;
	if (!QDir().mkpath(base))
		return;

	// THE undo-invalidation choke point: a new operation supersedes the
	// previous run's undo candidate, so every finished, non-dirty journal
	// dies here — before this run writes its first line. One place is
	// both the retention policy and the invalidation rule, so the two
	// can never disagree. (An Undo run spares the journal it reverses.)
	pruneSuperseded(base, sparePath);

	// Timestamp orders the files chronologically. Two runs in the SAME
	// millisecond would tie on the stamp and be ordered by the random
	// uuid — and name order is how scan() and latestUndoable() decide
	// "newest" — so a process-local sequence number breaks the tie
	// deterministically. The uuid tail still keeps two PROCESSES started
	// in the same millisecond from colliding on a filename.
	static std::atomic<quint32> s_sequence{0};
	const QString stamp =
		QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz"));
	const QString seq = QStringLiteral("%1").arg(s_sequence.fetch_add(1) % 10000, 4,
												 10, QLatin1Char('0'));
	const QString tag = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
	m_path = base + QStringLiteral("/journal-%1-%2-%3.jsonl").arg(stamp, seq, tag);

	m_file = std::make_unique<QFile>(m_path);
	if (!m_file->open(QIODevice::WriteOnly))
	{
		m_file.reset();
		m_path.clear();
		return;
	}

	writeLine({{QStringLiteral("schema"), kSchema},
			   {QStringLiteral("record"), QStringLiteral("begin")},
			   {QStringLiteral("kind"), opKindName(kind)},
			   {QStringLiteral("started"), nowIso()},
			   {QStringLiteral("appVersion"), QCoreApplication::applicationVersion()},
			   {QStringLiteral("processId"), QCoreApplication::applicationPid()},
			   {QStringLiteral("host"), QSysInfo::machineHostName()},
			   {QStringLiteral("metadata"), meta}});

	// Make the journal's EXISTENCE durable, not just its first line's
	// bytes: a file whose directory entry is lost to a power cut protects
	// nothing. Real on both platforms now (NativeFile::syncDirectory).
	NativeFile::syncDirectory(base);
}

OpJournal::~OpJournal() = default;

bool OpJournal::isOpen() const
{
	return m_file && m_file->isOpen();
}

// MARK: - Plan

void OpJournal::writePlan(const QString &dest, bool preserve, const QVector<OpItem> &items,
						 const QVector<VolumeIdentity> &volumes)
{
	QJsonArray files;
	for (const OpItem &it : items)
	{
		QJsonObject o{{QStringLiteral("source"), it.src}, {QStringLiteral("name"), it.name}};
		if (!it.folder.isEmpty())
			o.insert(QStringLiteral("folder"), it.folder);
		if (it.bytes > 0)
			o.insert(QStringLiteral("bytes"), QJsonValue(it.bytes));
		if (!it.policy.isEmpty())
			o.insert(QStringLiteral("policy"), it.policy);
		// The scan's claims about the media, so resumed runs keep their
		// cross-checks and every message can name the clip.
		if (!it.mobId.isEmpty())
			o.insert(QStringLiteral("mobId"), it.mobId);
		if (!it.masterMobId.isEmpty())
			o.insert(QStringLiteral("masterMobId"), it.masterMobId);
		if (!it.clipName.isEmpty())
			o.insert(QStringLiteral("clipName"), it.clipName);
		// Rename (Rebalance) items carry their own full destination and
		// their relatives-atomic group.
		if (!it.renameDst.isEmpty())
			o.insert(QStringLiteral("destination"), it.renameDst);
		if (!it.groupKey.isEmpty())
			o.insert(QStringLiteral("group"), it.groupKey);
		files.append(o);
	}

	// The fingerprint of every volume this run touches, so recovery and
	// undo can re-find a drive that came back under a different name —
	// and refuse a different drive squatting at the recorded address.
	QJsonArray vols;
	for (const VolumeIdentity &v : volumes)
		vols.append(v.toJson());

	writeLine({{QStringLiteral("record"), QStringLiteral("plan")},
			   {QStringLiteral("destination"), dest},
			   {QStringLiteral("preserve"), preserve},
			   {QStringLiteral("files"), files},
			   {QStringLiteral("volumes"), vols}});
}

// MARK: - Per-op records

int OpJournal::planOp(const QString &src, const QString &dst, qint64 bytes, const QString &parked,
					 const FileIdentity &srcId, const FileIdentity &parkedOriginalId)
{
	const int id = m_nextId++;

	QJsonObject o{{QStringLiteral("record"), QStringLiteral("op")},
				  {QStringLiteral("id"), id},
				  {QStringLiteral("source"), src},
				  {QStringLiteral("destination"), dst}};
	if (bytes > 0)
		o.insert(QStringLiteral("bytes"), QJsonValue(bytes));
	if (!parked.isEmpty())
		o.insert(QStringLiteral("parked"), parked);
	// The identities this op was verified against. Recovery and undo
	// re-verify against THESE, not against fresh guesses.
	if (srcId.confidence != FileIdentity::Confidence::Low)
		o.insert(QStringLiteral("sourceId"), srcId.toJson());
	if (parkedOriginalId.confidence != FileIdentity::Confidence::Low)
		o.insert(QStringLiteral("destinationId"), parkedOriginalId.toJson());

	writeLine(o);
	return id;
}

void OpJournal::markDone(int id, const DoneInfo &info)
{
	QJsonObject o{{QStringLiteral("record"), QStringLiteral("done")}, {QStringLiteral("id"), id}};
	if (!info.finalPath.isEmpty())
		o.insert(QStringLiteral("final"), info.finalPath);
	if (!info.hash.isEmpty())
		o.insert(QStringLiteral("hash"), info.hash);
	if (info.landedId.confidence != FileIdentity::Confidence::Low)
		o.insert(QStringLiteral("destinationId"), info.landedId.toJson());
	if (!info.parkedFinal.isEmpty())
		o.insert(QStringLiteral("parkedFinal"), info.parkedFinal);
	writeLine(o);
}

void OpJournal::markFailed(int id, const QString &error, bool rollbackIncomplete)
{
	QJsonObject o{{QStringLiteral("record"), QStringLiteral("fail")},
				  {QStringLiteral("id"), id},
				  {QStringLiteral("error"), error}};
	if (rollbackIncomplete)
	{
		o.insert(QStringLiteral("dirty"), true);
		m_hasDirty = true;
	}
	writeLine(o);
}

void OpJournal::markSkipped(int id)
{
	writeLine({{QStringLiteral("record"), QStringLiteral("skip")}, {QStringLiteral("id"), id}});
}

void OpJournal::writeNote(const QString &text)
{
	writeLine({{QStringLiteral("record"), QStringLiteral("note")},
			   {QStringLiteral("text"), text},
			   {QStringLiteral("timestamp"), nowIso()}});
}

// MARK: - Finish

void OpJournal::finish(int succeeded, int failed, int skipped, bool cancelled)
{
	if (!isOpen())
		return;

	QJsonObject o{{QStringLiteral("record"), QStringLiteral("end")},
				  {QStringLiteral("succeeded"), succeeded},
				  {QStringLiteral("failed"), failed},
				  {QStringLiteral("skipped"), skipped},
				  {QStringLiteral("ended"), nowIso()}};
	if (cancelled)
		o.insert(QStringLiteral("cancelled"), true);
	// Forensics only — the read side computes dirtiness from the fail
	// lines, which survive a crash between the last op and this end line.
	if (m_hasDirty)
		o.insert(QStringLiteral("dirty"), true);
	writeLine(o);

	m_finished = true;
	m_file->close();

	// A finished journal normally STAYS — it is the undo candidate (the
	// v2 retention change). The one exception: degraded. With lines
	// missing, the on-disk file no longer tells the truth — recovery
	// could read a finished run as interrupted and "roll back" work that
	// completed, and undo would reverse from an incomplete record.
	// Degraded overrides even dirty: a half-told story is worse than
	// none, and the caller's critical log is the surviving record.
	if (m_degraded && !m_path.isEmpty())
		QFile::remove(m_path);
}

// MARK: - Retention

void OpJournal::pruneSuperseded(const QString &dir, const QString &sparePath)
{
	// scan() only ever returns schema-2 records, so legacy files are
	// structurally safe from this sweep — invisible, untouched.
	for (const Record &rec : scan(dir))
	{
		if (!sparePath.isEmpty() && rec.path == sparePath)
			continue; // the journal an undo is reversing; see the ctor
		// Finished and clean = a superseded undo candidate. Interrupted
		// journals (no end line) and dirty ones belong to recovery.
		if (rec.complete && !rec.dirty)
			QFile::remove(rec.path);
	}
}

// MARK: - Locations

QString OpJournal::standardJournalDir()
{
	// Escape hatch for tests, and for moving the journal off a full
	// system disk. Points straight at the journal dir, no /journal suffix.
	const QString override = qEnvironmentVariable("MEDIAMUSTER_JOURNAL_DIR");
	if (!override.isEmpty())
		return override;

	QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	// AppDataLocation can come back empty on a misconfigured box; fall
	// back to the dotfolder so we never silently lose the journal.
	if (base.isEmpty())
		base = QDir::homePath() + QStringLiteral("/.mediamuster");
	return base + QStringLiteral("/journal");
}

bool OpJournal::standardDirWritable()
{
	const QString dir = standardJournalDir();
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

QString OpJournal::openFailedText(OpKind k)
{
	return QStringLiteral("Couldn't open the operations journal — this %1 runs "
						  "without crash recovery.")
		.arg(opKindName(k));
}

QString OpJournal::degradedText()
{
	return QStringLiteral("The operations journal stopped accepting writes (disk "
						  "full?). The run continues, but crash recovery can't "
						  "protect files from here on.");
}

// MARK: - Line writer

void OpJournal::writeLine(const QJsonObject &obj)
{
	if (!m_file || !m_file->isOpen())
		return;
	// A WAL line that never reached the disk protects nothing. Track the
	// first failure (full disk, dying drive) as permanent degradation:
	// the callers warn the user once, and finish() disposes of the file
	// rather than leaving a half-told story for recovery to misread.
	const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
	bool ok = m_file->write(line) == line.size();
	ok = m_file->write("\n", 1) == 1 && ok;
	ok = syncLine(*m_file) && ok;
	if (!ok)
		m_degraded = true;
}

// MARK: - Read side

int OpJournal::Record::doneCount() const
{
	int n = 0;
	for (const Entry &op : ops)
		if (op.completed)
			++n;
	return n;
}

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
		const QString r = o.value(QStringLiteral("record")).toString();

		if (r == QStringLiteral("begin"))
		{
			rec.schema = o.value(QStringLiteral("schema")).toInt(0);
			const auto kind = opKindFromName(o.value(QStringLiteral("kind")).toString());
			rec.kindKnown = kind.has_value();
			rec.kind = kind.value_or(OpKind::Copy);
			rec.meta = o.value(QStringLiteral("metadata")).toObject();
			rec.started = o.value(QStringLiteral("started")).toString();
			rec.pid = o.value(QStringLiteral("processId")).toInteger(0);
			rec.host = o.value(QStringLiteral("host")).toString();
			rec.undoes = rec.meta.value(QStringLiteral("undoes")).toString();
			rec.originalKind =
				opKindFromName(rec.meta.value(QStringLiteral("originalKind")).toString());
		}
		else if (r == QStringLiteral("op"))
		{
			Entry e;
			e.id = o.value(QStringLiteral("id")).toInt(-1);
			e.src = o.value(QStringLiteral("source")).toString();
			e.dst = o.value(QStringLiteral("destination")).toString();
			e.bytes = o.value(QStringLiteral("bytes")).toInteger(0);
			e.parked = o.value(QStringLiteral("parked")).toString();
			e.srcId = FileIdentity::fromJson(o.value(QStringLiteral("sourceId")).toObject());
			e.parkedOriginalId =
				FileIdentity::fromJson(o.value(QStringLiteral("destinationId")).toObject());
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
				rec.ops[idx].hash = o.value(QStringLiteral("hash")).toString();
				rec.ops[idx].landedId =
					FileIdentity::fromJson(o.value(QStringLiteral("destinationId")).toObject());
				rec.ops[idx].parkedFinal = o.value(QStringLiteral("parkedFinal")).toString();
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
		else if (r == QStringLiteral("undone"))
		{
			rec.undone = true;
		}
		else if (r == QStringLiteral("note"))
		{
			const QString text = o.value(QStringLiteral("text")).toString();
			if (!text.isEmpty())
				rec.notes << text;
		}
		else if (r == QStringLiteral("plan"))
		{
			rec.hasPlan = true;
			rec.planDest = o.value(QStringLiteral("destination")).toString();
			rec.planPreserve = o.value(QStringLiteral("preserve")).toBool(false);
			rec.plan.clear();
			const QJsonArray files = o.value(QStringLiteral("files")).toArray();
			rec.plan.reserve(files.size());
			for (const QJsonValue &v : files)
			{
				const QJsonObject fo = v.toObject();
				OpItem it;
				it.src = fo.value(QStringLiteral("source")).toString();
				it.name = fo.value(QStringLiteral("name")).toString();
				it.folder = fo.value(QStringLiteral("folder")).toString();
				it.bytes = fo.value(QStringLiteral("bytes")).toInteger(0);
				it.policy = fo.value(QStringLiteral("policy")).toString();
				it.mobId = fo.value(QStringLiteral("mobId")).toString();
				it.masterMobId = fo.value(QStringLiteral("masterMobId")).toString();
				it.clipName = fo.value(QStringLiteral("clipName")).toString();
				it.renameDst = fo.value(QStringLiteral("destination")).toString();
				it.groupKey = fo.value(QStringLiteral("group")).toString();
				if (!it.src.isEmpty())
					rec.plan.append(it);
			}
			rec.volumes.clear();
			const QJsonArray vols = o.value(QStringLiteral("volumes")).toArray();
			rec.volumes.reserve(vols.size());
			for (const QJsonValue &v : vols)
				rec.volumes.append(VolumeIdentity::fromJson(v.toObject()));
		}
	}

	// The no-backwards-compatibility line: anything that isn't OUR schema
	// — an old beta's journal, a future build's, or a file so torn its
	// begin line never parsed — is not ours to interpret. Invisible to
	// every caller, left untouched on disk.
	if (rec.schema != kSchema)
		return std::nullopt;

	return rec;
}

QVector<OpJournal::Record> OpJournal::scan(const QString &dir)
{
	const QString base = dir.isEmpty() ? standardJournalDir() : dir;
	QVector<Record> out;

	QDir d(base);
	if (!d.exists())
		return out;

	// Name sort == chronological, since the filename leads with a
	// zero-padded UTC timestamp.
	const QStringList files =
		d.entryList({QStringLiteral("journal-*.jsonl")}, QDir::Files, QDir::Name);

	for (const QString &name : files)
	{
		// A file we can't open, that vanished mid-sweep, or that isn't
		// schema 2 simply isn't part of the set; skip it rather than
		// fail the whole scan.
		if (const auto rec = readOne(d.filePath(name)))
			out.append(*rec);
	}

	return out;
}

// MARK: - Undo bookkeeping

std::optional<OpJournal::Record> OpJournal::latestUndoable(const QString &dir)
{
	const QVector<Record> records = scan(dir);
	// Newest first: scan is oldest-first by name.
	for (int i = records.size() - 1; i >= 0; --i)
	{
		const Record &rec = records[i];
		// The full qualification, spelled out:
		//   complete    — the run concluded on the user's watch. Cancelled
		//                 counts: stop-and-keep means landed work is real.
		//   doneCount   — something actually landed; a run of pure
		//                 skips/failures has nothing to reverse.
		//   !dirty      — a stranded park makes the record recovery's
		//                 business, not undo's.
		//   !undone     — single-level undo, spent is spent.
		//   !recovered  — the sweep already acted on it.
		//   kind!=Undo  — no undo-of-undo (no redo), by design.
		if (rec.complete && !rec.dirty && !rec.undone && !rec.recovered && rec.kindKnown &&
			rec.kind != OpKind::Undo && rec.doneCount() > 0)
			return rec;
	}
	return std::nullopt;
}

bool OpJournal::markRecovered(const QString &journalPath, int reversed, int failed)
{
	QFile f(journalPath);
	if (!f.open(QIODevice::Append))
		return false;

	const QJsonObject o{{QStringLiteral("record"), QStringLiteral("recovered")},
						{QStringLiteral("reversed"), reversed},
						{QStringLiteral("failed"), failed},
						{QStringLiteral("timestamp"), nowIso()}};
	f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
	f.write("\n", 1);
	syncLine(f);
	f.close();
	return true;
}

bool OpJournal::markUndone(const QString &journalPath, const QString &by)
{
	QFile f(journalPath);
	if (!f.open(QIODevice::Append))
		return false;

	const QJsonObject o{{QStringLiteral("record"), QStringLiteral("undone")},
						{QStringLiteral("byJournal"), by},
						{QStringLiteral("timestamp"), nowIso()}};
	f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
	f.write("\n", 1);
	syncLine(f);
	f.close();
	return true;
}
