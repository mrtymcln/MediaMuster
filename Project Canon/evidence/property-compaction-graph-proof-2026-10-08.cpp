#include "canon/mdbreader.h"
#include "canon/legacyreader.h"
#include "canon/projection.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <algorithm>
#include <mach/mach.h>

// Read-only diagnostic. Stream all semantic fields in declared/recorded order;
// sort only hash-map keys. Allocation capacities are measured outside the digest.
class HashSink final : public QIODevice {
public:
    HashSink() { buffer.reserve(65536); open(QIODevice::WriteOnly | QIODevice::Unbuffered); }
    QByteArray result() { flush(); return hash.result().toHex(); }
    qint64 serializedBytes = 0;
protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 size) override {
        const qint64 requested = size;
        serializedBytes += size;
        while (size > 0) {
            const qint64 count = qMin<qint64>(size, 65536 - buffer.size());
            buffer.append(data, count); data += count; size -= count;
            if (buffer.size() == 65536) flush();
        }
        return requested;
    }
private:
    void flush() { if (!buffer.isEmpty()) { hash.addData(buffer); buffer.clear(); } }
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QByteArray buffer;
};

class Fingerprint {
public:
    Fingerprint() : stream(&sink) { stream.setVersion(QDataStream::Qt_6_0); stream.setByteOrder(QDataStream::BigEndian); }
    HashSink sink;
    QDataStream stream;
    qint64 objects = 0, relationships = 0, properties = 0, capacity = 0, encodingBytes = 0;
    template <typename T> void optional(const std::optional<T> &value) { stream << bool(value); if (value) stream << qint32(*value); }
    void range(const Canon::ByteRange &value) { stream << value.offset << value.length; }
    void ranges(const QVector<Canon::ByteRange> &values) { stream << qint64(values.size()); for (const auto &value : values) range(value); }
    void locator(const Canon::PropertyLocator &value) { stream << value.name << value.key << value.objectNumber; ranges(value.ranges); }
    void snapshot(const SourceSnapshotRef &value) {
        stream << bool(value);
        if (value) stream << qint32(value->source) << value->path << value->modified << qint32(value->readState);
    }
    void property(const Canon::RawProperty &value) {
        ++properties; encodingBytes += value.encoding.size();
        locator(value.locator); stream << value.encoding << value.decoded << qint32(value.state) << value.interpretation;
        optional(value.textEncoding); optional(value.textEncodingBasis);
        stream << bool(value.bento);
        if (value.bento) {
            const auto &native = *value.bento;
            stream << native.property << native.type << native.generation << native.referenceListObject << native.typeName;
            ranges(native.tocRanges); optional(native.metadataBigEndian);
        }
        stream << bool(value.mxf);
        if (value.mxf) {
            const auto &native = *value.mxf;
            stream << native.localTag << native.primerOffset << native.mappedAuid << native.typeName << native.framingBytes;
            ranges(native.framingRanges);
        }
        stream << value.bytesRetained;
    }
    void propertyList(const QVector<Canon::RawProperty> &values) {
        stream << qint64(values.size()); capacity += values.capacity(); for (const auto &value : values) property(value);
    }
    void graph(const Canon::ParsedSource &value) {
        stream << qint32(value.outcome) << value.readReason; snapshot(value.snapshot);
        stream << qint32(value.container); optional(value.omfRevision); locator(value.embedding);
        stream << qint64(value.recordSets.size());
        for (const auto &set : value.recordSets) {
            stream << set.name; optional(set.pmrFileSet); stream << set.version << set.declaredCount << set.objects << set.framingComplete;
        }
        stream << qint64(value.objects.size()); objects += value.objects.size();
        for (const auto &object : value.objects) {
            stream << object.handle << qint32(object.role); snapshot(object.snapshot);
            stream << object.recordedIdentity << object.identityEncoding << bool(object.mxf);
            if (object.mxf) {
                const auto &native = *object.mxf;
                stream << native.key << native.name << native.partitionOffset; range(native.framing); range(native.value);
            }
            stream << bool(object.avb);
            if (object.avb) {
                const auto &native = *object.avb;
                stream << native.classId; range(native.framing); range(native.value);
                stream << native.bigEndian << native.interpretationComplete;
            }
            propertyList(object.properties);
        }
        stream << qint64(value.relationships.size()); relationships += value.relationships.size();
        for (const auto &relationship : value.relationships) {
            stream << relationship.origin << relationship.target; locator(relationship.locator);
            stream << relationship.recordedReference << relationship.referenceEncoding << qint32(relationship.basis) << relationship.explanation;
        }
        propertyList(value.unownedProperties); stream << value.diagnostics;
        stream << qint64(value.embeddedSources.size()); for (const auto &child : value.embeddedSources) graph(child);
    }
    void readResult(const PropertyReadResult &value) {
        stream << qint32(value.state) << qint32(value.reason) << qint32(value.applicability) << value.explanation;
    }
    void evidence(const MediaEvidence &value) {
        const auto &coverage = value.sourceCoverage(); stream << qint64(coverage.size());
        for (const auto &item : coverage) {
            snapshot(item.snapshot); stream << item.objectIdentity << qint32(item.defaultReason) << item.eligible << qint32(item.freshness);
            auto keys = item.fields.keys(); std::sort(keys.begin(), keys.end(), [](auto a, auto b) { return int(a) < int(b); });
            stream << qint64(keys.size()); for (const auto key : keys) { stream << qint32(key); readResult(item.fields.value(key)); }
        }
        stream << qint32(MediaProperty::Count);
        for (int index = 0; index < int(MediaProperty::Count); ++index) {
            const auto field = MediaProperty(index); const auto &observations = value.observations(field);
            stream << qint32(field) << qint64(observations.size());
            for (const auto &observation : observations) {
                snapshot(observation.snapshot);
                stream << observation.property << observation.objectIdentity << observation.value << observation.rawValue
                       << qint32(observation.readState) << qint32(observation.readReason) << qint32(observation.basis)
                       << qint32(observation.freshness) << observation.explanation << observation.eligible;
                optional(observation.textEncoding); optional(observation.textEncodingBasis);
            }
            readResult(value.readStatus(field));
            const auto selected = value.selected(field);
            stream << selected.value << qint32(selected.readState) << qint32(selected.agreement) << selected.selectedObservation
                   << selected.rule << selected.reason << qint32(selected.readReason) << qint32(selected.applicability);
        }
    }
    void files(const QVector<Canon::ProjectedFile> &values) {
        stream << qint64(values.size());
        for (const auto &file : values) {
            stream << file.fileMobId << file.masterMobIds << file.filenames << qint64(file.objects.size());
            for (const auto &object : file.objects) { snapshot(object.source); stream << object.handle; }
            evidence(file.evidence);
        }
    }
    void projection(const Canon::Projection &value) { files(value.files); files(value.masters); stream << value.diagnostics; }
    QByteArray result() { if (stream.status() != QDataStream::Ok) qFatal("Serialization failed"); return sink.result(); }
};

qint64 footprint() {
    task_vm_info_data_t info{}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS ? qint64(info.phys_footprint) : -1;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 1;
    const QString path = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return 2;
    const bool database = QFileInfo(path).suffix().compare(QStringLiteral("mdb"), Qt::CaseInsensitive) == 0;
    auto receipt = QSharedPointer<SourceSnapshot>::create(); receipt->path = path; receipt->modified = QFileInfo(path).lastModified();
    const Canon::Cancellation cancellation; QElapsedTimer timer; timer.start();
    const auto memoryBefore = footprint();
    const auto graph = database ? Canon::MdbReader{}.read(file, {receipt, cancellation}) : Canon::LegacyReader{}.read(file, {receipt, cancellation});
    const auto parseMs = timer.elapsed(), memoryParsed = footprint();
    timer.restart(); Fingerprint graphProof; graphProof.graph(graph); const auto graphHash = graphProof.result(); const auto hashMs = timer.elapsed();
    timer.restart(); const auto projection = Canon::projectOmf(graph, cancellation); const auto projectMs = timer.elapsed();
    Fingerprint projectionProof; projectionProof.projection(projection); const auto projectionHash = projectionProof.result();
    const QJsonObject result{{"path", path}, {"outcome", int(graph.outcome)}, {"container", int(graph.container)},
        {"graphSha256", QString::fromLatin1(graphHash)}, {"projectionSha256", QString::fromLatin1(projectionHash)},
        {"objects", graphProof.objects}, {"relationships", graphProof.relationships}, {"properties", graphProof.properties},
        {"propertyCapacity", graphProof.capacity}, {"encodingBytes", graphProof.encodingBytes},
        {"projectedFiles", projection.files.size()}, {"projectedMasters", projection.masters.size()},
        {"parseMs", parseMs}, {"graphHashMs", hashMs}, {"projectMs", projectMs},
        {"footprintBefore", memoryBefore}, {"footprintParsed", memoryParsed},
        {"graphSerializedBytes", graphProof.sink.serializedBytes}, {"projectionSerializedBytes", projectionProof.sink.serializedBytes}};
    std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
}
