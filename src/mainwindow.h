#pragma once

#include "managemediadialog.h"
#include "mediafile.h"
#include "mediafilterproxy.h"
#include "mediascanner.h"
#include "mediatablemodel.h"
#include "fileoperationcontroller.h"
#include "volumelistwidget.h"
#include "volumemanager.h"

#include <QElapsedTimer>
#include <QItemSelection>
#include <QListWidget>
#include <QMainWindow>
#include <QSet>
#include <functional>

class QLabel;
class QLayout;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStatusBar;
class QTabBar;
class QTableView;
class QTimer;

// MARK: - MainWindow

/// Composes volume discovery, scanning and operation controllers with
/// MediaTableModel + MediaFilterProxy, and on-demand dialogs.
/// Owns window widgets, selection and presentation of the current media set.
class MainWindow : public QMainWindow
{
	Q_OBJECT
	friend class TestOperationUi;

public:
	/// UiOnly supports interface tests/previews without volume discovery,
	/// crash collection or journal recovery against the host.
	enum class StartupMode
	{
		Normal,
		UiOnly
	};
	explicit MainWindow(QWidget *parent = nullptr, StartupMode startup = StartupMode::Normal);
	~MainWindow();

private slots:

	// MARK: - User actions

	void refreshVolumes();
	void scanSelected();
	void scanEverything();
	void onScanDiscovering(const QString &path);
	void onScanProgress(int current, int total, const QString &currentPath);
	void onScanLogBatch(const QVector<LogMessage> &batch);
	void onScanFinished(const QVector<MediaFile> &results);
	void onScanFailed(const QString &message);
	void onFilterChanged(int index);
	void onSearchChanged(const QString &text);
	void onSelectionChanged();
	void onFileOperations();
	void onExportCsv();
	void onRevealInFinder();
	void onSelectRelatives();
	void onInvertSelection();
	void autoFitColumns();
	void onTableDoubleClicked(const QModelIndex &index);
	void onPathsDropped(const QStringList &paths);
	void onFilterByBins();
	void onFilterPrecomputes();
	void onRebalance();
	void onAbout();
	void showMediaMusterTrashDialog(const QString &trashFolderPath, int fileCount);
	void showTableContextMenu(const QPoint &pos);

private:
	// MARK: - Setup

	void setupUi();
	void buildSidePanel();
	QWidget *buildToolbar();
	void buildTable();
	void buildConsole();
	void buildStatusBar();

	void setupMenus();
	void buildFileMenu();
	void buildEditMenu();
	void buildViewMenu();
	void buildSpecialMenu();
	void buildDebugMenu();
	void buildHelpMenu();

	void setupConnections();
	void scanListedPaths(const QStringList &paths);
	void rescanPaths(const QStringList &paths);
	void startScan(const QVector<MediaScanner::Location> &locations);
	void addLog(QtMsgType level, const QString &module, const QString &message);

	/// Adapt the selected media rows to the controller's request format.
	bool dispatchOperation(OpKind kind, QVector<MediaFile> files,
						   const QString &dest, bool preserve,
						   const QHash<QString, ConflictPolicy> &policies);

	/// Collect macOS crash reports and offer to reveal them for reporting.
	void collectCrashReports();

	// MARK: - Status bar

	void updateStatusBar();
	void doUpdateStatusBar();

	void doUpdateSelectionBytes();

	void updateFilterCounts();
	void openManageMedia(ManageMediaDialog::Operation initialOp);
	void updateActivityUi();
	void updateSelectionActions();
	class ProgressDialog *progressDialog();
	/// Only visible selected rows become operation/export inputs. Remembered
	/// hidden selections are restored to the view when filters permit them.
	QVector<MediaFile> selectedFiles() const;
	void addVolumePath(const QString &path);

	/// Apply the same presentation to detected volumes and manual folders.
	/// displayName includes any suffix needed to distinguish duplicate names.
	class QListWidgetItem *makeVolumeItem(const VolumeInfo &v, const QString &displayName);

	/// Map visible rows back to the inventory before reading their metadata.
	const MediaFile &fileForProxyIndex(const QModelIndex &proxyIndex) const;
	const MediaFile &fileAtProxyRow(int proxyRow) const;

	/// Coalesce ascending visible rows into ranges to reduce selection signals.
	QItemSelection selectionForRows(const QVector<int> &proxyRows) const;

	/// Sum sizeBytes over inclusive proxy rows [first, last]; empty ranges sum to zero.
	qint64 sumBytesInProxyRange(int first, int last) const;

	// MARK: - Selection persistence

	/// Restore selections by path after filtering drops hidden rows from Qt's selection model.
	void applyFilterPreservingSelection(const std::function<void()> &mutation);

	/// Keeps manually-added paths when detected volumes change.
	void rebuildVolumeList(const QVector<VolumeInfo> &volumes);

	void addFilterChip(QLayout *layout, const QString &text, std::function<void()> onClose);
	QString precomputeSelectionLabel(const PrecomputeFilter &filter) const;
	QString precomputeVolumeLabel(const QString &volumePath) const;
	void rebuildFilterChips();
	/// Refresh dependent views after the inventory changes; does not scan disk.
	void refreshEverything();
	void rebuildProjectList();
	void setOmfScanEnabled(bool enabled);
	void setPrecomputesEnabled(bool enabled);

	/// Reset filters so criteria from the previous scan cannot hide new results.
	void resetFiltersForNewScan();

	// MARK: - Owned services

	VolumeManager *m_volumeManager;
	MediaScanner *m_scanner;
	FileOperationController *m_operations;

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

	QPushButton *m_manageMediaButton;
	QPushButton *m_binFilterButton;
	QPushButton *m_precomputeFilterButton = nullptr;
	QPushButton *m_exportCsvButton;
	QPushButton *m_rebalanceButton;

	// Shared commands keep menu and button availability in sync.
	class QAction *m_addFolderAction = nullptr;
	class QAction *m_refreshVolumesAction = nullptr;
	class QAction *m_scanSelectedAction = nullptr;
	class QAction *m_scanAllAction = nullptr;
	class QAction *m_manageMediaAction = nullptr;
	class QAction *m_binFilterAction = nullptr;
	class QAction *m_rebalanceAction = nullptr;
	class QAction *m_exportAction = nullptr;
	class QAction *m_revealAction = nullptr;
	class QAction *m_selectRelativesAction = nullptr;
	class QAction *m_selectInverseAction = nullptr;
	bool m_exportInProgress = false;
	bool m_selectionHasMasterMob = false;

	// MARK: - Status-bar labels

	QLabel *m_statusFiles;
	QLabel *m_statusSelected;
	QLabel *m_statusSize;
	QLabel *m_statusSelSize;
	QLabel *m_statusScanTime;
	QLabel *m_statusSep1;
	QLabel *m_statusSep2;

	// MARK: - Scan state

	QElapsedTimer m_scanTimer;
	bool m_omfScanEnabled = false;
	bool m_precomputesEnabled = false;
	class QAction *m_precomputeFilterAction = nullptr;
	/// Paths found by the last startup/Refresh detection; empty is a valid result.
	QStringList m_detectedVolumePaths;
	/// Session-only manual paths and their admitted tree/base meaning.
	QHash<QString, MediaScanner::Location::Kind> m_manualVolumes;

	// MARK: - Selection persistence

	/// Selected paths survive filtering, including rows hidden from Qt's selection model.
	QSet<QString> m_persistentSelectedPaths;

	/// Ignore selection notifications caused by filter restoration.
	bool m_inFilterRestore = false;

	class AvbFilterDialog *m_binFilterDialog = nullptr;

	/// Chip state remains available without constructing the dialog.
	bool m_binFilterActive = false;
	bool m_binFilterIncomplete = false;
	QStringList m_binFilterBinNames;

	// MARK: - Debounce timers

	/// Debounces typed searches; stopped when a new scan clears the search.
	QTimer *m_searchDebounceTimer = nullptr;

	/// Coalesce repeated status-bar byte tallies after filter changes.
	QTimer *m_statusBarUpdateTimer = nullptr;

	/// Defer the selected-byte tally; selection controls update immediately.
	QTimer *m_selectionBytesTimer = nullptr;
};
