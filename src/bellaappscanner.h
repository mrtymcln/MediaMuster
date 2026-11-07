#pragma once

#include "bella/bellascanner.h"
#include "mediafile.h"

#include <QObject>
#include <QPointer>
#include <memory>

class QThread;

struct LogMessage
{
	QtMsgType level = QtInfoMsg;
	QString module;
	QString message;
};

// App-side owner-thread connection to Bella's synchronous organiser.
class BellaAppScanner : public QObject
{
	Q_OBJECT
public:
	using Location = Bella::ScanLocation;
	struct Options
	{
		QVector<Location> locations;
		bool includeOmf = false;
		bool includePrecomputeDetails = false;
	};

	explicit BellaAppScanner(QObject *parent = nullptr);
	~BellaAppScanner() override;
	static std::optional<Location> addedLocation(const QString &path);
	static Location rescanLocation(const QString &path);

	void startScan(const Options &options);
	bool cancelScan();
	bool isActive() const noexcept;

signals:
	void scanDiscovering(const QString &path);
	void scanProgress(const QString &stage, qsizetype completed, qsizetype total, const QString &path);
	void scanFinalising();
	void scanLogBatch(const QVector<LogMessage> &batch);
	void scanFinished(const QVector<MediaFile> &rows, bool cancelled, bool discoveryComplete);
	void scanFailed(const QString &message);

private:
	struct Run;
	static void finishWhenReady(QThread *worker, const QPointer<BellaAppScanner> &owner);
	void publishCompletion();
	void publishFailure(const QString &message);
	void publishLog(const QVector<LogMessage> &batch);

	std::shared_ptr<Run> m_run;
	QThread *m_worker = nullptr; // Self-deletes on its owner thread after readiness.
	bool m_publishing = false;
};
