#include "mainwindow.h"
#include "binfilterdialog.h"
#include "debugslowdown.h"
#include "dragdroputil.h"
#include "formatutil.h"
#include "managemediadialog.h"
#include "mediamanagerverify.h"
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
#include <QFutureWatcher>
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
#include <QStyleFactory>
#include <QTextStream>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
#include <functional>
#include <QSysInfo>
#include <QOperatingSystemVersion>
#include <QTimer>
#include <array>
#include "csvutil.h"

// MARK: - Log-level prefixes

namespace
{
constexpr std::array<const char *, 5> kLogLevelPfx{"INFO", "WARN", "ERR ", " OK ", "DBG "};
const char *logPfx(int level)
{
	return kLogLevelPfx[(level >= 0 && level < int(kLogLevelPfx.size())) ? level : 4];
}

// Console log-line format: `HH:MM:SS [LEVEL] [module] message`.
// Shared by addLog and onScanLogBatch.
QString formatLogLine(const QString &time, int level,
                      const QString &module, const QString &message)
{
	return QStringLiteral("%1 [%2] [%3] %4")
	    .arg(time, QLatin1String(logPfx(level)), module, message);
}
} // namespace

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>  // SHOpenFolderAndSelectItems, SHParseDisplayName
#include <objbase.h> // CoInitializeEx — Shell API needs COM apartment
#include <QSettings> // registry read for Windows DisplayVersion + UBR
#endif

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
#include <QtConcurrent>

// MARK: - VolumeListWidget

VolumeListWidget::VolumeListWidget(QWidget *parent)
    : QListWidget(parent)
{
	setAcceptDrops(true);
	setDragDropMode(QAbstractItemView::DropOnly);
}

void VolumeListWidget::setDropHighlight(bool on)
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

// Shared by dragEnterEvent and dragMoveEvent so both honour the
// same drop-accept rules.
static bool dragHasLocalDir(const QMimeData *mime)
{
	return DragDropUtil::hasAnyLocalUrl(mime, [](const QString &path)
	                                    { return QFileInfo(path).isDir(); });
}

void VolumeListWidget::dragEnterEvent(QDragEnterEvent *event)
{
	if (dragHasLocalDir(event->mimeData()))
	{
		event->acceptProposedAction();
		setDropHighlight(true);
	}
	else
	{
		event->ignore();
	}
}

void VolumeListWidget::dragMoveEvent(QDragMoveEvent *event)
{
	if (dragHasLocalDir(event->mimeData()))
		event->acceptProposedAction();
	else
		event->ignore();
}

void VolumeListWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
	setDropHighlight(false);
	QListWidget::dragLeaveEvent(event);
}

void VolumeListWidget::dropEvent(QDropEvent *event)
{
	setDropHighlight(false);
	QStringList paths;
	for (const QUrl &url : event->mimeData()->urls())
	{
		if (!url.isLocalFile())
			continue;
		const QString path = url.toLocalFile();
		const QFileInfo fi(path);
		// Filter to directories that actually exist and are readable.
		if (fi.isDir() && fi.exists() && fi.isReadable())
			paths.append(path);
	}
	if (!paths.isEmpty())
	{
		emit pathsDropped(paths);
		event->acceptProposedAction();
	}
}

// MARK: - MediaTableModel

MediaTableModel::MediaTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int MediaTableModel::rowCount(const QModelIndex &) const
{
	return m_files.size();
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

void MediaTableModel::removeFilesByPath(const QSet<QString> &paths)
{
	if (paths.isEmpty() || m_files.isEmpty())
		return;

	// Back-to-front so lower-index rows stay valid through each erase.
	// Groups contiguous removals into begin/endRemoveRows ranges.
	int rangeEnd = -1; // -1 means "no range in progress"
	for (int i = m_files.size() - 1; i >= 0; --i)
	{
		const bool removeThis = paths.contains(m_files[i].filePath);
		if (removeThis && rangeEnd == -1)
		{
			rangeEnd = i;
		}
		else if (!removeThis && rangeEnd != -1)
		{
			beginRemoveRows({}, i + 1, rangeEnd);
			m_files.erase(m_files.begin() + i + 1,
			              m_files.begin() + rangeEnd + 1);
			endRemoveRows();
			rangeEnd = -1;
		}
	}
	if (rangeEnd != -1)
	{
		beginRemoveRows({}, 0, rangeEnd);
		m_files.erase(m_files.begin(), m_files.begin() + rangeEnd + 1);
		endRemoveRows();
	}
}

const MediaFile &MediaTableModel::fileAt(int row) const
{
	return m_files[row];
}

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
	if (!index.isValid() || index.row() >= m_files.size())
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
			return f.volumeDisplay;
		case Column::Created:
			return f.created.isValid()
			           ? f.created.toString("yyyy-MM-dd HH:mm")
			           : f.modified.toString("yyyy-MM-dd HH:mm");
		case Column::Source:
			return f.sourceFileName;
		case Column::Type:
			return f.kind;
		case Column::Count_:
			break;
		}
	}
	if (role == Qt::TextAlignmentRole && static_cast<Column>(index.column()) == Column::SizeMB)
		return QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
	if (role == Qt::ToolTipRole)
	{
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
			    "The filepath for this clip.");
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

// MARK: - MediaFilterProxy

MediaFilterProxy::MediaFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void MediaFilterProxy::setFilterMode(FilterMode mode)
{
	m_mode = mode;
	invalidateRowsFilter();
}

void MediaFilterProxy::setSearchText(const QString &text)
{
	// filterAcceptsRow uses Qt::CaseInsensitive contains — folds
	// inline, no throwaway lowercased copies.
	m_search = text;
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
	m_binFilterActive = isActive;
	m_binFilterAcceptedMobs = isActive ? acceptedMobs : QSet<QString>{};
	invalidateRowsFilter();
}

bool MediaFilterProxy::filterAcceptsRow(int row,
                                        const QModelIndex &parent) const
{
	Q_UNUSED(parent);
	auto *model = qobject_cast<MediaTableModel *>(sourceModel());
	if (!model || row >= model->allFiles().size())
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

	if (!m_selectedProjects.isEmpty() && !m_selectedProjects.contains(f.project))
		return false;

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
		// Qt::CaseInsensitive folds on the fly during the compare.
		return f.clipName.contains(m_search, Qt::CaseInsensitive) ||
		       f.project.contains(m_search, Qt::CaseInsensitive) ||
		       f.originalBin.contains(m_search, Qt::CaseInsensitive) ||
		       f.mxfFolder.contains(m_search, Qt::CaseInsensitive) ||
		       f.codec.contains(m_search, Qt::CaseInsensitive) ||
		       f.volumeName.contains(m_search, Qt::CaseInsensitive) ||
		       f.fileName.contains(m_search, Qt::CaseInsensitive) ||
		       f.sourceFileName.contains(m_search, Qt::CaseInsensitive) ||
		       f.sourceFilePath.contains(m_search, Qt::CaseInsensitive) ||
		       f.startTimecode.contains(m_search, Qt::CaseInsensitive);
	}
	return true;
}

bool MediaFilterProxy::lessThan(const QModelIndex &left,
                                const QModelIndex &right) const
{
	const QVariant lv = sourceModel()->data(left, Qt::UserRole);
	const QVariant rv = sourceModel()->data(right, Qt::UserRole);
	switch (static_cast<MediaTableModel::Column>(left.column()))
	{
	case MediaTableModel::Column::SizeMB:
		return lv.toDouble() < rv.toDouble();
	case MediaTableModel::Column::Created:
		return lv.toDateTime() < rv.toDateTime();
	default:
		// Qt::CaseInsensitive folds inline — no toLower() copies.
		// Saves ~20M allocations per sort on 300K rows.
		return QString::compare(lv.toString(), rv.toString(),
		                        Qt::CaseInsensitive) < 0;
	}
}

// MARK: - MainWindow construction

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
	m_volumeManager = new VolumeManager(this);
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

	addLog(0, "app", QString("%1 %2 initialised").arg(APP_NAME, APP_VERSION));

#ifdef Q_OS_MAC
	{
		const auto v = QOperatingSystemVersion::current();
		addLog(0, "app",
		       QString("Platform: %1 %2.%3.%4")
		           .arg(v.name())
		           .arg(v.majorVersion())
		           .arg(v.minorVersion())
		           .arg(v.microVersion()));
	}
#else
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

	onDetectVolumes();
	m_volumeManager->startMonitoring();

#ifdef Q_OS_MAC
	if (!VolumeManager::hasFullDiskAccess())
	{
		addLog(1, "app",
		       "Full Disk Access not granted. Some locations may be "
		       "inaccessible.");
		addLog(0, "app",
		       "Go to System Preferences > Privacy & Security > Full Disk "
		       "Access, to grant permission.");
	}
	else
	{
		addLog(3, "app", "Full Disk Access: granted!");
	}
#endif // Q_OS_MAC
}

MainWindow::~MainWindow() = default;

// MARK: - UI layout

void MainWindow::setupUi()
{
	m_sidePanel = new QWidget;
	m_sidePanel->setFixedWidth(260);
	auto *sideLayout = new QVBoxLayout(m_sidePanel);
	sideLayout->setContentsMargins(0, 0, 0, 0);
	sideLayout->setSpacing(12);

	auto *volGroup = new QGroupBox(tr("Volumes"));
	auto *volLayout = new QVBoxLayout(volGroup);
	volLayout->setContentsMargins(6, 6, 6, 6);
	volLayout->setSpacing(4);

	m_volumeList = new VolumeListWidget;
	m_volumeList->setSelectionMode(QAbstractItemView::MultiSelection);
	m_volumeList->setMinimumHeight(60);
	m_volumeList->setMaximumHeight(140);
	m_volumeList->setToolTip(
	    tr("Drag folders or volumes here to add them."));
	m_volumeList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	volLayout->addWidget(m_volumeList);

	m_scanButton = new QPushButton(tr("Scan Selected"));
	m_scanButton->setDefault(true);
	volLayout->addWidget(m_scanButton);

	m_scanAllButton = new QPushButton(tr("Scan All"));
	m_scanAllButton->setToolTip(
	    tr("Scan every mounted volume and known Avid location"));
	volLayout->addWidget(m_scanAllButton);

	sideLayout->addWidget(volGroup);

	auto *projGroup = new QGroupBox(tr("Projects"));
	auto *projLayout = new QVBoxLayout(projGroup);
	projLayout->setContentsMargins(6, 6, 6, 6);
	m_projectList = new QListWidget;
	m_projectList->setSelectionMode(QAbstractItemView::MultiSelection);
	// Prevent long project names from blowing out the splitter.
	m_projectList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	projLayout->addWidget(m_projectList);
	sideLayout->addWidget(projGroup);

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

	m_searchField = new QLineEdit;
	m_searchField->setPlaceholderText(tr("Search"));
	m_searchField->setClearButtonEnabled(true);
	m_searchField->setMinimumWidth(200);
	m_searchField->setMaximumWidth(320);

	m_btnFileOps = new QPushButton(tr("Manage Media..."));
	m_btnBinFilter = new QPushButton(tr("Filter by Bin..."));
	m_btnExport = new QPushButton(tr("Export CSV..."));
	m_btnRebalance = new QPushButton(tr("Rebalance..."));
	m_btnRebalance->setToolTip(
	    tr("Redistribute MXF files so each MediaFiles folder stays "
	       "under 5,000 files (Avid recommendation)."));
	m_btnFileOps->setEnabled(false);
	m_btnRebalance->setEnabled(false);

	// Hidden until a filter is active; rebuilt from scratch by
	// rebuildFilterChips on every filter change.
	m_chipsBar = new QWidget;
	m_chipsBar->setVisible(false);
	auto *chipsInternal = new QHBoxLayout(m_chipsBar);
	chipsInternal->setContentsMargins(0, 0, 0, 0);
	chipsInternal->setSpacing(6);

	auto *toolbarWidget = new QWidget;
	auto *toolbarV = new QVBoxLayout(toolbarWidget);
	toolbarV->setContentsMargins(12, 8, 12, 8);
	toolbarV->setSpacing(8);

	auto *actionsRow = new QHBoxLayout;
	actionsRow->setContentsMargins(0, 0, 0, 0);
	actionsRow->setSpacing(8);
	actionsRow->addWidget(m_btnFileOps);
	actionsRow->addWidget(m_btnBinFilter);
	actionsRow->addWidget(m_btnRebalance);
	actionsRow->addWidget(m_btnExport);
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

	// Starting widths. autoFitColumns runs once after the first
	// scan to size them to actual content.
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

	// Default - table dominates the window; console sits at its minimum size.
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

	setCentralWidget(m_mainSplitter);

	m_statusFiles = new QLabel(tr("0 files"));
	m_statusSize = new QLabel(tr("0 GB"));
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

// MARK: - Menus

void MainWindow::setupMenus()
{
	auto *fileMenu = menuBar()->addMenu("&File");

	auto *a2 = fileMenu->addAction(tr("Add &Folder or Volume"));
	a2->setShortcut(QKeySequence("Ctrl+O"));
	connect(a2, &QAction::triggered, this, [this]()
	        {
        QString dir =
            QFileDialog::getExistingDirectory(this, "Add Volume or Folder");
        if (!dir.isEmpty())
            addVolumePath(dir); });

	fileMenu->addSeparator();

	auto *a1 = fileMenu->addAction(tr("Scan &Selected"));
	connect(a1, &QAction::triggered, this, &MainWindow::onScanClicked);

	auto *a1b = fileMenu->addAction(tr("Scan &All"));
	a1b->setShortcut(QKeySequence("Ctrl+Shift+A"));
	connect(a1b, &QAction::triggered, this, &MainWindow::onScanAllClicked);

	auto *ra = fileMenu->addAction(tr("Refresh &Volumes"));
	ra->setShortcut(QKeySequence("Ctrl+Shift+D"));
	connect(ra, &QAction::triggered, this, &MainWindow::onDetectVolumes);

	fileMenu->addSeparator();

	auto *revealAct = fileMenu->addAction(tr("Reveal in Finder"));
	revealAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
	connect(revealAct, &QAction::triggered, this,
	        &MainWindow::onRevealInFinder);

	fileMenu->addSeparator();

	auto *a3 = fileMenu->addAction(tr("&Export CSV..."));
	a3->setShortcut(QKeySequence("Ctrl+E"));
	connect(a3, &QAction::triggered, this, &MainWindow::onExportCsv);

#ifndef Q_OS_MAC
	fileMenu->addSeparator();
	auto *a4 = fileMenu->addAction(tr("&Quit"));
	a4->setShortcut(QKeySequence("Ctrl+Q"));
	connect(a4, &QAction::triggered, qApp, &QApplication::quit);
#endif

	auto *editMenu = menuBar()->addMenu("&Edit");
	auto *a5 = editMenu->addAction(tr("&Find"));
	a5->setShortcut(QKeySequence::Find);
	connect(a5, &QAction::triggered, m_searchField,
	        qOverload<>(&QWidget::setFocus));
	auto *a6 = editMenu->addAction(tr("Select &All"));
	a6->setShortcut(QKeySequence::SelectAll);
	connect(a6, &QAction::triggered, m_tableView, &QTableView::selectAll);

	// Picks every visible row sharing the selected file's
	// composition MOB — e.g. V01 + A01 + A02 of the same master.
	auto *relAct = editMenu->addAction(tr("Select &Relatives"));
	relAct->setShortcut(QKeySequence("Ctrl+Shift+L"));
	connect(relAct, &QAction::triggered, this,
	        &MainWindow::onSelectRelatives);

	auto *viewMenu = menuBar()->addMenu("&View");
	auto *ca = viewMenu->addAction(tr("Show &Console"));
	ca->setCheckable(true);
	ca->setChecked(true);
	connect(ca, &QAction::triggered, this, [this](bool c)
	        { m_console->setVisible(c); });

	// Off by default — empty tabs hide themselves so the bar isn't
	// cluttered with (0) placeholders.
	auto *showAllTabsAct = viewMenu->addAction(tr("Show &All Filter Tabs"));
	showAllTabsAct->setCheckable(true);
	showAllTabsAct->setChecked(false);
	connect(showAllTabsAct, &QAction::triggered, this, [this](bool on)
	        {
		m_showAllFilterTabs = on;
		updateFilterCounts(); });

	viewMenu->addSeparator();

	auto *fitAct = viewMenu->addAction(tr("&Resize Columns to Fit"));
	fitAct->setShortcut(QKeySequence("Ctrl+T"));
	connect(fitAct, &QAction::triggered, this, &MainWindow::autoFitColumns);

	auto *specialMenu = menuBar()->addMenu("&Special");
	auto *sa = specialMenu->addAction(tr("&Project Summary"));
	connect(sa, &QAction::triggered, this, &MainWindow::onProjectSummary);

	specialMenu->addSeparator();
	auto *bfa = specialMenu->addAction(tr("Filter by &Bin..."));
	bfa->setShortcut(QKeySequence("Ctrl+Shift+B"));
	connect(bfa, &QAction::triggered, this, &MainWindow::onFilterByBins);

	specialMenu->addSeparator();
	auto *rba = specialMenu->addAction(tr("&Rebalance..."));
	connect(rba, &QAction::triggered, this, &MainWindow::onRebalance);

#ifdef Q_OS_MAC
	specialMenu->addSeparator();
	auto *pa = specialMenu->addAction(tr("Check &Permissions..."));
	connect(pa, &QAction::triggered, this, &MainWindow::onCheckPermissions);
#endif

	auto *debugMenu = menuBar()->addMenu("&Debug");
	auto *slowAct = debugMenu->addAction(tr("&Slow mode"));
	slowAct->setCheckable(true);
	slowAct->setChecked(false);
	connect(slowAct, &QAction::triggered, this, [this](bool on)
	        {
		DebugSlowdown::setEnabled(on);
		addLog(0, "app", on ? "Slow Mode enabled (1/50th speed)"
							: "Slow Mode disabled"); });

	auto *verifyAct = debugMenu->addAction(tr("&Verification checks"));
	verifyAct->setCheckable(true);
	verifyAct->setChecked(MediaManagerVerify::enabled());
	connect(verifyAct, &QAction::triggered, this, [this](bool on)
	        {
		MediaManagerVerify::setEnabled(on);
		addLog(0, "app", on ? "Verification checks enabled"
							: "Verification checks disabled"); });

	auto *rawHexAct = debugMenu->addAction(tr("&Raw hex"));
	rawHexAct->setCheckable(true);
	rawHexAct->setChecked(false);
	connect(rawHexAct, &QAction::triggered, this, [this](bool on)
	        {
		m_model->setShowRawCodecHex(on);
		addLog(0, "app", on ? "Codec as Raw Hex enabled"
							: "Codec as Raw Hex disabled"); });

#ifdef Q_OS_MAC
	const QString nativeStyleName = QStringLiteral("macos");
#elif defined(Q_OS_WIN)
	const QString nativeStyleName = QStringLiteral("windows");
#else
	const QString nativeStyleName = QStringLiteral("fusion");
#endif
	auto *fusionAct = debugMenu->addAction(tr("&Fusion"));
	fusionAct->setCheckable(true);
	fusionAct->setChecked(false);
	connect(fusionAct, &QAction::triggered, this, [this, nativeStyleName](bool on)
	        {
		const QString target = on ? QStringLiteral("fusion") : nativeStyleName;
		QApplication::setStyle(QStyleFactory::create(target));
		addLog(0, "app", QStringLiteral("Style: %1").arg(target)); });

	auto *helpMenu = menuBar()->addMenu("&Help");
	auto *aa = helpMenu->addAction(tr("About MediaMuster"));
	connect(aa, &QAction::triggered, this, &MainWindow::onAbout);
}

// MARK: - About dialog

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

	auto *version = new QLabel(tr("Version %1").arg(APP_VERSION));
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

	auto *credits = new QLabel(
	    tr("Built with <a href=\"https://qt.io/download-qt-installer\">Qt %1</a> under GNU LGPL v3.<br>"
	       "Developed from examination of files,<br>"
	       "no copyright infringement intended.")
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

// MARK: - Signal wiring

void MainWindow::setupConnections()
{
	connect(
	    m_volumeManager, &VolumeManager::volumesChanged, this,
	    [this](const QVector<VolumeInfo> &volumes)
	    {
		    // The poller's pool thread already did the work — use the
		    // vector the signal carries.
		    rebuildVolumeList(volumes);
	    },
	    Qt::QueuedConnection);

	connect(m_scanner, &MediaScanner::scanProgress, this,
	        &MainWindow::onScanProgress, Qt::QueuedConnection);
	connect(m_scanner, &MediaScanner::scanLogBatch, this,
	        &MainWindow::onScanLogBatch, Qt::QueuedConnection);
	connect(m_scanner, &MediaScanner::scanFinished, this,
	        &MainWindow::onScanFinished, Qt::QueuedConnection);

	connect(
	    m_fileOps, &MediaManager::operationProgress, this,
	    [this](const QString &name, int cur, int total, double)
	    {
		    auto *dlg = progressDialog();
		    if (!dlg->isVisible())
		    {
			    setBusy(true);
			    dlg->begin(m_pendingOpTitle.isEmpty()
			                   ? tr("Processing files...")
			                   : m_pendingOpTitle);
		    }
		    dlg->setProgress(cur, total);
		    dlg->setDetail(name);
	    },
	    Qt::QueuedConnection);
	connect(
	    m_fileOps, &MediaManager::operationLog, this,
	    [this](int level, const QString &message)
	    {
		    addLog(level, "ops", message);
		    // Surface verify activity so big copies don't look frozen
		    // between 'bytes finished' and 'verify pass done'.
		    if (message.startsWith(QStringLiteral("Verifying ")))
		    {
			    if (auto *dlg = progressDialog(); dlg && dlg->isVisible())
				    dlg->setDetail(message);
		    }
	    },
	    Qt::QueuedConnection);
	connect(
	    m_fileOps, &MediaManager::operationItemDone, this,
	    [this](const QString &name, const QString &filePath, bool ok,
	           const QString &err, bool skipped)
	    {
		    addLog(ok ? 3 : 2, "ops",
		           ok ? name + " done" : name + " FAILED: " + err);

		    // Track successful paths so we can prune the table once
		    // the job finishes. Skipped items left the source alone
		    // — pruning their rows would lie about table state.
		    if (ok && !skipped && m_removeAfterOp)
			    m_successfulOpPaths.insert(filePath);
	    },
	    Qt::QueuedConnection);
	connect(
	    m_fileOps, &MediaManager::operationFinished, this,
	    [this](int ok, int fail)
	    {
		    setBusy(false);
		    addLog(fail > 0 ? 1 : 3, "ops",
		           QString("Complete: %1 ok, %2 failed").arg(ok).arg(fail));

		    // After Move/Delete, prune successful files so the table
		    // reflects reality without a re-scan. begin/endRemoveRows
		    // per contiguous range preserves scroll and selection.
		    if (m_removeAfterOp && !m_successfulOpPaths.isEmpty())
		    {
			    const int removedCount = m_successfulOpPaths.size();
			    m_model->removeFilesByPath(m_successfulOpPaths);
			    m_allFiles = m_model->allFiles();
			    updateFilterCounts();
			    updateStatusBar();
			    addLog(0, "ops",
			           QString("Removed %1 files from table")
			               .arg(removedCount));
		    }
		    m_removeAfterOp = false;
		    m_successfulOpPaths.clear();
		    m_pendingOpTitle.clear();
	    },
	    Qt::QueuedConnection);

	// Network volumes have no OS recycle bin, so deletes there
	// land in a per-volume `_MediaMuster_Trash` folder.
	connect(m_fileOps, &MediaManager::mediaMusterTrashUsed,
	        this, &MainWindow::showMediaMusterTrashDialog,
	        Qt::QueuedConnection);

	connect(m_filterTabs, &QTabBar::currentChanged, this,
	        &MainWindow::onFilterChanged);

	// 200 ms debounce on the expensive proxy invalidation. Chip
	// strip still updates per-keystroke for instant feedback.
	{
		auto *searchDebounce = new QTimer(this);
		searchDebounce->setSingleShot(true);
		searchDebounce->setInterval(200);
		connect(m_searchField, &QLineEdit::textChanged,
		        searchDebounce, qOverload<>(&QTimer::start));
		connect(searchDebounce, &QTimer::timeout, this, [this]()
		        { onSearchChanged(m_searchField->text()); });

		connect(m_searchField, &QLineEdit::textChanged, this,
		        [this]()
		        { rebuildFilterChips(); });
	}

	// 100 ms debounce on the status bar's O(n) byte walk.
	// updateStatusBar restarts the timer; doUpdateStatusBar runs
	// once the burst settles.
	m_statusBarUpdateTimer = new QTimer(this);
	m_statusBarUpdateTimer->setSingleShot(true);
	m_statusBarUpdateTimer->setInterval(100);
	connect(m_statusBarUpdateTimer, &QTimer::timeout, this,
	        &MainWindow::doUpdateStatusBar);

	// Same pattern for the 'X MB selected' string — cheap state
	// runs synchronously, byte sum waits for the timer.
	m_selectionBytesTimer = new QTimer(this);
	m_selectionBytesTimer->setSingleShot(true);
	m_selectionBytesTimer->setInterval(100);
	connect(m_selectionBytesTimer, &QTimer::timeout, this,
	        &MainWindow::doUpdateSelectionBytes);
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

	connect(m_volumeList, &VolumeListWidget::pathsDropped, this,
	        &MainWindow::onPathsDropped);

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

	connect(m_tableView, &QTableView::customContextMenuRequested,
	        this, &MainWindow::showTableContextMenu);

	new QShortcut(QKeySequence::Delete, this, [this]()
	              { openManageMedia(+ManageMediaDialog::Operation::Delete); });

	new QShortcut(QKeySequence(Qt::Key_Escape), this, [this]()
	              {
					  m_searchField->clear();
					  m_projectList->clearSelection();
					  m_filterTabs->setCurrentIndex(0); });
}

// MARK: - Full Disk Access prompt

void MainWindow::onCheckPermissions()
{
#ifdef Q_OS_MAC
	bool hasFDA = VolumeManager::hasFullDiskAccess();
	if (hasFDA)
	{
		QMessageBox::information(
		    this, tr("Permissions"),
		    tr("Full Disk Access is <b>granted</b>.<br><br>"
		       "I can muster all volumes and folders on this Mac!"));
	}
	else
	{
		const auto ret = QMessageBox::warning(
		    this, tr("Permissions"),
		    tr("Full Disk Access is <b>not granted</b>.<br><br>"
		       "I may not be able to muster all your media.<br><br>"
		       "Would you like to open System Preferences to grant access?<br><br>"
		       "<i>After granting access, quit and relaunch MediaMuster.</i>"),
		    QMessageBox::Yes | QMessageBox::No);
		if (ret == QMessageBox::Yes)
		{
			VolumeManager::openFullDiskAccessSettings();
		}
	}
#endif // Q_OS_MAC
}

// MARK: - Bin filter

void MainWindow::onFilterByBins()
{
	// Lazy construction — dialog stays parented to the main window
	// so chain state persists across show/hide.
	if (!m_binFilterDialog)
	{
		m_binFilterDialog = new BinFilterDialog(this);
		connect(m_binFilterDialog, &BinFilterDialog::filterChainChanged,
		        m_proxy, &MediaFilterProxy::setBinFilterMobs);
		connect(m_binFilterDialog, &BinFilterDialog::filterChainChanged, this,
		        [this](bool isActive, const QSet<QString> &acceptedMobs)
		        {
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

			        // Log a bin sample + file sample side-by-side to spot
			        // namespace mismatches (case, dotted vs undotted hex).
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

// MARK: - Rebalance

// Gathers the volume > mxfRoot map from the indexed files so the
// picker only shows volumes with scanned data.
void MainWindow::onRebalance()
{
	if (m_allFiles.isEmpty())
	{
		QMessageBox::information(
		    this, tr("Rebalance"),
		    tr("Please scan a volume first. I need to know where things are before I can rebalance them."));
		return;
	}

	// MXF root = grandparent of the file:
	//   <volume>/Avid MediaFiles/MXF/<folder>/<file.mxf>
	//                              ^^^^^^^^ this is the root we want
	QHash<QString, QString> mxfRootsByLabel;
	QHash<QString, QVector<MediaFile>> filesByMxfRoot;
	QHash<QString, int> countByLabel;
	QHash<QString, QString> volumePathByLabel;

	for (const MediaFile &mf : m_allFiles)
	{
		if (mf.filePath.isEmpty())
			continue;
		const QString folderDir = QFileInfo(mf.filePath).absolutePath();
		const QString mxfRoot = QFileInfo(folderDir).absolutePath();

		// Grandparent must be the literal 'MXF' folder — anything
		// else isn't safe to rebalance.
		if (QFileInfo(mxfRoot).fileName() != QStringLiteral("MXF"))
			continue;

		QString label = mf.volumeName;
		if (label.isEmpty())
			label = QFileInfo(mf.volumePath).fileName();
		if (label.isEmpty())
			label = mxfRoot;

		mxfRootsByLabel.insert(label, mxfRoot);
		filesByMxfRoot[mxfRoot].append(mf);
		countByLabel[label] += 1;
		if (!volumePathByLabel.contains(label))
			volumePathByLabel.insert(label, mf.volumePath);
	}

	if (filesByMxfRoot.isEmpty())
	{
		QMessageBox::warning(
		    this, tr("Rebalance"),
		    tr("No 'Avid MediaFiles/MXF' folders were found in "
		       "the current scan. Rebalance only operates on "
		       "Avid's file structure."));
		return;
	}

	// Default to whichever volume has the most scanned files.
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
	       QString("Opening rebalance dialog (%1 volume(s), default '%2')")
	           .arg(mxfRootsByLabel.size())
	           .arg(initialLabel));

	RebalanceDialog dlg(mxfRootsByLabel, filesByMxfRoot,
	                    initialLabel, this);
	connect(&dlg, &RebalanceDialog::logMessage, this,
	        [this](int level, const QString &msg)
	        { addLog(level, QStringLiteral("rebalance"), msg); });
	dlg.exec();

	// Re-scan if didRebalance — a cancelled run can still have
	// moved files.
	if (!dlg.didRebalance())
		return;

	const QString volumePath = volumePathByLabel.value(dlg.rebalancedLabel());
	if (volumePath.isEmpty())
	{
		addLog(1, "rebalance",
		       "Couldn't determine volume path for re-scan; please scan "
		       "manually");
		return;
	}
	addLog(0, "rebalance",
	       QString("Re-scanning '%1' after rebalance").arg(volumePath));
	startScanWithPaths(QStringList() << volumePath);
}

// MARK: - Volume list management

void MainWindow::onPathsDropped(const QStringList &paths)
{
	for (const QString &path : paths)
		addVolumePath(path);
}

void MainWindow::addVolumePath(const QString &path)
{
	for (int i = 0; i < m_volumeList->count(); ++i)
	{
		if (m_volumeList->item(i)->data(Qt::UserRole).toString() == path)
		{
			m_volumeList->item(i)->setSelected(true);
			return;
		}
	}
	QFileInfo fi(path);
	QString name = fi.fileName();
	if (name.isEmpty())
		name = path;

	// Manually-added paths have unknown volume type; icon helper
	// sniffs QStorageInfo.
	auto *item = new QListWidgetItem(iconForVolumeType({}, path), name);
	item->setData(Qt::UserRole, path);

	QString tooltip = path;
	QStorageInfo si(path);
	if (si.isValid() && si.bytesTotal() > 0)
	{
		const qint64 used = si.bytesTotal() - si.bytesAvailable();
		tooltip += QString("\n%1 of %2 used")
		               .arg(Format::bytes(used), Format::bytes(si.bytesTotal()));
	}
	item->setToolTip(tooltip);
	item->setSelected(true);
	m_volumeList->addItem(item);
	m_manualVolumes.insert(path);
	addLog(0, "volumes", QString("Added: %1").arg(path));
}

void MainWindow::onDetectVolumes()
{
	// Synchronous re-detect and rebuild. Seed the VolumeManager
	// cache so the next async poll's identity check has a baseline.
	auto drives = m_volumeManager->detectVolumes();
	m_volumeManager->seedLastVolumes(drives);
	rebuildVolumeList(std::move(drives));
}

void MainWindow::rebuildVolumeList(const QVector<VolumeInfo> &volumes)
{
	// Snapshot selected paths so the rebuild can restore ticks.
	// Otherwise a 'hot' volume mount would clear the selection.
	QSet<QString> previouslySelected;
	for (auto *item : m_volumeList->selectedItems())
		previouslySelected.insert(item->data(Qt::UserRole).toString());

	QSet<QString> manualCopy = m_manualVolumes;
	m_volumeList->clear();

	// Count name collisions (e.g. 'Macintosh HD' + its APFS data
	// relative) so we know which entries need a suffix.
	QHash<QString, int> nameCounts;
	for (const auto &d : volumes)
		++nameCounts[d.name];

	// Disk Utility-style suffix: system root keeps its bare name,
	// every other colliding volume gets the last path component
	// appended. So `/System/Volumes/Data` becomes
	// 'Macintosh HD - Data'.
	auto suffixForPath = [](const QString &path, const QString &name) -> QString
	{
		if (path == QLatin1String("/") || path.isEmpty())
			return {};

		QString p = path;
		while (p.endsWith(QLatin1Char('/')) || p.endsWith(QLatin1Char('\\')))
			p.chop(1);

		QString basename = QFileInfo(p).fileName();
		if (basename.isEmpty())
			basename = p;

		// If basename echoes the volume name (e.g. /Volumes/Backup
		// and /Volumes/Backup-1 both 'Backup'), fall back to the
		// full path to keep rows distinct.
		if (basename.compare(name, Qt::CaseInsensitive) == 0)
			return path;
		return basename;
	};

	for (const VolumeInfo &d : volumes)
	{
		QString displayName = d.name;
		if (nameCounts.value(d.name) > 1)
		{
			const QString suffix = suffixForPath(d.path, d.name);
			if (!suffix.isEmpty())
				displayName = QString("%1 - %2").arg(d.name, suffix);
		}

		auto *item = new QListWidgetItem(
		    iconForVolumeType(d.volumeType, d.path), displayName);
		item->setData(Qt::UserRole, d.path);

		QString tooltip = d.path;
		if (d.totalBytes > 0)
		{
			tooltip += QString("\n%1 of %2 used (%3)")
			               .arg(Format::bytes(d.usedBytes),
			                    Format::bytes(d.totalBytes),
			                    d.volumeType);
		}
		item->setToolTip(tooltip);

		if (d.hasAvidMedia)
		{
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
		}

		// Preserve previous ticks for paths that still exist, and
		// auto-select newly mounted volumes.
		const bool wasSelected = previouslySelected.contains(d.path);
		const bool newAvidVolume = d.hasAvidMedia && !wasSelected && !previouslySelected.isEmpty();
		// Empty previouslySelected = cold start (auto-select every
		// Avid volume); non-empty = hot mount (only auto-select new).
		const bool coldStart = previouslySelected.isEmpty() && d.hasAvidMedia;
		if (wasSelected || newAvidVolume || coldStart)
			item->setSelected(true);

		m_volumeList->addItem(item);
		// Already detected? Drop from the manual set so we don't
		// double-add the same volume below.
		manualCopy.remove(d.path);
	}

	for (const QString &mp : manualCopy)
		addVolumePath(mp);

	int ac = 0;
	for (const auto &d : volumes)
		if (d.hasAvidMedia)
			++ac;
	addLog(0, "volumes",
	       QString("Found %1 volumes (%2 with Avid MediaFiles)")
	           .arg(volumes.size())
	           .arg(ac));
}

// MARK: - Scan controls

void MainWindow::onScanClicked()
{
	if (m_scanner->isRunning())
	{
		m_scanner->cancelScan();
		return;
	}

	QStringList paths;
	for (auto *item : m_volumeList->selectedItems())
		paths << item->data(Qt::UserRole).toString();

	if (paths.isEmpty())
	{
		QMessageBox::information(
		    this, tr("No Volumes Selected"),
		    tr("Select at least one volume, or use \"Scan All Volumes\"."));
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

	QStringList paths = m_volumeManager->allScannablePaths();
	for (const QString &mp : m_manualVolumes)
	{
		if (!paths.contains(mp))
			paths.append(mp);
	}

	addLog(0, "scanner", QString("Scan All: %1 locations").arg(paths.size()));
	startScanWithPaths(paths);
}

void MainWindow::startScanWithPaths(const QStringList &paths)
{
	MediaScanner::Options opts;
	opts.volumePaths = paths;
	opts.parseMxfHeaders = true;

	setBusy(true);
	progressDialog()->begin(tr("Scanning media..."));
	progressDialog()->setDetail(tr("Starting scan..."));
	m_scanTimer.start();

	m_scanner->startScan(opts);
}

void MainWindow::onScanProgress(const QString &phase, int current, int total,
                                const QString &currentPath)
{
	auto *dlg = progressDialog();
	dlg->setProgress(current, total);

	// Friendlier display than a bare `/`-prefixed path:
	//   /Volumes/haa-edit/...     → "Volumes/haa-edit/..."
	//   /Users/Shared/...         → "System Volume/Users/Shared/..."
	//   C:\Avid MediaFiles\MXF\1  → native separators kept
	QString displayPath = currentPath;

#ifdef Q_OS_MAC
	if (displayPath.startsWith('/'))
	{
		if (!displayPath.startsWith("/Volumes/"))
			displayPath = "System Volume" + displayPath;
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
	// One appendPlainText per batch — keeps the console responsive
	// under heavy scanner log volume.
	if (batch.isEmpty())
		return;

	const QString now = QTime::currentTime().toString("HH:mm:ss");
	QString combined;
	combined.reserve(batch.size() * 80);

	for (int i = 0; i < batch.size(); ++i)
	{
		if (i > 0)
			combined += QLatin1Char('\n');
		combined += formatLogLine(now, batch[i].level,
		                          batch[i].module, batch[i].message);
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
		    QString("%1 (%2 files, %3)").arg(p).arg(count).arg(Format::bytes(bytes)));
		item->setData(Qt::UserRole, p);
		m_projectList->addItem(item);
	}

	updateFilterCounts();
	updateStatusBar();
	autoFitColumns();

	setBusy(false);
}

// MARK: - Filter / search slots

void MainWindow::onFilterChanged(int index)
{
	// Order must match the tab order in setupUi. Both ends are indexed.
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
	// Counting via ranges is O(ranges), not O(rows), so its faster. 
	const auto selection = m_tableView->selectionModel()->selection();
	int selectedCount = 0;
	for (const QItemSelectionRange &range : selection)
		selectedCount += range.bottom() - range.top() + 1;
	const bool hasSelection = selectedCount > 0;

	m_btnFileOps->setEnabled(hasSelection);
	m_statusSep1->setVisible(hasSelection);
	m_statusSelected->setVisible(hasSelection);
	m_statusSep2->setVisible(hasSelection);
	m_statusSelSize->setVisible(hasSelection);

	if (!hasSelection)
		return;

	m_statusSelected->setText(QString("%1 selected").arg(selectedCount));

	// Defer the byte-sum walk. For Cmd-A on a big table it's
	// O(N × log N) — one mapToSource per row, each one hitting the
	// proxy's index. Debouncing coalesces rapid selection changes
	// (arrow keys, shift-click ranges) into one tally.
	m_selectionBytesTimer->start();
}

void MainWindow::doUpdateSelectionBytes()
{
	qint64 selBytes = 0;
	const auto selection = m_tableView->selectionModel()->selection();
	for (const QItemSelectionRange &range : selection)
	{
		for (int row = range.top(); row <= range.bottom(); ++row)
		{
			const int srcRow =
			    m_proxy->mapToSource(m_proxy->index(row, 0)).row();
			selBytes += m_model->fileAt(srcRow).sizeBytes;
		}
	}
	m_statusSelSize->setText(
	    QString("%1 selected").arg(Format::bytes(selBytes)));
}

// MARK: - File operation entry points

void MainWindow::onFileOperations()
{
	openManageMedia(+ManageMediaDialog::Operation::Copy);
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

	const auto policies = dlg.conflictPolicies();

	// Move/Delete prune the table on completion. Copy leaves source
	// rows in place.
	m_removeAfterOp = (dlg.operation() != ManageMediaDialog::Operation::Copy);
	m_successfulOpPaths.clear();

	switch (dlg.operation())
	{
	case ManageMediaDialog::Operation::Copy:
		addLog(0, "ops", QString("Copying %1 files to %2").arg(files.size()).arg(dlg.destination()));
		m_pendingOpTitle = tr("Copying files...");
		m_fileOps->executeCopy(files, dlg.destination(),
		                       dlg.preserveStructure(), policies);
		break;

	case ManageMediaDialog::Operation::Move:
		addLog(0, "ops", QString("Moving %1 files to %2").arg(files.size()).arg(dlg.destination()));
		m_pendingOpTitle = tr("Moving files...");
		m_fileOps->executeMove(files, dlg.destination(),
		                       dlg.preserveStructure(), policies);
		break;

	case ManageMediaDialog::Operation::Delete:
		addLog(0, "ops", QString("Deleting %1 files").arg(files.size()));
		m_pendingOpTitle = tr("Deleting files...");
		m_fileOps->executeDelete(files);
		break;
	}
}

// MARK: - MediaMuster Trash dialog

void MainWindow::showMediaMusterTrashDialog(const QString &trashFolderPath,
                                            int fileCount)
{
	QMessageBox msgBox(this);
	msgBox.setIcon(QMessageBox::Information);
	msgBox.setWindowTitle(tr("MediaMuster Trash"));
	msgBox.setText(tr("<b>%1 file%2 moved to the MediaMuster Trash</b>")
	                   .arg(fileCount)
	                   .arg(fileCount == 1 ? "" : "s"));
	msgBox.setInformativeText(
	    tr("Network volumes only support permanent delete, so "
	       "MediaMuster has moved the file%1 to:\n\n%2\n\n"
	       "These files can be restored by moving them back to "
	       "their original location. To permanently delete them, "
	       "click \"Empty Trash\".")
	        .arg(fileCount == 1 ? "" : "s")
	        .arg(trashFolderPath));

	auto *btnOk = msgBox.addButton(QMessageBox::Ok);
	auto *btnOpen = msgBox.addButton(tr("Take Me There"),
	                                 QMessageBox::ActionRole);
	auto *btnEmpty = msgBox.addButton(tr("Empty Trash"),
	                                  QMessageBox::DestructiveRole);
	msgBox.setDefaultButton(btnOk);
	msgBox.exec();

	if (msgBox.clickedButton() == btnOpen)
	{
#ifdef Q_OS_MAC
		QProcess::startDetached("open", {trashFolderPath});
#elif defined(Q_OS_WIN)
		QProcess::startDetached("explorer.exe",
		                        {QDir::toNativeSeparators(trashFolderPath)});
#endif
		return;
	}

	if (msgBox.clickedButton() != btnEmpty)
		return;

	int trashFileCount = 0;
	qint64 trashBytes = 0;
	QDirIterator it(trashFolderPath, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
	{
		it.next();
		trashFileCount++;
		trashBytes += it.fileInfo().size();
	}

	const auto ret = QMessageBox::warning(
	    this, tr("Empty the MediaMuster Trash"),
	    tr("Permanently delete %1 file%2 (%3) from:\n\n%4\n\n"
	       "This cannot be undone.")
	        .arg(trashFileCount)
	        .arg(trashFileCount == 1 ? "" : "s")
	        .arg(Format::bytes(trashBytes), trashFolderPath),
	    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

	if (ret != QMessageBox::Yes)
		return;

	if (QDir(trashFolderPath).removeRecursively())
		addLog(3, "ops",
		       QString("MediaMuster Trash emptied: %1").arg(trashFolderPath));
	else
		addLog(2, "ops",
		       QString("Failed to empty MediaMuster Trash: %1")
		           .arg(trashFolderPath));
}

// MARK: - CSV export

// Worker-thread CSV writer. File scope so the move-captured rows read cleanly.
static bool writeCsvFile(const QString &path, const QVector<MediaFile> &rows)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
		return false;
	QTextStream out(&file);

	out << "Clip Name,Filename,Project,Bin,Kind,Codec,Resolution,FPS,"
	       "Duration,Start TC,Source File,Source Path,Source Container,"
	       "Imported,Size (MB),Volume,Folder,Path,MOB ID,Composition MOB,"
	       "UMID,Type,Date Created\n";

	for (const MediaFile &f : rows)
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
		    << CsvUtil::quoted(f.volumeName) << ','
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
	}
	return out.status() == QTextStream::Ok;
}

void MainWindow::onExportCsv()
{
	const auto sel = selectedFiles();
	bool exportSelected = false;

	if (!sel.isEmpty())
	{
		QMessageBox msgBox(this);
		msgBox.setWindowTitle(tr("Export CSV"));
		msgBox.setText(tr("Export all rows, or only the selected rows?"));
		auto *btnSel = msgBox.addButton(tr("Selected"), QMessageBox::AcceptRole);
		auto *btnAll = msgBox.addButton(tr("All"), QMessageBox::AcceptRole);
		msgBox.addButton(QMessageBox::Cancel);
		msgBox.setDefaultButton(btnSel);
		msgBox.exec();

		auto *clicked = msgBox.clickedButton();
		if (clicked == btnSel)
			exportSelected = true;
		else if (clicked != btnAll)
			return;
	}

	const QString path = QFileDialog::getSaveFileName(
	    this, "Export CSV", QDir::homePath() + "/mediamuster_export.csv",
	    "CSV Files (*.csv)");
	if (path.isEmpty())
		return;

	// Snapshot on the main thread — Qt models are thread-affine.
	// Also freezes the export against later filter/sort changes.
	QVector<MediaFile> rows;
	if (exportSelected)
	{
		rows = sel;
	}
	else
	{
		rows.reserve(m_proxy->rowCount());
		for (int row = 0; row < m_proxy->rowCount(); ++row)
		{
			const auto si = m_proxy->mapToSource(m_proxy->index(row, 0));
			rows.append(m_model->fileAt(si.row()));
		}
	}

	const int count = rows.size();
	const QString label = exportSelected ? "selected records" : "records";
	addLog(0, "export",
	       QString("Exporting %1 %2 to %3").arg(count).arg(label).arg(path));
	m_btnExport->setEnabled(false);

	// Dispatch the write to a worker so big exports don't freeze the UI.
	auto *watcher = new QFutureWatcher<bool>(this);
	connect(watcher, &QFutureWatcher<bool>::finished, this,
	        [this, watcher, path, count, label]()
	        {
		        const bool ok = watcher->result();
		        watcher->deleteLater();
		        m_btnExport->setEnabled(true);
		        if (ok)
		        {
			        addLog(3, "export",
			               QString("Exported %1 %2 to %3")
			                   .arg(count)
			                   .arg(label)
			                   .arg(path));
		        }
		        else
		        {
			        addLog(2, "export",
			               QString("Failed to write %1").arg(path));
		        }
	        });

	watcher->setFuture(QtConcurrent::run(
	    [path, rows = std::move(rows)]()
	    { return writeCsvFile(path, rows); }));
}

// MARK: - Project summary

void MainWindow::onProjectSummary()
{
	if (m_allFiles.isEmpty())
	{
		QMessageBox::information(this, tr("Project Summary"),
		                         tr("Hold your horses! Scan some media first to see the project summary."));
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
		if (!s.volumes.contains(f.volumeName))
			s.volumes.append(f.volumeName);
	}
	auto *dlg = new QDialog(this);
	dlg->setWindowTitle(tr("Project Summary"));
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
		sm->setItem(r, 4, new QStandardItem(Format::bytes(s.totalBytes)));
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

// MARK: - Reveal in Finder

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

	// Open the parent enclosing folder, if the file itself can't be revealed. 
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
	// Fast path: `open -R <path>`. Works for most local paths.
	if (QProcess::startDetached("open", {"-R", path}))
		return;

	// Fallback: AppleScript reveal — more resilient on network
	// mounts where `open -R` loses the file.
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

	addLog(1, "reveal", "open(1) and osascript both failed; opening parent");
	openParent();

#elif defined(Q_OS_WIN)
	// Three-step reveal on Windows:
	//   1. Shell API (SHParseDisplayName + SHOpenFolderAndSelectItems).
	//   2. `explorer.exe /select,"<path>"` fallback.
	//   3. Open parent folder unhighlighted.
	//
	// Explorer needs `/select,"<path>"` with the comma — use
	// setNativeArguments so Qt's quoting doesn't glue the args.
	const QString nativePath = QDir::toNativeSeparators(path);

	// `GetFullPathNameW` handles relative and long paths in one
	// call. 4096 wchars is well above `MAX_PATH`.
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

	// CoInitializeEx defensively — Qt may have already initialised
	// COM with a different model (RPC_E_CHANGED_MODE). Skip the
	// matching CoUninitialize in that case.
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

	// Last resort: just open the parent enclosing folder.
	addLog(1, "reveal",
	       "Shell API + explorer.exe /select both failed; opening parent");
	openParent();
#endif
}

// MARK: - Select relatives

void MainWindow::onSelectRelatives()
{
	// Selects every visible row sharing the composition MOB —
	// V01 + A01 + A02 of one master clip. Hidden rows stay hidden.
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
			// Span all columns so the row highlights end-to-end —
			// selecting just column 0 leaves a narrow strip.
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
		       "No relatives found (the relatives may be filtered out)");
		return;
	}

	// Replace the existing selection with the relatives set and
	// scroll so the first match is in view — saves the editor
	// hunting for what just got selected.
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
	       QString("Selected %1 relative%2 (composition MOB: %3...)")
	           .arg(matched)
	           .arg(matched == 1 ? "" : "s")
	           .arg(compMob.right(16)));
}

void MainWindow::onTableDoubleClicked(const QModelIndex &)
{
	onRevealInFinder();
}

// MARK: - Context menu

void MainWindow::showTableContextMenu(const QPoint &pos)
{
	QMenu menu(this);

	const QModelIndex index = m_tableView->indexAt(pos);
	if (index.isValid())
	{
		const QString cellText = index.data(Qt::DisplayRole).toString();
		QString menuLabel = cellText;
		if (menuLabel.length() > 25)
			menuLabel = menuLabel.left(22) + "...";
		menu.addAction(tr("Copy \"%1\"").arg(menuLabel),
		               [cellText]()
		               { QApplication::clipboard()->setText(cellText); });
		menu.addSeparator();
	}

	menu.addAction(tr("Reveal in Finder"), this, &MainWindow::onRevealInFinder);
	menu.addAction(tr("Copy Path"), [this]()
	               {
		const auto sel = selectedFiles();
		if (sel.isEmpty())
			return;
		QStringList paths;
		for (const auto &f : sel)
			paths << f.filePath;
		QApplication::clipboard()->setText(paths.join("\n")); });

	const auto sel = selectedFiles();
	const bool hasComp =
	    !sel.isEmpty() && !sel.first().compositionMobId.isEmpty();
	QAction *relAct = menu.addAction(tr("Select Relatives"), this,
	                                 &MainWindow::onSelectRelatives);
	relAct->setEnabled(hasComp);
	if (!hasComp)
		relAct->setToolTip(tr("No composition MOB — this file isn't related "
		                      "to a master clip we can follow"));

	menu.addSeparator();
	menu.addAction(tr("Copy To..."), this, [this]()
	               { openManageMedia(+ManageMediaDialog::Operation::Copy); });
	menu.addAction(tr("Move To..."), this, [this]()
	               { openManageMedia(+ManageMediaDialog::Operation::Move); });
	menu.addAction(tr("Delete..."), this, [this]()
	               { openManageMedia(+ManageMediaDialog::Operation::Delete); });
	menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

// MARK: - Selection helper

QVector<MediaFile> MainWindow::selectedFiles() const
{
	const auto rows = m_tableView->selectionModel()->selectedRows();
	QVector<MediaFile> result;
	result.reserve(rows.size());
	for (const auto &pi : rows)
		result.append(m_model->fileAt(m_proxy->mapToSource(pi).row()));
	return result;
}

// MARK: - Console logging

void MainWindow::addLog(int level, const QString &module,
                        const QString &message)
{
	m_console->appendPlainText(formatLogLine(
	    QTime::currentTime().toString("HH:mm:ss"), level, module, message));
}

// MARK: - Column auto-fit

void MainWindow::autoFitColumns()
{
	m_tableView->resizeColumnsToContents();
	// Clamp at 300px so long filenames can't push the table out.
	for (int i = 0; i < +MediaTableModel::Column::Count_; ++i)
	{
		if (m_tableView->columnWidth(i) > 300)
			m_tableView->setColumnWidth(i, 300);
	}
}

// MARK: - Busy state

void MainWindow::setBusy(bool busy)
{
	m_scanButton->setEnabled(!busy);
	m_scanAllButton->setEnabled(!busy);

	// Manage Media: always disabled when busy; otherwise follows selection.
	const bool hasSel = m_tableView->selectionModel() && !m_tableView->selectionModel()->selectedRows().isEmpty();
	m_btnFileOps->setEnabled(!busy && hasSel);
	m_btnRebalance->setEnabled(!busy && !m_allFiles.isEmpty());

	if (!busy && m_progressDialog)
		m_progressDialog->finish();

	// Skip the 5 second polling tick when a scan or op is running. 
	m_volumeManager->setBusy(busy);
}

// MARK: - Progress dialog

ProgressDialog *MainWindow::progressDialog()
{
	// Cancel fires on both workers — idle worker's cancel() is a no-op.
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

// MARK: - Status bar

void MainWindow::updateStatusBar()
{
	// Coalesces bursts: doUpdateStatusBar fires 100 ms after the
	// last call.
	m_statusBarUpdateTimer->start();
}

void MainWindow::doUpdateStatusBar()
{
	const int total = m_proxy->rowCount();
	const int grandTotal = m_allFiles.size();

	qint64 totalBytes = 0;
	for (int i = 0; i < total; ++i)
		totalBytes += m_model->fileAt(
		                         m_proxy->mapToSource(m_proxy->index(i, 0)).row())
		                  .sizeBytes;

	m_statusFiles->setText(total < grandTotal
	                           ? QString("%1 files (filtered from %2)").arg(total).arg(grandTotal)
	                           : QString("%1 files").arg(total));
	m_statusSize->setText(Format::bytes(totalBytes));
}

// MARK: - Filter tab counts

void MainWindow::updateFilterCounts()
{
	struct TabStat
	{
		const char *label;
		int count = 0;
	};
	TabStat stats[] = {
	    {"All", int(m_allFiles.size())},
	    {"Video"},
	    {"Audio"},
	    {"Orphaned"},
	    {"Bad UMID"},
	    {"Unreferenced"},
	    {"Non-Portable"},
	    {"Quarantined"},
	};

	auto isQuarantined = [](const MediaFile &f)
	{
		return f.mxfFolder.contains("Quarantined", Qt::CaseInsensitive) ||
		       f.filePath.contains("Quarantined", Qt::CaseInsensitive);
	};

	for (const MediaFile &f : m_allFiles)
	{
		if (f.mediaType == MediaFile::Type::Video)
			++stats[1].count;
		if (f.mediaType == MediaFile::Type::Audio)
			++stats[2].count;
		if (f.isUnmanaged)
			++stats[3].count;
		if (f.isBadUmid)
			++stats[4].count;
		if (f.isUnreferenced)
			++stats[5].count;
		if (f.isNonPortable)
			++stats[6].count;
		if (isQuarantined(f))
			++stats[7].count;
	}

	for (int i = 0; i < 8; ++i)
	{
		m_filterTabs->setTabText(
		    i, QString("%1 (%2)").arg(stats[i].label).arg(stats[i].count));

		// "All" stays visible no matter what; the others auto-hide
		// when empty,  unless the editor choses 'Show All Filter Tabs'.
		const bool visible =
		    (i == 0) || m_showAllFilterTabs || stats[i].count > 0;
		m_filterTabs->setTabVisible(i, visible);
	}
}

// MARK: - Icon helpers

QIcon MainWindow::iconForVolumeType(const QString &volumeType, const QString &path)
{
	auto *style = QApplication::style();
	const QString t = volumeType.toLower();

	if (t.contains("network") || t.contains("smb") || t.contains("nfs") ||
	    t.contains("afp") || t.contains("cifs") || t.contains("nexis"))
		return style->standardIcon(QStyle::SP_DriveNetIcon);
	if (t.contains("cd") || t.contains("dvd") || t.contains("optical"))
		return style->standardIcon(QStyle::SP_DriveCDIcon);
	if (t.contains("removable") || t.contains("usb") || t.contains("flash") ||
	    t.contains("floppy"))
		return style->standardIcon(QStyle::SP_DriveFDIcon);

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

	// UNMANAGED_FILES gets a warning glyph so it stands out.
	if (projectName.compare(QLatin1String("UNMANAGED_FILES"),
	                        Qt::CaseInsensitive) == 0)
		return style->standardIcon(QStyle::SP_MessageBoxWarning);

	return style->standardIcon(QStyle::SP_DirIcon);
}

// MARK: - Filter chips

void MainWindow::rebuildFilterChips()
{
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

	auto addChip = [this, layout](const QString &text,
	                              std::function<void()> onClose)
	{
		auto *chip = new QPushButton(text + "  ✕");
		chip->setCursor(Qt::PointingHandCursor);
		chip->setStyleSheet(kChipStyle);
		chip->setToolTip(tr("Click to remove this filter"));
		QObject::connect(chip, &QPushButton::clicked, this,
		                 [cb = std::move(onClose)]()
		                 { cb(); });
		layout->addWidget(chip);
	};

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

	const QString searchText = m_searchField->text();
	if (!searchText.isEmpty())
	{
		QString shown = searchText;
		if (shown.length() > 24)
			shown = shown.left(22) + QStringLiteral("...");
		addChip(QString("Search: \"%1\"").arg(shown),
		        [this]()
		        { m_searchField->clear(); });
	}

	for (auto *item : m_projectList->selectedItems())
	{
		const QString proj = item->data(Qt::UserRole).toString();
		addChip(QString("Project: %1").arg(proj),
		        [item]()
		        { item->setSelected(false); });
	}

	// Chip pauses the proxy filter; the chain persists in dialog.
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