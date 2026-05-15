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
#include <optional>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif

static constexpr qint64 COPY_BUFFER_SIZE = 4 * 1024 * 1024; // 4 MB chunks

namespace
{
    struct XxhStream
    {
        XXH3_state_t *s = XXH3_createState();
        XxhStream() { if (s) XXH3_64bits_reset(s); }
        ~XxhStream() { if (s) XXH3_freeState(s); }
        // Rule of Five: copies are deleted (XXH3 state isn't copyable), so
        // moves would be implicitly deleted too — declare them so intent is
        // explicit instead of relying on the compiler's silent inference.
        XxhStream(const XxhStream &) = delete;
        XxhStream &operator=(const XxhStream &) = delete;
        XxhStream(XxhStream &&) = delete;
        XxhStream &operator=(XxhStream &&) = delete;
        void update(const void *data, size_t n) { XXH3_64bits_update(s, data, n); }
        quint64 digest() const { return XXH3_64bits_digest(s); }
        bool ok() const { return s != nullptr; }
    };

    // APFS fast path for copy operation. Returns true if succeeded —
    // caller should treat this as completed, and skip both the byte loop
    // and the verify pass.
    bool tryCloneFile(const QString &src, const QString &dst)
    {
#ifdef __APPLE__
        // encodeName guarantees UTF-8 bytes regardless of LC_CTYPE; clonefile
        // (and APFS/HFS+ in general) expects UTF-8, and toLocal8Bit can produce
        // non-UTF-8 when the locale is C or POSIX.
        return clonefile(QFile::encodeName(src).constData(),
                         QFile::encodeName(dst).constData(),
                         0) == 0;
#else
        Q_UNUSED(src);
        Q_UNUSED(dst);
        return false;
#endif
    }

    // Tail-end log line for Copy/Move/Delete. Skipped count is suppressed when 0.
    QString formatOperationSummary(const QString &verb, int succeeded, int failed, int skipped = 0)
    {
        QString s = QString("%1 complete: %2 succeeded, %3 failed")
                        .arg(verb).arg(succeeded).arg(failed);
        if (skipped > 0)
            s += QString(", %1 skipped").arg(skipped);
        return s;
    }
}

MediaManager::MediaManager(QObject *parent) : QObject(parent) {}

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
        // arg(value, width, base, fillChar) is the type-safe Qt 6 function for
        // zero-padded integers; QString::asprintf is the C sprintf
        // holdover we used to reach for in Qt 5.
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

MediaManager::ConflictAction MediaManager::resolveConflict(
    const MediaFile &mf, QString &dstPath, const QHash<QString, int> &policies)
{
    if (!QFile::exists(dstPath))
        return ConflictAction::Proceed;

    const int policy = policies.value(mf.filePath, +ConflictPolicy::Replace);

    if (policy == +ConflictPolicy::Skip)
    {
        emit operationItemDone(mf.fileName, true, "Skipped (already exists)", true);
        return ConflictAction::Skip;
    }

    if (policy == +ConflictPolicy::KeepBoth)
    {
        const QString renamed = generateRenamePath(dstPath);
        if (renamed.isEmpty())
        {
            emit operationItemDone(mf.fileName, false,
                                   tr("There are already 999 copies! Did somebody mean to delete some of these?"));
            return ConflictAction::Fail;
        }
        dstPath = renamed;
        emit operationLog(0, QString("Renaming to %1").arg(QFileInfo(renamed).fileName()));
    }
    // Replace falls through — copyFileWithProgress will truncate dst, and
    // doMove will clear the slot itself before calling QFile::rename.

    return ConflictAction::Proceed;
}

bool MediaManager::copyFileWithProgress(const QString &src, const QString &dst,
                                        const QString &name, int current, int total)
{
    QFile srcFile(src);
    if (!srcFile.open(QIODevice::ReadOnly))
    {
        emit operationItemDone(name, false,
                               tr("Couldn't read the source file. The system said: %1").arg(srcFile.errorString()));
        return false;
    }

    QFileInfo dstInfo(dst);
    QDir().mkpath(dstInfo.absolutePath());

    // Clonefile won't overwrite an existing dst; remove it first so Replace policies can also use the fast path.
    if (QFile::exists(dst))
        QFile::remove(dst);

    // On success the clone shares blocks with the source, so we skip both the byte loop and the verify pass.
    if (tryCloneFile(src, dst))
    {
        emit operationProgress(name, current, total, 100.0);
        emit operationLog(0, tr("Cloned %1").arg(name));
        return true;
    }

    // NewOnly closes the TOCTOU window between the QFile::remove above and
    // this open: if another process raced in and created dst, we fail loud
    // instead of silently truncating whatever appeared.
    QFile dstFile(dst);
    if (!dstFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        emit operationItemDone(name, false,
                               tr("Couldn't create the destination file. The system said: %1").arg(dstFile.errorString()));
        return false;
    }

    const qint64 totalSize = srcFile.size();
    qint64 copied = 0;
    QByteArray buffer;
    buffer.resize(COPY_BUFFER_SIZE);

    // Hashed during the existing read pass; only fed when verify is on.
    const bool verify = MediaManagerVerify::enabled();
    XxhStream srcHash;
    if (verify && !srcHash.ok())
    {
        emit operationItemDone(name, false, tr("Couldn't initialise verification."));
        return false;
    }

    // Time-gated progress emits via ProgressThrottle, plus a 32 MB byte
    // threshold so very large copies still tick visibly even when the time
    // gate is rate-limiting the UI.
    constexpr qint64 kProgressIntervalBytes = 32 * 1024 * 1024;
    ProgressThrottle throttle;
    qint64 lastEmitBytes = 0;

    while (!srcFile.atEnd() && !m_job.isCancelled())
    {
        const qint64 bytesRead = srcFile.read(buffer.data(), COPY_BUFFER_SIZE);
        // QIODevice::read returns -1 on error and 0 on EOF; conflating the two
        // hides a truncated copy because the verify pass would still succeed.
        if (bytesRead < 0)
        {
            emit operationItemDone(name, false,
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
            emit operationItemDone(name, false,
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

    // Detect file size changes during the copy operation.
    // Shrinking, growing, moving, or deleting from another machine
    // truncates the source under us. The destination isn't a
    // coherent snapshot of the file we started on — the verify pass
    // can't catch it because srcHash only saw the bytes we read.
    const qint64 srcSizeAfter = QFileInfo(src).size();
    if (copied != totalSize || srcSizeAfter != totalSize)
    {
        emit operationItemDone(name, false,
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
            emit operationItemDone(name, false,
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
    buf.resize(COPY_BUFFER_SIZE);

    while (!f.atEnd() && !m_job.isCancelled())
    {
        const qint64 n = f.read(buf.data(), COPY_BUFFER_SIZE);
        if (n <= 0)
            break;
        h.update(buf.constData(), n);
    }

    if (m_job.isCancelled())
        return std::nullopt;
    return h.digest();
}

void MediaManager::executeCopy(const QVector<MediaFile> &files, const QString &destRoot,
                               bool preserveStructure,
                               const QHash<QString, int> &conflictPolicies)
{
    m_job.start([this, files, destRoot, preserveStructure, conflictPolicies]
                { doCopy(files, destRoot, preserveStructure, conflictPolicies); });
}

void MediaManager::doCopy(const QVector<MediaFile> &files, const QString &dest,
                          bool preserve, const QHash<QString, int> &policies)
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
            emit operationItemDone(mf.fileName, true, {});
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

void MediaManager::executeMove(const QVector<MediaFile> &files, const QString &destRoot,
                               bool preserveStructure,
                               const QHash<QString, int> &conflictPolicies)
{
    m_job.start([this, files, destRoot, preserveStructure, conflictPolicies]
                { doMove(files, destRoot, preserveStructure, conflictPolicies); });
}

void MediaManager::doMove(const QVector<MediaFile> &files, const QString &dest,
                          bool preserve, const QHash<QString, int> &policies)
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

        // Replace-policy on Move: QFile::rename won't overwrite, so clear the slot.
        // (KeepBoth has redirected dstPath to a non-existent path; no-op here.)
        if (QFile::exists(dstPath))
            QFile::remove(dstPath);

        QDir().mkpath(QFileInfo(dstPath).absolutePath());

        // Same-volume rename is the fast path.
        if (QFile::rename(mf.filePath, dstPath))
        {
            emit operationItemDone(mf.fileName, true, {});
            ++succeeded;
            continue;
        }

        // Cross-volume: copy then delete source. Roll back dest if source delete fails.
        if (copyFileWithProgress(mf.filePath, dstPath, mf.fileName, i + 1, total))
        {
            if (QFile::remove(mf.filePath))
            {
                emit operationItemDone(mf.fileName, true, {});
                ++succeeded;
            }
            else
            {
                QFile::remove(dstPath);
                emit operationItemDone(mf.fileName, false,
                                       tr("The copy worked but the original couldn't be removed, so we rolled back. The file's still safe at its source."));
                ++failed;
            }
        }
        else
        {
            ++failed;
        }

        DebugSlowdown::pauseForMs(40);
    }

    emit operationLog(failed > 0 ? 1 : 3,
                      formatOperationSummary("Move", succeeded, failed, skipped));
    emit operationFinished(succeeded, failed);
}

void MediaManager::executeDelete(const QVector<MediaFile> &files)
{
    m_job.start([this, files] { doDelete(files); });
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
            emit operationItemDone(mf.fileName, true, {});
            ++succeeded;
            continue;
        }

        // Network volumes have no OS trash; fall back to a per-volume
        // _MediaMuster_Trash that mirrors the source path.
        const QStorageInfo vol(mf.filePath);
        const QString volRoot = vol.rootPath();

        if (volRoot.isEmpty())
        {
            emit operationItemDone(mf.fileName, false,
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
            emit operationItemDone(mf.fileName, false,
                                   tr("Couldn't create the MediaMuster Trash. File left alone — check your write permissions."));
            ++failed;
            continue;
        }

        // Clear any prior trashed copy with the same name so rename succeeds.
        if (QFile::exists(binDest))
            QFile::remove(binDest);

        if (QFile::rename(mf.filePath, binDest))
        {
            emit operationItemDone(mf.fileName, true, {});
            ++succeeded;
            ++trashedCount;
            trashFolder = binRoot;
        }
        else
        {
            emit operationItemDone(mf.fileName, false,
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