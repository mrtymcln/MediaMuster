#include "mediamanager.h"
#include "debugslowdown.h"
#include "formatutil.h"
#include "mediamanagerverify.h"
#include "progressthrottle.h"
#include "third_party/xxhash.h"
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUuid>
#include <optional>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif

// MARK: - Tunables

// 4 MB: largest cache window that helps on APFS/NTFS without
// ballooning RSS under parallel copies.
static constexpr qint64 kCopyBufferSize = 4 * 1024 * 1024;

// MARK: - Internal helpers

namespace
{
// RAII wrapper for xxHash3 streaming state. Hashes the source
// during the read pass, re-hashes the destination after copy.
// Mismatch means copy fails, destination removed.
struct XxhStream
{
	XXH3_state_t *s = XXH3_createState();
	XxhStream()
	{
		if (s)
			XXH3_64bits_reset(s);
	}
	~XxhStream()
	{
		if (s)
			XXH3_freeState(s);
	}

	// XXH3 state isn't copyable; moving would double-free.
	XxhStream(const XxhStream &) = delete;
	XxhStream &operator=(const XxhStream &) = delete;
	XxhStream(XxhStream &&) = delete;
	XxhStream &operator=(XxhStream &&) = delete;

	void update(const void *data, size_t n)
	{
		XXH3_64bits_update(s, data, n);
	}
	quint64 digest() const
	{
		return XXH3_64bits_digest(s);
	}
	bool ok() const
	{
		return s != nullptr;
	}
};

// APFS fast path: same volume clonefile makes a new inode sharing
// the source's blocks — effectively free. Caller skips the byte
// loop and verify pass on success.
bool tryCloneFile(const QString &src, const QString &dst)
{
#ifdef __APPLE__
	return clonefile(QFile::encodeName(src).constData(),
	                 QFile::encodeName(dst).constData(),
	                 0) == 0;
#else
	Q_UNUSED(src);
	Q_UNUSED(dst);
	return false;
#endif
}

// Skipped count is suppressed when 0 so Delete (no skip path)
// doesn't trail a `, 0 skipped`.
QString formatOperationSummary(const QString &verb, int succeeded, int failed, int skipped = 0)
{
	QString s = QString("%1 complete: %2 succeeded, %3 failed")
	                .arg(verb)
	                .arg(succeeded)
	                .arg(failed);
	if (skipped > 0)
		s += QString(", %1 skipped").arg(skipped);
	return s;
}
} // namespace

// MARK: - Construction

MediaManager::MediaManager(QObject *parent)
    : QObject(parent)
{
}

// MARK: - Path helpers

QString MediaManager::buildDestPath(const MediaFile &mf, const QString &destRoot,
                                    bool preserve)
{
	if (preserve)
		return destRoot + QStringLiteral("/Avid MediaFiles/MXF/") + mf.mxfFolder +
		       QLatin1Char('/') + mf.fileName;
	return destRoot + QLatin1Char('/') + mf.fileName;
}

QString MediaManager::generateRenamePath(const QString &destPath)
{
	const QFileInfo fi(destPath);
	const QString dir = fi.absolutePath();
	const QString base = fi.completeBaseName();
	const QString ext = fi.suffix();

	for (int n = 1; n <= 999; ++n)
	{
		const QString suffix =
		    QStringLiteral(".Copy.%1").arg(n, 2, 10, QLatin1Char('0'));
		const QString candidate = ext.isEmpty()
		                              ? dir + QLatin1Char('/') + base + suffix
		                              : dir + QLatin1Char('/') + base + suffix + QLatin1Char('.') + ext;
		if (!QFile::exists(candidate))
			return candidate;
	}
	return {};
}

// MARK: - Conflict resolution

MediaManager::ConflictAction MediaManager::resolveConflict(
    const MediaFile &mf, QString &dstPath, const QHash<QString, ConflictPolicy> &policies)
{
	if (!QFile::exists(dstPath))
		return ConflictAction::Proceed;

	const ConflictPolicy policy = policies.value(mf.filePath, ConflictPolicy::Replace);

	if (policy == ConflictPolicy::Skip)
	{
		emit operationItemDone(mf.fileName, mf.filePath, true,
		                       "Skipped (already exists)", true);
		return ConflictAction::Skip;
	}

	if (policy == ConflictPolicy::KeepBoth)
	{
		const QString renamed = generateRenamePath(dstPath);
		if (renamed.isEmpty())
		{
			emit operationItemDone(mf.fileName, mf.filePath, false,
			                       tr("There are already 999 copies! Did somebody mean to delete some of these?"));
			return ConflictAction::Fail;
		}
		dstPath = renamed;
		emit operationLog(0, QString("Renaming to %1").arg(QFileInfo(renamed).fileName()));
	}

	// Replace falls through — copyFileWithProgress truncates the
	// destination, and doMove clears the slot itself before calling
	// QFile::rename.
	return ConflictAction::Proceed;
}

// MARK: - Buffered copy + verify

bool MediaManager::copyFileWithProgress(const QString &src, const QString &dst,
                                        const QString &name, int current, int total)
{
	QFile srcFile(src);
	if (!srcFile.open(QIODevice::ReadOnly))
	{
		emit operationItemDone(name, src, false,
		                       tr("Couldn't read the source file. The system said: %1").arg(srcFile.errorString()));
		return false;
	}

	QFileInfo dstInfo(dst);
	QDir().mkpath(dstInfo.absolutePath());

	// clonefile refuses to overwrite. Remove the destination first
	// so Replace-policy copies can still take the fast path.
	if (QFile::exists(dst))
		QFile::remove(dst);

	// Cloned files share blocks with the source — bytes are already
	// correct, no read pass and no verify pass.
	if (tryCloneFile(src, dst))
	{
		emit operationProgress(name, current, total, 100.0);
		emit operationLog(0, tr("Cloned %1").arg(name));
		return true;
	}

	// NewOnly closes the TOCTOU window after the QFile::remove
	// above — fail loud if another process raced in to create `dst`.
	QFile dstFile(dst);
	if (!dstFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
	{
		emit operationItemDone(name, src, false,
		                       tr("Couldn't create the destination file. The system said: %1").arg(dstFile.errorString()));
		return false;
	}

	const qint64 totalSize = srcFile.size();
	qint64 copied = 0;
	QByteArray buffer;
	buffer.resize(kCopyBufferSize);

	// Hash during the existing read pass — no extra source-side
	// disk traffic when verify is enabled.
	const bool verify = MediaManagerVerify::enabled();
	XxhStream srcHash;
	if (verify && !srcHash.ok())
	{
		emit operationItemDone(name, src, false, tr("Couldn't initialise verification."));
		return false;
	}

	// ProgressThrottle (~30 Hz) plus a 32 MB byte threshold so large
	// single-file copies still tick visibly.
	constexpr qint64 kProgressIntervalBytes = 32 * 1024 * 1024;
	ProgressThrottle throttle;
	qint64 lastEmitBytes = 0;

	while (!srcFile.atEnd() && !m_job.isCancelled())
	{
		const qint64 bytesRead = srcFile.read(buffer.data(), kCopyBufferSize);

		// -1 (error) vs 0 (EOF): treating them the same hides a
		// truncated copy since srcHash only sees bytes we read.
		if (bytesRead < 0)
		{
			emit operationItemDone(name, src, false,
			                       tr("Read failed partway through. The system said: %1").arg(srcFile.errorString()));
			dstFile.close();
			QFile::remove(dst);
			return false;
		}
		if (bytesRead == 0)
			break;

		const qint64 bytesWritten = dstFile.write(buffer.data(), bytesRead);
		if (bytesWritten != bytesRead)
		{
			emit operationItemDone(name, src, false,
			                       tr("Write failed partway through. The system said: %1").arg(dstFile.errorString()));
			dstFile.close();
			QFile::remove(dst);
			return false;
		}
		copied += bytesWritten;

		if (verify)
			srcHash.update(buffer.constData(), bytesWritten);

		DebugSlowdown::pauseForMs(5);

		if (srcFile.atEnd() ||
		    throttle.shouldEmit() ||
		    (copied - lastEmitBytes) >= kProgressIntervalBytes)
		{
			const double pct = totalSize > 0 ? (100.0 * copied / totalSize) : 100.0;
			emit operationProgress(name, current, total, pct);
			lastEmitBytes = copied;
		}
	}

	srcFile.close();
	dstFile.close();

	if (m_job.isCancelled())
	{
		QFile::remove(dst);
		return false;
	}

	// Detect a source-size change mid-copy. A networked writer
	// shrinking/growing/moving the file breaks snapshot coherence
	// — verify can't catch it (srcHash only saw bytes we read).
	const qint64 srcSizeAfter = QFileInfo(src).size();
	if (copied != totalSize || srcSizeAfter != totalSize)
	{
		emit operationItemDone(name, src, false,
		                       tr("Source file changed during the copy "
		                          "(started at %1, read %2, now %3). "
		                          "Try again when the file is stable.")
		                           .arg(Format::bytes(totalSize),
		                                Format::bytes(copied),
		                                Format::bytes(srcSizeAfter)));
		QFile::remove(dst);
		return false;
	}

	if (verify)
	{
		emit operationLog(0, tr("Verifying %1").arg(name));
		const quint64 expected = srcHash.digest();
		const std::optional<quint64> actual = hashFile(dst);

		if (m_job.isCancelled())
		{
			QFile::remove(dst);
			return false;
		}

		if (!actual || *actual != expected)
		{
			emit operationItemDone(name, src, false,
			                       tr("Copy completed but failed verification. The destination file has been removed."));
			QFile::remove(dst);
			return false;
		}
	}
	return true;
}

std::optional<quint64> MediaManager::hashFile(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return std::nullopt;

	XxhStream h;
	if (!h.ok())
		return std::nullopt;

	QByteArray buf;
	buf.resize(kCopyBufferSize);

	while (!f.atEnd() && !m_job.isCancelled())
	{
		const qint64 n = f.read(buf.data(), kCopyBufferSize);
		if (n <= 0)
			break;
		h.update(buf.constData(), n);
	}

	if (m_job.isCancelled())
		return std::nullopt;
	return h.digest();
}

// MARK: - Copy job

void MediaManager::executeCopy(const QVector<MediaFile> &files, const QString &destRoot,
                               bool preserveStructure,
                               const QHash<QString, ConflictPolicy> &conflictPolicies)
{
	m_job.start([this, files, destRoot, preserveStructure, conflictPolicies]
	            { doCopy(files, destRoot, preserveStructure, conflictPolicies); });
}

void MediaManager::doCopy(const QVector<MediaFile> &files, const QString &dest,
                          bool preserve, const QHash<QString, ConflictPolicy> &policies)
{
	int succeeded = 0, failed = 0, skipped = 0;
	const int total = files.size();

	emit operationLog(0, QString("Copying %1 files to %2").arg(total).arg(dest));

	for (int i = 0; i < total && !m_job.isCancelled(); ++i)
	{
		const MediaFile &mf = files[i];
		QString dstPath = buildDestPath(mf, dest, preserve);

		emit operationProgress(mf.fileName, i + 1, total, 0);

		if (const auto action = resolveConflict(mf, dstPath, policies);
		    action != ConflictAction::Proceed)
		{
			(action == ConflictAction::Skip) ? ++skipped : ++failed;
			continue;
		}

		if (copyFileWithProgress(mf.filePath, dstPath, mf.fileName, i + 1, total))
		{
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
		}
		else
		{
			++failed;
		}

		DebugSlowdown::pauseForMs(40);
	}

	emit operationLog(failed > 0 ? 1 : 3,
	                  formatOperationSummary("Copy", succeeded, failed, skipped));
	emit operationFinished(succeeded, failed);
}

// MARK: - Move job

void MediaManager::executeMove(const QVector<MediaFile> &files, const QString &destRoot,
                               bool preserveStructure,
                               const QHash<QString, ConflictPolicy> &conflictPolicies)
{
	m_job.start([this, files, destRoot, preserveStructure, conflictPolicies]
	            { doMove(files, destRoot, preserveStructure, conflictPolicies); });
}

void MediaManager::doMove(const QVector<MediaFile> &files, const QString &dest,
                          bool preserve, const QHash<QString, ConflictPolicy> &policies)
{
	int succeeded = 0, failed = 0, skipped = 0;
	const int total = files.size();

	emit operationLog(0, QString("Moving %1 files to %2").arg(total).arg(dest));

	for (int i = 0; i < total && !m_job.isCancelled(); ++i)
	{
		const MediaFile &mf = files[i];
		QString dstPath = buildDestPath(mf, dest, preserve);

		emit operationProgress(mf.fileName, i + 1, total, 0);

		if (const auto action = resolveConflict(mf, dstPath, policies);
		    action != ConflictAction::Proceed)
		{
			(action == ConflictAction::Skip) ? ++skipped : ++failed;
			continue;
		}

		// Replace on Move: QFile::rename won't overwrite, so park the
		// existing destination first. Restore on failure; delete on
		// success. KeepBoth already redirected dstPath, so parkedDst
		// stays unset there.
		QString parkedDst;
		if (QFile::exists(dstPath))
		{
			parkedDst = dstPath + QStringLiteral(".__movereplace_") +
			            QUuid::createUuid().toString(QUuid::WithoutBraces);
			if (!QFile::rename(dstPath, parkedDst))
			{
				emit operationItemDone(mf.fileName, mf.filePath, false,
				                       tr("Couldn't move the existing destination aside before the move. Leaving everything where it is."));
				++failed;
				continue;
			}
		}

		QDir().mkpath(QFileInfo(dstPath).absolutePath());

		auto restoreParked = [&]()
		{
			if (parkedDst.isEmpty())
				return;
			QFile::remove(dstPath);            // kill any half-written copy
			QFile::rename(parkedDst, dstPath); // and put the original back
		};

		auto commitParked = [&]()
		{
			if (!parkedDst.isEmpty())
				QFile::remove(parkedDst);
		};

		// Same-volume rename: pure inode swap. QFile::rename returns
		// false on a filesystem-boundary crossing — fall back to
		// copy-then-delete.
		if (QFile::rename(mf.filePath, dstPath))
		{
			commitParked();
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
			continue;
		}

		// Cross-volume: copy then delete. Failure paths restore the
		// parked destination.
		if (copyFileWithProgress(mf.filePath, dstPath, mf.fileName, i + 1, total))
		{
			if (QFile::remove(mf.filePath))
			{
				commitParked();
				emit operationItemDone(mf.fileName, mf.filePath, true, {});
				++succeeded;
			}
			else
			{
				restoreParked();
				emit operationItemDone(mf.fileName, mf.filePath, false,
				                       tr("The copy worked but the original couldn't be removed, so we rolled back. The file's still safe at its source."));
				++failed;
			}
		}
		else
		{
			restoreParked();
			++failed;
		}

		DebugSlowdown::pauseForMs(40);
	}

	emit operationLog(failed > 0 ? 1 : 3,
	                  formatOperationSummary("Move", succeeded, failed, skipped));
	emit operationFinished(succeeded, failed);
}

// MARK: - Delete job

void MediaManager::executeDelete(const QVector<MediaFile> &files)
{
	m_job.start([this, files]
	            { doDelete(files); });
}

void MediaManager::doDelete(const QVector<MediaFile> &files)
{
	int succeeded = 0, failed = 0, trashedCount = 0;
	const int total = files.size();
	QString trashFolder;

	emit operationLog(0, QString("Deleting %1 files").arg(total));

	for (int i = 0; i < total && !m_job.isCancelled(); ++i)
	{
		const MediaFile &mf = files[i];
		emit operationProgress(mf.fileName, i + 1, total, 0);

		if (QFile::moveToTrash(mf.filePath))
		{
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
			continue;
		}

		// Per-volume fallback: `_MediaMuster_Trash` folder at the
		// volume root, mirroring the source's path layout.
		const QStorageInfo vol(mf.filePath);
		const QString volRoot = vol.rootPath();

		if (volRoot.isEmpty())
		{
			emit operationItemDone(mf.fileName, mf.filePath, false,
			                       tr("Couldn't figure out which volume this lives on, so it's been left alone."));
			++failed;
			continue;
		}

		const QString relPath = QDir(volRoot).relativeFilePath(mf.filePath);
		const QChar lastChar = volRoot.back();
		const QString binRoot = volRoot +
		                        (lastChar == QLatin1Char('/') || lastChar == QLatin1Char('\\')
		                             ? QStringLiteral("_MediaMuster_Trash")
		                             : QStringLiteral("/_MediaMuster_Trash"));
		const QString binDest = binRoot + QLatin1Char('/') + relPath;

		if (!QDir().mkpath(QFileInfo(binDest).absolutePath()))
		{
			emit operationItemDone(mf.fileName, mf.filePath, false,
			                       tr("Couldn't create the MediaMuster Trash. File left alone — check your write permissions."));
			++failed;
			continue;
		}

		// Clear any prior trashed copy so the rename below succeeds.
		if (QFile::exists(binDest))
			QFile::remove(binDest);

		if (QFile::rename(mf.filePath, binDest))
		{
			emit operationItemDone(mf.fileName, mf.filePath, true, {});
			++succeeded;
			++trashedCount;
			trashFolder = binRoot;
		}
		else
		{
			emit operationItemDone(mf.fileName, mf.filePath, false,
			                       tr("Couldn't move into the MediaMuster Trash. The file's still where it was — check if somebody else has it open."));
			++failed;
		}

		DebugSlowdown::pauseForMs(40);
	}

	emit operationLog(failed > 0 ? 1 : 3,
	                  formatOperationSummary("Delete", succeeded, failed));

	if (trashedCount > 0)
	{
		emit operationLog(0,
		                  QString("%1 file(s) moved to MediaMuster Trash at %2")
		                      .arg(trashedCount)
		                      .arg(trashFolder));
		emit mediaMusterTrashUsed(trashFolder, trashedCount);
	}

	emit operationFinished(succeeded, failed);
}