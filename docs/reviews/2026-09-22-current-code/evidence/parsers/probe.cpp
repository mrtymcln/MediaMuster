#include "bentofile.h"
#include "omfobjects.h"
#include "mdbparser.h"
#include "mediafile.h"
#include "mobid.h"
#include "mxfparser.h"
#include "testbento.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
static QByteArray u16(quint16 v){QByteArray b(2, '\0'); qToBigEndian(v,b.data());return b;}
static QByteArray u32(quint32 v){QByteArray b(4, '\0'); qToBigEndian(v,b.data());return b;}
static QByteArray field(quint16 tag,QByteArray v){return u16(tag)+u16(v.size())+v;}
static QByteArray set(quint8 type,QByteArray v){QByteArray b=QByteArray::fromHex("060e2b34025301010d01010101010000");b[14]=char(type);return b+QByteArray(1,'\x82')+u16(v.size())+v;}
static void write(QString path,QByteArray b){QFile f(path);f.open(QIODevice::WriteOnly);f.write(b);}
int main(int argc,char**argv){QCoreApplication app(argc,argv);
 for(quint32 samples:{4800u, 9600u, 47040u, 48000u}) {
  BentoBuilder w;const auto file=w.addObject("MOBJ"),desc=w.addObject("PCMA");
  const QByteArray id(32,'\x11'); w.set(file,"OMFI:MOBJ:MobID",id);w.setHandle(file,"OMFI:MOBJ:PhysicalMedia",desc);
  w.setRational(desc,"OMFI:MDFL:SampleRate",48000,1);w.setRational(file,"OMFI:CPNT:EditRate",25,1);
  w.setU32(desc,"OMFI:MDFL:Length",samples);w.setU16(desc,"OMFI:MDAU:BitsPerSample",24);w.setU16(desc,"OMFI:MDAU:NumChannels",2);
  auto path=QString("/tmp/mediamuster-review-20260922/parsers/short-%1.mdb").arg(samples);write(path,w.build());
  bool ok=false; auto db=MdbParser::load(path,&ok);auto f=db.files.value(MobId::format(id));
  MediaFile mf; mf.durationFrames=f.essence.durationFrames; mf.timecodeBase=f.essence.timecodeBase; qInfo()<<"MDB"<<samples<<"ok"<<ok<<"complete"<<f.essenceComplete<<"frames"<<f.essence.durationFrames<<"base"<<f.essence.timecodeBase<<"display"<<mf.durationDisplay();
 }
 QByteArray materialId(32,'\x22'),fileId(32,'\x33'),descUid(16,'\x44');
 QByteArray material=field(0x4401,materialId)+field(0x4402,QByteArray::fromHex("00570072006f006e00670020006d006100730074006500720000"))+field(0x4403,u32(0)+u32(16));
 QByteArray file=field(0x4401,fileId)+field(0x4701,descUid);
 QByteArray desc=field(0x3c0a,descUid)+field(0x3203,u32(1920))+field(0x3202,u32(1080))+field(0x3001,u32(25)+u32(1));
 QString path="/tmp/mediamuster-review-20260922/parsers/unrelated-master.mxf";write(path,set(0x36,material)+set(0x37,file)+set(0x28,desc));
 auto m=MxfParser::parseHeader(path);qInfo()<<"MXF unrelated"<<"valid"<<m.valid<<"status"<<int(m.headerStatus)<<"hasMaterial"<<m.hasMaterialPackage<<"classificationKnown"<<m.classificationKnown<<"name"<<m.clipName;
}
