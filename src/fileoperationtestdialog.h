#pragma once
#include "backgroundjob.h"
#include "opdiagnostics.h"
#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;

class FileOperationTestDialog : public QDialog
{
	Q_OBJECT
  public:
	explicit FileOperationTestDialog(QWidget *parent = nullptr);
	~FileOperationTestDialog() override;
	void reject() override;

  private:
	void start();
	void saveReport();
	QLineEdit *m_source = nullptr;
	QLineEdit *m_destination = nullptr;
	QLineEdit *m_notes = nullptr;
	QLabel *m_samplesLabel = nullptr;
	QPlainTextEdit *m_output = nullptr;
	QPushButton *m_run = nullptr;
	QPushButton *m_cancel = nullptr;
	QPushButton *m_save = nullptr;
	QStringList m_samples;
	OpDiagnostics::Report m_report;
	bool m_running = false;
	BackgroundJob m_job{this};
};
