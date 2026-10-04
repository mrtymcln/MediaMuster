#include "canon/avbreader.h"
#include "canon/avbreferences.h"
#include <QSet>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <sys/resource.h>

class CountedFile final : public QFile
{
public:
	using QFile::QFile;
	qint64 bytesRequested = 0, bytesReturned = 0;
protected:
	qint64 readData(char *data, qint64 count) override
	{
		bytesRequested += count;
		const auto got = QFile::readData(data, count);
		if (got > 0) bytesReturned += got;
		return got;
	}
};
QString outcome(Canon::ParsedSource::Outcome value)
{
	using O = Canon::ParsedSource::Outcome;
	switch (value)
	{
	case O::NotRead:return "NotRead";case O::Complete:return "Complete";case O::Incomplete:return "Incomplete";
	case O::Malformed:return "Malformed";case O::Unsupported:return "Unsupported";case O::IoError:return "IoError";
	case O::Cancelled:return "Cancelled";
	}
	return {};
}
QString issueName(Canon::AvbReferenceIssue::Kind kind)
{
	using K = Canon::AvbReferenceIssue::Kind;
	switch(kind)
	{
	case K::InvalidSelection:return "InvalidSelection"; case K::IncompleteSource:return "IncompleteSource";
	case K::UnresolvedReference:return "UnresolvedReference";case K::AmbiguousReference:return "AmbiguousReference";
	case K::UnsupportedIdentity:return "UnsupportedIdentity";case K::Cancelled:return "Cancelled";
	}
	return {};
}
QJsonObject resolution(const Canon::AvbResolution &result)
{
	QJsonObject counts;
	QJsonArray issues;
	for (const auto &issue : result.issues)
	{
		const auto kind = issueName(issue.kind);
		counts[kind] = counts[kind].toInt() + 1;
		if (issues.size() < 200) issues.append(QJsonObject{{"kind", kind}, {"source", qint64(issue.object.source)}, {"object", qint64(issue.object.object)}, {"property", issue.property.name}, {"explanation", issue.explanation}});
	}
	QSet<QByteArray> fullIds, legacyIds;
	for (const auto &reference : result.media)
	{
		if (!reference.mobId.isEmpty()) fullIds.insert(reference.mobId);
		if (!reference.legacyId.isEmpty()) legacyIds.insert(reference.legacyId);
	}
	QJsonObject terminalCounts;
	for (const auto &terminal : result.terminals)
	{
		QString kind = terminal.kind == Canon::AvbTerminalReference::Kind::Null ? "Null" : "Filler";
		terminalCounts[kind] = terminalCounts[kind].toInt() + 1;
	}
	return {{"complete", result.complete}, {"cancelled", result.cancelled}, {"roots", result.roots.size()},
		{"edges", result.edges.size()}, {"mediaLocators", result.media.size()}, {"uniqueFullMediaIds", fullIds.size()},
		{"uniqueLegacyMediaIds", legacyIds.size()}, {"terminalCounts", terminalCounts}, {"issueCounts", counts}, {"issueExamples", issues}};
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTextStream output(stdout);
	for (const auto &path : app.arguments().sliced(1))
	{
		const QFileInfo before(path);
		CountedFile input(path);
		QJsonObject report{{"path", path}, {"fileSize", before.size()}};
		if (!input.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
		{
			report["openError"] = input.errorString();
			output << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
			continue;
		}
		Canon::Cancellation cancellation;
		auto receipt = QSharedPointer<SourceSnapshot>::create(); receipt->path = path;
		QElapsedTimer timer; timer.start();
		const auto result = Canon::AvbReader{}.read(input, {receipt, cancellation});
		report["elapsedMs"] = double(timer.nsecsElapsed()) / 1000000;
		report["outcome"] = outcome(result.outcome);
		struct rusage usage {};
		if (getrusage(RUSAGE_SELF, &usage) == 0) report["processPeakRssBytes"] = qint64(usage.ru_maxrss);
		report["objects"] = result.objects.size();
		report["unownedProperties"] = result.unownedProperties.size();
		report["bytesRequested"] = input.bytesRequested;
		report["bytesReturned"] = input.bytesReturned;
		QJsonArray inspected;
		const QSet<Canon::ObjectHandle> inspect{83291,83292,83307,83308,83323,83324};
		for (const auto &object : result.objects)
		{
			if (!inspect.contains(object.handle)) continue;
			QJsonArray fields, incoming, outgoing;
			for (const auto &field : object.properties)
				if (!field.locator.name.startsWith('@')) fields.append(QJsonObject{{"name",field.locator.name},{"value",QJsonValue::fromVariant(field.decoded)},{"bytes",QString::fromLatin1(field.encoding.toHex())}});
			for (const auto &relation : result.relationships)
			{
				if (relation.origin == object.handle) outgoing.append(QJsonObject{{"property",relation.locator.name},{"target",qint64(relation.target)}});
				if (relation.target == object.handle) incoming.append(QJsonObject{{"property",relation.locator.name},{"origin",qint64(relation.origin)}});
			}
			inspected.append(QJsonObject{{"object",qint64(object.handle)},{"class",QString::fromLatin1(object.avb->classId)},{"fields",fields},{"incoming",incoming},{"outgoing",outgoing}});
		}
		report["inspectedObjects"] = inspected;
		QJsonObject classes, incompleteClasses, referenceCounts;
		QJsonArray partialObjects, unreadableProperties, sequences;
		qint64 properties = 0, rawBytes = 0, rangeOnly = 0, typedMobIds = 0;
		for (const auto &object : result.objects)
		{
			const auto cls = object.avb ? QString::fromLatin1(object.avb->classId) : QStringLiteral("missing-context");
			classes[cls] = classes[cls].toInt() + 1;
			if (!object.avb || !object.avb->interpretationComplete)
			{
				incompleteClasses[cls] = incompleteClasses[cls].toInt() + 1;
				if (partialObjects.size() < 100) partialObjects.append(QJsonObject{{"object", qint64(object.handle)}, {"class", cls}, {"offset", object.avb ? object.avb->value.offset : -1}});
			}
			QString name;
			int mobType = -1, usage = -1;
			for (const auto &property : object.properties)
			{
				++properties; rawBytes += property.encoding.size(); rangeOnly += !property.bytesRetained;
				const auto &field = property.locator.name;
				if (field == "Component.name") name = property.decoded.toString();
				if (field == "Composition.mob_type") mobType = property.decoded.toInt();
				if (field == "Composition.usage_code") usage = property.decoded.toInt();
				if (field.endsWith(".mob_id") && property.decoded.toByteArray().size() == 32) ++typedMobIds;
				if (property.state == PropertyReadState::Unreadable)
					unreadableProperties.append(QJsonObject{{"object", qint64(object.handle)}, {"class", cls}, {"property", field}, {"length", property.encoding.size()}, {"interpretation", property.interpretation}});
			}
			if (mobType == 1 && usage == 0) sequences.append(QJsonObject{{"object", qint64(object.handle)}, {"name", name}, {"mobId", QString::fromLatin1(object.recordedIdentity.toHex())}});
		}
		for (const auto &property : result.unownedProperties) rawBytes += property.encoding.size();
		for (const auto &relation : result.relationships)
		{
			const auto key = relation.referenceEncoding;
			auto counts = referenceCounts[key].toObject();
			const QString status = relation.target ? QStringLiteral("resolved")
				: key == "AVB.ObjectIndex" && relation.recordedReference.toULongLong() == 0 ? QStringLiteral("null") : QStringLiteral("unresolved");
			counts[status] = counts[status].toInt() + 1;
			referenceCounts[key] = counts;
		}
		report["objectProperties"] = properties;
		report["rawEncodingBytesRetained"] = rawBytes;
		report["rangeOnlyProperties"] = rangeOnly;
		report["typedMobIdObservations"] = typedMobIds;
		report["classCounts"] = classes;
		report["incompleteClassCounts"] = incompleteClasses;
		report["partialObjectExamples"] = partialObjects;
		report["unreadableProperties"] = unreadableProperties;
		report["referenceCounts"] = referenceCounts;
		report["sequenceCandidates"] = sequences;
		report["diagnostics"] = QJsonArray::fromStringList(result.diagnostics);
		timer.restart();
		auto parsed = QSharedPointer<Canon::ParsedSource>::create(result);
		Canon::AvbReferenceIndex index({parsed}, cancellation);
		report["referenceIndexMs"] = double(timer.nsecsElapsed()) / 1000000;
		QJsonArray selectedResults;
		for (const auto &sequence : index.sequences())
		{
			timer.restart();
			const auto resolved = index.resolve({{0, Canon::AvbScope::Kind::SelectedSequences, {sequence.key.object}}}, cancellation);
			const auto ms = double(timer.nsecsElapsed()) / 1000000;
			auto item = resolution(resolved);
			item["object"] = qint64(sequence.key.object);
			item["name"] = sequence.name;
			item["mobId"] = QString::fromLatin1(sequence.mobId.toHex());
			item["userPlaced"] = sequence.userPlaced;
			item["resolveMs"] = ms;
			selectedResults.append(item);
		}
		report["sequenceResolutions"] = selectedResults;
		timer.restart();
		const auto whole = index.resolve({{0, Canon::AvbScope::Kind::EntireBin, {}}}, cancellation);
		const auto wholeMs = double(timer.nsecsElapsed()) / 1000000;
		auto wholeReport = resolution(whole); wholeReport["resolveMs"] = wholeMs;
		report["entireBinResolution"] = wholeReport;
		if (getrusage(RUSAGE_SELF, &usage) == 0) report["processPeakRssIncludingResolutionBytes"] = qint64(usage.ru_maxrss);
		const QFileInfo after(path);
		report["sizeAndMtimeUnchanged"] = before.size() == after.size() && before.lastModified() == after.lastModified();
		output << QJsonDocument(report).toJson(QJsonDocument::Compact) << '\n';
		output.flush();
	}
}
