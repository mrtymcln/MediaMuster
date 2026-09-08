#include "mediascanner.h"
#include "avbparser.h"
#include "omfuid.h"
#include "mediafilterproxy.h"
#include "mediatablemodel.h"
#include "mediacsv.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTextStream>
#include <QAbstractItemModelTester>
#include <sys/resource.h>
#include <signal.h>
#include <iostream>

QVector<MediaFile> scan(const QString &path, bool manual) {
 MediaScanner scanner; QSignalSpy finished(&scanner, &MediaScanner::scanFinished);
 MediaScanner::Options opts; (manual ? opts.manualPaths : opts.volumePaths) = {path};
 scanner.startScan(opts); if (!finished.wait(10000)) qFatal("scan timed out");
 return finished.takeFirst().at(0).value<QVector<MediaFile>>();
}
int main(int argc,char**argv) {
 QCoreApplication app(argc,argv); QTemporaryDir tmp;
 if (!tmp.isValid()) return 2;
 const QString fixture="/Users/martymclean/Developer/MediaMuster/tests/fixtures/TONE_100A01.EA7D504A.611740.mxf";
 const QString folder=tmp.path()+"/Avid MediaFiles/MXF/1"; QDir().mkpath(folder);
 QFile::copy(fixture,folder+"/tone.mxf");
 QFile::copy(fixture,folder+"/parked.mxf.__movereplace_ab12");
 auto rows=scan(tmp.path(),false);
 for(const auto &f:rows) std::cout << "SCAN "<<f.fileName.toStdString()<<" ext="<<f.extension.toStdString()<<" header="<<f.needsHeaderRead<<" clip="<<f.clipName.toStdString()<<" mob="<<f.masterMobId.toStdString()<<'\n';
 const QString standalone=tmp.path()+"/Archive"; QDir().mkpath(standalone); QFile::copy(fixture,standalone+"/tone.mxf");
 std::cout<<"STANDALONE rows="<<scan(standalone,true).size()<<'\n';
 const QString sibling=tmp.path()+"/Avid MediaFiles/MXF/2"; QDir().mkpath(sibling); QFile::copy(fixture,sibling+"/other.mxf");
 std::cout<<"SINGLE_FOLDER rows="<<scan(folder,true).size()<<'\n';
 MediaFile absent,named; absent.filePath="/unknown.mxf"; named.filePath="/named.mxf"; named.project="No project";
 MediaTableModel model; model.setMediaFiles({absent,named}); MediaFilterProxy proxy; proxy.setSourceModel(&model); proxy.setProjectFilter({"No project"});
 std::cout<<"PROJECT_SENTINEL selected="<<proxy.rowCount()<<" unknown="<<absent.hasNoProject()<<" named_unknown="<<named.hasNoProject()<<'\n';
 MediaFile df,ndf; df.clipName="DF"; ndf.clipName="NDF";df.fps="29.97";ndf.fps="30";df.kind=ndf.kind=MediaFile::Kind::Video; df.durationFrames=107893; df.timecodeBase=30;df.dropFrame=true;ndf.durationFrames=108000;ndf.timecodeBase=30;
 model.setMediaFiles({df,ndf}); proxy.setProjectFilter({});proxy.sort(int(MediaTableModel::Column::Duration));
 std::cout<<"DURATION_SORT "<<proxy.index(0,0).data().toString().toStdString()<<"="<<proxy.index(0,int(MediaTableModel::Column::Duration)).data().toString().toStdString()<<" then "<<proxy.index(1,0).data().toString().toStdString()<<"="<<proxy.index(1,int(MediaTableModel::Column::Duration)).data().toString().toStdString()<<'\n';
 AvbBin avb;avb.valid=avb.complete=true;AvbMob master;master.mobType=AvbMob::masterMobType;master.mobId=OmfUid::canonicalHex(QByteArray::fromHex("2a0000001122334455667788"));master.name="FIRST MASTER ONLY";master.originalBin="First master bin";avb.mobs.append(master);
 MediaFile unrelated;unrelated.masterMobId=OmfUid::canonicalHex(QByteArray::fromHex("2a0000004433221166558877"));MediaTableModel enriched;enriched.setMediaFiles({unrelated});enriched.setAvbBins({avb});std::cout<<"OMF_WRONG_ENRICH distinct="<<(master.mobId!=unrelated.masterMobId)<<" master="<<master.mobId.toStdString()<<" row="<<unrelated.masterMobId.toStdString()<<" adopted_name="<<enriched.fileAt(0).clipName.toStdString()<<" adopted_bin="<<enriched.fileAt(0).originalBin.toStdString()<<'\n';
 std::cout<<"MODEL_PARENT rows="<<model.rowCount(model.index(0,0))<<" cols="<<model.columnCount(model.index(0,0))<<'\n';
 for(int n:{10000,20000,40000}) {
  QVector<MediaFile> data; data.reserve(n);QSet<QString> remove;
  for(int i=0;i<n;++i){MediaFile f;f.filePath=QString::number(i);data.push_back(f);if(i%2==0)remove.insert(f.filePath);}
  MediaTableModel perf;perf.setMediaFiles(data);data.clear();QElapsedTimer t;t.start();perf.removeFilesByPath(remove);
  std::cout<<"REMOVE_PERF n="<<n<<" elapsed_ms="<<t.elapsed()<<'\n';
 }
 const QString output=tmp.path()+"/report.csv";
 {QFile f(output);f.open(QIODevice::WriteOnly);f.write("ORIGINAL REPORT");}
 struct rlimit oldLimit{};getrlimit(RLIMIT_FSIZE,&oldLimit);struct rlimit small=oldLimit;small.rlim_cur=8;signal(SIGXFSZ,SIG_IGN);if(setrlimit(RLIMIT_FSIZE,&small))return 3;
 bool ok=MediaCsv::write(output,{named});setrlimit(RLIMIT_FSIZE,&oldLimit);
 QFile result(output);result.open(QIODevice::ReadOnly);std::cout<<"CSV_WRITE returned="<<ok<<" expected_bytes="<<(3+MediaCsv::headerLine().toUtf8().size()+MediaCsv::rowLine(named).toUtf8().size())<<" actual_bytes="<<result.size()<<" contents="<<result.readAll().toHex().toStdString()<<'\n';
}
