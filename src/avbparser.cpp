// Compatibility view of MediaEngine's retained bin graph. The reader owns decoding;
// this adapter supplies the existing table and whole-bin filter contracts.
#include "avbparser.h"
#include "mediaengine/avbreader.h"
#include "mediaengine/avbreferences.h"
#include "diagnostics.h"
#include "omfuid.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <string_view>

namespace
{
	using namespace std::string_view_literals;
	constexpr auto littleHeader = "\x06\x00"
								  "DomainDJBO\x07\x00"
								  "AObjDoc"sv;
	constexpr auto bigHeader = "\x00\x06"
							   "DomainOBJD\x00\x07"
							   "AObjDoc"sv;
	using Object = MediaEngine::AvidObject;
	using Property = MediaEngine::RawProperty;

	const Property *field(const Object &object, const QString &name, QStringList &warnings, bool nonEmptyText = false)
	{
		const Property *found = nullptr;
		for (const auto &value : object.properties)
		{
			if (value.locator.name != name || value.state != PropertyReadState::Present || !value.decoded.isValid())
				continue;
			if (nonEmptyText && (value.decoded.metaType().id() != QMetaType::QString || value.decoded.toString().isEmpty()))
				continue;
			if (found && found->decoded != value.decoded)
			{
				warnings.append(QStringLiteral("Object %1 has conflicting %2 observations; no value was selected.").arg(object.handle).arg(name));
				return nullptr;
			}
			found = &value;
		}
		return found;
	}
	bool hasField(const Object &object, const QString &name)
	{
		return std::any_of(object.properties.cbegin(), object.properties.cend(), [&](const auto &value)
						   { return value.locator.name == name; });
	}
	bool nonzero(const QByteArray &bytes)
	{
		return std::any_of(bytes.cbegin(), bytes.cend(), [](char byte)
						   { return byte != 0; });
	}
	QString compositionId(const Object &object, QStringList &warnings)
	{
		if (hasField(object, QStringLiteral("Composition.mob_id")))
		{
			const auto *id = field(object, QStringLiteral("Composition.mob_id"), warnings);
			if (!id)
				return {};
			const auto raw = id->decoded.toByteArray();
			return raw.size() == 32 ? BinFileId::fromMobId(MobId::format(raw)).fullId : QString{};
		}
		if (!object.avb || !object.avb->interpretationComplete)
			return {}; // An unread tail may contain a modern ID.
		const auto *first = field(object, QStringLiteral("Composition.legacy_word0"), warnings);
		const auto *second = field(object, QStringLiteral("Composition.legacy_word1"), warnings);
		if (!first || !second)
			return {};
		QByteArray legacy(8, Qt::Uninitialized);
		qToLittleEndian(first->decoded.toUInt(), legacy.data());
		qToLittleEndian(second->decoded.toUInt(), legacy.data() + 4);
		return nonzero(legacy) ? OmfUid::toMobIdText(reinterpret_cast<const uchar *>(legacy.constData())) : QString{};
	}
	MetadataObservation observation(const Object &object, const Property &field)
	{
		MetadataObservation result;
		result.snapshot = object.snapshot;
		result.property = field.locator.name;
		result.objectIdentity = QString::number(object.handle);
		result.value = field.decoded;
		result.rawValue = field.encoding;
		result.readState = field.state;
		result.textEncoding = field.textEncoding;
		result.textEncodingBasis = field.textEncodingBasis;
		if (field.textEncodingBasis == EvidenceBasis::Derived)
			result.basis = EvidenceBasis::Derived;
		result.explanation = QStringLiteral("Recorded AVB object %1, matched through this file's master association.").arg(object.handle);
		if (!field.interpretation.isEmpty())
			result.explanation += QLatin1Char(' ') + field.interpretation;
		return result;
	}
	struct OriginalBin
	{
		QString name;
		QString uid;
		QVector<MetadataObservation> observations;
	};
	OriginalBin originalBin(const Object &composition, const QHash<MediaEngine::ObjectHandle, const Object *> &objects,
							QStringList &warnings, const MediaEngine::Cancellation &cancel)
	{
		const auto *attributes = field(composition, QStringLiteral("Component.attributes"), warnings);
		const auto *table = attributes ? objects.value(attributes->decoded.toULongLong()) : nullptr;
		if (!table || !table->avb || table->avb->classId != "ATTR")
			return {};
		OriginalBin selected;
		bool found = false, conflict = false;
		for (const auto &name : table->properties)
		{
			if (cancel.cancelled())
				return {};
			if (!name.locator.name.startsWith(QLatin1String("Attributes.items[")) || !name.locator.name.endsWith(QLatin1String("].name")) ||
				name.state != PropertyReadState::Present || name.decoded.toString() != QLatin1String("_ORG_BIN"))
				continue;
			const auto prefix = name.locator.name.chopped(5);
			const auto *type = field(*table, prefix + QStringLiteral(".type"), warnings);
			const auto *reference = field(*table, prefix + QStringLiteral(".value"), warnings);
			const auto *bin = reference ? objects.value(reference->decoded.toULongLong()) : nullptr;
			if (!type || type->decoded.toUInt() != 3 || !bin)
				continue;
			if (!bin->avb || bin->avb->classId != "MCBR")
			{
				warnings.append(QStringLiteral("AVB _ORG_BIN refers to object %1 which is not a bin reference; its annotation was not used.").arg(bin->handle));
				conflict = true;
				continue;
			}
			const auto *modern = field(*bin, QStringLiteral("BinRef.name_utf8"), warnings, true);
			const auto *legacy = field(*bin, QStringLiteral("BinRef.name"), warnings, true);
			const bool usableModern = std::any_of(bin->properties.cbegin(), bin->properties.cend(), [](const Property &property)
												  { return property.locator.name == QLatin1String("BinRef.name_utf8") && property.state == PropertyReadState::Present &&
														   property.decoded.metaType().id() == QMetaType::QString && !property.decoded.toString().isEmpty(); });
			const auto *high = field(*bin, QStringLiteral("BinRef.uid_high"), warnings);
			const auto *low = field(*bin, QStringLiteral("BinRef.uid_low"), warnings);
			for (const auto &property : bin->properties)
				if (property.locator.name == QLatin1String("BinRef.name") || property.locator.name == QLatin1String("BinRef.name_utf8"))
				{
					auto evidence = observation(*bin, property);
					evidence.eligible = property.locator.name == QLatin1String("BinRef.name_utf8")
											? usableModern
											: !usableModern;
					if (!evidence.eligible)
						evidence.explanation += property.locator.name == QLatin1String("BinRef.name")
													? QStringLiteral(" The non-empty UTF-8 bin name takes precedence over the legacy name.")
													: QStringLiteral(" The modern bin-name field is empty or unreadable.");
					selected.observations.append(std::move(evidence));
				}
			OriginalBin value;
			if (usableModern)
			{
				if (modern)
					value.name = modern->decoded.toString();
			}
			else if (legacy)
				value.name = legacy->decoded.toString();
			if (high && low)
				value.uid = QStringLiteral("%1%2").arg(quint32(high->decoded.toInt()), 8, 16, QLatin1Char('0')).arg(quint32(low->decoded.toInt()), 8, 16, QLatin1Char('0'));
			if (found && (selected.name != value.name || selected.uid != value.uid))
			{
				warnings.append(QStringLiteral("Object %1 has conflicting original-bin references; no original bin was selected.").arg(composition.handle));
				conflict = true;
			}
			selected.name = value.name;
			selected.uid = value.uid;
			found = true;
		}
		if (conflict)
		{
			selected.name.clear();
			selected.uid.clear();
			for (auto &value : selected.observations)
				value.eligible = false;
		}
		return selected;
	}
}

AvbHeaderCheck AvbParser::inspectHeader(const QString &path)
{
	const QFileInfo info(path);
	if (!info.exists())
		return {false, QStringLiteral("This bin file no longer exists.")};
	if (!info.isFile())
		return {false, QStringLiteral("This path is not a regular file.")};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
		return {false, file.errorString()};
	std::array<char, littleHeader.size()> bytes{};
	if (file.read(bytes.data(), bytes.size()) != qint64(bytes.size()))
		return {false, file.error() == QFileDevice::NoError ? QStringLiteral("This file is not an Avid bin.") : file.errorString()};
	const std::string_view signature(bytes.data(), bytes.size());
	return signature == littleHeader || signature == bigHeader ? AvbHeaderCheck{true, {}}
															   : AvbHeaderCheck{false, QStringLiteral("This file is not an Avid bin.")};
}

AvbBin AvbParser::parse(const QString &path, const std::atomic_bool *cancelled)
{
	AvbBin bin;
	bin.filePath = path;
	bin.displayName = QFileInfo(path).completeBaseName();
	MediaEngine::Cancellation cancel(cancelled);
	if (cancel.cancelled())
	{
		bin.error = QStringLiteral("Bin reading cancelled.");
		return bin;
	}
	if (!QFileInfo(path).isFile())
	{
		bin.error = QStringLiteral("AVB path is not a regular file.");
		return bin;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
	{
		bin.error = file.errorString();
		return bin;
	}
	const auto originalSize = file.size();
	const auto originalModified = file.fileTime(QFileDevice::FileModificationTime);
	auto snapshot = QSharedPointer<SourceSnapshot>::create();
	snapshot->path = path;
	snapshot->source = MetadataSource::Avb;
	auto source = QSharedPointer<MediaEngine::ParsedSource>::create(MediaEngine::AvbReader{}.read(file, {snapshot, cancel}));
	bin.source = source;
	using Outcome = MediaEngine::ParsedSource::Outcome;
	MediaEngine::ObjectHandle root = 0;
	for (const auto &property : source->unownedProperties)
		if (property.locator.name == QLatin1String("Header.root_index") && property.state == PropertyReadState::Present)
			root = property.decoded.toULongLong();
	const bool readable = std::any_of(source->objects.cbegin(), source->objects.cend(), [root](const auto &object)
									  { return root && object.handle == root && object.avb && (object.avb->classId == "ABIN" || object.avb->classId == "BINF") &&
											   std::any_of(object.properties.cbegin(), object.properties.cend(), [](const auto &property)
														   { return property.locator.name == QLatin1String("Bin.version") && property.state == PropertyReadState::Present; }); });
	const auto changed = [&]
	{ return file.size() != originalSize || file.fileTime(QFileDevice::FileModificationTime) != originalModified; };
	if (!readable || cancel.cancelled() || changed())
	{
		bin.error = cancel.cancelled() ? QStringLiteral("Bin reading cancelled.") : changed() ? QStringLiteral("AVB file changed while reading; load it again.")
																							  : source->diagnostics.join(QStringLiteral("; "));
		if (bin.error.isEmpty())
			bin.error = QStringLiteral("The bin document could not be read.");
		qCWarning(lcAvb) << "cannot parse" << path << bin.error;
		return bin;
	}
	bin.warnings = source->diagnostics;
	MediaEngine::AvbReferenceIndex index({source}, cancel);
	auto resolution = QSharedPointer<MediaEngine::AvbResolution>::create(index.resolve({{0, MediaEngine::AvbScope::Kind::EntireBin, {}}}, cancel));
	bin.resolution = resolution;
	for (const auto &issue : resolution->issues)
		bin.warnings.append(QStringLiteral("Object %1: %2").arg(issue.object.object).arg(issue.explanation));
	QHash<MediaEngine::ObjectHandle, const Object *> objects;
	for (const auto &object : source->objects)
		objects.insert(object.handle, &object);
	for (const auto &relation : source->relationships)
	{
		if (cancel.cancelled())
			break;
		if (!relation.target)
			continue;
		const auto *target = objects.value(relation.target);
		const auto &name = relation.locator.name;
		const QByteArray expected = name.startsWith(QLatin1String("Bin.items[")) && name.endsWith(QLatin1String("].mob")) ? QByteArrayLiteral("CMPO")
									: name == QLatin1String("Component.attributes")										  ? QByteArrayLiteral("ATTR")
																														  : QByteArray{};
		if (!expected.isEmpty() && target && (!target->avb || target->avb->classId != expected))
		{
			bin.warnings.append(QStringLiteral("AVB %1 refers to object %2 with an incompatible class; the reference was not used for metadata.").arg(name).arg(relation.target));
		}
	}
	for (const auto &object : source->objects)
	{
		if (cancel.cancelled())
			break;
		if (!object.avb || object.avb->classId != "CMPO")
			continue;
		AvbMob mob;
		mob.mobId = compositionId(object, bin.warnings);
		if (mob.mobId.isEmpty())
			continue;
		if (const auto *name = field(object, QStringLiteral("Component.name"), bin.warnings))
			mob.name = name->decoded.toString();
		for (const auto &property : object.properties)
			if (property.locator.name == QLatin1String("Component.name"))
				mob.nameObservations.append(observation(object, property));
		if (const auto *type = field(object, QStringLiteral("Composition.mob_type"), bin.warnings))
			mob.mobType = type->decoded.toInt();
		if (const auto *usage = field(object, QStringLiteral("Composition.usage_code"), bin.warnings))
			mob.usageCode = usage->decoded.toInt();
		const auto original = originalBin(object, objects, bin.warnings, cancel);
		mob.originalBin = original.name;
		mob.originalBinUid = original.uid;
		mob.originalBinObservations = original.observations;
		bin.mobs.append(std::move(mob));
	}
	for (const auto &reference : resolution->media)
	{
		if (cancel.cancelled())
			break;
		if (reference.mobId.size() == 32)
			bin.mediaFileIds.add(BinFileId::fromMobId(MobId::format(reference.mobId)));
		else if (reference.legacyId.size() == 8)
			bin.mediaFileIds.add(BinFileId::fromLegacyWords(qFromLittleEndian<quint32>(reference.legacyId.constData()), qFromLittleEndian<quint32>(reference.legacyId.constData() + 4)));
	}
	if (cancel.cancelled() || resolution->cancelled || !bin.error.isEmpty() || changed())
	{
		bin.mobs.clear();
		bin.mediaFileIds = {};
		if (bin.error.isEmpty())
			bin.error = changed() ? QStringLiteral("AVB file changed while reading; load it again.") : QStringLiteral("Bin reading cancelled.");
		return bin;
	}
	std::sort(bin.mobs.begin(), bin.mobs.end(), [](const auto &a, const auto &b)
			  {
		if (a.mobId != b.mobId) return a.mobId < b.mobId;
		if (a.name != b.name) return a.name < b.name;
		if (a.originalBinUid != b.originalBinUid) return a.originalBinUid < b.originalBinUid;
		return a.originalBin < b.originalBin; });
	bin.warnings.removeDuplicates();
	bin.valid = true;
	bin.complete = source->outcome == Outcome::Complete && resolution->complete && bin.warnings.isEmpty();
	return bin;
}
