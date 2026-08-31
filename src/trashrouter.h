#pragma once

#include <QHash>
#include <QString>

class OpSink;

// MARK: - TrashRouter
//
// Marty's delete rule, as one reusable mechanism: PREFER THE OS TRASH;
// if it is full, inaccessible for any reason, or does not exist
// (network/Nexis volumes) — or accepts a file without saying where it
// landed, which would make undo and recovery dead-end — use our own
// `_MediaMuster_Trash` at the volume root instead.
//
// Shared by Delete (every file), by Replace's disposal of parked
// originals, and by Undo (a copied file being un-copied goes to the
// trash, never a hard delete) — so "no hard delete of user media" is
// enforced by construction: this class is the only exit.
class TrashRouter
{
public:
	/// `sink` receives the once-per-run warnings (a volume whose trash
	/// won't report addresses).
	explicit TrashRouter(OpSink &sink);

	struct Landing
	{
		bool ok = false;
		QString finalPath; ///< Where the file now lives; journaled so
						   ///< undo and recovery can bring it back.
		bool usedMediaMusterTrash = false;
		QString error; ///< User-facing sentence when !ok.
	};

	Landing trash(const QString &filePath);

	/// How many files this run routed into a `_MediaMuster_Trash`, and
	/// where — for the post-op "Take Me There" dialog.
	int mediaMusterCount() const { return m_mmCount; }
	QString mediaMusterFolder() const { return m_mmFolder; }

private:
	bool osTrashReportsPath(const QString &sampleFilePath);

	OpSink &m_sink;
	bool m_osDisabled;
	QString m_rootOverride;
	QHash<QString, bool> m_reportsByVolume;
	bool m_pathlessWarned = false;
	int m_mmCount = 0;
	QString m_mmFolder;
};
