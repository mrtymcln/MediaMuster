#pragma once

#include "scanner/bella_scanner.h"
#include "media/media_file_record.h"

#include <QObject>
#include <QPointer>
#include <memory>

class QThread;

// MARK: - Scan log messages

struct LogMessage
{
	QtMsgType level = QtInfoMsg;
	QString module;
	QString message;
};

// MARK: - Scan manager

// Runs Bella in the background and delivers inventory records on the GUI thread.
class ScanManager : public QObject
{
	Q_OBJECT
public:
	using Location = Bella::ScanLocation;

	// MARK: - Lifecycle

	explicit ScanManager(QObject *parent = nullptr);
	~ScanManager() override;

	// MARK: - Scan locations

	static std::optional<Location> addedLocation(const QString &path);
	static Location rescanLocation(const QString &path);

	// MARK: - Scan control

	void startScan(const Bella::ScanRequest &request);
	bool cancelScan();
	bool isActive() const noexcept;

	// MARK: - Scan signals

signals:
	void scanDiscovering(const QString &path);
	void scanProgress(const QString &stage, qsizetype completed, qsizetype total, const QString &path);
	void scanFinalising();
	void scanLogBatch(const QVector<LogMessage> &batch);
	void scanFinished(const QVector<MediaFileRecord> &rows, bool cancelled, bool discoveryComplete);
	void scanFailed(const QString &message);

private:
	// MARK: - Worker completion

	struct Run;
	static void finishWhenReady(QThread *worker, const QPointer<ScanManager> &owner);
	void publishCompletion();
	void publishFailure(const QString &message);
	void publishLog(const QVector<LogMessage> &batch);

	// MARK: - Run state

	std::shared_ptr<Run> m_run;
	QThread *m_worker = nullptr; // Self-deletes on its owner thread after readiness.
	bool m_publishing = false;
};
