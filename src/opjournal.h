#pragma once

#include "fileidentity.h"
#include "oprequest.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <memory>
#include <optional>

class QFile;

// MARK: - OpJournal
//
// The write-ahead journal for every operation the engine runs (Copy,
// Move, Delete, Rename, Undo). The rule it exists to enforce: WRITE THE
// INTENT DOWN, DURABLY, BEFORE TOUCHING THE DISK — then write the
// outcome after. If the app dies at any instant, the next launch reads
// the journal and knows exactly what was happening, what finished, and
// what was left mid-flight; and after a clean finish the journal IS the
// record that makes Edit ▸ Undo possible.
//
// On disk it's JSON-lines (one object per line) under
//   <app-data>/journal/journal-<timestamp>-<uuid>.jsonl
// Append-only; every line is fsync'd before the disk step it describes,
// and the directory entry is synced on create (both platforms — the
// Windows directory sync is NativeFile::syncDirectory, the piece the v1
// journal knowingly lacked). Records, in the order a run writes them:
//
//   begin     schema:2, kind, started, app, pid, host, meta.
//             An Undo run's meta names the journal it reverses
//             (`undoes`) and the original's kind (`originalKind`), so a
//             CRASHED undo can itself be recovered.
//   plan      The whole to-do list (one OpItem each, with the scan's
//             mob-id/clip-name claims) plus the identity of every
//             VOLUME the run touches. What lets a later launch offer to
//             finish an interrupted run — and refuse an impostor drive.
//   op        Write-ahead for one file: src, dst, bytes, the park path
//             an existing destination is about to be renamed to, the
//             SOURCE's captured identity, and (Replace only) the
//             identity of the destination file being parked.
//   done      The op concluded: where the file landed (`final`, Delete's
//             trash path), the copy's checksum (`hash`), the LANDED
//             file's identity (`dstId`), and where a replaced original
//             was trashed to (`parkedFinal`). Undo reads all of these.
//   fail/skip As v1: a plain fail means disk is as if the op never ran;
//             `dirty:true` means a rollback stalled with an original
//             still parked — recovery must retry it, and the journal
//             refuses to die while one exists.
//   note      A plain sentence worth keeping with the run (e.g. "the
//             destination volume couldn't give the full durability
//             barrier"). Forensics; recovery re-surfaces them.
//   end       The run finished or was cancelled. Cancel is stop-and-
//             keep: landed work is real work, so a cancelled journal is
//             both clean for recovery AND a valid undo candidate.
//   recovered Appended by the launch sweep once it has tidied an
//             interrupted journal (idempotency marker).
//   undone    Appended to the ORIGINAL journal when an undo of it
//             finishes clean, so it can never be undone twice.
//
// Retention is the v2 change of heart: a cleanly finished journal is NOT
// deleted at finish — it stays on disk as the undo candidate. The
// moment a NEW operation begins, its constructor prunes every finished,
// non-dirty journal (pruneSuperseded), which is simultaneously the
// retention policy ("keep the last completed run until the next one
// starts") and the undo-invalidation rule ("a newer operation
// invalidates undo") enforced at one choke point.
//
// Backwards compatibility: none, on purpose (beta decision). The reader
// treats any file whose begin line isn't schema:2 as not ours — skipped
// wholesale, left untouched on disk, never interpreted. A wrong
// rollback is worse than doing nothing, and a stranded park from an old
// beta stays visible in the media table via the Conventions temp-suffix
// rule either way.
class OpJournal
{
public:
	// MARK: - Writer

	/// Opens a fresh journal and writes the begin line. FIRST prunes every
	/// finished, non-dirty journal in the directory — see the retention
	/// note above; this is the one place undo is invalidated. `meta` goes
	/// in the begin line (dest root, preserve flag; undo: `undoes` +
	/// `originalKind`). `dir` defaults to the standard journal location; tests
	/// pass a temp dir. Always check isOpen() before trusting protection.
	///
	/// `sparePath`: a journal the prune must NOT touch. An Undo run passes
	/// the journal it is reversing — pruning it would destroy the very
	/// record the undo (and its own crash recovery) reads from; the
	/// original is retired via markUndone at clean finish instead, and
	/// the NEXT ordinary operation's prune takes it off disk.
	OpJournal(OpKind kind, const QJsonObject &meta, const QString &dir = QString(),
			 const QString &sparePath = QString());

	/// Leaves the file as-is when finish() was never called; that
	/// half-open state is exactly what recovery looks for.
	///
	/// This is HALF of why the per-op handle below is a separate class:
	/// the two destructors are deliberate opposites. An abandoned journal
	/// must stay open and unfinished (the signal a run was interrupted);
	/// an abandoned JournalOpGuard must close itself and stamp 'interrupted'
	/// (so a stray early return can't leave a phantom op in a journal that
	/// then closes clean). One type cannot do both. The other half is
	/// lifetime: one journal per run, one JournalOpGuard per file.
	~OpJournal();

	OpJournal(const OpJournal &) = delete;
	OpJournal &operator=(const OpJournal &) = delete;

	bool isOpen() const;
	QString path() const { return m_path; }

	/// The whole to-do list plus the fingerprint of every volume the run
	/// will touch. Written once, right after begin, BEFORE the first op.
	void writePlan(const QString &dest, bool preserve, const QVector<OpItem> &items,
				   const QVector<VolumeIdentity> &volumes);

	/// Write-ahead for one file: call BEFORE any disk mutation for it.
	/// `parked` is the park path an existing destination will be renamed
	/// to (empty when the slot is free); `srcId` is the source's captured
	/// identity; `parkedOriginalId` is the identity of the existing
	/// destination being parked (Replace only). Returns the op id.
	int planOp(const QString &src, const QString &dst, qint64 bytes, const QString &parked,
			   const FileIdentity &srcId, const FileIdentity &parkedOriginalId = {});

	/// Everything a 'done' line can carry. All optional; Delete fills
	/// finalPath, copies fill hash + landedId, Replace fills parkedFinal.
	struct DoneInfo
	{
		QString finalPath;	 ///< Delete: where the file landed (trash path).
		QString hash;		 ///< XXH3 hex when a hashing path ran; empty for clone/rename.
		FileIdentity landedId; ///< Identity of the landed file, captured after completion.
		QString parkedFinal; ///< Replace: the trash path the parked original went to.
	};

	void markDone(int id, const DoneInfo &info = {});
	void markFailed(int id, const QString &error, bool rollbackIncomplete = false);
	void markSkipped(int id);

	/// A plain sentence worth keeping with the run (durability degrades,
	/// reroutes). Forensics: recovery re-surfaces notes to the user.
	void writeNote(const QString &text);

	/// Close the run. Cancel passes cancelled=true; either way the journal
	/// is clean for recovery AND (with ≥1 done op) an undo candidate. The
	/// ONE case that still self-destructs here: a degraded journal (a line
	/// write failed mid-run). With lines missing the file no longer tells
	/// the truth — recovery reading it could "roll back" work that
	/// finished — so a degraded journal is deleted even when dirty; the
	/// caller's critical log line is the surviving record.
	void finish(int succeeded, int failed, int skipped, bool cancelled = false);

	/// True once any journal line failed to reach disk (write, flush, or
	/// fsync — full disk, dying drive). From that moment the run keeps
	/// going but is honestly unprotected; callers surface it once.
	bool degraded() const { return m_degraded; }

	/// Test seam: pretend a line write just failed, so tests can pin the
	/// degraded behaviour without arranging a full disk.
	void debugForceWriteFailure() { m_degraded = true; }

	// MARK: - Retention

	/// Delete every schema-2 journal in `dir` that is finished and not
	/// dirty — superseded undo candidates. Interrupted journals (no end
	/// line) and dirty ones are recovery's business and stay; legacy
	/// files are not ours and stay untouched. `sparePath` survives (see
	/// the constructor). Called by the constructor; public so the sweep
	/// and tests can drive it directly.
	static void pruneSuperseded(const QString &dir = QString(),
								const QString &sparePath = QString());

	// MARK: - Locations

	/// <app-data>/journal, `MEDIAMUSTER_JOURNAL_DIR` override first. Created
	/// on demand by the constructor.
	static QString standardJournalDir();

	/// Can a journal actually be written right now? Probes with a scratch
	/// file. UI calls this BEFORE dispatching a destructive run so "no
	/// crash protection" becomes a question the user answers, not a
	/// console line nobody reads.
	static bool standardDirWritable();

	/// The two journal-unavailable warnings, worded once for every flow.
	static QString openFailedText(OpKind k);
	static QString degradedText();

	// MARK: - Read side

	/// One journaled op, as read back for recovery or undo.
	struct Entry
	{
		int id = -1;
		QString src;
		QString dst;
		QString finalPath; ///< Delete: where it landed (trash path).
		QString parked;	   ///< Replace: the parked-aside path.
		qint64 bytes = 0;
		FileIdentity srcId;			  ///< Source identity at op time.
		FileIdentity parkedOriginalId; ///< Replace: the parked file's identity.
		FileIdentity landedId;		  ///< From 'done': the landed file's identity.
		QString hash;				  ///< From 'done': XXH3 hex, when a hashing path ran.
		QString parkedFinal;		  ///< From 'done': trash path of the committed park.
		bool completed = false;
		bool failed = false;
		bool skipped = false;
		/// A 'fail' line carried dirty:true — the rollback stalled and the
		/// parked original is still stranded; recovery retries these even
		/// inside a journal that carries an end line.
		bool rollbackIncomplete = false;
	};

	/// A parsed journal file.
	struct Record
	{
		QString path;
		OpKind kind = OpKind::Copy;
		bool kindKnown = false;
		int schema = 0;
		QJsonObject meta;
		QString started;
		qint64 pid = 0;
		QString host;
		bool complete = false;	///< Saw an 'end' line (finished or cancelled).
		bool cancelled = false;
		bool recovered = false; ///< The launch sweep already tidied it.
		bool undone = false;	///< An undo of this run finished clean.
		bool dirty = false;		///< Any op has rollbackIncomplete set.
		QVector<Entry> ops;
		QStringList notes;

		bool hasPlan = false;
		QString planDest;
		bool planPreserve = false;
		QVector<OpItem> plan;
		QVector<VolumeIdentity> volumes;

		/// Undo runs only: what they reverse. The pair answers "which run"
		/// and "what kind of run" — and `originalKind` is deliberately NOT
		/// this record's own `kind` (always Undo here), nor the machinery
		/// recovery ends up using (undoing a Copy uses delete machinery;
		/// see OpRescue's reverserKindFor).
		QString undoes;						///< The original journal's file name.
		std::optional<OpKind> originalKind; ///< The kind of the run being reversed.

		/// Count of ops that concluded with a 'done' line.
		int doneCount() const;
	};

	/// Parse one journal file. nullopt if it can't be opened, or if its
	/// begin line isn't schema 2 (legacy: not ours to interpret). Torn
	/// lines (crash mid-write) are skipped, never fatal.
	static std::optional<Record> readOne(const QString &journalPath);

	/// Parse every schema-2 journal-*.jsonl in `dir` (standard dir when
	/// empty), oldest first (name sort == chronological). Legacy files
	/// are invisible here — skipped and left on disk untouched.
	static QVector<Record> scan(const QString &dir = QString());

	// MARK: - Undo bookkeeping

	/// The one run Edit ▸ Undo may reverse right now: the NEWEST journal
	/// that finished (end line — cancelled counts: landed work is real),
	/// actually landed something (≥1 done), is not dirty, was not already
	/// undone or swept, and is not itself an undo. nullopt when there is
	/// nothing to offer.
	static std::optional<Record> latestUndoable(const QString &dir = QString());

	/// Appended by the launch sweep once it has tidied an interrupted
	/// journal; makes the rollback idempotent (crash mid-rollback leaves
	/// it unstamped, so the next launch retries).
	static bool markRecovered(const QString &journalPath, int reversed, int failed);

	/// Appended to the ORIGINAL journal when an undo of it finishes clean.
	/// `by` is the undo run's journal file name, for forensics.
	static bool markUndone(const QString &journalPath, const QString &by);

private:
	void writeLine(const QJsonObject &obj);

	QString m_path;
	std::unique_ptr<QFile> m_file;
	int m_nextId = 0;
	bool m_finished = false;
	bool m_hasDirty = false;
	bool m_degraded = false;
};

// MARK: - JournalOpGuard
//
// RAII handle for one journaled op. Build it right before touching disk
// (it writes the 'op' line), then settle it exactly once with done() /
// failed() / failedDirty() / skipped(). If it dies un-settled it marks
// itself failed, so a stray early return can't leave a phantom op in a
// journal that closes clean. A real crash skips the destructor on
// purpose: the op stays unmarked, which is what recovery probes for.
//
// A null journal (open failed) makes every call a no-op, so callers
// don't have to branch.
class JournalOpGuard
{
public:
	JournalOpGuard(OpJournal *journal, const QString &src, const QString &dst, qint64 bytes,
			 const QString &parked, const FileIdentity &srcId,
			 const FileIdentity &parkedOriginalId = {})
		: m_journal(journal)
	{
		if (m_journal)
			m_id = m_journal->planOp(src, dst, bytes, parked, srcId, parkedOriginalId);
	}

	~JournalOpGuard()
	{
		if (m_journal && !m_settled)
			m_journal->markFailed(m_id, QStringLiteral("interrupted"));
	}

	JournalOpGuard(const JournalOpGuard &) = delete;
	JournalOpGuard &operator=(const JournalOpGuard &) = delete;

	void done(const OpJournal::DoneInfo &info = {})
	{
		if (m_journal && !m_settled)
			m_journal->markDone(m_id, info);
		m_settled = true;
	}

	void failed(const QString &error = QString())
	{
		if (m_journal && !m_settled)
			m_journal->markFailed(m_id, error);
		m_settled = true;
	}

	/// The op failed AND its rollback failed: the replaced original is
	/// still sitting at the parked path this op's record carries. Keeps
	/// the journal alive past finish() so next-launch recovery can finish
	/// the job. Call when ParkedFile::isStranded() reports true.
	void failedDirty(const QString &error = QString())
	{
		if (m_journal && !m_settled)
			m_journal->markFailed(m_id, error, /*rollbackIncomplete=*/true);
		m_settled = true;
	}

	/// The op concluded with disk unchanged (conflict policy said Skip).
	/// The plan still lists the file, so the journal has to say it was
	/// dealt with or a resume would offer it again.
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
