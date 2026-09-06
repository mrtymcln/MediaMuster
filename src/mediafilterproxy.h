#pragma once

#include "binfilter.h"
#include "precomputefilter.h"

#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>

class MediaTableModel;
struct MediaFile;

/// Independent filters all have to pass: tab, projects, bin MOBs,
/// optional precompute categories/effect categories/effect names/volume, and free text in visible string fields.
class MediaFilterProxy : public QSortFilterProxyModel
{
	Q_OBJECT
public:
	enum class FilterMode : int
	{
		All,
		Video,
		Audio,
		NoReference,
		NoProject,
		NoDatabase,
		InvalidUmid,
		NonPortable,
		Quarantined,
		Precompute
	};

	explicit MediaFilterProxy(QObject *parent = nullptr);

	// MARK: - Filter setters

	void setFilterMode(FilterMode mode);
	void setSearchText(const QString &text);
	void setProjectFilter(const QSet<QString> &projects);

	/// Experimental effect details are off by default. Turning them off
	/// clears every precompute detail selection; setters cannot activate them while off.
	void setEffectDetailsEnabled(bool enabled);
	bool effectDetailsEnabled() const { return m_effectDetailsEnabled; }
	/// Whole checked paths are ORed; the other filters still have to pass.
	/// This replaces the legacy independent category/name selections.
	void setPrecomputeTreeFilter(const PrecomputeFilter &filter);
	PrecomputeFilter precomputeTreeFilter() const { return m_precomputeTreeFilter; }
	/// Exact names are ORed; any name or volume selection first requires a
	/// proven Precompute. Empty names means all names on the chosen volume.
	void setEffectFilter(const QStringList &effects);
	QStringList effectFilter() const;
	/// Each dimension is ORed internally, and ANDed with the other filters.
	void setPrecomputeCategoryFilter(const QStringList &categories);
	QStringList precomputeCategoryFilter() const;
	void setEffectCategoryFilter(const QStringList &categories);
	QStringList effectCategoryFilter() const;
	/// The row's stored volumePath, not its display label. Empty means all.
	void setEffectVolumeFilter(const QString &volumePath);
	QString effectVolumeFilter() const { return m_effectVolumePath; }

	/// Caches the concrete model pointer for the hot
	/// filterAcceptsRow / lessThan paths; see m_sourceModel.
	void setSourceModel(QAbstractItemModel *sourceModel) override;

	/// True if `f` matches `mode` on the type/flag axis (the tab filter)
	/// alone — project, bin, and search are separate axes applied in
	/// filterAcceptsRow. Static and shared so MainWindow's tab-count
	/// tally can't drift from what the table actually shows.
	static bool matchesMode(FilterMode mode, const MediaFile &f);

public slots:
	/// Apply ordered bin operations to each row's file/master membership.
	void setBinFilter(const BinFilter &filter);

	/// isActive=false bypasses this check (matches everything);
	/// true requires the row's mobId or masterMobId to be in
	/// acceptedMobs.
	void setBinFilterMobs(bool isActive, const QSet<QString> &acceptedMobs);

protected:
	bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
	/// Cached on setSourceModel; avoids per-row qobject_cast in
	/// filterAcceptsRow and lessThan. Null if the source model
	/// isn't a MediaTableModel; the hot paths fall back gracefully.
	MediaTableModel *m_sourceModel = nullptr;

	FilterMode m_mode = FilterMode::All;
	QString m_search;
	/// `m_search` in Unicode NFC, precomputed by setSearchText.
	/// filterAcceptsRow compares NFC-on-NFC so composed keyboard input
	/// matches decomposed (NFD) filenames macOS volumes hand back.
	QString m_searchNfc;
	QSet<QString> m_selectedProjects;
	bool m_effectDetailsEnabled = false;
	PrecomputeFilter m_precomputeTreeFilter;
	QSet<QString> m_selectedEffects;
	QSet<QString> m_selectedPrecomputeCategories;
	QSet<QString> m_selectedEffectCategories;
	QString m_effectVolumePath;
	BinFilter m_binFilter;
};
