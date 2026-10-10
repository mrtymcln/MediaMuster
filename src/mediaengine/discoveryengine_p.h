#pragma once

// Helper used only inside discovery. Turns the filesystem query results
// into evidence, preserving the difference between an unavailable value
// and a property that has not been checked.

#include "scanmodel.h"

namespace MediaEngine::Detail
{
	// Internal evidence construction shared with the discovery contract tests.
	// An unavailable attempted result is not evidence of absence or of no attempt.
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
