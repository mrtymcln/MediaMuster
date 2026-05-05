#include "mediamanager.h"
#include "debugslowdown.h"
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

static constexpr qint64 COPY_BUFFER_SIZE = 4 * 1024 * 1024; // 4 MB chunks

MediaManager::MediaManager(QObject *parent) : QObject(parent) {}

MediaManager::~MediaManager()
{
    if (m_thread)
    {
        cancel();
        m_thread->quit();
        if (!m_thread->wait(5000))
        {
            // Last-resort terminate.
            qWarning("FileOperations: worker did not quit within 5s; terminating");
            m_thread->terminate();
            m_thread->wait(1000);
        }
        // Thread self-deletes via QThread::finished → deleteLater; don't delete here.
    }
}

void MediaManager::cancel()
{
    m_cancel.store(true, std::memory_order_relaxed);
}

QString MediaManager::buildDestPath(const MediaFile &mf, const QString &destRoot,
                                      bool preserve)
{
    if (preserve)
    {
        QString subPath = QString("Avid MediaFiles/MXF/%1/%2")
                              .arg(mf.mxfFolder)
                              .arg(mf.fileName);
        return destRoot + "/" + subPath;
    }
    else
    {
        return destRoot + "/" + mf.fileName;
    }
}

// Inserts .Copy.NN before the extension, incrementing until free.
// V01.E5FD948F_…AV.mxf → V01.E5FD948F_…AV.Copy.01.mxf → .Copy.02.mxf …
// Returns empty if 999 copies exhausted.
QString MediaManager::generateRenamePath(const QString &destPath)
{
    const QFileInfo fi(destPath);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName(); // everything before last '.'
    const QString ext  = fi.suffix();           // extension without the dot

    for (int n = 1; n <= 999; ++n)
    {
        QString candidate;
        if (ext.isEmpty())
            candidate = QString("%1/%2.Copy.%3")
                            .arg(dir, base)
                            .arg(n, 2, 10, QChar('0'));
        else
            candidate = QString("%1/%2.Copy.%3.%4")
                            .arg(dir, base)
                            .arg(n, 2, 10, QChar('0'))
                            .arg(ext);

        if (!QFile::exists(candidate))
            return candidate;
    }
    return {}; // extremely unlikely — 999 copies of the same file
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

    QFile dstFile(dst);
    if (!dstFile.open(QIODevice::WriteOnly))
    {
        emit operationItemDone(name, false,
            tr("Couldn't create the destination file. The system said: %1").arg(dstFile.errorString()));
        return false;
    }

    qint64 totalSize = srcFile.size();
    qint64 copied = 0;
    QByteArray buffer;
    buffer.resize(COPY_BUFFER_SIZE);

    // Throttle progress to 30 Hz or 32 MB; force final emit at 100%.
    constexpr qint64 kProgressIntervalMs = 33;
    constexpr qint64 kProgressIntervalBytes = 32 * 1024 * 1024;
    QElapsedTimer progressTimer;
    progressTimer.start();
    qint64 lastEmitMs = 0;
    qint64 lastEmitBytes = 0;

    while (!srcFile.atEnd() && !m_cancel.load(std::memory_order_relaxed))
    {
        qint64 bytesRead = srcFile.read(buffer.data(), COPY_BUFFER_SIZE);
        if (bytesRead <= 0)
            break;

        qint64 bytesWritten = dstFile.write(buffer.data(), bytesRead);
        if (bytesWritten != bytesRead)
        {
            emit operationItemDone(name, false,
                tr("Write failed partway through. The system said: %1").arg(dstFile.errorString()));
            dstFile.close();
            QFile::remove(dst);
            return false;
        }
        copied += bytesWritten;

        // Slow-mode chunk pause; no-op otherwise.
        DebugSlowdown::pauseForMs(5);

        const qint64 nowMs = progressTimer.elapsed();
        const bool atEnd = srcFile.atEnd();
        if (atEnd ||
            (nowMs - lastEmitMs) >= kProgressIntervalMs ||
            (copied - lastEmitBytes) >= kProgressIntervalBytes)
        {
            const double pct = totalSize > 0 ? (100.0 * copied / totalSize) : 100.0;
            emit operationProgress(name, current, total, pct);
            lastEmitMs = nowMs;
            lastEmitBytes = copied;
        }
    }

    srcFile.close();
    dstFile.close();

    if (m_cancel.load(std::memory_order_relaxed))
    {
        QFile::remove(dst);
        return false;
    }

    return true;
}

void MediaManager::executeCopy(const QVector<MediaFile> &files, const QString &destRoot,
                                 bool preserveStructure,
                                 const QHash<QString, int> &conflictPolicies)
{
    // Wait for any in-progress operation to finish.
    if (m_thread && m_thread->isRunning())
    {
        cancel();
        m_thread->wait(5000);
    }
    m_cancel.store(false, std::memory_order_relaxed);
    m_thread = QThread::create([this, files, destRoot, preserveStructure, conflictPolicies]()
                               { doCopy(files, destRoot, preserveStructure, conflictPolicies); });
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]()
            { m_thread = nullptr; });
    m_thread->start();
}

void MediaManager::doCopy(const QVector<MediaFile> &files, const QString &dest,
                            bool preserve, const QHash<QString, int> &policies)
{
    int succeeded = 0, failed = 0, skipped = 0;
    int total = static_cast<int>(files.size());

    emit operationLog(0, QString("Copying %1 files to %2").arg(total).arg(dest));

    for (int i = 0; i < total && !m_cancel.load(std::memory_order_relaxed); ++i)
    {
        const MediaFile &mf = files[i];
        QString dstPath = buildDestPath(mf, dest, preserve);

        emit operationProgress(mf.fileName, i + 1, total, 0);

        if (QFile::exists(dstPath))
        {
            const int policy = policies.value(mf.filePath, +ConflictPolicy::Overwrite);

            if (policy == +ConflictPolicy::Skip)
            {
                emit operationItemDone(mf.fileName, true, "Skipped (already exists)");
                skipped++;
                succeeded++;
                continue;
            }

            if (policy == +ConflictPolicy::Rename)
            {
                const QString renamed = generateRenamePath(dstPath);
                if (renamed.isEmpty())
                {
                    emit operationItemDone(mf.fileName, false,
                        tr("Couldn't find a free name — there are already 999 copies. Did somebody mean to delete some of these?"));
                    failed++;
                    continue;
                }
                dstPath = renamed;
                emit operationLog(0, QString("Renaming to %1").arg(QFileInfo(renamed).fileName()));
            }
            // Overwrite falls through; copyFileWithProgress overwrites.
        }

        if (copyFileWithProgress(mf.filePath, dstPath, mf.fileName, i + 1, total))
        {
            emit operationItemDone(mf.fileName, true, {});
            succeeded++;
        }
        else
        {
            failed++;
        }

        DebugSlowdown::pauseForMs(40);
    }

    QString summary = QString("Copy complete: %1 succeeded, %2 failed").arg(succeeded).arg(failed);
    if (skipped > 0)
        summary += QString(", %1 skipped").arg(skipped);
    emit operationLog(failed > 0 ? 1 : 3, summary);
    emit operationFinished(succeeded, failed);
}

void MediaManager::executeMove(const QVector<MediaFile> &files, const QString &destRoot,
                                 bool preserveStructure,
                                 const QHash<QString, int> &conflictPolicies)
{
    if (m_thread && m_thread->isRunning())
    {
        cancel();
        m_thread->wait(5000);
    }
    m_cancel.store(false, std::memory_order_relaxed);
    m_thread = QThread::create([this, files, destRoot, preserveStructure, conflictPolicies]()
                               { doMove(files, destRoot, preserveStructure, conflictPolicies); });
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]()
            { m_thread = nullptr; });
    m_thread->start();
}

void MediaManager::doMove(const QVector<MediaFile> &files, const QString &dest,
                            bool preserve, const QHash<QString, int> &policies)
{
    int succeeded = 0, failed = 0, skipped = 0;
    int total = static_cast<int>(files.size());

    emit operationLog(0, QString("Moving %1 files to %2").arg(total).arg(dest));

    for (int i = 0; i < total && !m_cancel.load(std::memory_order_relaxed); ++i)
    {
        const MediaFile &mf = files[i];
        QString dstPath = buildDestPath(mf, dest, preserve);

        emit operationProgress(mf.fileName, i + 1, total, 0);

        if (QFile::exists(dstPath))
        {
            const int policy = policies.value(mf.filePath, +ConflictPolicy::Overwrite);

            if (policy == +ConflictPolicy::Skip)
            {
                emit operationItemDone(mf.fileName, true, "Skipped (already exists)");
                skipped++;
                succeeded++;
                continue;
            }

            if (policy == +ConflictPolicy::Rename)
            {
                const QString renamed = generateRenamePath(dstPath);
                if (renamed.isEmpty())
                {
                    emit operationItemDone(mf.fileName, false,
                        tr("Couldn't find a free name — there are already 999 copies. Did somebody mean to delete some of these?"));
                    failed++;
                    continue;
                }
                dstPath = renamed;
                emit operationLog(0, QString("Renaming to %1").arg(QFileInfo(renamed).fileName()));
            }

            // Remove existing so rename can succeed.
            if (policy == +ConflictPolicy::Overwrite)
                QFile::remove(dstPath);
        }

        QDir().mkpath(QFileInfo(dstPath).absolutePath());

        // Fast same-volume rename first.
        if (QFile::rename(mf.filePath, dstPath))
        {
            emit operationItemDone(mf.fileName, true, {});
            succeeded++;
            continue;
        }

        // Cross-volume: copy then delete source.
        if (copyFileWithProgress(mf.filePath, dstPath, mf.fileName, i + 1, total))
        {
            if (QFile::remove(mf.filePath))
            {
                emit operationItemDone(mf.fileName, true, {});
                succeeded++;
            }
            else
            {
                // Source remove failed — roll back dest so source remains the only copy.
                QFile::remove(dstPath);
                emit operationItemDone(mf.fileName, false,
                    tr("The copy worked but the original couldn't be removed, so we rolled back. The file's still safe at its source."));
                failed++;
            }
        }
        else
        {
            failed++;
        }

        DebugSlowdown::pauseForMs(40);
    }

    QString summary = QString("Move complete: %1 succeeded, %2 failed").arg(succeeded).arg(failed);
    if (skipped > 0)
        summary += QString(", %1 skipped").arg(skipped);
    emit operationLog(failed > 0 ? 1 : 3, summary);
    emit operationFinished(succeeded, failed);
}

void MediaManager::executeDelete(const QVector<MediaFile> &files)
{
    if (m_thread && m_thread->isRunning())
    {
        cancel();
        m_thread->wait(5000);
    }
    m_cancel.store(false, std::memory_order_relaxed);
    m_thread = QThread::create([this, files]()
                               { doDelete(files); });
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]()
            { m_thread = nullptr; });
    m_thread->start();
}

void MediaManager::doDelete(const QVector<MediaFile> &files)
{
    int succeeded = 0, failed = 0;
    int total = static_cast<int>(files.size());
    int binChickenBinCount = 0;
    QString binChickenBinPath; // last-used bin folder (for the post-op dialog)

    emit operationLog(0, QString("Deleting %1 files").arg(total));

    for (int i = 0; i < total && !m_cancel.load(std::memory_order_relaxed); ++i)
    {
        const MediaFile &mf = files[i];
        emit operationProgress(mf.fileName, i + 1, total, 0);

        // attempt 1: OS recycle bin (fails on network/SMB/AFP/CIFS — no OS trash there).
        if (QFile::moveToTrash(mf.filePath))
        {
            emit operationItemDone(mf.fileName, true, {});
            succeeded++;
            continue;
        }

        // attempt 2: same-volume rename into _MediaMuster_Trash (mirrors source path
        // so restore is exact). Instant rename — zero network I/O even for huge MXF.
        //   //Nexis/Media/Avid MediaFiles/MXF/12/V01.mxf
        //   → //Nexis/Media/_MediaMuster_Trash/Avid MediaFiles/MXF/12/V01.mxf
        const QStorageInfo vol(mf.filePath);
        const QString volRoot = vol.rootPath();

        if (volRoot.isEmpty())
        {
            emit operationItemDone(mf.fileName, false,
                tr("Couldn't figure out which volume this lives on, so it's been left alone."));
            failed++;
            continue;
        }

        const QString relPath = QDir(volRoot).relativeFilePath(mf.filePath);
        const QString binRoot = volRoot +
            (volRoot.endsWith('/') || volRoot.endsWith('\\')
                 ? QStringLiteral("_MediaMuster_Trash")
                 : QStringLiteral("/_MediaMuster_Trash"));
        const QString binDest = binRoot + "/" + relPath;

        const QString binDestDir = QFileInfo(binDest).absolutePath();
        if (!QDir().mkpath(binDestDir))
        {
            emit operationItemDone(mf.fileName, false,
                tr("Couldn't create the MediaMuster Trash folder on this volume. File left alone — check your write permissions."));
            failed++;
            continue;
        }

        // Remove any prior trashed copy with the same name so rename succeeds.
        if (QFile::exists(binDest))
            QFile::remove(binDest);

        if (QFile::rename(mf.filePath, binDest))
        {
            emit operationItemDone(mf.fileName, true, {});
            succeeded++;
            binChickenBinCount++;
            binChickenBinPath = binRoot;
        }
        else
        {
            emit operationItemDone(mf.fileName, false,
                tr("Couldn't move this into the MediaMuster Trash. The file's still where it was — check whether anything else has it open."));
            failed++;
        }

        DebugSlowdown::pauseForMs(40);
    }

    emit operationLog(failed > 0 ? 1 : 3,
                      QString("Delete complete: %1 succeeded, %2 failed").arg(succeeded).arg(failed));

    if (binChickenBinCount > 0)
    {
        emit operationLog(0,
            QString("%1 file(s) moved to MediaMuster Trash at %2")
                .arg(binChickenBinCount)
                .arg(binChickenBinPath));
        emit networkBinUsed(binChickenBinPath, binChickenBinCount);
    }

    emit operationFinished(succeeded, failed);
}