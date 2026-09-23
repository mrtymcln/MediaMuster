#include "oprunner.h"
#include "rebalancer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>

static void put(const QString &path, const QByteArray &bytes) {
 QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
 if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("Fixture write failed");
}
struct Sink : OpSink {
 QVector<OpResult> results;
 void progress(const QString&,int,int,double) override {}
 void log(QtMsgType,const QString&) override {}
 void trashUsed(const QString&,int) override {}
 void result(const OpResult&r) override {results.append(r);}
};
int main(int argc,char**argv) {
 QCoreApplication app(argc,argv);
 QTemporaryDir temp; const QString root=OpJournal::canonicalPath(temp.path());
 {
  QString source=root+"/trash-source.bin"; put(source,"before");
  OpRequest req; req.kind=OpKind::Delete; OpItem item; item.src=source; item.name="trash-source.bin"; item.bytes=6; req.items.append(item);
  Sink sink; std::atomic<bool> cancel{false}; OpRunner runner(sink,cancel);
  runner.hooks.nativeTrash=[&](const auto&) {put(source,"changed but still present"); return OpTrash::Result{};};
  runner.run(req,root+"/trash-journal");
  qInfo()<<"TRASH changed source remains:"<<QFileInfo::exists(source)<<"reported sourceRemoved:"<<sink.results.last().sourceRemoved;
 }
 {
  QString mediaRoot=root+"/Avid MediaFiles/MXF"; QString source=mediaRoot+"/1/clip.mxf"; put(source,"x");
  RebalancePlan plan; plan.mxfRoot=mediaRoot; plan.volumeLabel="Disposable"; RenameOp op; op.srcPath=source; op.dest={"",2}; op.sizeBytes=1; plan.ops.append(op);
  qputenv("MEDIAMUSTER_JOURNAL_DIR",(root+"/rebalance-journal").toUtf8());
  Rebalancer worker; bool finished=false; int completed=-1; bool cancelled=false;
  QObject::connect(&worker,&Rebalancer::finished,&app,[&](int s,int,bool c){finished=true;completed=s;cancelled=c;});
  worker.executeAsync(plan);
  // Let preflight queue its callback while deliberately holding off GUI dispatch.
  QThread::msleep(1000);
  worker.cancel();
  QElapsedTimer time; time.start();
  while(!finished && time.elapsed()<10000) {QCoreApplication::processEvents();QThread::msleep(2);}
  qInfo()<<"REBALANCE cancelled before engine dispatch, finished:"<<finished<<"moved:"<<completed<<"cancelled flag:"<<cancelled<<"source present:"<<QFileInfo::exists(source)<<"destination present:"<<QFileInfo::exists(mediaRoot+"/2/clip.mxf");
 }
 {
  RebalancePlan invalid; invalid.mxfRoot=root+"/missing/Avid MediaFiles/MXF"; RenameOp op; op.srcPath=invalid.mxfRoot+"/1/missing.mxf"; op.dest={"",2}; invalid.ops.append(op);
  qputenv("MEDIAMUSTER_JOURNAL_DIR",(root+"/invalid-plan-journal").toUtf8());
  Rebalancer worker; bool finished=false; int completed=-1,failed=-1,aborted=0;
  QObject::connect(&worker,&Rebalancer::finished,&app,[&](int s,int f,bool){finished=true;completed=s;failed=f;});
  QObject::connect(&worker,&Rebalancer::aborted,&app,[&](const QString&){++aborted;});
  worker.executeAsync(invalid); QElapsedTimer time; time.start();
  while(!finished && !aborted && time.elapsed()<10000) {QCoreApplication::processEvents();QThread::msleep(2);}
  qInfo()<<"REBALANCE invalid plan, finished:"<<finished<<"moved:"<<completed<<"failed:"<<failed<<"aborted signals:"<<aborted;
 }
}
