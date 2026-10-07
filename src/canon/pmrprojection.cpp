// Keeps PMR records and their legacy/Unicode set provenance separate.
// Linking their recorded filenames to physical files belongs to ScanEngine.

#include "projection.h"

namespace Canon
{
	Projection projectPmr(const ParsedSource &source, const Cancellation &cancellation)
	{
		Projection result;
		QHash<ObjectHandle, QString> setsByObject;
		for (const auto &set : source.recordSets)
			for (const auto handle : set.objects)
			{
				if (cancellation.cancelled())
					return result;
				setsByObject.insert(handle, set.name);
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
				item.property = QStringLiteral("%1.%2").arg(setName, name);
				file.evidence.observe(field, std::move(item));
			}
			if (source.outcome == ParsedSource::Outcome::Complete)
				for (int field = int(MediaProperty::ClipName); field <= int(MediaProperty::ComponentDepth); ++field)
					if (file.evidence.observations(MediaProperty(field)).isEmpty())
					{
						MetadataObservation absent;
						absent.snapshot = source.snapshot;
						absent.property = setName + QStringLiteral(".record layout");
						absent.objectIdentity = QStringLiteral("object:%1").arg(object.handle);
						absent.readState = PropertyReadState::Absent;
						absent.explanation = QStringLiteral("This PMR record layout does not store this metadata field");
						file.evidence.observe(MediaProperty(field), absent);
					}
			result.files.append(std::move(file));
		}
		return result;
	}
}
