#include "oldscanner/mediaengine/projection.h"
#include "oldscanner/mediaengine/metadataselectionpolicy.h"
namespace MediaEngine {
	void selectMetadata(MediaEvidence &evidence)
	{
		for (const auto &policy : propertyPolicies())
		{
			// Effects depend on the final selected name and type, so derive them last.
			if (policy.rule == SelectionRule::DerivedEffect)
				continue;
			auto field = resolveProperty(evidence, policy);
			if (policy.rule == SelectionRule::FileDuration && field.value.isValid())
			{
				// A display clock does not change the recorded length. Only clocks
				// attached to equivalent durations can supplement the selected one.
				const auto selected = mediaDuration(field.value);
				MediaEvidence clocks;
				for (auto item : evidence.observations(policy.property))
				{
					const auto candidate = mediaDuration(item.value);
					if (!item.eligible || candidate.units != selected.units ||
						!candidate.rate.sameRate(selected.rate) || !candidate.displayRate.valid())
						continue;
					item.value = rateValue(candidate.displayRate);
					clocks.observe(MediaProperty::FrameRate, std::move(item));
				}
				auto clockPolicy = policy;
				clockPolicy.property = MediaProperty::FrameRate;
				const auto clock = resolveProperty(clocks, clockPolicy);
				auto value = field.value.toMap();
				value.insert(QStringLiteral("DisplayRate"), clock.value);
				field.value = value;
				field.reason = EvidenceExplanation::withDisplayClock(field.reason, clock.reason);
			}
			evidence.select(policy.property, std::move(field));
		}
		selectEffectMetadata(evidence);
	}

	MediaEvidence selectedProjectedEvidence(const ProjectedFile &file, const Projection &projection)
	{
		MediaEvidence result = file.evidence;
		const QString masterMobId = resolveProperty(result, propertyPolicy(MediaProperty::MasterMobId)).value.toString();
		if (!masterMobId.isEmpty())
			for (const auto &master : projection.masters)
				if (masterGroupMatchesAssociation(result, master, masterMobId))
					appendMasterMetadata(result, master.evidence);
		selectMetadata(result);
		return result;
	}

}
