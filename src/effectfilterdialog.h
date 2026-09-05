#pragma once

#include "mediafile.h"
#include "precomputefilter.h"

#include <QDialog>
#include <QHash>
#include <QVector>

class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

/// A draft, branch-based filter for already classified precomputes. Checking a
/// branch includes all its descendants; separate checked branches are ORed.
class EffectFilterDialog : public QDialog
{
	Q_OBJECT
public:
	explicit EffectFilterDialog(const QVector<MediaFile> &files,
								const PrecomputeFilter &selection, const QString &selectedVolume,
								QWidget *parent = nullptr);

	PrecomputeFilter precomputeFilter() const;
	QString selectedVolume() const;

private:
	struct Counts
	{
		qint64 total = 0;
		QHash<QString, qint64> volumes;
	};

	void buildTree(const QVector<MediaFile> &files);
	QTreeWidgetItem *addChoice(QTreeWidgetItem *parent, const QString &label,
							   const PrecomputeFilterPath &path);
	void applySelection(const PrecomputeFilter &selection);
	void setSubtreeChecked(QTreeWidgetItem *item, Qt::CheckState state);
	void updateParentChecks(QTreeWidgetItem *item);
	void collectSelection(QTreeWidgetItem *item, QVector<PrecomputeFilterPath> &paths) const;
	void updateMatchingCount();
	qint64 matchingCount(QTreeWidgetItem *item) const;

	QHash<QTreeWidgetItem *, PrecomputeFilterPath> m_paths;
	QHash<QTreeWidgetItem *, Counts> m_counts;
	QComboBox *m_volumes = nullptr;
	QLabel *m_matchCount = nullptr;
	QTreeWidget *m_effects = nullptr;
};
