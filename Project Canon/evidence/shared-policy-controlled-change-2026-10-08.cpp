// Two independent builds exercise developer-edited display preferences using
// authored observations. No parser, scanner I/O or media mutation is performed.
#include "canon/metadataselectionpolicy.h"
#include "canon/scanengine.h"
#include "canonadapter.h"
#include "binmetadataresolver.h"
#include "avbparser.h"
#include "mediacsv.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

namespace
{
	MetadataObservation recorded(MetadataSource kind, const QVariant &value, const char *property)
	{
		MetadataObservation item;
		item.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			kind, QStringLiteral("authored-source-%1").arg(int(kind)), {}, SourceReadState::Complete});
		item.property = QString::fromLatin1(property);
		item.objectIdentity = QStringLiteral("master:authored");
		item.value = value;
		item.rawValue = value.toString().toUtf8();
		item.readState = PropertyReadState::Present;
		item.readReason = PropertyReadReason::None;
		item.basis = EvidenceBasis::Recorded;
		item.freshness = SourceFreshness::Unknown;
		item.explanation = QStringLiteral("Authored associated-master observation; no format claim.");
		return item;
	}

	QJsonObject observationJson(const MetadataObservation &item)
	{
		return {{"source", int(item.snapshot->source)}, {"source_path", item.snapshot->path},
			{"source_read_state", int(item.snapshot->readState)}, {"property", item.property},
			{"object", item.objectIdentity}, {"value", QJsonValue::fromVariant(item.value)},
			{"raw_hex", QString::fromLatin1(item.rawValue.toByteArray().toHex())},
			{"read_state", int(item.readState)}, {"read_reason", int(item.readReason)},
			{"basis", int(item.basis)}, {"freshness", int(item.freshness)},
			{"eligible", item.eligible}, {"explanation", item.explanation}};
	}

	QJsonArray observationsJson(const QVector<MetadataObservation> &items, qsizetype count)
	{
		QJsonArray result;
		for (qsizetype index = 0; index < count; ++index)
			result.append(observationJson(items[index]));
		return result;
	}

	QString digest(const QJsonArray &items)
	{
		return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(items).toJson(
			QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
	}
}

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	if (argc != 2)
		return 2;
	const bool databasePreferred = QByteArray(argv[1]) == "mdb";
	const QString headerName = QStringLiteral("Sequence,3D_Warp+1");
	const QString databaseName = QStringLiteral("Sequence,Color_Correction+1");
	const QString expected = databasePreferred ? databaseName : headerName;
	const QString master = QStringLiteral("authored-master-id");
	const QString fileId = QStringLiteral("authored-file-id");
	MediaEvidence evidence;
	evidence.observe(MediaProperty::ClipName,
		recorded(MetadataSource::Mxf, headerName, "MaterialPackage.Name"));
	evidence.observe(MediaProperty::ClipName,
		recorded(MetadataSource::Mdb, databaseName, "MOBJ.Name"));
	evidence.observe(MediaProperty::Type,
		recorded(MetadataSource::Mxf, int(MediaType::Precompute), "MaterialPackage.UsageCode"));
	evidence.observe(MediaProperty::MasterMobId,
		recorded(MetadataSource::Mxf, master, "MaterialPackage.PackageUID"));
	evidence.observe(MediaProperty::FileMobId,
		recorded(MetadataSource::Mxf, fileId, "SourcePackage.PackageUID"));
	const auto originalNames = evidence.observations(MediaProperty::ClipName);
	const auto originalJson = observationsJson(originalNames, originalNames.size());
	Canon::selectMetadata(evidence);
	const auto scanSelection = evidence.selected(MediaProperty::ClipName);

	MediaFile row;
	row.kelpieId = 41;
	row.mediaFilePath = QStringLiteral("/authored/row.mxf");
	row.fileName = QStringLiteral("row.mxf");
	row.scanStamp = {row.mediaFilePath, QStringLiteral("authored-volume"), {}, fileId, {master}};
	row.canonScan = QSharedPointer<Canon::ScanResult>::create();
	row.evidence = evidence;
	applyResolvedMetadata(row);
	const QString initialDisplay = row.clipName;
	row.clipName = QStringLiteral("stale previously displayed name");
	row.clipNameSource = MediaFile::ClipNameSource::MaterialPackage;

	AvbBin bin;
	bin.valid = true;
	bin.complete = true;
	bin.filePath = QStringLiteral("authored-bin.avb");
	auto source = QSharedPointer<Canon::ParsedSource>::create();
	const auto binName = recorded(MetadataSource::Avb, QStringLiteral("bin fallback"), "Component.name");
	source->snapshot = binName.snapshot;
	bin.source = source;
	AvbMob mob;
	mob.mobId = master;
	mob.mobType = AvbMob::masterMobType;
	mob.name = binName.value.toString();
	mob.nameObservations.append(binName);
	bin.mobs.append(mob);
	BinMetadataResolver resolver;
	resolver.setBins({bin});
	const bool refreshChanged = resolver.apply(row);
	const auto binSelection = row.evidence.selected(MediaProperty::ClipName);
	const auto names = row.evidence.observations(MediaProperty::ClipName);
	const auto retainedJson = observationsJson(names, originalNames.size());
	const bool originalsPreserved = retainedJson == originalJson &&
		names[0].snapshot == originalNames[0].snapshot && names[1].snapshot == originalNames[1].snapshot;
	const bool physicalPreserved = row.kelpieId == 41 && row.mediaFilePath == QStringLiteral("/authored/row.mxf") &&
		row.fileName == QStringLiteral("row.mxf") && row.scanStamp.path == row.mediaFilePath &&
		row.scanStamp.mobId == fileId && row.scanStamp.masterMobIds == QStringList{master} &&
		row.scanStamp.volumeIdentifier == QStringLiteral("authored-volume");
	const bool repeatChanged = resolver.apply(row);
	const bool directRefreshChanged = applyResolvedMetadata(row);
	const auto csv = MediaCsv::rowLine(row, {true, false});
	const bool csvAgrees = csv.startsWith('"' + expected + QStringLiteral("\","));
	const bool effectsAgree = row.effect == row.evidence.selected(MediaProperty::Effect).value.toString() &&
		row.effectCategory == row.evidence.selected(MediaProperty::EffectCategory).value.toString() &&
		row.effectSequence == row.evidence.selected(MediaProperty::EffectSequence).value.toString();
	const auto &policy = Canon::propertyPolicy(MediaProperty::ClipName);
	const auto expectedSource = databasePreferred ? MetadataSource::Mdb : MetadataSource::Mxf;
	const bool sourceAgrees = binSelection.selectedObservation >= 0 &&
		names[binSelection.selectedObservation].snapshot->source == expectedSource;
	const bool success = scanSelection.value.toString() == expected && initialDisplay == expected &&
		binSelection.value.toString() == expected && row.clipName == expected &&
		originalsPreserved && physicalPreserved && csvAgrees && effectsAgree && sourceAgrees &&
		refreshChanged && !repeatChanged && !directRefreshChanged;
	QJsonObject output{{"build", databasePreferred ? "copied-table-mdb-preferred" : "original-table-header-preferred"},
		{"success", success}, {"policy_version", int(policy.version)},
		{"clip_name_priorities", QJsonObject{{"mdb", policy.sources.mdb}, {"mxf", policy.sources.mxf},
			{"omf", policy.sources.omf}, {"avb", policy.sources.avb}}},
		{"scan_selection", scanSelection.value.toString()}, {"initial_adapter_value", initialDisplay},
		{"bin_selection", binSelection.value.toString()}, {"display_value", row.clipName},
		{"selected_source", int(expectedSource)}, {"clip_name_source_flag", int(row.clipNameSource)},
		{"selected_rule_version", int(binSelection.ruleVersion)},
		{"raw_originals_preserved", originalsPreserved}, {"raw_originals_sha256", digest(originalJson)},
		{"retained_originals_sha256", digest(retainedJson)}, {"kelpie_id", qint64(row.kelpieId)},
		{"physical_fields_and_stamp_preserved", physicalPreserved}, {"refresh_changed", refreshChanged},
		{"identical_bin_reapply_changed", repeatChanged}, {"direct_repeat_refresh_changed", directRefreshChanged},
		{"csv_clip_name_agrees", csvAgrees}, {"effects_match_final_selection", effectsAgree},
		{"effect", row.effect}, {"effect_category", row.effectCategory}, {"effect_sequence", row.effectSequence},
		{"name_observation_count", names.size()}, {"original_observations", originalJson},
		{"csv_header", MediaCsv::headerLine({true, false})}, {"csv_row", csv},
		{"scope", "Authored owner-qualified observations; no parser or real-media reads or mutations."}};
	std::cout << QJsonDocument(output).toJson().constData();
	return success ? 0 : 1;
}
