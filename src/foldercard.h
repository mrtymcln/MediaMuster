#pragma once

#include "rebalanceplan.h"

#include <QFrame>

class FolderCard : public QFrame
{
	Q_OBJECT
public:
	explicit FolderCard(QWidget *parent = nullptr);
	void setFolder(const FolderState &fs);
	void setCurrentCount(int current);
	void markFinished();
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString m_folderName;
	bool m_inScope = true;
	bool m_isNew = false;
	bool m_finished = false;
	int m_currentCount = 0;
	int m_projectedCount = 0;
	int m_initialCount = 0;
};