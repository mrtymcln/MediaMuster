#include "mainwindow.h"

#include "aboutdialog.h"
#include "diagnostics.h"
#include "conventions.h"
#include "binfilterdialog.h"
#include "enumutil.h"
#include "effectfilterdialog.h"
#include "featureflags.h"
#include "formatutil.h"
#include "layoututil.h"
#include "managemediadialog.h"
#include "mediacsv.h"
#include "progressdialog.h"
#include "rebalancedialog.h"
#include "rebalanceplanner.h"
#include "revealinfinder.h"
#include "version.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QOperatingSystemVersion>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStorageInfo>
#include <QStyle>
#include <QStyleFactory>
#include <QSysInfo>
#include <QTabBar>
#include <QTableView>
#include <QtConcurrent>
#include <QTime>
#include <QKeyEvent>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

// MARK: - Console Log prefixes

namespace
{
	/// Shared fixed-pitch font for the table and console.
	QFont monoFont()
	{
#ifdef Q_OS_MAC
		return QFont(QStringLiteral("Menlo"), 12);
#else // Q_OS_WIN
		return QFont(QStringLiteral("Consolas"), 12);
#endif
	}

	// Fixed-width tags so the console's [ ] column stays aligned. QtFatalMsg
	// is never emitted by us; it folds to the error tag defensively.
	const char *consoleLevelLabel(QtMsgType level)
	{
		switch (level)
		{
		case QtInfoMsg:
			return "INFO";
		case QtWarningMsg:
			return "WARN";
		case QtCriticalMsg:
			return "ERR ";
		case QtDebugMsg:
			return "DBG ";
		case QtFatalMsg:
			return "ERR ";
		}
		return "INFO";
	}

	QString formatLogLine(const QString &time, QtMsgType level, const QString &module,
						  const QString &message)
	{
		return QStringLiteral("%1 [%2] [%3] %4")
			.arg(time, QLatin1String(consoleLevelLabel(level)), module, message);
	}

	QString mediaTreeForFolder(const QString &parent)
	{
		QString folder = parent;
		QString mxfRoot;
		for (;;)
		{
			const QFileInfo info(folder);
			const auto name = info.fileName();
			if (name.compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0 ||
				Conventions::isOmfRootName(name))
				return folder;
			if (Conventions::isMxfRootName(name))
				mxfRoot = folder;
			const auto ancestor = info.absolutePath();
			if (ancestor == folder)
				break;
			folder = ancestor;
		}
		return mxfRoot.isEmpty() ? parent : mxfRoot;
	}

	bool isStandardMediaTree(const QString &path)
	{
		const auto name = QFileInfo(path).fileName();
		return name.compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0 ||
			   Conventions::isOmfRootName(name);
	}

	QStringList restorationScanPaths(const QVector<MediaFile> &files, const QSet<QString> &restored)
	{
		// One lexical calculation per media folder, with no directory probes
		// on the UI thread. Keep the scanner's original volume root where it
		// is available so a drive is not relabelled "Avid MediaFiles".
		QHash<QString, QString> originsByFolder;
		for (const auto &file : files)
			originsByFolder.insert(QFileInfo(file.filePath).absolutePath(), file.volumePath);
		QSet<QString> trees;
		QSet<QString> roots;
		for (auto it = originsByFolder.cbegin(); it != originsByFolder.cend(); ++it)
		{
			const auto tree = mediaTreeForFolder(it.key());
			trees.insert(tree);
			roots.insert(QDir::cleanPath(isStandardMediaTree(tree) && !it.value().isEmpty() ? it.value() : tree));
		}
		for (const auto &path : restored)
		{
			const auto tree = mediaTreeForFolder(QFileInfo(path).absolutePath());
			if (trees.contains(tree))
				continue;
			roots.insert(isStandardMediaTree(tree) ? QFileInfo(tree).absolutePath() : tree);
			trees.insert(tree);
		}
		// Scanning the containing volume already includes its MXF/OMF trees.
		const auto candidates = roots.values();
		for (const auto &root : candidates)
			if (isStandardMediaTree(root) && roots.contains(QFileInfo(root).absolutePath()))
				roots.remove(root);
		return roots.values();
	}

	// MARK: - Filter tab definitions

	struct FilterDef
	{
		MediaFilterProxy::FilterMode mode;
		const char *label;
		const char *tooltip = nullptr; ///< Optional hover explanation for loaded terms.
	};

	constexpr std::array<FilterDef, 7> kFilterDefs{{
		// Technical Avid/domain vocabulary — invariant, never translated.
		{MediaFilterProxy::FilterMode::All, "All"},
		{MediaFilterProxy::FilterMode::Video, "Video"},
		{MediaFilterProxy::FilterMode::Audio, "Audio"},
		{MediaFilterProxy::FilterMode::Precompute, "Precomputes",
		 "Media whose metadata identifies a precompute master: rendered effects,\n"
		 "titles and matte keys, or a precompute with an unknown category."},
		// MainWindow supplies tab text; MediaFile supplies row-status explanations.
		{MediaFilterProxy::FilterMode::NoDatabase, "No Database"},
		{MediaFilterProxy::FilterMode::NonPortable, "Non-Portable"},
		{MediaFilterProxy::FilterMode::Quarantined, "Quarantined"},
	}};

} // namespace

// MARK: - MainWindow construction

MainWindow::MainWindow(QWidget *parent, StartupMode startup)
	: QMainWindow(parent),
	  // Match declaration order; QObject parenting binds these services to the window.
	  m_volumeManager(new VolumeManager(this)),
	  m_scanner(new MediaScanner(this)),
	  m_operations(new FileOperationController(this)),
	  m_model(new MediaTableModel(this)),
	  m_proxy(new MediaFilterProxy(this))
{
	m_proxy->setSourceModel(m_model);
	m_proxy->setSortRole(Qt::UserRole);

	setupUi();
	setupMenus();
	setupConnections();
	updateFilterCounts();
	updateActivityUi();

	setWindowTitle("MediaMuster");
	resize(1200, 750);
	if (startup == StartupMode::UiOnly)
		return;

	QString platform;
#ifdef Q_OS_MAC
	const auto v = QOperatingSystemVersion::current();
	platform = QStringLiteral("%1 %2.%3.%4")
				   .arg(v.name())
				   .arg(v.majorVersion())
				   .arg(v.minorVersion())
				   .arg(v.microVersion());
#else
	platform = QSysInfo::prettyProductName();
#endif
	addLog(QtInfoMsg, QStringLiteral("app"), QStringLiteral("%1 %2 initialised on %3").arg(APP_NAME, APP_VERSION, platform));

	refreshVolumes();
	m_volumeManager->startMonitoring();

#ifdef Q_OS_MAC
	if (!VolumeManager::hasFullDiskAccess())
	{
		addLog(QtWarningMsg, QStringLiteral("app"),
			   "Full Disk Access not granted. Go to System Preferences > Privacy & Security.");
	}
#endif // Q_OS_MAC

	// Show the window before recovery. Finish any crash-report notice first
	// so it cannot overlap the recovery prompts.
	QTimer::singleShot(0, this,
					   [this]
					   {
						   collectCrashReports();
						   m_operations->runStartupRecovery();
					   });
}

MainWindow::~MainWindow()
{
	// Model/selection resets during child destruction must not call back into
	// a MainWindow whose derived destructor has already finished.
	for (auto *child : findChildren<QObject *>())
		disconnect(child, nullptr, this, nullptr);
}

// MARK: - Crash recovery

void MainWindow::collectCrashReports()
{
	const QString logsDir = QFileInfo(Diagnostics::logPath()).absolutePath();
	const QStringList collected =
		Diagnostics::collectCrashReports(Diagnostics::systemCrashReportsDir(), logsDir);
	if (collected.isEmpty())
		return;

	addLog(QtWarningMsg, QStringLiteral("app"),
		   QStringLiteral("I quit unexpectedly. Go to Help > Reveal Logs and send them to developer."));

	QMessageBox::information(
		this, QString(),
		tr("I quit unexpectedly. A crash report has been saved "
		   "with your logs.\n\nGo to Help > Reveal Logs to send them to the developer."));
}

// MARK: - UI layout

void MainWindow::setupUi()
{
	buildSidePanel();
	QWidget *toolbarWidget = buildToolbar();
	buildTable();
	buildConsole();

	// Table + console stacked vertically.
	m_contentSplitter = new QSplitter(Qt::Vertical);
	m_contentSplitter->addWidget(m_tableView);
	m_contentSplitter->addWidget(m_console);
	m_contentSplitter->setStretchFactor(0, 1);
	m_contentSplitter->setStretchFactor(1, 0);
	// Table dominates the window; console sits at its minimum size.
	m_contentSplitter->setSizes({100000, 80});

	// Toolbar above the table + console.
	auto *contentWidget = new QWidget;
	auto *contentLayout = new QVBoxLayout(contentWidget);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);
	contentLayout->addWidget(toolbarWidget);
	contentLayout->addWidget(m_contentSplitter);

	// Side panel on the left.
	m_mainSplitter = new QSplitter(Qt::Horizontal);
	m_mainSplitter->addWidget(m_sidePanel);
	m_mainSplitter->addWidget(contentWidget);
	m_mainSplitter->setStretchFactor(0, 0);
	m_mainSplitter->setStretchFactor(1, 1);
	setCentralWidget(m_mainSplitter);

	buildStatusBar();
}

// MARK: - Side panel

void MainWindow::buildSidePanel()
{
	m_sidePanel = new QWidget;
	m_sidePanel->setFixedWidth(260);
	auto *sideLayout = new QVBoxLayout(m_sidePanel);
	sideLayout->setContentsMargins(0, 0, 0, 0);
	sideLayout->setSpacing(12);

	// Volumes group.
	auto *volGroup = new QGroupBox(tr("Volumes"));
	auto *volLayout = new QVBoxLayout(volGroup);
	volLayout->setContentsMargins(6, 6, 6, 6);
	volLayout->setSpacing(4);

	m_volumeList = new VolumeListWidget;
	m_volumeList->setSelectionMode(QAbstractItemView::MultiSelection);
	m_volumeList->setMinimumHeight(60);
	m_volumeList->setMaximumHeight(140);
	m_volumeList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	volLayout->addWidget(m_volumeList);

	m_scanButton = new QPushButton(tr("Scan Selected"));
	m_scanButton->setDefault(true);
	volLayout->addWidget(m_scanButton);

	m_scanAllButton = new QPushButton(tr("Scan All"));
	volLayout->addWidget(m_scanAllButton);

	sideLayout->addWidget(volGroup);

	// Projects group.
	auto *projGroup = new QGroupBox(tr("Projects"));
	auto *projLayout = new QVBoxLayout(projGroup);
	projLayout->setContentsMargins(6, 6, 6, 6);
	m_projectList = new QListWidget;
	m_projectList->setSelectionMode(QAbstractItemView::MultiSelection);
	// Prevent long project names from blowing out the splitter.
	m_projectList->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	projLayout->addWidget(m_projectList);
	sideLayout->addWidget(projGroup);
}

// MARK: - Toolbar

QWidget *MainWindow::buildToolbar()
{
	m_filterTabs = new QTabBar;
	for (const auto &fd : kFilterDefs)
	{
		const int idx = m_filterTabs->addTab(QString::fromLatin1(fd.label));
		if (fd.mode == MediaFilterProxy::FilterMode::NoDatabase)
		{
			// Share database explanations with the table tooltip and CSV.
			using DbStatus = MediaFile::DbStatus;
			m_filterTabs->setTabToolTip(idx, MediaFile::dbStatusText(DbStatus::NoDatabase).why +
												 QStringLiteral("\n\n") + MediaFile::dbStatusText(DbStatus::DbUnreadable).why);
		}
		else if (fd.tooltip)
			m_filterTabs->setTabToolTip(idx, QString::fromLatin1(fd.tooltip));
	}
	m_filterTabs->setExpanding(false);
	m_filterTabs->setDocumentMode(true);

	m_searchField = new QLineEdit;
	m_searchField->setPlaceholderText(tr("Search"));
	m_searchField->setClearButtonEnabled(true);
	m_searchField->setMinimumWidth(200);
	m_searchField->setMaximumWidth(320);

	m_btnFileOps = new QPushButton(tr("Manage Media…"));
	m_btnBinFilter = new QPushButton(tr("Filter by Bin…"));
	m_btnEffectFilter = new QPushButton(tr("Filter Precomputes…"));
	m_btnEffectFilter->setObjectName(QStringLiteral("filterByEffectButton"));
	m_btnEffectFilter->setVisible(false);
	m_btnEffectFilter->setEnabled(false);
	m_btnExport = new QPushButton(tr("Export CSV…"));
	m_btnRebalance = new QPushButton(tr("Rebalance…"));
	m_btnFileOps->setEnabled(false);
	m_btnRebalance->setEnabled(false);

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
	actionsRow->addWidget(m_btnEffectFilter);
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

	return toolbarWidget;
}

// MARK: - Table

namespace
{
	// The default behaviour of QTableView is such that it scrolls to always
	// keep the 'current cell' visible. autoScroll is off in buildTable; but
	// that also disables the scroll on arrow keys, so we restore it below.
	class MediaTableView : public QTableView
	{
	public:
		using QTableView::QTableView;

	protected:
		void keyPressEvent(QKeyEvent *event) override
		{
			const QPersistentModelIndex before = currentIndex();
			const int horizontal = horizontalScrollBar()->value();

			QTableView::keyPressEvent(event);

			const QModelIndex now = currentIndex();
			if (now.isValid() && now != before)
				scrollTo(now);							 // vertical for arrows/pg up/pg down
			horizontalScrollBar()->setValue(horizontal); // never sideways
		}
	};
} // namespace

void MainWindow::buildTable()
{
	m_tableView = new MediaTableView;
	m_tableView->setModel(m_proxy);
	m_tableView->setSortingEnabled(true);
	m_tableView->setAutoScroll(false);
	m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tableView->setAlternatingRowColors(true);
	m_tableView->setShowGrid(false);
	// Fill the whole column width and hard-clip at the edge rather than the
	// default ElideRight, which drops extra characters to make room for an
	// "…". No ellipsis; a narrow column shows as many characters as fit.
	m_tableView->setTextElideMode(Qt::ElideNone);
	m_tableView->verticalHeader()->setVisible(false);
	m_tableView->verticalHeader()->setDefaultSectionSize(24);
	m_tableView->horizontalHeader()->setStretchLastSection(false);
	m_tableView->horizontalHeader()->setSectionsMovable(true);
	m_tableView->horizontalHeader()->setHighlightSections(false);
	m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_tableView->setFont(monoFont());

	// Every session starts here. Scans leave widths alone; the View menu
	// provides Qt's native content fitting when requested.
	using Col = MediaTableModel::Column;
	auto setW = [this](Col c, int w)
	{ m_tableView->setColumnWidth(Enum::to_underlying(c), w); };
	setW(Col::ClipName, 240);
	setW(Col::Project, 150);
	setW(Col::OriginalBin, 150);
	setW(Col::Kind, 75);
	setW(Col::Duration, 110);
	setW(Col::SizeMB, 110);
	setW(Col::Codec, 160);
	setW(Col::Resolution, 110);
	setW(Col::Fps, 75);
	setW(Col::SampleRate, 110);
	setW(Col::BitDepth, 100);
	setW(Col::Type, 110);
	setW(Col::Created, 165);
	setW(Col::FileName, 260);
	setW(Col::SourceFile, 240);
	setW(Col::Location, 400);
}

// MARK: - Console

void MainWindow::buildConsole()
{
	m_console = new QPlainTextEdit;
	m_console->setReadOnly(true);
	m_console->setMaximumBlockCount(2000);
	m_console->setMinimumHeight(80);
	m_console->setFont(monoFont());
}

// MARK: - Status bar

void MainWindow::buildStatusBar()
{
	m_statusFiles = new QLabel(tr("0 files"));
	m_statusSize = new QLabel(tr("0 GB"));
	m_statusSelected = new QLabel;
	m_statusSelSize = new QLabel;
	m_statusScanTime = new QLabel;

	auto *primarySep = new QLabel(" | ");
	m_statusSep1 = new QLabel(" | ");
	m_statusSep2 = new QLabel(" | ");

	// Hidden until rows are selected.
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
	buildFileMenu();
	buildEditMenu();
	buildViewMenu();
	buildSpecialMenu();
	buildDebugMenu();
	buildHelpMenu();
}

// MARK: File menu

void MainWindow::buildFileMenu()
{
	auto *fileMenu = menuBar()->addMenu(tr("&File"));

	m_addFolderAct = fileMenu->addAction(tr("Add &Folder or Volume…"));
	m_addFolderAct->setShortcut(QKeySequence("Ctrl+O"));
	connect(m_addFolderAct, &QAction::triggered, this,
			[this]()
			{
				QString dir = QFileDialog::getExistingDirectory(this, tr("Add Volume or Folder"));
				if (!dir.isEmpty())
					addVolumePath(dir);
			});

	m_refreshVolumesAct = fileMenu->addAction(tr("Refresh &Volumes"));
	m_refreshVolumesAct->setShortcut(QKeySequence("Ctrl+R"));
	connect(m_refreshVolumesAct, &QAction::triggered, this, &MainWindow::refreshVolumes);

	fileMenu->addSeparator();

	m_scanSelectedAct = fileMenu->addAction(tr("Scan &Selected"));
	m_scanSelectedAct->setObjectName(QStringLiteral("scanSelectedAction"));
	connect(m_scanSelectedAct, &QAction::triggered, this, &MainWindow::scanSelected);

	m_scanAllAct = fileMenu->addAction(tr("Scan &All"));
	m_scanAllAct->setObjectName(QStringLiteral("scanAllAction"));
	m_scanAllAct->setShortcut(QKeySequence("Ctrl+Shift+A"));
	connect(m_scanAllAct, &QAction::triggered, this, &MainWindow::scanEverything);

	fileMenu->addSeparator();

	// One place for unfinished jobs and originals awaiting restoration.
	fileMenu->addAction(m_operations->recoveryAction());

	fileMenu->addSeparator();

	m_revealAct = fileMenu->addAction(tr("Reveal in Finder"));
	m_revealAct->setObjectName(QStringLiteral("revealInFinderAction"));
	m_revealAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
	connect(m_revealAct, &QAction::triggered, this, &MainWindow::onRevealInFinder);

	m_exportAct = fileMenu->addAction(tr("&Export CSV…"));
	m_exportAct->setObjectName(QStringLiteral("exportCsvAction"));
	m_exportAct->setShortcut(QKeySequence("Ctrl+E"));
	connect(m_exportAct, &QAction::triggered, this, &MainWindow::onExportCsv);

#ifndef Q_OS_MAC
	fileMenu->addSeparator();
	auto *quitAct = fileMenu->addAction(tr("&Quit"));
	quitAct->setShortcut(QKeySequence("Ctrl+Q"));
	connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);
#endif
}

// MARK: Edit menu

void MainWindow::buildEditMenu()
{
	auto *editMenu = menuBar()->addMenu(tr("&Edit"));

	// Hidden and without a shortcut until Debug enables it, so ordinary
	// text-field Undo keeps working throughout the default beta workflow.
	editMenu->addAction(m_operations->undoAction());

	auto *findAct = editMenu->addAction(tr("&Find"));
	findAct->setShortcut(QKeySequence::Find);
	connect(findAct, &QAction::triggered, m_searchField, qOverload<>(&QWidget::setFocus));

	editMenu->addSeparator();
	m_selectRelativesAct = editMenu->addAction(tr("Select &Relatives"));
	m_selectRelativesAct->setObjectName(QStringLiteral("selectRelativesAction"));
	m_selectRelativesAct->setShortcut(QKeySequence("Ctrl+Shift+L"));
	connect(m_selectRelativesAct, &QAction::triggered, this, &MainWindow::onSelectRelatives);

	m_selectInverseAct = editMenu->addAction(tr("Select &Inverse"));
	m_selectInverseAct->setObjectName(QStringLiteral("selectInverseAction"));
	m_selectInverseAct->setShortcut(QKeySequence("Ctrl+Shift+I"));
	connect(m_selectInverseAct, &QAction::triggered, this, &MainWindow::onInvertSelection);
}

// MARK: View menu

void MainWindow::buildViewMenu()
{
	auto *viewMenu = menuBar()->addMenu(tr("&View"));

	auto *consoleAct = viewMenu->addAction(tr("Show &Console"));
	consoleAct->setCheckable(true);
	consoleAct->setChecked(true);
	connect(consoleAct, &QAction::triggered, this, [this](bool c)
			{ m_console->setVisible(c); });

	// Off by default; empty tabs hide themselves so the bar isn't
	// cluttered with (0) placeholders.
	auto *showAllTabsAct = viewMenu->addAction(tr("Show &All Filter Tabs"));
	showAllTabsAct->setCheckable(true);
	showAllTabsAct->setChecked(false);
	connect(showAllTabsAct, &QAction::triggered, this,
			[this](bool on)
			{
				m_showAllFilterTabs = on;
				updateFilterCounts();
			});

	viewMenu->addSeparator();

	auto *fitAct = viewMenu->addAction(tr("&Resize Columns to Fit"));
	fitAct->setShortcut(QKeySequence("Ctrl+T"));
	connect(fitAct, &QAction::triggered, this, &MainWindow::autoFitColumns);
}

// MARK: Special menu

void MainWindow::buildSpecialMenu()
{
	auto *specialMenu = menuBar()->addMenu(tr("&Special"));

	m_manageMediaAct = specialMenu->addAction(tr("Manage &Media…"));
	m_manageMediaAct->setObjectName(QStringLiteral("manageMediaAction"));
	connect(m_manageMediaAct, &QAction::triggered, this, &MainWindow::onFileOperations);

	m_binFilterAct = specialMenu->addAction(tr("Filter by &Bin…"));
	m_binFilterAct->setObjectName(QStringLiteral("filterByBinAction"));
	m_binFilterAct->setShortcut(QKeySequence("Ctrl+Shift+B"));
	connect(m_binFilterAct, &QAction::triggered, this, &MainWindow::onFilterByBins);
	m_effectFilterAct = specialMenu->addAction(tr("Filter &Precomputes…"));
	m_effectFilterAct->setObjectName(QStringLiteral("filterByEffectAction"));
	m_effectFilterAct->setVisible(false);
	m_effectFilterAct->setEnabled(false);
	connect(m_effectFilterAct, &QAction::triggered, this, &MainWindow::onFilterByEffects);

	specialMenu->addSeparator();
	m_rebalanceAct = specialMenu->addAction(tr("&Rebalance…"));
	m_rebalanceAct->setObjectName(QStringLiteral("rebalanceAction"));
	connect(m_rebalanceAct, &QAction::triggered, this, &MainWindow::onRebalance);
}

// MARK: Debug menu

void MainWindow::buildDebugMenu()
{
	if constexpr (!FeatureFlags::kDebugMenuEnabled)
		return;

	auto *debugMenu = menuBar()->addMenu(tr("&Debug"));
	debugMenu->setObjectName(QStringLiteral("debugMenu"));
	m_enableOmfAct = debugMenu->addAction(tr("Enable OMF"));
	m_enableOmfAct->setObjectName(QStringLiteral("enableOmfDebugAction"));
	m_enableOmfAct->setCheckable(true);
	connect(m_enableOmfAct, &QAction::toggled, this, &MainWindow::setOmfEnabled);

	// One gate covers classification, details, filtering and CSV fields.
	m_enablePrecomputesAct = debugMenu->addAction(tr("Enable Precomputes"));
	m_enablePrecomputesAct->setObjectName(QStringLiteral("enablePrecomputesDebugAction"));
	m_enablePrecomputesAct->setCheckable(true);
	m_enablePrecomputesAct->setChecked(false);
	connect(m_enablePrecomputesAct, &QAction::toggled, this, &MainWindow::setPrecomputesEnabled);

	debugMenu->addAction(m_operations->enableUndoAction());
	debugMenu->addSeparator();

	// Whatever style main.cpp installed at startup is the one to restore.
	// Read it here, before the toggle below can change it — main.cpp stays
	// the single authority on the platform's native style.
	const QString nativeStyleName = QApplication::style()->name();
	auto *fusionStyleAct = debugMenu->addAction(tr("Fusion style"));
	fusionStyleAct->setObjectName(QStringLiteral("fusionStyleDebugAction"));
	fusionStyleAct->setCheckable(true);
	fusionStyleAct->setChecked(false);
	connect(fusionStyleAct, &QAction::triggered, this,
			[this, nativeStyleName](bool on)
			{
				const QString target = on ? QStringLiteral("fusion") : nativeStyleName;
				QApplication::setStyle(QStyleFactory::create(target));
				addLog(QtInfoMsg, QStringLiteral("app"), QStringLiteral("Using %1 style for appearance.").arg(target));
			});

	debugMenu->addSeparator();

	// Rebalance demos: synthetic plans for visual QA. Each opens
	// the dialog in demo mode against a fabricated RebalancePlan;
	// clicking Rebalance runs a simulated progress sweep, not real
	// disk moves.
	auto *rebalanceDemosMenu = debugMenu->addMenu(tr("Rebalance demos"));
	rebalanceDemosMenu->setObjectName(QStringLiteral("rebalanceDemosMenu"));
	const auto addDemo = [this, rebalanceDemosMenu](const QString &label,
													RebalanceDialog::DemoScenario scenario)
	{
		auto *act = rebalanceDemosMenu->addAction(label);
		connect(act, &QAction::triggered, this,
				[this, scenario]
				{
					auto *dlg = RebalanceDialog::createDemo(scenario, this);
					dlg->setAttribute(Qt::WA_DeleteOnClose);
					dlg->show();
				});
	};
	addDemo(tr("Small"), RebalanceDialog::DemoScenario::Small);
	addDemo(tr("Big"), RebalanceDialog::DemoScenario::Big);
	addDemo(tr("Really big"), RebalanceDialog::DemoScenario::ReallyBig);
}

// MARK: Help menu

void MainWindow::buildHelpMenu()
{
	auto *helpMenu = menuBar()->addMenu(tr("&Help"));
	auto *aboutAct = helpMenu->addAction(tr("About MediaMuster"));
	connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);

	helpMenu->addSeparator();

#ifdef Q_OS_MAC
	auto *permissionsAct = helpMenu->addAction(tr("Full Disk Access"));
	connect(permissionsAct, &QAction::triggered, this, &MainWindow::onCheckPermissions);
#endif

	// The diagnostic log always runs; this just surfaces it so the user can
	// send it in, even though it lives in hidden ~/Library.
	auto *revealLogAct = helpMenu->addAction(tr("Reveal Logs"));
	connect(revealLogAct, &QAction::triggered, this,
			[this]
			{
				RevealInFinder::reveal(Diagnostics::logPath(), [this](QtMsgType level, const QString &msg)
									   { addLog(level, QStringLiteral("app"), msg); });
			});
}

// MARK: - About dialog

void MainWindow::onAbout()
{
	// Deletes itself on close; see AboutDialog.
	(new AboutDialog(this))->show();
}

// MARK: - Signal wiring

void MainWindow::setupConnections()
{
	connect(
		m_volumeManager, &VolumeManager::volumesChanged, this,
		[this](const QVector<VolumeInfo> &volumes)
		{
			rebuildVolumeList(volumes);
		},
		Qt::QueuedConnection);

	connect(m_scanner, &MediaScanner::scanProgress, this, &MainWindow::onScanProgress,
			Qt::QueuedConnection);
	connect(m_scanner, &MediaScanner::scanLogBatch, this, &MainWindow::onScanLogBatch,
			Qt::QueuedConnection);
	connect(m_scanner, &MediaScanner::scanFinished, this, &MainWindow::onScanFinished,
			Qt::QueuedConnection);
	connect(
		m_scanner, &MediaScanner::scanFinalising, this,
		[this]
		{
			// Walk and parse are done; flip to an indeterminate "Finalising..."
			// so the post-walk stages can't masquerade as a frozen 100%.
			auto *dlg = progressDialog();
			dlg->setProgress(0, 0);
			dlg->setDetail(tr("Finalising..."));
		},
		Qt::QueuedConnection);

	connect(m_operations, &FileOperationController::activityChanged, this,
			[this](FileOperationController::Activity)
			{ updateActivityUi(); });
	connect(m_operations, &FileOperationController::logMessage, this, &MainWindow::addLog);
	connect(m_operations, &FileOperationController::mediaMusterTrashUsed, this,
			&MainWindow::showMediaMusterTrashDialog);
	connect(m_operations, &FileOperationController::originalsRestored, this,
			[this](const QSet<QString> &paths)
			{
				const auto roots = restorationScanPaths(m_model->allFiles(), paths);
				if (!roots.isEmpty())
					startScanWithPaths(roots);
			});
	connect(m_operations, &FileOperationController::sourcesRemoved, this,
			[this](const QSet<QString> &paths)
			{
				const int rowsBefore = m_model->rowCount();
				m_model->removeFilesByPath(paths);
				m_persistentSelectedPaths.subtract(paths);
				refreshEverything();
				addLog(QtInfoMsg, QStringLiteral("ops"),
					   QStringLiteral("Removed %1 rows from the table.").arg(rowsBefore - m_model->rowCount()));
			});

	connect(m_filterTabs, &QTabBar::currentChanged, this, &MainWindow::onFilterChanged);

	// 200 ms debounce on the expensive proxy invalidation. Chip
	// strip still updates per-keystroke for instant feedback.
	{
		auto *searchDebounce = new QTimer(this);
		searchDebounce->setSingleShot(true);
		searchDebounce->setInterval(200);
		connect(m_searchField, &QLineEdit::textChanged, searchDebounce,
				qOverload<>(&QTimer::start));
		connect(searchDebounce, &QTimer::timeout, this,
				[this]()
				{ onSearchChanged(m_searchField->text()); });

		connect(m_searchField, &QLineEdit::textChanged, this, [this]()
				{ rebuildFilterChips(); });
	}

	// 200 ms debounce on the status bar's O(n) byte walk.
	// updateStatusBar restarts the timer; doUpdateStatusBar runs
	// once the burst settles.
	m_statusBarUpdateTimer = new QTimer(this);
	m_statusBarUpdateTimer->setSingleShot(true);
	m_statusBarUpdateTimer->setInterval(200);
	connect(m_statusBarUpdateTimer, &QTimer::timeout, this, &MainWindow::doUpdateStatusBar);

	// Same pattern for the 'X MB selected' string; cheap state
	// runs synchronously, byte sum waits for the timer.
	m_selectionBytesTimer = new QTimer(this);
	m_selectionBytesTimer->setSingleShot(true);
	m_selectionBytesTimer->setInterval(200);
	connect(m_selectionBytesTimer, &QTimer::timeout, this, &MainWindow::doUpdateSelectionBytes);
	connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
			&MainWindow::onSelectionChanged);
	connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::onTableDoubleClicked);
	const auto bindButton = [](QPushButton *button, QAction *action)
	{
		QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
		QObject::connect(action, &QAction::changed, button,
						 [button, action]
						 { button->setEnabled(action->isEnabled()); });
		button->setEnabled(action->isEnabled());
	};
	bindButton(m_btnFileOps, m_manageMediaAct);
	bindButton(m_btnBinFilter, m_binFilterAct);
	bindButton(m_btnEffectFilter, m_effectFilterAct);
	bindButton(m_btnExport, m_exportAct);
	bindButton(m_btnRebalance, m_rebalanceAct);
	bindButton(m_scanButton, m_scanSelectedAct);
	bindButton(m_scanAllButton, m_scanAllAct);

	connect(m_volumeList, &QListWidget::itemSelectionChanged, this, &MainWindow::updateActivityUi);
	for (auto *model : {m_volumeList->model(), static_cast<QAbstractItemModel *>(m_proxy)})
	{
		connect(model, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateActivityUi);
		connect(model, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateActivityUi);
		connect(model, &QAbstractItemModel::modelReset, this, &MainWindow::updateActivityUi);
	}
	connect(m_model, &QAbstractItemModel::modelReset, this, &MainWindow::updateActivityUi);
	connect(m_model, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateActivityUi);
	connect(m_model, &QAbstractItemModel::dataChanged, this, &MainWindow::updateSelectionActions);

	connect(m_volumeList, &VolumeListWidget::pathsDropped, this, &MainWindow::onPathsDropped);

	connect(m_projectList, &QListWidget::itemSelectionChanged, this,
			[this]()
			{
				QSet<QString> selected;
				for (auto *item : m_projectList->selectedItems())
				{
					selected.insert(item->data(Qt::UserRole).toString());
				}
				applyFilterPreservingSelection([this, &selected]()
											   { m_proxy->setProjectFilter(selected); });
				updateStatusBar();
				rebuildFilterChips();
			});

	connect(m_tableView, &QTableView::customContextMenuRequested, this,
			&MainWindow::showTableContextMenu);

	// No keyboard shortcut for Delete: QKeySequence::Delete is Forward Delete
	// (⌦), which isn't the macOS "delete selected item" key (that's ⌘⌫), and a
	// destructive action shouldn't hang off a stray keypress. Delete stays
	// available via the right-click menu and Manage Media.
	new QShortcut(QKeySequence(Qt::Key_Escape), this,
				  [this]()
				  {
					  m_searchField->clear();
					  m_projectList->clearSelection();
					  m_filterTabs->setCurrentIndex(0);
				  });
}

// MARK: - Full Disk Access prompt

void MainWindow::onCheckPermissions()
{
#ifdef Q_OS_MAC
	bool hasFDA = VolumeManager::hasFullDiskAccess();
	if (hasFDA)
	{
		QMessageBox::information(this, QString(),
								 tr("Full Disk Access is <b>granted</b>.<br><br>"
									"I can muster all volumes and folders on this Mac!"));
	}
	else
	{
		QMessageBox box(this);
		box.setIcon(QMessageBox::Warning);
		box.setText(tr("Full Disk Access is <b>not granted</b>.<br><br>"
					   "I may not be able to muster all your media.<br><br>"
					   "<i>After granting access, quit and relaunch MediaMuster.</i>"));
		auto *openBtn = box.addButton(tr("Open System Preferences"), QMessageBox::AcceptRole);
		box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(openBtn);
		box.exec();
		if (box.clickedButton() == openBtn)
		{
			VolumeManager::openFullDiskAccessSettings();
		}
	}
#endif // Q_OS_MAC
}

// MARK: - Session-only developer features

void MainWindow::setOmfEnabled(bool enabled)
{
	// Omitting buildDebugMenu() must leave the feature unavailable too.
	enabled = enabled && m_enableOmfAct;
	if (!m_operations->isIdle())
	{
		if (m_enableOmfAct)
		{
			const QSignalBlocker blocker(m_enableOmfAct);
			m_enableOmfAct->setChecked(m_omfEnabled);
		}
		return;
	}
	if (m_omfEnabled == enabled)
		return;
	m_omfEnabled = enabled;
	if (m_enableOmfAct)
	{
		const QSignalBlocker blocker(m_enableOmfAct);
		m_enableOmfAct->setChecked(enabled);
	}
	if (!enabled)
	{
		QSet<QString> legacyPaths;
		for (const auto &file : m_model->allFiles())
			if (file.omfEra || Conventions::isOmfRootName(file.mediaFolderName) ||
				Conventions::hasOmfEraExtension(file.fileName))
				legacyPaths.insert(file.filePath);
		m_model->removeFilesByPath(legacyPaths);
		m_persistentSelectedPaths.subtract(legacyPaths);
		refreshEverything();
		updateActivityUi();
	}
	addLog(QtInfoMsg, QStringLiteral("scanner"), enabled ? tr("Legacy media files are ON for this session. Rescan to include OMFI MediaFiles.") : tr("Legacy media files are OFF for this session."));
}

// MARK: - Precompute classification, details and filter

void MainWindow::setPrecomputesEnabled(bool enabled)
{
	enabled = enabled && m_enablePrecomputesAct;
	if (!m_operations->isIdle())
	{
		if (m_enablePrecomputesAct)
		{
			const QSignalBlocker blocker(m_enablePrecomputesAct);
			m_enablePrecomputesAct->setChecked(m_precomputesEnabled);
		}
		return;
	}
	if (m_precomputesEnabled == enabled)
		return;
	m_precomputesEnabled = enabled;
	applyFilterPreservingSelection([this, enabled]()
								   {
		// A sort column that is about to disappear must not keep controlling
		// the rows while its heading is no longer available to the editor.
		if (!enabled && m_proxy->sortColumn() >= Enum::to_underlying(MediaTableModel::Column::PrecomputeCategory))
			m_tableView->sortByColumn(Enum::to_underlying(MediaTableModel::Column::ClipName), Qt::AscendingOrder);
		const int tab = m_filterTabs->currentIndex();
		if (!enabled && tab >= 0 && tab < static_cast<int>(kFilterDefs.size()) &&
			kFilterDefs[tab].mode == MediaFilterProxy::FilterMode::Precompute)
		{
			const QSignalBlocker blocker(m_filterTabs);
			m_filterTabs->setCurrentIndex(0);
		}
		m_proxy->setPrecomputesEnabled(enabled);
		m_model->setPrecomputesEnabled(enabled); });
	if (m_enablePrecomputesAct)
	{
		const QSignalBlocker blocker(m_enablePrecomputesAct);
		m_enablePrecomputesAct->setChecked(enabled);
	}
	m_btnEffectFilter->setVisible(enabled);
	m_effectFilterAct->setVisible(enabled);
	updateActivityUi();
	if (enabled)
	{
		// Put the newly enabled details together after Type, ahead of
		// Date Created, without moving or resetting the existing columns.
		auto *header = m_tableView->horizontalHeader();
		int position = header->visualIndex(Enum::to_underlying(MediaTableModel::Column::Type)) + 1;
		for (auto column : {MediaTableModel::Column::PrecomputeCategory, MediaTableModel::Column::EffectCategory,
							MediaTableModel::Column::Effect, MediaTableModel::Column::EffectSequence})
		{
			const int logical = Enum::to_underlying(column);
			header->moveSection(header->visualIndex(logical), position++);
			m_tableView->setColumnWidth(logical,
										column == MediaTableModel::Column::PrecomputeCategory || column == MediaTableModel::Column::EffectCategory ? 180 : 240);
		}
	}
	updateFilterCounts();
	rebuildFilterChips();
	updateStatusBar();
	addLog(QtInfoMsg, QStringLiteral("effects"), enabled ? QStringLiteral("Precompute filters are ON for this session.") : QStringLiteral("Precompute filters are OFF for this session."));
}

void MainWindow::onFilterByEffects()
{
	if (!m_precomputesEnabled || !m_operations->isIdle() || m_model->allFiles().isEmpty())
		return;
	EffectFilterDialog dialog(m_model->allFiles(), m_proxy->precomputeTreeFilter(), m_proxy->effectVolumeFilter(), this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	const PrecomputeFilter filter = dialog.precomputeFilter();
	const QString volume = dialog.selectedVolume();
	applyFilterPreservingSelection([this, &filter, &volume]()
								   {
		m_proxy->setPrecomputeTreeFilter(filter);
		m_proxy->setEffectVolumeFilter(volume); });
	rebuildFilterChips();
	updateStatusBar();
	QStringList checkedPaths;
	for (const auto &path : filter.paths)
	{
		QStringList names;
		for (const auto &name : {path.precomputeCategory, path.effectCategory, path.effect})
			if (!name.isEmpty())
				names.append(name);
		checkedPaths.append(names.isEmpty() ? QStringLiteral("all precomputes") : names.join(QStringLiteral(" / ")));
	}
	const QString choices = !filter.active			 ? QStringLiteral("all precomputes")
							: checkedPaths.isEmpty() ? QStringLiteral("no checked branches")
													 : checkedPaths.join(QStringLiteral("; "));
	const QString location = volume.isEmpty() ? QStringLiteral("all scanned volumes") : volume;
	addLog(QtInfoMsg, QStringLiteral("effects"),
		   !filter.active && volume.isEmpty() ? QStringLiteral("Precompute filter removed.")
											  : QStringLiteral("Precompute filter active: %1 in %2.").arg(choices, location));
}

// MARK: - Bin filter

void MainWindow::onFilterByBins()
{
	if (!m_operations->isIdle())
		return;
	// Lazy construction; the dialog stays parented to the main window
	// so chain state persists across show/hide.
	if (!m_binFilterDialog)
	{
		m_binFilterDialog = new BinFilterDialog(this);
		connect(m_binFilterDialog, &BinFilterDialog::loadError, this,
				[this](const QString &path, const QString &reason)
				{
					addLog(QtWarningMsg, QStringLiteral("binfilter"),
						   tr("Bin unavailable: %1: %2").arg(path, reason));
				});
		connect(m_binFilterDialog, &BinFilterDialog::binsChanged, this,
				[this](const QVector<AvbBin> &bins)
				{
					applyFilterPreservingSelection([this, &bins]
												   { m_model->setAvbBins(bins); });
					updateStatusBar();
				});
		// Apply the chain change through the selection-preserving
		// helper so the user's selection survives the filter shuffle.
		connect(m_binFilterDialog, &BinFilterDialog::filterChainChanged, this,
				[this](const BinFilter &filter, const QStringList &)
				{
					applyFilterPreservingSelection(
						[this, &filter]()
						{ m_proxy->setBinFilter(filter); });
				});
		connect(
			m_binFilterDialog, &BinFilterDialog::filterChainChanged, this,
			[this](const BinFilter &filter, const QStringList &binNames)
			{
				m_binFilterActive = filter.isActive();
				m_binFilterBinNames = binNames;
				rebuildFilterChips();

				if (!filter.isActive())
				{
					addLog(QtInfoMsg, QStringLiteral("binfilter"), "Bin filter removed.");
					updateStatusBar();
					return;
				}
				addLog(QtInfoMsg, QStringLiteral("binfilter"),
					   QStringLiteral("Bin filter active: %1 steps.").arg(filter.steps.size()));
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
	if (!m_operations->isIdle() || m_model->allFiles().isEmpty() || !m_operations->resolvePreviousJob())
		return;

	// MXF root = grandparent of the file:
	//   <volume>/Avid MediaFiles/MXF/<folder>/<file.mxf>
	//                              ^^^^^^^^ this is the root we want
	QHash<QString, QString> mxfRootsByLabel;
	QHash<QString, QVector<MediaFile>> filesByMxfRoot;
	QHash<QString, int> countByLabel;
	QHash<QString, QString> volumePathByLabel;

	// Each MXF root gets one stable, unique label so the label→root map stays
	// 1:1. Without this, two volumes sharing a name (two 'Backup' mounts)
	// would collide and one root would silently vanish from the picker.
	QHash<QString, QString> labelByRoot;
	QSet<QString> usedLabels;

	for (const MediaFile &mf : m_model->allFiles())
	{
		if (!RebalancePlanner::isEligible(mf))
			continue;
		const QString folderDir = QFileInfo(mf.filePath).absolutePath();
		const QString mxfRoot = QFileInfo(folderDir).absolutePath();

		QString label = labelByRoot.value(mxfRoot);
		if (label.isEmpty())
		{
			// First file from this root — settle its label once.
			QString base = mf.volumeName;
			if (base.isEmpty())
				base = QFileInfo(mf.volumePath).fileName();
			if (base.isEmpty())
				base = mxfRoot;

			label = base;
			for (int n = 2; usedLabels.contains(label); ++n)
				label = QStringLiteral("%1 (%2)").arg(base).arg(n);

			labelByRoot.insert(mxfRoot, label);
			usedLabels.insert(label);
			mxfRootsByLabel.insert(label, mxfRoot);
			volumePathByLabel.insert(label, mf.volumePath);
		}

		filesByMxfRoot[mxfRoot].append(mf);
		countByLabel[label] += 1;
	}

	if (filesByMxfRoot.isEmpty())
	{
		QMessageBox::warning(this, tr("Rebalance"),
							 tr("No 'Avid MediaFiles/MXF' folders were found in "
								"the current scan. Rebalance only operates on "
								"Avid's file structure."));
		return;
	}

	// Default to whichever volume has the most scanned files.
	QString initialLabel;
	int maxCount = -1;
	for (auto it = countByLabel.constBegin(); it != countByLabel.constEnd(); ++it)
	{
		if (it.value() > maxCount)
		{
			maxCount = it.value();
			initialLabel = it.key();
		}
	}

	RebalanceDialog dlg(mxfRootsByLabel, filesByMxfRoot, initialLabel, this);
	dlg.beforeRebalance = [this, &dlg]
	{
		if (m_operations->resolveBeforeRebalance())
			return true;
		if (m_operations->manager()->isRunning())
			dlg.done(QDialog::Rejected);
		return false;
	};
	connect(&dlg, &RebalanceDialog::logMessage, this, [this](QtMsgType level, const QString &msg)
			{ addLog(level, QStringLiteral("rebalance"), msg); });
	m_operations->setActivity(FileOperationController::Activity::RebalanceDialog);
	dlg.exec();
	m_operations->endRebalanceDialog();
	m_operations->refreshHistory();

	// Re-scan if didRebalance; a cancelled run can still have
	// moved files.
	if (!dlg.didRebalance())
		return;

	const QString volumePath = volumePathByLabel.value(dlg.rebalancedLabel());
	if (volumePath.isEmpty())
	{
		addLog(QtWarningMsg, QStringLiteral("rebalance"),
			   "Couldn't determine volume path for rescan; please scan manually.");
		return;
	}
	addLog(QtInfoMsg, QStringLiteral("rebalance"), QStringLiteral("Re-scanning '%1' after rebalance").arg(volumePath));
	startScanWithPaths(QStringList() << volumePath);
}

// MARK: - Volume list management

namespace
{
	// Disk Utility-style suffix: a row whose name collides with another gets
	// its last path component appended to keep the two distinct, falling
	// back to the full path when that component still echoes the name
	// (/Volumes/Backup and /Volumes/Backup-1 are both called "Backup").
	// Shared by the detected-volume rebuild and by manual adds, so two rows
	// can't end up identically labelled depending on how they arrived.
	QString volumeNameSuffix(const QString &path, const QString &name)
	{
		if (path.isEmpty())
			return {};

		QString p = path;
		while (p.endsWith(QLatin1Char('/')) || p.endsWith(QLatin1Char('\\')))
			p.chop(1);

		QString basename = QFileInfo(p).fileName();
		if (basename.isEmpty())
			basename = p;

		if (basename.compare(name, Qt::CaseInsensitive) == 0)
			return path;
		return basename;
	}

	QString disambiguated(const QString &name, const QString &path)
	{
		const QString suffix = volumeNameSuffix(path, name);
		return suffix.isEmpty() ? name : QStringLiteral("%1 - %2").arg(name, suffix);
	}
} // namespace

QListWidgetItem *MainWindow::makeVolumeItem(const VolumeInfo &v, const QString &displayName)
{
	// Hand-added folders use their containing volume's native icon too.
	const QString root = QStorageInfo(v.path).rootPath();
	const QIcon icon = QFileIconProvider().icon(QFileInfo(root.isEmpty() ? v.path : root));
	auto *item = new QListWidgetItem(icon, displayName);
	item->setData(Qt::UserRole, v.path);

	QString tooltip = v.path;
	if (v.totalBytes > 0)
		tooltip += tr("\n%1 of %2 used (%3)")
					   .arg(Format::bytes(v.usedBytes), Format::bytes(v.totalBytes), v.volumeType);
	item->setToolTip(tooltip);

	if (v.hasAvidMedia)
	{
		QFont f = item->font();
		f.setBold(true);
		item->setFont(f);
	}
	return item;
}

void MainWindow::onPathsDropped(const QStringList &paths)
{
	for (const QString &path : paths)
		addVolumePath(path);
}

void MainWindow::addVolumePath(const QString &path)
{
	if (!MediaScanner::canScanPath(path))
	{
		const QString message = tr("Please add a recognised Avid folder, or its parent.");
		addLog(QtWarningMsg, QStringLiteral("volumes"), message);
		statusBar()->showMessage(message, 10000);
		return;
	}
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

	// Through the same factory the detected volumes use, so a folder added
	// by hand gets its real volume type, its size, and the bold that says
	// "there is Avid media in here" — all of which it used to go without.
	const VolumeInfo info = VolumeManager::makeVolumeInfo(name, path, QStorageInfo(path));

	// Disambiguate against whatever is already listed, for the same reason
	// the rebuild does it among detected volumes: two folders called Media
	// from different parents must not both read as "Media".
	QString displayName = info.name;
	for (int i = 0; i < m_volumeList->count(); ++i)
	{
		if (m_volumeList->item(i)->text().compare(info.name, Qt::CaseInsensitive) != 0)
			continue;
		displayName = disambiguated(info.name, info.path);
		break;
	}

	auto *item = makeVolumeItem(info, displayName);
	item->setSelected(true);
	m_volumeList->addItem(item);
	m_manualVolumes.insert(path);
	addLog(QtInfoMsg, QStringLiteral("volumes"), QStringLiteral("Added: %1").arg(path));
}

void MainWindow::refreshVolumes()
{
	// Re-detect now, then seed the cache so the next async poll has
	// something to diff against.
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

	// Count name collisions (e.g. two network drives both labelled 'Data') so we
	// know which entries need a disambiguating suffix.
	QHash<QString, int> nameCounts;
	for (const auto &d : volumes)
		++nameCounts[d.name];

	for (const VolumeInfo &d : volumes)
	{
		const QString displayName =
			nameCounts.value(d.name) > 1 ? disambiguated(d.name, d.path) : d.name;
		auto *item = makeVolumeItem(d, displayName);

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
	addLog(QtInfoMsg, QStringLiteral("volumes"),
		   QStringLiteral("Found %1 volumes; %2 contain Avid media.").arg(volumes.size()).arg(ac));
}

// MARK: - Scan controls

void MainWindow::scanSelected()
{
	if (!m_operations->isIdle())
		return;
	// No in-scan cancel branch: a running scan raises the modal progress sheet,
	// whose Cancel button is the stop control, so neither this button nor its
	// menu action is reachable mid-scan. (MediaScanner::startScan also self-
	// guards against a double start.) This handler therefore only begins a scan.
	QStringList paths;
	for (auto *item : m_volumeList->selectedItems())
		paths << item->data(Qt::UserRole).toString();

	if (paths.isEmpty())
		return;
	startScanWithPaths(paths);
}

void MainWindow::scanEverything()
{
	if (!m_operations->isIdle() || m_volumeList->count() == 0)
		return;
	// See scanSelected: cancel is via the modal progress sheet, so there is no
	// reachable in-scan cancel path here. Only ever begins a scan.
	QStringList paths = m_volumeManager->allScannablePaths();
	for (const QString &mp : m_manualVolumes)
	{
		if (!paths.contains(mp))
			paths.append(mp);
	}

	addLog(QtInfoMsg, QStringLiteral("scanner"), QStringLiteral("Scan All: %1 locations").arg(paths.size()));
	startScanWithPaths(paths);
}

void MainWindow::startScanWithPaths(const QStringList &paths)
{
	if (!m_operations->isIdle() || paths.isEmpty())
		return;
	// Detected volumes and hand-added folders scan differently (see
	// MediaScanner::Options): a volume is probed at its root only; manual
	// paths resolve a managed tree or its immediate container. Callers use
	// one merged list — the ticked rows, Scan All, the post-rebalance
	// rescan — so the split is made here, once. A path is a volume only
	// when VolumeManager detected it (a mount, or a system-drive base);
	// everything else is scanned as a folder — including the rescan after a
	// rebalance, whose path is the scanned root the rows derived (".../Avid
	// MediaFiles" for a hand-added ".../MXF/3"), which is not the string
	// the user added and would otherwise be probed as a drive and miss.
	const QStringList detected = m_volumeManager->allScannablePaths();
	MediaScanner::Options opts;
	opts.includeOmf = m_omfEnabled;
	for (const QString &path : paths)
	{
		if (detected.contains(path) && !m_manualVolumes.contains(path))
			opts.volumePaths.append(path);
		else
			opts.manualPaths.append(path);
	}

	m_operations->setActivity(FileOperationController::Activity::Scanning);
	progressDialog()->begin();
	progressDialog()->setDetail(tr("Starting scan..."));
	m_scanTimer.start();

	m_scanner->startScan(opts);
}

void MainWindow::onScanProgress(int current, int total, const QString &currentPath)
{
	auto *dlg = progressDialog();
	dlg->setProgress(current, total);

	QString displayPath = currentPath;

#ifdef Q_OS_MAC
	// Drop the leading slash on /Volumes/ paths so the drive name leads; other
	// (system-volume) paths are shown exactly as they are.
	if (displayPath.startsWith("/Volumes/"))
		displayPath.remove(0, 1);
#else // Q_OS_WIN
	displayPath = QDir::toNativeSeparators(displayPath);
#endif

	dlg->setDetail(displayPath);
}

void MainWindow::onScanLogBatch(const QVector<LogMsg> &batch)
{
	// One appendPlainText per batch; keeps the console responsive
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
		combined += formatLogLine(now, batch[i].level, batch[i].module, batch[i].message);
		// Also write each console message to the diagnostic log.
		Diagnostics::appendConsoleLine(batch[i].level, batch[i].module, batch[i].message);
	}
	m_console->appendPlainText(combined);
}

void MainWindow::onScanFinished(const QVector<MediaFile> &results)
{
	m_model->setMediaFiles(results);
	m_persistentSelectedPaths.clear();

	const qint64 elapsed = m_scanTimer.isValid() ? m_scanTimer.elapsed() : 0;
	QString timeStr = tr("Scan: %1 ms").arg(elapsed);
	m_statusScanTime->setText(timeStr);

	// Fresh dataset: clear any filters left over from the previous scan
	// before tallying, so the counts and table reflect the full results.
	resetFiltersForNewScan();
	refreshEverything();

	m_operations->setActivity(FileOperationController::Activity::Idle);
}

void MainWindow::refreshEverything()
{
	rebuildProjectList();
	updateFilterCounts();
	rebuildFilterChips();
	updateStatusBar();
}

void MainWindow::rebuildProjectList()
{
	QSet<QString> selected;
	for (auto *item : m_projectList->selectedItems())
		selected.insert(item->data(Qt::UserRole).toString());
	const QSignalBlocker blocker(m_projectList);
	m_projectList->clear();
	struct ProjectStat
	{
		int count = 0;
		qint64 bytes = 0;
		bool hasProject = true;
	};
	// Sidebar totals describe the whole scan, even when the table is filtered.
	QHash<QString, ProjectStat> projectStats;
	for (const auto &file : m_model->allFiles())
	{
		auto &stat = projectStats[file.projectDisplay()];
		++stat.count;
		stat.bytes += file.sizeBytes;
		stat.hasProject = !file.hasNoProject();
	}
	QStringList names = projectStats.keys();
	names.sort();
	for (const auto &name : names)
	{
		const auto &stat = projectStats[name];
		const QIcon icon = QApplication::style()->standardIcon(
			stat.hasProject ? QStyle::SP_DirIcon : QStyle::SP_MessageBoxInformation);
		auto *item = new QListWidgetItem(icon, name);
		item->setData(Qt::UserRole, name);
		QString tip = tr("%1 files, %2").arg(stat.count).arg(Format::bytes(stat.bytes));
		if (!stat.hasProject)
			tip += QStringLiteral("\n\n") + MediaFile::noProjectWhy();
		item->setToolTip(tip);
		m_projectList->addItem(item);
		item->setSelected(selected.contains(name));
	}
	QSet<QString> retained;
	for (auto *item : m_projectList->selectedItems())
		retained.insert(item->data(Qt::UserRole).toString());
	applyFilterPreservingSelection([this, &retained]()
								   { m_proxy->setProjectFilter(retained); });
}

// MARK: - Filter / search slots

void MainWindow::onFilterChanged(int index)
{
	// kFilterDefs is the shared source of truth; the same array that
	// fed the tab labels in setupUi.
	if (index >= 0 && index < static_cast<int>(kFilterDefs.size()))
	{
		if (!m_precomputesEnabled && kFilterDefs[index].mode == MediaFilterProxy::FilterMode::Precompute)
		{
			const QSignalBlocker blocker(m_filterTabs);
			m_filterTabs->setCurrentIndex(0);
			index = 0;
		}
		applyFilterPreservingSelection([this, index]()
									   { m_proxy->setFilterMode(kFilterDefs[index].mode); });
		updateStatusBar();
		rebuildFilterChips();
	}
}

void MainWindow::onSearchChanged(const QString &text)
{
	applyFilterPreservingSelection([this, &text]()
								   { m_proxy->setSearchText(text); });
	updateStatusBar();
	rebuildFilterChips();
}

void MainWindow::onSelectionChanged()
{
	// Sync the persistent path set from the current visible selection.
	// Skipped during applyFilterPreservingSelection's restore phase;
	// the proxy drops hidden rows from the selection model there, and
	// absorbing that would silently forget the user's earlier picks.
	if (!m_inFilterRestore)
	{
		m_persistentSelectedPaths.clear();
		const auto rows = m_tableView->selectionModel()->selectedRows();
		for (const QModelIndex &idx : rows)
			m_persistentSelectedPaths.insert(fileForProxyIndex(idx).filePath);
	}

	// Counting via ranges is O(ranges), not O(rows), so it's faster.
	const auto selection = m_tableView->selectionModel()->selection();
	int selectedCount = 0;
	for (const QItemSelectionRange &range : selection)
		selectedCount += range.bottom() - range.top() + 1;
	const bool hasSelection = selectedCount > 0;

	// Background bin metadata can change the selection while a scan or
	// operation is running. Preserve the busy gate during that restoration.
	updateSelectionActions();
	m_statusSep1->setVisible(hasSelection);
	m_statusSelected->setVisible(hasSelection);
	m_statusSep2->setVisible(hasSelection);
	m_statusSelSize->setVisible(hasSelection);

	if (!hasSelection)
		return;

	m_statusSelected->setText(tr("%1 selected").arg(Format::count(selectedCount)));

	// Defer the byte-sum walk. For Cmd-A on a big table it's
	// O(N × log N): one mapToSource per row, each one hitting the
	// proxy's index. Debouncing coalesces rapid selection changes
	// (arrow keys, shift-click ranges) into one tally.
	m_selectionBytesTimer->start();
}

void MainWindow::doUpdateSelectionBytes()
{
	qint64 selBytes = 0;
	const auto selection = m_tableView->selectionModel()->selection();
	for (const QItemSelectionRange &range : selection)
		selBytes += sumBytesInProxyRange(range.top(), range.bottom());
	m_statusSelSize->setText(tr("%1 selected").arg(Format::bytes(selBytes)));
}

// MARK: - Selection persistence

void MainWindow::applyFilterPreservingSelection(const std::function<void()> &mutation)
{
	// selectionChanged fires both when the proxy drops hidden rows,
	// and when we rewrite the visible selection.
	m_inFilterRestore = true;

	mutation();

	if (!m_persistentSelectedPaths.isEmpty())
	{
		// Re-select any rows whose path is in the persistent set.
		// Rows that became hidden by the filter stay in the set but
		// aren't selected; when they reappear, bring them back.

		QVector<int> rows;
		const int rowCount = m_proxy->rowCount();
		for (int row = 0; row < rowCount; ++row)
		{
			if (m_persistentSelectedPaths.contains(fileAtProxyRow(row).filePath))
				rows.append(row);
		}
		const QItemSelection newSelection = selectionForRows(rows);

		auto *selModel = m_tableView->selectionModel();
		selModel->clearSelection();
		if (!newSelection.isEmpty())
			selModel->select(newSelection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}

	m_inFilterRestore = false;
}

// MARK: - File operation entry points

void MainWindow::onFileOperations()
{
	openManageMedia(Enum::to_underlying(ManageMediaDialog::Operation::Copy));
}

void MainWindow::openManageMedia(int initialOp)
{
	if (!m_operations->isIdle())
		return;
	auto files = selectedFiles();
	if (files.isEmpty())
		return;

	ManageMediaDialog dlg(files, this, static_cast<ManageMediaDialog::Operation>(initialOp));
	if (dlg.exec() != QDialog::Accepted)
		return;

	OpKind kind = OpKind::Copy;
	switch (dlg.operation())
	{
	case ManageMediaDialog::Operation::Copy:
		kind = OpKind::Copy;
		break;
	case ManageMediaDialog::Operation::Move:
		kind = OpKind::Move;
		break;
	case ManageMediaDialog::Operation::Delete:
		kind = OpKind::Delete;
		break;
	}
	dispatchOperation(kind, std::move(files), dlg.destination(), dlg.preserveStructure(),
					  dlg.conflictPolicies());
}

bool MainWindow::dispatchOperation(OpKind kind, QVector<MediaFile> files, const QString &dest,
								   bool preserve, const QHash<QString, ConflictPolicy> &policies)
{
	// One shape for every dispatch: the selection becomes a request (the
	// engine's whole read of a MediaFile happens in itemsFromMediaFiles),
	// and the shared tail below does the gate + engine call.
	OpRequest req;
	req.kind = kind;
	req.destRoot = dest;
	req.preserve = preserve;
	req.items = OpManager::itemsFromMediaFiles(files, policies);
	return m_operations->dispatchRequest(std::move(req));
}

// MARK: - MediaMuster Trash dialog

void MainWindow::showMediaMusterTrashDialog(const QString &trashFolderPath, int fileCount)
{
	QMessageBox msgBox(this);
	msgBox.setIcon(QMessageBox::Information);
	msgBox.setWindowTitle(tr("MediaMuster Trash"));
	msgBox.setText(tr("<b>%n file(s) moved to the MediaMuster Trash</b>", nullptr, fileCount));
	msgBox.setInformativeText(tr("Files were moved to:\n\n%1\n\n"
								 "Their original locations are recorded in the operation journal. "
								 "Moving files to this folder does not free disk space.")
								  .arg(trashFolderPath));
	msgBox.addButton(QMessageBox::Ok);
	auto *open = msgBox.addButton(tr("Open Folder"), QMessageBox::ActionRole);
	msgBox.exec();
	if (msgBox.clickedButton() == open)
		QDesktopServices::openUrl(QUrl::fromLocalFile(trashFolderPath));
}

// MARK: - CSV export

void MainWindow::onExportCsv()
{
	if (!m_operations->isIdle() || m_exportInProgress || m_proxy->rowCount() == 0)
		return;
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
		this, tr("Export CSV"), QDir::homePath() + "/mediamuster_export.csv", tr("CSV Files (*.csv)"));
	if (path.isEmpty())
		return;

	// Snapshot on the main thread; item models are thread-affine.
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
			rows.append(fileAtProxyRow(row));
	}

	const int count = rows.size();
	const MediaCsv::Options csvOptions{m_precomputesEnabled};
	const QString label = exportSelected ? "selected" : "visible";
	addLog(QtInfoMsg, QStringLiteral("export"), QStringLiteral("CSV export of %1 %2 rows to %3.").arg(count).arg(label, path));
	m_exportInProgress = true;
	updateActivityUi();

	// Dispatch the write to a worker so big exports don't freeze the UI.
	auto *watcher = new QFutureWatcher<bool>(this);
	connect(watcher, &QFutureWatcher<bool>::finished, this,
			[this, watcher, path]()
			{
				const bool ok = watcher->result();
				watcher->deleteLater();
				m_exportInProgress = false;
				updateActivityUi();
				if (!ok)
					addLog(QtCriticalMsg, QStringLiteral("export"), QStringLiteral("CSV export failed: %1.").arg(path));
			});

	watcher->setFuture(
		QtConcurrent::run([path, rows = std::move(rows), csvOptions]()
						  { return MediaCsv::write(path, rows, csvOptions); }));
}

// MARK: - Reveal in Finder

void MainWindow::onRevealInFinder()
{
	const auto sel = selectedFiles();
	if (sel.isEmpty())
		return;

	RevealInFinder::reveal(sel.first().filePath, [this](QtMsgType level, const QString &message)
						   { addLog(level, QStringLiteral("reveal"), message); });
}

// MARK: - Select relatives

void MainWindow::onSelectRelatives()
{
	// Add visible files sharing a MasterMobId with the current selection.
	// The total includes files that were already selected.
	const auto sel = selectedFiles();
	if (sel.isEmpty())
		return;

	QSet<QString> masterIds;
	for (const MediaFile &f : sel)
	{
		if (!f.masterMobId.isEmpty())
			masterIds.insert(f.masterMobId);
	}

	if (masterIds.isEmpty())
	{
		addLog(QtWarningMsg, QStringLiteral("relatives"),
			   "No MasterMobId found in the selected files. Relatives can't be matched.");
		return;
	}

	QVector<int> rows;
	for (int row = 0; row < m_proxy->rowCount(); ++row)
	{
		const MediaFile &f = fileAtProxyRow(row);
		if (masterIds.contains(f.masterMobId))
			rows.append(row);
	}

	// The selection-change handler records visible rows; retain earlier
	// selections hidden by filters while selecting these visible relatives.
	const auto previousPaths = m_persistentSelectedPaths;
	auto *selModel = m_tableView->selectionModel();
	selModel->select(selectionForRows(rows), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	m_persistentSelectedPaths.unite(previousPaths);
	const QModelIndex first = m_proxy->index(rows.first(), 0);
	selModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
	m_tableView->scrollTo(first, QAbstractItemView::EnsureVisible);

	addLog(QtInfoMsg, QStringLiteral("relatives"),
		   QStringLiteral("Selected %1 file%2 across %3 master clip%4.")
			   .arg(rows.size())
			   .arg(rows.size() == 1 ? "" : "s")
			   .arg(masterIds.size())
			   .arg(masterIds.size() == 1 ? "" : "s"));
}

// MARK: - Select inverse

// Flips the visible selection. Hidden rows are left alone, so
// you can't accidentally select filtered-out media.
void MainWindow::onInvertSelection()
{
	auto *selModel = m_tableView->selectionModel();
	const int rowCount = m_proxy->rowCount();
	if (rowCount == 0)
		return;

	// Snapshot the currently selected proxy rows so the inversion
	// pass is O(1) per row.
	QSet<int> currentlySelected;
	for (const QModelIndex &idx : selModel->selectedRows())
		currentlySelected.insert(idx.row());

	// Everything not currently selected; the helper coalesces contiguous
	// runs so a big table gets one event per run, not one per row.
	QVector<int> rows;
	for (int row = 0; row < rowCount; ++row)
	{
		if (!currentlySelected.contains(row))
			rows.append(row);
	}
	const QItemSelection newSelection = selectionForRows(rows);

	const int newCount = rows.size();
	selModel->clearSelection();
	if (newCount > 0)
		selModel->select(newSelection, QItemSelectionModel::Select | QItemSelectionModel::Rows);

	addLog(QtInfoMsg, QStringLiteral("selection"),
		   QStringLiteral("Selection inverted: %1 of %2 visible row%3 selected.")
			   .arg(newCount)
			   .arg(rowCount)
			   .arg(rowCount == 1 ? "" : "s"));
}

void MainWindow::onTableDoubleClicked(const QModelIndex &)
{
	onRevealInFinder();
}

// MARK: - Context menu

void MainWindow::showTableContextMenu(const QPoint &pos)
{
	const QModelIndex index = m_tableView->indexAt(pos);

	// macOS convention (Finder, Mail): right-clicking a row that isn't
	// part of the current selection selects it first, so every action
	// below acts on the row under the pointer — never on a stale
	// selection that may be scrolled out of view.
	if (index.isValid() && !m_tableView->selectionModel()->isSelected(index))
	{
		m_tableView->selectionModel()->select(
			index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		m_tableView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
	}

	QMenu menu(this);

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

	updateSelectionActions();
	menu.addAction(m_revealAct);
	auto *copyPathAct = menu.addAction(tr("Copy Path"),
									   [this]()
									   {
										   const auto sel = selectedFiles();
										   if (sel.isEmpty())
											   return;
										   QStringList paths;
										   for (const auto &f : sel)
											   paths << f.filePath;
										   QApplication::clipboard()->setText(paths.join("\n"));
									   });

	copyPathAct->setEnabled(m_tableView->selectionModel()->hasSelection());
	menu.addAction(m_selectRelativesAct);
	menu.addAction(m_selectInverseAct);

	menu.addSeparator();
	menu.addAction(tr("Copy To…"), this, [this]()
				   { openManageMedia(Enum::to_underlying(ManageMediaDialog::Operation::Copy)); })
		->setEnabled(m_manageMediaAct->isEnabled());
	menu.addAction(tr("Move To…"), this, [this]()
				   { openManageMedia(Enum::to_underlying(ManageMediaDialog::Operation::Move)); })
		->setEnabled(m_manageMediaAct->isEnabled());
	menu.addAction(tr("Delete…"), this, [this]()
				   { openManageMedia(Enum::to_underlying(ManageMediaDialog::Operation::Delete)); })
		->setEnabled(m_manageMediaAct->isEnabled());
	menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

// MARK: - Selection helper

QVector<MediaFile> MainWindow::selectedFiles() const
{
	const auto rows = m_tableView->selectionModel()->selectedRows();
	QVector<MediaFile> result;
	result.reserve(rows.size());
	for (const auto &pi : rows)
		result.append(fileForProxyIndex(pi));
	return result;
}

// MARK: - Proxy row lookup

const MediaFile &MainWindow::fileForProxyIndex(const QModelIndex &proxyIndex) const
{
	return m_model->fileAt(m_proxy->mapToSource(proxyIndex).row());
}

const MediaFile &MainWindow::fileAtProxyRow(int proxyRow) const
{
	return fileForProxyIndex(m_proxy->index(proxyRow, 0));
}

QItemSelection MainWindow::selectionForRows(const QVector<int> &proxyRows) const
{
	// Coalesce contiguous runs into single full-width ranges so the view
	// fires one selectionChanged per run, not one per row. `proxyRows` must
	// be ascending — every caller iterates rows in order.
	QItemSelection sel;
	const int lastCol = m_proxy->columnCount() - 1;
	const int n = proxyRows.size();
	for (int i = 0; i < n;)
	{
		const int start = proxyRows[i];
		int end = start;
		while (i + 1 < n && proxyRows[i + 1] == end + 1)
			end = proxyRows[++i];
		sel.select(m_proxy->index(start, 0), m_proxy->index(end, lastCol));
		++i;
	}
	return sel;
}

qint64 MainWindow::sumBytesInProxyRange(int first, int last) const
{
	qint64 sum = 0;
	for (int row = first; row <= last; ++row)
		sum += fileAtProxyRow(row).sizeBytes;
	return sum;
}

// MARK: - Console logging

void MainWindow::addLog(QtMsgType level, const QString &module, const QString &message)
{
	m_console->appendPlainText(
		formatLogLine(QTime::currentTime().toString("HH:mm:ss"), level, module, message));
	// Also write the console message to the diagnostic log.
	Diagnostics::appendConsoleLine(level, module, message);
}

// MARK: - Column auto-fit

void MainWindow::autoFitColumns()
{
	// Let wide paths use horizontal scrolling instead of imposing a width cap.
	// Qt's header samples rows when measuring, keeping large scans responsive.
	m_tableView->resizeColumnsToContents();
}

// MARK: - Busy state

void MainWindow::updateActivityUi()
{
	const bool busy = !m_operations->isIdle();
	m_addFolderAct->setEnabled(!busy);
	m_refreshVolumesAct->setEnabled(!busy);
	m_scanSelectedAct->setEnabled(!busy && m_volumeList->selectionModel()->hasSelection());
	m_scanAllAct->setEnabled(!busy && m_volumeList->count() > 0);
	m_binFilterAct->setEnabled(!busy);
	m_rebalanceAct->setEnabled(!busy && !m_model->allFiles().isEmpty());
	m_exportAct->setEnabled(!busy && !m_exportInProgress && m_proxy->rowCount() > 0);
	m_effectFilterAct->setEnabled(!busy && m_precomputesEnabled && !m_model->allFiles().isEmpty());
	const bool hasSelection = m_tableView->selectionModel()->hasSelection();
	m_manageMediaAct->setEnabled(!busy && hasSelection);
	m_revealAct->setEnabled(!busy && hasSelection);
	m_selectInverseAct->setEnabled(!busy && m_proxy->rowCount() > 0);
	m_selectRelativesAct->setEnabled(!busy && hasSelection && m_selectionHasMasterMob);
	if (m_enablePrecomputesAct)
		m_enablePrecomputesAct->setEnabled(!busy);
	if (m_enableOmfAct)
		m_enableOmfAct->setEnabled(!busy);

	if (!busy && m_progressDialog)
		m_progressDialog->finish();

	// Skip the 5 second polling tick when a scan or op is running.
	m_volumeManager->setBusy(busy);
}

void MainWindow::updateSelectionActions()
{
	const auto selection = m_tableView->selectionModel()->selection();
	// Only selection/metadata changes need this walk. Volume-list and activity
	// updates reuse the result, including for large selections without MOB IDs.
	m_selectionHasMasterMob = false;
	for (const auto &range : selection)
	{
		for (int row = range.top(); row <= range.bottom(); ++row)
		{
			if (!fileAtProxyRow(row).masterMobId.isEmpty())
			{
				m_selectionHasMasterMob = true;
				break;
			}
		}
		if (m_selectionHasMasterMob)
			break;
	}
	updateActivityUi();
}

// MARK: - Progress dialog

ProgressDialog *MainWindow::progressDialog()
{
	// This dialog belongs to scanning; file jobs own their progress dialog.
	if (!m_progressDialog)
	{
		m_progressDialog = new ProgressDialog(this);
		connect(m_progressDialog, &ProgressDialog::cancelRequested, this,
				[this]()
				{
					m_scanner->cancelScan();
					addLog(QtWarningMsg, QStringLiteral("app"), "Cancel requested");
				});
	}
	return m_progressDialog;
}

// MARK: - Status bar

void MainWindow::updateStatusBar()
{
	// Coalesces bursts: doUpdateStatusBar fires 200 ms after the
	// last call.
	m_statusBarUpdateTimer->start();
}

void MainWindow::doUpdateStatusBar()
{
	const int total = m_proxy->rowCount();
	const int grandTotal = m_model->rowCount();

	const qint64 totalBytes = sumBytesInProxyRange(0, total - 1);

	m_statusFiles->setText(total < grandTotal
							   ? tr("%1 files (filtered from %2)")
									 .arg(Format::count(total), Format::count(grandTotal))
							   : tr("%1 files").arg(Format::count(total)));
	m_statusSize->setText(Format::bytes(totalBytes));
}

// MARK: - Filter tab counts

void MainWindow::updateFilterCounts()
{
	// Count the whole inventory so totals stay stable when other filters change.
	std::array<int, kFilterDefs.size()> counts{};
	for (const MediaFile &file : m_model->allFiles())
	{
		for (size_t i = 0; i < kFilterDefs.size(); ++i)
		{
			if (MediaFilterProxy::matchesMode(kFilterDefs[i].mode, file))
				++counts[i];
		}
	}

	for (size_t i = 0; i < kFilterDefs.size(); ++i)
	{
		const int tabIndex = static_cast<int>(i);
		m_filterTabs->setTabText(
			tabIndex, QStringLiteral("%1 (%2)").arg(QString::fromLatin1(kFilterDefs[i].label)).arg(counts[i]));

		// "All" stays visible no matter what; the others auto-hide when empty.
		const bool isAll = (kFilterDefs[i].mode == MediaFilterProxy::FilterMode::All);
		const bool available = m_precomputesEnabled || kFilterDefs[i].mode != MediaFilterProxy::FilterMode::Precompute;
		const bool visible = available && (isAll || m_showAllFilterTabs || counts[i] > 0);
		m_filterTabs->setTabVisible(tabIndex, visible);
	}
	// Hidden tabs can leave a cached width; refresh the layout's size hint.
	m_filterTabs->updateGeometry();
}

// MARK: - Filter chips

void MainWindow::rebuildFilterChips()
{
	if (!m_chipsBar)
		return;
	auto *layout = m_chipsBar->layout();
	if (!layout)
		return;

	LayoutUtil::clearLayout(layout);

	static const char *kChipStyle = "QPushButton {"
									" background-color: rgba(74, 144, 226, 0.28);"
									" border: none;"
									" border-radius: 11px;"
									" padding: 4px 12px 5px 12px;"
									" font-size: 12px;"
									"}"
									"QPushButton:hover {"
									" background-color: rgba(74, 144, 226, 0.42);"
									"}";

	auto addChip = [this, layout](const QString &text, std::function<void()> onClose)
	{
		auto *chip = new QPushButton(text + "  ✕");
		chip->setCursor(Qt::PointingHandCursor);
		chip->setFlat(true);
		chip->setFocusPolicy(Qt::NoFocus);
		chip->setAttribute(Qt::WA_MacShowFocusRect, false);
		chip->setStyleSheet(kChipStyle);
		QObject::connect(chip, &QPushButton::clicked, this, [cb = std::move(onClose)]()
						 { cb(); });
		layout->addWidget(chip);
	};

	const int tabIdx = m_filterTabs->currentIndex();
	const bool hasPrecomputeSelection = m_precomputesEnabled && m_proxy->precomputeTreeFilter().active;
	// The detail selection already restricts the table to precomputes. When
	// that tab is also selected, one chip represents both restrictions.
	const bool combinedPrecomputeChip = hasPrecomputeSelection && tabIdx >= 0 &&
										tabIdx < static_cast<int>(kFilterDefs.size()) &&
										kFilterDefs[tabIdx].mode == MediaFilterProxy::FilterMode::Precompute;
	if (tabIdx > 0 && !combinedPrecomputeChip)
	{
		QString label = m_filterTabs->tabText(tabIdx);
		const int paren = label.indexOf(QLatin1String(" ("));
		if (paren > 0)
			label.truncate(paren);
		addChip(tr("Type: %1").arg(label), [this]()
				{ m_filterTabs->setCurrentIndex(0); });
	}

	const QString searchText = m_searchField->text();
	if (!searchText.isEmpty())
	{
		QString shown = searchText;
		if (shown.length() > 24)
			shown = shown.left(22) + QStringLiteral("...");
		addChip(tr("Search: \"%1\"").arg(shown), [this]()
				{ m_searchField->clear(); });
	}

	for (auto *item : m_projectList->selectedItems())
	{
		const QString proj = item->data(Qt::UserRole).toString();
		addChip(tr("Project: %1").arg(proj), [item]()
				{ item->setSelected(false); });
	}

	if (m_precomputesEnabled)
	{
		// Keep complete checked paths together: splitting category/name chips
		// would change their OR semantics. Volume is an independent filter.
		const PrecomputeFilter filter = m_proxy->precomputeTreeFilter();
		QString selectionLabel;
		if (filter.active)
		{
			if (filter.paths.isEmpty())
				selectionLabel = tr("Precompute: none");
			else if (filter.paths.size() == 1)
			{
				const auto &path = filter.paths.first();
				QStringList names;
				for (const auto &name : {path.precomputeCategory, path.effectCategory, path.effect})
					if (!name.isEmpty())
						names.append(name);
				selectionLabel = names.isEmpty() ? tr("Precompute: all")
												 : tr("Precompute: %1").arg(names.join(QStringLiteral(" / ")));
			}
			else
				selectionLabel = tr("%1 precompute selections").arg(filter.paths.size());
		}
		if (!selectionLabel.isEmpty())
			addChip(selectionLabel, [this, combinedPrecomputeChip]()
					{
				applyFilterPreservingSelection([this, combinedPrecomputeChip]() {
					m_proxy->setPrecomputeTreeFilter({});
					if (combinedPrecomputeChip)
					{
						const QSignalBlocker blocker(m_filterTabs);
						m_filterTabs->setCurrentIndex(0);
						m_proxy->setFilterMode(MediaFilterProxy::FilterMode::All);
					}
				});
				rebuildFilterChips();
				updateStatusBar(); });

		const QString volumePath = m_proxy->effectVolumeFilter();
		if (!volumePath.isEmpty())
		{
			QHash<QString, QString> volumeNames;
			for (const auto &file : m_model->allFiles())
				if (!file.volumePath.isEmpty() && !volumeNames.contains(file.volumePath))
					volumeNames.insert(file.volumePath, file.volumeName.isEmpty() ? file.volumePath : file.volumeName);
			QString volumeLabel = volumeNames.value(volumePath, volumePath);
			if (volumeLabel != volumePath)
				for (auto it = volumeNames.cbegin(); it != volumeNames.cend(); ++it)
					if (it.key() != volumePath && it.value() == volumeLabel)
					{
						volumeLabel = tr("%1 (%2)").arg(volumeLabel, volumePath);
						break;
					}
			addChip(tr("Precompute volume: %1").arg(volumeLabel), [this]()
					{
				applyFilterPreservingSelection([this]() {
					m_proxy->setEffectVolumeFilter({});
				});
				rebuildFilterChips();
				updateStatusBar(); });
		}
	}

	// One chip per unique bin referenced in the chain; mirrors the
	// Project: <name> pattern above. Dismissing any chip clears the
	// entire chain. Loaded bins stay loaded, so re-filtering is trivial.
	if (m_binFilterActive)
	{
		auto clearBinFilter = [this]()
		{
			if (m_binFilterDialog)
			{
				// Dialog drives the cleanup: clearChain re-emits with
				// an empty expression, which fans out through the connected
				// signal handlers to update the proxy, cache, log, and
				// chip strip uniformly.
				m_binFilterDialog->clearChain();
				return;
			}
			// Defensive: chips shouldn't exist without the dialog (it
			// owns the chain), but reset locally just in case.
			m_binFilterActive = false;
			m_binFilterBinNames.clear();
			applyFilterPreservingSelection([this]()
										   { m_proxy->setBinFilter({}); });
			rebuildFilterChips();
			updateStatusBar();
		};
		for (const QString &name : m_binFilterBinNames)
			addChip(tr("Bin: %1").arg(name), clearBinFilter);
	}

	m_chipsBar->setVisible(layout->count() > 0);
}

// MARK: - Filter reset

void MainWindow::resetFiltersForNewScan()
{
	// A scan replaces the dataset, so every active filter describes the old
	// data. Reset them all to a clean baseline; mirrors the persistent-
	// selection clear in onScanFinished. The widgets are the source of truth
	// for the chip strip, so reset those too — not just the proxy predicates.

	// Type tab back to "All". Block the signal and push the mode to the proxy
	// directly below so the already-on-All case still resets.
	{
		const QSignalBlocker block(m_filterTabs);
		m_filterTabs->setCurrentIndex(0);
	}

	// Search box. Block the widget so its debounced handler doesn't fire a
	// stale-text query a beat later; the proxy is reset directly below.
	{
		const QSignalBlocker block(m_searchField);
		m_searchField->clear();
	}
	{
		const QSignalBlocker block(m_projectList);
		m_projectList->clearSelection();
	}

	// Bin filter: when the dialog exists, clearChain() tears down its internal
	// chain and the chain-list UI. The cached chip state is reset here for the
	// no-dialog / empty-chain paths clearChain() skips.
	if (m_binFilterDialog)
		m_binFilterDialog->clearChain();
	m_binFilterActive = false;
	m_binFilterBinNames.clear();

	// Reset the predicates as well as their widgets, regardless of which
	// paths above already touched the proxy.
	m_proxy->setFilterMode(MediaFilterProxy::FilterMode::All);
	m_proxy->setSearchText({});
	m_proxy->setProjectFilter({});
	m_proxy->setBinFilter({});
	m_proxy->setPrecomputeTreeFilter({});
	m_proxy->setEffectVolumeFilter({});

	rebuildFilterChips();
}
