
#include "pmrparser.h"
#include "mdbparser.h"
#include "mxfparser.h"
#include "omfparser.h"
#include "avbparser.h"
#include "bentofile.h"
#include "omfobjects.h"
#include "avidtext.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QTextStream>
QJsonObject metadata(const MediaMetadata&m){return {{"valid",m.valid},{"status",int(m.headerStatus)},{"fileId",m.fileMobId},{"masterId",m.umid},{"clip",m.clipName},{"project",m.projectName},{"codec",m.codec},{"width",m.width},{"height",m.height},{"layout",m.frameLayout},{"rate",m.frameRate},{"bits",m.bitDepth},{"samplesPerSecond",m.sampleRate},{"channels",m.channels},{"duration",QString::number(m.duration.units)},{"durationRate",m.duration.rate.value()},{"durationSource",int(m.duration.source)},{"typeKnown",m.classificationKnown},{"precompute",m.isPrecompute},{"category",int(m.precomputeCategory)}};}
int main(int argc,char**argv){QCoreApplication app(argc,argv);QTextStream in(stdin),out(stdout);if(argc>1){QJsonArray a;for(int n:{1237,1244,1256,1258,1259,1260,1270,1489}){auto u=OmfObjects::ulFromResId(n);a.append(QJsonObject{{"resolutionId",n},{"fabricatedUl",QString::fromLatin1(u.toHex())},{"codec",MediaMetadataUtil::codecFromCompressionLabel(u,"25")}});}QJsonObject o{{"resolutionFallbacks",a},{"legacyBytesC3A9",AvidText::decode("\xc3\xa9",2)},{"directMacRomanC3A9",QString::fromUtf8("√©")}};out<<QJsonDocument(o).toJson(QJsonDocument::Compact)<<Qt::endl;return 0;}while(!in.atEnd()){QString path=in.readLine();if(path.isEmpty())continue;QJsonObject o{{"path",path},{"size",QString::number(QFileInfo(path).size())}};QString ext=QFileInfo(path).suffix().toLower();qint64 bytes=0;
if(ext=="pmr"){bool ok=false;auto v=PmrParser::parse(path,&ok);o["ok"]=ok;o["count"]=v.size();QJsonArray a;for(auto&r:v)a.append(QJsonObject{{"name",r.fileName},{"fileId",r.fileMobId},{"masterId",r.masterMobId},{"project",r.project},{"mtime",double(r.fileModifiedSecs)}});o["records"]=a;}
else if(ext=="mdb"){bool ok=false;auto db=MdbParser::load(path,&ok);o["ok"]=ok;o["revision"]=int(db.revision);o["masters"]=db.masters.size();o["files"]=db.files.size();QJsonArray a;for(auto i=db.files.cbegin();i!=db.files.cend();++i){auto r=metadata(i->essence);r["fileId"]=i.key();r["complete"]=i->essenceComplete;r["project"]=i->project;r["masterId"]=i->masterMobId;a.append(r);}o["records"]=a;}
else if(ext=="avb"){auto b=AvbParser::parse(path);o["ok"]=b.valid;o["complete"]=b.complete;o["error"]=b.error;QJsonArray w;for(auto&s:b.warnings)w.append(s);o["warnings"]=w;QJsonArray a;for(auto&m:b.mobs)a.append(QJsonObject{{"id",m.mobId},{"name",m.name},{"type",int(m.mobType)},{"usage",m.usageCode},{"bin",m.originalBin}});o["mobs"]=a;QJsonArray ids;for(auto&s:b.mediaFileIds.fullIds)ids.append(s);o["fileIds"]=ids;}
else if(ext=="mxf"){o["metadata"]=metadata(MxfParser::parseHeader(path,&bytes));o["bytesRead"]=QString::number(bytes);}
else{auto m=OmfParser::parseHeader(path,&bytes);o["metadata"]=metadata(m.essence);o["bytesRead"]=QString::number(bytes);}
out<<QJsonDocument(o).toJson(QJsonDocument::Compact)<<Qt::endl;}}
