#include "mediacsv.h"
#include "mediatablemodel.h"
#include "mediafilterproxy.h"
#include "effectfilterdialog.h"
#include "pathkey.h"
#include <QFile>
#include <QApplication>
#include <QFileInfo>
#include <QDebug>
#include <QAbstractItemModelTester>
#include <QTreeWidget>
#include <sys/resource.h>
#include <signal.h>
int main(int argc, char** argv) {
 QApplication app(argc, argv);
 MediaFile df,ndf;
 df.clipName="DF"; df.durationFrames=17982; df.timecodeBase=30; df.dropFrame=true;
 ndf.clipName="NDF"; ndf.durationFrames=17990; ndf.timecodeBase=30;
 MediaTableModel model;
 model.setMediaFiles({df,ndf});
 MediaFilterProxy proxy;
 proxy.setSourceModel(&model);
 proxy.sort(int(MediaTableModel::Column::Duration));
 for(int i=0;i<proxy.rowCount();++i) qInfo()<<"ascending duration"<<proxy.index(i,0).data().toString()<<proxy.index(i,int(MediaTableModel::Column::Duration)).data().toString();
 qInfo()<<"valid parent rowCount/columnCount"<<model.rowCount(model.index(0,0))<<model.columnCount(model.index(0,0));
 QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::Warning);
 QString nfcPath="/tmp/mediamuster-review-20260922/scanner/caf\u00e9.mxf";
 QString nfdPath="/tmp/mediamuster-review-20260922/scanner/cafe\u0301.mxf";
 QFile nf(nfcPath); nf.open(QIODevice::WriteOnly); nf.write("x"); nf.close();
 qInfo()<<"NFC and NFD same canonical leaf"<<(QFileInfo(nfcPath).canonicalFilePath()==QFileInfo(nfdPath).canonicalFilePath())<<"same path key"<<(PathKey::normalise(nfcPath)==PathKey::normalise(nfdPath));
 rlimit oldLimit, limit;
 getrlimit(RLIMIT_FSIZE, &oldLimit); limit=oldLimit; limit.rlim_cur=0;
 signal(SIGXFSZ, SIG_IGN);
 setrlimit(RLIMIT_FSIZE,&limit);
 QString path="/tmp/mediamuster-review-20260922/scanner/write-denied.csv";
 bool ok=MediaCsv::write(path,{df});
 setrlimit(RLIMIT_FSIZE,&oldLimit);
 qInfo()<<"CSV write with zero-byte file limit returned"<<ok<<"file bytes"<<QFileInfo(path).size();
 return 0;
}
