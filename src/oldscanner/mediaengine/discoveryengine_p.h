#pragma once

// Turns filesystem results into evidence, distinguishing unavailable values
// from properties that have not been checked.

#include "mediaengine/scanmodel.h"

namespace MediaEngine::Detail
{
	// An invalid query result establishes unreadability, not absence.
	inline void filesystemObservation(MediaFile &file, const SourceSnapshotRef &source,
									  MediaProperty property, const QString &locator, const QVariant &value)
	{
		MetadataObservation observation;
		observation.snapshot = source;
		observation.property = locator;
		observation.value = value;
		observation.readState = value.isValid() ? PropertyReadState::Present : PropertyReadState::Unreadable;
		if (!value.isValid())
			observation.explanation = QStringLiteral("The filesystem query did not provide a usable %1; absence is not established.").arg(locator);
		file.evidence.observe(property, std::move(observation));
	}
}
