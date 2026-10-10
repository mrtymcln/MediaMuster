// Keeps PMR records and their legacy/Unicode set provenance separate.
// Linking their recorded filenames to physical files belongs to ScanEngine.

#include "projection.h"

namespace MediaEngine
{
	Projection projectPmr(const ParsedSource &source, const Cancellation &cancellation)
	{
		Projection result;
		QHash<ObjectHandle, QString> setsByObject;
		QHash<ObjectHandle, qint32> versionsByObject;
		for (const auto &set : source.recordSets)
			for (const auto handle : set.objects)
			{
				if (cancellation.cancelled())
					return result;
				setsByObject.insert(handle, set.name);
				versionsByObject.insert(handle, set.version);
			}
		for (const auto &object : source.objects)
		{
			if (cancellation.cancelled())
				break;
			ProjectedFile file;
			file.objects.append({source.snapshot, object.handle});
			const bool bigEndian = object.identityEncoding.endsWith(QStringLiteral("/big-endian"));
			file.fileMobId = canonicalDatabaseId(object.recordedIdentity, bigEndian);
			const QString setName = setsByObject.value(object.handle);
			const QString owner = QStringLiteral("object:%1").arg(object.handle);
			file.evidence.registerSource(source.snapshot, owner, PropertyReadReason::NotStoredByFormat);
			// These fields belong to the known record layout. A partial record must
			// not turn an unattempted read into a format-omission claim.
			for (const auto field : {MediaProperty::Filename, MediaProperty::Project,
									MediaProperty::FileMobId, MediaProperty::MasterMobId})
				file.evidence.recordReadStatus(field, source.snapshot, owner,
					{PropertyReadState::NotRead, source.outcome == ParsedSource::Outcome::Incomplete
						? PropertyReadReason::SourceIncomplete : PropertyReadReason::CoverageNotEstablished,
					 PropertyApplicability::Applicable, QStringLiteral("Known PMR record field; its observation records the read outcome")});
			const auto modification = std::find_if(object.properties.cbegin(), object.properties.cend(),
				[](const RawProperty &property) { return property.locator.name == QLatin1String("ModificationWord"); });
			PropertyReadResult modified;
			if (modification == object.properties.cend())
			{
				modified.reason = PropertyReadReason::SourceIncomplete;
				modified.explanation = QStringLiteral("The PMR record did not reach its modification word");
			}
			else if (modification->state == PropertyReadState::Unreadable)
			{
				modified.state = PropertyReadState::Unreadable;
				modified.reason = PropertyReadReason::ValueUnreadable;
				modified.explanation = QStringLiteral("The PMR modification word could not be read");
			}
			else
			{
				modified.reason = PropertyReadReason::UnsupportedInterpretation;
				modified.explanation = QStringLiteral("PMR ModificationWord is retained without a proven timestamp interpretation");
			}
			file.evidence.recordReadStatus(MediaProperty::Modified, source.snapshot, owner, std::move(modified));
			for (const auto &raw : object.properties)
			{
				const auto property = withInferredText(raw, 2);
				const QString name = property.locator.name;
				MediaProperty field;
				QVariant value;
				if (name == QStringLiteral("Filename"))
				{
					field = MediaProperty::Filename;
					value = property.decoded;
					if (!value.toString().isEmpty())
						file.filenames.append(value.toString());
				}
				else if (name == QStringLiteral("Project"))
				{
					field = MediaProperty::Project;
					value = property.decoded;
				}
				else if (name == QStringLiteral("FileMobId"))
				{
					field = MediaProperty::FileMobId;
					value = file.fileMobId.isEmpty() ? QVariant{} : QVariant(file.fileMobId);
				}
				else if (name == QStringLiteral("MasterMobId"))
				{
					field = MediaProperty::MasterMobId;
					const QString identity = canonicalDatabaseId(property.encoding, bigEndian);
					if (!identity.isEmpty())
						file.masterMobIds.append(identity);
					value = identity.isEmpty() ? QVariant{} : QVariant(identity);
				}
				else
					continue;
				auto item = observation(source, object, property, value);
				if (property.state == PropertyReadState::Absent && versionsByObject.value(object.handle) == 1)
					item.readReason = PropertyReadReason::NotStoredByFormat;
				item.property = QStringLiteral("%1.%2").arg(setName, name);
				file.evidence.observe(field, std::move(item));
			}
			result.files.append(std::move(file));
		}
		return result;
	}
}
