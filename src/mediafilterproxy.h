#pragma once

#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>

class MediaTableModel;
struct MediaFile;

/// Independent filters all have to pass: tab, projects, bin MOBs,
/// optional effect names/volume, and free text in visible string fields.
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
	/// clears both effect selections; setters cannot activate them while off.
	void setEffectDetailsEnabled(bool enabled);
	bool effectDetailsEnabled() const { return m_effectDetailsEnabled; }
	/// Exact names are ORed; any name or volume selection first requires a
	/// proven Precompute. Empty names means all names on the chosen volume.
	void setEffectFilter(const QStringList &effects);
	QStringList effectFilter() const;
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
	QSet<QString> m_selectedEffects;
	QString m_effectVolumePath;
	bool m_binFilterActive = false;
	QSet<QString> m_binFilterAcceptedMobs;
};
