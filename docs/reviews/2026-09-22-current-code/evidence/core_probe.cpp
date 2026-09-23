#include "logfile.h"
#include "revealinfinder.h"
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <iostream>
static void put(const QString &path,const char* bytes) { QFile f(path); if(!f.open(QIODevice::WriteOnly)) std::abort(); f.write(bytes); }
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 QStandardPaths::setTestModeEnabled(true);
 app.setApplicationName("MediaMusterReview20260922-"+QString::number(QCoreApplication::applicationPid()));
 const QString dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
 if(!dir.contains(".qttest")) return 2;
 QDir().mkpath(dir+"/logs");
 put(dir+"/mediamuster.log","current log\n");
 put(dir+"/logs/mediamuster.log","old unique log history\n");
 put(dir+"/logs/MediaMuster-old.ips","saved crash\n");
 AppLog::install();
 std::cout<<"legacy-log-exists="<<QFileInfo::exists(dir+"/logs/mediamuster.log")<<" saved-crash-exists="<<QFileInfo::exists(dir+"/logs/MediaMuster-old.ips")<<'\n';
 QFile f(dir+"/mediamuster.log"); f.open(QIODevice::ReadOnly);
 std::cout<<"old-history-preserved="<<f.readAll().contains("old unique log history")<<'\n';
 std::cout<<"invalid-expiry-isValid="<<QDate::fromString("2026-02-31",Qt::ISODate).isValid()<<'\n';
 std::cout<<"detached-failing-command-reported="<<QProcess::startDetached("/usr/bin/false",{})<<'\n';
 QDir(dir).removeRecursively();
}
