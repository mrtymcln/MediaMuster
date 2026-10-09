#include "canon/mdbreader.h"
#include "canon/mxfreader.h"
#include "canon/projection.h"
#include "mediafile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <mach/mach.h>

// Diagnostic only. Count reader requests and optionally suppress redundant
// underlying QFile seeks; the production readers and retained facts are unchanged.
class CountedInput final : public QIODevice
{
public:
    CountedInput(const QString &path, bool skipSamePosition)
        : file(path), skip(skipSamePosition)
    {
        if (file.open(QIODevice::ReadOnly)) open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    qint64 size() const override { return file.size(); }
    bool seek(qint64 offset) override
    {
        ++seeks;
        const bool same = file.pos() == offset;
        samePositionSeeks += same;
        if ((!skip || !same) && !file.seek(offset)) return false;
        forwardedSeeks += !skip || !same;
        return QIODevice::seek(offset);
    }
    qint64 reads = 0, seeks = 0, samePositionSeeks = 0, forwardedSeeks = 0, bytes = 0, tinyReads = 0;
protected:
    qint64 readData(char *data, qint64 length) override
    {
        ++reads;
        tinyReads += length <= 16;
        const auto got = file.read(data, length);
        if (got > 0) bytes += got;
        return got;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    QFile file;
    bool skip;
};

qint64 footprint()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS
        ? qint64(info.phys_footprint) : -1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QJsonObject sizes{{"RawProperty", int(sizeof(Canon::RawProperty))},
        {"PropertyLocator", int(sizeof(Canon::PropertyLocator))},
        {"BentoPropertyContext", int(sizeof(Canon::BentoPropertyContext))},
        {"MxfPropertyContext", int(sizeof(Canon::MxfPropertyContext))},
        {"AvidObject", int(sizeof(Canon::AvidObject))}, {"Relationship", int(sizeof(Canon::Relationship))},
        {"MetadataObservation", int(sizeof(MetadataObservation))},
        {"CanonMediaFile", int(sizeof(Canon::MediaFile))}, {"UiMediaFile", int(sizeof(MediaFile))}};
    if (argc < 2) { std::puts(QJsonDocument(sizes).toJson(QJsonDocument::Compact).constData()); return 0; }
    const QString path = QString::fromLocal8Bit(argv[1]);
    const bool skip = argc > 2 && QByteArray(argv[2]) == "skip-same-position";
    CountedInput input(path, skip);
    if (!input.isOpen()) return 2;
    const auto memoryBefore = footprint();
    const Canon::Cancellation cancellation;
    QElapsedTimer timer;
    timer.start();
    const bool mxf = QFileInfo(path).suffix().compare(QLatin1String("mxf"), Qt::CaseInsensitive) == 0;
    const auto source = mxf ? Canon::MxfReader{}.read(input, {{}, cancellation})
                            : Canon::MdbReader{}.read(input, {{}, cancellation});
    const auto parseMs = timer.elapsed();
    const auto memoryParsed = footprint();
    timer.restart();
    const auto projection = mxf ? Canon::projectMxf(source, cancellation) : Canon::projectOmf(source, cancellation);
    const auto projectMs = timer.elapsed();
    const auto memoryProjected = footprint();
    qint64 properties = 0, capacity = 0, encodingBytes = 0, bentoContexts = 0, mxfContexts = 0;
    QCryptographicHash digest(QCryptographicHash::Sha256);
    auto count = [&](const Canon::RawProperty &property) {
        ++properties;
        encodingBytes += property.encoding.size();
        bentoContexts += bool(property.bento);
        mxfContexts += bool(property.mxf);
        digest.addData(property.locator.name.toUtf8());
        digest.addData(property.locator.key);
        digest.addData(property.encoding);
        for (const auto &range : property.locator.ranges)
        {
            digest.addData(QByteArray::number(range.offset));
            digest.addData(QByteArray::number(range.length));
        }
    };
    for (const auto &object : source.objects)
    {
        capacity += object.properties.capacity();
        for (const auto &property : object.properties) count(property);
    }
    capacity += source.unownedProperties.capacity();
    for (const auto &property : source.unownedProperties) count(property);
    const QJsonObject result{{"path", path}, {"skipSamePosition", skip}, {"sizeof", sizes},
        {"outcome", int(source.outcome)}, {"parseMs", parseMs}, {"projectMs", projectMs},
        {"footprintBefore", memoryBefore}, {"footprintParsed", memoryParsed}, {"footprintProjected", memoryProjected},
        {"objects", source.objects.size()}, {"properties", properties}, {"propertyCapacity", capacity},
        {"encodingBytes", encodingBytes}, {"bentoContexts", bentoContexts}, {"mxfContexts", mxfContexts},
        {"relationships", source.relationships.size()}, {"projectedFiles", projection.files.size()},
        {"projectedMasters", projection.masters.size()}, {"readCalls", input.reads}, {"bytesRead", input.bytes},
        {"readsAtMost16Bytes", input.tinyReads}, {"seekCalls", input.seeks},
        {"samePositionSeeks", input.samePositionSeeks}, {"forwardedSeeks", input.forwardedSeeks},
        {"rawPropertyDigest", QString::fromLatin1(digest.result().toHex())}};
    std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
}
