#include "oprunner.h"
#include "oprescue.h"
#include "opundo.h"
#include "parkedfile.h"
#include "rebalancer.h"
#include "testpause.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <functional>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/resource.h>
#include <csignal>

static void require(bool condition) { if (!condition) throw std::runtime_error("setup/assertion failed"); }
static void put(const QString &p,const QByteArray &b) { require(QDir().mkpath(QFileInfo(p).absolutePath())); QFile f(p);require(f.open(QIODevice::WriteOnly));require(f.write(b)==b.size()); }
static QByteArray get(const QString &p) { QFile f(p);require(f.open(QIODevice::ReadOnly));return f.readAll(); }
static QByteArray fixture() { auto b=get("/Users/martymclean/Developer/MediaMuster/tests/fixtures/avid_headers/V01.E683C412_F82F4F82F461DV.mxf"); b.append(QByteArray(8*1024*1024-b.size(),'x')); return b; }
static void deadOwner(const QString &p) { QByteArray out;for (auto line:get(p).split('\n')) { if(line.isEmpty())continue; auto o=QJsonDocument::fromJson(line).object();if(o.value("record")=="begin")o["processId"]=0;out+=QJsonDocument(o).toJson(QJsonDocument::Compact)+'\n'; } put(p,out); }
struct Sink final:OpSink { std::function<void(const QString&,double)> hook; QStringList messages;
 void progress(const QString&n,int,int,double pct) override {if(hook)hook(n,pct);}
 void itemDone(const QString&,const QString&,bool,const QString&e,bool) override {messages<<e;}
 void log(QtMsgType,const QString&m) override {messages<<m;}
 void trashUsed(const QString&,int) override {} };
static OpRequest request(const QString&s,const QString&d,OpKind k) { OpRequest r;r.kind=k;r.destRoot=d;OpItem i;i.src=s;i.name=QFileInfo(s).fileName();i.bytes=QFileInfo(s).size();r.items<<i;return r; }
static void report(const QString&name,const QJsonObject&o) { QTextStream(stdout)<<name<<" "<<QJsonDocument(o).toJson(QJsonDocument::Compact)<<'\n'; }

static void inplaceSourceEdit() {
 QTemporaryDir t; auto s=t.path()+"/src/a.mxf",d=t.path()+"/dst",dst=d+"/a.mxf"; put(s,fixture());
 auto before=FileIdentity::capture(s);require(!before.contentUmid.isEmpty());std::atomic<bool>cancel{false};Sink sink;bool changed=false;
 sink.hook=[&](const QString&n,double){ if(n.startsWith("Verifying")&&!changed){QFile f(s);require(f.open(QIODevice::ReadWrite));require(f.seek(3*1024*1024));require(f.write("Z",1)==1);require(f.flush());changed=true;}};
 qputenv("MEDIAMUSTER_FORCE_MOVE_COPY","1");qputenv("MEDIAMUSTER_DISABLE_CLONEFILE","1"); OpRunner runner(sink,cancel);auto r=runner.run(request(s,d,OpKind::Move),t.path()+"/journal");
 require(changed&&r.succeeded==1&&!QFile::exists(s)&&get(dst).at(3*1024*1024)=='x');
 report("SOURCE_EDIT_LOST",{{"sourceConfidence",int(before.confidence)},{"sourceHadUmid",true},{"sourceExists",QFile::exists(s)},{"destinationRetainedOldByte",true},{"succeeded",r.succeeded},{"failed",r.failed}});
}
static void replacedDestinationCancel() {
 QTemporaryDir t;auto s=t.path()+"/src/a.mxf",d=t.path()+"/dst",dst=d+"/a.mxf";put(s,fixture());std::atomic<bool>cancel{false};Sink sink;bool changed=false;
 sink.hook=[&](const QString&n,double){if(n.startsWith("Verifying")&&!changed){require(QFile::remove(dst));put(dst,"another editor's unique file");changed=true;cancel=true;}};
 OpRunner runner(sink,cancel);auto r=runner.run(request(s,d,OpKind::Copy),t.path()+"/journal");require(changed&&!QFile::exists(dst)&&QFile::exists(s));
 report("CANCEL_DELETES_REPLACEMENT",{{"replacementExists",QFile::exists(dst)},{"sourceExists",QFile::exists(s)},{"succeeded",r.succeeded},{"failed",r.failed}});
}
static void failedRecoveryNoRetry() {
 QTemporaryDir t; auto src=t.path()+"/src/a.mxf",dst=t.path()+"/dst/a.mxf",park=dst+".__copyreplace_test";put(src,fixture());put(park,"original media");put(dst,"unfinished");QString jp;
 {OpJournal j(OpKind::Copy,{},t.path()+"/journal");jp=j.path();auto req=request(src,QFileInfo(dst).absolutePath(),OpKind::Copy);j.writePlan(req.destRoot,false,req.items,{});auto id=j.planOp(src,dst,QFileInfo(src).size(),park,FileIdentity::capture(src),FileIdentity::capture(park));j.markFailed(id,"restore blocked",true);j.finish(0,1,0);}
 deadOwner(jp);require(::chflags(QFile::encodeName(dst).constData(),UF_IMMUTABLE)==0);auto a=OpRescue::run(t.path()+"/journal");require(a.opsFlagged==1);require(::chflags(QFile::encodeName(dst).constData(),0)==0);require(QFile::remove(dst));auto b=OpRescue::run(t.path()+"/journal");
 require(QFile::exists(park)&&!QFile::exists(dst)&&!QFile::exists(jp));report("FAILED_RECOVERY_FORGOTTEN",{{"firstFlagged",a.opsFlagged},{"secondReversed",b.opsReversed},{"originalStillParked",QFile::exists(park)},{"journalExists",QFile::exists(jp)}});
}
static void recoveredPathsGoStale() {
 QTemporaryDir t; auto old=t.path()+"/old",now=t.path()+"/new";put(now+"/a.mxf","media");QString jp;VolumeIdentity v;v.rootPath=old;v.uuid="TEST-VOLUME";v.label="TEST";v.confidence=VolumeIdentity::Confidence::High;
 {OpJournal j(OpKind::Delete,{},t.path()+"/journal");jp=j.path();OpItem i;i.src=old+"/a.mxf";i.name="a.mxf";i.bytes=5;j.writePlan({},false,{i},{v});}deadOwner(jp);auto mounted=v;mounted.rootPath=now;
 auto a=OpRescue::run(t.path()+"/journal",{mounted});auto b=OpRescue::run(t.path()+"/journal",{mounted});auto p=OpRescue::pending(t.path()+"/journal");require(a.resumable.size()==1&&b.resumable.size()==1&&p.size()==1);
 require(a.resumable[0].remaining[0].src==now+"/a.mxf"&&b.resumable[0].remaining[0].src==old+"/a.mxf"&&p[0].remaining[0].src==old+"/a.mxf");report("RECOVERED_PATHS_REVERT",{{"firstUsesNewRoot",true},{"secondUsesOldRoot",true},{"pendingUsesOldRoot",true}});
}
static void unverifiedWholeCopy() {
 QTemporaryDir t;auto src=t.path()+"/src/a.mxf",dst=t.path()+"/dst/a.mxf";put(src,fixture());auto bad=fixture();bad[3*1024*1024]='Z';put(dst,bad);QString jp;
 {OpJournal j(OpKind::Copy,{},t.path()+"/journal");jp=j.path();auto req=request(src,QFileInfo(dst).absolutePath(),OpKind::Copy);j.writePlan(req.destRoot,false,req.items,{});j.planOp(src,dst,QFileInfo(src).size(),{},FileIdentity::capture(src));}deadOwner(jp);auto r=OpRescue::run(t.path()+"/journal");require(r.opsFlagged==0&&r.resumable.isEmpty()&&get(dst)!=get(src));report("UNVERIFIED_WHOLE_COPY_ACCEPTED",{{"differentBytes",true},{"sameSize",true},{"opsFlagged",r.opsFlagged},{"resumeOffers",r.resumable.size()}});
}
static void innerMoveRollbackLost() {
 QTemporaryDir t;auto src=t.path()+"/src/a.mxf",d=t.path()+"/dst",dst=d+"/a.mxf";put(src,fixture());Sink sink;std::atomic<bool>cancel{false};bool armed=false;
 sink.hook=[&](const QString&n,double pct){if(pct>0&&!n.startsWith("Verifying")&&!armed){require(::chflags(QFile::encodeName(dst).constData(),UF_IMMUTABLE)==0);armed=true;cancel=true;}};
 TestPause::setEnabled(true);OpRunner runner(sink,cancel);auto r=runner.run(request(src,d,OpKind::Move),t.path()+"/journal");TestPause::setEnabled(false);auto records=OpJournal::scan(t.path()+"/journal");require(records.size()==1);const bool dirty=records[0].dirty;require(::chflags(QFile::encodeName(dst).constData(),0)==0);require(armed&&QFile::exists(dst)&&!dirty&&QFileInfo(dst).size()<QFileInfo(src).size());
 report("MOVE_CANCEL_STRANDING_UNJOURNALED",{{"destinationLeftBehind",QFile::exists(dst)},{"destinationBytes",QFileInfo(dst).size()},{"sourceBytes",QFileInfo(src).size()},{"journalDirty",dirty},{"journalComplete",records[0].complete},{"failed",r.failed}});
}
static void endlessRebalance() {
 QTemporaryDir t;auto root=t.path()+"/MXF";QVector<MediaFile> files;for(int i=0;i<Conventions::kFolderTarget;++i){MediaFile m;m.fileName=QString::number(i)+".mxf";m.filePath=root+"/1/"+m.fileName;m.mxfFolder="1";m.masterMobId="SAME_MASTER";put(m.filePath,{});files<<m;}
 auto a=Rebalancer::computePlan(root,"test",files);require(a.ops.size()==Conventions::kFolderTarget);require(a.newFolders.size()==1);require(QDir().mkpath(root+"/2"));for(auto&m:files){auto np=root+"/2/"+m.fileName;require(QFile::rename(m.filePath,np));m.filePath=np;m.mxfFolder="2";}
 auto b=Rebalancer::computePlan(root,"test",files);require(b.ops.size()==Conventions::kFolderTarget);report("BALANCED_FULL_GROUP_MOVES_FOREVER",{{"folderTarget",Conventions::kFolderTarget},{"firstMoves",a.ops.size()},{"secondMoves",b.ops.size()},{"firstDestination",a.newFolders[0].display()},{"secondDestination",b.newFolders[0].display()}});
}
static void undoReplaceRetryLosesTail() {
 QTemporaryDir t;auto src=t.path()+"/src/a.mxf",d=t.path()+"/dst",dst=d+"/a.mxf",jd=t.path()+"/journal";put(src,"new media");put(dst,"old original");qputenv("MEDIAMUSTER_DISABLE_OS_TRASH","1");qputenv("MEDIAMUSTER_TRASH_ROOT",t.path().toUtf8());qunsetenv("MEDIAMUSTER_FORCE_MOVE_COPY");Sink sink;std::atomic<bool>cancel{false};OpRunner runner(sink,cancel);auto req=request(src,d,OpKind::Move);req.items[0].policy="replace";require(runner.run(req,jd).succeeded==1);auto rec=OpJournal::latestUndoable(jd);require(rec.has_value());auto parked=rec->ops[0].parkedFinal;require(::chflags(QFile::encodeName(parked).constData(),UF_IMMUTABLE)==0);OpUndo undo(sink,cancel);auto first=undo.run(rec->path,jd);require(first.failed==1);require(::chflags(QFile::encodeName(parked).constData(),0)==0);auto second=undo.run(rec->path,jd);auto after=OpJournal::readOne(rec->path);require(second.skipped==1&&after->undone&&QFile::exists(parked)&&!QFile::exists(dst));report("DORMANT_UNDO_FORGETS_REPLACE_TAIL",{{"firstFailed",first.failed},{"secondSkipped",second.skipped},{"journalUndone",after->undone},{"replacedOriginalStillInTrash",true},{"destinationStillMissing",true}});qunsetenv("MEDIAMUSTER_DISABLE_OS_TRASH");qunsetenv("MEDIAMUSTER_TRASH_ROOT");
}
static void renameUnavailableJournal() {
 QTemporaryDir t;auto src=t.path()+"/MXF/1/a.mxf",dst=t.path()+"/MXF/2/a.mxf",jd=t.path()+"/not-a-dir";put(src,"media");require(QDir().mkpath(QFileInfo(dst).absolutePath()));put(jd,"blocking file");auto r=request(src,{},OpKind::Rename);r.items[0].renameDst=dst;Sink sink;std::atomic<bool>cancel{false};OpRunner runner(sink,cancel);auto out=runner.run(r,jd);require(out.succeeded==1&&!QFile::exists(src)&&OpJournal::scan(jd).isEmpty());report("RENAME_WITHOUT_JOURNAL_SUCCEEDS",{{"succeeded",out.succeeded},{"sourceExists",QFile::exists(src)},{"destinationExists",QFile::exists(dst)},{"journalCount",OpJournal::scan(jd).size()}});
}
static void relativesSplitOnFailure() {
 QTemporaryDir t;auto root=t.path()+"/MXF";put(root+"/1/video.mxf","video");put(root+"/1/audio.mxf","audio");put(root+"/2/video.mxf","existing conflict");auto r=request(root+"/1/video.mxf",{},OpKind::Rename);r.items[0].renameDst=root+"/2/video.mxf";r.items[0].groupKey="one-clip";auto audio=request(root+"/1/audio.mxf",{},OpKind::Rename).items[0];audio.renameDst=root+"/2/audio.mxf";audio.groupKey="one-clip";r.items<<audio;Sink sink;std::atomic<bool>cancel{false};OpRunner runner(sink,cancel);auto out=runner.run(r,t.path()+"/journal");require(out.succeeded==1&&out.failed==1&&QFile::exists(root+"/1/video.mxf")&&!QFile::exists(root+"/1/audio.mxf")&&QFile::exists(root+"/2/audio.mxf"));report("RELATIVES_SPLIT_ON_FAILURE",{{"succeeded",out.succeeded},{"failed",out.failed},{"videoStayedInFolder1",true},{"audioMovedToFolder2",true}});
}
static void renameJournalStopsWriting() {
 QTemporaryDir t;auto src=t.path()+"/MXF/1/a.mxf",dst=t.path()+"/MXF/2/a.mxf",jd=t.path()+"/journal";put(src,"media");require(QDir().mkpath(QFileInfo(dst).absolutePath()));auto req=request(src,{},OpKind::Rename);req.items[0].renameDst=dst;Sink sink;std::atomic<bool>cancel{false};struct rlimit original{};require(::getrlimit(RLIMIT_FSIZE,&original)==0);auto previous=std::signal(SIGXFSZ,SIG_IGN);bool armed=false;bool journalInitiallyValid=false;
 sink.hook=[&](const QString&,double){if(!armed){auto records=OpJournal::scan(jd);journalInitiallyValid=records.size()==1&&records[0].hasPlan;require(journalInitiallyValid);struct rlimit restricted=original;restricted.rlim_cur=8;require(::setrlimit(RLIMIT_FSIZE,&restricted)==0);armed=true;}};
 OpRunner runner(sink,cancel);auto out=runner.run(req,jd);require(::setrlimit(RLIMIT_FSIZE,&original)==0);std::signal(SIGXFSZ,previous);require(armed&&out.succeeded==1&&!QFile::exists(src)&&OpJournal::scan(jd).isEmpty());report("JOURNAL_FAILS_MIDRUN_RENAME_CONTINUES",{{"initialJournalAndPlanPresent",journalInitiallyValid},{"succeeded",out.succeeded},{"failed",out.failed},{"destinationExists",QFile::exists(dst)},{"journalCount",OpJournal::scan(jd).size()},{"degradedWarningEmitted",sink.messages.join('\n').contains("stopped accepting writes")}});
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{if(argc>1&&QByteArray(argv[1])=="journal-degrade"){renameJournalStopsWriting();return 0;}inplaceSourceEdit();replacedDestinationCancel();failedRecoveryNoRetry();recoveredPathsGoStale();unverifiedWholeCopy();innerMoveRollbackLost();endlessRebalance();undoReplaceRetryLosesTail();renameUnavailableJournal();relativesSplitOnFailure();}catch(const std::exception&e){QTextStream(stderr)<<e.what()<<'\n';return 1;}return 0;}
