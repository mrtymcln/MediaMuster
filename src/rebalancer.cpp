#include "rebalancer.h"
#include "avidlayout.h"
#include "formatutil.h"
#include "opjournal.h"
#include "probesweep.h"
#include "progressthrottle.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QThread>

#include <algorithm>
#include <limits>

// MARK: - Construction

Rebalancer::Rebalancer(QObject *parent)
	: QObject(parent)
{
}

// MARK: - Folder name parsing

std::optional<FolderId> Rebalancer::parseFolderName(const QString &name)
{
	const int lastDot = name.lastIndexOf('.');
	const QString prefix = lastDot < 0 ? QString() : name.left(lastDot);
	const QString tail = lastDot < 0 ? name : name.mid(lastDot + 1);

	bool ok = false;
	const int n = tail.toInt(&ok);
	if (!ok || n <= 0)
		return std::nullopt;

	FolderId fid{prefix, n};

	// Round-trip guard. `.5`, `01`, `MartysiMac.005` all parse as
	// `n=5` but don't survive a canonical re-render; rejecting
	// them keeps us from 'rebalancing' a folder into a name that
	// doesn't match what was on disk.
	if (fid.display() != name)
		return std::nullopt;
	return fid;
}

std::optional<FolderId> Rebalancer::srcFolderOf(const QString &srcPath)
{
	// dir().dirName() pulls the parent folder name straight off, no second
	// QFileInfo allocation.
	return parseFolderName(QFileInfo(srcPath).dir().dirName());
}

// MARK: - Planning helpers

namespace
{
	/// True when a directory entry occupies Avid's per-folder file budget.
	/// In an `Avid MediaFiles/MXF/<n>` folder only MXF essence counts:
	/// Avid's own databases, dot-hidden files, shell junk, and stray
	/// non-MXF files are not media. Counting them inflated the preview's
	/// per-folder count and stole slots from the kFolderCap packing.
	/// The name rule itself lives in AvidLayout. It is the BUDGET rule,
	/// deliberately narrower than the table's (see isAvidMediaName): the
	/// preview counts what Avid counts, not what the table shows.
	bool countsTowardFolderBudget(const QString &fileName)
	{
		return AvidLayout::countsAsEssenceName(fileName);
	}

	// Prefix for synthetic relatives keys assigned to loose files
	// (no masterMobId). Lets us detect 'this was a loose file'
	// later via a cheap startsWith check.
	inline constexpr QLatin1String kLoneKeyPrefix("__lone__:");

	// Loose files get a unique synthetic key so each becomes a
	// one-member group; files with a master MOB share their master
	// clip's ID. Single implementation; planner-side and
	// executor-side overloads delegate here so the format can't drift.
	QString relativesKey(const QString &masterMobId, const QString &fallbackPath)
	{
		return masterMobId.isEmpty() ? kLoneKeyPrefix + fallbackPath : masterMobId;
	}

	QString relativesKey(const MediaFile &mf)
	{
		return relativesKey(mf.masterMobId, mf.filePath);
	}

	QString relativesKey(const MoveOp &op)
	{
		return relativesKey(op.masterMobId, op.srcPath);
	}

} // namespace

// MARK: - Plan computation

RebalancePlan Rebalancer::computePlan(const QString &mxfRoot, const QString &volumeLabel,
									  const QVector<MediaFile> &files)
{
	RebalancePlan plan;
	plan.mxfRoot = mxfRoot;
	plan.volumeLabel = volumeLabel;

	QDir mxfDir(mxfRoot);
	if (!mxfDir.exists())
		return plan;

	// MARK: Snapshot current folder state on disk

	QHash<QString, QSet<int>> existingByPrefix; // prefix → {n}
	QHash<FolderId, int> realCount;

	const QStringList subdirs = mxfDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for (const QString &name : subdirs)
	{
		// Count lazily via QDirIterator instead of materialising the whole
		// filename list — Avid folders run to thousands of files, and this
		// throwaway list would be one QString allocation per file. Only
		// files that occupy Avid's budget count (see countsTowardFolderBudget).
		int onDiskCount = 0;
		QDirIterator folderFiles(mxfDir.filePath(name), QDir::Files | QDir::NoDotAndDotDot);
		while (folderFiles.hasNext())
		{
			folderFiles.next();
			if (countsTowardFolderBudget(folderFiles.fileName()))
				++onDiskCount;
		}

		const auto parsed = parseFolderName(name);
		FolderState fs;
		fs.name = name;
		fs.count = onDiskCount;
		fs.inScope = parsed.has_value();
		if (parsed)
		{
			fs.id = *parsed;
			existingByPrefix[fs.id.prefix].insert(fs.id.n);
			realCount[fs.id] = onDiskCount;
		}
		plan.folders.append(fs);
	}

	// MARK: Pre-parse each file's mxfFolder once

	// parseFolderName tokenises "MartysiMac.42" into prefix+n on every
	// call; doing it inline in each per-file loop below adds up to
	// ~250k allocations on a 50k-file project. Pair each file with
	// its parsed folder here so the inner loops can read directly.
	// Files in out-of-scope folders (Quarantined, malformed names)
	// are dropped; they're excluded from rebalancing anyway.
	struct IndexedMedia
	{
		const MediaFile *file;
		FolderId folder;
	};

	// MARK: Tally bytes + bucket files into relatives groups

	// One pass over `files` does both: tallies bytes per source folder,
	// and groups files by master MOB so relatives stay together.
	QHash<FolderId, qint64> realBytes;
	QHash<QString, QVector<IndexedMedia>> bucketed;
	for (const auto &mf : files)
	{
		const auto parsed = parseFolderName(mf.mxfFolder);
		if (!parsed)
			continue;
		realBytes[*parsed] += mf.sizeBytes;
		bucketed[relativesKey(mf)].append({&mf, *parsed});
	}
	for (auto &fs : plan.folders)
	{
		if (fs.inScope)
			fs.bytes = realBytes.value(fs.id, 0);
	}

	// MARK: Build group descriptors with chosen home folder

	// One relatives group (or one lone file) plus the `<prefix, n>` we
	// want to consolidate it into. Members carry their pre-parsed
	// FolderId so all downstream loops are re-parse-free.
	struct Group
	{
		QString homePrefix;
		int homeN = 1;
		QString masterMobId;
		QVector<IndexedMedia> members;
	};
	QVector<Group> groups;
	groups.reserve(bucketed.size());

	for (auto it = bucketed.constBegin(); it != bucketed.constEnd(); ++it)
	{
		Group g;
		g.members = it.value();
		g.masterMobId = it.key().startsWith(kLoneKeyPrefix) ? QString() : it.key();

		QHash<QString, int> prefixCount;
		for (const auto &m : g.members)
			prefixCount[m.folder.prefix] += 1;

		// Most-populous prefix wins; tie broken by sorted prefix, which
		// keeps the home choice deterministic so reruns on the same
		// project produce the same plan.
		QStringList prefixes = prefixCount.keys();
		std::sort(prefixes.begin(), prefixes.end());
		int best = -1;
		for (const QString &p : prefixes)
		{
			if (prefixCount[p] > best)
			{
				best = prefixCount[p];
				g.homePrefix = p;
			}
		}

		// Home N = smallest N within the home prefix that contains
		// a member. Prefer existing folders over new ones, and lower
		// numbers over higher.
		int home = std::numeric_limits<int>::max();
		for (const auto &m : g.members)
		{
			if (m.folder.prefix == g.homePrefix)
				home = qMin(home, m.folder.n);
		}
		g.homeN = (home == std::numeric_limits<int>::max() ? 1 : home);

		groups.append(g);
	}

	// MARK: Sort groups for stable, deterministic packing

	// Prefix asc > home N asc > larger groups first within a home.
	// Larger first so we don't waste capacity by scattering small
	// groups into a folder that a 4000-file group then can't fit.
	std::sort(groups.begin(), groups.end(),
			  [](const Group &a, const Group &b)
			  {
				  if (a.homePrefix != b.homePrefix)
					  return a.homePrefix < b.homePrefix;
				  if (a.homeN != b.homeN)
					  return a.homeN < b.homeN;
				  return a.members.size() > b.members.size();
			  });

	// MARK: Pack groups into folders

	// One host (one prefix) at a time. `projected` tracks the
	// running per-folder count as we plan moves into it, so we can
	// check kFolderCap against future state, not on-disk state.
	QHash<FolderId, int> projected = realCount;
	QHash<QString, QSet<int>> newByPrefix;

	auto allFoldersForPrefix = [&](const QString &prefix)
	{
		QSet<int> all = existingByPrefix.value(prefix);
		for (int n : newByPrefix.value(prefix))
			all.insert(n);
		return all;
	};

	// Pick the smallest free N and register it in all the per-pass
	// state so subsequent passes see it as if already on disk.
	auto allocateNewFolder = [&](const QString &prefix) -> FolderId
	{
		QSet<int> all = allFoldersForPrefix(prefix);
		int next = 1;
		for (int n : all)
			if (n >= next)
				next = n + 1;
		newByPrefix[prefix].insert(next);
		FolderId id{prefix, next};
		projected[id] = 0;
		plan.newFolders.append(id);

		FolderState fs;
		fs.id = id;
		fs.name = id.display();
		fs.isNew = true;
		fs.inScope = true;
		plan.folders.append(fs);
		return id;
	};

	// No-op when src == dest (already where we want it). Takes
	// IndexedMedia so the source folder is free; no re-parse.
	auto pushOp = [&](const IndexedMedia &m, FolderId dest)
	{
		if (m.folder == dest)
			return;
		plan.ops.append({m.file->filePath, dest, m.file->masterMobId, m.file->sizeBytes});
		projected[m.folder] -= 1;
		projected[dest] += 1;
	};

	for (const Group &g : groups)
	{
		const QString &prefix = g.homePrefix;
		const FolderId home{prefix, g.homeN};
		const int size = static_cast<int>(g.members.size());

		// Edge case: relatives group >= kFolderCap. Deterministic split
		// across new folders.
		if (size >= kFolderCap)
		{
			int idx = 0;
			while (idx < size)
			{
				FolderId target;
				const int slackHome = kFolderCap - projected.value(home, 0);
				if (idx == 0 && slackHome >= (size - idx))
					target = home;
				else
					target = allocateNewFolder(prefix);
				const int slack = kFolderCap - projected.value(target, 0);
				const int chunk = qMin(slack, size - idx);
				for (int i = 0; i < chunk; ++i, ++idx)
					pushOp(g.members[idx], target);
			}
			continue;
		}

		int membersAtHome = 0;
		for (const auto &m : g.members)
		{
			if (m.folder == home)
				++membersAtHome;
		}
		const int neededAtHome = size - membersAtHome;

		// Already entirely at home and home isn't over kFolderCap, so
		// leave it. Common case for a mildly fragmented project.
		if (membersAtHome == size && projected.value(home, 0) <= kFolderCap)
			continue;

		// Strays fit in home? Pull them in.
		if (projected.value(home, 0) + neededAtHome <= kFolderCap)
		{
			for (const auto &m : g.members)
			{
				if (m.folder != home)
					pushOp(m, home);
			}
			continue;
		}

		// Home is full. First-fit ascending within the prefix; if
		// nothing existing has room, allocate a new folder.
		// First-fit (not best-fit) keeps low Ns denser, matching
		// editor intuition that "1" is the busiest folder.
		FolderId dest;
		bool found = false;
		QSet<int> all = allFoldersForPrefix(prefix);
		QList<int> sortedNs(all.constBegin(), all.constEnd());
		std::sort(sortedNs.begin(), sortedNs.end());
		for (int n : sortedNs)
		{
			FolderId cand{prefix, n};
			if (projected.value(cand, 0) + size <= kFolderCap)
			{
				dest = cand;
				found = true;
				break;
			}
		}
		if (!found)
			dest = allocateNewFolder(prefix);

		for (const auto &m : g.members)
		{
			if (m.folder != dest)
				pushOp(m, dest);
		}
	}

	// MARK: Tally per-folder filesIn / filesOut / bytesIn / bytesOut

	// Indices, not pointers. A future post-tally append to
	// plan.folders (via allocateNewFolder or similar) would silently
	// invalidate every pointer in this hash; indices survive
	// QVector reallocations.
	QHash<FolderId, int> stateByFid;
	for (int i = 0; i < plan.folders.size(); ++i)
	{
		if (plan.folders[i].inScope)
			stateByFid[plan.folders[i].id] = i;
	}

	for (const auto &op : plan.ops)
	{
		if (auto srcFid = srcFolderOf(op.srcPath); srcFid && stateByFid.contains(*srcFid))
		{
			const int idx = stateByFid.value(*srcFid);
			plan.folders[idx].filesOut += 1;
			plan.folders[idx].bytesOut += op.sizeBytes;
		}
		if (stateByFid.contains(op.dest))
		{
			const int idx = stateByFid.value(op.dest);
			plan.folders[idx].filesIn += 1;
			plan.folders[idx].bytesIn += op.sizeBytes;
		}
	}

	return plan;
}

// MARK: - Execution

void Rebalancer::executeAsync(const RebalancePlan &plan)
{
	m_job.start([this, plan]
				{ doExecute(plan); });
}

void Rebalancer::doExecute(const RebalancePlan &plan)
{
	int succeeded = 0;
	int failed = 0;
	bool cancelled = false;
	QSet<FolderId> touched;
	const int total = plan.ops.size();

	// MARK: Open the journal before anything touches disk

	// The journal used to open after the probes, which left the probe
	// renames as the one disk mutation the WAL never saw — a crash between
	// a probe's two renames stranded a real clip with no record. Probes are
	// ops like any other now, so they open the journal first.
	auto journal = std::make_unique<OpJournal>(
		OpJournal::Kind::Rebalance, QJsonObject{{QStringLiteral("mxfRoot"), plan.mxfRoot}});
	if (!journal->isOpen())
		emit log(QtWarningMsg, OpJournal::openFailedText(OpJournal::Kind::Rebalance));

	// MARK: Recover stranded probe files from a previous force-quit

	// Journalled probes are restored by startup recovery; this sweep is the
	// belt to that brace, catching strandings from older builds and from
	// runs whose journal degraded. Shared with recovery via ProbeSweep.
	{
		const ProbeSweep::Result swept = ProbeSweep::recoverStranded(plan.mxfRoot);
		for (const QString &name : swept.restored)
			emit log(QtInfoMsg, QStringLiteral("Recovered stranded probe file: %1").arg(name));
		for (const QString &name : swept.stuck)
			emit log(QtCriticalMsg,
					 QStringLiteral("Stranded probe file '%1' couldn't be restored (its original "
									"name may be taken); left in place.")
						 .arg(name));
	}

	// MARK: Pre-flight — probe rename per donor folder

	// Rename one file from each donor folder out and immediately
	// back. If the folder is locked for any reason, the first
	// rename fails and we abort cleanly before doing real damage.
	// The retry on the rename-back handles the rare race where a
	// watcher picks the file up between the two attempts.
	//
	// Each probe is write-ahead journalled (src → probe path) so a crash
	// between the two renames is just an unfinished op: recovery's
	// move-reversal renames the probe home at next launch. A clean
	// out-and-back settles the op as skipped — disk unchanged.
	{
		QHash<FolderId, QString> probeFiles;
		for (const MoveOp &op : plan.ops)
		{
			auto srcFid = srcFolderOf(op.srcPath);
			if (srcFid && !probeFiles.contains(*srcFid))
				probeFiles.insert(*srcFid, op.srcPath);
		}

		const QString suffix = ProbeSweep::makeProbeSuffix();
		for (auto it = probeFiles.constBegin(); it != probeFiles.constEnd(); ++it)
		{
			const QString src = it.value();
			const QString probe = src + suffix;

			JournalOp jop(journal.get(), src, probe);
			if (!QFile::rename(src, probe))
			{
				// Nothing moved; settle plain-failed and close the journal
				// clean so recovery has nothing to chew on.
				jop.failed(QStringLiteral("probe rename-out refused"));
				journal->finish(0, 1, 0);
				journal->prune();
				emit aborted(tr("Cannot rename files in folder '%1'. "
								"Quit Avid Media Composer (or any other "
								"app that has these files open) and try "
								"again.")
								 .arg(it.key().display()));
				return;
			}

			// Short-backoff retries cover a watcher grabbing the file
			// between the two attempts.
			constexpr int kRestoreAttempts = 5;
			constexpr int kRestoreBackoffMs = 100;
			bool restored = false;
			for (int attempt = 0; attempt < kRestoreAttempts; ++attempt)
			{
				if (QFile::rename(probe, src))
				{
					restored = true;
					break;
				}
				QThread::msleep(kRestoreBackoffMs);
			}
			if (!restored)
			{
				// The clip is stranded under the probe name and we couldn't
				// put it back. Dirty fail: the journal survives finish() so
				// next launch's recovery keeps retrying the rename home.
				jop.failedDirty(QStringLiteral("probe restore failed"));
				journal->finish(0, 1, 0);
				journal->prune(); // refuses: dirty
				emit aborted(tr("Couldn't restore a pre-flight test rename. "
								"'%1' needs to be renamed back to '%2' — "
								"Avid or another app probably grabbed it "
								"between the two attempts. No rebalance "
								"moves have been made; MediaMuster will keep "
								"trying to restore it on the next launch.")
								 .arg(probe, src));
				return;
			}
			jop.skipped(); // out and back: disk unchanged
		}
	}

	// MARK: Pre-create new folders

	// Per-move failures are handled inside the move loop; if one
	// of these mkpaths fails the corresponding moves will fail
	// naturally and log themselves.
	for (const FolderId &fid : plan.newFolders)
	{
		const QString path = plan.mxfRoot + QLatin1Char('/') + fid.display();
		if (!QDir().mkpath(path))
			emit log(QtCriticalMsg, QStringLiteral("Failed to create folder: %1").arg(path));
	}

	// MARK: Group ops for atomic cancel boundaries

	// Cancel only fires between relatives groups, never within
	// one. Lone files become one-op groups, so this is effectively
	// per-file cancel for them.
	QHash<QString, QVector<int>> opsByComp;
	QStringList compOrder;
	for (int i = 0; i < plan.ops.size(); ++i)
	{
		const QString key = relativesKey(plan.ops[i]);
		if (!opsByComp.contains(key))
			compOrder.append(key);
		opsByComp[key].append(i);
	}

	int doneCount = 0;
	// ~30 Hz emit cap, same throttle the scanner/copy loops use.
	ProgressThrottle throttle;

	// Warn once if the journal degrades mid-run (see OpJournal::degraded()):
	// the moves continue, but the user must know crash recovery stopped
	// covering them.
	bool journalDegradedWarned = false;

	// MARK: Execute moves

	for (const QString &compKey : compOrder)
	{
		// Cancel only at the top of each group; once we start a
		// group we finish it, so relatives stay atomic.
		if (m_job.isCancelled())
		{
			cancelled = true;
			break;
		}

		for (int idx : opsByComp[compKey])
		{
			const MoveOp &op = plan.ops[idx];
			const QString destPath = plan.mxfRoot + QLatin1Char('/') + op.dest.display() +
									 QLatin1Char('/') + QFileInfo(op.srcPath).fileName();

			if (QFile::exists(destPath))
			{
				emit log(QtCriticalMsg,
						 QStringLiteral("Destination already exists, skipping: %1").arg(destPath));
				++failed;
			}
			else
			{
				JournalOp jop(journal.get(), op.srcPath, destPath, op.sizeBytes);
				if (QFile::rename(op.srcPath, destPath))
				{
					jop.done();
					++succeeded;
					touched.insert(op.dest);
					if (const auto srcFid = srcFolderOf(op.srcPath))
						touched.insert(*srcFid);
				}
				else
				{
					jop.failed(QStringLiteral("rename failed"));
					emit log(QtWarningMsg, QStringLiteral("Move failed: %1 → %2").arg(op.srcPath, destPath));
					++failed;
				}
			}
			++doneCount;

			if (journal->degraded() && !journalDegradedWarned)
			{
				journalDegradedWarned = true;
				emit log(QtCriticalMsg, OpJournal::degradedText());
			}

			if (doneCount == total || throttle.shouldEmit())
				emit progress(doneCount, total, QFileInfo(op.srcPath).fileName());
		}
	}

	// Cancel means stop and keep: close the journal clean so recovery
	// leaves the moves that did happen in place, then delete it. The DB
	// reset below isn't journalled because Avid regenerates those files.
	journal->finish(succeeded, failed, /*skipped=*/0, cancelled);
	journal->prune();

	// MARK: Reset per-folder databases

	// Delete msmMMOB.mdb and msmFMID.pmr; Avid regenerates them
	// on next launch. Stale DBs would show ghost/missing entries.
	for (const FolderId &fid : touched)
	{
		const QString folderPath = plan.mxfRoot + QLatin1Char('/') + fid.display();
		for (const char *db : {"/msmMMOB.mdb", "/msmFMID.pmr"})
		{
			QFile f(folderPath + QLatin1String(db));
			if (f.exists() && !f.remove())
				emit log(QtWarningMsg, QStringLiteral("Couldn't delete %1").arg(f.fileName()));
		}
	}

	emit log(
		QtInfoMsg,
		QStringLiteral("Rebalance %1: %2 moved, %3 failed%4")
			.arg(cancelled ? QStringLiteral("cancelled") : QStringLiteral("complete"),
				 Format::count(succeeded), Format::count(failed),
				 touched.isEmpty()
					 ? QString()
					 : QStringLiteral(", databases reset in %1 folders")
						   .arg(Format::count(touched.size()))));
	emit finished(succeeded, failed, cancelled);
}