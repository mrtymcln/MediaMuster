#pragma once

#include "ui/dialogs/manage_media_dialog.h"
#include "media/media_file_record.h"
#include "media_filter_proxy.h"
#include "ui/scan_manager.h"
#include "media_table_model.h"
#include "ui/file_operation_controller.h"
#include "volume_list_widget.h"
#include "storage/volume_detector.h"

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
	/// UiOnly supports interface tests without volume discovery,
	/// crash collection or journal recovery against the host.
	enum class StartupMode
	{
		Normal,
		UiOnly
	};

	// MARK: - Lifecycle

	explicit MainWindow(QWidget *parent = nullptr, StartupMode startup = StartupMode::Normal);
	~MainWindow();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void closeEvent(QCloseEvent *event) override;

private slots:

	// MARK: - Volume list management

	void onAddVolume();
	void refreshVolumes();
	void onPathsDropped(const QStringList &paths);

	// MARK: - User actions

	void scanSelected();
	void scanEverything();
	void onScanDiscovering(const QString &path);
	void onScanLogBatch(const QVector<LogMessage> &batch);
	void onScanFinished(const QVector<MediaFileRecord> &results);
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
	void onFilterByBins();
	void onFilterPrecomputes();
	void onRebalance();
	void onAbout();
	void showMediaMusterTrashDialog(const QString &trashFolderPath, int fileCount);
	void showTableContextMenu(const QPoint &pos);

private:
	// MARK: - Volume list management

	void setupVolumeConnections();
	void addVolumePath(const QString &path);
	/// Apply the same presentation to detected volumes and manual folders.
	/// displayName includes any suffix needed to distinguish duplicate names.
	class QListWidgetItem *makeVolumeItem(const VolumeInfo &v, const QString &displayName);
	/// Keeps manually-added paths when detected volumes change.
	void rebuildVolumeList(const QVector<VolumeInfo> &volumes);

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
	void buildHelpMenu();

	void setupConnections();

	// MARK: - Scan control and reporting

	void scanListedPaths(const QStringList &paths);
	void rescanPaths(const QStringList &paths);
	void startScan(const QVector<ScanManager::Location> &locations);
	void addLog(QtMsgType level, const QString &module, const QString &message);
	enum class BellaClose
	{
		None,
		Window,
		Quit
	};
	void requestBellaClose(BellaClose close);
	void finishBellaClose();
	void onBellaScanProgress(const QString &stage, qsizetype completed, qsizetype total, const QString &path);

	// MARK: - File operation dispatch

	/// Adapt the selected media rows to the controller's request format.
	bool dispatchOperation(OperationKind kind, QVector<MediaFileRecord> files,
						   const QString &dest, bool preserve,
						   const QHash<QString, ConflictPolicy> &policies);

	// MARK: - Crash reports

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

	// MARK: - Selected media and row lookup

	/// Only visible selected rows become operation/export inputs. Remembered
	/// hidden selections are restored to the view when filters permit them.
	QVector<MediaFileRecord> selectedFiles() const;

	/// Map visible rows back to the inventory before reading their metadata.
	const MediaFileRecord &fileForProxyIndex(const QModelIndex &proxyIndex) const;
	const MediaFileRecord &fileAtProxyRow(int proxyRow) const;

	/// Coalesce ascending visible rows into ranges to reduce selection signals.
	QItemSelection selectionForRows(const QVector<int> &proxyRows) const;

	/// Sum sizeBytes over inclusive proxy rows [first, last]; empty ranges sum to zero.
	qint64 sumBytesInProxyRange(int first, int last) const;

	// MARK: - Selection persistence

	/// Restore selections by path after filtering drops hidden rows from Qt's selection model.
	void applyFilterPreservingSelection(const std::function<void()> &mutation);

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

	VolumeDetector *m_volumeDetector;
	ScanManager *m_scanner;
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
	/// File jobs update rows immediately and refresh inventory summaries at finish.
	bool m_operationSummariesDirty = false;
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
	BellaClose m_bellaClose = BellaClose::None;
	bool m_bellaCancelling = false;
	bool m_omfScanEnabled = false;
	bool m_precomputesEnabled = false;
	class QAction *m_precomputeFilterAction = nullptr;

	// MARK: - Volume list state

	/// Paths found by the last startup/Refresh detection; empty is a valid result.
	QStringList m_detectedVolumePaths;
	/// Session-only manual paths and their admitted tree/base meaning.
	QHash<QString, ScanManager::Location::Kind> m_manualVolumes;

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
