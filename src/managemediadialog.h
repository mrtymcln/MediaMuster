#pragma once

#include "mediafile.h"
#include "mediamanager.h"

#include <QDialog>
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

	/// Unary + for nicer call sites — +Operation::Copy instead of
	/// static_cast<int>(Operation::Copy).
	friend constexpr int operator+(Operation o) noexcept
	{
		return static_cast<int>(o);
	}

	explicit ManageMediaDialog(const QVector<MediaFile> &files,
	                           QWidget *parent = nullptr,
	                           Operation initialOp = Operation::Copy);

	// MARK: - Result accessors

	Operation operation() const;
	QString destination() const;
	bool preserveStructure() const;

	/// Keyed by source file path. Only contains entries for rows
	/// that actually conflicted with an existing destination;
	/// MediaManager treats absent keys as Replace, so the caller
	/// passes this straight through.
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

	/// Keyed by source path. Only conflict rows have entries —
	/// that's how we know which rows to walk.
	QHash<QString, QComboBox *> m_perFileConflictCombos;

	QPushButton *m_btnCancel = nullptr;
	QPushButton *m_btnExecute = nullptr;
};