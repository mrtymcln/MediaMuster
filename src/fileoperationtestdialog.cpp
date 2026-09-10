#include "fileoperationtestdialog.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVBoxLayout>

FileOperationTestDialog::FileOperationTestDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Test File Operations"));
	setObjectName("fileOperationTestDialog");
	setWindowModality(Qt::ApplicationModal);
	resize(760, 620);
	auto *layout = new QVBoxLayout(this);
	auto *intro = new QLabel(
		tr("Run Copy, Move, Trash, Rebalance and recovery checks using disposable files. "
		   "The test creates its own folders in BOTH selected locations. Test files and reports "
		   "are kept for inspection, including after a stopped run."),
		this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *form = new QFormLayout;
	auto folderRow = [&](const QString &label, QLineEdit *&field)
	{
		auto *row = new QWidget(this);
		auto *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		field = new QLineEdit(QDir::homePath(), row);
		auto *browse = new QPushButton(tr("Browse…"), row);
		rowLayout->addWidget(field, 1);
		rowLayout->addWidget(browse);
		connect(browse, &QPushButton::clicked, this,
				[this, field]
				{
					const auto path = QFileDialog::getExistingDirectory(
						this, tr("Choose a Test Location"), field->text());
					if (!path.isEmpty())
						field->setText(path);
				});
		form->addRow(label, row);
	};
	folderRow(tr("Source drive folder"), m_source);
	folderRow(tr("Destination drive folder"), m_destination);
	m_notes = new QLineEdit(this);
	m_notes->setPlaceholderText(tr("Optional: NEXIS client version, NAS model, connection type…"));
	form->addRow(tr("Storage notes"), m_notes);
	layout->addLayout(form);
	auto *sampleRow = new QHBoxLayout;
	auto *browseSamples = new QPushButton(tr("Add MXF Samples…"), this);
	auto *clearSamples = new QPushButton(tr("Clear Extras"), this);
	m_samplesLabel = new QLabel(tr("3 bundled relatives · no extra samples"), this);
	sampleRow->addWidget(browseSamples);
	sampleRow->addWidget(clearSamples);
	sampleRow->addWidget(m_samplesLabel, 1);
	layout->addLayout(sampleRow);
	auto *sampleInfo = new QLabel(
		tr("Includes one video and two audio MXFs from one master clip. Disposable copies test "
		   "their identities and Rebalance grouping. You can add more MXFs; selected originals "
		   "stay in place."),
		this);
	sampleInfo->setWordWrap(true);
	layout->addWidget(sampleInfo);
	connect(browseSamples, &QPushButton::clicked, this,
			[this]
			{
				const auto files = QFileDialog::getOpenFileNames(this, tr("Choose MXF Samples"), {},
																 tr("MXF files (*.mxf *.MXF)"));
				if (files.isEmpty())
					return;
				m_samples = files;
				m_samplesLabel->setText(
					tr("3 bundled relatives + %n extra sample(s)", nullptr, m_samples.size()));
			});
	connect(clearSamples, &QPushButton::clicked, this,
			[this]
			{
				m_samples.clear();
				m_samplesLabel->setText(tr("3 bundled relatives · no extra samples"));
			});
	m_output = new QPlainTextEdit(this);
	m_output->setReadOnly(true);
	m_output->setPlaceholderText(
		tr("Results will distinguish Passed, Failed, Unsupported and Not tested. "
		   "Testing Windows on NEXIS does not validate macOS on NEXIS."));
	layout->addWidget(m_output, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	m_run = buttons->addButton(tr("Run Tests"), QDialogButtonBox::ActionRole);
	m_cancel = buttons->addButton(tr("Cancel Tests"), QDialogButtonBox::ActionRole);
	m_save = buttons->addButton(tr("Save Report…"), QDialogButtonBox::ActionRole);
	m_cancel->setEnabled(false);
	m_save->setEnabled(false);
	connect(m_run, &QPushButton::clicked, this, &FileOperationTestDialog::start);
	connect(m_cancel, &QPushButton::clicked, this,
			[this]
			{
				m_job.cancel();
				m_cancel->setEnabled(false);
				m_output->appendPlainText(tr(
					"Cancelling after the current file operation reaches a safe stopping point…"));
			});
	connect(m_save, &QPushButton::clicked, this, &FileOperationTestDialog::saveReport);
	connect(buttons, &QDialogButtonBox::rejected, this, &FileOperationTestDialog::reject);
	layout->addWidget(buttons);
}

FileOperationTestDialog::~FileOperationTestDialog()
{
	m_job.shutdown();
}
void FileOperationTestDialog::reject()
{
	if (m_running)
	{
		m_cancel->click();
		return;
	}
	QDialog::reject();
}
void FileOperationTestDialog::start()
{
	if (!QFileInfo(m_source->text()).isDir() || !QFileInfo(m_destination->text()).isDir())
	{
		QMessageBox::information(this, tr("Choose Test Folders"),
								 tr("Choose an existing folder on each drive to test."));
		return;
	}
	OpDiagnostics::Options options;
	options.sourceArea = m_source->text();
	options.destinationArea = m_destination->text();
	options.reportArea =
		QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/diagnostics";
	options.storageNotes = m_notes->text();
	options.samples = m_samples;
	m_running = true;
	m_run->setEnabled(false);
	m_cancel->setEnabled(true);
	m_save->setEnabled(false);
	m_output->clear();
	m_job.start(
		[this, options]
		{
			const auto report = OpDiagnostics::run(options, m_job.cancelFlag(),
												   [this](const QString &message)
												   {
													   QMetaObject::invokeMethod(
														   this, [this, message]
														   { m_output->appendPlainText(message); },
														   Qt::QueuedConnection);
												   });
			QMetaObject::invokeMethod(
				this,
				[this, report]
				{
					m_report = report;
					m_output->setPlainText(report.text);
					m_running = false;
					m_run->setEnabled(true);
					m_cancel->setEnabled(false);
					m_save->setEnabled(true);
				},
				Qt::QueuedConnection);
		});
}
void FileOperationTestDialog::saveReport()
{
	const auto path = QFileDialog::getSaveFileName(
		this, tr("Save Test Report"), "MediaMuster-file-tests.json", tr("JSON report (*.json)"));
	if (path.isEmpty())
		return;
	QSaveFile file(path);
	const auto bytes = QJsonDocument(m_report.json).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
		QMessageBox::warning(this, tr("Report Not Saved"), file.errorString());
}
