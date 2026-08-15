#pragma once

#include "avidlayout.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUuid>

// MARK: - ProbeSweep

/// Finds media files stranded under a `.__rebalprobe_<uuid>` suffix by a
/// rebalance that died between its two pre-flight renames, and renames them
/// home. Avid ignores the suffixed name (it breaks format recognition), so a
/// stranded probe is a clip that has silently vanished from the project.
///
/// Shared by the rebalancer (pre-run sweep of its own root) and startup
/// recovery (sweep of an interrupted rebalance journal's root, read from the
/// journal's meta) so the two sides can't drift. Probes are also journalled
/// per-op now; this sweep is the belt to that brace, catching strandings from
/// older builds and from runs whose journal degraded mid-write.
namespace ProbeSweep
{
	// MARK: - Probe-name protocol

	/// The writer/reader contract for pre-flight probe files: the
	/// rebalancer names probes with makeProbeSuffix(), and the sweep
	/// below recognises them by the same marker + UUID shape. The marker
	/// itself lives in AvidLayout beside the park tags, because the
	/// scanner also needs to know these names are temp-renamed media.
	/// Files stranded by OLDER builds still carry this shape on disk,
	/// which is why the tests pin the raw literal as a tripwire.
	inline constexpr QLatin1String kProbeMarker = AvidLayout::kProbeMarker;

	/// Marker + 36-char dashed UUID — exactly the form the sweep accepts.
	inline QString makeProbeSuffix()
	{
		return kProbeMarker + QUuid::createUuid().toString(QUuid::WithoutBraces);
	}

	struct Result
	{
		QStringList restored; ///< Probe names renamed back to their originals.
		QStringList stuck;	  ///< Couldn't restore: original slot occupied, or rename failed.
	};

	inline Result recoverStranded(const QString &mxfRoot)
	{
		// Function-local static: built once, shared by both call sites.
		// Composed from kProbeMarker so writer and reader cannot drift.
		static const QRegularExpression probeRe(
			QStringLiteral("^(.*)") + QRegularExpression::escape(kProbeMarker) +
			QStringLiteral("[0-9a-f-]{36}$"));
		const QLatin1String marker = kProbeMarker;

		Result out;
		QDir root(mxfRoot);
		const QStringList subdirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		for (const QString &subdir : subdirs)
		{
			QDir folder(root.filePath(subdir));
			const QStringList files = folder.entryList(QDir::Files | QDir::NoDotAndDotDot);
			for (const QString &name : files)
			{
				// Cheap substring prefilter; the regex is several times more
				// expensive and almost no entries match on a healthy system.
				if (!name.contains(marker))
					continue;
				const auto m = probeRe.match(name);
				if (!m.hasMatch())
					continue;
				const QString restored = folder.filePath(m.captured(1));
				if (!QFile::exists(restored) && QFile::rename(folder.filePath(name), restored))
					out.restored << name;
				else
					out.stuck << name;
			}
		}
		return out;
	}
} // namespace ProbeSweep