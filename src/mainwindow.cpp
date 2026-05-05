#include "mainwindow.h"
#include "binfilterdialog.h"
#include "debugslowdown.h"
#include "managemediadialog.h"
#include "progressdialog.h"
#include "rebalancedialog.h"
#include "rebalancer.h"
#include "theme.h"
#include "version.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QShortcut>
#include <QStorageInfo>
#include <QTextStream>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
#include <functional>
#include <QSysInfo>
#include <QOperatingSystemVersion>
#include <QTimer>
#include "csvutil.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>	  // SHOpenFolderAndSelectItems, SHParseDisplayName
#include <shellapi.h> // (kept for any future shell-verb fallbacks)
#include <objbase.h>  // CoInitializeEx / CoUninitialize — Shell API needs COM
#include <QSettings>  // registry read for Windows DisplayVersion + UBR
#endif

// Widget headers (forward-declared in mainwindow.h).
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTableView>

DriveListWidget::DriveListWidget(QWidget *parent) : QListWidget(parent)
{
	setAcceptDrops(true);
	setDragDropMode(QAbstractItemView::DropOnly);
}

void DriveListWidget::setDropHighlight(bool on)
{
	if (on)
	{
		setStyleSheet(
			"QListWidget { border: 2px solid #4A90E2; "
			"background-color: rgba(74, 144, 226, 0.08); }");
	}
	else
	{
		setStyleSheet({});
	}
}

void DriveListWidget::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
	{
		for (const QUrl &url : event->mimeData()->urls())
		{
			if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir())
			{
				event->acceptProposedAction();
				setDropHighlight(true);
				return;
			}
		}
	}
	event->ignore();
}

void DriveListWidget::dragMoveEvent(QDragMoveEvent *event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void DriveListWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
	setDropHighlight(false);
	QListWidget::dragLeaveEvent(event);
}

void DriveListWidget::dropEvent(QDropEvent *event)
{
	setDropHighlight(false);
	QStringList paths;
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (url.isLocalFile())
		{
			QString path = url.toLocalFile();
			if (QFileInfo(path).isDir())
				paths.append(path);
		}
	}
	if (!paths.isEmpty())
	{
		emit pathsDropped(paths);
		event->acceptProposedAction();
	}
}

MediaTableModel::MediaTableModel(QObject *parent)
	: QAbstractTableModel(parent) {}

int MediaTableModel::rowCount(const QModelIndex &) const
{
	return static_cast<int>(m_files.size());
}
int MediaTableModel::columnCount(const QModelIndex &) const
{
	return +Column::Count_;
}

void MediaTableModel::setMediaFiles(const QVector<MediaFile> &files)
{
	beginResetModel();
	m_files = files;
	endResetModel();
}

const MediaFile &MediaTableModel::fileAt(int row) const { return m_files[row]; }

void MediaTableModel::setShowRawCodecHex(bool on)
{
	if (m_showRawCodecHex == on)
		return;
	m_showRawCodecHex = on;
	if (!m_files.isEmpty())
		emit dataChanged(index(0, +Column::Codec), index(m_files.size() - 1, +Column::Codec), {Qt::DisplayRole});
}

QVariant MediaTableModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= static_cast<int>(m_files.size()))
		return {};
	const MediaFile &f = m_files[index.row()];

	if (role == Qt::DisplayRole)
	{
		switch (static_cast<Column>(index.column()))
		{
		case Column::ClipName:
			return f.clipName.isEmpty() ? f.fileName : f.clipName;
		case Column::FileName:
			return f.fileName;
		case Column::Project:
			return f.project;
		case Column::OriginalBin:
			return f.originalBin;
		case Column::Kind:
			return f.mediaType == MediaFile::Type::Audio ? "Audio" : "Video";
		case Column::Codec:
			if (m_showRawCodecHex && !f.codecHex.isEmpty())
				return f.codecHex;
			return f.codec;
		case Column::Resolution:
			return f.resolution;
		case Column::Fps:
			return f.fps;
		case Column::Duration:
			return f.durationDisplay();
		case Column::StartTC:
			return f.startTimecode;
		case Column::SizeMB:
			return QString::number(f.sizeMB, 'f', 1);
		case Column::Volume:
			// "DATA / 2", "Nexis / zzz1" — pre-computed in ScannerWorker::buildMediaFile.
			return f.volumeDisplay;
		case Column::Created:
			return f.created.isValid()
					   ? f.created.toString("yyyy-MM-dd HH:mm")
					   : f.modified.toString("yyyy-MM-dd HH:mm");
		case Column::Source:
			// Basename in the cell; full path in the tooltip below.
			return f.sourceFileName;
		case Column::Type:
			return f.kind; // "Media" or "Precomp"
		case Column::Count_:
			break;
		}
	}
	if (role == Qt::TextAlignmentRole && static_cast<Column>(index.column()) == Column::SizeMB)
		return QVariant(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
	if (role == Qt::ToolTipRole)
	{
		// Source tooltip = full path + container format.
		if (static_cast<Column>(index.column()) == Column::Source && !f.sourceFilePath.isEmpty())
		{
			QString tip = f.sourceFilePath;
			if (!f.sourceContainer.isEmpty())
				tip += QStringLiteral("\n(%1)").arg(f.sourceContainer);
			return tip;
		}
		return {};
	}
	if (role == Qt::UserRole)
	{
		if (static_cast<Column>(index.column()) == Column::SizeMB)
			return f.sizeMB;
		if (static_cast<Column>(index.column()) == Column::Created)
			return f.created.isValid() ? f.created : f.modified;
		return data(index, Qt::DisplayRole);
	}
	return {};
}

QVariant MediaTableModel::headerData(int section, Qt::Orientation orientation,
									 int role) const
{
	if (orientation != Qt::Horizontal)
		return {};

	if (role == Qt::ToolTipRole)
	{
		if (section == +Column::OriginalBin)
		{
			return tr(
				"The bin this clip was originally imported into.");
		}
		if (section == +Column::Volume)
		{
			return tr(
				"The volume filepath for this clip.");
		}
		if (section == +Column::Created)
		{
			return tr(
				"Falls back to Date Modified, if Date Created is unavailable.");
		}
		return {};
	}

	if (role != Qt::DisplayRole)
		return {};

	const char *headers[] = {"Clip Name", "Filename", "Project", "Original Bin",
							 "Kind", "Codec", "Resolution", "FPS",
							 "Duration", "Start TC", "Size (MB)",
							 "Volume", "Date Created", "Source", "Type"};
	if (section >= 0 && section < +Column::Count_)
		return QString(headers[section]);
	return {};
}

MediaFilterProxy::MediaFilterProxy(QObject *parent)
	: QSortFilterProxyModel(parent) {}

void MediaFilterProxy::setFilterMode(FilterMode mode)
{
	m_mode = mode;
	invalidateRowsFilter();
}
void MediaFilterProxy::setSearchText(const QString &text)
{
	m_search = text.toLower();
	invalidateRowsFilter();
}

void MediaFilterProxy::setProjectFilter(const QSet<QString> &projects)
{
	m_selectedProjects = projects;
	invalidateRowsFilter();
}

void MediaFilterProxy::setBinFilterMobs(bool isActive,
										const QSet<QString> &acceptedMobs)
{
	// Clear the set when inactive so the memory isn't held.
	m_binFilterActive = isActive;
	m_binFilterAcceptedMobs = isActive ? acceptedMobs : QSet<QString>{};
	invalidateRowsFilter();
}

bool MediaFilterProxy::filterAcceptsRow(int row,
										const QModelIndex &parent) const
{
	Q_UNUSED(parent);
	auto *model = qobject_cast<MediaTableModel *>(sourceModel());
	if (!model || row >= static_cast<int>(model->allFiles().size()))
		return false;
	const MediaFile &f = model->fileAt(row);
	switch (m_mode)
	{
	case FilterMode::Video:
		if (f.mediaType != MediaFile::Type::Video)
			return false;
		break;
	case FilterMode::Audio:
		if (f.mediaType != MediaFile::Type::Audio)
			return false;
		break;
	case FilterMode::Unmanaged:
		if (!f.isUnmanaged)
			return false;
		break;
	case FilterMode::BadUmid:
		if (!f.isBadUmid)
			return false;
		break;
	case FilterMode::Unreferenced:
		if (!f.isUnreferenced)
			return false;
		break;
	case FilterMode::NonPortable:
		if (!f.isNonPortable)
			return false;
		break;
	case FilterMode::Quarantined:
		if (!f.mxfFolder.contains("Quarantined", Qt::CaseInsensitive) && !f.filePath.contains("Quarantined", Qt::CaseInsensitive))
			return false;
		break;
	case FilterMode::All:
		break;
	}

	// Project filter: if projects are selected in sidebar, only show those
	if (!m_selectedProjects.isEmpty())
	{
		if (!m_selectedProjects.contains(f.project))
			return false;
	}

	// O(1) MOB check; runs after the cheaper project/type filters.
	if (m_binFilterActive)
	{
		const bool hit =
			(!f.mobId.isEmpty() && m_binFilterAcceptedMobs.contains(f.mobId)) ||
			(!f.compositionMobId.isEmpty() &&
			 m_binFilterAcceptedMobs.contains(f.compositionMobId));
		if (!hit)
			return false;
	}

	if (!m_search.isEmpty())
	{
		return f.clipName.toLower().contains(m_search) ||
			   f.project.toLower().contains(m_search) ||
			   f.originalBin.toLower().contains(m_search) ||
			   f.mxfFolder.toLower().contains(m_search) ||
			   f.codec.toLower().contains(m_search) ||
			   f.driveName.toLower().contains(m_search) ||
			   f.fileName.toLower().contains(m_search) ||
			   f.sourceFileName.toLower().contains(m_search) ||
			   f.sourceFilePath.toLower().contains(m_search) ||
			   f.startTimecode.toLower().contains(m_search);
	}
	return true;
}

bool MediaFilterProxy::lessThan(const QModelIndex &left,
								const QModelIndex &right) const
{
	QVariant lv = sourceModel()->data(left, Qt::UserRole);
	QVariant rv = sourceModel()->data(right, Qt::UserRole);
	if (static_cast<MediaTableModel::Column>(left.column()) == MediaTableModel::Column::SizeMB)
		return lv.toDouble() < rv.toDouble();
	if (static_cast<MediaTableModel::Column>(left.column()) == MediaTableModel::Column::Created)
		return lv.toDateTime() < rv.toDateTime();
	return lv.toString().toLower() < rv.toString().toLower();
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	m_driveManager = new DriveManager(this);
	m_scanner = new MediaScanner(this);
	m_fileOps = new MediaManager(this);
	m_model = new MediaTableModel(this);
	m_proxy = new MediaFilterProxy(this);
	m_proxy->setSourceModel(m_model);
	m_proxy->setSortRole(Qt::UserRole);

	setupUi();
	setupMenus();
	setupConnections();

	setWindowTitle("MediaMuster");
	resize(1200, 750);

	addLog(0, "app", QString("%1 v%2 initialised").arg(APP_NAME, APP_VERSION));

	// Build the platform string with full version + Windows DisplayVersion/UBR.
	// macOS  : "Platform: macOS 15.7.6"
	// Windows: "Platform: Windows 11 23H2 build 22631"
#ifdef Q_OS_MAC
	{
		// QSysInfo stops at major.minor ("macOS 15.7"); rebuild from QOperatingSystemVersion for the patch.
		const auto v = QOperatingSystemVersion::current();
		addLog(0, "app",
			   QString("Platform: %1 %2.%3.%4")
				   .arg(v.name())
				   .arg(v.majorVersion())
				   .arg(v.minorVersion())
				   .arg(v.microVersion()));
	}
#else // Q_OS_WIN
	{
		QString platform = QSysInfo::prettyProductName();
		QSettings reg(QStringLiteral(
						  "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"),
					  QSettings::NativeFormat);
		const QString displayVersion = reg.value("DisplayVersion").toString();
		const QString buildNumber = reg.value("CurrentBuildNumber").toString();
		const QVariant ubr = reg.value("UBR");

		QString extras;
		if (!displayVersion.isEmpty())
			extras += " " + displayVersion;
		if (!buildNumber.isEmpty())
		{
			extras += " build " + buildNumber;
			if (ubr.isValid())
				extras += "." + ubr.toString();
		}
		// Replace Qt's noisy "Version XXXX" suffix with a prettier string.
		if (!displayVersion.isEmpty() || !buildNumber.isEmpty())
		{
			const int versionIdx = platform.indexOf(" Version ");
			if (versionIdx > 0)
				platform.truncate(versionIdx);
			platform += extras;
		}
		addLog(0, "app", QString("Platform: %1").arg(platform));
	}
#endif

	onDetectDrives();
	m_driveManager->startMonitoring();

	// Check Full Disk Access on macOS
#ifdef Q_OS_MAC
	if (!DriveManager::hasFullDiskAccess())
	{
		addLog(1, "app",
			   "Full Disk Access not granted — some locations may be "
			   "inaccessible.");
		addLog(0, "app",
			   "Go to System Preferences > Privacy & Security > Full Disk "
			   "Access, to grant permission."); // Do not change to System Settings!
	}
	else
	{
		addLog(3, "app", "Full Disk Access: granted!");
	}
#endif // Q_OS_MAC
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi()
{
	m_sidePanel = new QWidget;
	m_sidePanel->setFixedWidth(260);
	auto *sideLayout = new QVBoxLayout(m_sidePanel);
	sideLayout->setContentsMargins(0, 0, 0, 0);
	sideLayout->setSpacing(12);

	auto *volGroup = new QGroupBox("Volumes");
	auto *volLayout = new QVBoxLayout(volGroup);
	volLayout->setContentsMargins(6, 6, 6, 6);
	volLayout->setSpacing(4);

	m_driveList = new DriveListWidget;
	m_driveList->setSelectionMode(QAbstractItemView::MultiSelection);
	m_driveList->setMinimumHeight(60);
	m_driveList->setMaximumHeight(140);
	m_driveList->setToolTip(
		"Drag folders or volumes here to add them.");
	m_driveList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	volLayout->addWidget(m_driveList);

	m_scanButton = new QPushButton("Scan Selected");
	m_scanButton->setDefault(true);
	volLayout->addWidget(m_scanButton);

	m_scanAllButton = new QPushButton("Scan All");
	m_scanAllButton->setToolTip(
		"Scan every mounted volume and known Avid location");
	volLayout->addWidget(m_scanAllButton);

	sideLayout->addWidget(volGroup);

	auto *projGroup = new QGroupBox("Projects");
	auto *projLayout = new QVBoxLayout(projGroup);
	projLayout->setContentsMargins(6, 6, 6, 6);
	m_projectList = new QListWidget;
	m_projectList->setSelectionMode(QAbstractItemView::MultiSelection);
	// Prevent long project names from expanding the splitter.
	m_projectList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	projLayout->addWidget(m_projectList);
	sideLayout->addWidget(projGroup);

	// Filter tabs
	m_filterTabs = new QTabBar;
	m_filterTabs->addTab("All");
	m_filterTabs->addTab("Video");
	m_filterTabs->addTab("Audio");
	m_filterTabs->addTab("Orphaned");
	m_filterTabs->addTab("Bad UMID");
	m_filterTabs->addTab("Unreferenced");
	m_filterTabs->addTab("Non-Portable");
	m_filterTabs->addTab("Quarantined");
	m_filterTabs->setExpanding(false);
	m_filterTabs->setDocumentMode(true);

	// Toolbar
	m_searchField = new QLineEdit;
	m_searchField->setPlaceholderText("Search");
	m_searchField->setClearButtonEnabled(true);
	m_searchField->setMinimumWidth(200);
	m_searchField->setMaximumWidth(320);

	m_btnFileOps = new QPushButton("Manage Media");
	m_btnBinFilter = new QPushButton("Filter by Bin");
	m_btnExport = new QPushButton("Export CSV");
	m_btnRebalance = new QPushButton("Rebalance…");
	m_btnRebalance->setToolTip(
		tr("Redistribute MXF files so each MediaFiles folder stays "
		   "under 5,000 files (Avid recommendation)."));
	m_btnFileOps->setEnabled(false);
	m_btnRebalance->setEnabled(false);

	// Hidden until a filter is active; rebuilt by rebuildFilterChips().
	m_chipsBar = new QWidget;
	m_chipsBar->setVisible(false);
	auto *chipsInternal = new QHBoxLayout(m_chipsBar);
	chipsInternal->setContentsMargins(0, 0, 0, 0);
	chipsInternal->setSpacing(6);

	// Toolbar: actions row + filter/search row.
	auto *toolbarWidget = new QWidget;
	auto *toolbarV = new QVBoxLayout(toolbarWidget);
	toolbarV->setContentsMargins(12, 8, 12, 8);
	toolbarV->setSpacing(8);

	auto *actionsRow = new QHBoxLayout;
	actionsRow->setContentsMargins(0, 0, 0, 0);
	actionsRow->setSpacing(8);
	actionsRow->addWidget(m_btnFileOps);
	actionsRow->addWidget(m_btnBinFilter);
	actionsRow->addWidget(m_btnExport);
	actionsRow->addWidget(m_btnRebalance);
	actionsRow->addStretch();
	toolbarV->addLayout(actionsRow);

	auto *filterRow = new QHBoxLayout;
	filterRow->setContentsMargins(0, 0, 0, 0);
	filterRow->setSpacing(8);
	filterRow->addWidget(m_filterTabs);
	filterRow->addWidget(m_chipsBar);
	filterRow->addStretch();
	filterRow->addWidget(m_searchField);
	toolbarV->addLayout(filterRow);

	// Table
	m_tableView = new QTableView;
	m_tableView->setModel(m_proxy);
	m_tableView->setSortingEnabled(true);
	m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tableView->setAlternatingRowColors(true);
	m_tableView->setShowGrid(false);
	m_tableView->verticalHeader()->setVisible(false);
	m_tableView->verticalHeader()->setDefaultSectionSize(24);
	m_tableView->horizontalHeader()->setStretchLastSection(true);
	m_tableView->horizontalHeader()->setSectionsMovable(true);
	m_tableView->horizontalHeader()->setHighlightSections(false);
	m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_tableView->setFont(QFont(Theme::monoFont(), Theme::monoFontSize()));

	// Starting widths; autoFitColumns() runs after the first scan.
	m_tableView->setColumnWidth(+MediaTableModel::Column::ClipName, 180);
	m_tableView->setColumnWidth(+MediaTableModel::Column::FileName, 150);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Project, 100);
	m_tableView->setColumnWidth(+MediaTableModel::Column::OriginalBin, 120);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Kind, 60);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Codec, 120);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Resolution, 85);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Fps, 50);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Duration, 90);
	m_tableView->setColumnWidth(+MediaTableModel::Column::StartTC, 90);
	m_tableView->setColumnWidth(+MediaTableModel::Column::SizeMB, 70);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Volume, 240);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Created, 130);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Source, 140);
	m_tableView->setColumnWidth(+MediaTableModel::Column::Type, 60);

	// Console
	m_console = new QPlainTextEdit;
	m_console->setReadOnly(true);
	m_console->setMaximumBlockCount(2000);
	m_console->setMinimumHeight(80);
	m_console->setFont(QFont(Theme::monoFont(), Theme::monoFontSize()));

	m_contentSplitter = new QSplitter(Qt::Vertical);
	m_contentSplitter->addWidget(m_tableView);
	m_contentSplitter->addWidget(m_console);
	m_contentSplitter->setStretchFactor(0, 1);
	m_contentSplitter->setStretchFactor(1, 0);

	// Table dominates; console at minimum.
	m_contentSplitter->setSizes({100000, 80});

	auto *contentWidget = new QWidget;
	auto *contentLayout = new QVBoxLayout(contentWidget);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);
	contentLayout->addWidget(toolbarWidget);
	contentLayout->addWidget(m_contentSplitter);

	m_mainSplitter = new QSplitter(Qt::Horizontal);
	m_mainSplitter->addWidget(m_sidePanel);
	m_mainSplitter->addWidget(contentWidget);
	m_mainSplitter->setStretchFactor(0, 0);
	m_mainSplitter->setStretchFactor(1, 1);

	// Ensure this stays intact to attach the layout to the main window.
	setCentralWidget(m_mainSplitter);

	m_statusFiles = new QLabel("0 files");
	m_statusSize = new QLabel("0 GB");
	m_statusSelected = new QLabel;
	m_statusSelSize = new QLabel;
	m_statusScanTime = new QLabel;

	auto *primarySep = new QLabel(" | ");
	m_statusSep1 = new QLabel(" | ");
	m_statusSep2 = new QLabel(" | ");

	// Hidden until selection exists.
	m_statusSep1->setVisible(false);
	m_statusSep2->setVisible(false);
	m_statusSelected->setVisible(false);
	m_statusSelSize->setVisible(false);

	statusBar()->addWidget(m_statusFiles);
	statusBar()->addWidget(primarySep);
	statusBar()->addWidget(m_statusSize);
	statusBar()->addWidget(m_statusSep1);
	statusBar()->addWidget(m_statusSelected);
	statusBar()->addWidget(m_statusSep2);
	statusBar()->addWidget(m_statusSelSize);
	statusBar()->addPermanentWidget(m_statusScanTime);
}

void MainWindow::setupMenus()
{
	// File menu
	auto *fileMenu = menuBar()->addMenu("&File");

	auto *a1 = fileMenu->addAction("Scan &Selected");
	a1->setShortcut(QKeySequence("Ctrl+R"));
	connect(a1, &QAction::triggered, this, &MainWindow::onScanClicked);

	auto *a1b = fileMenu->addAction("Scan &All");
	a1b->setShortcut(QKeySequence("Ctrl+Shift+A"));
	connect(a1b, &QAction::triggered, this, &MainWindow::onScanAllClicked);

	auto *a2 = fileMenu->addAction("Add &Folder...");
	a2->setShortcut(QKeySequence("Ctrl+O"));
	connect(a2, &QAction::triggered, this, [this]()
			{
        QString dir =
            QFileDialog::getExistingDirectory(this, "Add Volume or Folder");
        if (!dir.isEmpty())
            addDrivePath(dir); });

	fileMenu->addSeparator();

	// Do not change to "Show in Explorer" on Windows.
	auto *revealAct = fileMenu->addAction("Reveal in Finder");
	revealAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
	connect(revealAct, &QAction::triggered, this,
			&MainWindow::onRevealInFinder);

	// "Select Relatives" — picks every row in the visible table that
	// shares the selected file's composition Mob (e.g. V01 + A01 + A02 of
	// the same master clip).
	auto *relAct = fileMenu->addAction("Select &Relatives");
	relAct->setShortcut(QKeySequence("Ctrl+Shift+L"));
	connect(relAct, &QAction::triggered, this,
			&MainWindow::onSelectRelatives);

	fileMenu->addSeparator();

	auto *a3 = fileMenu->addAction("&Export CSV...");
	a3->setShortcut(QKeySequence("Ctrl+E"));
	connect(a3, &QAction::triggered, this, &MainWindow::onExportCsv);

#ifndef Q_OS_MAC
	fileMenu->addSeparator();
	auto *a4 = fileMenu->addAction("&Quit");
	a4->setShortcut(QKeySequence("Ctrl+Q"));
	connect(a4, &QAction::triggered, qApp, &QApplication::quit);
#endif

	// Edit menu
	auto *editMenu = menuBar()->addMenu("&Edit");
	auto *a5 = editMenu->addAction("&Find...");
	a5->setShortcut(QKeySequence::Find);
	connect(a5, &QAction::triggered, m_searchField,
			qOverload<>(&QWidget::setFocus));
	auto *a6 = editMenu->addAction("Select &All");
	a6->setShortcut(QKeySequence::SelectAll);
	connect(a6, &QAction::triggered, m_tableView, &QTableView::selectAll);

	// View menu
	auto *viewMenu = menuBar()->addMenu("&View");
	auto *ca = viewMenu->addAction("Show &Console");
	ca->setCheckable(true);
	ca->setChecked(true);
	connect(ca, &QAction::triggered, this, [this](bool c)
			{
        m_console->setVisible(c);
        m_consoleVisible = c; });

	viewMenu->addSeparator();

	// Auto-Fit Columns: so they resize to fit contents.
	auto *fitAct = viewMenu->addAction("Auto-Fit &Columns");
	fitAct->setShortcut(QKeySequence("Ctrl+T"));
	connect(fitAct, &QAction::triggered, this, &MainWindow::onAutoFitColumns);

	viewMenu->addSeparator();

	// Show All Filter Tabs: when off (default), tabs with a zero count
	// are hidden. When on, all tabs are always visible — useful for
	// debugging or for demonstrating all the app's features.
	auto *showAllTabsAct = viewMenu->addAction("Show &All Filter Tabs");
	showAllTabsAct->setCheckable(true);
	showAllTabsAct->setChecked(false);
	connect(showAllTabsAct, &QAction::triggered, this, [this](bool on)
			{
		m_showAllFilterTabs = on;
		updateFilterCounts(); });

	viewMenu->addSeparator();
	auto *ra = viewMenu->addAction("Refresh &Drives");
	ra->setShortcut(QKeySequence("Ctrl+Shift+D"));
	connect(ra, &QAction::triggered, this, &MainWindow::onDetectDrives);

	// Special menu
	// Holds the stuff that doesn't have a natural home in File/Edit/View.
	auto *specialMenu = menuBar()->addMenu("&Special");
	auto *sa = specialMenu->addAction("&Project Summary");
	connect(sa, &QAction::triggered, this, &MainWindow::onProjectSummary);

	specialMenu->addSeparator();
	auto *bfa = specialMenu->addAction("Filter by &Bins...");
	bfa->setShortcut(QKeySequence("Ctrl+Shift+B"));
	connect(bfa, &QAction::triggered, this, &MainWindow::onFilterByBins);

	specialMenu->addSeparator();
	auto *rba = specialMenu->addAction("&Rebalance MXF Folders...");
	rba->setShortcut(QKeySequence("Ctrl+Shift+R"));
	connect(rba, &QAction::triggered, this, &MainWindow::onRebalance);

#ifdef Q_OS_MAC
	specialMenu->addSeparator();
	auto *pa = specialMenu->addAction("Check &Permissions...");
	connect(pa, &QAction::triggered, this, &MainWindow::onCheckPermissions);
#endif

	// Throttles workers via DebugSlowdown::enabled(); off by default.
	specialMenu->addSeparator();
	auto *slowAct = specialMenu->addAction(
		tr("&Debug: slow mode"));
	slowAct->setCheckable(true);
	slowAct->setChecked(false);
	connect(slowAct, &QAction::triggered, this, [this](bool on)
			{
		DebugSlowdown::setEnabled(on);
		addLog(0, "app", on ? "Slow Mode enabled (1/50th speed)"
							: "Slow Mode disabled"); });

	auto *rawHexAct = specialMenu->addAction(tr("&Debug: raw hex"));
	rawHexAct->setCheckable(true);
	rawHexAct->setChecked(false);
	connect(rawHexAct, &QAction::triggered, this, [this](bool on)
			{
		m_model->setShowRawCodecHex(on);
		addLog(0, "app", on ? "Raw hex mode: Codec column shows raw essence label hex"
							: "Raw hex mode disabled"); });

	// Help menu
	auto *helpMenu = menuBar()->addMenu("&Help");
	auto *aa = helpMenu->addAction("About MediaMuster");
	connect(aa, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::onAbout()
{
	auto *dlg = new QDialog(this);
	dlg->setWindowTitle(tr("About MediaMuster"));
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setModal(true);

	auto *layout = new QVBoxLayout(dlg);
	layout->setContentsMargins(40, 28, 40, 24);
	layout->setSpacing(8);
	layout->setAlignment(Qt::AlignHCenter);

	// Icon. Falls back gracefully when no icon resource is bundled.
	auto *icon = new QLabel;
	const QPixmap appIcon =
		QApplication::windowIcon().pixmap(QSize(96, 96));
	if (!appIcon.isNull())
	{
		icon->setPixmap(appIcon);
		icon->setAlignment(Qt::AlignHCenter);
		layout->addWidget(icon);
		layout->addSpacing(12);
	}

	auto *name = new QLabel(APP_NAME);
	{
		QFont f = name->font();
		f.setPointSize(f.pointSize() + 10);
		f.setBold(true);
		name->setFont(f);
	}
	name->setAlignment(Qt::AlignHCenter);
	layout->addWidget(name);

	auto *version = new QLabel(QString("Version %1").arg(APP_VERSION));
	{
		QFont f = version->font();
		f.setPointSize(f.pointSize() - 1);
		version->setFont(f);
	}
	version->setAlignment(Qt::AlignHCenter);
	version->setStyleSheet("color: black;");
	layout->addWidget(version);

	layout->addSpacing(10);

	auto *copyright = new QLabel(
		tr("Copyright © 2026 Martin McLean.\nAll rights reserved."));
	copyright->setAlignment(Qt::AlignHCenter);
	{
		QFont f = copyright->font();
		f.setPointSize(f.pointSize() - 1);
		copyright->setFont(f);
	}
	copyright->setStyleSheet("color: gray;");
	layout->addWidget(copyright);

	layout->addSpacing(10);

	// Required Qt LGPL attribution.
	auto *credits = new QLabel(
		tr("Built with <a href=\"https://qt.io/download-qt-installer\">Qt %1</a> under the GNU LGPL v3.<br>"
		   "Developed from examination of files.<br>"
		   "No copyright infringement intended.")
			.arg(QT_VERSION_STR));
	credits->setAlignment(Qt::AlignHCenter);
	credits->setOpenExternalLinks(true);
	credits->setTextFormat(Qt::RichText);
	{
		QFont f = credits->font();
		f.setPointSize(f.pointSize() - 1);
		credits->setFont(f);
	}
	credits->setStyleSheet("color: gray;");
	layout->addWidget(credits);

	layout->addSpacing(15);

	auto *btnRow = new QHBoxLayout;
	btnRow->addStretch();
	auto *okBtn = new QPushButton(tr("Sweet as!"));
	okBtn->setDefault(true);
	connect(okBtn, &QPushButton::clicked, dlg, &QDialog::accept);
	btnRow->addWidget(okBtn);
	btnRow->addStretch();
	layout->addLayout(btnRow);

	dlg->setFixedWidth(420);
	dlg->adjustSize();
	dlg->show();
}

void MainWindow::setupConnections()
{
	connect(
		m_driveManager, &DriveManager::drivesChanged, this,
		[this](const QVector<DriveInfo> &)
		{
			if (m_driveList->count() == 0)
				onDetectDrives();
		},
		Qt::QueuedConnection);

	// Scanner — cross-thread signals
	connect(m_scanner, &MediaScanner::scanProgress, this,
			&MainWindow::onScanProgress, Qt::QueuedConnection);
	// scanLogBatch only — connecting scanLog too would double-log.
	connect(m_scanner, &MediaScanner::scanLogBatch, this,
			&MainWindow::onScanLogBatch, Qt::QueuedConnection);
	connect(m_scanner, &MediaScanner::scanFinished, this,
			&MainWindow::onScanFinished, Qt::QueuedConnection);

	// File operations — cross-thread signals
	connect(
		m_fileOps, &MediaManager::operationProgress, this,
		[this](const QString &name, int cur, int total, double)
		{
			auto *dlg = progressDialog();
			if (!dlg->isVisible())
			{
				setBusy(true);
				dlg->begin(m_pendingOpTitle.isEmpty()
							   ? tr("Processing files…")
							   : m_pendingOpTitle);
			}
			dlg->setProgress(cur, total);
			dlg->setDetail(name);
		},
		Qt::QueuedConnection);
	connect(
		m_fileOps, &MediaManager::operationItemDone, this,
		[this](const QString &name, bool ok, const QString &err)
		{
			addLog(ok ? 3 : 2, "ops",
				   ok ? name + " done" : name + " FAILED: " + err);
			// Track successful paths for post-op table update.
			if (ok && m_removeAfterOp)
			{
				// Find the full path by matching filename against m_allFiles.
				for (const auto &f : m_allFiles)
				{
					if (f.fileName == name)
					{
						m_successfulOpPaths.insert(f.filePath);
						break;
					}
				}
			}
		},
		Qt::QueuedConnection);
	connect(
		m_fileOps, &MediaManager::operationFinished, this,
		[this](int ok, int fail)
		{
			setBusy(false);
			addLog(fail > 0 ? 1 : 3, "ops",
				   QString("Complete: %1 ok, %2 failed").arg(ok).arg(fail));

			// After a Move or Delete, remove successfully-operated files
			// from the table so it reflects reality without a re-scan.
			if (m_removeAfterOp && !m_successfulOpPaths.isEmpty())
			{
				QVector<MediaFile> remaining;
				remaining.reserve(m_allFiles.size() - m_successfulOpPaths.size());
				for (const auto &f : m_allFiles)
				{
					if (!m_successfulOpPaths.contains(f.filePath))
						remaining.append(f);
				}
				m_allFiles = std::move(remaining);
				m_model->setMediaFiles(m_allFiles);
				updateFilterCounts();
				updateStatusBar();
				addLog(0, "ops",
					   QString("Removed %1 files from table")
						   .arg(m_successfulOpPaths.size()));
			}
			m_removeAfterOp = false;
			m_successfulOpPaths.clear();
			m_pendingOpTitle.clear();
		},
		Qt::QueuedConnection);

	// When files on a network drive are deleted, the OS Recycle Bin
	// isn't available so they're moved to a "_MediaMuster_Trash" folder on
	// the same volume instead. This signal fires after the operation
	// completes, triggering a dialog that explains what happened
	// and gives the user options.
	connect(
		m_fileOps, &MediaManager::networkBinUsed, this,
		[this](const QString &binPath, int fileCount)
		{
			QMessageBox msgBox(this);
			msgBox.setIcon(QMessageBox::Information);
			msgBox.setWindowTitle(tr("MediaMuster Trash"));
			msgBox.setText(tr(
							   "<b>%1 file%2 moved to the MediaMuster Trash</b>")
							   .arg(fileCount)
							   .arg(fileCount == 1 ? "" : "s"));
			msgBox.setInformativeText(tr(
										  "Network drives only support permenant delete, so "
										  "MediaMuster has moved the file%1 to:\n\n%2\n\n"
										  "These files can be restored by moving them back to "
										  "their original location. To permanently delete them, "
										  "click \"Empty Trash\".")
										  .arg(fileCount == 1 ? "" : "s")
										  .arg(binPath));

			auto *btnOk = msgBox.addButton(QMessageBox::Ok);
			auto *btnOpen = msgBox.addButton(
				tr("Take Me There"), QMessageBox::ActionRole);
			auto *btnEmpty = msgBox.addButton(
				tr("Empty Trash"), QMessageBox::DestructiveRole);
			msgBox.setDefaultButton(btnOk);
			msgBox.exec();

			if (msgBox.clickedButton() == btnOpen)
			{
#ifdef Q_OS_MAC
				QProcess::startDetached("open", {binPath});
#elif defined(Q_OS_WIN)
				QProcess::startDetached("explorer.exe",
										{QDir::toNativeSeparators(binPath)});
#endif
			}
			else if (msgBox.clickedButton() == btnEmpty)
			{
				// Confirm before permanent deletion.
				QDir binDir(binPath);
				// Count files in the bin folder.
				int binFileCount = 0;
				qint64 binBytes = 0;
				QDirIterator it(binPath, QDir::Files,
								QDirIterator::Subdirectories);
				while (it.hasNext())
				{
					it.next();
					binFileCount++;
					binBytes += it.fileInfo().size();
				}

				QString sizeStr = formatBytes(binBytes);

				auto ret = QMessageBox::warning(
					this, tr("Empty the MediaMuster Trash"),
					tr("Permanently delete %1 file%2 (%3) from:\n\n%4\n\n"
					   "This cannot be undone.")
						.arg(binFileCount)
						.arg(binFileCount == 1 ? "" : "s")
						.arg(sizeStr, binPath),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::No);

				if (ret == QMessageBox::Yes)
				{
					if (binDir.removeRecursively())
					{
						addLog(3, "ops",
							   QString("MediaMuster Trash emptied: %1")
								   .arg(binPath));
					}
					else
					{
						addLog(2, "ops",
							   QString("Failed to empty MediaMuster Trash: %1")
								   .arg(binPath));
					}
				}
			}
		},
		Qt::QueuedConnection);

	// UI
	connect(m_filterTabs, &QTabBar::currentChanged, this,
			&MainWindow::onFilterChanged);

	// 200 ms debounce so search doesn't fire on every keystroke.
	{
		auto *searchDebounce = new QTimer(this);
		searchDebounce->setSingleShot(true);
		searchDebounce->setInterval(200);
		connect(m_searchField, &QLineEdit::textChanged,
				searchDebounce, qOverload<>(&QTimer::start));
		connect(searchDebounce, &QTimer::timeout, this, [this]()
				{ onSearchChanged(m_searchField->text()); });

		// Chips update per-keystroke; the table filter waits for the debounce.
		connect(m_searchField, &QLineEdit::textChanged, this,
				[this]()
				{ rebuildFilterChips(); });
	}
	connect(m_tableView->selectionModel(),
			&QItemSelectionModel::selectionChanged, this,
			&MainWindow::onSelectionChanged);
	connect(m_tableView, &QTableView::doubleClicked, this,
			&MainWindow::onTableDoubleClicked);
	connect(m_btnFileOps, &QPushButton::clicked, this,
			&MainWindow::onFileOperations);
	connect(m_btnBinFilter, &QPushButton::clicked, this,
			&MainWindow::onFilterByBins);
	connect(m_btnExport, &QPushButton::clicked, this, &MainWindow::onExportCsv);
	connect(m_btnRebalance, &QPushButton::clicked, this, &MainWindow::onRebalance);
	connect(m_scanButton, &QPushButton::clicked, this,
			&MainWindow::onScanClicked);
	connect(m_scanAllButton, &QPushButton::clicked, this,
			&MainWindow::onScanAllClicked);

	connect(m_driveList, &DriveListWidget::pathsDropped, this,
			&MainWindow::onPathsDropped);

	// Project sidebar: multi-select filtering
	connect(m_projectList, &QListWidget::itemSelectionChanged, this,
			[this]()
			{
				QSet<QString> selected;
				for (auto *item : m_projectList->selectedItems())
				{
					selected.insert(item->data(Qt::UserRole).toString());
				}
				m_proxy->setProjectFilter(selected);
				updateStatusBar();
				rebuildFilterChips();
			});

	connect(m_tableView, &QTableView::customContextMenuRequested, this,
			[this](const QPoint &pos)
			{
				QMenu menu(this);

				// Per-cell "Copy <text>" entry.
				QModelIndex index = m_tableView->indexAt(pos);
				if (index.isValid())
				{
					QString cellText = index.data(Qt::DisplayRole).toString();

					// Make the menu look clean if the text is massive
					QString menuLabel = cellText;
					if (menuLabel.length() > 25)
					{
						menuLabel = menuLabel.left(22) + "...";
					}

					menu.addAction(
						QString("Copy \"%1\"").arg(menuLabel), [cellText]()
						{ QApplication::clipboard()->setText(cellText); });
					menu.addSeparator();
				}

				// Same label on Mac and Windows.
				menu.addAction("Reveal in Finder", this, &MainWindow::onRevealInFinder);
				menu.addAction("Copy Path", [this]()
							   {
                    auto sel = selectedFiles();
                    if (!sel.isEmpty()) {
                        QStringList p;
                        for (const auto &f : sel)
                            p << f.filePath;
                        QApplication::clipboard()->setText(p.join("\n"));
                    } });

				// Disabled when the file has no composition MOB.
				{
					auto sel = selectedFiles();
					QAction *relAct = menu.addAction(
						"Select Relatives", this,
						&MainWindow::onSelectRelatives);
					const bool hasComp =
						!sel.isEmpty() && !sel.first().compositionMobId.isEmpty();
					relAct->setEnabled(hasComp);
					if (!hasComp)
						relAct->setToolTip(
							"No composition MOB — this file isn't linked "
							"to a master clip we can follow");
				}

				menu.addSeparator();
				menu.addAction("Copy To...", this, &MainWindow::onCopyClicked);
				menu.addAction("Move To...", this, &MainWindow::onMoveClicked);
				menu.addAction("Delete", this, &MainWindow::onDeleteClicked);
				menu.exec(m_tableView->viewport()->mapToGlobal(pos));
			});

	new QShortcut(QKeySequence::Delete, this, [this]()
				  { onDeleteClicked(); });

	// Escape: clear search field and project selection
	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this]()
				  {
					  m_searchField->clear();
					  m_projectList->clearSelection();
					  m_filterTabs->setCurrentIndex(0); // Reset to "All"
				  });

	// Ctrl+1..8 → filter tab N (skips hidden).
	for (int i = 0; i < 8; ++i)
	{
		auto *sc = new QShortcut(
			QKeySequence(QString("Ctrl+%1").arg(i + 1)), this);
		connect(sc, &QShortcut::activated, this, [this, i]()
				{
					if (i < m_filterTabs->count() &&
						m_filterTabs->isTabVisible(i))
						m_filterTabs->setCurrentIndex(i); });
	}
}

void MainWindow::onCheckPermissions()
{
#ifdef Q_OS_MAC
	bool hasFDA = DriveManager::hasFullDiskAccess();
	if (hasFDA)
	{
		QMessageBox::information(
			this, "Permissions",
			"Full Disk Access is <b>granted</b>.<br><br>"
			"MediaMuster can scan all drives and folders on this Mac.");
	}
	else
	{
		auto ret = QMessageBox::warning(
			this, "Permissions",
			"Full Disk Access is <b>not granted</b>.<br><br>"
			"MediaMuster can scan external drives and /Users/Shared, "
			"but some internal folders may be inaccessible.<br><br>"
			"Would you like to open System Prefs to grant access?",
			QMessageBox::Yes | QMessageBox::No);
		if (ret == QMessageBox::Yes)
		{
			DriveManager::openFullDiskAccessSettings();
		}
	}
#endif // Q_OS_MAC
}

void MainWindow::onFilterByBins()
{
	// Lazy; chain state persists across close/reopen. Parented to MainWindow.
	if (!m_binFilterDialog)
	{
		m_binFilterDialog = new BinFilterDialog(this);
		connect(m_binFilterDialog, &BinFilterDialog::filterChainChanged,
				m_proxy, &MediaFilterProxy::setBinFilterMobs);
		connect(m_binFilterDialog, &BinFilterDialog::filterChainChanged, this,
				[this](bool isActive, const QSet<QString> &acceptedMobs)
				{
					// Cached so the chip strip doesn't reach into the dialog.
					m_binFilterActive = isActive;
					m_binFilterMobCount = acceptedMobs.size();
					rebuildFilterChips();

					if (!isActive)
					{
						addLog(0, "binfilter", "Bin filter cleared");
						updateStatusBar();
						return;
					}
					addLog(3, "binfilter",
						   QString("Bin filter active — %1 MOBs accepted")
							   .arg(acceptedMobs.size()));
					// Sample bin/file MOBs side-by-side to spot namespace mismatches.
					if (!acceptedMobs.isEmpty() && !m_allFiles.isEmpty())
					{
						auto binSample = *acceptedMobs.constBegin();
						QString fileSample = m_allFiles.first().mobId;
						QString compSample = m_allFiles.first().compositionMobId;
						addLog(4, "binfilter",
							   QString("Bin MOB sample   : %1").arg(binSample));
						addLog(4, "binfilter",
							   QString("File mobId sample: %1").arg(fileSample));
						addLog(4, "binfilter",
							   QString("File compMobId   : %1").arg(compSample));

						// Count hits in the currently-scanned files
						int hits = 0;
						for (const MediaFile &f : m_allFiles)
						{
							if (!f.mobId.isEmpty() &&
								acceptedMobs.contains(f.mobId))
							{
								++hits;
								continue;
							}
							if (!f.compositionMobId.isEmpty() &&
								acceptedMobs.contains(f.compositionMobId))
							{
								++hits;
							}
						}
						addLog(hits > 0 ? 3 : 1, "binfilter",
							   QString("Matched %1 / %2 scanned files")
								   .arg(hits)
								   .arg(m_allFiles.size()));
					}
					updateStatusBar();
				});
	}
	m_binFilterDialog->show();
	m_binFilterDialog->raise();
	m_binFilterDialog->activateWindow();
}

// Redistribute MXF files so each MediaFiles folder stays under Avid's 5000-file threshold.
void MainWindow::onRebalance()
{
	if (m_allFiles.isEmpty())
	{
		QMessageBox::information(
			this, tr("Rebalance"),
			tr("Scan a drive first. Rebalance needs to know what's "
			   "currently on the drive."));
		return;
	}

	// MXF root = grandparent of the file: <drive>/Avid MediaFiles/MXF/<folder>/<file.mxf>.
	QHash<QString, QString> mxfRootsByLabel;
	QHash<QString, QVector<MediaFile>> filesByMxfRoot;
	QHash<QString, int> countByLabel;
	QHash<QString, QString> drivePathByLabel;

	for (const MediaFile &mf : m_allFiles)
	{
		if (mf.filePath.isEmpty())
			continue;
		const QString folderDir = QFileInfo(mf.filePath).absolutePath();
		const QString mxfRoot = QFileInfo(folderDir).absolutePath();
		// Skip non-standard layouts; grandparent must be the literal "MXF" folder.
		if (QFileInfo(mxfRoot).fileName() != QStringLiteral("MXF"))
			continue;

		QString label = mf.driveName;
		if (label.isEmpty())
			label = QFileInfo(mf.drivePath).fileName();
		if (label.isEmpty())
			label = mxfRoot;

		mxfRootsByLabel.insert(label, mxfRoot);
		filesByMxfRoot[mxfRoot].append(mf);
		countByLabel[label] += 1;
		if (!drivePathByLabel.contains(label))
			drivePathByLabel.insert(label, mf.drivePath);
	}

	if (filesByMxfRoot.isEmpty())
	{
		QMessageBox::warning(
			this, tr("Rebalance"),
			tr("No standard 'Avid MediaFiles/MXF' folders were found in "
			   "the current scan results. Rebalance only operates on "
			   "Avid's standard layout."));
		return;
	}

	// Default the picker to whichever drive has the most scanned files.
	QString initialLabel;
	int maxCount = -1;
	for (auto it = countByLabel.constBegin();
		 it != countByLabel.constEnd(); ++it)
	{
		if (it.value() > maxCount)
		{
			maxCount = it.value();
			initialLabel = it.key();
		}
	}

	addLog(0, "rebalance",
		   QString("Opening rebalance dialog (%1 drive(s), default '%2')")
			   .arg(mxfRootsByLabel.size())
			   .arg(initialLabel));

	RebalanceDialog dlg(mxfRootsByLabel, filesByMxfRoot,
						initialLabel, this);
	dlg.exec();

	// Re-scan the drive so the table catches up with the moves.
	if (!dlg.didExecute())
		return;

	const QString drivePath = drivePathByLabel.value(dlg.executedLabel());
	if (drivePath.isEmpty())
	{
		addLog(1, "rebalance",
			   "Couldn't determine drive path for re-scan; please scan "
			   "manually");
		return;
	}
	addLog(0, "rebalance",
		   QString("Re-scanning '%1' after rebalance").arg(drivePath));
	startScanWithPaths(QStringList() << drivePath);
}

void MainWindow::onPathsDropped(const QStringList &paths)
{
	for (const QString &path : paths)
		addDrivePath(path);
}

void MainWindow::addDrivePath(const QString &path)
{
	for (int i = 0; i < m_driveList->count(); ++i)
	{
		if (m_driveList->item(i)->data(Qt::UserRole).toString() == path)
		{
			m_driveList->item(i)->setSelected(true);
			return;
		}
	}
	QFileInfo fi(path);
	QString name = fi.fileName();
	if (name.isEmpty())
		name = path;

	// No DriveInfo for manual paths — icon helper sniffs QStorageInfo.
	auto *item = new QListWidgetItem(iconForDriveType({}, path), name);
	item->setData(Qt::UserRole, path);

	QString tooltip = path;
	QStorageInfo si(path);
	if (si.isValid() && si.bytesTotal() > 0)
	{
		const qint64 used = si.bytesTotal() - si.bytesAvailable();
		tooltip += QString("\n%1 of %2 used")
					   .arg(formatBytes(used), formatBytes(si.bytesTotal()));
	}
	item->setToolTip(tooltip);
	item->setSelected(true);
	m_driveList->addItem(item);
	m_manualDrives.insert(path);
	addLog(0, "drives", QString("Added: %1").arg(path));
}

void MainWindow::onDetectDrives()
{
	auto drives = m_driveManager->detectDrives();
	QSet<QString> manualCopy = m_manualDrives;
	m_driveList->clear();

	// Count name collisions (e.g. "Macintosh HD" + APFS data sibling) so we can suffix.
	QHash<QString, int> nameCounts;
	for (const auto &d : drives)
		++nameCounts[d.name];

	// Disk-Utility-style suffix: system root keeps bare name; others get last path component.
	// "/System/Volumes/Data" → "Data" → "Macintosh HD - Data".
	auto suffixForPath = [](const QString &path, const QString &name) -> QString
	{
		if (path == QLatin1String("/") || path.isEmpty())
			return {}; // system volume — bare name

		QString p = path;
		while (p.endsWith(QLatin1Char('/')) || p.endsWith(QLatin1Char('\\')))
			p.chop(1);

		QString basename = QFileInfo(p).fileName();
		if (basename.isEmpty())
			basename = p;

		// Use full path if basename echoes volume name (e.g. /Volumes/Backup, /Volumes/Backup-1).
		if (basename.compare(name, Qt::CaseInsensitive) == 0)
			return path;
		return basename;
	};

	for (const DriveInfo &d : drives)
	{
		// Volume name + path suffix on collision; capacity/type stay in the tooltip.
		QString displayName = d.name;
		if (nameCounts.value(d.name) > 1)
		{
			const QString suffix = suffixForPath(d.path, d.name);
			if (!suffix.isEmpty())
				displayName = QString("%1 - %2").arg(d.name, suffix);
		}

		auto *item = new QListWidgetItem(
			iconForDriveType(d.driveType, d.path), displayName);
		item->setData(Qt::UserRole, d.path);

		QString tooltip = d.path;
		if (d.totalBytes > 0)
		{
			tooltip += QString("\n%1 of %2 used (%3)")
						   .arg(formatBytes(d.usedBytes),
								formatBytes(d.totalBytes),
								d.driveType);
		}
		item->setToolTip(tooltip);

		if (d.hasAvidMedia)
		{
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
			item->setSelected(true);
		}

		m_driveList->addItem(item);
		manualCopy.remove(d.path); // Don't re-add if already detected
	}

	// Re-add manual drives not in detected list
	for (const QString &mp : manualCopy)
		addDrivePath(mp);

	int ac = 0;
	for (const auto &d : drives)
		if (d.hasAvidMedia)
			ac++;
	addLog(0, "drives",
		   QString("Found %1 volumes (%2 with Avid MediaFiles)")
			   .arg(static_cast<int>(drives.size()))
			   .arg(ac));
}

void MainWindow::onScanClicked()
{
	if (m_scanner->isRunning())
	{
		m_scanner->cancelScan();
		m_scanButton->setText("Scan Selected");
		return;
	}

	QStringList paths;
	for (auto *item : m_driveList->selectedItems())
		paths << item->data(Qt::UserRole).toString();

	if (paths.isEmpty())
	{
		QMessageBox::information(
			this, "No Volumes Selected",
			"Select at least one volume, or use \"Scan All Drives\".");
		return;
	}
	startScanWithPaths(paths);
}

void MainWindow::onScanAllClicked()
{
	if (m_scanner->isRunning())
	{
		m_scanner->cancelScan();
		return;
	}

	QStringList paths = m_driveManager->allScannablePaths();
	// Also include any manually added paths
	for (const QString &mp : m_manualDrives)
	{
		if (!paths.contains(mp))
			paths.append(mp);
	}

	addLog(
		0, "scanner",
		QString("Scan All: %1 locations").arg(static_cast<int>(paths.size())));
	startScanWithPaths(paths);
}

void MainWindow::startScanWithPaths(const QStringList &paths)
{
	MediaScanner::Options opts;
	opts.drivePaths = paths;
	opts.parseMxfHeaders = true;
	opts.scanUnmanaged = true;

	setBusy(true);
	progressDialog()->begin(tr("Scanning media…"));
	progressDialog()->setDetail(tr("Starting scan…"));
	m_scanTimer.start();

	m_scanner->startScan(opts);
}

void MainWindow::onScanProgress(const QString &phase, int current, int total,
								const QString &currentPath)
{
	auto *dlg = progressDialog();
	dlg->setProgress(current, total);

	// Cosmetic path normalisation for the progress dialog:
	//   /Volumes/haa-edit/...     > Volumes/haa-edit/...
	//   /Users/Shared/...         > System Drive/Users/Shared/...
	//   C:\Avid MediaFiles\MXF\1  > native separators kept
	QString displayPath = currentPath;

#ifdef Q_OS_MAC
	if (displayPath.startsWith('/'))
	{
		if (!displayPath.startsWith("/Volumes/"))
			displayPath = "System Drive" + displayPath;
		else
			displayPath.remove(0, 1);
	}
#else // Q_OS_WIN
	displayPath = QDir::toNativeSeparators(displayPath);
#endif

	dlg->setDetail(displayPath);
}

void MainWindow::onScanLogBatch(const QVector<LogMsg> &batch)
{
	// One appendPlainText per batch — one reflow instead of N keeps the console responsive.
	if (batch.isEmpty())
		return;

	static const char *pfx[] = {"INFO", "WARN", "ERR ", " OK ", "DBG "};
	const QString now = QTime::currentTime().toString("HH:mm:ss");

	QString combined;
	combined.reserve(batch.size() * 80);

	for (int i = 0; i < batch.size(); ++i)
	{
		const LogMsg &m = batch[i];
		const int idx = (m.level >= 0 && m.level <= 4) ? m.level : 4;
		if (i > 0)
			combined += QLatin1Char('\n');
		combined += now;
		combined += QLatin1String(" [");
		combined += QLatin1String(pfx[idx]);
		combined += QLatin1String("] [");
		combined += m.module;
		combined += QLatin1String("] ");
		combined += m.message;
	}
	m_console->appendPlainText(combined);
}

void MainWindow::onScanFinished(const QVector<MediaFile> &results)
{
    m_allFiles = results;
    m_model->setMediaFiles(results);

    qint64 elapsed = m_scanTimer.elapsed();
    QString timeStr = QString("Scan: %1 ms").arg(elapsed);
    m_statusScanTime->setText(timeStr);

    m_projectList->clear();
    QHash<QString, QPair<int, qint64>> projectStats;
    for (const auto &f : results)
    {
        auto &stat = projectStats[f.project];
        stat.first++;
        stat.second += f.sizeBytes;
    }
    
    QStringList sorted = projectStats.keys();
    sorted.sort();
    for (const QString &p : sorted)
    {
        auto [count, bytes] = projectStats[p];
        auto *item = new QListWidgetItem(
            iconForProject(p),
            QString("%1 (%2 files, %3)").arg(p).arg(count).arg(formatBytes(bytes)));
        item->setData(Qt::UserRole, p);
        m_projectList->addItem(item);
    }
    
    updateFilterCounts();
    updateStatusBar();
    autoFitColumns();

    setBusy(false); 
}

void MainWindow::onFilterChanged(int index)
{
	// Order must match the tabs in setupUi.
	using FM = MediaFilterProxy::FilterMode;
	static const FM m[] = {
		FM::All, FM::Video, FM::Audio, FM::Unmanaged,
		FM::BadUmid, FM::Unreferenced, FM::NonPortable, FM::Quarantined};

	if (index >= 0 && index < 8)
	{
		m_proxy->setFilterMode(m[index]);
		updateStatusBar();
		rebuildFilterChips();
	}
}

void MainWindow::onSearchChanged(const QString &text)
{
	m_proxy->setSearchText(text);
	updateStatusBar();
	rebuildFilterChips();
}

void MainWindow::onSelectionChanged()
{
	auto rows = m_tableView->selectionModel()->selectedRows();
	int c = static_cast<int>(rows.size());
	m_btnFileOps->setEnabled(c > 0);

	// Show selection stats only when something is selected.
	const bool hasSel = (c > 0);
	m_statusSep1->setVisible(hasSel);
	m_statusSelected->setVisible(hasSel);
	m_statusSep2->setVisible(hasSel);
	m_statusSelSize->setVisible(hasSel);

	if (hasSel)
	{
		qint64 selBytes = 0;
		for (const auto &pi : rows)
			selBytes += m_model->fileAt(m_proxy->mapToSource(pi).row()).sizeBytes;

		m_statusSelected->setText(QString("%1 selected").arg(c));
		m_statusSelSize->setText(
			QString("%1 selected").arg(formatBytes(selBytes)));
	}
}

void MainWindow::onFileOperations()
{
	openManageMedia(+ManageMediaDialog::Operation::Copy);
}

void MainWindow::onCopyClicked()
{
	openManageMedia(+ManageMediaDialog::Operation::Copy);
}

void MainWindow::onMoveClicked()
{
	openManageMedia(+ManageMediaDialog::Operation::Move);
}

void MainWindow::onDeleteClicked()
{
	openManageMedia(+ManageMediaDialog::Operation::Delete);
}

void MainWindow::openManageMedia(int initialOp)
{
	auto files = selectedFiles();
	if (files.isEmpty())
		return;

	ManageMediaDialog dlg(files, this,
						  static_cast<ManageMediaDialog::Operation>(initialOp));
	if (dlg.exec() != QDialog::Accepted)
		return;

	QHash<QString, int> policies;
	const auto perFile = dlg.conflictPolicies();
	for (auto it = perFile.constBegin(); it != perFile.constEnd(); ++it)
		policies.insert(it.key(), static_cast<int>(it.value()));

	// Move/Delete prunes the table; Copy does not.
	m_removeAfterOp = (dlg.operation() != ManageMediaDialog::Operation::Copy);
	m_successfulOpPaths.clear();

	switch (dlg.operation())
	{
	case ManageMediaDialog::Operation::Copy:
		addLog(0, "ops", QString("Copying %1 files to %2").arg(files.size()).arg(dlg.destination()));
		m_pendingOpTitle = tr("Copying files…");
		m_fileOps->executeCopy(files, dlg.destination(),
							   dlg.preserveStructure(), policies);
		break;

	case ManageMediaDialog::Operation::Move:
		addLog(0, "ops", QString("Moving %1 files to %2").arg(files.size()).arg(dlg.destination()));
		m_pendingOpTitle = tr("Moving files…");
		m_fileOps->executeMove(files, dlg.destination(),
							   dlg.preserveStructure(), policies);
		break;

	case ManageMediaDialog::Operation::Delete:
		addLog(0, "ops", QString("Sending %1 files to Bin").arg(files.size()));
		m_pendingOpTitle = tr("Deleting files…");
		m_fileOps->executeDelete(files);
		break;
	}
}

void MainWindow::onExportCsv()
{
	// Determine which files to export.
	auto sel = selectedFiles();
	bool hasSelection = !sel.isEmpty();
	bool exportSelected = false;

	if (hasSelection)
	{
		QMessageBox msgBox(this);
		msgBox.setWindowTitle("Export CSV");
		msgBox.setText("Export all rows, or only the selected rows?");
		auto *btnSel = msgBox.addButton("Selected", QMessageBox::AcceptRole);
		auto *btnAll = msgBox.addButton("All", QMessageBox::AcceptRole);
		msgBox.addButton(QMessageBox::Cancel);
		msgBox.setDefaultButton(btnSel);
		msgBox.exec();

		if (msgBox.clickedButton() == btnSel)
		{
			exportSelected = true;
		}
		else if (msgBox.clickedButton() == btnAll)
		{
			exportSelected = false;
		}
		else
		{
			return;
		}
	}

	QString path = QFileDialog::getSaveFileName(
		this, "Export CSV", QDir::homePath() + "/mediamuster_export.csv",
		"CSV Files (*.csv)");
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		addLog(2, "export", "Cannot write to " + path);
		return;
	}
	QTextStream out(&file);

	out << "Clip Name,Filename,Project,Bin,Kind,Codec,Resolution,FPS,"
		   "Duration,Start TC,Source File,Source Path,Source Container,"
		   "Imported,Size (MB),Volume,Folder,Path,MOB ID,Composition MOB,"
		   "UMID,Type,Date Created\n";

	// RFC-4180 quoting via CsvUtil — embedded commas/quotes/newlines are safe.
	auto writeRow = [&out](const MediaFile &f)
	{
		out << CsvUtil::quoted(f.clipName) << ','
			<< CsvUtil::quoted(f.fileName) << ','
			<< CsvUtil::quoted(f.project) << ','
			<< CsvUtil::quoted(f.originalBin) << ','
			<< CsvUtil::quoted(f.mediaType == MediaFile::Type::Audio ? "Audio" : "Video") << ','
			<< CsvUtil::quoted(f.codec) << ','
			<< CsvUtil::quoted(f.resolution) << ','
			<< CsvUtil::quoted(f.fps) << ','
			<< CsvUtil::quoted(f.durationDisplay()) << ','
			<< CsvUtil::quoted(f.startTimecode) << ','
			<< CsvUtil::quoted(f.sourceFileName) << ','
			<< CsvUtil::quoted(f.sourceFilePath) << ','
			<< CsvUtil::quoted(f.sourceContainer) << ','
			<< (f.isImported ? "yes" : "no") << ','
			<< QString::number(f.sizeMB, 'f', 1) << ','
			<< CsvUtil::quoted(f.driveName) << ','
			<< CsvUtil::quoted(f.mxfFolder) << ','
			<< CsvUtil::quoted(f.filePath) << ','
			<< CsvUtil::quoted(f.mobId) << ','
			<< CsvUtil::quoted(f.compositionMobId) << ','
			<< CsvUtil::quoted(f.umid) << ','
			<< CsvUtil::quoted(f.kind) << ','
			<< (f.created.isValid()
					? f.created.toString("yyyy-MM-dd")
					: f.modified.toString("yyyy-MM-dd"))
			<< '\n';
	};

	int count = 0;
	if (exportSelected)
	{
		for (const auto &f : sel)
		{
			writeRow(f);
			count++;
		}
	}
	else
	{
		for (int row = 0; row < m_proxy->rowCount(); ++row)
		{
			auto si = m_proxy->mapToSource(m_proxy->index(row, 0));
			writeRow(m_model->fileAt(si.row()));
			count++;
		}
	}

	file.close();
	addLog(3, "export",
		   QString("Exported %1 %2 to %3")
			   .arg(count)
			   .arg(exportSelected ? "selected records" : "records")
			   .arg(path));
}

void MainWindow::onProjectSummary()
{
	if (m_allFiles.isEmpty())
	{
		QMessageBox::information(this, "Project Summary",
								 "Hold your horses! Scan some media first to see the project summary.");
		return;
	}
	QMap<QString, ProjectSummary> map;
	for (const auto &f : m_allFiles)
	{
		auto &s = map[f.project];
		s.name = f.project;
		if (f.mediaType == MediaFile::Type::Video)
			s.videoCount++;
		else
			s.audioCount++;
		s.totalBytes += f.sizeBytes;
		if (!f.originalBin.isEmpty() && !s.bins.contains(f.originalBin))
			s.bins.append(f.originalBin);
		if (!s.drives.contains(f.driveName))
			s.drives.append(f.driveName);
	}
	auto *dlg = new QDialog(this);
	dlg->setWindowTitle("Project Summary");
	dlg->resize(600, 380);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	auto *t = new QTableView(dlg);
	auto *sm = new QStandardItemModel(static_cast<int>(map.size()), 5, dlg);
	sm->setHorizontalHeaderLabels(
		{"Project", "Video", "Audio", "Bins", "Size"});
	int r = 0;
	for (auto it = map.begin(); it != map.end(); ++it, ++r)
	{
		const auto &s = it.value();
		sm->setItem(r, 0, new QStandardItem(iconForProject(s.name), s.name));
		sm->setItem(r, 1, new QStandardItem(QString::number(s.videoCount)));
		sm->setItem(r, 2, new QStandardItem(QString::number(s.audioCount)));
		sm->setItem(r, 3,
					new QStandardItem(
						QString::number(static_cast<int>(s.bins.size()))));
		sm->setItem(r, 4, new QStandardItem(formatBytes(s.totalBytes)));
	}
	t->setModel(sm);
	t->setSortingEnabled(true);
	t->setAlternatingRowColors(true);
	t->horizontalHeader()->setStretchLastSection(true);
	t->setColumnWidth(0, 180);
	t->verticalHeader()->setVisible(false);
	auto *l = new QVBoxLayout(dlg);
	l->addWidget(t);
	auto *b = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(b, &QDialogButtonBox::rejected, dlg, &QDialog::close);
	l->addWidget(b);
	dlg->show();
}

void MainWindow::onRevealInFinder()
{
	auto sel = selectedFiles();
	if (sel.isEmpty())
		return;

	const QString path = sel.first().filePath;
	if (path.isEmpty())
	{
		addLog(2, "reveal", "No file path to reveal");
		return;
	}

	QFileInfo fi(path);
	const bool fileExists = fi.exists();
	const QString parentDir = fi.absolutePath();

	// Fallback used by every platform branch.
	auto openParent = [this, parentDir]()
	{
		if (parentDir.isEmpty() || !QFileInfo(parentDir).exists())
		{
			addLog(2, "reveal",
				   "Cannot reveal: file and parent folder are both unreachable");
			return;
		}
#ifdef Q_OS_MAC
		QProcess::startDetached("open", {parentDir});
#else // Q_OS_WIN
		QProcess::startDetached("explorer.exe",
								{QDir::toNativeSeparators(parentDir)});
#endif
	};

	// File gone — open parent unhighlighted.
	if (!fileExists)
	{
		addLog(1, "reveal",
			   QString("File not found, opening parent folder: %1").arg(parentDir));
		openParent();
		return;
	}

#ifdef Q_OS_MAC
	// open -R: fast path, no Automation prompt.
	if (QProcess::startDetached("open", {"-R", path}))
		return;

	// AppleScript fallback — more resilient on network mounts but prompts for Automation.
	QString escapedPath = path;
	escapedPath.replace("\\", "\\\\").replace("\"", "\\\"");
	const QString revealScript =
		QString("tell application \"Finder\" to reveal POSIX file \"%1\"")
			.arg(escapedPath);
	const QStringList args = {
		"-e", revealScript,
		"-e", "tell application \"Finder\" to activate"};
	if (QProcess::startDetached("osascript", args))
		return;

	// Last resort.
	addLog(1, "reveal", "open(1) and osascript both failed; opening parent");
	openParent();

#elif defined(Q_OS_WIN)
	// Reveal with highlight: SHParseDisplayName → explorer.exe /select → parent.
	// Explorer needs exactly: explorer.exe /select,"<path>" — setNativeArguments
	// bypasses Qt's quoting, which would otherwise glue the args into one token.
	const QString nativePath = QDir::toNativeSeparators(path);

	// GetFullPathNameW handles relative components and long paths in one shot.
	std::wstring fullPath(4096, L'\0');
	DWORD fullPathLen = GetFullPathNameW(
		reinterpret_cast<LPCWSTR>(nativePath.utf16()),
		static_cast<DWORD>(fullPath.size()), fullPath.data(), nullptr);
	if (fullPathLen == 0 || fullPathLen >= fullPath.size())
	{
		addLog(1, "reveal",
			   "GetFullPathNameW failed; opening parent folder");
		openParent();
		return;
	}
	fullPath.resize(fullPathLen);
	const QString canonicalPath = QString::fromWCharArray(fullPath.data());

	// Defensive CoInitializeEx; skip CoUninitialize on RPC_E_CHANGED_MODE.
	const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool needCoUninit = (coHr == S_OK || coHr == S_FALSE);

	bool shellApiSucceeded = false;
	PIDLIST_ABSOLUTE pidl = nullptr;
	SFGAOF sfgao = 0;
	HRESULT parseHr = SHParseDisplayName(fullPath.data(), nullptr,
										 &pidl, 0, &sfgao);
	if (SUCCEEDED(parseHr) && pidl)
	{
		HRESULT openHr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
		CoTaskMemFree(pidl);
		shellApiSucceeded = SUCCEEDED(openHr);
		if (!shellApiSucceeded)
		{
			addLog(1, "reveal",
				   QString("SHOpenFolderAndSelectItems failed: 0x%1")
					   .arg(static_cast<quint32>(openHr), 8, 16, QChar('0')));
		}
	}
	else
	{
		addLog(1, "reveal",
			   QString("SHParseDisplayName failed: 0x%1")
				   .arg(static_cast<quint32>(parseHr), 8, 16, QChar('0')));
	}

	if (needCoUninit)
		CoUninitialize();

	if (shellApiSucceeded)
		return;

	// explorer.exe /select,"path" — setNativeArguments preserves Explorer's expected quoting.
	QProcess explorer;
	explorer.setProgram("explorer.exe");
	explorer.setNativeArguments(
		QString("/select,\"%1\"").arg(canonicalPath));
	if (explorer.startDetached())
		return;

	// Last resort: just open the parent folder, no highlight.
	addLog(1, "reveal",
		   "Shell API + explorer.exe /select both failed; opening parent");
	openParent();
#endif
}

void MainWindow::onSelectRelatives()
{
	// Selects every visible row sharing the composition MOB (V01/A01/A02 of one master clip).
	auto sel = selectedFiles();
	if (sel.isEmpty())
		return;

	const QString compMob = sel.first().compositionMobId;
	if (compMob.isEmpty())
	{
		addLog(1, "relatives",
			   "Selected file has no composition MOB — nothing to select");
		return;
	}

	auto *selModel = m_tableView->selectionModel();
	QItemSelection newSelection;
	int rowCount = m_proxy->rowCount();
	int matched = 0;

	for (int row = 0; row < rowCount; ++row)
	{
		QModelIndex proxyIndex = m_proxy->index(row, 0);
		QModelIndex sourceIndex = m_proxy->mapToSource(proxyIndex);
		if (!sourceIndex.isValid())
			continue;

		const MediaFile &f = m_model->fileAt(sourceIndex.row());
		if (f.compositionMobId == compMob)
		{
			// Select the whole row by spanning all columns.
			QModelIndex leftEdge = m_proxy->index(row, 0);
			QModelIndex rightEdge = m_proxy->index(
				row, +MediaTableModel::Column::Count_ - 1);
			newSelection.select(leftEdge, rightEdge);
			++matched;
		}
	}

	if (matched == 0)
	{
		addLog(1, "relatives",
			   "No relatives found (the siblings may be filtered out)");
		return;
	}

	// Replace the existing selection with the relatives set and move the
	// current-row focus to the first match so the user can see it.
	selModel->clearSelection();
	selModel->select(newSelection,
					 QItemSelectionModel::Select | QItemSelectionModel::Rows);
	if (!newSelection.isEmpty())
	{
		QModelIndex first = newSelection.indexes().first();
		selModel->setCurrentIndex(
			first,
			QItemSelectionModel::NoUpdate);
		m_tableView->scrollTo(first, QAbstractItemView::EnsureVisible);
	}

	addLog(3, "relatives",
		   QString("Selected %1 relative%2 (composition MOB: %3…)")
			   .arg(matched)
			   .arg(matched == 1 ? "" : "s")
			   .arg(compMob.right(16)));
}

void MainWindow::onTableDoubleClicked(const QModelIndex &)
{
	onRevealInFinder();
}

QVector<MediaFile> MainWindow::selectedFiles() const
{
	QVector<MediaFile> result;
	for (const auto &pi : m_tableView->selectionModel()->selectedRows())
		result.append(m_model->fileAt(m_proxy->mapToSource(pi).row()));
	return result;
}

void MainWindow::addLog(int level, const QString &module,
						const QString &message)
{
	const char *pfx[] = {"INFO", "WARN", "ERR ", " OK ", "DBG "};
	int idx = (level >= 0 && level <= 4) ? level : 4;
	m_console->appendPlainText(
		QString("%1 [%2] [%3] %4")
			.arg(QTime::currentTime().toString("HH:mm:ss"), pfx[idx], module,
				 message));
}

void MainWindow::autoFitColumns()
{
	m_tableView->resizeColumnsToContents();
	// Clamp at 300px so one long filename can't push the table off-screen.
	for (int i = 0; i < +MediaTableModel::Column::Count_; ++i)
	{
		if (m_tableView->columnWidth(i) > 300)
			m_tableView->setColumnWidth(i, 300);
	}
}

void MainWindow::onAutoFitColumns() { autoFitColumns(); }

void MainWindow::setBusy(bool busy)
{
	m_scanButton->setEnabled(!busy);
	m_scanAllButton->setEnabled(!busy);

	// Manage Media follows onSelectionChanged's "enabled if selected" rule when idle.
	const bool hasSel = m_tableView->selectionModel() && !m_tableView->selectionModel()->selectedRows().isEmpty();
	m_btnFileOps->setEnabled(!busy && hasSel);
	m_btnRebalance->setEnabled(!busy && !m_allFiles.isEmpty());

	if (!busy && m_progressDialog)
		m_progressDialog->finish();
}

ProgressDialog *MainWindow::progressDialog()
{
	// Cancel signal goes to both workers; the idle one's cancel() is a no-op.
	if (!m_progressDialog)
	{
		m_progressDialog = new ProgressDialog(this);
		connect(m_progressDialog, &ProgressDialog::cancelRequested, this, [this]()
				{
			m_scanner->cancelScan();
			m_fileOps->cancel();
			addLog(1, "app", "Cancel requested"); });
	}
	return m_progressDialog;
}

void MainWindow::updateStatusBar()
{
	const int total = m_proxy->rowCount();
	const int grandTotal = static_cast<int>(m_allFiles.size());

	qint64 tb = 0;
	for (int i = 0; i < total; ++i)
		tb += m_model->fileAt(
						 m_proxy->mapToSource(m_proxy->index(i, 0)).row())
				  .sizeBytes;

	if (total < grandTotal)
		m_statusFiles->setText(
			QString("%1 files (filtered from %2)").arg(total).arg(grandTotal));
	else
		m_statusFiles->setText(QString("%1 files").arg(total));

	m_statusSize->setText(formatBytes(tb));
}

void MainWindow::updateFilterCounts()
{
	int v = 0, a = 0, u = 0, bu = 0, ur = 0, np = 0, q = 0;
	for (const auto &f : m_allFiles)
	{
		if (f.mediaType == MediaFile::Type::Video)
			v++;
		if (f.mediaType == MediaFile::Type::Audio)
			a++;
		if (f.isUnmanaged)
			u++;
		if (f.isBadUmid)
			bu++;
		if (f.isUnreferenced)
			ur++;
		if (f.isNonPortable)
			np++;
		if (f.mxfFolder.contains("Quarantined", Qt::CaseInsensitive) || f.filePath.contains("Quarantined", Qt::CaseInsensitive))
			q++;
	}
	m_filterTabs->setTabText(
		0, QString("All (%1)").arg(static_cast<int>(m_allFiles.size())));
	m_filterTabs->setTabText(1, QString("Video (%1)").arg(v));
	m_filterTabs->setTabText(2, QString("Audio (%1)").arg(a));
	m_filterTabs->setTabText(3, QString("Orphaned (%1)").arg(u));
	m_filterTabs->setTabText(4, QString("Bad UMID (%1)").arg(bu));
	m_filterTabs->setTabText(5, QString("Unreferenced (%1)").arg(ur));
	m_filterTabs->setTabText(6, QString("Non-Portable (%1)").arg(np));
	m_filterTabs->setTabText(7, QString("Quarantined (%1)").arg(q));

	// Tab 0 ("All") always visible; others hidden when count==0 unless overridden.
	if (!m_showAllFilterTabs)
	{
		const int counts[] = {0, v, a, u, bu, ur, np, q};
		for (int i = 1; i < 8; ++i)
			m_filterTabs->setTabVisible(i, counts[i] > 0);
	}
	else
	{
		for (int i = 1; i < 8; ++i)
			m_filterTabs->setTabVisible(i, true);
	}
}
QString MainWindow::formatBytes(qint64 bytes)
{
	if (bytes < 0)
		bytes = 0;

	constexpr qint64 KB = 1024;
	constexpr qint64 MB = KB * 1024;
	constexpr qint64 GB = MB * 1024;
	constexpr qint64 TB = GB * 1024;

	if (bytes >= TB)
		return QString("%1 TB").arg(bytes / static_cast<double>(TB), 0, 'f', 1);
	if (bytes >= GB)
		return QString("%1 GB").arg(bytes / static_cast<double>(GB), 0, 'f', 1);
	if (bytes >= MB)
		return QString("%1 MB").arg(bytes / static_cast<double>(MB), 0, 'f', 1);
	if (bytes >= KB)
		return QString("%1 KB").arg(bytes / static_cast<double>(KB), 0, 'f', 0);
	return QString("%1 B").arg(bytes);
}

QIcon MainWindow::iconForDriveType(const QString &driveType, const QString &path)
{
	auto *style = QApplication::style();
	const QString t = driveType.toLower();

	if (t.contains("network") || t.contains("smb") || t.contains("nfs") ||
		t.contains("afp") || t.contains("cifs") || t.contains("nexis"))
		return style->standardIcon(QStyle::SP_DriveNetIcon);
	if (t.contains("cd") || t.contains("dvd") || t.contains("optical"))
		return style->standardIcon(QStyle::SP_DriveCDIcon);
	if (t.contains("removable") || t.contains("usb") || t.contains("flash") ||
		t.contains("floppy"))
		return style->standardIcon(QStyle::SP_DriveFDIcon);

	// Manually-added folders have no driveType; sniff QStorageInfo for network mounts.
	if (!path.isEmpty())
	{
		QStorageInfo si(path);
		if (si.isValid())
		{
			const QString fs =
				QString::fromUtf8(si.fileSystemType()).toLower();
			if (fs.contains("smb") || fs.contains("nfs") ||
				fs.contains("afp") || fs.contains("cifs") ||
				fs.contains("webdav"))
				return style->standardIcon(QStyle::SP_DriveNetIcon);
		}
	}

	return style->standardIcon(QStyle::SP_DriveHDIcon);
}

QIcon MainWindow::iconForProject(const QString &projectName)
{
	auto *style = QApplication::style();

	// UNMANAGED_FILES — warning glyph so it stands out from real projects.
	if (projectName.compare(QLatin1String("UNMANAGED_FILES"),
							Qt::CaseInsensitive) == 0)
		return style->standardIcon(QStyle::SP_MessageBoxWarning);

	return style->standardIcon(QStyle::SP_DirIcon);
}

void MainWindow::rebuildFilterChips()
{
	// Rebuild from scratch — chip count is 0-6, diffing isn't worth it.
	if (!m_chipsBar)
		return;

	auto *layout = m_chipsBar->layout();
	if (!layout)
		return;

	while (auto *child = layout->takeAt(0))
	{
		if (auto *w = child->widget())
			w->deleteLater();
		delete child;
	}

	// Shared chip stylesheet.
	static const char *kChipStyle =
		"QPushButton {"
		" background-color: rgba(74, 144, 226, 0.18);"
		" border: 1px solid rgba(74, 144, 226, 0.55);"
		" border-radius: 11px;"
		" padding: 3px 11px 4px 11px;"
		" font-size: 12px;"
		"}"
		"QPushButton:hover {"
		" background-color: rgba(74, 144, 226, 0.32);"
		"}";

	// Append a chip whose × invokes onClose.
	auto addChip = [this, layout](const QString &text,
								  std::function<void()> onClose)
	{
		auto *chip = new QPushButton(text + "  ✕");
		chip->setCursor(Qt::PointingHandCursor);
		chip->setFlat(false);
		chip->setStyleSheet(kChipStyle);
		chip->setToolTip(tr("Click to remove this filter"));
		QObject::connect(chip, &QPushButton::clicked, this,
						 [cb = std::move(onClose)]()
						 { cb(); });
		layout->addWidget(chip);
	};

	// Strip the trailing "(123)" count so the chip reads "Video" not "Video (123)".
	const int tabIdx = m_filterTabs->currentIndex();
	if (tabIdx > 0)
	{
		QString label = m_filterTabs->tabText(tabIdx);
		const int paren = label.indexOf(QLatin1String(" ("));
		if (paren > 0)
			label.truncate(paren);
		addChip(QString("Type: %1").arg(label),
				[this]()
				{ m_filterTabs->setCurrentIndex(0); });
	}

	// Closing the search chip clears m_searchField.
	const QString searchText = m_searchField->text();
	if (!searchText.isEmpty())
	{
		QString shown = searchText;
		if (shown.length() > 24)
			shown = shown.left(22) + QStringLiteral("…");
		addChip(QString("Search: \"%1\"").arg(shown),
				[this]()
				{ m_searchField->clear(); });
	}

	// One chip per selected project.
	for (auto *item : m_projectList->selectedItems())
	{
		const QString proj = item->data(Qt::UserRole).toString();
		addChip(QString("Project: %1").arg(proj),
				[item]()
				{ item->setSelected(false); });
	}

	// Closing pauses the proxy — chain is preserved in the dialog.
	if (m_binFilterActive)
	{
		const int n = m_binFilterMobCount;
		addChip(QString("Bin filter: %1 MOB%2").arg(n).arg(n == 1 ? "" : "s"),
				[this]()
				{
					m_binFilterActive = false;
					m_binFilterMobCount = 0;
					m_proxy->setBinFilterMobs(false, {});
					rebuildFilterChips();
					updateStatusBar();
					addLog(0, "binfilter",
						   "Bin filter paused (chain preserved in dialog)");
				});
	}

	m_chipsBar->setVisible(layout->count() > 0);
}