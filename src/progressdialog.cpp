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
	// No title of any kind: it is a sheet on the main window, and the
	// console says what is running. The detail line carries the rest.

	// Modal so the user can't kick off a second operation whilst this
	// one is mid-flight; Cancel is the only way out.
	setModal(true);
	setWindowFlags(Qt::Sheet);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(18, 16, 18, 16);
	layout->setSpacing(10);

	// MARK: Detail row

	m_detailLabel = new QLabel;
	m_detailLabel->setTextFormat(Qt::PlainText);
	// Fixed width so the dialog's own width is stable: the detail text is
	// mid-elided to this width in setDetail(), and it's the widest row, so it
	// determines the fixed dialog size below.
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

	// Size to content and lock it: the dialog can't be resized, and because the
	// detail row is fixed-width the size never jitters as the text changes.
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
		setProgress(current, total); // indeterminate path, one home
		return;
	}

	// 1000 bar ticks per item: fine enough that a multi-GB file's bytes
	// move the bar visibly, coarse enough to never overflow int for any
	// plausible selection size.
	constexpr int kTicksPerItem = 1000;
	const int clampedCurrent = qBound(1, current, total);
	const double clampedPct = qBound(0.0, pct, 100.0);
	const int max = total * kTicksPerItem;
	if (m_bar->maximum() != max)
		m_bar->setRange(0, max);
	m_bar->setValue((clampedCurrent - 1) * kTicksPerItem +
					int(clampedPct * (kTicksPerItem / 100.0)));

	// The label counts whole items, with the overall percentage folding
	// the current item's fraction in — "42% — 3 of 7".
	const double overall = ((clampedCurrent - 1) + clampedPct / 100.0) / total * 100.0;
	m_counterLabel->setText(tr("%1% — %2 of %3")
								.arg(qRound(overall))
								.arg(Format::count(current), Format::count(total)));
}

void ProgressDialog::setDetail(const QString &text)
{
	// Mid-elide so long paths still show the head and the tail.
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
	// Already cancelling: don't re-emit for button mashing or Esc.
	if (!m_cancelBtn->isEnabled())
		return;

	// Acknowledge immediately. The worker's cooperative cancel can take
	// seconds on a large copy, and a silent, still-enabled button reads
	// as broken. begin() re-arms the button for the next run.
	m_cancelBtn->setEnabled(false);
	m_cancelBtn->setText(tr("Cancelling..."));
	emit cancelRequested();
}

// MARK: - Close/Escape interception

void ProgressDialog::closeEvent(QCloseEvent *event)
{
	// Redirect the OS close button to our Cancel flow.
	event->ignore();
	requestCancel();
}

void ProgressDialog::reject()
{
	// Esc lands here, not in closeEvent — QDialog::reject would hide
	// the dialog while the operation kept running headless. Treat it
	// as a cancel request and stay visible until finish().
	requestCancel();
}