#include "progressdialog.h"

#include <QCloseEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

ProgressDialog::ProgressDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Progress"));

    // App-modal — no other window can be clicked while a scan or file
    // op is running. Cancel is the only out.
    setModal(true);

    // Qt::Sheet renders as a native macOS sheet (slides from the parent's
    // title bar, dims the parent) and as a frameless modal child on
    // Windows. Width inherits from the parent on macOS so setMinimumWidth
    // is ignored there; Windows respects explicit sizes.
    setWindowFlags(Qt::Sheet);

    setMinimumWidth(520);
    resize(640, 0);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    m_titleLabel = new QLabel;
    QFont tf = m_titleLabel->font();
    tf.setPointSize(tf.pointSize() + 1);
    tf.setBold(true);
    m_titleLabel->setFont(tf);
    layout->addWidget(m_titleLabel);

    m_detailLabel = new QLabel;
    m_detailLabel->setTextFormat(Qt::PlainText);
    m_detailLabel->setMinimumWidth(480);
    // One elided line reads better than a wrapping multi-line path.
    layout->addWidget(m_detailLabel);

    m_bar = new QProgressBar;
    m_bar->setTextVisible(false);
    m_bar->setRange(0, 0); // starts indeterminate
    layout->addWidget(m_bar);

    auto *bottomRow = new QHBoxLayout;
    m_counterLabel = new QLabel;
    QFont cf = m_counterLabel->font();
    cf.setPointSize(cf.pointSize() - 1);
    m_counterLabel->setFont(cf);
    m_counterLabel->setStyleSheet("color: gray;");
    bottomRow->addWidget(m_counterLabel);

    bottomRow->addStretch();

    m_cancelBtn = new QPushButton(tr("Cancel"));
    connect(m_cancelBtn, &QPushButton::clicked, this,
            &ProgressDialog::cancelRequested);
    bottomRow->addWidget(m_cancelBtn);

    layout->addLayout(bottomRow);
}

void ProgressDialog::begin(const QString &title)
{
    m_titleLabel->setText(title);
    m_detailLabel->clear();
    m_counterLabel->clear();
    m_bar->setRange(0, 0); // indeterminate until first real progress
    m_bar->setValue(0);
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setText(tr("Cancel"));
    show();
    raise();
}

void ProgressDialog::setProgress(int current, int total)
{
    if (total > 0)
    {
        if (m_bar->maximum() != total)
            m_bar->setRange(0, total);
        m_bar->setValue(current);
        const int pct = qRound((100.0 * current) / total);
        // "88%, 23 of 45"
        m_counterLabel->setText(
            tr("%1% — %2 of %3").arg(pct).arg(current).arg(total));
    }
    else
    {
        m_bar->setRange(0, 0); // indeterminate
        m_counterLabel->clear();
    }
}

void ProgressDialog::setDetail(const QString &text)
{
    // Mid-elide so long paths still show the volume (head) and filename (tail).
    const int availWidth = m_detailLabel->width() > 0
                               ? m_detailLabel->width()
                               : 420;
    const QString elided = m_detailLabel->fontMetrics().elidedText(
        text, Qt::ElideMiddle, availWidth);
    m_detailLabel->setText(elided);
}

void ProgressDialog::finish()
{
    hide();
}

void ProgressDialog::closeEvent(QCloseEvent *event)
{
    // Don't let the OS close button stash a running operation in the
    // background. Emit cancelRequested and wait for the worker to wind down.
    event->ignore();
    if (m_cancelBtn->isEnabled())
        emit cancelRequested();
}