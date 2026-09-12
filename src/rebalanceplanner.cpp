#include "rebalanceplanner.h"
#include "mobid.h"
#include "pathkey.h"
#include "conventions.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <limits>

// MARK: - Folder name parsing

std::optional<FolderName> RebalancePlanner::parseFolderName(const QString &name)
{
	const int lastDot = name.lastIndexOf('.');
	const QString prefix = lastDot < 0 ? QString() : name.left(lastDot);
	const QString tail = lastDot < 0 ? name : name.mid(lastDot + 1);

	bool ok = false;
	const int n = tail.toInt(&ok);
	if (!ok || n <= 0)
		return std::nullopt;

	FolderName fid{prefix, n};

	// Round-trip guard. `.5`, `01`, `MartysiMac.005` all parse as
	// `n=5` but don't survive a canonical re-render; rejecting
	// them keeps us from 'rebalancing' a folder into a name that
	// doesn't match what was on disk.
	if (fid.display() != name)
		return std::nullopt;
	return fid;
}

std::optional<FolderName> RebalancePlanner::srcFolderOf(const QString &srcPath)
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
/// per-folder count and stole slots from the Conventions::kFolderTarget packing.
/// The name rule itself lives in Conventions. It is the BUDGET rule,
/// deliberately narrower than the table's (see isAvidMediaName): the
/// preview counts what Avid counts, not what the table shows.
bool countsTowardFolderBudget(const QString &fileName)
{
	return Conventions::countsAsEssenceName(fileName);
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
	if (masterMobId.isEmpty() || MobId::isAllZero(masterMobId) ||
		MobId::toPmrForm(masterMobId).isEmpty())
		return kLoneKeyPrefix + fallbackPath;
	const QFileInfo parent(QFileInfo(fallbackPath).absolutePath());
	const auto folder = RebalancePlanner::parseFolderName(parent.fileName());
	const QString prefix = folder ? folder->prefix : QString();
	return parent.absolutePath() + QChar(0x1f) + prefix + QChar(0x1f) + masterMobId;
}

QString relativesKey(const MediaFile &mf)
{
	return relativesKey(mf.masterMobId, mf.filePath);
}

QString relativesKey(const RenameOp &op)
{
	return relativesKey(op.masterMobId, op.srcPath);
}

} // namespace

// MARK: - Plan computation

RebalancePlan RebalancePlanner::computePlan(const QString &mxfRoot, const QString &volumeLabel,
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
	QHash<FolderName, int> realCount;

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

	// MARK: Pre-parse each file's mediaFolderName once

	// parseFolderName tokenises "MartysiMac.42" into prefix+n on every
	// call; doing it inline in each per-file loop below adds up to
	// ~250k allocations on a 50k-file project. Pair each file with
	// its parsed folder here so the inner loops can read directly.
	// Files in out-of-scope folders (Quarantined, malformed names)
	// are dropped; they're excluded from rebalancing anyway.
	struct IndexedMedia
	{
		const MediaFile *file;
		FolderName folder;
	};

	// MARK: Tally bytes + bucket files into relatives groups

	// One pass over `files` does both: tallies bytes per source folder,
	// and groups files by master MOB so relatives stay together.
	QHash<FolderName, qint64> realBytes;
	QHash<QString, QVector<IndexedMedia>> bucketed;
	for (const auto &mf : files)
	{
		const auto parsed = parseFolderName(mf.mediaFolderName);
		if (!parsed)
			continue;
		if (PathKey::normalise(QFileInfo(mf.filePath).absolutePath()) !=
			PathKey::normalise(QDir(mxfRoot).filePath(mf.mediaFolderName)))
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
	// FolderName so all downstream loops are re-parse-free.
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
		g.masterMobId =
			it.key().startsWith(kLoneKeyPrefix) ? QString() : g.members.first().file->masterMobId;

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
				  if (a.members.size() != b.members.size())
					  return a.members.size() > b.members.size();
				  if (a.masterMobId != b.masterMobId)
					  return a.masterMobId < b.masterMobId;
				  return a.members.first().file->filePath < b.members.first().file->filePath;
			  });

	// MARK: Pack groups into folders

	// One host (one prefix) at a time. `projected` tracks the
	// running per-folder count as we plan moves into it, so we can
	// check Conventions::kFolderTarget against future state, not on-disk state.
	QHash<FolderName, int> projected = realCount;
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
	auto allocateNewFolder = [&](const QString &prefix) -> FolderName
	{
		QSet<int> all = allFoldersForPrefix(prefix);
		int next = 1;
		for (int n : all)
			if (n >= next)
				next = n + 1;
		newByPrefix[prefix].insert(next);
		FolderName id{prefix, next};
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
	auto pushOp = [&](const IndexedMedia &m, FolderName dest)
	{
		if (m.folder == dest)
			return;
		plan.ops.append({m.file->filePath, dest, m.file->masterMobId, m.file->sizeBytes,
						 m.file->modified.isValid() ? m.file->modified.toMSecsSinceEpoch() : -1});
		projected[m.folder] -= 1;
		projected[dest] += 1;
	};

	for (const Group &g : groups)
	{
		const QString &prefix = g.homePrefix;
		const FolderName home{prefix, g.homeN};
		const int size = static_cast<int>(g.members.size());

		// Oversized relatives groups need multiple folders. Keep a completed packing stable.
		// across new folders.
		if (size > Conventions::kFolderTarget)
		{
			QSet<FolderName> occupiedFolders;
			bool withinBudget = true;
			for (const auto &member : g.members)
			{
				occupiedFolders.insert(member.folder);
				withinBudget =
					withinBudget && projected.value(member.folder) <= Conventions::kFolderTarget;
			}
			const int minimumFolders =
				(size + Conventions::kFolderTarget - 1) / Conventions::kFolderTarget;
			if (withinBudget && occupiedFolders.size() == minimumFolders)
				continue;
			int idx = 0;
			while (idx < size)
			{
				FolderName target;
				const int slackHome = Conventions::kFolderTarget - projected.value(home, 0);
				if (idx == 0 && slackHome >= (size - idx))
					target = home;
				else
					target = allocateNewFolder(prefix);
				const int slack = Conventions::kFolderTarget - projected.value(target, 0);
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

		// Already entirely at home and home isn't over Conventions::kFolderTarget, so
		// leave it. Common case for a mildly fragmented project.
		if (membersAtHome == size && projected.value(home, 0) <= Conventions::kFolderTarget)
			continue;

		// Strays fit in home? Pull them in.
		if (projected.value(home, 0) + neededAtHome <= Conventions::kFolderTarget)
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
		FolderName dest;
		bool found = false;
		QSet<int> all = allFoldersForPrefix(prefix);
		QList<int> sortedNs(all.constBegin(), all.constEnd());
		std::sort(sortedNs.begin(), sortedNs.end());
		for (int n : sortedNs)
		{
			FolderName cand{prefix, n};
			if (projected.value(cand, 0) + size <= Conventions::kFolderTarget)
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
	QHash<FolderName, int> stateByFid;
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

OpRequest RebalancePlanner::requestForPlan(const RebalancePlan &plan)
{
	QHash<QString, QVector<int>> opsByComp;
	QStringList compOrder;
	for (int i = 0; i < plan.ops.size(); ++i)
	{
		const QString key = relativesKey(plan.ops[i]);
		if (!opsByComp.contains(key))
			compOrder.append(key);
		opsByComp[key].append(i);
	}

	OpRequest req;
	req.kind = OpKind::Rename;
	req.items.reserve(plan.ops.size());
	for (const QString &compKey : compOrder)
	{
		for (int idx : opsByComp[compKey])
		{
			const RenameOp &op = plan.ops[idx];
			const QString fileName = QFileInfo(op.srcPath).fileName();
			OpItem it;
			it.src = op.srcPath;
			it.name = fileName;
			it.bytes = op.sizeBytes;
			it.modifiedMs = op.modifiedMs;
			it.masterMobId = op.masterMobId;
			it.renameDst =
				plan.mxfRoot + QLatin1Char('/') + op.dest.display() + QLatin1Char('/') + fileName;
			it.groupKey = compKey;
			req.items.append(it);
		}
	}
	return req;
}
