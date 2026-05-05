#pragma once

#include "mediafile.h"

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

// ManageMediaDialog — unified Copy/Move/Delete with live preview.
// Shows the user exactly what will happen before committing. Includes
// per-file conflict resolution when a destination file already exists.

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
    enum class ConflictPolicy
    {
        Overwrite,
        Skip,
        Rename
    };

    // Promote to int for QButtonGroup ids and QComboBox userData.
    friend constexpr int operator+(Operation o) noexcept { return static_cast<int>(o); }
    friend constexpr int operator+(ConflictPolicy p) noexcept { return static_cast<int>(p); }

    explicit ManageMediaDialog(const QVector<MediaFile> &files,
                                  QWidget *parent = nullptr,
                                  Operation initialOp = Operation::Copy);

    Operation operation() const;
    QString destination() const;
    bool preserveStructure() const;

    // Per-file conflict resolution. Key = source file path. Only files
    // whose destination already exists have an entry. If a file is not
    // in the map it has no conflict. The caller should check this before
    // executing to honour the user's per-file choices.
    QHash<QString, ConflictPolicy> conflictPolicies() const;

private slots:
    void onChooseDestination();
    void onOperationChanged();
    void onGlobalConflictPolicyChanged(int index);

private:
    void setupUi();
    void updatePreview();
    void updateSummary();
    void syncGlobalFromPerFile();
    QString buildDestPath(const MediaFile &mf, const QString &destRoot,
                          bool preserve) const;

    QVector<MediaFile> m_files;

    // Operation group
    QRadioButton *m_radioCopy = nullptr;
    QRadioButton *m_radioMove = nullptr;
    QRadioButton *m_radioDelete = nullptr;
    QButtonGroup *m_opGroup = nullptr;

    // Destination
    QLineEdit *m_destPath = nullptr;
    QPushButton *m_btnChoose = nullptr;
    QWidget *m_destWidget = nullptr;

    // Options
    QGroupBox *m_optGroup = nullptr;
    QCheckBox *m_chkPreserve = nullptr;

    // Destination space warning (shown when dest has insufficient free space)
    QLabel *m_spaceWarning = nullptr;

    // Conflict resolution
    QGroupBox *m_conflictGroup = nullptr;
    QComboBox *m_conflictGlobalCombo = nullptr;

    // Preview
    QTreeWidget *m_previewTree = nullptr;
    QLabel *m_summaryLabel = nullptr;

    // Per-file conflict combos: keyed by source path. Only conflict
    // rows have entries; non-conflict rows are not in this map.
    QHash<QString, QComboBox *> m_perFileConflictCombos;

    // Footer
    QPushButton *m_btnCancel = nullptr;
    QPushButton *m_btnExecute = nullptr;
};