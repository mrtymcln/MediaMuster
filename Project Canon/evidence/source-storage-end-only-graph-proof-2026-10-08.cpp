#include "canon/mdbreader.h"
#include "canon/legacyreader.h"
#include "canon/pmrreader.h"
#include "canon/avbreader.h"
#include "canon/avbreferences.h"
#include "canon/projection.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <sys/resource.h>
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
    void key(const Canon::AvbObjectKey &value) { stream << qint64(value.source) << value.object; }
    void resolution(const Canon::AvbResolution &value) {
        stream << qint64(value.roots.size()); for (const auto &root : value.roots) key(root);
        stream << qint64(value.edges.size());
        for (const auto &edge : value.edges) { key(edge.origin); key(edge.target); stream << qint64(edge.relationship); }
        stream << qint64(value.media.size());
        for (const auto &media : value.media) { key(media.locator); stream << qint64(media.property) << media.mobId << media.legacyId; }
        stream << qint64(value.terminals.size());
        for (const auto &terminal : value.terminals) { key(terminal.object); stream << qint64(terminal.relationship) << qint32(terminal.kind); }
        stream << qint64(value.issues.size());
        for (const auto &issue : value.issues) { stream << qint32(issue.kind); key(issue.object); locator(issue.property); stream << issue.explanation; }
        stream << value.complete << value.cancelled;
    }
    QByteArray result() { if (stream.status() != QDataStream::Ok) qFatal("Serialization failed"); return sink.result(); }
};

qint64 footprint() {
    task_vm_info_data_t info{}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS ? qint64(info.phys_footprint) : -1;
}
qint64 peakResident() {
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? qint64(usage.ru_maxrss) : -1;
}
struct GraphStorage {
    struct Slots { qint64 count = 0, capacity = 0, elementBytes = 0; };
    QMap<QString, Slots> totals;
    template <typename T> void add(const char *name, const QVector<T> &values) {
        auto &total = totals[QString::fromLatin1(name)];
        total.count += values.size(); total.capacity += values.capacity(); total.elementBytes = sizeof(T);
    }
    void count(const Canon::ParsedSource &source) {
        add("objects", source.objects); add("relationships", source.relationships);
        add("recordSets", source.recordSets); add("embeddedSources", source.embeddedSources);
        add("unownedProperties", source.unownedProperties);
        add("diagnostics", static_cast<const QList<QString> &>(source.diagnostics));
        for (const auto &object : source.objects) add("objectProperties", object.properties);
        for (const auto &set : source.recordSets) add("recordSetObjects", set.objects);
        for (const auto &child : source.embeddedSources) count(child);
    }
    QJsonObject json() const {
        QJsonObject result;
        for (auto it = totals.cbegin(); it != totals.cend(); ++it) {
            const auto &value = it.value();
            result.insert(it.key(), QJsonObject{{"count", value.count}, {"capacity", value.capacity},
                {"elementBytes", value.elementBytes}, {"allocatedSlotBytes", value.capacity * value.elementBytes}});
        }
        return result;
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 1;
    const QString path = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return 2;
    const auto extension = QFileInfo(path).suffix().toLower();
    const bool database = extension == QStringLiteral("mdb"), pmr = extension == QStringLiteral("pmr"), avb = extension == QStringLiteral("avb");
    auto receipt = QSharedPointer<SourceSnapshot>::create(); receipt->path = path; receipt->modified = QFileInfo(path).lastModified();
    const Canon::Cancellation cancellation; QElapsedTimer timer; timer.start();
    const auto memoryBefore = footprint(), peakBefore = peakResident();
    const auto graph = [&]() {
        const Canon::ReaderContext context{receipt, cancellation};
        if (pmr) return Canon::PmrReader{}.read(file, context);
        if (avb) return Canon::AvbReader{}.read(file, context);
        if (database) return Canon::MdbReader{}.read(file, context);
        return Canon::LegacyReader{}.read(file, context);
    }();
    const auto parseMs = timer.elapsed(), memoryParsed = footprint(), peakParsed = peakResident();
    // Capture parse memory before hashing/projection allocate diagnostic storage.
    GraphStorage storage; storage.count(graph);
    timer.restart(); Fingerprint graphProof; graphProof.graph(graph); const auto graphHash = graphProof.result(); const auto hashMs = timer.elapsed();
    timer.restart();
    QJsonObject semantic;
    qint64 projectionBytes = 0;
    if (avb) {
        const Canon::AvbReferenceIndex index({QSharedPointer<Canon::ParsedSource>::create(graph)}, cancellation);
        Fingerprint referenceProof;
        referenceProof.stream << qint64(index.sequences().size());
        QVector<Canon::ObjectHandle> handles;
        for (const auto &sequence : index.sequences()) {
            referenceProof.key(sequence.key);
            referenceProof.stream << sequence.name << sequence.mobId << sequence.userPlaced;
            referenceProof.locator(sequence.membership); handles.append(sequence.key.object);
        }
        const auto whole = index.resolve({{0, Canon::AvbScope::Kind::EntireBin, {}}}, cancellation);
        const auto selected = index.resolve({{0, Canon::AvbScope::Kind::SelectedSequences, handles}}, cancellation);
        referenceProof.resolution(whole); referenceProof.resolution(selected);
        referenceProof.stream << qint64(handles.size());
        for (const auto handle : handles) {
            referenceProof.stream << handle;
            referenceProof.resolution(index.resolve({{0, Canon::AvbScope::Kind::SelectedSequences, {handle}}}, cancellation));
        }
        semantic = {{"avbResolutionSha256", QString::fromLatin1(referenceProof.result())},
                    {"sequenceCandidates", index.sequences().size()},
                    {"wholeBinRoots", whole.roots.size()}, {"wholeBinEdges", whole.edges.size()},
                    {"wholeBinMedia", whole.media.size()}, {"wholeBinIssues", whole.issues.size()},
                    {"wholeBinComplete", whole.complete}, {"selectedRoots", selected.roots.size()},
                    {"selectedEdges", selected.edges.size()}, {"selectedMedia", selected.media.size()},
                    {"selectedIssues", selected.issues.size()}, {"selectedComplete", selected.complete}};
        projectionBytes = referenceProof.sink.serializedBytes;
    } else {
        const auto projection = pmr ? Canon::projectPmr(graph, cancellation) : Canon::projectOmf(graph, cancellation);
        Fingerprint projectionProof; projectionProof.projection(projection);
        semantic = {{"projectionSha256", QString::fromLatin1(projectionProof.result())},
                    {"projectedFiles", projection.files.size()}, {"projectedMasters", projection.masters.size()}};
        projectionBytes = projectionProof.sink.serializedBytes;
    }
    const auto projectMs = timer.elapsed();
    QJsonObject result{{"path", path}, {"outcome", int(graph.outcome)}, {"container", int(graph.container)},
        {"graphSha256", QString::fromLatin1(graphHash)},
        {"objects", graphProof.objects}, {"relationships", graphProof.relationships}, {"properties", graphProof.properties},
        {"propertyCapacity", graphProof.capacity}, {"encodingBytes", graphProof.encodingBytes},
        {"parseMs", parseMs}, {"graphHashMs", hashMs}, {"projectMs", projectMs},
        {"footprintBefore", memoryBefore}, {"footprintParsed", memoryParsed}, {"peakResidentBefore", peakBefore},
        {"peakResidentParsed", peakParsed}, {"listStorage", storage.json()},
        {"graphSerializedBytes", graphProof.sink.serializedBytes}, {"projectionSerializedBytes", projectionBytes}};
    for (auto it = semantic.begin(); it != semantic.end(); ++it) result.insert(it.key(), it.value());
    std::puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
}
