#pragma once

#include "mediafile.h"
#include <QObject>
#include <QTimer>

class DriveManager : public QObject
{
    Q_OBJECT
public:
    explicit DriveManager(QObject *parent = nullptr);

    QVector<DriveInfo> detectDrives() const;
    QStringList allScannablePaths() const;

    void startMonitoring(int intervalMs = 5000);
    void stopMonitoring();

    static bool hasFullDiskAccess();
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
    static QStringList knownAvidLocations();
};