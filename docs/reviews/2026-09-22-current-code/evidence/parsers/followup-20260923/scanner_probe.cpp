#include "mediascanner.h"
#include "mxfparser.h"
#include "mobid.h"
#include "testbento.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
static QByteArray be16(quint16 v){QByteArray b(2, '\0');qToBigEndian(v,b.data());return b;}
static QByteArray be32(quint32 v){QByteArray b(4, '\0');qToBigEndian(v,b.data());return b;}
static QByteArray be64(quint64 v){QByteArray b(8, '\0');qToBigEndian(v,b.data());return b;}
static QByteArray field(quint16 tag,QByteArray v){return be16(tag)+be16(v.size())+v;}
static QByteArray set(quint8 type,QByteArray v){QByteArray b=QByteArray::fromHex("060e2b34025301010d01010101010000");b[14]=char(type);return b+QByteArray(1,'\x82')+be16(v.size())+v;}
static QByteArray utf16(QString s){QByteArray b;for(QChar c:s)b+=be16(c.unicode());return b+be16(0);}
static QByteArray refs(QByteArray id){return be32(1)+be32(16)+id;}
static QByteArray swap(QByteArray b){auto*p=reinterpret_cast<unsigned char*>(b.data());MobId::swapMiddleFields(p,p);return b;}
static void write(QString path,QByteArray b){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(b)!=b.size())qFatal("write failed");}
static QByteArray pmr(QByteArray name,QByteArray file,QByteArray master,quint32 stamp){auto le=BentoBuilder::le32;QByteArray project("Probe project");return le(0x7a9)+le(8)+le(1)+file+le(name.size()).first(2)+name+le(project.size()).first(2)+project+master+le(stamp);}
static QByteArray header(QByteArray fileId,QByteArray masterId,bool connected,bool graphDeclared=true){
 const QByteArray descId(16,'\x44'),trackId(16,'\x55'),clipId(16,'\x66'),masterInstance(16,'\x77'),fileInstance(16,'\x88');
 QByteArray material=field(0x3c0a,masterInstance)+field(0x4401,masterId)+field(0x4402,utf16(connected?"Connected clip":"Disconnected clip"));
 if(graphDeclared)material+=field(0x4403,refs(trackId));
 QByteArray file=field(0x3c0a,fileInstance)+field(0x4401,fileId)+field(0x4701,descId);
 QByteArray descriptor=field(0x3c0a,descId)+field(0x3001,be32(48000)+be32(1))+field(0x3002,be64(47040))+field(0x3d01,be32(24))+field(0x3d07,be32(2));
 QByteArray track=field(0x3c0a,trackId)+field(0x4b01,be32(25)+be32(1))+field(0x4803,clipId);
 QByteArray clip=field(0x3c0a,clipId)+field(0x0202,be64(25))+field(0x1101,connected?fileId:QByteArray(32,'\x99'));
 QByteArray metadata=set(0x36,material)+set(0x37,file)+set(0x48,descriptor)+set(0x3b,track)+set(0x11,clip);
 QByteArray pack(88,'\0');qToBigEndian<quint16>(1,pack.data());qToBigEndian<quint16>(3,pack.data()+2);qToBigEndian<quint32>(1,pack.data()+4);qToBigEndian<quint64>(metadata.size(),pack.data()+32);
 return QByteArray::fromHex("060e2b34020501010d01020101020400")+QByteArray(1,char(pack.size()))+pack+metadata;
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);
 const QString base=argc>1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/mediamuster-review-20260923/followup/parsers");
 QDir().mkpath(base);
 const QString root=base+"/scan",folder=root+"/Avid MediaFiles/MXF/1";QDir().mkpath(folder);
 const QByteArray fileId=QByteArray::fromHex("060a2b340101010501010f10130000004a507dea741106907a361e6a605d3613"),masterId=QByteArray::fromHex("060a2b340101010501010f1013000000d2467dea7411069091901e6a605d3613");
 const QString media=folder+"/short.mxf";write(media,header(swap(fileId),swap(masterId),true));
 constexpr quint32 stamp=1780000000;{QFile f(media);if(!f.open(QIODevice::ReadWrite)||!f.setFileTime(QDateTime::fromSecsSinceEpoch(stamp),QFileDevice::FileModificationTime))qFatal("timestamp failed");}
 BentoBuilder w;const auto head=w.addObject("HEAD"),master=w.addObject("MOBJ"),file=w.addObject("MOBJ"),desc=w.addObject("PCMA"),track=w.addObject("TRAK"),clip=w.addObject("SCLP");
 w.setImmediate(head,"OMFI:Version",QByteArray::fromHex("0100"));w.set(master,"OMFI:MOBJ:MobID",masterId);w.setU32(master,"OMFI:MOBJ:UsageCode",7);w.setString(master,"OMFI:CPNT:Name","Connected clip");w.setHandles(master,"OMFI:TRKG:Tracks",{track});w.setHandle(track,"OMFI:TRAK:TrackComponent",clip);w.set(clip,"OMFI:SCLP:SourceID",fileId);
 w.set(file,"OMFI:MOBJ:MobID",fileId);w.setHandle(file,"OMFI:MOBJ:PhysicalMedia",desc);w.setRational(file,"OMFI:CPNT:EditRate",25,1);w.setRational(desc,"OMFI:MDFL:SampleRate",48000,1);w.setU32(desc,"OMFI:MDFL:Length",47040);w.setU16(desc,"OMFI:MDAU:BitsPerSample",24);w.setU16(desc,"OMFI:MDAU:NumChannels",2);
 write(folder+"/msmMMOB.mdb",w.build());write(folder+"/msmFMID.pmr",pmr("short.mxf",fileId,masterId,stamp));
 auto direct=MxfParser::parseHeader(media);MediaFile directRow;directRow.durationFrames=direct.durationFrames;directRow.timecodeBase=direct.timecodeBase;qInfo()<<"direct MXF header"<<"valid"<<direct.valid<<"frames"<<direct.durationFrames<<"base"<<direct.timecodeBase<<"display"<<directRow.durationDisplay();
 MediaScanner scanner;QEventLoop loop;QVector<MediaFile> results;QObject::connect(&scanner,&MediaScanner::scanFinished,&loop,[&](const auto&rows){results=rows;loop.quit();});QTimer::singleShot(10000,&loop,&QEventLoop::quit);MediaScanner::Options opts;opts.manualPaths={root};opts.includeOmf=false;scanner.startScan(opts);loop.exec();
 qInfo()<<"scan includeOmf=false"<<"rows"<<results.size();for(const auto&r:results)qInfo()<<r.fileName<<"omfEra"<<r.omfEra<<"databaseMetadataCurrent"<<r.databaseMetadataCurrent<<"needsHeaderRead"<<r.needsHeaderRead<<"frames"<<r.durationFrames<<"base"<<r.timecodeBase<<"display"<<r.durationDisplay();
 for(bool connected:{true,false}){QString p=base+(connected?"/connected.mxf":"/disconnected.mxf");write(p,header(swap(fileId),swap(masterId),connected));auto m=MxfParser::parseHeader(p);qInfo()<<(connected?"connected material graph":"disconnected material graph")<<"valid"<<m.valid<<"status"<<int(m.headerStatus)<<"material"<<m.hasMaterialPackage<<"known"<<m.classificationKnown<<"name"<<m.clipName;}
 const QString graphless=base+"/graphless.mxf";write(graphless,header(swap(fileId),swap(masterId),false,false));auto m=MxfParser::parseHeader(graphless);qInfo()<<"graphless recovery"<<"valid"<<m.valid<<"material"<<m.hasMaterialPackage<<"name"<<m.clipName;
}
