#include "canon/mxfreader.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    QStringList paths;
    for (int i = 1; i < argc; ++i) {
        const QString path = QString::fromLocal8Bit(argv[i]);
        if (QFileInfo(path).isDir()) {
            QDirIterator entries(path, QStringList{"*.mxf", "*.MXF"}, QDir::Files, QDirIterator::Subdirectories);
            while (entries.hasNext()) paths.append(entries.next());
        } else paths.append(path);
    }
    paths.sort();
    for (const auto &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) return 2;
        Canon::Cancellation cancellation;
        const auto source = Canon::MxfReader{}.read(file, {{}, cancellation});
        QJsonArray unreadable, diagnostics;
        QJsonObject unknownProperties, unknownSets;
        qint64 missingMappings = 0;
        qint64 properties = 0, decoded = 0, rawBytes = 0, knownTypes = 0;
        for (const auto &object : source.objects) {
            if (object.mxf && object.mxf->name.isEmpty()) {
                const QString key = QString::fromLatin1(object.mxf->key.toHex());
                unknownSets.insert(key, unknownSets.value(key).toInteger() + 1);
            }
            for (const auto &property : object.properties) {
                ++properties;
                decoded += property.decoded.isValid();
                knownTypes += property.mxf && !property.mxf->typeName.isEmpty();
                if (property.mxf && property.mxf->mappedAuid.isEmpty()) ++missingMappings;
                if (property.mxf && property.mxf->typeName.isEmpty()) {
                    const QString key = (object.mxf ? QString::fromLatin1(object.mxf->key.toHex()) : QString{}) + QLatin1Char(':')
                        + QString::fromLatin1(property.mxf->mappedAuid.toHex());
                    unknownProperties.insert(key, unknownProperties.value(key).toInteger() + 1);
                }
                rawBytes += property.encoding.size();
                if (property.state == PropertyReadState::Unreadable) {
                    unreadable.append(QJsonObject{
                        {"object", qint64(object.handle)},
                        {"set", object.mxf ? object.mxf->name : QString{}},
                        {"name", property.locator.name},
                        {"type", property.mxf ? property.mxf->typeName : QString{}},
                        {"length", qint64(property.encoding.size())},
                        {"hex", QString::fromLatin1(property.encoding.left(64).toHex())},
                        {"reason", property.interpretation}
                    });
                }
            }
        }
        for (const auto &diagnostic : source.diagnostics) diagnostics.append(diagnostic);
        output << QJsonDocument(QJsonObject{
            {"path", path}, {"fileSize", file.size()}, {"outcome", int(source.outcome)},
            {"objects", qint64(source.objects.size())}, {"properties", properties},
            {"decoded", decoded}, {"knownTypes", knownTypes}, {"rawBytes", rawBytes},
            {"relationships", qint64(source.relationships.size())},
            {"unreadable", unreadable}, {"diagnostics", diagnostics}, {"missingMappings", missingMappings},
            {"unknownProperties", unknownProperties}, {"unknownSets", unknownSets}
        }).toJson(QJsonDocument::Compact) << '\n';
    }
}
