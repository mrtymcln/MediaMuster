#pragma once

// Background/UI boundary for the MediaEngine scan engine. A fresh session is owned
// by its returned rows, so later scans cannot borrow stale parser caches.
#include "backgroundjob.h"
#include "mediafile.h"
#include <QObject>
#include <atomic>

struct LogMessage
{
	QtMsgType level = QtInfoMsg;
	QString module;
	QString message;
};

class MediaScanner : public QObject
{
	Q_OBJECT
public:
	struct Options
	{
		QStringList volumePaths;
		QStringList manualPaths;
		bool includeOmf = false;
	};
	explicit MediaScanner(QObject *parent = nullptr);
	~MediaScanner() override { m_job.shutdown(); }
	static bool canScanPath(const QString &path);
	void startScan(const Options &options);
	void cancelScan();
signals:
	void scanDiscovering(const QString &path);
	void scanProgress(int current, int total, const QString &currentPath);
	void scanFinalising();
	void scanLogBatch(const QVector<LogMessage> &batch);
	void scanFinished(const QVector<MediaFile> &results);
	void scanFailed(const QString &message);
	void scanIssuesFinished(const QVector<ScanIssue> &issues);

private:
	struct ScanCompletion
	{
		QVector<MediaFile> rows;
		QVector<ScanIssue> issues;
	};
	ScanCompletion doScan();
	void reportFailure(const QString &message);
	BackgroundJob m_job;
	std::atomic_bool m_running{false};
	Options m_options;
};
