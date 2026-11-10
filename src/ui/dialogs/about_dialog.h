#pragma once

#include <QDialog>

// MARK: - About dialog

class AboutDialog : public QDialog
{
	Q_OBJECT
public:
	explicit AboutDialog(QWidget *parent = nullptr);
};
