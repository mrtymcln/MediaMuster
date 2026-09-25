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
		NoDatabase,
		NonPortable,
		Quarantined,
		Precompute
	};

	explicit MediaFilterProxy(QObject *parent = nullptr);

	// MARK: - Filter setters

	void setFilterMode(FilterMode mode);
	void setSearchText(const QString &text);
	void setProjectFilter(const QSet<QString> &projects);

	/// Experimental precompute features are off by default. Turning them off
	/// resets Precompute mode to All and clears every detail selection;
	/// setters cannot activate them while off.
	void setPrecomputesEnabled(bool enabled);
	bool precomputesEnabled() const { return m_precomputesEnabled; }
	/// Whole checked paths are ORed; the other filters still have to pass.
	void setPrecomputeTreeFilter(const PrecomputeFilter &filter);
	PrecomputeFilter precomputeTreeFilter() const { return m_precomputeTreeFilter; }
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

protected:
	bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
	/// Cached concrete model pointer for filtering and sorting. Other model
	/// types have all rows rejected; sorting falls back to QSortFilterProxyModel.
	MediaTableModel *m_sourceModel = nullptr;

	FilterMode m_mode = FilterMode::All;
	/// Search text in Unicode NFC, precomputed by setSearchText.
	/// filterAcceptsRow compares NFC-on-NFC so composed keyboard input
	/// matches decomposed (NFD) filenames macOS volumes hand back.
	QString m_searchNfc;
	QSet<QString> m_selectedProjects;
	bool m_precomputesEnabled = false;
	PrecomputeFilter m_precomputeTreeFilter;
	QString m_effectVolumePath;
	BinFilter m_binFilter;
};
