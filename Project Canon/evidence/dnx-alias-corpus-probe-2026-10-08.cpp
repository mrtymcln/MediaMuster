// Read-only investigation: search actual parsed compression properties, and
// separately search all retained metadata bytes. Never scan recording payloads.
#include "canon/avbreader.h"
#include "canon/legacyreader.h"
#include "canon/mdbreader.h"
#include "canon/mxfreader.h"
#include "canon/pmrreader.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTextStream>
#include <algorithm>

using namespace Canon;

struct Alias { QByteArray ul; QVector<QByteArray> spellings; QString profile; };
QVector<Alias> aliases;

QByteArray reverseFields(QByteArray bytes)
{
    std::reverse(bytes.begin(), bytes.begin() + 4);
    std::reverse(bytes.begin() + 4, bytes.begin() + 6);
    std::reverse(bytes.begin() + 6, bytes.begin() + 8);
    return bytes;
}

QByteArray codingUl(const RawProperty &property, const AvidObject *object)
{
    if (property.encoding.size() != 16 || property.state != PropertyReadState::Present) return {};
    const auto &name = property.locator.name;
    if (name == QLatin1String("GenericPictureEssenceDescriptor.PictureEssenceCoding") ||
        name == QLatin1String("GenericSoundEssenceDescriptor.SoundEssenceCompression")) return property.encoding;
    if (name == QLatin1String("OMFI:DIDD:EssenceCompression") && property.bento &&
        property.bento->metadataBigEndian.has_value())
    {
        const auto bytes = *property.bento->metadataBigEndian ? property.encoding : reverseFields(property.encoding);
        return bytes.mid(8, 8) + bytes.first(8);
    }
    if (name == QLatin1String("DIDDescriptor.essence_compression") && object && object->avb)
    {
        const auto bytes = object->avb->bigEndian ? property.encoding : reverseFields(property.encoding);
        return bytes.mid(8, 8) + bytes.first(8);
    }
    return {};
}

void inspect(const ParsedSource &source, const QString &context, QJsonArray &matches,
             QMap<QString, qint64> &labels, QJsonObject &counts, QJsonArray &details)
{
    auto inspectProperty = [&](const RawProperty &property, const AvidObject *object) {
        counts["properties"] = counts["properties"].toInteger() + 1;
        counts["retainedBytes"] = counts["retainedBytes"].toInteger() + property.encoding.size();
        if (!property.bytesRetained) counts["unretainedProperties"] = counts["unretainedProperties"].toInteger() + 1;
        const auto ul = codingUl(property, object);
        if (!ul.isEmpty()) labels[QString::fromLatin1(ul.toHex())]++;
        for (const auto &alias : aliases)
        {
            bool byteMatch = false;
            for (const auto &spelling : alias.spellings)
                byteMatch |= property.encoding.contains(spelling);
            if (!byteMatch && ul != alias.ul) continue;
            QJsonArray ranges;
            for (const auto &range : property.locator.ranges)
                ranges.append(QJsonObject{{"offset", range.offset}, {"length", range.length}});
            matches.append(QJsonObject{{"alias", QString::fromLatin1(alias.ul.toHex())},
                {"assignedProfile", alias.profile}, {"context", context}, {"object", qint64(object ? object->handle : 0)},
                {"property", property.locator.name}, {"compressionPropertyMatch", ul == alias.ul},
                {"rawMetadataByteMatch", byteMatch}, {"ranges", ranges}});
        }
    };
    details.append(QJsonObject{{"context", context}, {"container", int(source.container)},
        {"outcome", int(source.outcome)}, {"objects", source.objects.size()},
        {"diagnostics", QJsonArray::fromStringList(source.diagnostics)}});
    for (const auto &object : source.objects)
        for (const auto &property : object.properties) inspectProperty(property, &object);
    for (const auto &property : source.unownedProperties) inspectProperty(property, nullptr);
    for (qsizetype i = 0; i < source.embeddedSources.size(); ++i)
        inspect(source.embeddedSources[i], context + QStringLiteral("/embedded:%1").arg(i), matches, labels, counts, details);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    QFile manifest(app.arguments()[1]);
    if (!manifest.open(QIODevice::ReadOnly)) return 2;
    const auto entries = QJsonDocument::fromJson(manifest.readAll()).array();
    // Keep the investigated identifiers here so this receipt remains repeatable
    // after their unproven production mappings are removed.
    const struct { const char *label; const char *profile; } investigatedAliases[] = {
        {"060e2b34040101010d01030102060301", "DNxHD LB"},
        {"060e2b34040101010d01030102060101", "DNxHD SQ"},
        {"060e2b34040101010d01030102060201", "DNxHD HQ"},
        {"060e2b34040101010d01030102060202", "DNxHD HQX"},
        {"060e2b34040101010d01030102110101", "DNxHR LB"},
        {"060e2b34040101010d01030102110201", "DNxHR SQ"},
        {"060e2b34040101010d01030102110301", "DNxHR HQ"},
        {"060e2b34040101010d01030102110401", "DNxHR HQX"},
        {"060e2b34040101010d01030102110501", "DNxHR 444"}};
    for (const auto &profile : investigatedAliases)
    {
        const auto ul = QByteArray::fromHex(profile.label);
        const auto auid = ul.mid(8, 8) + ul.first(8);
        aliases.append({ul, {ul, auid, reverseFields(auid)},
            QString::fromLatin1(profile.profile)});
    }
    QTextStream output(stdout);
    QTextStream progress(stderr);
    qsizetype completed = 0;
    for (const auto &entry : entries)
    {
        const auto spec = entry.toObject();
        const auto path = spec["path"].toString();
        QFile file(path);
        QJsonObject receipt{{"path", path}, {"group", spec["group"]}};
        if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        {
            receipt["openError"] = file.errorString();
        }
        else
        {
            const Cancellation cancellation;
            const auto suffix = QFileInfo(path).suffix().toLower();
            ParsedSource source;
            if (suffix == QLatin1String("mxf")) source = MxfReader{}.read(file, {{}, cancellation});
            else if (suffix == QLatin1String("mdb")) source = MdbReader{}.read(file, {{}, cancellation});
            else if (suffix == QLatin1String("pmr")) source = PmrReader{}.read(file, {{}, cancellation});
            else if (suffix == QLatin1String("avb")) source = AvbReader{}.read(file, {{}, cancellation});
            else source = LegacyReader{}.read(file, {{}, cancellation});
            QJsonArray matches, details;
            QMap<QString, qint64> labels;
            QJsonObject counts;
            inspect(source, QStringLiteral("source"), matches, labels, counts, details);
            QJsonObject labelCounts;
            for (auto it = labels.cbegin(); it != labels.cend(); ++it) labelCounts[it.key()] = it.value();
            receipt["fileSize"] = file.size();
            receipt["outcome"] = int(source.outcome);
            receipt["codingLabels"] = labelCounts;
            receipt["matches"] = matches;
            receipt["counts"] = counts;
            receipt["graphs"] = details;
        }
        output << QJsonDocument(receipt).toJson(QJsonDocument::Compact) << '\n';
        output.flush();
        if (++completed % 100 == 0) progress << completed << '/' << entries.size() << " sources checked\n" << Qt::flush;
    }
}
