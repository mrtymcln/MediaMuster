#pragma once

#include "volumemanager.h"
#include "mediamanager.h"
#include "mediafile.h"
#include "mediascanner.h"

#include <QAbstractTableModel>
#include <QElapsedTimer>
#include <QIcon>
#include <QListWidget>
#include <QMainWindow>
#include <QSet>
#include <QSortFilterProxyModel>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QMimeData;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStatusBar;
class QTabBar;
class QTableView;
class QTimer;

// MARK: - VolumeListWidget

class VolumeListWidget : public QListWidget
{
	Q_OBJECT
public:
	explicit VolumeListWidget(QWidget *parent = nullptr);

signals:
	/// Filtered to existing, readable directories.
	void pathsDropped(const QStringList &paths);

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	void setDropHighlight(bool on);
};

// MARK: - MediaTableModel

/// One row per MediaFile. Columns ordered identity > context >
/// technical, matching the visual order in the UI.
class MediaTableModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	// MARK: - Columns

	enum class Column : int
	{
		ClipName,
		FileName,
		Project,
		OriginalBin,
		Kind,
		Codec,
		Resolution,
		Fps,
		Duration,
		StartTC,
		SizeMB,
		Volume,
		Created,
		Source,
		Type,
		Count_
	};

	/// Unary + so Qt model APIs (which want int) accept
	/// `+Column::ClipName` directly.
	friend constexpr int operator+(Column c) noexcept
	{
		return static_cast<int>(c);
	}

	explicit MediaTableModel(QObject *parent = nullptr);

	// MARK: - QAbstractItemModel overrides

	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
	                    int role) const override;

	// MARK: - Bulk updates

	void setMediaFiles(const QVector<MediaFile> &files);

	/// Groups contiguous deletions into single beginRemoveRows /
	/// endRemoveRows ranges, so the view is preserved.
	void removeFilesByPath(const QSet<QString> &paths);

	const MediaFile &fileAt(int row) const;
	const QVector<MediaFile> &allFiles() const
	{
		return m_files;
	}

	/// Debug toggle: Codec column shows the raw 16-byte
	/// essence-container UL hex instead of the resolved codec name.
	void setShowRawCodecHex(bool on);

private:
	QVector<MediaFile> m_files;
	bool m_showRawCodecHex = false;
};

// MARK: - MediaFilterProxy

/// Four independent filters all have to pass for a row to make it
/// through: FilterMode tab, project list, bin filter MOB set, and
/// free text search across the visible string columns.
class MediaFilterProxy : public QSortFilterProxyModel
{
	Q_OBJECT
public:
	enum class FilterMode : int
	{
		All,
		Video,
		Audio,
		Unmanaged,
		BadUmid,
		Unreferenced,
		NonPortable,
		Quarantined
	};

	explicit MediaFilterProxy(QObject *parent = nullptr);

	// MARK: - Filter setters

	void setFilterMode(FilterMode mode);
	void setSearchText(const QString &text);
	void setProjectFilter(const QSet<QString> &projects);
	FilterMode filterMode() const
	{
		return m_mode;
	}

public slots:
	/// isActive=false bypasses this check (matches everything);
	/// true requires the row's mobId or compositionMobId to be in
	/// acceptedMobs.
	void setBinFilterMobs(bool isActive, const QSet<QString> &acceptedMobs);

protected:
	bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
	bool lessThan(const QModelIndex &left,
	              const QModelIndex &right) const override;

private:
	FilterMode m_mode = FilterMode::All;
	QString m_search;
	QSet<QString> m_selectedProjects;
	bool m_binFilterActive = false;
	QSet<QString> m_binFilterAcceptedMobs;
};

// MARK: - MainWindow

/// Composes VolumeManager, MediaScanner, MediaManager,
/// MediaTableModel + MediaFilterProxy, and on-demand dialogs.
/// Most of the work happens in the slot handlers; this class is
/// mostly wiring and UI assembly.
class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:

	// MARK: - User actions

	void onDetectVolumes();
	void onScanClicked();
	void onScanAllClicked();
	void onScanProgress(const QString &phase, int current, int total,
	                    const QString &currentPath);
	void onScanLogBatch(const QVector<LogMsg> &batch);
	void onScanFinished(const QVector<MediaFile> &results);
	void onFilterChanged(int index);
	void onSearchChanged(const QString &text);
	void onSelectionChanged();
	void onFileOperations();
	void onExportCsv();
	void onProjectSummary();
	void onRevealInFinder();
	void onSelectRelatives();
	void autoFitColumns();
	void onTableDoubleClicked(const QModelIndex &index);
	void onPathsDropped(const QStringList &paths);
	void onCheckPermissions();
	void onFilterByBins();
	void onRebalance();
	void onAbout();
	void showMediaMusterTrashDialog(const QString &trashFolderPath, int fileCount);
	void showTableContextMenu(const QPoint &pos);

private:
	// MARK: - Setup

	void setupUi();
	void setupMenus();
	void setupConnections();
	void startScanWithPaths(const QStringList &paths);
	void addLog(int level, const QString &module, const QString &message);

	// MARK: - Status bar

	/// 100 ms debounced refresh. The actual O(n) walk happens in
	/// doUpdateStatusBar after the filter changes.
	void updateStatusBar();
	void doUpdateStatusBar();

	/// Debounced byte-sum for the 'x MB selected' indicator for speed.
	void doUpdateSelectionBytes();

	void updateFilterCounts();
	void openManageMedia(int initialOp);
	void setBusy(bool busy);
	class ProgressDialog *progressDialog();
	QVector<MediaFile> selectedFiles() const;
	void addVolumePath(const QString &path);

	/// Preserves the editor's current selection across the refresh
	/// and merges manually-added paths from m_manualVolumes. Called
	/// from both onDetectVolumes (sync, startup / menu) and the
	/// volumesChanged handler (hot mount refresh from the async poller).
	void rebuildVolumeList(const QVector<VolumeInfo> &volumes);

	void rebuildFilterChips();

	// MARK: - Icon helpers

	static QIcon iconForVolumeType(const QString &volumeType,
	                               const QString &path = {});
	static QIcon iconForProject(const QString &projectName);

	// MARK: - Owned services

	VolumeManager *m_volumeManager;
	MediaScanner *m_scanner;
	MediaManager *m_fileOps;

	MediaTableModel *m_model;
	MediaFilterProxy *m_proxy;

	// MARK: - Widgets

	QSplitter *m_mainSplitter;
	QWidget *m_sidePanel;
	VolumeListWidget *m_volumeList;
	QPushButton *m_scanButton;
	QPushButton *m_scanAllButton;
	QListWidget *m_projectList;
	QTabBar *m_filterTabs;
	QWidget *m_chipsBar = nullptr; ///< Hidden when no filters are active.
	QLineEdit *m_searchField;
	QTableView *m_tableView;
	QPlainTextEdit *m_console;
	QSplitter *m_contentSplitter;

	class ProgressDialog *m_progressDialog = nullptr;

	QPushButton *m_btnFileOps;
	QPushButton *m_btnBinFilter;
	QPushButton *m_btnExport;
	QPushButton *m_btnRebalance;

	// MARK: - Status-bar labels

	QLabel *m_statusFiles;
	QLabel *m_statusSelected;
	QLabel *m_statusSize;
	QLabel *m_statusSelSize;
	QLabel *m_statusScanTime;
	QLabel *m_statusSep1;
	QLabel *m_statusSep2;

	// MARK: - Scan state

	QVector<MediaFile> m_allFiles;
	QElapsedTimer m_scanTimer;
	bool m_showAllFilterTabs = false;
	QSet<QString> m_manualVolumes;

	/// Set by openManageMedia for Move/Delete (not Copy). Tells the
	/// operationFinished handler to prune the table by the paths we
	/// accumulated in m_successfulOpPaths.
	bool m_removeAfterOp = false;
	QSet<QString> m_successfulOpPaths;

	QString m_pendingOpTitle;

	class BinFilterDialog *m_binFilterDialog = nullptr;

	/// Cached so the filter strip can render without reaching into
	/// the dialog (which may not exist yet if not opened).
	bool m_binFilterActive = false;
	int m_binFilterMobCount = 0;

	// MARK: - Debounce timers

	/// Debounces the status bar's O(n) byte tally so bursts of
	/// filter changes coalesce into a single walk.
	QTimer *m_statusBarUpdateTimer = nullptr;

	/// Same pattern for the selected-bytes tally — every keystroke
	/// fires onSelectionChanged; the cheap state runs synchronously,
	/// the expensive byte sum waits for this timer.
	QTimer *m_selectionBytesTimer = nullptr;
};