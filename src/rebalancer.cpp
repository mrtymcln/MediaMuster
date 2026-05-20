#include "rebalancer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <climits>

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
	// `n=5` but don't survive a canonical re-render — rejecting
	// them keeps us from "rebalancing" a folder into a name that
	// doesn't match what was on disk.
	if (fid.display() != name)
		return std::nullopt;
	return fid;
}

// MARK: - Planning helpers

namespace
{
// Prefix for synthetic composition keys assigned to loose files
// (no compositionMobId). Lets us detect 'this was a loose file'
// later via a cheap startsWith check.
inline constexpr QLatin1String kLoneCompositionPrefix("__lone__:");

// Loose files get a unique synthetic key so each becomes a
// one-member group; composition-bound files share the MOB id of
// their master clip. Single implementation; planner-side and
// executor-side overloads delegate here so the format can't drift.
QString compositionKey(const QString &compMobId, const QString &fallbackPath)
{
	return compMobId.isEmpty()
	           ? kLoneCompositionPrefix + fallbackPath
	           : compMobId;
}

QString compositionKey(const MediaFile &mf)
{
	return compositionKey(mf.compositionMobId, mf.filePath);
}

QString compositionKey(const MoveOp &op)
{
	return compositionKey(op.compositionMobId, op.srcPath);
}

// MoveOp doesn't carry the source FolderId — recompute it from the
// source path. QFileInfo::dir().dirName() gives the parent folder
// name directly, no second QFileInfo allocation.
std::optional<FolderId> srcFolderOf(const QString &srcPath)
{
	return Rebalancer::parseFolderName(
	    QFileInfo(srcPath).dir().dirName());
}
} // namespace

// MARK: - Plan computation

RebalancePlan Rebalancer::computePlan(const QString &mxfRoot,
                                      const QString &volumeLabel,
                                      const QVector<MediaFile> &files)
{
	RebalancePlan plan;
	plan.mxfRoot = mxfRoot;
	plan.volumeLabel = volumeLabel;

	QDir mxfDir(mxfRoot);
	if (!mxfDir.exists())
	{
		plan.warnings.append(
		    QObject::tr("MXF root not found: %1").arg(mxfRoot));
		return plan;
	}

	// MARK: Snapshot current folder state on disk

	QHash<QString, QSet<int>> existingByPrefix; // prefix → {n}
	QHash<FolderId, int> realCount;

	const QStringList subdirs = mxfDir.entryList(
	    QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for (const QString &name : subdirs)
	{
		const int onDiskCount = QDir(mxfDir.filePath(name))
		                            .entryList(QDir::Files | QDir::NoDotAndDotDot)
		                            .count();

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

	// MARK: Tally bytes per folder from the indexed file set

	// Only indexed files contribute — the before/after preview
	// shows delta bytes, so un-indexed files don't matter.
	QHash<FolderId, qint64> realBytes;
	for (const auto &mf : files)
	{
		if (const auto fid = parseFolderName(mf.mxfFolder))
			realBytes[*fid] += mf.sizeBytes;
	}
	for (auto &fs : plan.folders)
	{
		if (fs.inScope)
			fs.bytes = realBytes.value(fs.id, 0);
	}

	// MARK: Bucket files into composition groups

	// Relatives of one master clip stay together. Out-of-scope
	// folders (e.g. Quarantined Files) are excluded — those stay
	// put no matter how badly they push the file count up.
	QHash<QString, QVector<MediaFile>> bucketed;
	for (const auto &mf : files)
	{
		if (parseFolderName(mf.mxfFolder))
			bucketed[compositionKey(mf)].append(mf);
	}

	// MARK: Build group descriptors with chosen home folder

	// One composition (or one lone file) plus the `<prefix, n>`
	// we want to consolidate it into.
	struct Group
	{
		QString homePrefix;
		int homeN = 1;
		QString compositionMobId;
		QVector<MediaFile> members;
	};
	QVector<Group> groups;
	groups.reserve(bucketed.size());

	for (auto it = bucketed.constBegin(); it != bucketed.constEnd(); ++it)
	{
		Group g;
		g.members = it.value();
		g.compositionMobId =
		    it.key().startsWith(kLoneCompositionPrefix) ? QString() : it.key();

		QHash<QString, int> prefixCount;
		for (const auto &m : g.members)
		{
			if (const auto fid = parseFolderName(m.mxfFolder))
				prefixCount[fid->prefix] += 1;
		}

		// Most-populous prefix wins; tie broken by sorted prefix —
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
		int home = INT_MAX;
		for (const auto &m : g.members)
		{
			const auto fid = parseFolderName(m.mxfFolder);
			if (fid && fid->prefix == g.homePrefix)
				home = qMin(home, fid->n);
		}
		g.homeN = (home == INT_MAX ? 1 : home);

		// Cross-host-prefix groups are rare — usually an editor moved
		// bins between systems by hand. Surface it.
		if (prefixes.size() > 1)
		{
			plan.warnings.append(QObject::tr(
			                         "Composition '%1' has members in multiple host prefixes; "
			                         "consolidating into '%2'.")
			                         .arg(g.compositionMobId.isEmpty()
			                                  ? QStringLiteral("(loose)")
			                                  : g.compositionMobId,
			                              g.homePrefix.isEmpty()
			                                  ? QStringLiteral("(none)")
			                                  : g.homePrefix));
		}

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

	// No-op when src == dest (already where we want it).
	auto pushOp = [&](const MediaFile &m, FolderId dest)
	{
		const auto src = parseFolderName(m.mxfFolder);
		if (!src || *src == dest)
			return;
		plan.ops.append({m.filePath, dest, m.compositionMobId, m.sizeBytes});
		projected[*src] -= 1;
		projected[dest] += 1;
	};

	for (const Group &g : groups)
	{
		const QString &prefix = g.homePrefix;
		const FolderId home{prefix, g.homeN};
		const int size = static_cast<int>(g.members.size());

		// Edge case: composition >= kFolderCap. Deterministic split
		// across new folders.
		if (size >= kFolderCap)
		{
			plan.warnings.append(QObject::tr(
			                         "Composition '%1' has %2 streams (≥ %3); splitting "
			                         "across multiple folders.")
			                         .arg(g.compositionMobId.isEmpty()
			                                  ? QStringLiteral("(loose)")
			                                  : g.compositionMobId)
			                         .arg(size)
			                         .arg(kFolderCap));

			int idx = 0;
			while (idx < size)
			{
				FolderId target;
				const int slackHome = kFolderCap - projected.value(home, 0);
				if (idx == 0 && slackHome >= (size - idx))
					target = home;
				else
					target = allocateNewFolder(prefix);
				const int slack =
				    kFolderCap - projected.value(target, 0);
				const int chunk = qMin(slack, size - idx);
				for (int i = 0; i < chunk; ++i, ++idx)
					pushOp(g.members[idx], target);
			}
			continue;
		}

		int membersAtHome = 0;
		for (const auto &m : g.members)
		{
			const auto fid = parseFolderName(m.mxfFolder);
			if (fid && *fid == home)
				++membersAtHome;
		}
		const int neededAtHome = size - membersAtHome;

		// Already entirely at home and home isn't over kFolderCap —
		// leave it. Common case for a mildly fragmented project.
		if (membersAtHome == size && projected.value(home, 0) <= kFolderCap)
			continue;

		// Strays fit in home? Pull them in.
		if (projected.value(home, 0) + neededAtHome <= kFolderCap)
		{
			for (const auto &m : g.members)
			{
				const auto fid = parseFolderName(m.mxfFolder);
				if (fid && *fid != home)
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
			const auto fid = parseFolderName(m.mxfFolder);
			if (!fid || *fid != dest)
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

	// MARK: Recover orphan probe files from a previous force-quit

	// If a prior run was killed mid-probe, the suffixed file is
	// stranded. Avid ignores it (suffix breaks format recognition)
	// but the bytes are intact. Rename back where we can; skip
	// (with a log line) where the original slot is now occupied.
	{
		static const QRegularExpression probeRe(
		    QStringLiteral("^(.*)\\.__rebalprobe_[0-9a-f-]{36}$"));
		static const QLatin1String kProbeMarker(".__rebalprobe_");
		QDir mxfDir(plan.mxfRoot);
		for (const QString &subdir :
		     mxfDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		{
			QDir folder(mxfDir.filePath(subdir));
			for (const QString &name :
			     folder.entryList(QDir::Files | QDir::NoDotAndDotDot))
			{
				// Cheap substring prefilter — the regex is 5× more
				// expensive than contains, and on a 100k-file system,
				// almost no entries match anyway.
				if (!name.contains(kProbeMarker))
					continue;
				const auto m = probeRe.match(name);
				if (!m.hasMatch())
					continue;
				const QString original = m.captured(1);
				const QString suffixed = folder.filePath(name);
				const QString restored = folder.filePath(original);
				if (QFile::exists(restored))
				{
					emit log(2, tr("Orphan probe '%1' would collide with "
					               "existing '%2'; left in place.")
					                .arg(name, original));
					continue;
				}
				if (QFile::rename(suffixed, restored))
					emit log(0, tr("Recovered orphan probe: %1 → %2")
					                .arg(name, original));
				else
					emit log(2, tr("Could not rename orphan probe %1.")
					                .arg(name));
			}
		}
	}

	// MARK: Pre-flight — probe rename per donor folder

	// Rename one file from each donor folder out and immediately
	// back. If the folder is locked for any reason, the first
	// rename fails and we abort cleanly before doing real damage.
	// The retry on the rename-back handles the rare race where a
	// watcher picks the file up between the two attempts.
	{
		QHash<FolderId, QString> probeFiles;
		for (const MoveOp &op : plan.ops)
		{
			auto srcFid = srcFolderOf(op.srcPath);
			if (srcFid && !probeFiles.contains(*srcFid))
				probeFiles.insert(*srcFid, op.srcPath);
		}

		const QString suffix = QStringLiteral(".__rebalprobe_") +
		                       QUuid::createUuid().toString(
		                           QUuid::WithoutBraces);
		for (auto it = probeFiles.constBegin();
		     it != probeFiles.constEnd(); ++it)
		{
			const QString src = it.value();
			const QString probe = src + suffix;
			if (!QFile::rename(src, probe))
			{
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
				emit aborted(tr("Couldn't restore a pre-flight test rename. "
				                "'%1' needs to be renamed back to '%2' — "
				                "Avid or another app probably grabbed it "
				                "between the two attempts. No rebalance "
				                "moves have been made; restore the file "
				                "and try again.")
				                 .arg(probe, src));
				return;
			}
		}
	}

	// MARK: Pre-create new folders

	// Per-move failures are handled inside the move loop — if one
	// of these mkpaths fails the corresponding moves will fail
	// naturally and log themselves.
	for (const FolderId &fid : plan.newFolders)
	{
		const QString path = plan.mxfRoot + "/" + fid.display();
		if (!QDir().mkpath(path))
			emit log(2, tr("Failed to create folder: %1").arg(path));
	}

	// MARK: Group ops for atomic cancel boundaries

	// Cancel only fires between composition groups, never within
	// one. Lone files become one-op groups, so this is effectively
	// per-file cancel for them.
	QHash<QString, QVector<int>> opsByComp;
	QStringList compOrder;
	for (int i = 0; i < plan.ops.size(); ++i)
	{
		const QString key = compositionKey(plan.ops[i]);
		if (!opsByComp.contains(key))
			compOrder.append(key);
		opsByComp[key].append(i);
	}

	int doneCount = 0;
	QElapsedTimer timer;
	timer.start();
	qint64 lastEmit = -100;

	// MARK: Execute moves

	for (const QString &compKey : compOrder)
	{
		// Cancel only at the top of each group — once we start a
		// group we finish it, so relatives stay atomic.
		if (m_job.isCancelled())
		{
			cancelled = true;
			break;
		}

		for (int idx : opsByComp[compKey])
		{
			const MoveOp &op = plan.ops[idx];
			const QString destPath = plan.mxfRoot + QLatin1Char('/') +
			                         op.dest.display() + QLatin1Char('/') +
			                         QFileInfo(op.srcPath).fileName();

			if (QFile::exists(destPath))
			{
				emit log(2, tr("Destination already exists, skipping: %1")
				                .arg(destPath));
				++failed;
			}
			else if (QFile::rename(op.srcPath, destPath))
			{
				++succeeded;
				touched.insert(op.dest);
				if (const auto srcFid = srcFolderOf(op.srcPath))
					touched.insert(*srcFid);
			}
			else
			{
				emit log(1, tr("Move failed: %1 → %2")
				                .arg(op.srcPath, destPath));
				++failed;
			}
			++doneCount;

			// ~30 Hz throttle — same cadence as the rest of the
			// app. Last item always emits.
			const qint64 now = timer.elapsed();
			if (now - lastEmit >= 33 || doneCount == total)
			{
				emit progress(doneCount, total,
				              QFileInfo(op.srcPath).fileName());
				lastEmit = now;
			}
		}
	}

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
				emit log(1, tr("Couldn't delete %1").arg(f.fileName()));
		}
	}

	emit log(0, tr("Rebalance %1: %2 moved, %3 failed%4")
	                .arg(cancelled ? tr("cancelled") : tr("complete"))
	                .arg(succeeded)
	                .arg(failed)
	                .arg(touched.isEmpty()
	                         ? QString()
	                         : tr(", databases reset in %1 folders")
	                               .arg(touched.size())));
	emit finished(succeeded, failed, cancelled);
}