#include "mxfparser.h"
#include "omfparser.h"
#include "pmrparser.h"
#include "mdbparser.h"
#include "mobid.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QTextStream>
static QJsonObject identity(const QString& file,const QString& master){return {{"fileId",file},{"masterId",master},{"fileZero",MobId::isAllZero(file)},{"masterZero",MobId::isAllZero(master)}};}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv); QTextStream out(stdout);
 QString base=QDir(argc>1?QString::fromLocal8Bit(argv[1]):"tests/fixtures").absolutePath();
 QDirIterator it(base,QDir::Files,QDirIterator::Subdirectories);
 auto emitRow=[&out](QJsonObject o){out<<QJsonDocument(o).toJson(QJsonDocument::Compact)<<'\n';};
 while(it.hasNext()){
 const QString path=it.next(); const auto fi=it.fileInfo(); QString ext=fi.suffix().toLower();
 if(argc>2 && QString::fromLocal8Bit(argv[2])=="mxf" && ext!="mxf")continue;
 if(!QStringList{"mxf","omf","aif","wav","pmr","mdb"}.contains(ext))continue;
 QJsonObject obj{{"path",QDir(base).relativeFilePath(path)},{"kind",ext},{"size",fi.size()}};
 QFile file(path); if(file.open(QIODevice::ReadOnly)) {QCryptographicHash h(QCryptographicHash::Sha256);h.addData(&file);obj["sha256"]=QString::fromLatin1(h.result().toHex());}
 if(ext=="pmr"){
 bool ok; auto rows=PmrParser::parse(path,&ok); obj["ok"]=ok;obj["records"]=rows.size();emitRow(obj);
 for(const auto&e:rows){auto row=identity(e.mobId,e.masterMobId);row["kind"]="pmr-entry";row["path"]=obj["path"];row["filename"]=e.fileName;emitRow(row);}continue;}
 if(ext=="mdb"){
 bool ok;auto db=MdbParser::load(path,&ok);obj["ok"]=ok;obj["files"]=db.files.size();obj["masters"]=db.masters.size();emitRow(obj);
 for(const auto&e:db.files){auto row=identity(e.mobIdHex,e.masterMobId);row["kind"]="mdb-file";row["path"]=obj["path"];row["essenceComplete"]=e.essenceComplete;emitRow(row);}continue;}
 MediaMetadata md;
 if(ext=="mxf")md=MxfParser::parseHeader(path);
 else{auto omf=OmfParser::parseHeader(path);md=omf.essence;md.fileMobId=omf.fileMobId;obj["hasMediaDescriptor"]=omf.hasMediaDescriptor;}
 auto ids=identity(md.fileMobId,md.umid);for(auto key:ids.keys())obj[key]=ids[key];
 obj["headerStatus"]=int(md.headerStatus);obj["valid"]=md.valid;obj["hasMaterialPackage"]=md.hasMaterialPackage;obj["classificationKnown"]=md.classificationKnown;obj["clipName"]=md.clipName;emitRow(obj);
 }
}
