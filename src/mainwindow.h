#pragma once

#include "mediafile.h"
#include "mediafilterproxy.h"
#include "mediamanager.h"
#include "mediascanner.h"
#include "mediatablemodel.h"
#include "oprecovery.h"
#include "volumelistwidget.h"
#include "volumemanager.h"

#include <QElapsedTimer>
#include <QItemSelection>
#include <QListWidget>
#include <QMainWindow>
#include <QSet>
#include <functional>

class QDialog;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QStatusBar;
class QTabBar;
class QTableView;
class QTimer;

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
	void onScanProgress(int current, int total, const QString &currentPath);
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
	void onInvertSelection();
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
	void startScanWithPaths(const QStringList &paths);
	void addLog(QtMsgType level, const QString &module, const QString &message);

	// MARK: - Crash recovery

	/// Run once at launch, off the GUI thread: rolls back any operation a
	/// previous run died partway through, deletes the clean journals, then
	/// reports via onRecoveryDone.
	void runStartupRecovery();
	void onRecoveryDone(const OpRecovery::Summary &summary);

	// MARK: - Resume an interrupted operation

	/// Re-reads the oplog folder for interrupted runs that can be finished,
	/// OFF the GUI thread (it stats media paths), then updates the menu
	/// item. Called after a resume, a discard, or a finished operation; the
	/// launch sweep supplies the first list for free.
	void refreshResumable();

	/// Applies the File-menu item's enabled state from the cached list.
	/// Cheap and synchronous — safe to call from setBusy on every edge.
	void updateResumeAction();

	/// Ask about the oldest resumable run: Resume (dispatch the unfinished
	/// files through the ordinary engine, then delete that journal), Resume
	/// Later (leave it; the menu command stays enabled), Discard (delete it).
	/// Esc and the close button count as Resume Later — never Discard.
	void offerResume();

	/// The one dispatch path for Copy/Move/Delete: journal-writable gate,
	/// engine call, busy state, progress sheet. Used by the Manage Media
	/// dialog and by Resume, so a resumed run is a normal run.
	/// Returns false when nothing was started (empty list, or the user
	/// declined the no-journal warning).
	bool dispatchOperation(OpJournal::Kind kind, QVector<MediaFile> files,
						   const QString &dest, bool preserve,
						   const QHash<QString, MediaManager::ConflictPolicy> &policies);

	/// Collects MacOS crash reports into the logs folder,
	/// and nudges the user to send them to the developer.
	void collectCrashReports();

	// MARK: - Status bar

	void updateStatusBar();
	void doUpdateStatusBar();

	void doUpdateSelectionBytes();

	void updateFilterCounts();
	void openManageMedia(int initialOp);
	void setBusy(bool busy);
	class ProgressDialog *progressDialog();
	QVector<MediaFile> selectedFiles() const;
	void addVolumePath(const QString &path);

	/// Build (without showing) the Project Summary dialog: its slot guards
	/// on an empty table first, then shows what this returns.
	QDialog *buildProjectSummaryDialog(const QVector<MediaFile> &files);

	/// The MediaFile behind a proxy row/index: maps proxy → source, then
	/// looks it up in the model. One funnel for the map-then-fetch dance
	/// that used to be copy-pasted across the selection / status / export
	/// paths; pairs with MediaTableModel::fileAt's bounds guard.
	const MediaFile &fileForProxyIndex(const QModelIndex &proxyIndex) const;
	const MediaFile &fileAtProxyRow(int proxyRow) const;

	/// A full-width (all columns) selection over the given ascending proxy
	/// rows, with contiguous runs coalesced into single ranges so the view
	/// gets one selectionChanged per run, not one per row. Shared by Select
	/// Relatives, Select Inverse, and the filter-restore path.
	QItemSelection selectionForRows(const QVector<int> &proxyRows) const;

	/// Sum of sizeBytes over the inclusive proxy-row range [first, last].
	/// An empty range (last < first) sums to 0. Backs both the table total
	/// and the per-selection total in the status bar.
	qint64 sumBytesInProxyRange(int first, int last) const;

	// MARK: - Selection persistence

	/// Wrap a proxy filter mutation so the user's selection survives
	/// the row-visibility shuffle. The proxy drops hidden rows from
	/// the selection model when filtered out; this helper re-applies
	/// the persistent path set after the mutation runs.
	void applyFilterPreservingSelection(const std::function<void()> &mutation);

	/// Preserves the editor's current selection across the refresh
	/// and merges manually-added paths from m_manualVolumes. Called
	/// from both onDetectVolumes (sync, startup / menu) and the
	/// volumesChanged handler (hot mount refresh from the async poller).
	void rebuildVolumeList(const QVector<VolumeInfo> &volumes);

	void rebuildFilterChips();

	/// Drop every active filter back to its default after a scan swaps in a
	/// new dataset, so a stale predicate (a project name or bin MOB from the
	/// previous scan) can't hide the fresh results.
	void resetFiltersForNewScan();

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

	QElapsedTimer m_scanTimer;
	bool m_showAllFilterTabs = false;
	QSet<QString> m_manualVolumes;

	// MARK: - Selection persistence

	/// File paths of every row the user has selected, tracked across
	/// filter changes. The selection model itself drops hidden rows
	/// when a filter excludes them; this set survives.
	QSet<QString> m_persistentSelectedPaths;

	/// Guards `onSelectionChanged` while `applyFilterPreservingSelection`
	/// shuffles rows; without this the rows the proxy drops would
	/// shrink the persistent record and lose the user's picks.
	bool m_inFilterRestore = false;

	/// Set by openManageMedia for Move/Delete (not Copy). Tells the
	/// operationFinished handler to remove rows for the paths we
	/// accumulated in m_successfulOpPaths.
	bool m_removeAfterOp = false;
	QSet<QString> m_successfulOpPaths;

	/// Interrupted runs that can be finished (see OpRecovery::pending),
	/// oldest first; drives the File > Resume Interrupted Operation item.
	QVector<OpRecovery::Resumable> m_resumable;
	class QAction *m_resumeAct = nullptr;

	class BinFilterDialog *m_binFilterDialog = nullptr;

	/// Cached so the chip strip can render without reaching into
	/// the dialog (which may not exist yet if not opened).
	bool m_binFilterActive = false;
	QStringList m_binFilterBinNames;

	// MARK: - Debounce timers

	/// Debounces the status bar's O(n) byte tally so bursts of
	/// filter changes coalesce into a single walk.
	QTimer *m_statusBarUpdateTimer = nullptr;

	/// Same pattern for the selected-bytes tally; every keystroke
	/// fires onSelectionChanged; the cheap state runs synchronously,
	/// the expensive byte sum waits for this timer.
	QTimer *m_selectionBytesTimer = nullptr;
};