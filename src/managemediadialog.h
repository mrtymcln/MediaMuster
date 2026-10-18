#pragma once

#include "mediafile.h"
#include "opmanager.h"
#include "operationplan.h"

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

/// Collects Copy / Move / Delete choices and previews destinations and conflicts.
/// MainWindow passes the accepted choices to FileOperationController for execution.
/// Conflict policies can be set per file or for all conflicting rows.
class ManageMediaDialog : public QDialog
{
	Q_OBJECT
	friend class TestOperationUi;

public:
	enum class Operation
	{
		Copy,
		Move,
		Delete
	};

	/// The file-operation engine's conflict policy.
	using ConflictPolicy = ::ConflictPolicy;

	explicit ManageMediaDialog(const QVector<MediaFile> &files, QWidget *parent = nullptr,
							   Operation initialOp = Operation::Copy);

	// MARK: - Result accessors

	Operation operation() const;
	QString destination() const;
	bool preserveStructure() const;

	/// Choices for previewed conflicts, keyed by source path.
	/// A later conflict without an approved policy fails the item;
	/// the engine never overwrites the destination.
	QHash<QString, ConflictPolicy> conflictPolicies() const;

protected:
	void showEvent(QShowEvent *event) override;

private slots:
	void onChooseDestination();
	void onOperationChanged();
	void onGlobalConflictPolicyChanged(int index);

private:
	void setupUi();
	void updatePreview();
	void updateSummary();
	void startDestinationCheck(bool includeConflicts);
	void syncGlobalFromPerFile();

	QVector<MediaFile> m_files;

	// MARK: - Operation radios

	QRadioButton *m_radioCopy = nullptr;
	QRadioButton *m_radioMove = nullptr;
	QRadioButton *m_radioDelete = nullptr;
	QButtonGroup *m_opGroup = nullptr;

	// MARK: - Destination row

	QLineEdit *m_destPath = nullptr;
	QPushButton *m_chooseDestinationButton = nullptr;
	QWidget *m_destWidget = nullptr;

	QCheckBox *m_chkPreserve = nullptr;

	QLabel *m_spaceWarning = nullptr;

	// MARK: - Conflicts

	QGroupBox *m_conflictGroup = nullptr;
	QComboBox *m_conflictGlobalCombo = nullptr;

	// MARK: - Preview

	QTreeWidget *m_previewTree = nullptr;
	QLabel *m_summaryLabel = nullptr;

	/// Conflict-row controls keyed by source path.
	QHash<QString, QComboBox *> m_perFileConflictCombos;

	QPushButton *m_cancelButton = nullptr;
	QPushButton *m_executeButton = nullptr;

	// MARK: - Async destination check

	// Probe storage off the GUI thread and disable Execute until results arrive.
	// The preview is advisory; execution rechecks identities and conflicts.

	/// Borrowed row pointer, valid only for its preview generation.
	/// updatePreview() invalidates that generation before clearing the tree.
	struct PendingRow
	{
		QTreeWidgetItem *item = nullptr;
		QString sourcePath;
		QString destPath;
		bool dupFirst = false; ///< First of several rows sharing destPath.
		bool dupLater = false; ///< Later duplicate; renamed at execution.
	};

	/// Sweep output, index-aligned with m_pendingRows. `renamed` is null
	/// unless the row needed a rename preview (on-disk conflict or later
	/// duplicate); null-despite-needing-one means all 999 slots were taken.
	struct DestCheckResult
	{
		QVector<bool> exists;
		QVector<bool> alreadyAtDestination;
		QVector<QString> renamed;
		OperationPlan::CopyMoveAssessment assessment;
		qint64 availableBytes = -1;
	};

	void applyDestinationCheck(const DestCheckResult &result, bool includeConflicts);
	OperationPlan::CopyMoveAssessment m_assessment;
	qint64 m_availableBytes = -1;
	QVector<PendingRow> m_pendingRows;
	int m_destCheckGeneration = 0; ///< Invalidates superseded preview or policy checks.
	bool m_checkingDest = false;   ///< Gates Execute in updateSummary().
};
