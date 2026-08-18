#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <memory>
#include <optional>

class QFile;

// MARK: - OpJournal
//
// Write-ahead journal for the destructive jobs (Move, Delete, Copy). Log
// what we're about to do before doing it, then mark it done
// after. If the app dies mid-run, the next launch reads the half-finished
// journal and walks everything back.
//
// Copy earns a journal despite never removing its source: replacing a live
// destination parks the original aside first, so a crash mid-copy can leave
// the user with neither file. `parked` is what recovery puts back.
//
// On disk it's JSON-lines (one object per line) under
//   <app-data>/oplog/oplog-<timestamp>-<uuid>.jsonl
// Append-only, each line fsync'd to the drive before the step it records, so an
// app crash or a drive-acknowledged power loss can't lose it (see syncFile for
// the deliberate fsync-over-F_FULLFSYNC tradeoff). A 'begin'
// line opens the run and stamps the owning pid/host; a 'plan' line (once,
// right after 'begin') lists every file the run intends to touch, so a
// later launch can offer to finish an interrupted run; an 'end' line closes
// a run that finished or was cancelled; a 'recovered' line marks one a
// previous launch already rolled back. A journal with no 'end' whose pid is
// no longer alive on this host is an interrupted run, which is what
// recovery looks for.
//
// Old journals must stay readable by future builds, so every line carries
// enough to reverse its own op and the 'begin' line stamps a schema
// version for readers to branch on.
class OpJournal
{
public:
	enum class Kind
	{
		Move,
		Delete,
		Copy
	};

	/// Opens a fresh journal and writes the begin line. `meta` goes in the
	/// header for forensics (dest root, preserve flag…). `dir` defaults to
	/// the standard oplog location; tests pass a temp dir. Always check
	/// isOpen() before logging.
	OpJournal(Kind kind, const QJsonObject &meta, const QString &dir = QString());

	/// Leaves the file as-is when finish() was never called; that half-open
	/// state is what recovery looks for.
	~OpJournal();

	OpJournal(const OpJournal &) = delete;
	OpJournal &operator=(const OpJournal &) = delete;

	bool isOpen() const;
	QString path() const { return m_path; }

	// MARK: - The plan (whole to-do list, written once)

	/// One intended file, as the engine will see it. Only the fields the
	/// engine actually reads (see MediaManager::planItems), so an old
	/// journal can be replayed by a future build without a rescan.
	/// `policy` is the conflict policy by NAME ("keepboth"/"skip"/
	/// "replace", empty = none was chosen for this file) — a string, not
	/// the enum value, so the on-disk meaning can't shift if the enum does.
	struct PlanItem
	{
		QString src;
		QString name;	///< The filename the engine would place.
		QString folder; ///< The Avid MXF subfolder ("1", "hostname.3"...).
		qint64 bytes = 0;
		QString policy;
	};

	/// Write the whole to-do list, right after the begin line, BEFORE the
	/// first op. This is what lets a later launch offer to finish the job:
	/// the per-op lines say what was started and what finished; the plan
	/// says what was meant to happen at all. Copy/Move pass the
	/// destination and preserve flag; Delete passes an empty dest.
	void writePlan(const QString &dest, bool preserve, const QVector<PlanItem> &items);

	/// True once any journal line failed to reach disk (write, flush, or
	/// fsync). From that moment the on-disk journal is untrustworthy: ops
	/// may be missing, so it can neither protect the rest of the run nor be
	/// safely left for recovery to interpret. Callers surface this to the
	/// user once; prune() deletes a degraded journal even when dirty,
	/// because a journal with missing lines misleads recovery — next launch
	/// could read a finished run as interrupted and "roll back" moves that
	/// actually completed.
	bool degraded() const { return m_degraded; }

	/// Test seam: pretend a line write just failed. The real failure needs
	/// a full disk, which a unit test can't arrange; this lets tests pin
	/// the degraded-journal behaviour (prune-even-when-dirty above).
	void debugForceWriteFailure() { m_degraded = true; }

	// MARK: - Per-op records

	/// Write-ahead: log the intent before touching disk. Returns the op id
	/// to pass back to the mark* calls. `parked` is the temp path an
	/// existing destination was moved aside to (Move-Replace and Copy-Replace).
	int planOp(const QString &src, const QString &dst, qint64 bytes = 0,
			   const QString &parked = QString());

	/// `finalPath` is where the file landed; only Delete needs it (the
	/// trash path), so rollback knows what to put back.
	void markDone(int id, const QString &finalPath = QString());

	/// A plain fail means the forward op never stuck: disk state is as if
	/// the op hadn't run, and recovery ignores it. `rollbackIncomplete=true`
	/// is the opposite claim — the op mutated disk AND the rollback failed
	/// (a parked original is still sitting at its park path), so recovery
	/// must retry it even though the run finished. A journal holding one of
	/// these refuses prune() and survives for the next launch.
	void markFailed(int id, const QString &error, bool rollbackIncomplete = false);
	void markSkipped(int id);

	/// Writes the end line. Call it whether the run finished or the user
	/// cancelled (pass cancelled=true). Either way the journal counts as
	/// clean and recovery skips it; the flag is just forensics. A cancelled
	/// run keeps the work it already did; cancel doesn't undo completed ops
	/// (see scan() for the contract).
	void finish(int succeeded, int failed, int skipped, bool cancelled = false);

	/// Best-effort delete of the on-disk journal. Call right after a clean
	/// finish() so finished runs don't pile up over a long session. finish()
	/// has already written the end line, so if we crash just before the
	/// unlink the next launch sees a 'complete' journal and deletes it, not
	/// a phantom 'interrupted' one. No-op until finish() has run — and a
	/// deliberate no-op when any op was marked rollback-incomplete: that
	/// journal is the only record of where a stranded original is parked,
	/// and next-launch recovery needs it.
	void prune();

	// MARK: - Locations

	/// <app-data>/oplog. Created on demand by the constructor.
	static QString standardOplogDir();

	/// Can a journal actually be written right now? Probes the standard
	/// oplog dir with a scratch file. UI calls this BEFORE dispatching a
	/// destructive run so "no crash protection" becomes a question the user
	/// answers, not a console line nobody reads. Best-effort: a true here
	/// can still degrade mid-run (see degraded()), which the run loops
	/// surface separately.
	static bool standardDirWritable();

	// MARK: - Recovery (read side)

	/// One journalled op, as read back during recovery.
	struct Entry
	{
		int id = -1;
		QString src;
		QString dst;
		QString finalPath; ///< Delete only: where it ended up.
		QString parked;	   ///< Move/Copy-Replace: the parked-aside path.
		qint64 bytes = 0;
		bool completed = false; ///< Saw a matching 'done' line.
		bool failed = false;
		bool skipped = false;
		/// Saw a 'fail' line carrying dirty:true — the rollback failed and
		/// the parked original is still stranded. Recovery retries these
		/// even inside a journal that carries an end line.
		bool rollbackIncomplete = false;
	};

	/// A parsed journal file.
	struct Record
	{
		QString path;
		Kind kind = Kind::Move;
		bool kindKnown = false; ///< The begin line's kind was one this build knows.
		int schema = 0;			///< The begin line's schema stamp (kSchema when written by this build).
		QJsonObject meta;
		QString started;
		qint64 pid = 0;			///< Owning process from the begin line...
		QString host;			///< ...and the machine it ran on.
		bool complete = false;	///< Saw an 'end' line (finished or cancelled).
		bool cancelled = false; ///< That end line carried cancelled:true.
		bool recovered = false; ///< Saw a 'recovered' line; already rolled back.
		bool dirty = false;		///< Any op has rollbackIncomplete set.
		QVector<Entry> ops;

		/// The to-do list, when the writer recorded one (see writePlan).
		/// Journals from builds before the plan line have hasPlan=false and
		/// are rolled back exactly as before; only planned runs can be
		/// offered for resume.
		bool hasPlan = false;
		QString planDest;
		bool planPreserve = false;
		QVector<PlanItem> plan;
	};

	/// Appended by recovery once it has rolled an interrupted journal back.
	/// Makes rollback idempotent: a crash mid-rollback leaves the journal
	/// without this line, so the next launch retries.
	static bool markRecovered(const QString &journalPath, int reversed, int failed);

	/// Parse a single journal file into a Record. nullopt if it can't be
	/// opened. Shared by scan() (the bulk crash-recovery sweep) and the
	/// user-initiated undo path, which reverses one specific journal.
	static std::optional<Record> readOne(const QString &journalPath);

	/// Parse every oplog-*.jsonl in `dir` (standard dir when empty), oldest
	/// first. Malformed lines are skipped, not fatal: a torn final line from
	/// a crash mid-write mustn't poison the read.
	///
	/// A Record needs recovery when it's not recovered, its pid is no longer
	/// alive on this host, and it's either not complete (an interrupted run,
	/// reversed wholesale) or complete-but-dirty (a finished run with a
	/// failed rollback, where ONLY the rollback-incomplete ops are retried —
	/// a finished run's completed ops are never rolled back).
	///
	/// The invariant rollback leans on: a finished forward op leaves src
	/// GONE. So if src still exists the op did not finish, and reversing
	/// would clobber a live file. Reversal only ever runs when src is
	/// absent. The per-op decision table lives in oprecovery.cpp.
	static QVector<Record> scan(const QString &dir = QString());

	static QString kindName(Kind k);
	static Kind kindFromName(const QString &s, bool *ok = nullptr);

	/// The two journal-unavailable warnings, worded once for every flow.
	/// Every destructive flow surfaces these; one home so the
	/// app's key safety message can't drift between them.
	static QString openFailedText(Kind k);
	static QString degradedText();

private:
	void writeLine(const QJsonObject &obj);

	QString m_path;
	std::unique_ptr<QFile> m_file;
	int m_nextId = 0;
	bool m_finished = false;
	bool m_hasDirty = false; ///< An op was marked rollback-incomplete; blocks prune().
	bool m_degraded = false; ///< A line write failed; see degraded().
};

// MARK: - JournalOp
//
// RAII handle for one journalled op. Build it right before you touch disk
// (it writes the 'op' line), then call done()/failed()/skipped() once. If
// it dies un-settled it marks itself failed, so a stray early return can't
// leave a phantom op in a journal we close clean. A real crash skips the
// destructor on purpose: the op stays unmarked, which is what recovery
// probes for.
//
// A null journal (open failed) makes every call a no-op, so callers don't
// have to branch.
class JournalOp
{
public:
	JournalOp(OpJournal *journal, const QString &src, const QString &dst, qint64 bytes = 0,
			  const QString &parked = QString())
		: m_journal(journal)
	{
		if (m_journal)
			m_id = m_journal->planOp(src, dst, bytes, parked);
	}

	~JournalOp()
	{
		if (m_journal && !m_settled)
			m_journal->markFailed(m_id, QStringLiteral("interrupted"));
	}

	JournalOp(const JournalOp &) = delete;
	JournalOp &operator=(const JournalOp &) = delete;

	/// `finalPath` is where it landed: Delete passes the trash path so
	/// rollback knows what to put back; Move leaves it empty.
	void done(const QString &finalPath = QString())
	{
		if (m_journal && !m_settled)
			m_journal->markDone(m_id, finalPath);
		m_settled = true;
	}

	void failed(const QString &error = QString())
	{
		if (m_journal && !m_settled)
			m_journal->markFailed(m_id, error);
		m_settled = true;
	}

	/// The op failed AND its rollback failed: the replaced original is still
	/// sitting at the parked path this op's record carries. Keeps the
	/// journal alive past finish()/prune() so next-launch recovery can
	/// finish the rollback. Call when ParkedFile::isStranded() reports true.
	void failedDirty(const QString &error = QString())
	{
		if (m_journal && !m_settled)
			m_journal->markFailed(m_id, error, /*rollbackIncomplete=*/true);
		m_settled = true;
	}

	/// The op concluded with disk unchanged; recovery ignores it. Used for
	/// a file the conflict policy skipped: the plan still lists it, so the
	/// journal has to say it was dealt with or a resume would offer it
	/// again.
	void skipped()
	{
		if (m_journal && !m_settled)
			m_journal->markSkipped(m_id);
		m_settled = true;
	}

private:
	OpJournal *m_journal = nullptr;
	int m_id = -1;
	bool m_settled = false;
};