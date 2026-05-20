#include "progressdialog.h"

#include <QCloseEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

// MARK: - Construction

ProgressDialog::ProgressDialog(QWidget *parent)
    : QDialog(parent)
{
	setWindowTitle(tr("Progress"));

	// Modal so the user can't kick off a second operation whilst this
	// one is mid-flight — Cancel is the only way out.
	setModal(true);
	setWindowFlags(Qt::Sheet);

	setMinimumWidth(500);
	resize(640, 0);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(18, 16, 18, 16);
	layout->setSpacing(10);

	// MARK: Title row

	m_titleLabel = new QLabel;
	QFont tf = m_titleLabel->font();
	tf.setPointSize(tf.pointSize() + 1);
	tf.setBold(true);
	m_titleLabel->setFont(tf);
	layout->addWidget(m_titleLabel);

	// MARK: Detail row

	m_detailLabel = new QLabel;
	m_detailLabel->setTextFormat(Qt::PlainText);
	m_detailLabel->setMinimumWidth(480);
	layout->addWidget(m_detailLabel);

	// MARK: Progress bar

	m_bar = new QProgressBar;
	m_bar->setTextVisible(false);
	m_bar->setRange(0, 0); // Starts indeterminate (no known total yet).
	layout->addWidget(m_bar);

	// MARK: Bottom row

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

// MARK: - Public lifecycle

void ProgressDialog::begin(const QString &title)
{
	m_titleLabel->setText(title);
	m_detailLabel->clear();
	m_counterLabel->clear();
	m_bar->setRange(0, 0); // Reset to indeterminate for the new run.
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
		m_counterLabel->setText(
		    tr("%1% — %2 of %3").arg(pct).arg(current).arg(total));
	}
	else
	{
		m_bar->setRange(0, 0); // Indeterminate.
		m_counterLabel->clear();
	}
}

void ProgressDialog::setDetail(const QString &text)
{
	// Mid-elide so long paths still show the head and the tail.
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

// MARK: - Close button interception

void ProgressDialog::closeEvent(QCloseEvent *event)
{
	// Redirect OS close button to our Cancel flow.
	event->ignore();
	if (m_cancelBtn->isEnabled())
		emit cancelRequested();
}