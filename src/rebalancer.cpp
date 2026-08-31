#include "rebalancer.h"
#include "conventions.h"
#include "formatutil.h"
#include "progressthrottle.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <limits>

// MARK: - Construction

Rebalancer::Rebalancer(QObject *parent)
	: QObject(parent)
{
	m_engine = new OpManager(this);

	// The Avid per-folder database reset itself lives in the ENGINE's
	// Rename machine (so an undo of a rebalance resets them too, and the
	// honest-absence ordering is enforced in one place — see
	// oprunner.cpp's touchFolder). This hook only counts the folders for
	// the summary line. (Runs on the engine's worker thread.)
	m_engine->renameFolderTouched = [this](const QString &)
	{ m_foldersReset.fetch_add(1, std::memory_order_relaxed); };

	// Signal adaptation, once: the engine speaks OpManager, the dialog
	// speaks Rebalancer, and RebalanceDialog's four connects stay
	// exactly as they were.
	connect(m_engine, &OpManager::operationProgress, this,
			[this](const QString &name, int current, int total, double)
			{ emit progress(current, total, name); });
	connect(m_engine, &OpManager::operationLog, this,
			[this](QtMsgType level, const QString &message) { emit log(level, message); });
	connect(m_engine, &OpManager::operationItemDone, this,
			[this](const QString &name, const QString &, bool ok, const QString &error, bool)
			{
				// Successes stay quiet (the progress line already moves);
				// every refusal or failure is worth a console line.
				if (!ok)
					emit log(QtWarningMsg, QStringLiteral("%1: %2").arg(name, error));
			});
	connect(m_engine, &OpManager::operationFinished, this,
			[this](int succeeded, int failed)
			{
				const int reset = m_foldersReset.load(std::memory_order_relaxed);
				if (reset > 0)
					emit log(QtInfoMsg,
							 QStringLiteral("Avid databases reset in %1 folder(s); Avid "
											"rebuilds them on next launch.")
								 .arg(Format::count(reset)));
				emit finished(succeeded, failed,
							  m_cancelRequested.load(std::memory_order_acquire));
			});
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
	/// per-folder count and stole slots from the Conventions::kFolderRecommend packing.
	/// The name rule itself lives in Conventions. It is the BUDGET rule,
	/// deliberately narrower than the table's (see isAvidMediaName): the
	/// preview counts what Avid counts, not what the table shows.
	bool countsTowardFolderBudget(const QString &fileName)
	{
		return Conventions::countsAsEssenceName(fileName);
	}

	/// What a pre-flight check can conclude about a donor folder.
	enum class FolderCheck
	{
		Ok,		   ///< A scratch file was created and renamed here.
		Refused,   ///< The folder itself won't have it: gone, or read-only.
		Unverified ///< Couldn't create a scratch file for some other reason.
	};

	/// Can this folder accept the renames a rebalance is about to make?
	/// Proved with a scratch file of our own — create it, rename it, delete
	/// it — the way the rest of the app tests a location (compare
	/// OpJournal::standardDirWritable).
	///
	/// This check used to rename one of the USER'S clips out and straight
	/// back. That tested the same folder permission, but if the app died in
	/// the moment between the two renames it left a real clip under a name
	/// Avid cannot see — a vanished clip, and a whole write-ahead journal
	/// plus a recovery sweep existed only to put it back. No pre-flight
	/// result is worth that.
	///
	/// Creating a file needs free space; renaming one does not. So a
	/// create failure that ISN'T a permissions problem returns Unverified
	/// rather than Refused: a workspace with no room left is exactly when a
	/// rebalance is worth running, and every move reports itself if it
	/// fails. A clip Avid holds open is likewise not caught here — that one
	/// fails its own rename in the move loop, where it is counted and
	/// logged.
	FolderCheck checkFolderRenames(const QString &folderPath, QString &detail)
	{
		const QFileInfo info(folderPath);
		if (!info.isDir())
		{
			detail = QStringLiteral("the folder is no longer there");
			return FolderCheck::Refused;
		}

		const QString scratch = folderPath + QStringLiteral("/.mm_preflight_") +
								QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
		{
			QFile f(scratch);
			if (!f.open(QIODevice::WriteOnly))
			{
				detail = f.errorString();
				return info.isWritable() ? FolderCheck::Unverified : FolderCheck::Refused;
			}
			f.write("x", 1);
		}

		const QString renamed = scratch + QStringLiteral(".tmp");
		const bool ok = QFile::rename(scratch, renamed);
		QFile::remove(ok ? renamed : scratch);
		if (!ok)
		{
			detail = QStringLiteral("a test rename was refused");
			return FolderCheck::Refused;
		}
		return FolderCheck::Ok;
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
	// check Conventions::kFolderRecommend against future state, not on-disk state.
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

		// Edge case: relatives group >= Conventions::kFolderRecommend. Deterministic split
		// across new folders.
		if (size >= Conventions::kFolderRecommend)
		{
			int idx = 0;
			while (idx < size)
			{
				FolderId target;
				const int slackHome = Conventions::kFolderRecommend - projected.value(home, 0);
				if (idx == 0 && slackHome >= (size - idx))
					target = home;
				else
					target = allocateNewFolder(prefix);
				const int slack = Conventions::kFolderRecommend - projected.value(target, 0);
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

		// Already entirely at home and home isn't over Conventions::kFolderRecommend, so
		// leave it. Common case for a mildly fragmented project.
		if (membersAtHome == size && projected.value(home, 0) <= Conventions::kFolderRecommend)
			continue;

		// Strays fit in home? Pull them in.
		if (projected.value(home, 0) + neededAtHome <= Conventions::kFolderRecommend)
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
			if (projected.value(cand, 0) + size <= Conventions::kFolderRecommend)
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
	m_cancelRequested.store(false, std::memory_order_release);
	m_foldersReset.store(0, std::memory_order_relaxed);

	// Phase 1, on our own short-lived worker (a dead network mount must
	// not freeze the GUI): the donor pre-flight and folder creation,
	// unchanged from v1, then the plan becomes an engine request.
	m_preflight.start(
		[this, plan]
		{
			// MARK: Pre-flight — can each donor folder accept renames?
			//
			// A folder that is gone or read-only aborts the run before
			// anything moves, with a scratch file paying for the answer
			// rather than a clip. A single clip another app holds open
			// isn't caught here; it fails its own rename in the engine,
			// where it is counted and logged.
			{
				QSet<FolderId> donors;
				for (const MoveOp &op : plan.ops)
					if (const auto srcFid = srcFolderOf(op.srcPath))
						donors.insert(*srcFid);

				for (const FolderId &fid : donors)
				{
					QString detail;
					const FolderCheck check = checkFolderRenames(
						plan.mxfRoot + QLatin1Char('/') + fid.display(), detail);
					if (check == FolderCheck::Ok)
						continue;
					if (check == FolderCheck::Unverified)
					{
						emit log(QtWarningMsg,
								 tr("Couldn't pre-check folder '%1' (%2). Carrying on: moving "
									"a file needs no free space, and any file that can't move "
									"is reported.")
									 .arg(fid.display(), detail));
						continue;
					}
					emit aborted(tr("Can't move files out of folder '%1' — %2. If Avid Media "
									"Composer (or another app) is using these files, quit it "
									"and try again.")
									 .arg(fid.display(), detail));
					return; // no moves performed; no finished will follow
				}
			}

			// MARK: Pre-create new folders
			//
			// Per-move failures are handled by the engine; if one of
			// these mkpaths fails the corresponding renames fail
			// naturally and log themselves.
			for (const FolderId &fid : plan.newFolders)
			{
				const QString path = plan.mxfRoot + QLatin1Char('/') + fid.display();
				if (!QDir().mkpath(path))
					emit log(QtCriticalMsg,
							 QStringLiteral("Failed to create folder: %1").arg(path));
			}

			// MARK: Build the engine request, group-contiguously
			//
			// Relatives (one master clip's video + audio essence) are
			// contiguous in the item order and share a groupKey, and the
			// engine's Rename machine only honours cancel at group
			// boundaries — so relatives stay atomic, exactly as before.
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
					const MoveOp &op = plan.ops[idx];
					const QString fileName = QFileInfo(op.srcPath).fileName();
					OpItem it;
					it.src = op.srcPath;
					it.name = fileName;
					it.bytes = op.sizeBytes;
					it.masterMobId = op.masterMobId;
					it.renameDst = plan.mxfRoot + QLatin1Char('/') + op.dest.display() +
								   QLatin1Char('/') + fileName;
					it.groupKey = compKey;
					req.items.append(it);
				}
			}

			// A cancel that raced the pre-flight: stop before dispatch.
			if (m_preflight.isCancelled())
			{
				emit finished(0, 0, /*cancelled=*/true);
				return;
			}

			// Phase 2 must start from the GUI thread — BackgroundJob's
			// start() manages its worker from its owner's thread — so
			// hop back queued. This worker then exits.
			QMetaObject::invokeMethod(
				this, [this, req = std::move(req)]() mutable { startEngineRun(std::move(req)); },
				Qt::QueuedConnection);
		});
}

void Rebalancer::startEngineRun(OpRequest request)
{
	// The engine takes it from here: write-ahead journal, identity gates,
	// per-rename recovery coverage, undo candidacy. Its signals were
	// adapted onto ours in the constructor.
	m_engine->execute(std::move(request));
}
