#include "canon/mdbreader.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

// Read-only evidence receipt for real MDB Summary fields after the shared decoder.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QJsonArray sources;
    for (const auto &path : app.arguments().sliced(1))
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return 2;
        const Canon::Cancellation cancellation;
        const auto source = Canon::MdbReader{}.read(file, {{}, cancellation});
        QJsonArray fields;
        for (const auto &object : source.objects)
            for (const auto &property : object.properties)
            {
                if (!property.locator.name.contains(":Summary"))
                    continue;
                QJsonArray ranges;
                for (const auto &range : property.locator.ranges)
                    ranges.append(QJsonObject{{"offset", range.offset}, {"length", range.length}});
                QJsonValue decoded = QJsonValue::fromVariant(property.decoded);
                if (property.decoded.metaType() == QMetaType::fromType<QVariantMap>())
                    decoded = QJsonObject{{"fieldNames", QJsonArray::fromStringList(property.decoded.toMap().keys())}};
                else if (property.decoded.metaType() == QMetaType::fromType<quint64>())
                    decoded = QJsonObject{{"unsignedInteger", QString::number(property.decoded.toULongLong())}};
                else if (property.decoded.metaType() == QMetaType::fromType<QByteArray>())
                    decoded = QJsonObject{{"bytesHex", QString::fromLatin1(property.decoded.toByteArray().toHex())}};
                fields.append(QJsonObject{{"object", qint64(object.handle)},
                    {"property", property.locator.name}, {"state", int(property.state)},
                    {"decoded", decoded}, {"encodingBytes", property.encoding.size()},
                    {"encodingHex", property.encoding.size() <= 40 ? QString::fromLatin1(property.encoding.toHex()) : QString{}},
                    {"encodingSha256", QString::fromLatin1(QCryptographicHash::hash(property.encoding, QCryptographicHash::Sha256).toHex())},
                    {"ranges", ranges}, {"interpretation", property.interpretation},
                    {"textEncoding", property.textEncoding ? int(*property.textEncoding) : -1}});
            }
        sources.append(QJsonObject{{"path", path}, {"sourceOutcome", int(source.outcome)},
            {"diagnostics", QJsonArray::fromStringList(source.diagnostics)}, {"summaryFields", fields}});
    }
    std::puts(QJsonDocument(QJsonObject{{"sources", sources}}).toJson(QJsonDocument::Indented).constData());
}
