#pragma once

#include "mediafile.h"

#include <QDialog>
#include <QSet>
#include <QStringList>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

/// Select effects observed in the current scan, optionally on one volume.
/// This labels and filters known precomputes; it does not classify media.
class EffectFilterDialog : public QDialog
{
	Q_OBJECT
public:
	explicit EffectFilterDialog(const QVector<MediaFile> &files,
		const QStringList &selectedEffects, const QString &selectedVolume,
		QWidget *parent = nullptr);

	QStringList selectedEffects() const;
	QString selectedVolume() const;

private:
	void rebuildEffects();
	void filterChoices();
	void updateSelectionCount();

	QVector<MediaFile> m_files;
	QSet<QString> m_selectedEffects;
	QComboBox *m_volumes = nullptr;
	QLineEdit *m_search = nullptr;
	QTreeWidget *m_effects = nullptr;
	QLabel *m_summary = nullptr;
};
