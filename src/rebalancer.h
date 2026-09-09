#pragma once

#include "conventions.h"
#include "backgroundjob.h"
#include "mediafile.h"
#include "opmanager.h"
#include "rebalanceplan.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <optional>

// MARK: - Rebalancer

/// Plans MXF redistribution below 5,000 files per Avid folder and submits it
/// to the shared engine. Relatives are scoped by media root, workstation and
/// valid MasterMobId. Cancellation is honoured between groups; an I/O failure
/// stops the run with completed and pending moves recorded for recovery.
/// A private OpManager keeps the Rebalance dialog's progress separate from
/// MainWindow's operations. The shared journal lock serializes engine runs.
class Rebalancer : public QObject
{
	Q_OBJECT
public:
	explicit Rebalancer(QObject *parent = nullptr);

	~Rebalancer() override;

	// MARK: - Planning

	static RebalancePlan computePlan(const QString &mxfRoot, const QString &volumeLabel,
									 const QVector<MediaFile> &files);

	static std::optional<FolderName> parseFolderName(const QString &name);

	/// The source folder a RenameOp came from, recomputed from its srcPath
	/// (RenameOp doesn't store the FolderName). nullopt when the parent dir
	/// isn't a conforming Avid folder name.
	static std::optional<FolderName> srcFolderOf(const QString &srcPath);

	/// Shared plan-to-engine adapter, also exercised by the disposable diagnostics.
	static OpRequest requestForPlan(const RebalancePlan &plan);

	// MARK: - Execution

	/// Only one execute is in flight per instance; a second call
	/// cancels and joins the previous pre-flight before starting anew.
	void executeAsync(const RebalancePlan &plan);

	/// Checked at relatives-group boundaries so relatives stay
	/// together on cancellation. Failures may require recovery.
	void cancel()
	{
		m_cancelRequested.store(true, std::memory_order_release);
		m_preflight.cancel();
		m_engine->cancel();
	}

signals:

	// MARK: - Progress signals

	void progress(int current, int total, const QString &detail);
	void log(QtMsgType level, const QString &message);
	void finished(int succeeded, int failed, bool cancelled);

	/// No moves performed and no `finished` will follow; the dialog
	/// treats this as a terminal state on its own.
	void aborted(const QString &reason);

private:
	/// GUI-thread tail of executeAsync: the pre-flight worker hops back
	/// here (queued) to hand the built request to the engine.
	void startEngineRun(OpRequest request);

	/// The private engine instance (see the class comment for why it is
	/// not MainWindow's). Signal adaptation happens once, in the ctor.
	OpManager *m_engine = nullptr;

	/// Folders whose Avid databases this run has reset, for the summary
	/// line. Written from the engine's worker thread via the
	/// folder-touched hook; read on the GUI thread after finished.
	std::atomic<int> m_foldersReset{0};

	std::atomic<bool> m_cancelRequested{false};

	/// Request preparation runs here so it cannot freeze the GUI. The
	/// renames themselves run on the engine's own worker.
	BackgroundJob m_preflight{this};
};
