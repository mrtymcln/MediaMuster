#include "bella/mdbparser.h"
#include "oldscanner/mediaengine/mdbreader.h"
#include "oldscanner/mediaengine/projection.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <type_traits>
using namespace Bella;
template<class T> QJsonValue opt(const std::optional<T>&v){return v?QJsonValue(double(*v)):QJsonValue();}
template<class T> QString hex(const T &v){return QByteArray(reinterpret_cast<const char*>(v.data()),v.size()).toHex();}
QString id(const MobId&v){return QByteArray(v.bytes().data(),v.bytes().size()).toHex();}
QJsonObject words(const MdbRateWords &r){return {{"n",r.numerator},{"d",r.denominator}};}
QJsonValue rate(const std::optional<MdbRate>&v){if(!v)return {};auto r=words(v->words);r["encoding"]=int(v->encoding);return r;}
QJsonObject dimensions(const MdbDimensions &v){return {{"w",double(v.width)},{"h",double(v.height)}};}
QJsonObject bella(const QString &path,bool details){
 std::atomic_bool stop{false};QElapsedTimer timer;timer.start();auto r=parseMdb(path,stop,details);auto ns=timer.nsecsElapsed();
 if(qEnvironmentVariableIsSet("MDB_AUDIT_COUNT"))return {{"path",path},{"reading",int(r.reading)},{"filesCount",r.catalogue.files.size()},{"mastersCount",r.catalogue.masters.size()},{"parseNs",double(ns)}};
 QJsonArray files,masters;
 for(const auto &f:r.catalogue.files){
  QJsonArray links;for(auto i:f.directMasterRecords)links.append(double(i));
  QJsonObject o{{"id",id(f.fileMobId)},{"idKind",int(f.fileMobId.kind())},{"project",f.project},{"projectSelection",int(f.projectSelection)},{"projectKey",int(f.projectKey)},
   {"kind",int(f.kind)},{"descriptorQualified",f.ownDescriptorQualified},{"descriptorClass",f.descriptorClass?QString::fromLatin1(reinterpret_cast<const char*>(f.descriptorClass->data()),4):QString()},
   {"length",opt(f.length)},{"sampleRate",rate(f.sampleRate)},{"audioEditRate",rate(f.audioEditRate)},{"numbering",int(f.numbering)},{"masterIndices",links},{"masterLinksComplete",f.masterLinksComplete}};
  if(f.audioFrequency)o["audioFrequency"]=std::visit([](const auto &v)->QJsonValue{using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,MdbRateWords>)return words(v);else if constexpr(std::is_same_v<T,quint32>)return double(v);else return hex(v);},*f.audioFrequency);
  if(f.resolution){auto d=dimensions(f.resolution->dimensions);d["origin"]=int(f.resolution->origin);o["resolution"]=d;}
  QJsonObject depth{{"bits",opt(f.bitDepth.bits)},{"storageBits",opt(f.bitDepth.storageBits)},{"representation",int(f.bitDepth.representation)},{"rawComponentWidth",opt(f.bitDepth.rawComponentWidth)}};
  if(f.bitDepth.pixelLayout)depth["pixelLayout"]=hex(f.bitDepth.pixelLayout->values).left(f.bitDepth.pixelLayout->count*2);
  if(f.bitDepth.pixelStructure)depth["pixelStructure"]=hex(f.bitDepth.pixelStructure->values).left(f.bitDepth.pixelStructure->count*2);
  o["bitDepth"]=depth;
  const auto &c=f.compression;QJsonObject compression{{"codingState",int(c.codingState)},{"codingLabel",hex(c.codingLabel)},{"resolutionId",opt(c.resolutionId)},{"frameLayout",opt(c.frameLayout)},{"horizontalSubsampling",opt(c.horizontalSubsampling)},{"verticalSubsampling",opt(c.verticalSubsampling)},{"audioEncoding",int(c.audioEncoding)}};
  if(c.legacyCompression)compression["legacy"]=hex(*c.legacyCompression);if(c.namingGeometry)compression["namingGeometry"]=dimensions(*c.namingGeometry);o["compression"]=compression;files.append(o);
 }
 for(const auto&m:r.catalogue.masters)masters.append(QJsonObject{{"id",id(m.masterMobId)},{"idKind",int(m.masterMobId.kind())},{"clipName",m.clipName},{"originalBin",m.originalBin},{"type",int(m.type)},{"precomputeCategory",int(m.precomputeCategory)}});
 return {{"path",path},{"reading",int(r.reading)},{"sizeDateCheck",int(r.sizeDateCheck)},{"fileReferencesComplete",r.catalogue.fileReferencesComplete},{"notes",QJsonArray::fromStringList(r.problemNotes)},{"files",files},{"masters",masters},{"parseNs",double(ns)},{"includePrecomputeDetails",details}};
}
QJsonValue variant(const QVariant &v){if(v.metaType().id()==QMetaType::QByteArray)return QStringLiteral("hex:")+v.toByteArray().toHex();return QJsonValue::fromVariant(v);}
QJsonObject oldscanner(const QString &path,bool graph){
 using namespace MediaEngine;QFile file(path);if(!file.open(QIODevice::ReadOnly))return {{"path",path},{"error",file.errorString()}};
 Cancellation cancellation;auto snapshot=QSharedPointer<SourceSnapshot>::create();snapshot->source=MetadataSource::Mdb;snapshot->path=path;snapshot->modified=QFileInfo(path).lastModified();snapshot->readState=SourceReadState::Complete;
 QElapsedTimer timer;timer.start();auto raw=MdbReader().read(file,{snapshot,cancellation});auto projection=projectMdb(raw,cancellation);auto ns=timer.nsecsElapsed();
 if(qEnvironmentVariableIsSet("MDB_AUDIT_COUNT"))return {{"path",path},{"reading",int(raw.outcome)},{"filesCount",projection.files.size()},{"mastersCount",projection.masters.size()},{"parseNs",double(ns)}};
 auto serialize=[&](const ProjectedFile &f){QJsonObject fields,observations;auto selected=selectedProjectedEvidence(f,projection);
  for(int i=0;i<int(MediaProperty::Count);++i){auto p=MediaProperty(i);auto s=selected.selected(p);auto name=mediaPropertyName(p);fields[name]=QJsonObject{{"value",variant(s.value)},{"state",int(s.readState)},{"agreement",int(s.agreement)},{"reason",int(s.readReason)}};QJsonArray obs;for(const auto &o:f.evidence.observations(p))obs.append(QJsonObject{{"property",o.property},{"owner",o.objectIdentity},{"value",variant(o.value)},{"raw",variant(o.rawValue)},{"eligible",o.eligible},{"state",int(o.readState)},{"explanation",o.explanation}});if(!obs.isEmpty())observations[name]=obs;}
  QJsonArray owners;for(const auto &o:f.objects)owners.append(double(o.handle));
  return QJsonObject{{"id",f.fileMobId},{"masterIds",QJsonArray::fromStringList(f.masterMobIds)},{"owners",owners},{"fields",fields},{"observations",observations}};};
 QJsonArray files,masters;for(const auto&f:projection.files)files.append(serialize(f));for(const auto&f:projection.masters)masters.append(serialize(f));
 QJsonObject result{{"path",path},{"outcome",int(raw.outcome)},{"fileReferencesComplete",projection.fileReferencesComplete},{"notes",QJsonArray::fromStringList(raw.diagnostics+projection.diagnostics)},{"files",files},{"masters",masters},{"parseNs",double(ns)},{"objectCount",raw.objects.size()}};
 if(graph){QJsonArray objects,relationships;for(const auto&o:raw.objects){QJsonArray properties;for(const auto&p:o.properties)properties.append(QJsonObject{{"name",p.locator.name},{"bytes",p.encoding.toHex().constData()},{"decoded",variant(p.decoded)},{"state",int(p.state)},{"type",p.bento?p.bento->typeName:QString()}});objects.append(QJsonObject{{"handle",double(o.handle)},{"role",int(o.role)},{"recordedIdentity",o.recordedIdentity.toHex().constData()},{"identityEncoding",o.identityEncoding},{"properties",properties}});}for(const auto&r:raw.relationships)relationships.append(QJsonObject{{"from",double(r.origin)},{"to",double(r.target)},{"property",r.locator.name}});result["objects"]=objects;result["relationships"]=relationships;}
 return result;
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc<3)return 2;auto mode=QString::fromLocal8Bit(argv[1]);auto path=QString::fromLocal8Bit(argv[2]);auto result=mode.startsWith("bella")?bella(path,mode!="bella-fast"):oldscanner(path,mode=="old-graph");auto data=QJsonDocument(result).toJson(QJsonDocument::Compact);fwrite(data.data(),1,data.size(),stdout);fputc('\n',stdout);return 0;}
