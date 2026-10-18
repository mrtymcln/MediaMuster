#include "progressdialog.h"
#include "formatutil.h"

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
	// The Console identifies the run; this modal sheet shows progress and Cancel.
	setModal(true);
	setWindowFlags(Qt::Sheet);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(20, 20, 20, 20);
	layout->setSpacing(12);

	// MARK: Detail row

	m_detailLabel = new QLabel;
	m_detailLabel->setTextFormat(Qt::PlainText);
	// Keep the sheet's width stable as the current path changes.
	m_detailLabel->setFixedWidth(600);
	layout->addWidget(m_detailLabel);

	// MARK: Progress bar

	m_bar = new QProgressBar;
	m_bar->setTextVisible(false);
	m_bar->setRange(0, 0); // Indeterminate.
	layout->addWidget(m_bar);

	// MARK: Bottom row

	auto *bottomRow = new QHBoxLayout;
	m_counterLabel = new QLabel;
	bottomRow->addWidget(m_counterLabel);

	bottomRow->addStretch();

	m_cancelBtn = new QPushButton(tr("Cancel"));
	connect(m_cancelBtn, &QPushButton::clicked, this, &ProgressDialog::requestCancel);
	bottomRow->addWidget(m_cancelBtn);

	layout->addLayout(bottomRow);

	layout->setSizeConstraint(QLayout::SetFixedSize);
}

// MARK: - Public lifecycle

void ProgressDialog::begin()
{
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
			tr("%1% — %2 of %3").arg(pct).arg(Format::count(current), Format::count(total)));
	}
	else
	{
		m_bar->setRange(0, 0); // Indeterminate.
		m_counterLabel->clear();
	}
}

void ProgressDialog::setItemProgress(int current, int total, double pct)
{
	if (total <= 0)
	{
		setProgress(current, total);
		return;
	}

	// Subdivide work units so progress advances within a large file.
	constexpr int kTicksPerItem = 1000;
	const int clampedCurrent = qBound(1, current, total);
	const double clampedPct = qBound(0.0, pct, 100.0);
	const int max = total * kTicksPerItem;
	if (m_bar->maximum() != max)
		m_bar->setRange(0, max);
	const int ticks = (clampedCurrent - 1) * kTicksPerItem +
					  int(clampedPct * (kTicksPerItem / 100.0));
	// Native copying reaching its final byte still leaves publication,
	// persistence and (for Move) source removal. Only finish ends the job.
	m_bar->setValue(qMin(ticks, max - 1));

	// The engine may count separate copy/removal work units for one file.
	// Present overall progress without describing those steps as more files.
	const double overall = ((clampedCurrent - 1) + clampedPct / 100.0) / total * 100.0;
	m_counterLabel->setText(tr("%1%").arg(qMin(99, qRound(overall))));
}

void ProgressDialog::setDetail(const QString &text)
{
	const int availWidth = m_detailLabel->width() > 0 ? m_detailLabel->width() : 420;
	const QString elided =
		m_detailLabel->fontMetrics().elidedText(text, Qt::ElideMiddle, availWidth);
	m_detailLabel->setText(elided);
}

void ProgressDialog::finish()
{
	hide();
}

// MARK: - Cancel funnel

void ProgressDialog::requestCancel()
{
	if (!m_cancelBtn->isEnabled())
		return;

	// Acknowledge immediately while the worker finishes cooperative cancellation.
	m_cancelBtn->setEnabled(false);
	m_cancelBtn->setText(tr("Cancelling..."));
	emit cancelRequested();
}

// MARK: - Close/Escape interception

void ProgressDialog::closeEvent(QCloseEvent *event)
{
	event->ignore();
	requestCancel();
}

void ProgressDialog::reject()
{
	// QDialog::reject would hide the sheet before the worker finishes.
	requestCancel();
}
