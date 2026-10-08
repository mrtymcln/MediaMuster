#include "canon/mxfreader.h"
#include "canon/mdbreader.h"
#include "canon/legacyreader.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <functional>

using namespace Canon;
QString objectClass(const AvidObject &object)
{
    if (object.mxf) return object.mxf->name;
    for (const auto &property : object.properties)
        if (property.locator.name == "OMFI:ObjID" || property.locator.name == "OMFI:OOBJ:ObjClass")
            return property.decoded.toString();
    return {};
}
QJsonObject inspect(const ParsedSource &source)
{
    QHash<ObjectHandle, const AvidObject *> objects;
    QHash<ObjectHandle, QVector<const Relationship *>> outgoing;
    QHash<QString, int> classes;
    for (const auto &object : source.objects) { objects.insert(object.handle, &object); ++classes[objectClass(object)]; }
    for (const auto &link : source.relationships) outgoing[link.origin].append(&link);
    QJsonObject classCounts; for (auto it=classes.cbegin();it!=classes.cend();++it) classCounts[it.key()]=it.value();
    QJsonObject result{{"outcome", int(source.outcome)}, {"container", int(source.container)},
        {"objects", source.objects.size()}, {"relationships", source.relationships.size()}, {"classes", classCounts},
        {"diagnostics", QJsonArray::fromStringList(source.diagnostics)}};
    QJsonArray roots;
    QSet<ObjectHandle> indexedMobs, indexedMedia, indexedPackages, indexedEcd, spine;
    for (const auto &object : source.objects)
    {
        const auto cls=objectClass(object);
        if (cls!="HEAD" && cls!="Preface" && cls!="ContentStorage" && cls!="EssenceContainerData") continue;
        QJsonObject root{{"object", qint64(object.handle)}, {"class", cls}};
        if (object.mxf) root["partitionOffset"]=object.mxf->partitionOffset;
        QJsonArray properties;
        for (const auto &property : object.properties)
        {
            const auto name=property.locator.name;
            if (cls=="HEAD" && !name.contains("Mob") && !name.contains("MediaData") && !name.contains("Spine") &&
                !name.contains("Version") && !name.contains("ObjID") && !name.contains("ObjClass") && !name.contains("ByteOrder")) continue;
            if (cls=="Preface" && name!="Preface.ContentStorage" && name!="Preface.PrimaryPackage" && name!="Preface.EssenceFileMobID") continue;
            if (cls=="ContentStorage" && name!="ContentStorage.Packages" && name!="ContentStorage.EssenceContainerData") continue;
            if (cls=="EssenceContainerData" && name!="EssenceContainerData.LinkedPackageUID") continue;
            QJsonObject p{{"name",name},{"state",int(property.state)},{"encodingBytes",property.encoding.size()},
                {"first48Hex",QString::fromLatin1(property.encoding.first(std::min(qsizetype(48),property.encoding.size())).toHex())},{"interpretation",property.interpretation}};
            if (property.mxf) p["type"]=property.mxf->typeName;
            if (property.bento) {p["type"]=property.bento->typeName;p["propertyId"]=qint64(property.bento->property);p["typeId"]=qint64(property.bento->type);}
            QHash<QString,int> targetClasses; int resolved=0, unresolved=0, null=0, entries=0;
            QJsonArray examples;
            for (const auto *link : outgoing.value(object.handle))
                if (link->locator.name==name)
                {
                    ++entries;
                    const auto *target=objects.value(link->target);
                    if (target) {++resolved;++targetClasses[objectClass(*target)];}
                    else if (link->explanation.contains("null",Qt::CaseInsensitive)) ++null;
                    else ++unresolved;
                    if (target && (name=="OMFI:SourceMobs"||name=="OMFI:CompositionMobs"||name=="OMFI:HEAD:Mobs")) indexedMobs.insert(link->target);
                    if (target && (name=="OMFI:MediaData"||name=="OMFI:HEAD:MediaData")) indexedMedia.insert(link->target);
                    if (target && name=="OMFI:ObjectSpine") spine.insert(link->target);
                    if (target && name=="ContentStorage.Packages") indexedPackages.insert(link->target);
                    if (target && name=="ContentStorage.EssenceContainerData") indexedEcd.insert(link->target);
                    if (examples.size()<5)
                    {
                        QJsonObject e{{"target",qint64(link->target)},{"class",target?objectClass(*target):QString{}},{"explanation",link->explanation}};
                        const auto record=link->recordedReference.toMap();
                        if (!record.isEmpty()) {e["index"]=qint64(record.value("index").toLongLong());e["key"]=qint64(record.value("key").toUInt());e["mobIdHex"]=QString::fromLatin1(record.value("mobId").toByteArray().toHex());}
                        examples.append(e);
                    }
                }
            QJsonObject tc; for(auto it=targetClasses.cbegin();it!=targetClasses.cend();++it)tc[it.key()]=it.value();
            p["entries"]=entries;p["resolved"]=resolved;p["unresolved"]=unresolved;p["null"]=null;p["targetClasses"]=tc;p["examples"]=examples;
            properties.append(p);
        }
        root["properties"]=properties; roots.append(root);
    }
    result["roots"]=roots;
    QJsonArray mobDetails, unindexedCandidates; int descriptorMobs=0,indexedDescriptorMobs=0,totalMedia=0,indexedMediaCount=0,unindexedPackages=0,unindexedEcd=0;
    QHash<QString,int> duplicateIdentities;
    for (const auto &object:source.objects)
    {
        const auto cls=objectClass(object);
        bool descriptorMob=false;
        for(const auto *link:outgoing.value(object.handle))
            descriptorMob|=link->locator.name=="OMFI:MOBJ:PhysicalMedia"||link->locator.name=="OMFI:SMOB:MediaDescription";
        if(descriptorMob)
        {
            ++descriptorMobs;
            if(indexedMobs.contains(object.handle))++indexedDescriptorMobs;
            else if(unindexedCandidates.size()<8)unindexedCandidates.append(QJsonObject{{"object",qint64(object.handle)},{"class",cls},{"identityHex",QString::fromLatin1(object.recordedIdentity.toHex())}});
        }
        if(source.objects.size()<3000 && !cls.isEmpty() && cls!="CLSD")
        {
            QJsonObject d{{"object",qint64(object.handle)},{"class",cls},{"role",int(object.role)},
                {"identityHex",QString::fromLatin1(object.recordedIdentity.toHex())},{"identityEncoding",object.identityEncoding},
                {"inMobIndex",indexedMobs.contains(object.handle)},{"inMediaIndex",indexedMedia.contains(object.handle)},
                {"inObjectSpine",spine.contains(object.handle)}};
            QJsonArray fields, links, inbound;
            for(const auto &p:object.properties)
            {
                QJsonObject f{{"name",p.locator.name},{"state",int(p.state)},{"length",p.encoding.size()},
                    {"hex",QString::fromLatin1(p.encoding.first(std::min(qsizetype(64),p.encoding.size())).toHex())}};
                if(p.bento)f["type"]=p.bento->typeName;
                if(p.decoded.metaType().id()==QMetaType::QString)f["text"]=p.decoded.toString();
                fields.append(f);
            }
            for(const auto *link:outgoing.value(object.handle))
            {
                const auto *target=objects.value(link->target);
                links.append(QJsonObject{{"property",link->locator.name},{"target",qint64(link->target)},
                    {"class",target?objectClass(*target):QString{}},{"explanation",link->explanation}});
            }
            for(const auto &link:source.relationships)if(link.target==object.handle)
            {
                const auto *origin=objects.value(link.origin);
                inbound.append(QJsonObject{{"property",link.locator.name},{"origin",qint64(link.origin)},
                    {"class",origin?objectClass(*origin):QString{}}});
            }
            d["properties"]=fields;d["outbound"]=links;d["inbound"]=inbound;mobDetails.append(d);
        }
        if(cls=="WAVE"||cls=="AIFC"||cls=="TIFF"||cls=="JPEG"||cls=="MDAT"||cls=="IDAT") {++totalMedia;indexedMediaCount+=indexedMedia.contains(object.handle);}
        if(cls=="SourcePackage"||cls=="MaterialPackage")unindexedPackages+=!indexedPackages.contains(object.handle);
        if(cls=="EssenceContainerData")unindexedEcd+=!indexedEcd.contains(object.handle);
        for(const auto &property:object.properties)
            if(property.locator.name=="GenericPackage.PackageUID")
            {
                const QString key=QString::number(object.mxf?object.mxf->partitionOffset:-1)+"/"+QString::fromLatin1(property.encoding.toHex());
                ++duplicateIdentities[key];
            }
    }
    QJsonObject duplicates;for(auto it=duplicateIdentities.cbegin();it!=duplicateIdentities.cend();++it)if(it.value()>1)duplicates[it.key()]=it.value();
    result["descriptorMobs"]=descriptorMobs;result["indexedDescriptorMobs"]=indexedDescriptorMobs;result["unindexedDescriptorMobExamples"]=unindexedCandidates;
    result["smallGraphMobDetails"]=mobDetails;
    result["mediaDataObjects"]=totalMedia;result["indexedMediaDataObjects"]=indexedMediaCount;result["unindexedPackages"]=unindexedPackages;result["unindexedEcd"]=unindexedEcd;result["samePartitionDuplicatePackageUIDs"]=duplicates;
    QJsonArray embedded;for(const auto &child:source.embeddedSources)embedded.append(inspect(child));result["embedded"]=embedded;
    return result;
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv); QTextStream out(stdout);
    for(const auto &path:app.arguments().sliced(1))
    {
        QFile input(path);QJsonObject result{{"path",path},{"size",QFileInfo(path).size()}};
        if(!input.open(QIODevice::ReadOnly)){result["error"]=input.errorString();out<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<'\n';continue;}
        const Cancellation cancellation;const ReaderContext context{{},cancellation};
        auto parsed=path.endsWith(".mxf",Qt::CaseInsensitive)?MxfReader{}.read(input,context):
            path.endsWith(".mdb",Qt::CaseInsensitive)?MdbReader{}.read(input,context):LegacyReader{}.read(input,context);
        result["graph"]=inspect(parsed);out<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<'\n';out.flush();
    }
}
