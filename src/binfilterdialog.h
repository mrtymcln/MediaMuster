#pragma once

#include "avbparser.h"

#include <QDialog>
#include <QSet>
#include <QString>
#include <QVector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// Holds a bin set (loaded .avb files as AvbBins) and an ordered chain
// of (operation, bin set) steps. filterChainChanged emits the resolved
// accepted-MOB set, or an empty set with isActive=false if the chain
// is cleared (proxy then accepts all rows).

class BinFilterDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Operation
    {
        Intersect, // keep only MOBs that are in the selected bins
        Subtract,  // hide MOBs that are in the selected bins
        Add        // add MOBs from the selected bins back into the result
    };

    struct ChainStep
    {
        Operation op;
        QVector<QString> binDisplayNames; // for display only
        QSet<QString> mobIds;             // union of selected bins' MOBs
    };

    explicit BinFilterDialog(QWidget *parent = nullptr);
    ~BinFilterDialog() override;

    // Load a bin from disk and add it to the loaded bins list.
    // If same filePath, silently ignores duplicates.
    void addBinFromFile(const QString &avbFilePath);

signals:
    // Emitted whenever the chain changes: isActive is false when the chain
    // is empty (→ no filtering, accept everything). When true, acceptedMobs
    // is the set of MOB-ID hex strings that the filter should accept.
    // MediaFilterProxy should accept a row iff:
    //     !isActive
    //     || acceptedMobs.contains(mf.mobId)
    //     || acceptedMobs.contains(mf.compositionMobId)
    void filterChainChanged(bool isActive, const QSet<QString> &acceptedMobs);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onAddBinsClicked();
    void onRemoveSelectedBinsClicked();
    void onIntersectClicked();
    void onSubtractClicked();
    void onAddClicked();
    void onRemoveStep(int index);

private:
    void setupUi();
    void appendBinItem(int idx);
    void rebuildChainList();
    void recomputeAndEmit();
    void applyOperation(Operation op);

    // Union the MOBs of every bin currently ticked in m_binList.
    QSet<QString> selectedBinsMobs() const;
    QVector<QString> selectedBinsDisplayNames() const;
    int selectedBinsCount() const;

    // Collect the MOB IDs across every loaded bin: used as the starting
    // universe for the chain maths when the first operation is Subtract
    // (you can't subtract from 'accept everything', so prime with the
    // union of all loaded bins' MOBs). For Intersect-first the starting
    // universe doesn't matter because the intersect narrows immediately.
    QSet<QString> allLoadedMobs() const;

    QVector<AvbBin> m_bins;
    QVector<ChainStep> m_chain;

    QListWidget *m_binList = nullptr;
    QLabel *m_binListSummary = nullptr;
    QListWidget *m_chainList = nullptr;
    QLabel *m_chainSummary = nullptr;
    QPushButton *m_btnIntersect = nullptr;
    QPushButton *m_btnSubtract = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnDone = nullptr;
};