#include "canon/mdbreader.h"
#include "canon/legacyreader.h"
#include "canon/mxfreader.h"
#include "canon/pmrreader.h"
#include "canon/avbreader.h"
#include <sys/resource.h>
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
    explicit Fingerprint(QIODevice *device = nullptr) : stream(device ? device : &sink) { stream.setVersion(QDataStream::Qt_6_0); stream.setByteOrder(QDataStream::BigEndian); }
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

// Temporary feasibility measurement only. No production source/model changes.
// Stream bounded blocks: never allocate the complete uncompressed serialization.
class ChunkArchive final : public QIODevice {
public:
    struct Block { QByteArray packed; qsizetype rawBytes; };
    explicit ChunkArchive(int level) : compressionLevel(level) {
        buffer.reserve(kBlockBytes); open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    }
    void finish() { flushBlock(); blocks.squeeze(); }
    QByteArray unpackedDigest(bool &complete) const {
        QCryptographicHash hash(QCryptographicHash::Sha256); complete = true;
        for (const auto &block : blocks) {
            const QByteArray bytes = qUncompress(block.packed);
            if (bytes.size() != block.rawBytes) { complete = false; return {}; }
            hash.addData(bytes);
        }
        return hash.result().toHex();
    }
    QVector<Block> blocks;
    qint64 rawBytes = 0, packedBytes = 0, allocatedPackedBytes = 0;
    static constexpr qsizetype kBlockBytes = 65536;
protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 size) override {
        const qint64 requested = size; rawBytes += size;
        while (size > 0) {
            const qint64 count = qMin<qint64>(size, kBlockBytes - buffer.size());
            buffer.append(data, count); data += count; size -= count;
            if (buffer.size() == kBlockBytes) flushBlock();
        }
        return requested;
    }
private:
    void flushBlock() {
        if (buffer.isEmpty()) return;
        QByteArray packed = qCompress(buffer, compressionLevel);
        if (packed.isEmpty()) qFatal("Archive block compression failed");
        packed.squeeze(); // qCompress output otherwise retains near-uncompressed capacity.
        allocatedPackedBytes += packed.capacity();
        packedBytes += packed.size(); blocks.append({std::move(packed), buffer.size()});
        buffer.clear();
    }
    const int compressionLevel;
    QByteArray buffer;
};
qint64 peakResident() {
    rusage usage{}; return getrusage(RUSAGE_SELF, &usage) == 0 ? qint64(usage.ru_maxrss) : -1;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 1;
    const QString mode = QString::fromLocal8Bit(argv[1]);
    const QString path = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    const QFileInfo originalInfo(path); const auto modified = originalInfo.lastModified();
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return 2;
    auto receipt = QSharedPointer<SourceSnapshot>::create(); receipt->path = path; receipt->modified = modified;
    const Canon::Cancellation cancellation; QElapsedTimer timer; timer.start();
    const auto memoryBefore = footprint();
    const auto suffix = originalInfo.suffix().toLower();
    Canon::ParsedSource graph;
    if (suffix == "mdb") graph = Canon::MdbReader{}.read(file, {receipt, cancellation});
    else if (suffix == "mxf") graph = Canon::MxfReader{}.read(file, {receipt, cancellation});
    else if (suffix == "pmr") graph = Canon::PmrReader{}.read(file, {receipt, cancellation});
    else if (suffix == "avb") graph = Canon::AvbReader{}.read(file, {receipt, cancellation});
    else graph = Canon::LegacyReader{}.read(file, {receipt, cancellation});
    const auto parseMs = timer.restart(), memoryParsed = footprint();
    auto projection = suffix == "avb" ? Canon::Projection{} : suffix == "mxf" ? Canon::projectMxf(graph, cancellation) : suffix == "pmr" ? Canon::projectPmr(graph, cancellation) : Canon::projectOmf(graph, cancellation);
    const auto projectMs = timer.restart(), memoryHot = footprint();
    Fingerprint graphProof; graphProof.graph(graph); const auto graphHash = graphProof.result();
    Fingerprint projectionProof; projectionProof.projection(projection); const auto projectionHash = projectionProof.result();
    QJsonObject output{{"mode",mode},{"path",path},{"sourceBytes",originalInfo.size()},
        {"memoryBefore",memoryBefore},{"memoryParsed",memoryParsed},{"memoryGraphAndProjection",memoryHot},
        {"parseMs",parseMs},{"projectMs",projectMs},{"graphSha256",QString::fromLatin1(graphHash)},
        {"projectionSha256",QString::fromLatin1(projectionHash)},
        {"objects",graphProof.objects},{"properties",graphProof.properties},{"relationships",graphProof.relationships},
        {"serializedBytes",graphProof.sink.serializedBytes}};
    if (mode == "packed") {
        ChunkArchive archive(1); timer.restart();
        { Fingerprint archiveWriter(&archive); archiveWriter.graph(graph);
          if (archiveWriter.stream.status() != QDataStream::Ok) return 3; }
        archive.finish();
        output.insert("packMs",timer.restart());
        output.insert("memoryGraphProjectionAndArchive",footprint());
        output.insert("archivePayloadBytes",archive.packedBytes);
        output.insert("archiveAllocatedByteCapacity",archive.allocatedPackedBytes);
        output.insert("archiveBlockSlotBytes",qint64(archive.blocks.capacity()*sizeof(ChunkArchive::Block)));
        output.insert("archiveBlockCount",archive.blocks.size());
        output.insert("blockBytes",ChunkArchive::kBlockBytes); output.insert("compressionLevel",1);
        graph = {}; // Preserve projected facts and the lossless serialized graph archive.
        output.insert("releaseGraphMs",timer.restart());
        output.insert("memoryArchiveAndProjection",footprint());
        bool complete = false; const auto restoredHash = archive.unpackedDigest(complete);
        output.insert("verifyMs",timer.restart());
        output.insert("unpackedSha256",QString::fromLatin1(restoredHash));
        output.insert("archiveExact",complete && restoredHash == graphHash && archive.rawBytes == graphProof.sink.serializedBytes);
        if (!complete || restoredHash != graphHash) return 4;
        Fingerprint retainedProjection; retainedProjection.projection(projection);
        const auto retainedHash = retainedProjection.result();
        output.insert("projectionAfterReleaseSha256",QString::fromLatin1(retainedHash));
        if (retainedHash != projectionHash) return 5;
    }
    const QFileInfo finalInfo(path);
    output.insert("sourceStampUnchanged", finalInfo.size() == originalInfo.size() && finalInfo.lastModified() == modified);
    output.insert("peakResidentBytes",peakResident());
    std::puts(QJsonDocument(output).toJson(QJsonDocument::Compact).constData());
}
