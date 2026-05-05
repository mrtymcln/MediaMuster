#pragma once

#include "mediafile.h"
#include <QHash>
#include <QObject>
#include <QThread>
#include <atomic>

// MediaManager — performs copy/move/delete on media files//
// Runs on a background thread. Supports preserving the Avid MediaFiles
// folder structure during copy/move operations.

class MediaManager : public QObject
{
    Q_OBJECT
public:
    // Mirrors ManageMediaDialog::ConflictPolicy so the dialog's output
    // can flow through directly.
    enum class ConflictPolicy
    {
        Overwrite = 0,
        Skip = 1,
        Rename = 2
    };

    friend constexpr int operator+(ConflictPolicy p) noexcept { return static_cast<int>(p); }

    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager();

    // conflictPolicies: per-file policy keyed by source file path.
    // Files not in the map default to Overwrite (current behaviour).
    void executeCopy(const QVector<MediaFile> &files, const QString &destRoot,
                     bool preserveStructure = true,
                     const QHash<QString, int> &conflictPolicies = {});
    void executeMove(const QVector<MediaFile> &files, const QString &destRoot,
                     bool preserveStructure = true,
                     const QHash<QString, int> &conflictPolicies = {});
    void executeDelete(const QVector<MediaFile> &files);
    void cancel();

signals:
    void operationProgress(const QString &fileName, int current, int total, double pct);
    void operationItemDone(const QString &fileName, bool success, const QString &error);
    void operationFinished(int succeeded, int failed);
    void operationLog(int level, const QString &message);

    // Emitted after a delete operation if any files were moved to a
    // MediaMuster Trash folder on a network volume (because the OS Recycle
    // Bin doesn't support network drives). The GUI should show an info
    // dialog explaining what happened and offering to open the folder.
    void networkBinUsed(const QString &binFolderPath, int fileCount);

private:
    void doCopy(const QVector<MediaFile> &files, const QString &dest,
                bool preserve, const QHash<QString, int> &policies);
    void doMove(const QVector<MediaFile> &files, const QString &dest,
                bool preserve, const QHash<QString, int> &policies);
    void doDelete(const QVector<MediaFile> &files);

    bool copyFileWithProgress(const QString &src, const QString &dst, const QString &name,
                              int current, int total);
    QString buildDestPath(const MediaFile &mf, const QString &destRoot, bool preserve);

    // Generate a non-colliding rename path by inserting.Copy.01 before
    // the extension, incrementing to.Copy.02 etc: until a free name is
    // found. Returns empty string if 999 attempts exhausted.
    static QString generateRenamePath(const QString &destPath);

    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};
};