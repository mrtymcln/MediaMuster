#include "unfinishedbusinessdialog.h"
#include "formatutil.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace
{
	constexpr int kLayoutMargin = 16;
	constexpr int kRowSpacing = 12;
	constexpr int kButtonWidth = 150;
	constexpr int kButtonHeight = 44;
	constexpr int kPathsWidth = 560;
	constexpr int kPathsHeight = 140;
	constexpr int kJobLabelCharacters = 30;

	struct ActionRow
	{
		QWidget *widget;
		QPushButton *button;
	};

	ActionRow makeActionRow(const QString &label, const QString &explanation,
						   const QString &buttonName, QWidget *parent)
	{
		auto *row = new QWidget(parent);
		auto *layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(kRowSpacing);
		auto *button = new QPushButton(label, row);
		button->setObjectName(buttonName);
		button->setMinimumSize(kButtonWidth, kButtonHeight);
		button->setAutoDefault(false);
		layout->addWidget(button);
		auto *help = new QLabel(explanation, row);
		help->setTextFormat(Qt::PlainText);
		help->setWordWrap(true);
		layout->addWidget(help, 1);
		return {row, button};
	}

	QString jobKind(OpKind kind)
	{
		switch (kind)
		{
		case OpKind::Copy:
			return UnfinishedBusinessDialog::tr("Copy");
		case OpKind::Move:
			return UnfinishedBusinessDialog::tr("Move");
		case OpKind::Delete:
			return UnfinishedBusinessDialog::tr("Trash");
		case OpKind::Rename:
			return UnfinishedBusinessDialog::tr("Rebalance");
		case OpKind::Undo:
			return UnfinishedBusinessDialog::tr("Undo");
		}
		return UnfinishedBusinessDialog::tr("Interrupted job");
	}
} // namespace

QVector<UnfinishedBusinessDialog::Job> UnfinishedBusinessDialog::mergeJobs(
	const QVector<OperationRecovery::Resumable> &resumable,
	const QVector<OperationRecovery::Restorable> &restorable)
{
	QVector<Job> jobs;
	jobs.reserve(resumable.size() + restorable.size());
	// Originals awaiting restoration come first, even if their job was dismissed.
	for (const auto &source : restorable)
	{
		if (source.originals.isEmpty())
			continue;
		const auto duplicate = std::find_if(jobs.cbegin(), jobs.cend(),
			[&source](const Job &job) { return job.journalPath == source.journalPath; });
		if (duplicate != jobs.cend())
			continue;
		QStringList locations;
		for (qsizetype index = 0; index < source.originals.size(); ++index)
			locations.append(tr("Kept at: %1\nRestore to: %2")
				.arg(QDir::toNativeSeparators(source.retainedPaths.value(index)),
					 QDir::toNativeSeparators(source.originals[index])));
		const QString folder = QDir::toNativeSeparators(QFileInfo(source.originals.first()).absolutePath());
		Job job;
		job.journalPath = source.journalPath;
		job.label = tr("Restore originals — %1").arg(folder);
		const QString originals = source.originals.size() == 1 ? tr("1 original")
			: tr("%1 originals").arg(Format::count(source.originals.size()));
		job.summary = tr("%1 can be returned from temporary folders.\nOriginal folder: %2")
			.arg(originals, folder);
		job.restoreLocations = locations.join(QStringLiteral("\n\n"));
		jobs.append(job);
	}
	for (const auto &source : resumable)
	{
		auto match = std::find_if(jobs.begin(), jobs.end(),
			[&source](const Job &job) { return job.journalPath == source.journalPath; });
		if (match == jobs.end())
		{
			Job job;
			job.journalPath = source.journalPath;
			jobs.append(job);
			match = std::prev(jobs.end());
		}
		const QString kind = jobKind(source.kind);
		const auto started = QDateTime::fromString(source.started, Qt::ISODateWithMs);
		const QString date = started.isValid()
			? QLocale().toString(started.toLocalTime(), QLocale::ShortFormat) : QString();
		match->label = date.isEmpty() ? kind : tr("%1 — %2").arg(kind, date);
		QStringList summary{tr("%1 job: %2 of %3 files finished.")
			.arg(kind, Format::count(source.finished), Format::count(source.total))};
		if (!date.isEmpty())
			summary.append(tr("Started: %1").arg(date));
		if (!source.dest.isEmpty())
			summary.append(tr("Destination: %1").arg(QDir::toNativeSeparators(source.dest)));
		match->summary = summary.join(QLatin1Char('\n'));
		match->canResume = true;
	}
	return jobs;
}

UnfinishedBusinessDialog::UnfinishedBusinessDialog(
	const QVector<OperationRecovery::Resumable> &resumable,
	const QVector<OperationRecovery::Restorable> &restorable,
	const QString &preferredJournalPath, QWidget *parent)
	: QDialog(parent), m_jobs(mergeJobs(resumable, restorable))
{
	setObjectName(QStringLiteral("unfinishedBusinessDialog"));
	setWindowTitle(tr("Unfinished Business"));
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(kLayoutMargin, kLayoutMargin, kLayoutMargin, kLayoutMargin);
	layout->setSpacing(kRowSpacing);
	auto *intro = new QLabel(tr("Choose what happens next with an interrupted job."), this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *jobs = new QComboBox(this);
	jobs->setObjectName(QStringLiteral("unfinishedBusinessJob"));
	jobs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	jobs->setMinimumContentsLength(kJobLabelCharacters);
	int preferredIndex = 0;
	for (qsizetype index = 0; index < m_jobs.size(); ++index)
	{
		const auto &job = m_jobs[index];
		jobs->addItem(job.label, job.journalPath);
		if (job.journalPath == preferredJournalPath)
			preferredIndex = jobs->count() - 1;
	}
	jobs->setVisible(m_jobs.size() > 1);
	jobs->setCurrentIndex(preferredIndex);
	layout->addWidget(jobs);
	m_summary = new QLabel(this);
	m_summary->setObjectName(QStringLiteral("unfinishedBusinessSummary"));
	m_summary->setTextFormat(Qt::PlainText);
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);

	const auto resume = makeActionRow(tr("Resume Job"), tr("Continue the unfinished operation."),
		QStringLiteral("resumeInterruptedJobButton"), this);
	m_resumeRow = resume.widget;
	layout->addWidget(m_resumeRow);
	connect(resume.button, &QPushButton::clicked, this, [this] { choose(Choice::Resume); });
	const auto restore = makeActionRow(tr("Restore Originals"),
		tr("Return originals from temporary folders without replacing existing files. "
		   "Keep completed destination copies."), QStringLiteral("restoreOriginalsButton"), this);
	m_restoreRow = restore.widget;
	layout->addWidget(m_restoreRow);
	connect(restore.button, &QPushButton::clicked, this, [this] { choose(Choice::Restore); });
	const auto cancel = makeActionRow(tr("Cancel Job"),
		tr("Abandon the unfinished work and keep completed results. "
		   "Any stranded originals can still be restored later."),
		QStringLiteral("cancelInterruptedJobButton"), this);
	m_cancelRow = cancel.widget;
	layout->addWidget(m_cancelRow);
	connect(cancel.button, &QPushButton::clicked, this, [this] { choose(Choice::CancelJob); });

	auto *details = new QGroupBox(tr("Original locations"), this);
	m_restoreDetails = details;
	auto *detailsLayout = new QVBoxLayout(details);
	m_restorePaths = new QPlainTextEdit(details);
	m_restorePaths->setObjectName(QStringLiteral("restoreOriginalPaths"));
	m_restorePaths->setReadOnly(true);
	m_restorePaths->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_restorePaths->setMinimumSize(kPathsWidth, kPathsHeight);
	detailsLayout->addWidget(m_restorePaths);
	layout->addWidget(details);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto *close = buttons->button(QDialogButtonBox::Close);
	close->setObjectName(QStringLiteral("closeUnfinishedBusinessButton"));
	close->setDefault(true);
	close->setFocus();
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	connect(jobs, &QComboBox::currentIndexChanged, this, &UnfinishedBusinessDialog::showJob);
	showJob(jobs->currentIndex());
}

QString UnfinishedBusinessDialog::selectedJournalPath() const
{
	return m_selectedJob >= 0 && m_selectedJob < m_jobs.size()
		? m_jobs[m_selectedJob].journalPath : QString();
}

void UnfinishedBusinessDialog::showJob(int index)
{
	m_selectedJob = index;
	const bool valid = index >= 0 && index < m_jobs.size();
	const bool canResume = valid && m_jobs[index].canResume;
	const bool canRestore = valid && !m_jobs[index].restoreLocations.isEmpty();
	m_summary->setText(valid ? m_jobs[index].summary : tr("No unfinished jobs need attention."));
	m_resumeRow->setVisible(canResume);
	m_resumeRow->setEnabled(canResume);
	m_cancelRow->setVisible(canResume);
	m_cancelRow->setEnabled(canResume);
	m_restoreRow->setVisible(canRestore);
	m_restoreRow->setEnabled(canRestore);
	m_restoreDetails->setVisible(canRestore);
	m_restorePaths->setPlainText(canRestore ? m_jobs[index].restoreLocations : QString());
}

void UnfinishedBusinessDialog::choose(Choice choice)
{
	if (m_selectedJob < 0 || m_selectedJob >= m_jobs.size())
		return;
	const auto &job = m_jobs[m_selectedJob];
	if ((choice == Choice::Restore && job.restoreLocations.isEmpty()) ||
		((choice == Choice::Resume || choice == Choice::CancelJob) && !job.canResume))
		return;
	m_choice = choice;
	accept();
}
