#include "mainwindow.h"

#include "aboutdialog.h"
#include "applog.h"
#include "conventions.h"
#include "binfilterdialog.h"
#include "crashcollector.h"
#include "debugslowdown.h"
#include "enumutil.h"
#include "formatutil.h"
#include "icons.h"
#include "layoututil.h"
#include "managemediadialog.h"
#include "mediacsv.h"
#include "mediamanagerverify.h"
#include "opjournal.h"
#include "progressdialog.h"
#include "rebalancedialog.h"
#include "rebalancer.h"
#include "revealinfinder.h"
#include "version.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
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
#include <QStandardItemModel>
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
	/// The fixed-pitch face the table and console share. Lived in theme.h
	/// until 2026-08-31; folded in here as its only consumer (a real theme
	/// can move it back out the day one exists).
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
	const char *logPfx(QtMsgType level)
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
			.arg(time, QLatin1String(logPfx(level)), module, message);
	}

	// MARK: - Filter tab definitions

	struct FilterDef
	{
		MediaFilterProxy::FilterMode mode;
		const char *label;
		const char *tooltip = nullptr; ///< Optional hover explanation for loaded terms.
	};

	// The middle three are the local-database ladder, most-known to
	// least-known: No Project (in the MDB, project gone) > No Reference
	// (verified in neither database) > No Database (couldn't check).
	constexpr std::array<FilterDef, 10> kFilterDefs{{
		// Technical Avid/domain vocabulary — invariant, never translated.
		{MediaFilterProxy::FilterMode::All, "All"},
		{MediaFilterProxy::FilterMode::Video, "Video"},
		{MediaFilterProxy::FilterMode::Audio, "Audio"},
		{MediaFilterProxy::FilterMode::Precompute, "Precomputes",
		 "Rendered effects — media Avid generated from a sequence, as its own\n"
		 "usage code says. Re-rendering recreates them; nothing else can be\n"
		 "relinked to them."},
		// These three take their labels and tooltips from MediaFile at build
		// time (statusTabText below), so tab, table tooltip and CSV agree.
		{MediaFilterProxy::FilterMode::NoProject, "No Project"},
		{MediaFilterProxy::FilterMode::NoReference, "No Reference"},
		{MediaFilterProxy::FilterMode::NoDatabase, "No Database"},
		{MediaFilterProxy::FilterMode::InvalidUmid, "Invalid UMID",
		 "The file's MOB ID is all zeros — Avid never assigned a real identity,\n"
		 "so the media can't be tracked or relinked reliably."},
		{MediaFilterProxy::FilterMode::NonPortable, "Non-Portable"},
		{MediaFilterProxy::FilterMode::Quarantined, "Quarantined"},
	}};

} // namespace

// MARK: - MainWindow construction

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent),
	  // Order needs to match mainwindow.h, otherwise the compiler
	  // gets cranky. Pass `this` as parent and Qt handles cleanup.
	  m_volumeManager(new VolumeManager(this)),
	  m_scanner(new MediaScanner(this)),
	  m_fileOps(new OpManager(this)),
	  m_model(new MediaTableModel(this)),
	  m_proxy(new MediaFilterProxy(this))
{
	m_proxy->setSourceModel(m_model);
	m_proxy->setSortRole(Qt::UserRole);

	setupUi();
	setupMenus();
	setupConnections();

	setWindowTitle("MediaMuster");
	resize(1200, 750);

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

	onDetectVolumes();
	m_volumeManager->startMonitoring();

#ifdef Q_OS_MAC
	if (!VolumeManager::hasFullDiskAccess())
	{
		addLog(QtWarningMsg, QStringLiteral("app"),
			   "Full Disk Access not granted. Some locations may be "
			   "inaccessible.");
		addLog(QtInfoMsg, QStringLiteral("app"),
			   "Go to System Preferences > Privacy & Security > Full Disk "
			   "Access, to grant permission.");
	}
	else
	{
		addLog(QtInfoMsg, QStringLiteral("app"), "Full Disk Access: granted!");
	}
#endif // Q_OS_MAC

	// Deferred to the first event-loop tick so the window shows first. One
	// sequence, not two racing timers: the crash-report notice (synchronous
	// box) goes first, THEN the rollback starts on a pool thread and reports
	// — otherwise its notice and the resume offer would land on top of the
	// still-open crash box.
	QTimer::singleShot(0, this,
					   [this]
					   {
						   collectCrashReports();
						   runStartupRecovery();
					   });
}

MainWindow::~MainWindow() = default;

// MARK: - Crash recovery

void MainWindow::runStartupRecovery()
{
	// OpRescue::run only touches the filesystem, so it's safe on a pool
	// thread; the watcher cleans itself up once it fires. (A lambda, not
	// a function pointer: default arguments don't travel through
	// QtConcurrent's pointer overloads.)
	auto *watcher = new QFutureWatcher<OpRescue::Summary>(this);
	connect(watcher, &QFutureWatcher<OpRescue::Summary>::finished, this,
			[this, watcher]
			{
				const OpRescue::Summary summary = watcher->result();
				watcher->deleteLater();
				onRecoveryDone(summary);
			});
	watcher->setFuture(QtConcurrent::run([] { return OpRescue::run(); }));
}

void MainWindow::onRecoveryDone(const OpRescue::Summary &summary)
{
	// Tidying up after a crash is housekeeping, not news: the partial file
	// the interrupted run wrote is MediaMuster's own mess, and removing it
	// restores the state the user expects. So no dialog for it — but every
	// note goes to the console and the log, because the one thing that must
	// never be invisible is a file MediaMuster took off the user's disk.
	for (const QString &note : summary.notes)
		addLog(summary.hadTrouble() ? QtWarningMsg : QtInfoMsg, QStringLiteral("app"), note);

	// A dialog only when something needs the user: a file that couldn't be
	// put back or removed, or an original still sitting under a temp name.
	if (summary.hadTrouble())
		QMessageBox::warning(this, tr("Some files need a look"), summary.message());

	// The launch sweep already worked this out on the pool thread; no need
	// to re-read the journal folder here.
	m_resumable = summary.resumable;
	updateResumeAction();

	// Anything left to finish? Ask now; the File menu item stays live for
	// later if the answer is "Resume Later".
	if (!m_resumable.isEmpty())
		offerResume();
}

void MainWindow::collectCrashReports()
{
	const QString logsDir = QFileInfo(AppLog::logPath()).absolutePath();
	const QStringList collected =
		CrashCollector::collect(CrashCollector::systemReportsDir(), logsDir);
	if (collected.isEmpty())
		return;

	addLog(QtWarningMsg, QStringLiteral("app"),
		   QStringLiteral("MediaMuster quit unexpectedly. — %1 report(s) saved. "
						  "Go to Help > Reveal Logs to send them.")
			   .arg(collected.size()));

	QMessageBox::information(
		this, tr("MediaMuster closed unexpectedly"),
		tr("MediaMuster quit unexpectedly. A crash report has been saved with "
		   "your logs.\n\nGo to Help > Reveal Logs to send them to the developer."));
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
	// The project/database-status tabs read their words from MediaFile — the
	// same sentences the table tooltip and the CSV use — so they can't drift.
	const auto statusTabText = [](MediaFilterProxy::FilterMode mode) -> QString
	{
		using DbStatus = MediaFile::DbStatus;
		switch (mode)
		{
		case MediaFilterProxy::FilterMode::NoProject:
			return MediaFile::noProjectWhy();
		case MediaFilterProxy::FilterMode::NoReference:
			return MediaFile::dbStatusText(DbStatus::NoReference).why;
		case MediaFilterProxy::FilterMode::NoDatabase:
			return MediaFile::dbStatusText(DbStatus::NoDatabase).why + QStringLiteral("\n\n") +
				   MediaFile::dbStatusText(DbStatus::DbUnreadable).why;
		default:
			return {};
		}
	};
	for (const auto &fd : kFilterDefs)
	{
		const int idx = m_filterTabs->addTab(QString::fromLatin1(fd.label));
		const QString statusTip = statusTabText(fd.mode);
		if (!statusTip.isEmpty())
			m_filterTabs->setTabToolTip(idx, statusTip);
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

	m_btnFileOps = new QPushButton(tr("Manage Media..."));
	m_btnBinFilter = new QPushButton(tr("Filter by Bin..."));
	m_btnExport = new QPushButton(tr("Export CSV..."));
	m_btnRebalance = new QPushButton(tr("Rebalance..."));
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
				scrollTo(now); // vertical for arrows/pg up/pg down
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
	m_tableView->horizontalHeader()->setStretchLastSection(true);
	m_tableView->horizontalHeader()->setSectionsMovable(true);
	m_tableView->horizontalHeader()->setHighlightSections(false);
	m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_tableView->setFont(monoFont());

	// Starting widths. autoFitColumns runs once after the
	// first scan to size them to actual content.
	using Col = MediaTableModel::Column;
	auto setW = [this](Col c, int w)
	{ m_tableView->setColumnWidth(Enum::to_underlying(c), w); };
	setW(Col::ClipName, 150);
	setW(Col::FileName, 150);
	setW(Col::Project, 100);
	setW(Col::OriginalBin, 100);
	setW(Col::Kind, 50);
	setW(Col::Codec, 100);
	setW(Col::Resolution, 100);
	setW(Col::Fps, 50);
	setW(Col::Duration, 100);
	setW(Col::SizeMB, 100);
	setW(Col::Location, 320);
	setW(Col::Created, 100);
	setW(Col::Type, 50);
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

	auto *addFolderAct = fileMenu->addAction(tr("Add &Folder or Volume"));
	addFolderAct->setShortcut(QKeySequence("Ctrl+O"));
	connect(addFolderAct, &QAction::triggered, this,
			[this]()
			{
				QString dir = QFileDialog::getExistingDirectory(this, tr("Add Volume or Folder"));
				if (!dir.isEmpty())
					addVolumePath(dir);
			});

	fileMenu->addSeparator();

	auto *scanSelAct = fileMenu->addAction(tr("Scan &Selected"));
	connect(scanSelAct, &QAction::triggered, this, &MainWindow::onScanClicked);

	auto *scanAllAct = fileMenu->addAction(tr("Scan &All"));
	scanAllAct->setShortcut(QKeySequence("Ctrl+Shift+A"));
	connect(scanAllAct, &QAction::triggered, this, &MainWindow::onScanAllClicked);

	auto *refreshAct = fileMenu->addAction(tr("Refresh &Volumes"));
	refreshAct->setShortcut(QKeySequence("Ctrl+R"));
	connect(refreshAct, &QAction::triggered, this, &MainWindow::onDetectVolumes);

	fileMenu->addSeparator();

	auto *revealAct = fileMenu->addAction(tr("Reveal in Finder"));
	revealAct->setShortcut(QKeySequence("Ctrl+Shift+R"));
	connect(revealAct, &QAction::triggered, this, &MainWindow::onRevealInFinder);

	fileMenu->addSeparator();

	auto *exportAct = fileMenu->addAction(tr("&Export CSV..."));
	exportAct->setShortcut(QKeySequence("Ctrl+E"));
	connect(exportAct, &QAction::triggered, this, &MainWindow::onExportCsv);

	fileMenu->addSeparator();

	// Greyed out unless an interrupted Copy/Move/Delete is waiting to be
	// finished (see refreshResumable); offered automatically at launch too.
	m_resumeAct = fileMenu->addAction(tr("Resume Interrupted Operation..."));
	m_resumeAct->setEnabled(false);
	connect(m_resumeAct, &QAction::triggered, this, &MainWindow::offerResume);

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

	// Single-level undo of the last completed file operation. Label and
	// enabled state follow the newest undoable journal (refreshResumable);
	// the engine re-qualifies at run time, so a stale click refuses
	// cleanly rather than acting on an old belief.
	m_undoAct = editMenu->addAction(tr("&Undo"));
	m_undoAct->setShortcut(QKeySequence::Undo);
	m_undoAct->setEnabled(false);
	connect(m_undoAct, &QAction::triggered, this, &MainWindow::onUndoLastOperation);
	editMenu->addSeparator();

	auto *findAct = editMenu->addAction(tr("&Find"));
	findAct->setShortcut(QKeySequence::Find);
	connect(findAct, &QAction::triggered, m_searchField, qOverload<>(&QWidget::setFocus));

	auto *selectAllAct = editMenu->addAction(tr("Select &All"));
	selectAllAct->setShortcut(QKeySequence::SelectAll);
	connect(selectAllAct, &QAction::triggered, m_tableView, &QTableView::selectAll);

	auto *relAct = editMenu->addAction(tr("Select &Relatives"));
	relAct->setShortcut(QKeySequence("Ctrl+Shift+L"));
	connect(relAct, &QAction::triggered, this, &MainWindow::onSelectRelatives);

	auto *invAct = editMenu->addAction(tr("Select &Inverse"));
	invAct->setShortcut(QKeySequence("Ctrl+Shift+I"));
	connect(invAct, &QAction::triggered, this, &MainWindow::onInvertSelection);
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

	auto *summaryAct = specialMenu->addAction(tr("&Project Summary"));
	connect(summaryAct, &QAction::triggered, this, &MainWindow::onProjectSummary);

	specialMenu->addSeparator();
	auto *binFilterAct = specialMenu->addAction(tr("Filter by &Bin..."));
	binFilterAct->setShortcut(QKeySequence("Ctrl+Shift+B"));
	connect(binFilterAct, &QAction::triggered, this, &MainWindow::onFilterByBins);

	specialMenu->addSeparator();
	auto *rebalanceAct = specialMenu->addAction(tr("&Rebalance..."));
	connect(rebalanceAct, &QAction::triggered, this, &MainWindow::onRebalance);

#ifdef Q_OS_MAC
	specialMenu->addSeparator();
	auto *permissionsAct = specialMenu->addAction(tr("Check &Permissions..."));
	connect(permissionsAct, &QAction::triggered, this, &MainWindow::onCheckPermissions);
#endif
}

// MARK: Debug menu

void MainWindow::buildDebugMenu()
{
	auto *debugMenu = menuBar()->addMenu(tr("&Debug"));

	auto *slowAct = debugMenu->addAction(tr("&Slow mode"));
	slowAct->setCheckable(true);
	slowAct->setChecked(false);
	connect(slowAct, &QAction::triggered, this,
			[this](bool on)
			{
				DebugSlowdown::setEnabled(on);
				addLog(QtInfoMsg, QStringLiteral("app"), on ? "Slow Mode enabled (1/50th speed)" : "Slow Mode disabled");
			});

	auto *verifyAct = debugMenu->addAction(tr("&Verification checks"));
	verifyAct->setCheckable(true);
	verifyAct->setChecked(MediaManagerVerify::enabled());
	connect(verifyAct, &QAction::triggered, this,
			[this](bool on)
			{
				MediaManagerVerify::setEnabled(on);
				addLog(QtInfoMsg, QStringLiteral("app"),
					   on ? "Verification checks enabled" : "Verification checks disabled");
			});

	auto *rawHexAct = debugMenu->addAction(tr("&Raw hex"));
	rawHexAct->setCheckable(true);
	rawHexAct->setChecked(false);
	connect(rawHexAct, &QAction::triggered, this,
			[this](bool on)
			{
				m_model->setShowRawCodecHex(on);
				addLog(QtInfoMsg, QStringLiteral("app"), on ? "Codec as Raw Hex enabled" : "Codec as Raw Hex disabled");
			});

	// Database-first is the default: a row the folder's msmFMID.pmr +
	// msmMMOB.mdb fully describe never has its header read. This forces the
	// pre-database-first behaviour — every .mxf header read — which is also
	// how the two are compared (scan, export, toggle, rescan, export, diff).
	auto *forceHeaderAct = debugMenu->addAction(tr("&Force header scan"));
	forceHeaderAct->setCheckable(true);
	forceHeaderAct->setChecked(m_forceHeaderScan);
	connect(forceHeaderAct, &QAction::triggered, this,
			[this](bool on)
			{
				m_forceHeaderScan = on;
				addLog(QtInfoMsg, QStringLiteral("app"),
					   on ? "Force header scan enabled — every MXF header is read on the next scan"
						  : "Force header scan disabled — the databases describe rows on the next scan");
			});

	// Whatever style main.cpp installed at startup is the one to restore.
	// Read it here, before the toggle below can change it — main.cpp stays
	// the single authority on the platform's native style.
	const QString nativeStyleName = QApplication::style()->name();
	auto *fusionAct = debugMenu->addAction(tr("&Fusion"));
	fusionAct->setCheckable(true);
	fusionAct->setChecked(false);
	connect(fusionAct, &QAction::triggered, this,
			[this, nativeStyleName](bool on)
			{
				const QString target = on ? QStringLiteral("fusion") : nativeStyleName;
				QApplication::setStyle(QStyleFactory::create(target));
				addLog(QtInfoMsg, QStringLiteral("app"), QStringLiteral("Style: %1").arg(target));
			});

	// Rebalance Demos: synthetic plans for visual QA. Each opens
	// the dialog in demo mode against a fabricated RebalancePlan;
	// clicking Rebalance runs a simulated progress sweep, not real
	// disk moves.
	auto *demosMenu = debugMenu->addMenu(tr("Rebalance demos"));
	const auto addDemo = [this, demosMenu](const QString &label,
										   RebalanceDialog::DemoScenario scenario)
	{
		auto *act = demosMenu->addAction(label);
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

	// The diagnostic log always runs; this just surfaces it so the user can
	// send it in, even though it lives in hidden ~/Library.
	auto *revealLogAct = helpMenu->addAction(tr("Reveal Logs"));
	connect(revealLogAct, &QAction::triggered, this,
			[this]
			{
				RevealInFinder::reveal(AppLog::logPath(), [this](QtMsgType level, const QString &msg)
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

	connect(
		m_fileOps, &OpManager::operationProgress, this,
		[this](const QString &name, int cur, int total, double pct)
		{
			// The engine reports per-byte progress within the current
			// file; fold it into the bar so one 40 GB MXF doesn't look
			// like a hang (the counter label keeps counting files).
			auto *dlg = progressDialog();
			dlg->setItemProgress(cur, total, pct);
			dlg->setDetail(name);
		},
		Qt::QueuedConnection);
	connect(
		m_fileOps, &OpManager::operationLog, this,
		[this](QtMsgType level, const QString &message)
		{ addLog(level, QStringLiteral("ops"), message); },
		Qt::QueuedConnection);
	connect(
		m_fileOps, &OpManager::operationItemDone, this,
		[this](const QString &name, const QString &filePath, bool ok, const QString &err,
			   bool skipped)
		{
			// Three outcomes, three lines: a skip is neither a failure nor
			// "done" — the engine's message already reads "Skipped (...)".
			addLog(ok ? QtInfoMsg : QtCriticalMsg, QStringLiteral("ops"),
				   !ok		 ? name + " FAILED: " + err
				   : skipped ? name + ": " + err
							 : name + " done");

			// Track successful paths so we can drop those rows once
			// the job finishes. Skipped items left the source alone,
			// so removing their rows would lie about table state.
			if (ok && !skipped && m_removeAfterOp)
				m_successfulOpPaths.insert(filePath);
		},
		Qt::QueuedConnection);
	connect(
		m_fileOps, &OpManager::operationFinished, this,
		[this](int /*succeeded*/, int /*failed*/)
		{
			// The engine has already logged its own summary line via
			// operationLog; nothing to narrate here.
			setBusy(false);

			// After Move/Delete, drop the successful files so the table
			// reflects reality without a re-scan. begin/endRemoveRows
			// per contiguous range preserves scroll and selection.
			if (m_removeAfterOp && !m_successfulOpPaths.isEmpty())
			{
				// Count what actually left the table: a resumed Move/Delete
				// works from the journal, and its files may not be rows here.
				const int rowsBefore = m_model->rowCount();
				m_model->removeFilesByPath(m_successfulOpPaths);
				const int removedCount = rowsBefore - m_model->rowCount();
				// Drop the removed paths from the persistent selection
				// record so the next filter change doesn't try to
				// re-select rows no longer on disk.
				m_persistentSelectedPaths.subtract(m_successfulOpPaths);
				updateFilterCounts();
				updateStatusBar();
				addLog(QtInfoMsg, QStringLiteral("ops"), QStringLiteral("Removed %1 files from table").arg(removedCount));
			}
			m_removeAfterOp = false;
			m_successfulOpPaths.clear();

			// The concluded run's journal stays behind as the undo
			// candidate; re-read the folder (off-thread) so the Resume
			// item reflects the new state.
			refreshResumable();
		},
		Qt::QueuedConnection);

	// Network volumes have no OS recycle bin, so deletes there
	// land in a per-volume `_MediaMuster_Trash` folder.
	connect(m_fileOps, &OpManager::mediaMusterTrashUsed, this,
			&MainWindow::showMediaMusterTrashDialog, Qt::QueuedConnection);

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
	connect(m_btnFileOps, &QPushButton::clicked, this, &MainWindow::onFileOperations);
	connect(m_btnBinFilter, &QPushButton::clicked, this, &MainWindow::onFilterByBins);
	connect(m_btnExport, &QPushButton::clicked, this, &MainWindow::onExportCsv);
	connect(m_btnRebalance, &QPushButton::clicked, this, &MainWindow::onRebalance);
	connect(m_scanButton, &QPushButton::clicked, this, &MainWindow::onScanClicked);
	connect(m_scanAllButton, &QPushButton::clicked, this, &MainWindow::onScanAllClicked);

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
		QMessageBox::information(this, tr("Permissions"),
								 tr("Full Disk Access is <b>granted</b>.<br><br>"
									"I can muster all volumes and folders on this Mac!"));
	}
	else
	{
		QMessageBox box(this);
		box.setIcon(QMessageBox::Warning);
		box.setWindowTitle(tr("Permissions"));
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

// MARK: - Bin filter

void MainWindow::onFilterByBins()
{
	// Lazy construction; the dialog stays parented to the main window
	// so chain state persists across show/hide.
	if (!m_binFilterDialog)
	{
		m_binFilterDialog = new BinFilterDialog(this);
		// Apply the chain change through the selection-preserving
		// helper so the user's selection survives the filter shuffle.
		connect(m_binFilterDialog, &BinFilterDialog::filterChainChanged, this,
				[this](bool isActive, const QSet<QString> &acceptedMobs, const QStringList &)
				{
					applyFilterPreservingSelection(
						[this, isActive, &acceptedMobs]()
						{ m_proxy->setBinFilterMobs(isActive, acceptedMobs); });
				});
		connect(
			m_binFilterDialog, &BinFilterDialog::filterChainChanged, this,
			[this](bool isActive, const QSet<QString> &acceptedMobs, const QStringList &binNames)
			{
				m_binFilterActive = isActive;
				m_binFilterBinNames = binNames;
				rebuildFilterChips();

				if (!isActive)
				{
					addLog(QtInfoMsg, QStringLiteral("binfilter"), "Bin filter cleared");
					updateStatusBar();
					return;
				}
				addLog(QtInfoMsg, QStringLiteral("binfilter"),
					   QStringLiteral("Bin filter active — %1 MOBs accepted").arg(acceptedMobs.size()));

				// Log a bin sample + file sample side-by-side to spot
				// namespace mismatches (case, dotted vs undotted hex).
				if (!acceptedMobs.isEmpty() && !m_model->allFiles().isEmpty())
				{
					auto binSample = *acceptedMobs.constBegin();
					QString fileSample = m_model->allFiles().first().mobId;
					QString compSample = m_model->allFiles().first().masterMobId;
					addLog(QtDebugMsg, QStringLiteral("binfilter"), QStringLiteral("Bin MOB sample   : %1").arg(binSample));
					addLog(QtDebugMsg, QStringLiteral("binfilter"), QStringLiteral("File mobId sample: %1").arg(fileSample));
					addLog(QtDebugMsg, QStringLiteral("binfilter"), QStringLiteral("File masterMobId   : %1").arg(compSample));
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
	if (m_model->allFiles().isEmpty())
	{
		QMessageBox::information(this, tr("Rebalance"),
								 tr("Please scan a volume first. I need to know "
									"where things are "
									"before I can rebalance them."));
		return;
	}

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
		if (mf.filePath.isEmpty())
			continue;
		const QString folderDir = QFileInfo(mf.filePath).absolutePath();
		const QString mxfRoot = QFileInfo(folderDir).absolutePath();

		// Grandparent must be the MXF folder — any case, matching the
		// scanner's rule, so a share spelled 'mxf' that scans fine can
		// also rebalance. OMF roots stay excluded (scan-only; see
		// Conventions::isOmfRootName).
		if (!Conventions::isMxfRootName(QFileInfo(mxfRoot).fileName()))
			continue;

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

	addLog(QtInfoMsg, QStringLiteral("rebalance"),
		   QStringLiteral("Opening rebalance dialog (%1 volume(s), default '%2')")
			   .arg(mxfRootsByLabel.size())
			   .arg(initialLabel));

	RebalanceDialog dlg(mxfRootsByLabel, filesByMxfRoot, initialLabel, this);
	connect(&dlg, &RebalanceDialog::logMessage, this, [this](QtMsgType level, const QString &msg)
			{ addLog(level, QStringLiteral("rebalance"), msg); });
	dlg.exec();

	// Re-scan if didRebalance; a cancelled run can still have
	// moved files.
	if (!dlg.didRebalance())
		return;

	const QString volumePath = volumePathByLabel.value(dlg.rebalancedLabel());
	if (volumePath.isEmpty())
	{
		addLog(QtWarningMsg, QStringLiteral("rebalance"),
			   "Couldn't determine volume path for re-scan; please scan "
			   "manually");
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
	auto *item = new QListWidgetItem(Icons::forVolumeType(v.volumeType, v.path), displayName);
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

void MainWindow::onDetectVolumes()
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
		   QStringLiteral("Found %1 volumes (%2 with Avid MediaFiles)").arg(volumes.size()).arg(ac));
}

// MARK: - Scan controls

void MainWindow::onScanClicked()
{
	// No in-scan cancel branch: a running scan raises the modal progress sheet,
	// whose Cancel button is the stop control, so neither this button nor its
	// menu action is reachable mid-scan. (MediaScanner::startScan also self-
	// guards against a double start.) This handler therefore only begins a scan.
	QStringList paths;
	for (auto *item : m_volumeList->selectedItems())
		paths << item->data(Qt::UserRole).toString();

	if (paths.isEmpty())
	{
		QMessageBox::information(this, tr("No Volumes Selected"),
								 tr("Select at least one volume, or use "
									"\"Scan All Volumes\"."));
		return;
	}
	startScanWithPaths(paths);
}

void MainWindow::onScanAllClicked()
{
	// See onScanClicked: cancel is via the modal progress sheet, so there is no
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
	MediaScanner::Options opts;
	opts.volumePaths = paths;
	opts.forceHeaderScan = m_forceHeaderScan;

	setBusy(true);
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
		// Same tee as addLog(): every console line lands in the log file.
		AppLog::appendConsoleLine(batch[i].level, batch[i].module, batch[i].message);
	}
	m_console->appendPlainText(combined);
}

void MainWindow::onScanFinished(const QVector<MediaFile> &results)
{
	m_model->setMediaFiles(results);
	m_persistentSelectedPaths.clear();

	qint64 elapsed = m_scanTimer.elapsed();
	QString timeStr = tr("Scan: %1 ms").arg(elapsed);
	m_statusScanTime->setText(timeStr);

	m_projectList->clear();
	// Group by the displayed project name: real projects plus, at most, the
	// one "No project" row. Database status is NOT a project and is never
	// listed here — the filter tabs are where it lives.
	struct ProjectStat
	{
		int count = 0;
		qint64 bytes = 0;
		bool hasProject = true;
	};
	QHash<QString, ProjectStat> projectStats;
	for (const auto &f : results)
	{
		auto &stat = projectStats[f.projectDisplay()];
		stat.count++;
		stat.bytes += f.sizeBytes;
		stat.hasProject = !f.hasNoProject();
	}

	QStringList sorted = projectStats.keys();
	sorted.sort();
	for (const QString &p : sorted)
	{
		const ProjectStat &stat = projectStats[p];
		auto *item = new QListWidgetItem(Icons::forProject(stat.hasProject), p);
		item->setData(Qt::UserRole, p);
		QString tip = tr("%1 files, %2").arg(stat.count).arg(Format::bytes(stat.bytes));
		if (!stat.hasProject)
			tip += QStringLiteral("\n\n") + MediaFile::noProjectWhy();
		item->setToolTip(tip);
		m_projectList->addItem(item);
	}

	// Fresh dataset: clear any filters left over from the previous scan
	// before tallying, so the counts and table reflect the full results.
	resetFiltersForNewScan();

	updateFilterCounts();
	updateStatusBar();
	autoFitColumns();

	setBusy(false);
}

// MARK: - Filter / search slots

void MainWindow::onFilterChanged(int index)
{
	// kFilterDefs is the shared source of truth; the same array that
	// fed the tab labels in setupUi.
	if (index >= 0 && index < static_cast<int>(kFilterDefs.size()))
	{
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

	m_btnFileOps->setEnabled(hasSelection);
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
	return dispatchRequest(std::move(req));
}

// The write-ahead journal is the only thing that can put files back after
// a crash. If it can't be written (disk full, permissions), running
// anyway is the user's call to make — not a console line to miss. Cancel
// stays default: a stray Return must not waive the net. Shared by every
// dispatch INCLUDING undo — an undo without its own journal would itself
// be unrecoverable if interrupted.
bool MainWindow::confirmCrashProtection()
{
	if (OpJournal::standardDirWritable())
		return true;
	QMessageBox confirm(this);
	confirm.setIcon(QMessageBox::Warning);
	confirm.setWindowTitle(tr("No crash protection"));
	confirm.setText(tr("MediaMuster can't write its safety journal (the record used to "
					   "undo an interrupted operation). Check free space and permissions "
					   "on your system disk.\n\nIf this operation is interrupted, files "
					   "cannot be put back automatically. Continue anyway?"));
	auto *goBtn = confirm.addButton(tr("Continue Anyway"), QMessageBox::DestructiveRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.setDefaultButton(QMessageBox::Cancel);
	confirm.exec();
	return confirm.clickedButton() == goBtn;
}

void MainWindow::updateUndoAction()
{
	if (!m_undoAct)
		return;
	// Scan button doubles as the busy flag, exactly as for Resume.
	m_undoAct->setEnabled(!m_undoCandidate.path.isEmpty() && m_scanButton->isEnabled());
	m_undoAct->setText(m_undoCandidate.label.isEmpty() ? tr("&Undo") : m_undoCandidate.label);
}

void MainWindow::onUndoLastOperation()
{
	if (m_undoCandidate.path.isEmpty() || !m_scanButton->isEnabled())
		return;

	// Confirm intent first, naming exactly what will be undone and, per
	// kind, what that means on disk. Deliberately a bog-standard message
	// box — platform defaults, affirmative button as the default action
	// (Marty's call: the dialog itself is the guard; no button-role
	// gymnastics).
	{
		QString plainLabel = m_undoCandidate.label;
		plainLabel.remove(QLatin1Char('&'));
		if (plainLabel.isEmpty())
			plainLabel = tr("Undo last operation");

		QMessageBox confirm(this);
		confirm.setIcon(QMessageBox::Question);
		confirm.setWindowTitle(tr("Undo"));
		confirm.setText(tr("%1?").arg(plainLabel));
		confirm.setInformativeText(
			tr("Files will be put back where they were. Anything removed goes to the Trash."));
		auto *goBtn = confirm.addButton(plainLabel, QMessageBox::AcceptRole);
		confirm.addButton(QMessageBox::Cancel);
		confirm.exec();
		if (confirm.clickedButton() != goBtn)
			return;
	}

	if (!confirmCrashProtection())
		return;

	// Undo restores files ON DISK; the table can't follow (rows for
	// moved/deleted files were pruned when the run finished). Say so
	// once, up front, instead of leaving a silent mismatch.
	addLog(QtInfoMsg, QStringLiteral("ops"),
		   tr("Undoing the last operation. Rescan afterwards to refresh the table."));

	m_removeAfterOp = false;
	m_successfulOpPaths.clear();
	m_fileOps->executeUndo(m_undoCandidate.path);
	m_undoCandidate = {}; // spent (or re-offered by the refresh after the run)
	setBusy(true);
	progressDialog()->begin();
}

bool MainWindow::dispatchRequest(OpRequest request)
{
	if (request.items.isEmpty())
		return false;

	if (!confirmCrashProtection())
		return false;

	// Move/Delete remove the affected rows on completion. Copy leaves
	// source rows in place; a Rename changes paths without removing.
	m_removeAfterOp = (request.kind == OpKind::Move || request.kind == OpKind::Delete);
	m_successfulOpPaths.clear();

	// The engine narrates the run (start line, per-file lines, summary)
	// through operationLog; the window only dispatches.
	m_fileOps->execute(std::move(request));

	// Lock the UI and raise the modal progress sheet now, at dispatch —
	// not lazily on the first progress signal. That closes the window
	// where a second op could be launched (and cancel this one mid-run)
	// before the sheet goes up. Mirrors how startScanWithPaths does it.
	setBusy(true);
	progressDialog()->begin();
	return true;
}

// MARK: - Resume an interrupted operation

void MainWindow::updateResumeAction()
{
	// The Scan button is disabled exactly while an operation runs (setBusy),
	// so it doubles as the busy flag: no resuming on top of a live run.
	if (m_resumeAct)
		m_resumeAct->setEnabled(!m_resumable.isEmpty() && m_scanButton->isEnabled());
	// The Undo item rides the same busy flag and the same refresh moments.
	updateUndoAction();
}

void MainWindow::refreshResumable()
{
	// Off the GUI thread: reading the journal folder means stat()-ing the
	// media paths in every journal, and a dropped network mount would
	// freeze the window (the launch sweep runs on a pool thread for the
	// same reason). The menu item keeps its last state until this lands.
	auto *watcher = new QFutureWatcher<QVector<OpRescue::Resumable>>(this);
	connect(watcher, &QFutureWatcher<QVector<OpRescue::Resumable>>::finished, this,
			[this, watcher]
			{
				m_resumable = watcher->result();
				watcher->deleteLater();
				updateResumeAction();
			});
	watcher->setFuture(QtConcurrent::run([] { return OpRescue::pending(); }));

	// The Edit ▸ Undo candidate, computed in the same off-thread sweep
	// spirit: reading the newest journal stats media paths and must never
	// block the GUI on a dead mount.
	auto *undoWatcher = new QFutureWatcher<UndoCandidate>(this);
	connect(undoWatcher, &QFutureWatcher<UndoCandidate>::finished, this,
			[this, undoWatcher]
			{
				m_undoCandidate = undoWatcher->result();
				undoWatcher->deleteLater();
				updateUndoAction();
			});
	undoWatcher->setFuture(QtConcurrent::run(
		[]() -> UndoCandidate
		{
			const auto rec = OpJournal::latestUndoable();
			if (!rec)
				return {};
			QString kind = opKindName(rec->kind);
			if (!kind.isEmpty())
				kind[0] = kind[0].toUpper();
			return {rec->path,
					tr("&Undo %1 (%2 files)").arg(kind).arg(rec->doneCount())};
		}));
}

void MainWindow::offerResume()
{
	// Works from the cached list (filled by the launch sweep and refreshed
	// off-thread after every run); the menu command is only enabled when
	// the journal has something in it.
	if (m_resumable.isEmpty())
		return;
	// Never on top of a running scan or operation (the launch-time call can
	// land mid-scan if the user was quick); the menu command stays live.
	if (!m_scanButton->isEnabled())
		return;
	const OpRescue::Resumable r = m_resumable.first();

	QString headline, reassurance;
	const QString where = QDir::toNativeSeparators(r.dest);
	switch (r.kind)
	{
	case OpKind::Copy:
		headline = tr("I was copying %n file(s) for %1, but didn't get them all in.", nullptr,
					  r.total)
					   .arg(where);
		reassurance = tr("The successfully copied files are at the destination. "
						 "The remaining files are untouched.");
		break;
	case OpKind::Move:
		headline = tr("I was moving %n file(s) over to %1, but didn't get them all in.", nullptr,
					  r.total)
					   .arg(where);
		reassurance = tr("The successfully moved files are at the destination. "
						 "The remaining files are untouched.");
		break;
	case OpKind::Delete:
		headline = tr("I was clearing out %n file(s) but didn't get them all out.", nullptr,
					  r.total);
		// Network volumes have no OS trash, so those deletes go to a
		// MediaMuster Trash folder on the volume itself.
		reassurance = r.usedMediaMusterTrash
						  ? tr("The successfully deleted files are in the MediaMuster Trash "
							   "on that volume. The rest are untouched.")
						  : tr("The successfully deleted files are in your bin. "
							   "The rest are untouched.");
		break;
	case OpKind::Rename:
		headline = tr("I was reorganising %n file(s) between media folders, but didn't finish.",
					  nullptr, r.total)
					   .arg(where);
		reassurance = tr("The files already moved are in their new folders. "
						 "The rest are untouched.");
		break;
	case OpKind::Undo:
		// Never offered (resumableFrom refuses undo runs); listed only
		// so the switch stays exhaustive.
		break;
	}

	const QDateTime started = QDateTime::fromString(r.started, Qt::ISODateWithMs).toLocalTime();
	QString counts;
	if (started.isValid())
		counts = tr("Started %1 • %2 finished • %3 remaining")
					 .arg(started.toString(QStringLiteral("d MMM, h:mm ap")))
					 .arg(Format::count(r.finished))
					 .arg(Format::count(r.remaining.size()));
	else
		counts = tr("%1 finished • %2 remaining")
					 .arg(Format::count(r.finished))
					 .arg(Format::count(r.remaining.size()));

	QMessageBox box(this);
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(QString());
	box.setText(headline);
	box.setInformativeText(
		tr("%1\n\n%2 Resume to finish, or Resume Later from the File menu.")
			.arg(counts, reassurance));
	auto *resumeBtn = box.addButton(tr("Resume"), QMessageBox::AcceptRole);
	auto *laterBtn = box.addButton(tr("Resume Later"), QMessageBox::RejectRole);
	auto *discardBtn = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
	box.setDefaultButton(laterBtn);
	box.setEscapeButton(laterBtn);
	box.exec();

	if (box.clickedButton() == discardBtn)
	{
		if (!QFile::remove(r.journalPath))
			addLog(QtWarningMsg, QStringLiteral("app"),
				   QStringLiteral("Couldn't delete the interrupted-run journal %1").arg(r.journalPath));
		else
			addLog(QtInfoMsg, QStringLiteral("app"),
				   QStringLiteral("Discarded an interrupted %1 (%2 of %3 files were not done)")
					   .arg(opKindName(r.kind))
					   .arg(r.remaining.size())
					   .arg(r.total));
		refreshResumable();
		return;
	}
	if (box.clickedButton() != resumeBtn)
	{
		refreshResumable(); // Resume Later: journal stays; menu item stays live
		return;
	}

	// Resume = the same dispatch as Manage Media, over the unfinished
	// files — dispatched STRAIGHT from the journal's own plan items, with
	// their identities and clip names intact (no reconstituted rows).
	// The new run writes its own journal, so the old one is retired the
	// moment the new run is under way — and kept if the dispatch was
	// declined (journal-writable gate), so the offer survives.
	addLog(QtInfoMsg, QStringLiteral("app"),
		   QStringLiteral("Resuming an interrupted %1: %2 of %3 files still to do")
			   .arg(opKindName(r.kind))
			   .arg(r.remaining.size())
			   .arg(r.total));
	OpRequest req;
	req.kind = r.kind;
	req.destRoot = r.dest;
	req.preserve = r.preserve;
	req.items = r.remaining;
	const bool dispatched = dispatchRequest(std::move(req));
	if (dispatched && !QFile::remove(r.journalPath))
		addLog(QtWarningMsg, QStringLiteral("app"),
			   QStringLiteral("Couldn't delete the interrupted-run journal %1 — it may be offered again")
				   .arg(r.journalPath));
	refreshResumable();
}

// MARK: - MediaMuster Trash dialog

void MainWindow::showMediaMusterTrashDialog(const QString &trashFolderPath, int fileCount)
{
	QMessageBox msgBox(this);
	msgBox.setIcon(QMessageBox::Information);
	msgBox.setWindowTitle(tr("MediaMuster Trash"));
	msgBox.setText(tr("<b>%n file(s) moved to the MediaMuster Trash</b>", nullptr, fileCount));
	msgBox.setInformativeText(tr("Network volumes only support permanent delete, so "
								 "MediaMuster has moved the file(s) to:\n\n%1\n\n"
								 "These files can be restored by moving them back to "
								 "their original location. To permanently delete them, "
								 "click \"Empty Trash\".")
								  .arg(trashFolderPath));

	auto *btnOk = msgBox.addButton(QMessageBox::Ok);
	auto *btnOpen = msgBox.addButton(tr("Take Me There"), QMessageBox::ActionRole);
	auto *btnEmpty = msgBox.addButton(tr("Empty Trash"), QMessageBox::DestructiveRole);
	msgBox.setDefaultButton(btnOk);
	msgBox.exec();

	if (msgBox.clickedButton() == btnOpen)
	{
		if (!QDesktopServices::openUrl(QUrl::fromLocalFile(trashFolderPath)))
			addLog(QtWarningMsg, QStringLiteral("ops"),
				   QStringLiteral("Couldn't open %1 in the file browser").arg(trashFolderPath));
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

	// Destructive, irreversible: name the action on the button and give it
	// the DestructiveRole so it reads (and, on macOS, styles) as the danger
	// choice. Cancel stays the default so a stray Return doesn't wipe files.
	QMessageBox confirm(this);
	confirm.setIcon(QMessageBox::Warning);
	confirm.setWindowTitle(tr("Empty the MediaMuster Trash?"));
	confirm.setText(tr("Permanently delete %n file(s) (%1) "
					   "from:\n\n%2\n\n"
					   "This cannot be undone.",
					   nullptr, trashFileCount)
						.arg(Format::bytes(trashBytes), trashFolderPath));
	auto *emptyBtn = confirm.addButton(tr("Empty Trash"), QMessageBox::DestructiveRole);
	confirm.addButton(QMessageBox::Cancel);
	confirm.setDefaultButton(QMessageBox::Cancel);
	confirm.exec();

	if (confirm.clickedButton() != emptyBtn)
		return;

	if (QDir(trashFolderPath).removeRecursively())
		addLog(QtInfoMsg, QStringLiteral("ops"), QStringLiteral("MediaMuster Trash emptied: %1").arg(trashFolderPath));
	else
		addLog(QtCriticalMsg, QStringLiteral("ops"), QStringLiteral("Failed to empty MediaMuster Trash: %1").arg(trashFolderPath));
}

// MARK: - CSV export

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
		this, tr("Export CSV"), QDir::homePath() + "/mediamuster_export.csv", tr("CSV Files (*.csv)"));
	if (path.isEmpty())
		return;

	// Snapshot on the main thread; Qt models are thread-affine.
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
	const QString label = exportSelected ? "selected records" : "records";
	addLog(QtInfoMsg, QStringLiteral("export"), QStringLiteral("Exporting %1 %2 to %3").arg(count).arg(label).arg(path));
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
					addLog(QtInfoMsg, QStringLiteral("export"),
						   QStringLiteral("Exported %1 %2 to %3").arg(count).arg(label).arg(path));
				}
				else
				{
					addLog(QtCriticalMsg, QStringLiteral("export"), QStringLiteral("Failed to write %1").arg(path));
				}
			});

	watcher->setFuture(
		QtConcurrent::run([path, rows = std::move(rows)]()
						  { return MediaCsv::write(path, rows); }));
}

// MARK: - Project summary

QDialog *MainWindow::buildProjectSummaryDialog(const QVector<MediaFile> &files)
{
	QMap<QString, ProjectSummary> map;
	for (const auto &f : files)
	{
		auto &s = map[f.projectDisplay()];
		s.name = f.projectDisplay();
		s.hasProject = !f.hasNoProject();
		if (f.kind == MediaFile::Kind::Video)
			s.videoCount++;
		else
			s.audioCount++;
		s.totalBytes += f.sizeBytes;
		if (!f.originalBin.isEmpty() && !s.bins.contains(f.originalBin))
			s.bins.append(f.originalBin);
	}
	auto *dlg = new QDialog(this);
	dlg->setWindowTitle(tr("Project Summary"));
	dlg->resize(600, 380);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	auto *t = new QTableView(dlg);
	auto *sm = new QStandardItemModel(static_cast<int>(map.size()), 5, dlg);
	// Column-type vocabulary, kept literal to match the main table headers.
	sm->setHorizontalHeaderLabels({QStringLiteral("Project"), QStringLiteral("Video"),
								   QStringLiteral("Audio"), QStringLiteral("Bins"),
								   QStringLiteral("Size")});
	// Sort on UserRole, not the formatted display text: otherwise "1,000" sorts
	// before "9" and "5.6 GB" before "900 MB".
	sm->setSortRole(Qt::UserRole);

	// Right-aligned number cell whose sort key is the raw value behind the
	// formatted text.
	const auto numItem = [](const QString &text, qint64 sortValue)
	{
		auto *item = new QStandardItem(text);
		item->setData(QVariant::fromValue(sortValue), Qt::UserRole);
		item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		return item;
	};

	int r = 0;
	for (auto it = map.begin(); it != map.end(); ++it, ++r)
	{
		const auto &s = it.value();
		auto *proj = new QStandardItem(Icons::forProject(s.hasProject), s.name);
		proj->setData(s.name, Qt::UserRole);
		sm->setItem(r, 0, proj);
		sm->setItem(r, 1, numItem(Format::count(s.videoCount), s.videoCount));
		sm->setItem(r, 2, numItem(Format::count(s.audioCount), s.audioCount));
		sm->setItem(r, 3, numItem(Format::count(s.bins.size()), s.bins.size()));
		sm->setItem(r, 4, numItem(Format::bytes(s.totalBytes), s.totalBytes));
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
	return dlg;
}

void MainWindow::onProjectSummary()
{
	if (m_model->allFiles().isEmpty())
	{
		QMessageBox::information(
			this, tr("Project Summary"),
			tr("Hold your horses! Scan some media first to see the project summary."));
		return;
	}
	buildProjectSummaryDialog(m_model->allFiles())->show();
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
	// Selects every visible row sharing a master MOB with
	// anything currently selected; V01 + A01 + A02 of each clip.
	// Hidden or filtered rows stay hidden.
	auto sel = selectedFiles();
	if (sel.isEmpty())
		return;

	// Collect every unique non-empty comp MOB from the selection.
	// Empty MOBs mean the file isn't tied to a master clip we can
	// follow; they're silently excluded from the seed set but the
	// other selected MOBs still drive a result.
	QSet<QString> seedMobs;
	for (const MediaFile &f : sel)
	{
		if (!f.masterMobId.isEmpty())
			seedMobs.insert(f.masterMobId);
	}

	if (seedMobs.isEmpty())
	{
		addLog(QtWarningMsg, QStringLiteral("relatives"), "No master MOBs in selection — nothing to follow");
		return;
	}

	auto *selModel = m_tableView->selectionModel();
	const int rowCount = m_proxy->rowCount();

	QVector<int> rows;
	for (int row = 0; row < rowCount; ++row)
	{
		const MediaFile &f = fileAtProxyRow(row);
		if (!f.masterMobId.isEmpty() && seedMobs.contains(f.masterMobId))
			rows.append(row);
	}

	if (rows.isEmpty())
	{
		addLog(QtWarningMsg, QStringLiteral("relatives"), "No relatives found (the relatives may be filtered out)");
		return;
	}

	const int matched = rows.size();
	const QItemSelection newSelection = selectionForRows(rows);

	// Replace the existing selection with the relatives set and
	// scroll so the first match is in view; saves the editor
	// hunting for what just got selected.
	selModel->clearSelection();
	selModel->select(newSelection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
	const QModelIndex first = m_proxy->index(rows.first(), 0);
	selModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
	m_tableView->scrollTo(first, QAbstractItemView::EnsureVisible);

	addLog(QtInfoMsg, QStringLiteral("relatives"),
		   QStringLiteral("Selected %1 relative%2 across %3 master clip%4")
			   .arg(matched)
			   .arg(matched == 1 ? "" : "s")
			   .arg(seedMobs.size())
			   .arg(seedMobs.size() == 1 ? "" : "s"));
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
		   QStringLiteral("Inverted selection: %1 of %2 visible row%3 selected")
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

	menu.addAction(tr("Reveal in Finder"), this, &MainWindow::onRevealInFinder);
	menu.addAction(tr("Copy Path"),
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

	const auto sel = selectedFiles();
	const bool hasComp = std::any_of(sel.cbegin(), sel.cend(), [](const MediaFile &f)
									 { return !f.masterMobId.isEmpty(); });
	QAction *relAct =
		menu.addAction(tr("Select Relatives"), this, &MainWindow::onSelectRelatives);
	relAct->setEnabled(hasComp);

	QAction *invAct =
		menu.addAction(tr("Select Inverse"), this, &MainWindow::onInvertSelection);
	invAct->setEnabled(m_proxy->rowCount() > 0);

	menu.addSeparator();
	menu.addAction(tr("Copy To..."), this, [this]()
				   { openManageMedia(Enum::to_underlying(ManageMediaDialog::Operation::Copy)); });
	menu.addAction(tr("Move To..."), this, [this]()
				   { openManageMedia(Enum::to_underlying(ManageMediaDialog::Operation::Move)); });
	menu.addAction(tr("Delete..."), this, [this]()
				   { openManageMedia(Enum::to_underlying(ManageMediaDialog::Operation::Delete)); });
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
	const int lastCol = Enum::to_underlying(MediaTableModel::Column::Count_) - 1;
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
	// Tee the console into the diagnostic log so one file carries both the
	// user-facing story and the dev categories.
	AppLog::appendConsoleLine(level, module, message);
}

// MARK: - Column auto-fit

void MainWindow::autoFitColumns()
{
	// Fit means fit. This used to clamp every column at 300px, which was
	// invisible while the widest cell was a filename — and then made the
	// menu item a lie once Location started carrying whole paths, since
	// they never fit in 300px and the tail is the part worth reading.
	// A table wider than its window scrolls sideways; that is the normal
	// way to show wide content, not something to protect the user from.
	//
	// Qt measures the first 1000 rows by default (setResizeContentsPrecision)
	// rather than all of them, which is what keeps this instant on a
	// 300,000-file Nexis scan.
	m_tableView->resizeColumnsToContents();
}

// MARK: - Busy state

void MainWindow::setBusy(bool busy)
{
	m_scanButton->setEnabled(!busy);
	m_scanAllButton->setEnabled(!busy);

	// Manage Media: always disabled when busy; otherwise follows selection.
	const bool hasSel =
		m_tableView->selectionModel() && !m_tableView->selectionModel()->selectedRows().isEmpty();
	m_btnFileOps->setEnabled(!busy && hasSel);
	m_btnRebalance->setEnabled(!busy && !m_model->allFiles().isEmpty());
	updateResumeAction();

	if (!busy && m_progressDialog)
		m_progressDialog->finish();

	// Skip the 5 second polling tick when a scan or op is running.
	m_volumeManager->setBusy(busy);
}

// MARK: - Progress dialog

ProgressDialog *MainWindow::progressDialog()
{
	// Cancel fires on both workers; the idle worker's cancel() is a no-op.
	if (!m_progressDialog)
	{
		m_progressDialog = new ProgressDialog(this);
		connect(m_progressDialog, &ProgressDialog::cancelRequested, this,
				[this]()
				{
					m_scanner->cancelScan();
					m_fileOps->cancel();
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
	// Tally per filter using kFilterDefs as the single source of
	// truth. The order here matches the tab indices the QTabBar
	// uses, because both were populated from the same array.
	std::array<int, kFilterDefs.size()> counts{};
	for (const MediaFile &f : m_model->allFiles())
	{
		for (size_t i = 0; i < kFilterDefs.size(); ++i)
		{
			if (MediaFilterProxy::matchesMode(kFilterDefs[i].mode, f))
				++counts[i];
		}
	}

	for (size_t i = 0; i < kFilterDefs.size(); ++i)
	{
		const int idx = static_cast<int>(i);
		m_filterTabs->setTabText(
			idx, QStringLiteral("%1 (%2)").arg(QString::fromLatin1(kFilterDefs[i].label)).arg(counts[i]));

		// "All" stays visible no matter what; the others auto-hide
		// when empty, unless the editor chose 'Show All Filter Tabs'.
		const bool isAll = (kFilterDefs[i].mode == MediaFilterProxy::FilterMode::All);
		const bool visible = isAll || m_showAllFilterTabs || counts[i] > 0;
		m_filterTabs->setTabVisible(idx, visible);
	}
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
	if (tabIdx > 0)
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
				// isActive=false, which fans out through the connected
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
										   { m_proxy->setBinFilterMobs(false, {}); });
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

	// Bin filter: when the dialog exists, clearChain() tears down its internal
	// chain and the chain-list UI. The cached chip state is reset here for the
	// no-dialog / empty-chain paths clearChain() skips.
	if (m_binFilterDialog)
		m_binFilterDialog->clearChain();
	m_binFilterActive = false;
	m_binFilterBinNames.clear();

	// The project selection was wiped by the list rebuild (clear() emits no
	// signal), so the proxy's project set has to be reset by hand. Reset all
	// four predicates in one place regardless of which paths above already
	// touched the proxy.
	m_proxy->setFilterMode(MediaFilterProxy::FilterMode::All);
	m_proxy->setSearchText({});
	m_proxy->setProjectFilter({});
	m_proxy->setBinFilterMobs(false, {});

	rebuildFilterChips();
}