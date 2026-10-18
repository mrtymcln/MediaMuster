#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

// MARK: - ProgressDialog

/// Modal progress sheet shared by scans and file jobs.
/// Cancel requests cooperative shutdown; the owner hides the sheet after completion.
class ProgressDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ProgressDialog(QWidget *parent = nullptr);

	// MARK: - Lifecycle

	/// Start a run with indeterminate progress.
	void begin();

	/// A positive total enables determinate progress; otherwise keep it indeterminate.
	void setProgress(int current, int total);

	/// Include the current work unit's percentage (0–100) in overall progress.
	/// Work units may include separate copy/removal phases for one file.
	void setItemProgress(int current, int total, double pct);

	/// Mid-elide detail text so both ends of a long path stay visible.
	void setDetail(const QString &text);

	/// Hide the dialog. Caller is responsible for any final logging.
	void finish();

signals:
	/// The owner requests cooperative cancellation, then calls finish() when done.
	void cancelRequested();

protected:
	/// Closing requests Cancel and keeps the dialog visible until finish().
	void closeEvent(QCloseEvent *event) override;

	/// Escape bypasses closeEvent, so handle it as Cancel too.
	void reject() override;

private:
	/// Disable Cancel and emit once per run, whichever entry point requests it.
	void requestCancel();

	QLabel *m_detailLabel;
	QLabel *m_counterLabel;
	QProgressBar *m_bar;
	QPushButton *m_cancelBtn;
};
