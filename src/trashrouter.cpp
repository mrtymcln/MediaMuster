#include "trashrouter.h"

#include "avidlayout.h"
#include "oprunner.h" // OpSink, OpRunner::generateRenamePath

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUuid>

TrashRouter::TrashRouter(OpSink &sink)
	: m_sink(sink)
	// Test seams: force the per-volume fallback (the OS trash succeeds
	// on any local dev volume, so the branch is unreachable otherwise)
	// and re-root it inside the test sandbox (the real root is the
	// volume root, which tests must never write to). Never set in
	// production.
	, m_osDisabled(qEnvironmentVariableIsSet("MEDIAMUSTER_DISABLE_OS_TRASH"))
	, m_rootOverride(qEnvironmentVariable("MEDIAMUSTER_TRASH_ROOT"))
{
}

TrashRouter::Landing TrashRouter::trash(const QString &filePath)
{
	Landing out;

	bool useOsTrash = !m_osDisabled;
	// Resolved while the file still exists — after moveToTrash the path
	// is gone and QStorageInfo can't answer.
	QString volKey;
	if (useOsTrash)
	{
		volKey = QStorageInfo(filePath).rootPath();
		auto verdict = m_reportsByVolume.find(volKey);
		if (verdict == m_reportsByVolume.end())
			verdict = m_reportsByVolume.insert(volKey, osTrashReportsPath(filePath));
		if (!verdict.value())
		{
			useOsTrash = false;
			if (!m_pathlessWarned)
			{
				m_pathlessWarned = true;
				m_sink.log(QtWarningMsg,
						   QStringLiteral("The system trash on '%1' doesn't report where "
										  "files land, which would make these deletes "
										  "impossible to undo. Using the MediaMuster Trash "
										  "instead.")
							   .arg(volKey));
			}
		}
	}

	QString trashedPath;
	if (useOsTrash && QFile::moveToTrash(filePath, &trashedPath))
	{
		// Belt for the probe's braces: if a file still lands
		// addressless, remember it for this volume and say so — later
		// files reroute.
		if (trashedPath.isEmpty())
		{
			m_reportsByVolume[volKey] = false;
			if (!m_pathlessWarned)
			{
				m_pathlessWarned = true;
				m_sink.log(QtWarningMsg,
						   QStringLiteral("The system trash accepted '%1' without reporting "
										  "where it landed — that delete can't be undone "
										  "from here. Remaining files go to the MediaMuster "
										  "Trash instead.")
							   .arg(QFileInfo(filePath).fileName()));
			}
		}
		out.ok = true;
		out.finalPath = trashedPath;
		return out;
	}

	// Per-volume fallback: `_MediaMuster_Trash` folder at the volume
	// root, mirroring the source's path layout.
	QString volRoot = m_rootOverride;
	if (volRoot.isEmpty())
		volRoot = QStorageInfo(filePath).rootPath();

	if (volRoot.isEmpty())
	{
		out.error = QStringLiteral(
			"Couldn't find which volume this lives on, so it's been left alone.");
		return out;
	}

	// QDir::filePath joins without doubling a separator whether or not
	// the volume root already ends in one ("C:/" and "/Volumes/EDIT"
	// alike).
	const QDir volDir(volRoot);
	const QString relPath = volDir.relativeFilePath(filePath);
	const QString binRoot = volDir.filePath(AvidLayout::kMediaMusterTrashDir);
	const QString binDest = binRoot + QLatin1Char('/') + relPath;

	if (!QDir().mkpath(QFileInfo(binDest).absolutePath()))
	{
		out.error = QStringLiteral("Couldn't create the MediaMuster Trash. File left alone — "
								   "check your write permissions.");
		return out;
	}

	// A prior catch at this path is an earlier operation's safety copy;
	// never destroy it to make room. Divert the new arrival to a
	// " (2)"-style sibling and ledger wherever it actually lands.
	QString finalDest = binDest;
	if (QFile::exists(finalDest))
	{
		const auto renamed = OpRunner::generateRenamePath(finalDest);
		if (!renamed)
		{
			out.error = QStringLiteral(
				"The MediaMuster Trash already holds 999 copies of this file. File left in "
				"place — empty the trash and try again.");
			return out;
		}
		finalDest = *renamed;
	}

	if (!QFile::rename(filePath, finalDest))
	{
		out.error = QStringLiteral("Couldn't move to MediaMuster Trash. File left in place — "
								   "it may be open elsewhere.");
		return out;
	}

	out.ok = true;
	out.finalPath = finalDest;
	out.usedMediaMusterTrash = true;
	++m_mmCount;
	m_mmFolder = binRoot;
	return out;
}

// Probes whether the OS trash on this volume reports the trashed
// location. That location is what undo and crash recovery use to put a
// file back, so a trash that won't name one is a trash the router must
// avoid. A scratch file pays for the answer so no user file has to —
// written at the VOLUME ROOT when we may (out of the user's media
// folders; the v1 probe scratched right next to the MXF files), falling
// back beside the file only when the root is read-only for us. On a
// pathless volume the scratch stays in the OS trash — its address is
// exactly what we don't have — a few bytes of tmp litter in a bin the
// user empties.
bool TrashRouter::osTrashReportsPath(const QString &sampleFilePath)
{
	const QString scratchName = QStringLiteral(".mm_trashprobe_") +
								QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) +
								QStringLiteral(".tmp");
	QStringList candidates;
	const QString volRoot = QStorageInfo(sampleFilePath).rootPath();
	if (!volRoot.isEmpty())
		candidates << QDir(volRoot).filePath(scratchName);
	candidates << QFileInfo(sampleFilePath).absolutePath() + QLatin1Char('/') + scratchName;

	QString probePath;
	for (const QString &candidate : candidates)
	{
		QFile probe(candidate);
		if (probe.open(QIODevice::WriteOnly))
		{
			probe.write("probe", 5);
			probePath = candidate;
			break;
		}
	}
	if (probePath.isEmpty())
		return true; // can't probe here; keep the old behaviour

	QString where;
	if (!QFile::moveToTrash(probePath, &where))
	{
		QFile::remove(probePath);
		return true; // OS trash refuses outright; the per-file fallback covers that
	}
	if (where.isEmpty())
		return false;
	QFile::remove(where); // tidy our scratch out of the trash
	return true;
}
