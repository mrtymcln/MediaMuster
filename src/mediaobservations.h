#pragma once

#include "mediafile.h"
#include <utility>

// Records filesystem observations after a confirmed move or copy. The original
// MediaEngine source evidence stays attached to the row alongside its new location.
namespace MediaObservations
{
	inline void add(MediaFile &file, MediaProperty field, const SourceSnapshotRef &source,
					const QString &property, const QVariant &value, EvidenceBasis basis = EvidenceBasis::Recorded,
					const QVariant &raw = {}, const QString &owner = {})
	{
		MetadataObservation observation;
		observation.snapshot = source;
		observation.property = property;
		observation.objectIdentity = owner;
		observation.value = value;
		observation.rawValue = raw;
		observation.basis = basis;
		observation.eligible = source->readState != SourceReadState::Unreadable;
		const bool known = value.isValid() && !(value.metaType().id() == QMetaType::QString && value.toString().isEmpty());
		observation.readState = known ? PropertyReadState::Present : PropertyReadState::NotRead;
		if (!known && source->readState == SourceReadState::Unreadable)
			observation.readState = PropertyReadState::Unreadable;
		if (basis == EvidenceBasis::Derived)
			observation.explanation = QStringLiteral("Derived by the current format reader from the named inputs; raw inputs retained when available");
		file.evidence.observe(field, std::move(observation));
	}
}
