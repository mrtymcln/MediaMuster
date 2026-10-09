#include "canon/mdbreader.h"
#include "canon/mxfreader.h"
#include "canon/projection.h"
#include "canon/scanengine.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cstdio>

// Read-only receipt for the two real DV rows whose selected short names changed.
using namespace Canon;

QJsonArray ranges(const QVector<ByteRange> &values)
{
    QJsonArray result;
    for (const auto &value : values)
        result.append(QJsonObject{{"offset", value.offset}, {"length", value.length}});
    return result;
}

QJsonObject propertyReceipt(const RawProperty &property)
{
    QJsonObject result{{"name", property.locator.name}, {"readState", int(property.state)},
        {"encodingBytes", property.encoding.size()}, {"encodingHex", QString::fromLatin1(property.encoding.toHex())},
        {"ranges", ranges(property.locator.ranges)}, {"interpretation", property.interpretation}};
    if (property.decoded.metaType().id() != QMetaType::QByteArray)
        result.insert(QStringLiteral("decoded"), QJsonValue::fromVariant(property.decoded));
    if (property.bento)
    {
        result.insert(QStringLiteral("type"), property.bento->typeName);
        result.insert(QStringLiteral("propertyId"), qint64(property.bento->property));
        result.insert(QStringLiteral("typeId"), qint64(property.bento->type));
        result.insert(QStringLiteral("tocRanges"), ranges(property.bento->tocRanges));
        if (property.bento->metadataBigEndian)
            result.insert(QStringLiteral("metadataBigEndian"), *property.bento->metadataBigEndian);
    }
    if (property.mxf)
        result.insert(QStringLiteral("type"), property.mxf->typeName);
    return result;
}

QString identity(const RawProperty &property)
{
    if (property.locator.name == QLatin1String("OMFI:MOBJ:MobID"))
        return canonicalDatabaseId(property.encoding, property.bento && property.bento->metadataBigEndian.value_or(false));
    if (property.locator.name == QLatin1String("GenericPackage.PackageUID"))
        return canonicalMxfId(property.encoding);
    return {};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QSet<QString> ids{
        QStringLiteral("060a2b3401010105.01010f1013000000.83c926f960120690.0c1146fc807fda72"),
        QStringLiteral("060a2b3401010105.01010f1013000000.a0543bf960120690.c6ab46fc807fda72")};
    QJsonArray sources;
    for (const auto &path : app.arguments().sliced(1))
    {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly))
            return 2;
        const Cancellation cancellation;
        const bool mxf = path.endsWith(QLatin1String(".mxf"));
        const auto source = mxf ? MxfReader{}.read(input, {{}, cancellation}) : MdbReader{}.read(input, {{}, cancellation});
        const auto projection = mxf ? projectMxf(source, cancellation) : projectOmf(source, cancellation);
        QJsonArray owners, descriptors, projected;
        QSet<ObjectHandle> descriptorHandles;
        for (const auto &object : source.objects)
            for (const auto &property : object.properties)
                if (ids.contains(identity(property)))
                {
                    QJsonArray descriptorLinks;
                    for (const auto &link : source.relationships)
                        if (link.origin == object.handle && (link.locator.name == QLatin1String("OMFI:MOBJ:PhysicalMedia") ||
                            link.locator.name == QLatin1String("OMFI:SMOB:MediaDescription") ||
                            link.locator.name == QLatin1String("SourcePackage.Descriptor")))
                        {
                            descriptorHandles.insert(link.target);
                            descriptorLinks.append(QJsonObject{{"property", link.locator.name}, {"target", qint64(link.target)},
                                {"explanation", link.explanation}});
                        }
                    owners.append(QJsonObject{{"handle", qint64(object.handle)}, {"canonicalId", identity(property)},
                        {"identity", propertyReceipt(property)}, {"descriptorLinks", descriptorLinks}});
                }
        for (const auto &object : source.objects)
            if (descriptorHandles.contains(object.handle))
            {
                QJsonArray properties;
                for (const auto &property : object.properties)
                    if (property.locator.name == QLatin1String("OMFI:ObjID") ||
                        property.locator.name == QLatin1String("OMFI:OOBJ:ObjClass") ||
                        property.locator.name == QLatin1String("OMFI:DIDD:Compression") ||
                        property.locator.name == QLatin1String("OMFI:DIDD:DIDResolutionID") ||
                        property.locator.name == QLatin1String("OMFI:DIDD:EssenceCompression") ||
                        property.locator.name == QLatin1String("GenericPictureEssenceDescriptor.PictureEssenceCoding"))
                        properties.append(propertyReceipt(property));
                descriptors.append(QJsonObject{{"handle", qint64(object.handle)}, {"mxfClass", object.mxf ? object.mxf->name : QString{}},
                    {"properties", properties}});
            }
        for (auto file : projection.files)
            if (ids.contains(file.fileMobId))
            {
                selectMetadata(file.evidence);
                QJsonArray fields;
                for (const auto field : {MediaProperty::Compression, MediaProperty::CompressionLabel})
                {
                    QJsonArray observations;
                    for (const auto &observation : file.evidence.observations(field))
                        observations.append(QJsonObject{{"property", observation.property}, {"object", observation.objectIdentity},
                            {"value", field == MediaProperty::CompressionLabel ? QString::fromLatin1(observation.value.toByteArray().toHex())
                                : observation.value.toString()}, {"basis", int(observation.basis)}, {"explanation", observation.explanation}});
                    const auto selected = file.evidence.selected(field);
                    fields.append(QJsonObject{{"property", mediaPropertyName(field)},
                        {"selected", field == MediaProperty::CompressionLabel ? QString::fromLatin1(selected.value.toByteArray().toHex())
                            : selected.value.toString()}, {"selectionReason", selected.reason}, {"observations", observations}});
                }
                projected.append(QJsonObject{{"fileMobId", file.fileMobId}, {"fields", fields}});
            }
        sources.append(QJsonObject{{"path", path}, {"sourceOutcome", int(source.outcome)},
            {"owners", owners}, {"descriptors", descriptors}, {"projection", projected}});
    }
    std::puts(QJsonDocument(QJsonObject{{"sources", sources}}).toJson(QJsonDocument::Indented).constData());
}
