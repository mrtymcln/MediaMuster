#include "mxfparser.h"
#include "mdbparser.h"
#include "pmrparser.h"
#include "omfparser.h"
#include "testbento.h"
#include "testavb.h"
#include "avbparser.h"
#include "binfilter.h"
#include "omfuid.h"
#include "avideffects.h"
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <QtEndian>
#include <sys/resource.h>

QByteArray be16(quint16 v) { QByteArray b(2, '\0'); qToBigEndian(v,b.data()); return b; }
QByteArray be32(quint32 v) { QByteArray b(4, '\0'); qToBigEndian(v,b.data()); return b; }
QByteArray prop(quint16 t, const QByteArray &b) { return be16(t)+be16(b.size())+b; }
QByteArray klv(QByteArray key,const QByteArray &v) { return key+QByteArray::fromHex("84")+be32(v.size())+v; }
QByteArray set(quint8 type,const QByteArray &v) { auto key=QByteArray::fromHex("060e2b34025301010d01010101010000"); key[14]=type; return klv(key,v); }
QString write(const QString &name,const QByteArray &v) { QString path=QCoreApplication::applicationDirPath()+"/"+name; QFile f(path); if(!f.open(QIODevice::WriteOnly) || f.write(v)!=v.size()) qFatal("write failed"); return path; }
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 if(argc>1) {
   bool ok=false;
   if(QString(argv[1])=="mdb") { auto d=MdbParser::load(argv[2],&ok); qInfo()<<"mdb ok"<<ok<<"files"<<d.files.size(); }
   else { auto d=PmrParser::parse(argv[2],&ok); qInfo()<<"pmr ok"<<ok<<"entries"<<d.size(); }
   rusage u{}; getrusage(RUSAGE_SELF,&u); qInfo()<<"maximum_rss_bytes"<<u.ru_maxrss;
   return 0;
 }
 const auto hit=AvidEffects::lookup(QStringLiteral("Seq,Farbeffekt+1"));
 qInfo()<<"german_colliding_alias_name"<<hit.name<<"category"<<hit.category<<"matched"<<hit.matched;
 const auto mask=AvidEffects::lookup(QStringLiteral("Seq,1,85_Maske+1"));
 qInfo()<<"german_mask_name"<<mask.name<<"category"<<mask.category<<"matched"<<mask.matched<<"sequence"<<mask.sequence;
 QByteArray pack=klv(QByteArray::fromHex("060e2b34020501010d01020101020400"),QByteArray(88,'\0'));
 const auto edit=prop(0x3001,be32(24)+be32(1));
 const auto audio=prop(0x3d03,be32(48000)+be32(1));
 for(bool reverse : {false,true}) {
  auto m=MxfParser::parseHeader(write(reverse?"audio-edit-last.mxf":"audio-sampling-last.mxf",pack+set(0x48, (reverse?audio+edit:edit+audio)+prop(0x3d07,be32(1))+prop(0x3d01,be32(24)))));
  qInfo()<<"edit_rate_last"<<reverse<<"sampleRate"<<m.sampleRate<<"valid"<<m.valid<<"headerStatus"<<int(m.headerStatus);
 }
 for(const QByteArray &coding: {QByteArray(1,'x'),QByteArray(15,'x'),QByteArray(16,'x')}) {
  const auto malformed=MxfParser::parseHeader(write("malformed-sound-coding.mxf",pack+set(0x48,audio+prop(0x3d06,coding))));
  qInfo()<<"sound_coding_bytes"<<coding.size()<<"codec"<<malformed.codec<<"valid"<<malformed.valid<<"status"<<int(malformed.headerStatus);
 }
 {
  const auto aUid=QByteArray::fromHex("2a0000001122334455667788");
  const auto bUid=QByteArray::fromHex("2a0000004433221166558877");
  const auto a=OmfUid::canonicalHex(aUid),b=OmfUid::canonicalHex(bUid);
  const auto raw=OmfUid::wrap8(reinterpret_cast<const unsigned char*>(aUid.constData()+4));
  auto bin=AvbParser::parse(write("omf-a-only.avb",TestAvb::masterBin({QByteArray(reinterpret_cast<const char*>(raw.data()),raw.size())})));
  qInfo()<<"omf_alias a"<<a<<"b"<<b<<"distinct"<<(a!=b)<<"valid"<<bin.valid<<"complete"<<bin.complete<<"bin_has_a"<<bin.mobIds.contains(a)<<"bin_has_unrelated_b"<<bin.mobIds.contains(b);
  BinFilter filter; filter.steps.append({BinFilter::Operation::Intersect, {}, bin.mobIds});
  qInfo()<<"bin_filter_unrelated_b_matches"<<filter.matches(b,{});
 }
 {
  BentoBuilder b;
  const auto head=b.addObject("HEAD"); b.setImmediate(head,"OMFI:Version",QByteArray::fromHex("0100"));
  const auto uid=[](quint32 n){return BentoBuilder::le32(42)+BentoBuilder::le32(n)+BentoBuilder::le32(100);};
  const auto master=b.addObject("MOBJ"), file=b.addObject("MOBJ"), src1=b.addObject("MOBJ"), src2=b.addObject("MOBJ");
  b.set(master,"OMFI:MOBJ:MobID",uid(1)); b.setU32(master,"OMFI:MOBJ:UsageCode",7); b.setString(master,"OMFI:CPNT:Name","master");
  b.set(file,"OMFI:MOBJ:MobID",uid(2)); b.setU32(file,"OMFI:MOBJ:UsageCode",0);
  b.set(src1,"OMFI:MOBJ:MobID",uid(3)); b.set(src2,"OMFI:MOBJ:MobID",uid(3));
  const auto desc=b.addObject("CDCI"), phys=b.addObject("MDES");
  b.setHandle(file,"OMFI:MOBJ:PhysicalMedia",desc); b.setHandle(src1,"OMFI:MOBJ:PhysicalMedia",phys); b.setHandle(src2,"OMFI:MOBJ:PhysicalMedia",phys);
  b.setU32(desc,"OMFI:DIDD:StoredWidth",1920); b.setU32(desc,"OMFI:DIDD:StoredHeight",1080); b.setRational(desc,"OMFI:MDFL:SampleRate",25,1); b.setU32(desc,"OMFI:MDFL:Length",100); b.setU32(desc,"OMFI:DIDD:DIDResolutionID",1235);
  const auto link=[&](quint32 owner,const QByteArray& target){auto track=b.addObject("TRAK"), clip=b.addObject("SCLP"); b.setHandles(owner,"OMFI:TRKG:Tracks",{track}); b.setHandle(track,"OMFI:TRAK:TrackComponent",clip); b.set(clip,"OMFI:SCLP:SourceID",target);};
  link(master,uid(2)); link(file,uid(3));
  const auto attrs=b.addObject("ATTR"), attb=b.addObject("ATTB"); b.setHandle(src2,"OMFI:CPNT:Attributes",attrs); b.setHandles(attrs,"OMFI:ATTR:AttrRefs",{attb}); b.setString(attb,"OMFI:ATTB:Name","_PJ"); b.setU16(attb,"OMFI:ATTB:Kind",2); b.setString(attb,"OMFI:ATTB:StringAttribute","Correct project");
  const auto data=b.addObject("JPEG"); b.set(data,"OMFI:MDAT:MobID",uid(2));
  const auto path=write("duplicate-source.mdb",b.build()); bool ok=false; const auto db=MdbParser::load(path,&ok); const auto omf=OmfParser::parseHeader(path);
  qInfo()<<"duplicate_source mdb_ok"<<ok<<"file_count"<<db.files.size()<<"mdb_project"<<db.files.cbegin()->project<<"essence_complete"<<db.files.cbegin()->essenceComplete<<"omf_project"<<omf.essence.projectName;
 }
}
