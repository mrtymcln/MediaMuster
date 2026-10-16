#pragma once

// Background/UI boundary for the MediaEngine scan engine. Only prepared compact
// rows cross the result handoff; temporary scan owners stay in the worker.
#include "backgroundjob.h"
#include "mediafile.h"
#include <QObject>
#include <functional>
#include <mutex>

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
	/// False once the owning thread has accepted the result for publication.
	bool cancelScan();
signals:
	void scanDiscovering(const QString &path);
	void scanProgress(int current, int total, const QString &currentPath);
	void scanFinalising();
	void scanLogBatch(const QVector<LogMessage> &batch);
	void scanFinished(const QVector<MediaFile> &results);
	void scanFailed(const QString &message);
	void scanIssuesFinished(const QVector<ScanIssue> &issues);

private:
	friend class TestScanner;
	enum class ScanState
	{
		Idle,
		Active,
		CancelRequested,
		Publishing
	};
	enum class Checkpoint
	{
		FormattingRow,
		Prepared,
		BeforePublication
	};
	struct ScanCompletion
	{
		QVector<MediaFile> rows;
		QVector<ScanIssue> issues;
		bool cancelled = false;
		qint64 elapsedMs = 0;
	};
	ScanCompletion doScan();
	void publishCompletion(ScanCompletion completion);
	void publishFailure(const QString &message);
	void reportFailure(const QString &message);
	void checkpoint(Checkpoint phase, qsizetype row = 0);
	BackgroundJob m_job;
	std::mutex m_stateMutex;
	ScanState m_state = ScanState::Idle;
	// Assigned before starting by deterministic boundary tests; empty in the app.
	std::function<void(Checkpoint, qsizetype)> m_testCheckpoint;
	Options m_options;
};
