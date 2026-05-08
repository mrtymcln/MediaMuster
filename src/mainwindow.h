#pragma once

#include "drivemanager.h"
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

class DriveListWidget : public QListWidget
{
	Q_OBJECT
public:
	explicit DriveListWidget(QWidget *parent = nullptr);

signals:
	void pathsDropped(const QStringList &paths);

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	void setDropHighlight(bool on);
};

class MediaTableModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	// Order: identity, context, properties.
	enum class Column
	{
		ClipName,
		FileName,
		Project,
		OriginalBin, // from MDB _ORG_BIN
		Kind,		 // Video / Audio
		Codec,
		Resolution,
		Fps,
		Duration,
		StartTC,
		SizeMB,
		Volume,
		Created,
		Source, // filename of source file before import
		Type,	// Media / Precomp
		Count_	// sentinel; keep last
	};

	// Unary + → int (Qt model APIs want int): +Column::ClipName.
	friend constexpr int operator+(Column c) noexcept
	{
		return static_cast<int>(c);
	}

	explicit MediaTableModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = {}) const override;
	int columnCount(const QModelIndex &parent = {}) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation,
						int role) const override;

	void setMediaFiles(const QVector<MediaFile> &files);
	const MediaFile &fileAt(int row) const;
	const QVector<MediaFile> &allFiles() const { return m_files; }
	void setShowRawCodecHex(bool on);

private:
	QVector<MediaFile> m_files;
	bool m_showRawCodecHex = false;
};

class MediaFilterProxy : public QSortFilterProxyModel
{
	Q_OBJECT
public:
	enum class FilterMode
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

	void setFilterMode(FilterMode mode);
	void setSearchText(const QString &text);
	void setProjectFilter(const QSet<QString> &projects);
	FilterMode filterMode() const { return m_mode; }

public slots:
	// isActive=false bypasses the filter; true requires mobId or compositionMobId in the set.
	void setBinFilterMobs(bool isActive, const QSet<QString> &acceptedMobs);

protected:
	bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
	bool lessThan(const QModelIndex &left,
				  const QModelIndex &right) const override;

private:
	FilterMode m_mode = FilterMode::All;
	QString m_search;
	QSet<QString> m_selectedProjects; // empty = show all
	bool m_binFilterActive = false;
	QSet<QString> m_binFilterAcceptedMobs;
};

class MainWindow : public QMainWindow
{
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	void onDetectDrives();
	void onScanClicked();
	void onScanAllClicked();
	void onScanProgress(const QString &phase, int current, int total,
						const QString &currentPath);
	void onScanLogBatch(const QVector<LogMsg> &batch);
	void onScanFinished(const QVector<MediaFile> &results);
	void onFilterChanged(int index);
	void onSearchChanged(const QString &text);
	void onSelectionChanged();
	void onCopyClicked();
	void onMoveClicked();
	void onDeleteClicked();
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
	void setupUi();
	void setupMenus();
	void setupConnections();
	void startScanWithPaths(const QStringList &paths);
	void addLog(int level, const QString &module, const QString &message);
	void updateStatusBar();
	void updateFilterCounts();
	void openManageMedia(int initialOp);
	void setBusy(bool busy);
	class ProgressDialog *progressDialog();
	QVector<MediaFile> selectedFiles() const;
	void addDrivePath(const QString &path);

	// Rebuild from scratch on any filter change — chips are 0-6, diffing isn't worth it.
	void rebuildFilterChips();

	static QIcon iconForDriveType(const QString &driveType,
								  const QString &path = {});
	static QIcon iconForProject(const QString &projectName);

	DriveManager *m_driveManager;
	MediaScanner *m_scanner;
	MediaManager *m_fileOps;

	MediaTableModel *m_model;
	MediaFilterProxy *m_proxy;

	QSplitter *m_mainSplitter;
	QWidget *m_sidePanel;
	DriveListWidget *m_driveList;
	QPushButton *m_scanButton;
	QPushButton *m_scanAllButton;
	QListWidget *m_projectList;
	QTabBar *m_filterTabs;
	QWidget *m_chipsBar = nullptr; // hidden when no filters active
	QLineEdit *m_searchField;
	QTableView *m_tableView;
	QPlainTextEdit *m_console;
	QSplitter *m_contentSplitter;

	class ProgressDialog *m_progressDialog = nullptr;

	QPushButton *m_btnFileOps;
	QPushButton *m_btnBinFilter;
	QPushButton *m_btnExport;
	QPushButton *m_btnRebalance;

	QLabel *m_statusFiles;
	QLabel *m_statusSelected;
	QLabel *m_statusSize;
	QLabel *m_statusSelSize;
	QLabel *m_statusScanTime;
	QLabel *m_statusSep1;
	QLabel *m_statusSep2;

	QVector<MediaFile> m_allFiles;
	QElapsedTimer m_scanTimer;
	bool m_showAllFilterTabs = false;
	QSet<QString> m_manualDrives;

	bool m_removeAfterOp = false;
	QSet<QString> m_successfulOpPaths;

	QString m_pendingOpTitle;

	class BinFilterDialog *m_binFilterDialog = nullptr;

	bool m_binFilterActive = false;
	int m_binFilterMobCount = 0;
};