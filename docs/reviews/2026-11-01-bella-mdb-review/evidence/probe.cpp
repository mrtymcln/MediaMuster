// Temporary complete public-report comparison. No production source modifications.
#include "bella/mdbparser.h"
#include "bella/pmrparser.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <atomic>

using namespace Bella;
namespace {
QJsonObject text(const QString &s) { return {{"value",s},{"isNull",s.isNull()}}; }
QJsonArray texts(const QStringList &values) { QJsonArray a; for(const auto &v:values)a.append(text(v)); return a; }
QJsonObject date(const QDateTime &d) {
 return {{"isValid",d.isValid()},{"isNull",d.isNull()},{"timeSpec",int(d.timeSpec())},
         {"msecsSinceEpoch",d.isValid()?QJsonValue(d.toMSecsSinceEpoch()):QJsonValue{}},
         {"offsetFromUtc",d.isValid()?QJsonValue(d.offsetFromUtc()):QJsonValue{}},
         {"zoneIdHex",QString::fromLatin1(d.timeZone().id().toHex())}};
}
QJsonObject id(const MobId &v) { return {{"kind",int(v.kind())},{"bytesHex",QString::fromLatin1(QByteArray(v.bytes().data(),v.bytes().size()).toHex())}}; }
template<class T> QJsonValue number(const std::optional<T> &v) { return v?QJsonValue(qint64(*v)):QJsonValue{}; }
template<size_t N> QString bytes(const std::array<quint8,N>&v) { return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(v.data()),N).toHex()); }
QJsonObject words(const MdbRateWords &v) { return {{"numerator",v.numerator},{"denominator",v.denominator}}; }
QJsonValue rate(const std::optional<MdbRate>&v) { return v?QJsonValue(QJsonObject{{"words",words(v->words)},{"encoding",int(v->encoding)}}):QJsonValue{}; }
QJsonObject dimensions(const MdbDimensions &v) { return {{"width",v.width},{"height",v.height}}; }
QJsonValue components(const std::optional<MdbComponentArray>&v) {
 if(!v)return {};
 // Serialize all owned storage as well as count; preserve meaningful zeros and order.
 return QJsonObject{{"count",v->count},{"allValuesHex",bytes(v->values)}};
}
QJsonValue audio(const std::optional<MdbAudioSourceValue>&v) {
 if(!v)return {};
 QJsonValue value;
 if(const auto *p=std::get_if<MdbRateWords>(&*v))value=words(*p);
 else if(const auto *p=std::get_if<quint32>(&*v))value=qint64(*p);
 else value=bytes(std::get<std::array<quint8,10>>(*v));
 return QJsonObject{{"alternative",int(v->index())},{"value",value}};
}
QJsonObject depth(const MdbBitDepth&v) {
 return {{"bits",number(v.bits)},{"storageBits",number(v.storageBits)},{"representation",int(v.representation)},
         {"rawComponentWidth",number(v.rawComponentWidth)},{"pixelLayout",components(v.pixelLayout)},{"pixelStructure",components(v.pixelStructure)}};
}
QJsonObject compression(const MdbCompression&v) {
 return {{"codingState",int(v.codingState)},{"allCodingLabelBytesHex",bytes(v.codingLabel)},
         {"legacyCompression",v.legacyCompression?QJsonValue(bytes(*v.legacyCompression)):QJsonValue{}},
         {"resolutionId",number(v.resolutionId)},
         {"namingGeometry",v.namingGeometry?QJsonValue(dimensions(*v.namingGeometry)):QJsonValue{}},
         {"frameLayout",number(v.frameLayout)},{"horizontalSubsampling",number(v.horizontalSubsampling)},
         {"verticalSubsampling",number(v.verticalSubsampling)},{"audioEncoding",int(v.audioEncoding)}};
}
QJsonObject mdb(const MdbReport&r) {
 QJsonArray files,masters;
 for(const auto&v:r.catalogue.files) {
  QJsonArray links;for(const auto i:v.directMasterRecords)links.append(qint64(i));
  files.append(QJsonObject{{"fileMobId",id(v.fileMobId)},{"project",text(v.project)},
   {"projectSelection",int(v.projectSelection)},{"projectKey",int(v.projectKey)},{"kind",int(v.kind)},
   {"descriptorClass",v.descriptorClass?QJsonValue(bytes(*v.descriptorClass)):QJsonValue{}},
   {"ownDescriptorQualified",v.ownDescriptorQualified},{"length",number(v.length)},
   {"sampleRate",rate(v.sampleRate)},{"audioEditRate",rate(v.audioEditRate)},{"numbering",int(v.numbering)},
   {"audioFrequency",audio(v.audioFrequency)},
   {"resolution",v.resolution?QJsonValue(QJsonObject{{"dimensions",dimensions(v.resolution->dimensions)},{"origin",int(v.resolution->origin)}}):QJsonValue{}},
   {"bitDepth",depth(v.bitDepth)},{"compression",compression(v.compression)},
   {"directMasterRecords",links},{"masterLinksComplete",v.masterLinksComplete}});
 }
 for(const auto&v:r.catalogue.masters)masters.append(QJsonObject{{"masterMobId",id(v.masterMobId)},
  {"clipName",text(v.clipName)},{"originalBin",text(v.originalBin)},{"type",int(v.type)},{"precomputeCategory",int(v.precomputeCategory)}});
 return {{"path",text(r.path)},{"reading",int(r.reading)},{"sizeDateCheck",int(r.sizeDateCheck)},
         {"catalogue",QJsonObject{{"files",files},{"masters",masters},{"fileReferencesComplete",r.catalogue.fileReferencesComplete}}}};
}
QJsonObject pmr(const PmrReport&r) {
 QJsonValue known;
 if(r.records.knownSet) {
  QJsonArray entries;
  for(const auto&v:r.records.knownSet->entries)entries.append(QJsonObject{{"fileMobId",id(v.fileMobId)},
   {"filename",text(v.filename)},{"project",text(v.project)},
   {"masterMobId",v.masterMobId?QJsonValue(id(*v.masterMobId)):QJsonValue{}}});
  known=QJsonObject{{"version",r.records.knownSet->version},{"entries",entries}};
 }
 return {{"source",QJsonObject{{"path",text(r.source.path)},
              {"capturedStamp",QJsonObject{{"sizeBytes",number(r.source.capturedStamp.sizeBytes)},{"modified",date(r.source.capturedStamp.modified)}}}}},
         {"reading",int(r.reading)},{"sizeDateCheck",int(r.sizeDateCheck)},
         {"records",QJsonObject{{"knownSet",known},{"canUsePmrMetadata",r.records.canUsePmrMetadata},
                                {"referenceCoverageComplete",r.records.referenceCoverageComplete}}}};
}
}
int main(int argc,char**argv) {
 QCoreApplication app(argc,argv);
 if(app.arguments().size()!=2)return 2;
 QFile file(app.arguments()[1]);if(!file.open(QIODevice::ReadOnly))return 3;
 QJsonParseError err;const auto manifest=QJsonDocument::fromJson(file.readAll(),&err);
 if(err.error!=QJsonParseError::NoError||!manifest.isObject()||!manifest.object()["sourceInputs"].isArray())return 4;
 const std::atomic_bool stop{false};QJsonArray rows;
 for(const auto entry:manifest.object()["sourceInputs"].toArray()) {
  const auto e=entry.toObject();const QString path=e["path"].toString(),kind=e["kind"].toString();
  if(path.isEmpty()||(kind!="PMR"&&kind!="MDB"))return 5;
  if(kind=="PMR") {
   const auto r=parsePmr(path,stop);
   rows.append(QJsonObject{{"path",path},{"kind",kind},{"metadata",pmr(r)},{"diagnostics",texts(r.diagnostics)}});
  }else for(const bool details : e.contains("includePrecomputeDetails") ? QList<bool>{e["includePrecomputeDetails"].toBool()} : QList<bool>{true,false}) {
   const auto r=parseMdb(path,stop,details);
   rows.append(QJsonObject{{"path",path},{"kind",kind},{"includePrecomputeDetails",details},
                           {"metadata",mdb(r)},{"diagnostics",texts(r.problemNotes)}});
  }
 }
 QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 6;
 const auto result=QJsonDocument(QJsonObject{{"serializationVersion",1},{"rows",rows},
  {"scope","All public report values except diagnostic lists are in metadata; diagnostics separately preserve QString nullness, text, order and multiplicity."}}).toJson(QJsonDocument::Compact);
 return output.write(result)==result.size()?0:7;
}
