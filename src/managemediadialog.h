#pragma once

#include "mediafile.h"
#include "mediamanager.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QHash>
#include <QVector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QTreeWidget;
class QTreeWidgetItem;
class QGroupBox;
class QShowEvent;

// MARK: - ManageMediaDialog

/// Drives MediaManager. Lets the editor pick Copy / Move / Delete,
/// choose a destination, decide whether to preserve the Avid folder
/// structure, and resolve per-file conflicts before the op happens.
///
/// Preview tree shows source + destination per file. Conflicting
/// rows gain a per-row policy combo (Keep Both / Skip / Replace).
/// A global combo applies the same policy to every conflicting row;
/// syncing goes both ways via syncGlobalFromPerFile.
class ManageMediaDialog : public QDialog
{
	Q_OBJECT
public:
	enum class Operation
	{
		Copy,
		Move,
		Delete
	};

	/// Aliased so dialog clients don't need to include mediamanager.h.
	using ConflictPolicy = MediaManager::ConflictPolicy;

	explicit ManageMediaDialog(const QVector<MediaFile> &files, QWidget *parent = nullptr,
							   Operation initialOp = Operation::Copy);

	// MARK: - Result accessors

	Operation operation() const;
	QString destination() const;
	bool preserveStructure() const;

	/// Keyed by source file path. Only contains entries for rows
	/// that actually conflicted with an existing destination. A file absent
	/// from the map was never flagged as a conflict; MediaManager skips it
	/// rather than replace, should one appear. The caller passes this
	/// straight through.
	QHash<QString, ConflictPolicy> conflictPolicies() const;

protected:
	void showEvent(QShowEvent *event) override;

private slots:
	void onChooseDestination();
	void onOperationChanged();
	void onGlobalConflictPolicyChanged(int index);

	/// Applies the pool sweep's exists/rename results to the preview rows
	/// (red conflict rows, per-row combos, duplicate-rename previews) and
	/// re-enables Execute. Results from a superseded generation are dropped.
	void onDestCheckFinished();

private:
	void setupUi();
	void updatePreview();
	void updateSummary();
	void syncGlobalFromPerFile();

	QVector<MediaFile> m_files;

	// MARK: - Operation radios

	QRadioButton *m_radioCopy = nullptr;
	QRadioButton *m_radioMove = nullptr;
	QRadioButton *m_radioDelete = nullptr;
	QButtonGroup *m_opGroup = nullptr;

	// MARK: - Destination row

	QLineEdit *m_destPath = nullptr;
	QPushButton *m_btnChoose = nullptr;
	QWidget *m_destWidget = nullptr;

	QCheckBox *m_chkPreserve = nullptr;

	QLabel *m_spaceWarning = nullptr;

	// MARK: - Conflicts

	QGroupBox *m_conflictGroup = nullptr;
	QComboBox *m_conflictGlobalCombo = nullptr;

	// MARK: - Preview

	QTreeWidget *m_previewTree = nullptr;
	QLabel *m_summaryLabel = nullptr;

	/// Keyed by source path. Only conflict rows have entries;
	/// that's how we know which rows to walk.
	QHash<QString, QComboBox *> m_perFileConflictCombos;

	QPushButton *m_btnCancel = nullptr;
	QPushButton *m_btnExecute = nullptr;

	// MARK: - Async destination check

	// The exists()/rename probes for the preview run on a pool thread: on an
	// SMB/Nexis share, thousands of synchronous stats per option change froze
	// the dialog for seconds. While a sweep is in flight the Execute button is
	// held disabled (m_checkingDest) so a Copy/Move can't launch against
	// unknown conflicts; MediaManager still re-checks the disk live at
	// execution time, so the preview is advisory either way.

	/// One preview row awaiting sweep results. `item` stays valid for the
	/// generation it was created in: updatePreview() is the only place the
	/// tree is cleared, and it bumps m_destCheckGeneration first, so a
	/// generation match guarantees the pointers are live.
	struct PendingRow
	{
		QTreeWidgetItem *item = nullptr;
		QString sourcePath;
		QString destPath;
		bool dupFirst = false; ///< First of several rows sharing destPath.
		bool dupLater = false; ///< Later duplicate; renamed at execution.
	};

	/// Sweep output, index-aligned with m_pendingRows. `renamed` is null
	/// unless the row needed a .Copy.NN preview (on-disk conflict or later
	/// duplicate); null-despite-needing-one means all 999 slots were taken.
	struct DestCheckResult
	{
		QVector<bool> exists;
		QVector<QString> renamed;
	};

	QFutureWatcher<DestCheckResult> m_destCheckWatcher;
	QVector<PendingRow> m_pendingRows;
	int m_destCheckGeneration = 0; ///< Bumped by every updatePreview().
	int m_destCheckLaunched = -1;  ///< Generation the in-flight sweep belongs to.
	bool m_checkingDest = false;   ///< Gates Execute in updateSummary().
};