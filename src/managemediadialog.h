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
    // Source of truth lives on MediaManager — alias it here.
    using ConflictPolicy = MediaManager::ConflictPolicy;

    friend constexpr int operator+(Operation o) noexcept { return static_cast<int>(o); }

    explicit ManageMediaDialog(const QVector<MediaFile> &files,
                               QWidget *parent = nullptr,
                               Operation initialOp = Operation::Copy);

    Operation operation() const;
    QString destination() const;
    bool preserveStructure() const;

    // Per-file conflict resolution. Key = source file path.
    // The caller should check before executing, to honour the user's per-file choices.
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

    QRadioButton *m_radioCopy = nullptr;
    QRadioButton *m_radioMove = nullptr;
    QRadioButton *m_radioDelete = nullptr;
    QButtonGroup *m_opGroup = nullptr;

    QLineEdit *m_destPath = nullptr;
    QPushButton *m_btnChoose = nullptr;
    QWidget *m_destWidget = nullptr;

    QCheckBox *m_chkPreserve = nullptr;

    QLabel *m_spaceWarning = nullptr;

    QGroupBox *m_conflictGroup = nullptr;
    QComboBox *m_conflictGlobalCombo = nullptr;

    QTreeWidget *m_previewTree = nullptr;
    QLabel *m_summaryLabel = nullptr;

    // Per-file conflict combos: keyed by source path. Only conflict
    // rows have entries; non-conflict rows are not in this map.
    QHash<QString, QComboBox *> m_perFileConflictCombos;

    QPushButton *m_btnCancel = nullptr;
    QPushButton *m_btnExecute = nullptr;
};