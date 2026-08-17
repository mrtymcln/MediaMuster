#pragma once

#include <QDialog>

// MARK: - AboutDialog

/// The About box: app icon, name and version, then a credits roll that
/// starts scrolling once the dialog has settled and reveals a post-credits
/// line on its second lap.
///
/// Self-contained: it reads nothing from the main window and holds no state
/// worth querying, so callers just build it and show it (one line in
/// MainWindow::onAbout). It deletes itself on close (WA_DeleteOnClose) —
/// `new` it, show it, forget it.
class AboutDialog : public QDialog
{
	Q_OBJECT
public:
	explicit AboutDialog(QWidget *parent = nullptr);
};
