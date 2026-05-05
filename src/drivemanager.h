#pragma once

#include "mediafile.h"
#include <QObject>
#include <QTimer>

// DriveManager — detects volumes, monitors changes, checks permissions

class DriveManager : public QObject
{
    Q_OBJECT
public:
    explicit DriveManager(QObject *parent = nullptr);

    // Detect all mounted volumes + known Avid locations
    QVector<DriveInfo> detectDrives() const;

    // Collect ALL scannable paths: every mounted volume + known Avid locations
    QStringList allScannablePaths() const;

    // Monitor for volume mount/unmount
    void startMonitoring(int intervalMs = 5000);
    void stopMonitoring();

    // ---- macos permissions ----
    // Check if the app has Full Disk Access (macOS 10.14+)
    static bool hasFullDiskAccess();

    // Open System Preferences to the Full Disk Access pane
    static void openFullDiskAccessSettings();

signals:
    void drivesChanged(const QVector<DriveInfo> &drives);

private slots:
    void pollDrives();

private:
    QTimer m_timer;
    QVector<DriveInfo> m_lastDrives;

    static QString detectDriveType(const QString &name, const QString &path);
    static bool hasAvidMediaFolder(const QString &path);

    // Known Avid Media Composer locations per platform
    static QStringList knownAvidLocations();
};