#pragma once

#include "backgroundjob.h"
#include "mediafile.h"
#include "rebalanceplan.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <optional>

class Rebalancer : public QObject
{
	Q_OBJECT
public:
	// Avid recommends < 5000 files per folder.
	static constexpr int CAP = 4999;

	explicit Rebalancer(QObject *parent = nullptr);
	~Rebalancer() override = default; // m_job handles cancel + join

	// Builds a plan; synchronous file I/O on the caller's thread.
	static RebalancePlan computePlan(const QString &mxfRoot,
									 const QString &driveLabel,
									 const QVector<MediaFile> &files);

	// Parses <prefix>.<int> folder names ("MartysiMac.1", "Edit1.88", "1").
	// nullopt for non-canonical input ("Quarantined Files"). Round-trip enforced.
	static std::optional<FolderId> parseFolderName(const QString &name);

	// Runs the plan on a worker thread. One execute in flight per instance.
	void executeAsync(const RebalancePlan &plan);

	// Thread-safe cancel; checked at composition-group boundaries so siblings stay together.
	void cancel() { m_job.cancel(); }

signals:
	void progress(int current, int total, const QString &detail);
	void log(int level, const QString &message);
	void finished(int succeeded, int failed, bool cancelled);

	// Pre-flight refused (e.g. files locked); no moves done. No 'finished' will follow.
	void aborted(const QString &reason);

private:
	void doExecute(const RebalancePlan &plan);

	BackgroundJob m_job{this};
};