#include "rebalanceplanner.h"
#include "mobid.h"
#include "pathkey.h"
#include "conventions.h"
#include "avidmedialayout.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <limits>

namespace
{
	bool accessibleDirectory(const QString &path)
	{
		const QFileInfo directory(path);
		if (!directory.isDir() || !directory.isReadable())
			return false;
#ifdef Q_OS_UNIX
		// Directory search permission is distinct from listing permission.
		if (!directory.isExecutable())
			return false;
#endif
		return true;
	}

	QString resolvedMxfRoot(const QString &path)
	{
		const QFileInfo root(path);
		if (!AvidMediaLayout::isMxfRoot(path) || !root.isDir())
			return {};
		const QString resolved = root.canonicalFilePath();
		return AvidMediaLayout::isMxfRoot(resolved) ? resolved : QString{};
	}

	bool folderBelongsToRoot(const QString &path, const QString &resolvedRoot, bool allowMissing = false)
	{
		if (resolvedRoot.isEmpty())
			return false;
		const QFileInfo folder(path);
		const QString expected = QDir(resolvedRoot).filePath(folder.fileName());
		if (!folder.exists())
			return allowMissing && !folder.isSymLink() &&
				   PathKey::normalise(path) == PathKey::normalise(expected);
		if (!folder.isDir())
			return false;
		const QString actual = folder.canonicalFilePath();
		const auto location = AvidMediaLayout::locateMediaFolder(actual);
		return location && location->family == AvidMediaLayout::Family::Mxf && !location->isQuarantined &&
			   PathKey::normalise(actual) == PathKey::normalise(expected);
	}
} // namespace

// MARK: - Folder name parsing

std::optional<FolderName> RebalancePlanner::parseFolderName(const QString &name)
{
	const auto parsed = AvidMediaLayout::parseMxfFolderName(name);
	if (!parsed)
		return std::nullopt;

	bool ok = false;
	const int n = parsed->digits.toInt(&ok);
	if (!ok || n <= 0)
		return std::nullopt;

	FolderName fid{parsed->prefix, n};

	// The shared reader accepts padded names such as `01` and
	// `MartysiMac.005`. Rebalance requires the original spelling to survive
	// integer conversion so it never plans a rename using a different name.
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

bool RebalancePlanner::isEligible(const MediaFile &file)
{
	const QFileInfo source(file.filePath);
	if (file.omfEra || file.isQuarantined || !QDir::isAbsolutePath(file.filePath) || source.isSymLink() ||
		!AvidMediaLayout::acceptsFileName(AvidMediaLayout::Family::Mxf,
										  source.fileName()))
		return false;
	const auto location =
		AvidMediaLayout::locateMediaFolder(source.absolutePath());
	return location && location->family == AvidMediaLayout::Family::Mxf &&
		   parseFolderName(location->folderName).has_value() &&
		   location->folderName == file.mediaFolderName &&
		   folderBelongsToRoot(source.absolutePath(), resolvedMxfRoot(location->rootPath));
}

// MARK: - Planning helpers

namespace
{
	/// True when a directory entry occupies Avid's per-folder file budget.
	/// In an `Avid MediaFiles/MXF/<n>` folder only MXF essence counts:
	/// Avid's own databases, dot-hidden files, shell junk, and stray
	/// non-MXF files are not media. Counting them inflated the preview's
	/// per-folder count and stole slots from the Conventions::kFolderTarget packing.
	/// The name rule lives in Conventions. Managed-folder admission is
	/// checked separately before these entries are included in a plan.
	bool countsTowardFolderBudget(const QString &fileName)
	{
		return Conventions::countsAsEssenceName(fileName);
	}

	RebalancePlanner::FolderCount readFolderCount(const QString &path)
	{
		const QFileInfo before(path);
		RebalancePlanner::FolderCount result{-1, before.exists() || before.isSymLink()};
		if (!accessibleDirectory(path))
			return result;
		const QString resolved = before.canonicalFilePath();
		if (resolved.isEmpty())
			return result;
		int count = 0;
		QDirIterator entries(path, QDir::Files | QDir::NoDotAndDotDot);
		while (entries.hasNext())
		{
			entries.next();
			if (countsTowardFolderBudget(entries.fileName()))
				++count;
		}
		if (accessibleDirectory(path) && QFileInfo(path).canonicalFilePath() == resolved)
			result.count = count;
		return result;
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

QHash<FolderName, RebalancePlanner::FolderCount>
RebalancePlanner::countFolders(const QString &mxfRoot, const QSet<FolderName> &folders)
{
	QHash<FolderName, FolderCount> results;
	for (const FolderName &folder : folders)
		results.insert(folder, {});
	const QString resolvedRoot = resolvedMxfRoot(mxfRoot);
	if (resolvedRoot.isEmpty() || !accessibleDirectory(mxfRoot))
		return results;

	const QDir root(mxfRoot);
	const QStringList entries = root.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	for (const FolderName &folder : folders)
	{
		const QString name = folder.display();
		const auto parsed = parseFolderName(name);
		if (!parsed || *parsed != folder)
			continue;
		const QString path = root.filePath(name);
		const QFileInfo info(path);
		FolderCount &result = results[folder];
		result.exists = info.exists() || info.isSymLink();
		if (!result.exists && !entries.contains(name))
			result.count = 0;
		else if (folderBelongsToRoot(path, resolvedRoot))
			result = readFolderCount(path);
	}
	if (!accessibleDirectory(mxfRoot) || resolvedMxfRoot(mxfRoot) != resolvedRoot)
		for (FolderCount &result : results)
			result.count = -1;
	return results;
}

// MARK: - Plan computation

RebalancePlan RebalancePlanner::computePlan(const QString &mxfRoot, const QString &volumeLabel,
											const QVector<MediaFile> &files)
{
	RebalancePlan plan;
	plan.mxfRoot = mxfRoot;
	plan.volumeLabel = volumeLabel;

	QDir mxfDir(mxfRoot);
	const QString resolvedRoot = resolvedMxfRoot(mxfRoot);
	if (resolvedRoot.isEmpty() || !accessibleDirectory(mxfRoot))
		return plan;

	// MARK: Snapshot current folder state on disk

	QHash<QString, QSet<int>> existingByPrefix; // prefix → {n}
	QHash<QString, QSet<int>> occupiedByPrefix; // Includes aliases that cannot be destinations.
	QHash<FolderName, int> realCount;

	const QStringList subdirs = mxfDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
	for (const QString &name : subdirs)
	{
		const FolderCount onDisk = readFolderCount(mxfDir.filePath(name));
		const auto parsed = parseFolderName(name);
		if (parsed)
			occupiedByPrefix[parsed->prefix].insert(parsed->n);
		FolderState fs;
		fs.name = name;
		fs.count = onDisk.count;
		fs.inScope = onDisk.count >= 0 && parsed.has_value() && folderBelongsToRoot(mxfDir.filePath(name), resolvedRoot);
		if (fs.inScope)
		{
			fs.id = *parsed;
			existingByPrefix[fs.id.prefix].insert(fs.id.n);
			realCount[fs.id] = onDisk.count;
		}
		plan.folders.append(fs);
	}
	if (!accessibleDirectory(mxfRoot) || resolvedMxfRoot(mxfRoot) != resolvedRoot)
	{
		for (FolderState &folder : plan.folders)
		{
			folder.count = -1;
			folder.inScope = false;
		}
		return plan;
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
		if (!isEligible(mf))
			continue;
		const auto parsed = parseFolderName(mf.mediaFolderName);
		if (!parsed || !realCount.contains(*parsed))
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

	// Allocate above the highest occupied number in this prefix, including
	// aliases. Register it now so later groups cannot reuse its number.
	auto allocateNewFolder = [&](const QString &prefix) -> FolderName
	{
		QSet<int> all = occupiedByPrefix.value(prefix);
		all.unite(newByPrefix.value(prefix));
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
						 m.file->modified.isValid() ? m.file->modified.toMSecsSinceEpoch() : -1,
						 m.file->mobId});
		projected[m.folder] -= 1;
		projected[dest] += 1;
	};

	for (const Group &g : groups)
	{
		const QString &prefix = g.homePrefix;
		const FolderName home{prefix, g.homeN};
		const int size = static_cast<int>(g.members.size());

		// Split oversized groups only when their existing packing exceeds the
		// budget or uses more than the minimum number of folders.
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
	OpRequest req;
	req.kind = OpKind::Rename;
	const QString resolvedRoot = resolvedMxfRoot(plan.mxfRoot);
	if (resolvedRoot.isEmpty())
		return req;

	// Validate the whole plan before assembling any relatives group. A stale or
	// malformed member must not silently turn a group move into a partial move.
	const QString rootKey = PathKey::normalise(resolvedRoot);
	for (const RenameOp &op : plan.ops)
	{
		const QFileInfo source(op.srcPath);
		const auto location = AvidMediaLayout::locateMediaFolder(source.absolutePath());
		const auto sourceFolder = srcFolderOf(op.srcPath);
		const auto destination = parseFolderName(op.dest.display());
		if (!QDir::isAbsolutePath(op.srcPath) || source.isSymLink() ||
			!AvidMediaLayout::acceptsFileName(AvidMediaLayout::Family::Mxf, source.fileName()) ||
			!location || location->family != AvidMediaLayout::Family::Mxf ||
			PathKey::normalise(resolvedMxfRoot(location->rootPath)) != rootKey || !sourceFolder ||
			!destination || *destination != op.dest ||
			sourceFolder->prefix != destination->prefix || *sourceFolder == *destination ||
			!folderBelongsToRoot(source.absolutePath(), resolvedRoot) ||
			!folderBelongsToRoot(QDir(plan.mxfRoot).filePath(op.dest.display()), resolvedRoot, true))
			return req;
	}

	QHash<QString, QVector<int>> opsByComp;
	QStringList compOrder;
	for (int i = 0; i < plan.ops.size(); ++i)
	{
		const QString key = relativesKey(plan.ops[i]);
		if (!opsByComp.contains(key))
			compOrder.append(key);
		opsByComp[key].append(i);
	}

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
			it.mobId = op.fileMobId;
			it.renameDst =
				plan.mxfRoot + QLatin1Char('/') + op.dest.display() + QLatin1Char('/') + fileName;
			it.groupKey = compKey;
			req.items.append(it);
		}
	}
	return req;
}
