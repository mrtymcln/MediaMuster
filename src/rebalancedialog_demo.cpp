#include "rebalancedialog_demo.h"

namespace RebalanceDemo
{

	namespace
	{

		constexpr qint64 kAvgFileBytes = qint64(10) * 1024 * 1024; // 10 MB per file

		/// Build a FolderState with sensible defaults; current count drives
		/// the fullness bar's `current` value.
		FolderState makeFolder(const FolderId &id, int currentCount, bool isNew = false)
		{
			FolderState fs;
			fs.id = id;
			fs.name = id.display();
			fs.count = currentCount;
			fs.bytes = qint64(currentCount) * kAvgFileBytes;
			fs.isNew = isNew;
			fs.inScope = true;
			return fs;
		}

		FolderState makeOutOfScopeFolder(const QString &name, int fileCount)
		{
			FolderState fs;
			fs.name = name;
			fs.count = fileCount;
			fs.bytes = qint64(fileCount) * kAvgFileBytes;
			fs.inScope = false;
			return fs;
		}

		/// Locate the FolderState entry for `id`.
		FolderState *findById(QVector<FolderState> &folders, const FolderId &id)
		{
			for (auto &fs : folders)
				if (fs.inScope && fs.id == id)
					return &fs;
			return nullptr;
		}

		/// Append `count` MoveOps from src to dest into `plan` and update the
		/// corresponding folder state counters. srcPath uses a shared
		/// per-source template so 666K-op plans don't blow up memory.
		void pushMoves(RebalancePlan &plan, const FolderId &src, const FolderId &dest, int count)
		{
			if (count <= 0)
				return;

			// Shared QString; implicit sharing makes appending the same
			// path 666K times nearly free vs constructing a unique one each.
			const QString sharedSrcPath =
				QStringLiteral("/demo/Avid MediaFiles/MXF/%1/clip.mxf").arg(src.display());

			for (int i = 0; i < count; ++i)
			{
				MoveOp op;
				op.srcPath = sharedSrcPath;
				op.dest = dest;
				op.sizeBytes = kAvgFileBytes;
				plan.ops.append(op);
			}

			if (auto *srcFs = findById(plan.folders, src))
			{
				srcFs->filesOut += count;
				srcFs->bytesOut += qint64(count) * kAvgFileBytes;
			}
			if (auto *destFs = findById(plan.folders, dest))
			{
				destFs->filesIn += count;
				destFs->bytesIn += qint64(count) * kAvgFileBytes;
			}
		}

	} // namespace

	// MARK: - Scenarios

	RebalancePlan small()
	{
		RebalancePlan plan;
		plan.mxfRoot = QStringLiteral("/demo/Avid MediaFiles/MXF");
		plan.volumeLabel = QStringLiteral("Demo · Small");

		plan.folders.append(makeFolder({QString(), 1}, 3450));
		plan.folders.append(makeFolder({QString(), 2}, 1932));
		plan.folders.append(makeFolder({QString(), 3}, 412));
		plan.folders.append(makeFolder({QString(), 4}, 12));

		// 47 files migrate from "2" into "1"; "3" and "4" stay put.
		pushMoves(plan, {QString(), 2}, {QString(), 1}, 47);
		return plan;
	}

	RebalancePlan big()
	{
		RebalancePlan plan;
		plan.mxfRoot = QStringLiteral("/demo/Avid MediaFiles/MXF");
		plan.volumeLabel = QStringLiteral("Demo · Big");

		// 3 over-stuffed source folders, 3 fresh destinations.
		plan.folders.append(makeFolder({QStringLiteral("Edit14"), 1}, 4990));
		plan.folders.append(makeFolder({QStringLiteral("Edit14"), 2}, 4500));
		plan.folders.append(makeFolder({QStringLiteral("Edit14"), 3}, 4200));
		plan.folders.append(makeFolder({QStringLiteral("Edit14"), 4}, 0, true));
		plan.folders.append(makeFolder({QStringLiteral("Edit14"), 5}, 0, true));
		plan.folders.append(makeFolder({QStringLiteral("Edit14"), 6}, 0, true));
		plan.newFolders = {{QStringLiteral("Edit14"), 4},
						   {QStringLiteral("Edit14"), 5},
						   {QStringLiteral("Edit14"), 6}};

		pushMoves(plan, {QStringLiteral("Edit14"), 1}, {QStringLiteral("Edit14"), 4}, 1490);
		pushMoves(plan, {QStringLiteral("Edit14"), 2}, {QStringLiteral("Edit14"), 5}, 1500);
		pushMoves(plan, {QStringLiteral("Edit14"), 3}, {QStringLiteral("Edit14"), 6}, 1500);
		// Top up .4 from .2 so it feels like everything's moving.
		pushMoves(plan, {QStringLiteral("Edit14"), 2}, {QStringLiteral("Edit14"), 4}, 1500);
		pushMoves(plan, {QStringLiteral("Edit14"), 3}, {QStringLiteral("Edit14"), 5}, 1500);
		return plan;
	}

	RebalancePlan reallyBig()
	{
		RebalancePlan plan;
		plan.mxfRoot = QStringLiteral("/demo/Avid MediaFiles/MXF");
		plan.volumeLabel = QStringLiteral("Demo · Really Big");

		constexpr int kTargetOps = 666666;
		constexpr int kExistingFolders = 200;
		constexpr int kNewFolders = 60;
		constexpr auto kPrefix = QLatin1String("MartysiMac");

		const auto startingCount = [](int i)
		{
			if (i < 25)
				return 5500 + (i % 7) * 40 - 120; //  5,380-5,660 RED
			if (i < 100)
				return 4500 + (i % 11) * 30 - 150; // 4,350-4,650 AMBER
			return 2000 + (i % 13) * 25 - 150;	   // 1,850-2,150 GREEN
		};

		// Final state; mimics the real planner's first-fit packing:
		//   First 100 existing folders end packed AMBER (~4,200 each),
		//     the dense 'primary' working set.
		//   Next 100 existing folders end sparse GREEN (~1,500 each),
		//     the spillover area.
		//   60 new folders absorb residual at GREEN (~1,750 each).
		// Totals balance against the starting cohorts above.
		const auto finalCount = [](int i)
		{
			if (i < 100)
				return 4200 + ((i + 3) % 7) * 35 - 105; // ~4,100-4,310 AMBER
			if (i < kExistingFolders)
				return 1500 + ((i + 5) % 11) * 25 - 125;		   // ~1,375-1,625 GREEN
			return 1750 + ((i - kExistingFolders) % 9) * 30 - 120; // ~1,630-1,870
		};

		plan.folders.reserve(kExistingFolders + kNewFolders + 1);
		for (int i = 0; i < kExistingFolders; ++i)
		{
			plan.folders.append(makeFolder({QString(kPrefix), i + 1}, startingCount(i)));
		}
		for (int n = kExistingFolders + 1; n <= kExistingFolders + kNewFolders; ++n)
		{
			plan.folders.append(makeFolder({QString(kPrefix), n}, 0, true));
			plan.newFolders.append({QString(kPrefix), n});
		}
		plan.folders.append(makeOutOfScopeFolder(QStringLiteral("Quarantined Files"), 47));

		// PHASE 1: per-folder deltas, then greedy-match sources to
		// dests. Same shape as the real planner; drain over-filled
		// into under-filled until everyone hits their target.
		QVector<QPair<FolderId, int>> sources;
		QVector<QPair<FolderId, int>> dests;
		for (int i = 0; i < kExistingFolders + kNewFolders; ++i)
		{
			const int start = (i < kExistingFolders) ? startingCount(i) : 0;
			const int delta = finalCount(i) - start;
			const FolderId id{QString(kPrefix), i + 1};
			if (delta < 0)
				sources.append({id, -delta});
			else if (delta > 0)
				dests.append({id, delta});
		}

		plan.ops.reserve(kTargetOps);
		auto srcIt = sources.begin();
		auto destIt = dests.begin();
		while (srcIt != sources.end() && destIt != dests.end())
		{
			const int amount = qMin(srcIt->second, destIt->second);
			pushMoves(plan, srcIt->first, destIt->first, amount);
			srcIt->second -= amount;
			destIt->second -= amount;
			if (srcIt->second == 0)
				++srcIt;
			if (destIt->second == 0)
				++destIt;
		}

		// PHASE 2: pair-wise shuffles to inflate the op count to
		// kTargetOps. Each A-B pair exchanges N files in each
		// direction; net zero change to either folder, but 2N ops
		// generated. Keeps every folder at its post-phase-1 count.
		const int phase2Needed = kTargetOps - int(plan.ops.size());
		const int opsPerDirection = qMax(0, phase2Needed / kExistingFolders);
		for (int i = 0; i + 1 < kExistingFolders; i += 2)
		{
			const FolderId a{QString(kPrefix), i + 1};
			const FolderId b{QString(kPrefix), i + 2};
			pushMoves(plan, a, b, opsPerDirection);
			pushMoves(plan, b, a, opsPerDirection);
		}

		// Balanced top-up for any remainder from integer division.
		const int remainder = kTargetOps - int(plan.ops.size());
		if (remainder > 0)
		{
			const FolderId a{QString(kPrefix), 1};
			const FolderId b{QString(kPrefix), 2};
			const int half = remainder / 2;
			pushMoves(plan, a, b, half);
			pushMoves(plan, b, a, remainder - half);
		}

		return plan;
	}

} // namespace RebalanceDemo