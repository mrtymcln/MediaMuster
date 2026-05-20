#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

// MARK: - ProgressDialog

/// Modal progress dialog: bar, title, details, counter, Cancel.
/// Shared by scan, file ops, and rebalance.
/// Cancel routes through our button so the worker always gets shut down.
class ProgressDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ProgressDialog(QWidget *parent = nullptr);

	// MARK: - Lifecycle

	/// Show the dialog with the given title. The bar starts
	/// indeterminate and flips to determinate the
	/// moment `setProgress` arrives with a positive total.
	void begin(const QString &title);

	/// Update the bar. `total == 0` keeps it indeterminate; any
	/// positive total flips to determinate and displays "N% — N of N".
	void setProgress(int current, int total);

	/// Details under the title — usually the current file path,
	/// mid-elided to fit so head and tail stay visible.
	void setDetail(const QString &text);

	/// Hide the dialog. Caller is responsible for any final logging.
	void finish();

signals:
	/// The owning operation is expected to set its cancel atomic and
	/// wind down cooperatively — this dialog stays visible until
	/// `finish()` is called.
	void cancelRequested();

protected:
	/// Swallow the OS close button so an unfinished operation can't
	/// be hidden. The button emits `cancelRequested` instead.
	void closeEvent(QCloseEvent *event) override;

private:
	QLabel *m_titleLabel;
	QLabel *m_detailLabel;
	QLabel *m_counterLabel;
	QProgressBar *m_bar;
	QPushButton *m_cancelBtn;
};