#include "rebalancer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <climits>

Rebalancer::Rebalancer(QObject *parent) : QObject(parent) {}

Rebalancer::~Rebalancer()
{
	if (m_thread)
	{
		cancel();
		m_thread->quit();
		if (!m_thread->wait(5000))
		{
			m_thread->terminate();
			m_thread->wait(1000);
		}
	}
}

void Rebalancer::cancel()
{
	m_cancel.store(true, std::memory_order_relaxed);
}

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
	// Round-trip guard rejects ".5", "01", and other non-canonical names.
	if (fid.display() != name)
		return std::nullopt;
	return fid;
}

namespace
{
	// Loose files get a unique singleton key so they each become their own group.
	QString compositionKey(const MediaFile &mf)
	{
		return mf.compositionMobId.isEmpty()
				   ? QStringLiteral("__lone__:") + mf.filePath
				   : mf.compositionMobId;
	}

	// MoveOp doesn't carry the source FolderId, so re-parse from the path.
	std::optional<FolderId> srcFolderOf(const QString &srcPath)
	{
		const QString folderName =
			QFileInfo(QFileInfo(srcPath).absolutePath()).fileName();
		return Rebalancer::parseFolderName(folderName);
	}
} // namespace

RebalancePlan Rebalancer::computePlan(const QString &mxfRoot,
									  const QString &driveLabel,
									  const QVector<MediaFile> &files)
{
	RebalancePlan plan;
	plan.mxfRoot = mxfRoot;
	plan.driveLabel = driveLabel;

	QDir mxfDir(mxfRoot);
	if (!mxfDir.exists())
	{
		plan.warnings.append(
			QObject::tr("MXF root not found: %1").arg(mxfRoot));
		return plan;
	}

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

	// Per-folder bytes come only from indexed files; ΔBytes is what matters here.
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

	// Bucket by compositionMobId so siblings of one master clip stay together.
	// Out-of-scope folders ("Quarantined Files") stay put.
	QHash<QString, QVector<MediaFile>> bucketed;
	for (const auto &mf : files)
	{
		if (parseFolderName(mf.mxfFolder))
			bucketed[compositionKey(mf)].append(mf);
	}

	// For each group, pick a home prefix + home N.
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
			it.key().startsWith("__lone__:") ? QString() : it.key();

		QHash<QString, int> prefixCount;
		for (const auto &m : g.members)
		{
			if (const auto fid = parseFolderName(m.mxfFolder))
				prefixCount[fid->prefix] += 1;
		}

		// Most-populous prefix wins; tie broken by sorted prefix.
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

		// Home N = smallest N within the home prefix that contains a member.
		int home = INT_MAX;
		for (const auto &m : g.members)
		{
			const auto fid = parseFolderName(m.mxfFolder);
			if (fid && fid->prefix == g.homePrefix)
				home = qMin(home, fid->n);
		}
		g.homeN = (home == INT_MAX ? 1 : home);

		// Warn on cross-host-prefix groups; rare, surfaces ownership crossings.
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

	// Sort groups: home prefix asc, home N asc, size desc within.
	std::sort(groups.begin(), groups.end(),
			  [](const Group &a, const Group &b)
			  {
				  if (a.homePrefix != b.homePrefix)
					  return a.homePrefix < b.homePrefix;
				  if (a.homeN != b.homeN)
					  return a.homeN < b.homeN;
				  return a.members.size() > b.members.size();
			  });

	// Pack per-prefix; one host per prefix is invariant.
	QHash<FolderId, int> projected = realCount;
	QHash<QString, QSet<int>> newByPrefix;

	auto allFoldersForPrefix = [&](const QString &prefix)
	{
		QSet<int> all = existingByPrefix.value(prefix);
		for (int n : newByPrefix.value(prefix))
			all.insert(n);
		return all;
	};

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

		// Edge: composition >= CAP; split deterministically. Practically impossible.
		if (size >= CAP)
		{
			plan.warnings.append(QObject::tr(
									 "Composition '%1' has %2 streams (≥ %3); splitting "
									 "across multiple folders.")
									 .arg(g.compositionMobId.isEmpty()
											  ? QStringLiteral("(loose)")
											  : g.compositionMobId)
									 .arg(size)
									 .arg(CAP));

			int idx = 0;
			while (idx < size)
			{
				FolderId target;
				const int slackHome = CAP - projected.value(home, 0);
				if (idx == 0 && slackHome >= (size - idx))
					target = home;
				else
					target = allocateNewFolder(prefix);
				const int slack =
					CAP - projected.value(target, 0);
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

		// Already entirely at home and home isn't over CAP — leave it.
		if (membersAtHome == size && projected.value(home, 0) <= CAP)
			continue;

		// Strays fit in home? Pull them in.
		if (projected.value(home, 0) + neededAtHome <= CAP)
		{
			for (const auto &m : g.members)
			{
				const auto fid = parseFolderName(m.mxfFolder);
				if (fid && *fid != home)
					pushOp(m, home);
			}
			continue;
		}

		// Home full; first-fit asc within prefix, else allocate.
		FolderId dest;
		bool found = false;
		QSet<int> all = allFoldersForPrefix(prefix);
		QList<int> sortedNs(all.constBegin(), all.constEnd());
		std::sort(sortedNs.begin(), sortedNs.end());
		for (int n : sortedNs)
		{
			FolderId cand{prefix, n};
			if (projected.value(cand, 0) + size <= CAP)
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

	// Tally per-folder filesIn/filesOut/bytesIn/bytesOut for the preview.
	QHash<FolderId, FolderState *> stateByFid;
	for (auto &fs : plan.folders)
	{
		if (fs.inScope)
			stateByFid[fs.id] = &fs;
	}

	for (const auto &op : plan.ops)
	{
		if (auto srcFid = srcFolderOf(op.srcPath); srcFid && stateByFid.contains(*srcFid))
		{
			stateByFid[*srcFid]->filesOut += 1;
			stateByFid[*srcFid]->bytesOut += op.sizeBytes;
		}
		if (stateByFid.contains(op.dest))
		{
			stateByFid[op.dest]->filesIn += 1;
			stateByFid[op.dest]->bytesIn += op.sizeBytes;
		}
	}

	return plan;
}

void Rebalancer::executeAsync(const RebalancePlan &plan)
{
	if (m_thread && m_thread->isRunning())
	{
		cancel();
		m_thread->wait(5000);
	}
	m_cancel.store(false, std::memory_order_relaxed);
	m_thread = QThread::create([this, plan] { doExecute(plan); });
	connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
	connect(m_thread, &QThread::finished, this, [this] { m_thread = nullptr; });
	m_thread->start();
}

void Rebalancer::doExecute(const RebalancePlan &plan)
{
	int succeeded = 0;
	int failed = 0;
	bool cancelled = false;
	QSet<FolderId> touched;
	const int total = plan.ops.size();

	// Pre-flight: probe rename per donor folder; failure = files locked.
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
			if (!QFile::rename(probe, src))
			{
				emit log(2,
						 tr("Probe rename-back failed for %1; left at %2")
							 .arg(src, probe));
			}
		}
	}

	// Pre-create new folders; per-move failures handled below.
	for (const FolderId &fid : plan.newFolders)
	{
		const QString path = plan.mxfRoot + "/" + fid.display();
		if (!QDir().mkpath(path))
			emit log(2, tr("Failed to create folder: %1").arg(path));
	}

	// Group ops by composition for atomic cancel boundaries; lone files are single-op groups.
	QHash<QString, QVector<int>> opsByComp;
	QStringList compOrder;
	for (int i = 0; i < plan.ops.size(); ++i)
	{
		const QString key =
			plan.ops[i].compositionMobId.isEmpty()
				? (QStringLiteral("__lone__:") + plan.ops[i].srcPath)
				: plan.ops[i].compositionMobId;
		if (!opsByComp.contains(key))
			compOrder.append(key);
		opsByComp[key].append(i);
	}

	int doneCount = 0;
	QElapsedTimer timer;
	timer.start();
	qint64 lastEmit = -100;

	for (const QString &compKey : compOrder)
	{
		// Cancel only at composition-group boundaries; siblings stay atomic.
		if (m_cancel.load(std::memory_order_relaxed))
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

			// 30 Hz throttle on progress emit.
			const qint64 now = timer.elapsed();
			if (now - lastEmit >= 33 || doneCount == total)
			{
				emit progress(doneCount, total,
							  QFileInfo(op.srcPath).fileName());
				lastEmit = now;
			}
		}
	}

	// Delete stale per-folder DBs; Avid rebuilds them on next project open.
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