#pragma once

#include "backgroundjob.h"
#include "mediafile.h"
#include <QHash>
#include <QObject>
#include <optional>

class MediaManager : public QObject
{
    Q_OBJECT
public:
    // Per-file conflict policy for Copy/Move operations. The dialog
    // exposes this via ManageMediaDialog::ConflictPolicy alias.
    enum class ConflictPolicy
    {
        KeepBoth = 0,
        Skip = 1,
        Replace = 2
    };

    friend constexpr int operator+(ConflictPolicy p) noexcept { return static_cast<int>(p); }

    explicit MediaManager(QObject *parent = nullptr);
    ~MediaManager() override = default; // m_job handles cancel + join

    // conflictPolicies: per-file policy keyed by source file path.
    // Files not in the map default to Replace.
    void executeCopy(const QVector<MediaFile> &files, const QString &destRoot,
                     bool preserveStructure = true,
                     const QHash<QString, int> &conflictPolicies = {});
    void executeMove(const QVector<MediaFile> &files, const QString &destRoot,
                     bool preserveStructure = true,
                     const QHash<QString, int> &conflictPolicies = {});
    void executeDelete(const QVector<MediaFile> &files);
    void cancel() { m_job.cancel(); }

signals:
    void operationProgress(const QString &fileName, int current, int total, double pct);
    void operationItemDone(const QString &fileName, bool success, const QString &error,
                           bool skipped = false);
    void operationFinished(int succeeded, int failed);
    void operationLog(int level, const QString &message);

    // Emitted after a delete operation if any files were moved to the
    // MediaMuster Trash on a network volume.
    void mediaMusterTrashUsed(const QString &trashFolderPath, int fileCount);

public:
    // Appends ".Copy.NN" to end of filename, incrementing until free.
    static QString generateRenamePath(const QString &destPath);

    // Preserve=true mirrors the Avid MediaFiles/MXF/<n>/<filename> structure;
    // Preserve=false flattens the structure.
    static QString buildDestPath(const MediaFile &mf, const QString &destRoot,
                                 bool preserve);

private:
    enum class ConflictAction
    {
        Proceed, // no conflict, or Replace policy — caller continues with copy/move
        Skip,    // user picked Skip; helper has already emitted operationItemDone
        Fail     // helper exhausted .Copy.NN slots and emitted the failure
    };

    void doCopy(const QVector<MediaFile> &files, const QString &dest,
                bool preserve, const QHash<QString, int> &policies);
    void doMove(const QVector<MediaFile> &files, const QString &dest,
                bool preserve, const QHash<QString, int> &policies);
    void doDelete(const QVector<MediaFile> &files);

    // Inspect the per-file policy for an existing destination; mutates dstPath
    // to the renamed path on KeepBoth. Emits operationItemDone for Skip/fail.
    ConflictAction resolveConflict(const MediaFile &mf, QString &dstPath,
                                   const QHash<QString, int> &policies);

    bool copyFileWithProgress(const QString &src, const QString &dst, const QString &name,
                              int current, int total);

    // Returns nullopt on open error or cancel.
    std::optional<quint64> hashFile(const QString &path);

    BackgroundJob m_job{this};
};
