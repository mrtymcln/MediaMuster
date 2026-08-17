#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

// MARK: - ProgressDialog

/// Modal progress sheet: detail line, bar, counter, Cancel. No title —
/// it is a sheet on the main window, and the console narrates the run.
/// Shared by scan and Manage Media ops (RebalanceDialog draws its own bar).
/// Cancel routes through our button so the worker always gets shut down.
class ProgressDialog : public QDialog
{
	Q_OBJECT
public:
	explicit ProgressDialog(QWidget *parent = nullptr);

	// MARK: - Lifecycle

	/// Show the sheet for a new run. The bar starts indeterminate and
	/// flips to determinate the moment `setProgress` arrives with a
	/// positive total.
	void begin();

	/// Update the bar. `total == 0` keeps it indeterminate; any
	/// positive total flips to determinate and shows 'N%' and 'N of N'.
	void setProgress(int current, int total);

	/// Detail line; usually the current file path, mid-elided to fit
	/// so head and tail stay visible.
	void setDetail(const QString &text);

	/// Hide the dialog. Caller is responsible for any final logging.
	void finish();

signals:
	/// The owning operation is expected to set its cancel atomic and
	/// wind down cooperatively; this dialog stays visible until
	/// `finish()` is called.
	void cancelRequested();

protected:
	/// Swallow the OS close button so an unfinished operation can't
	/// be hidden. Routed through the same cancel funnel as the button.
	void closeEvent(QCloseEvent *event) override;

	/// Esc maps to reject(), which skips closeEvent entirely — without
	/// this override Esc would hide the dialog while the operation kept
	/// running headless. Treated as a cancel request instead; the
	/// dialog stays visible until `finish()`.
	void reject() override;

private:
	/// One funnel for every cancel entry point (button, Esc, OS close
	/// button): disables the button, flips it to 'Cancelling...', and
	/// emits `cancelRequested` exactly once. Further requests are
	/// no-ops until `begin()` re-arms the button for the next run.
	void requestCancel();

	QLabel *m_detailLabel;
	QLabel *m_counterLabel;
	QProgressBar *m_bar;
	QPushButton *m_cancelBtn;
};