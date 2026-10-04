#include "canon/mxfreader.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QTextStream>
#include <algorithm>

using Range = Canon::ByteRange;
class CountedFile final : public QFile
{
public:
    using QFile::QFile;
    QVector<Range> forbidden, reads;
    qint64 bytesRequested = 0, bytesReturned = 0;
    bool essenceReadAttempt = false;
protected:
    qint64 readData(char *data, qint64 amount) override
    {
        const auto offset = pos();
        bytesRequested += amount;
        for (const auto &sample : forbidden)
            if (offset < sample.offset + sample.length && sample.offset < offset + amount)
            {
                essenceReadAttempt = true;
                setErrorString(QStringLiteral("Probe rejected an essence read"));
                return -1;
            }
        const qint64 got = QFile::readData(data, amount);
        if (got > 0) { reads.append({offset, got}); bytesReturned += got; }
        return got;
    }
};
bool essence(const QByteArray &key)
{
    return key.startsWith(QByteArray::fromHex("060e2b34010201010d010301"))
        || key == QByteArray::fromHex("060e2b34010201010e04030115010801");
}
QVector<Range> essenceRanges(const QString &path, QStringList &errors)
{
    QFile input(path); QVector<Range> ranges;
    if (!input.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) { errors << input.errorString(); return ranges; }
    qint64 offset = 0;
    while (offset < input.size())
    {
        if (!input.seek(offset)) { errors << QStringLiteral("Inventory seek failed"); break; }
        const auto framing = input.read(17);
        if (framing.size() != 17) { errors << QStringLiteral("Inventory short key/BER"); break; }
        const quint8 first = quint8(framing[16]);
        const int width = first & 128 ? first & 127 : 0;
        if (first == 128 || width > 8) { errors << QStringLiteral("Inventory invalid BER"); break; }
        quint64 length = width ? 0 : first;
        if (width)
        {
            const auto digits = input.read(width);
            if (digits.size() != width) { errors << QStringLiteral("Inventory short BER"); break; }
            for (char digit : digits) length = (length << 8) | quint8(digit);
        }
        const qint64 valueOffset = offset + 17 + width;
        if (length > quint64(input.size() - valueOffset)) { errors << QStringLiteral("Inventory value beyond EOF"); break; }
        if (essence(framing.first(16))) ranges.append({valueOffset, qint64(length)});
        offset = valueOffset + qint64(length);
    }
    return ranges;
}
QString outcome(Canon::ParsedSource::Outcome status)
{
    using O = Canon::ParsedSource::Outcome;
    switch (status)
    {
    case O::NotRead:return QStringLiteral("NotRead");case O::Complete:return QStringLiteral("Complete");
    case O::Incomplete:return QStringLiteral("Incomplete");case O::Malformed:return QStringLiteral("Malformed");
    case O::Unsupported:return QStringLiteral("Unsupported");case O::IoError:return QStringLiteral("IoError");
    case O::Cancelled:return QStringLiteral("Cancelled");
    }
    return {};
}
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv); QTextStream output(stdout);
    for (const auto &path : application.arguments().sliced(1))
    {
        CountedFile source(path); QStringList inventoryErrors;
        source.forbidden = essenceRanges(path, inventoryErrors);
        QJsonObject record{{QStringLiteral("path"), path}, {QStringLiteral("inventoryErrors"), QJsonArray::fromStringList(inventoryErrors)}};
        if (!source.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
        { record[QStringLiteral("openError")] = source.errorString(); output << QJsonDocument(record).toJson(QJsonDocument::Compact) << '\n'; continue; }
        Canon::Cancellation cancellation; QElapsedTimer timer; timer.start();
        const auto result = Canon::MxfReader{}.read(source, {{}, cancellation});
        record[QStringLiteral("elapsedMs")] = timer.nsecsElapsed() / 1000000.0;
        record[QStringLiteral("fileSize")] = source.size();
        record[QStringLiteral("outcome")] = outcome(result.outcome);
        record[QStringLiteral("objects")] = result.objects.size();
        record[QStringLiteral("unownedProperties")] = result.unownedProperties.size();
        record[QStringLiteral("bytesRequested")] = source.bytesRequested;
        record[QStringLiteral("bytesReturned")] = source.bytesReturned;
        record[QStringLiteral("essenceReadAttempt")] = source.essenceReadAttempt;
        record[QStringLiteral("diagnostics")] = QJsonArray::fromStringList(result.diagnostics);
        QJsonArray samples;
        for (const auto &range : source.forbidden) samples.append(QJsonObject{{QStringLiteral("offset"), range.offset}, {QStringLiteral("length"), range.length}});
        record[QStringLiteral("guardedEssenceRanges")] = samples;
        qint64 essenceIntersection = 0;
        for (const auto &read : source.reads)
            for (const auto &range : source.forbidden)
                essenceIntersection += std::max<qint64>(0, std::min(read.offset + read.length, range.offset + range.length) - std::max(read.offset, range.offset));
        record[QStringLiteral("essenceReadIntersectionBytes")] = essenceIntersection;
        qint64 properties = 0, retainedBytes = 0, opaqueProperties = 0, decodedProperties = 0;
        QJsonArray unreadable;
        auto inspect = [&](const Canon::RawProperty &property, const Canon::AvidObject *object)
        {
            ++properties; retainedBytes += property.encoding.size();
            if (property.decoded.isValid()) ++decodedProperties;
            if (!property.bytesRetained) ++opaqueProperties;
            if (property.state != PropertyReadState::Unreadable) return;
            QJsonObject value{{QStringLiteral("property"), property.locator.name},
                {QStringLiteral("key"), QString::fromLatin1(property.locator.key.toHex())},
                {QStringLiteral("length"), property.encoding.size()},
                {QStringLiteral("interpretation"), property.interpretation}};
            if (object && object->mxf)
            {
                value[QStringLiteral("object")] = qint64(object->handle);
                value[QStringLiteral("set")] = object->mxf->name;
                value[QStringLiteral("setKey")] = QString::fromLatin1(object->mxf->key.toHex());
                value[QStringLiteral("setOffset")] = object->mxf->framing.offset;
            }
            if (property.mxf) value[QStringLiteral("type")] = property.mxf->typeName;
            unreadable.append(value);
        };
        for (const auto &object : result.objects)
            for (const auto &property : object.properties) inspect(property, &object);
        const auto objectProperties = properties;
        for (const auto &property : result.unownedProperties) inspect(property, nullptr);
        record[QStringLiteral("objectProperties")] = objectProperties;
        record[QStringLiteral("allProperties")] = properties;
        record[QStringLiteral("decodedProperties")] = decodedProperties;
        record[QStringLiteral("rangeOnlyProperties")] = opaqueProperties;
        record[QStringLiteral("rawEncodingBytesRetained")] = retainedBytes;
        record[QStringLiteral("unreadableProperties")] = unreadable;
        qint64 resolvedStrong = 0, unresolvedStrong = 0, resolvedWeak = 0, unresolvedWeak = 0;
        for (const auto &relation : result.relationships)
        {
            if (relation.referenceEncoding.contains(QStringLiteral("WeakRef"), Qt::CaseInsensitive))
            { if (relation.target) ++resolvedWeak; else ++unresolvedWeak; }
            else { if (relation.target) ++resolvedStrong; else ++unresolvedStrong; }
        }
        record[QStringLiteral("relationships")] = result.relationships.size();
        record[QStringLiteral("resolvedStrongReferences")] = resolvedStrong;
        record[QStringLiteral("unresolvedStrongReferences")] = unresolvedStrong;
        record[QStringLiteral("resolvedWeakReferences")] = resolvedWeak;
        record[QStringLiteral("unresolvedWeakReferences")] = unresolvedWeak;
        output << QJsonDocument(record).toJson(QJsonDocument::Compact) << '\n'; output.flush();
    }
}
