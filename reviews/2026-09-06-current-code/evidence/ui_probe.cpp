#include "mainwindow.h"
#include "managemediadialog.h"
#include "macaccessibilityguard.h"
#include "logfile.h"
#include "oprunner.h"
#include "rebalancedialog.cpp"
#include <QAccessible>
#include <QApplication>
#include <QFile>
#include <QListWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QLineEdit>
#include <QThread>
#include <QTextStream>
#include <QElapsedTimer>
#include <QDate>
#include <memory>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTextStream out(stdout);
    out << "EXPIRY configured=2027-02-31 QDate-valid=" << QDate::fromString("2027-02-31",Qt::ISODate).isValid() << "\n";
    MainWindow w(nullptr, MainWindow::StartupMode::UiOnly);
    VolumeInfo v; v.name="EDIT"; v.path="/tmp"; v.hasAvidMedia=true;
    w.rebuildVolumeList({v});
    out << "VOLUME initial selected=" << w.m_volumeList->selectedItems().size() << " expected=1\n";
    w.m_volumeList->item(0)->setSelected(true);
    w.rebuildVolumeList({v});
    out << "VOLUME after refresh selected=" << w.m_volumeList->selectedItems().size() << " expected=1\n";
    QTemporaryDir tmp;
    w.addVolumePath(tmp.path());
    out << "VOLUME manual-add selected=" << w.m_volumeList->selectedItems().size() << " expected=1\n";

    MediaFile f; f.filePath="/tmp/probe.mxf"; f.fileName="probe.mxf"; f.project="Finished Project"; f.sizeBytes=1;
    w.onScanFinished({f});
    w.m_removeAfterOp=true; w.m_successfulOpPaths.insert(f.filePath);
    w.m_fileOps->operationFinished(1,0);
    app.processEvents();
    out << "PROJECT rows-after-remove=" << w.m_model->rowCount() << " sidebar-projects=" << w.m_projectList->count() << " tooltip=" << w.m_projectList->item(0)->toolTip() << "\n";

    std::unique_ptr<RebalanceDialog> r(RebalanceDialog::createDemo(RebalanceDialog::DemoScenario::Small));
    r->primeLiveState();
    const auto op=r->m_currentPlan.ops.first();
    const auto src=*Rebalancer::srcFolderOf(op.srcPath);
    const int before=r->m_cards.value(src)->m_currentCount;
    const int projected=r->m_cards.value(src)->m_projectedCount;
    r->onFinished(0,0,true);
    out << "REBALANCE cancelled moved=0 source-before=" << before << " projected=" << projected << " displayed-after=" << r->m_cards.value(src)->m_currentCount << "\n";

    QVector<MediaFile> dups;
    for(int n=0;n<3;++n) { MediaFile d; d.fileName="duplicate.mxf"; d.filePath=tmp.filePath(QString::number(n)+"/duplicate.mxf"); d.sizeBytes=1; dups.append(d); }
    QDir().mkpath(tmp.filePath("destination"));
    ManageMediaDialog md(dups);
    md.m_destPath->setText(tmp.filePath("destination"));
    QElapsedTimer wait; wait.start();
    while(md.m_checkingDest && wait.elapsed()<10000) { app.processEvents(); QThread::msleep(1); }
    out << "DEST-PREVIEW";
    for(const auto &p : md.m_pendingRows) out << " | " << QFileInfo(p.item->text(1)).fileName();
    out << "\n";

    MediaFile large=f,small=f; large.filePath=tmp.filePath("large.mxf"); large.fileName="large.mxf"; large.sizeBytes=QStorageInfo(tmp.path()).bytesAvailable()+1; small.filePath=tmp.filePath("small.mxf"); small.fileName="small.mxf";
    {QFile x(tmp.filePath("destination/large.mxf")); x.open(QIODevice::WriteOnly); x.write("existing");}
    ManageMediaDialog space({large,small});
    space.m_destPath->setText(tmp.filePath("destination"));
    wait.restart(); while(space.m_checkingDest && wait.elapsed()<10000) { app.processEvents(); QThread::msleep(1); }
    space.m_conflictGlobalCombo->setCurrentIndex(1);
    out << "SPACE-SKIP policy=" << int(space.conflictPolicies().value(large.filePath)) << " execute-enabled=" << space.m_btnExecute->isEnabled() << " remaining-required=1 available=" << QStorageInfo(tmp.path()).bytesAvailable() << "\n";

    MacAccessibilityGuard::install();
    QListWidget list; list.addItem("Real media row");
    QAccessibleInterface *iface=QAccessible::queryAccessibleInterface(&list);
    out << "ACCESSIBILITY rows=" << list.count() << " accessible-children=" << iface->childCount() << " table-interface=" << bool(iface->tableInterface()) << "\n";

    // Only this process's unique QStandardPaths test directory is touched.
    QStandardPaths::setTestModeEnabled(true);
    app.setOrganizationName("MediaMusterReviewTemporary");
    app.setApplicationName("migration-" + QString::number(QCoreApplication::applicationPid()));
    const QString dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (QFileInfo::exists(dir)) return 2;
    QDir().mkpath(dir+"/logs");
    {QFile a(dir+"/mediamuster.log"); a.open(QIODevice::WriteOnly); a.write("CURRENT\n");}
    {QFile a(dir+"/logs/mediamuster.log"); a.open(QIODevice::WriteOnly); a.write("UNIQUE OLD HISTORY\n");}
    {QFile a(dir+"/logs/MediaMuster-old.ips"); a.open(QIODevice::WriteOnly); a.write("UNIQUE CRASH REPORT\n");}
    AppLog::install();
    out << "LOG-MIGRATION old-history-exists=" << QFileInfo::exists(dir+"/logs/mediamuster.log") << " crash-report-exists=" << QFileInfo::exists(dir+"/logs/MediaMuster-old.ips") << "\n";
    out.flush();
    QDir(dir).removeRecursively();
}
