#include "canon/mdbreader.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

QString outcomeName(Canon::ParsedSource::Outcome outcome)
{
    using O = Canon::ParsedSource::Outcome;
    switch (outcome) {
    case O::NotRead: return "NotRead";
    case O::Complete: return "Complete";
    case O::Incomplete: return "Incomplete";
    case O::Malformed: return "Malformed";
    case O::Unsupported: return "Unsupported";
    case O::IoError: return "IoError";
    case O::Cancelled: return "Cancelled";
    }
    return "InvalidOutcome";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    for (const QString &path : app.arguments().sliced(1)) {
        QJsonObject entry{{"path", path}};
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly)) {
            entry.insert("outcome", "OpenFailed");
            entry.insert("error", source.errorString());
            ++failures;
        } else {
            entry.insert("sourceBytes", source.size());
            Canon::Cancellation cancellation;
            QElapsedTimer clock;
            clock.start();
            const auto result = Canon::MdbReader{}.read(source, {{}, cancellation});
            const double elapsed = double(clock.nsecsElapsed()) / 1000000.0;
            qsizetype properties = 0, unreadableProperties = 0, unreadableStructure = 0;
            qsizetype nullReferences = 0, unresolvedNonNullReferences = 0, resolvedReferences = 0;
            for (const auto &object : result.objects) {
                properties += object.properties.size();
                for (const auto &property : object.properties)
                    unreadableProperties += property.state == PropertyReadState::Unreadable;
            }
            for (const auto &property : result.unownedProperties)
                unreadableStructure += property.state == PropertyReadState::Unreadable;
            for (const auto &relationship : result.relationships) {
                if (relationship.target != 0)
                    ++resolvedReferences;
                else if (relationship.recordedReference.toMap().value("key").toULongLong() == 0)
                    ++nullReferences;
                else
                    ++unresolvedNonNullReferences;
            }
            entry.insert("outcome", outcomeName(result.outcome));
            entry.insert("objects", qint64(result.objects.size()));
            entry.insert("objectProperties", qint64(properties));
            entry.insert("unownedProperties", qint64(result.unownedProperties.size()));
            entry.insert("unreadableObjectProperties", qint64(unreadableProperties));
            entry.insert("unreadableUnownedProperties", qint64(unreadableStructure));
            entry.insert("relationships", qint64(result.relationships.size()));
            entry.insert("resolvedReferences", qint64(resolvedReferences));
            entry.insert("nullReferences", qint64(nullReferences));
            entry.insert("unresolvedNonNullReferences", qint64(unresolvedNonNullReferences));
            entry.insert("diagnostics", qint64(result.diagnostics.size()));
            entry.insert("elapsedParseMs", elapsed);
            failures += result.outcome != Canon::ParsedSource::Outcome::Complete;
        }
        const QByteArray json = QJsonDocument(entry).toJson(QJsonDocument::Compact);
        std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
        std::fputc('\n', stdout);
    }
    return failures ? 1 : 0;
}
