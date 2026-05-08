#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class ProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProgressDialog(QWidget *parent = nullptr);

    // total == 0 leaves the bar indeterminate; any positive total flips
    // to determinate.
    void begin(const QString &title);
    void setProgress(int current, int total);
    void setDetail(const QString &text);
    void finish();

signals:
    void cancelRequested();

protected:
    // Swallow the OS close button — only the Cancel button aborts.
    void closeEvent(QCloseEvent *event) override;

private:
    QLabel *m_titleLabel;
    QLabel *m_detailLabel;
    QLabel *m_counterLabel;
    QProgressBar *m_bar;
    QPushButton *m_cancelBtn;
};