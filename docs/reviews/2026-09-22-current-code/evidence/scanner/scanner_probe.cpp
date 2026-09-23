#include "mediascanner.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QDebug>
int main(int argc,char**argv) {
 QCoreApplication app(argc,argv);
 QTemporaryDir tmp("/tmp/mediamuster-review-20260922/scanner/quarantine-XXXXXX");
 QString q=tmp.path()+"/Avid MediaFiles/MXF/Quarantined Files";
 QDir().mkpath(q+"/Old");
 const QString fx="/Users/martymclean/Developer/MediaMuster/tests/fixtures/";
 for(const QString file : {QString("msmFMID.pmr"), QString("msmMMOB.mdb")}) QFile::copy(fx+file,q+"/"+file);
 QString tone="TONE_100A01.EA7D504A.611740.mxf";
 QFile::copy(fx+tone,q+"/"+tone);
 QFile::copy(fx+tone,q+"/Old/"+tone);
 MediaScanner scanner; QEventLoop loop;
 QObject::connect(&scanner,&MediaScanner::scanFinished,&loop,[&](const QVector<MediaFile>&rows){
 for(const MediaFile &row:rows) qInfo()<<"relative"<<QDir(q).relativeFilePath(row.filePath)<<"PMR exists in containing folder"<<QFileInfo::exists(QFileInfo(row.filePath).absolutePath()+"/msmFMID.pmr")<<"db status"<<row.dbStatusText().label<<"project"<<row.project;
 loop.quit();});
 MediaScanner::Options opt; opt.volumePaths={tmp.path()}; scanner.startScan(opt); loop.exec();
}
