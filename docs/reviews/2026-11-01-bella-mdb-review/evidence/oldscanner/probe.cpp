#include "oldscanner/mediaengine/mdbreader.h"
#ifdef HAS_PROJECTION
#include "oldscanner/mediaengine/projection.h"
#endif
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
namespace E = MediaEngine;
#ifdef HAS_PROJECTION
QJsonArray dump(const QVector<E::ProjectedFile> &files) {
 QJsonArray list;
 for (const auto &file : files) {
  QJsonObject fields;
  for(int i=0;i<int(MediaProperty::Count);++i) {
   const auto field=MediaProperty(i);QJsonArray observations;
   for(const auto &v:file.evidence.observations(field)) observations.append(QJsonObject{{"property",v.property},{"object",v.objectIdentity},{"value",QJsonValue::fromVariant(v.value)},{"state",int(v.readState)},{"eligible",v.eligible}});
   if(!observations.isEmpty()) fields.insert(mediaPropertyName(field),observations);
  }
  list.append(QJsonObject{{"fileId",file.fileMobId},{"masterIds",QJsonArray::fromStringList(file.masterMobIds)},{"fields",fields}});
 }
 return list;
}
#endif
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile input(app.arguments()[1]);if(!input.open(QIODevice::ReadOnly))return 3;
 const E::Cancellation cancel;
 auto receipt=QSharedPointer<SourceSnapshot>::create();receipt->source=MetadataSource::Mdb;receipt->path=app.arguments()[1];
 const auto parsed=E::MdbReader{}.read(input,{receipt,cancel});
 qsizetype props=0;for(const auto &o:parsed.objects)props+=o.properties.size();
 QJsonObject result{{"path",app.arguments()[1]},{"outcome",int(parsed.outcome)},{"objects",qint64(parsed.objects.size())},{"properties",qint64(props)},{"relationships",qint64(parsed.relationships.size())},{"diagnostics",QJsonArray::fromStringList(parsed.diagnostics)}};
#ifdef HAS_PROJECTION
 const auto projected=E::projectMdb(parsed,cancel);
 result.insert("fileReferencesComplete",projected.fileReferencesComplete);
 result.insert("files",dump(projected.files));result.insert("masters",dump(projected.masters));result.insert("projectionDiagnostics",QJsonArray::fromStringList(projected.diagnostics));
#endif
 const QByteArray json=QJsonDocument(result).toJson(QJsonDocument::Compact);std::fwrite(json.data(),1,size_t(json.size()),stdout);return 0;
}
