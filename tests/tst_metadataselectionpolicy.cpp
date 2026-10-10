// Shared preferences select from qualified evidence without changing the source
// observations. Authored values exercise policy choices, not format correctness.
#include "mediaengine/metadataselectionpolicy.h"

#include <QTest>
#include <stdexcept>

namespace
{
	MetadataObservation observation(MetadataSource source, const QVariant &value)
	{
		MetadataObservation result;
		result.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			source, QStringLiteral("authored source"), {}, SourceReadState::Complete});
		result.property = QStringLiteral("Recorded fixture property");
		result.objectIdentity = QStringLiteral("object:7");
		result.value = value;
		result.rawValue = QByteArray("retained original bytes");
		result.readState = PropertyReadState::Present;
		result.basis = EvidenceBasis::Recorded;
		result.freshness = SourceFreshness::Unknown;
		result.explanation = QStringLiteral("Authored observation for selection testing.");
		return result;
	}
}

class TestMetadataSelectionPolicy : public QObject
{
	Q_OBJECT
private slots:
	void every_property_has_one_checked_policy()
	{
		const auto &policies = MediaEngine::propertyPolicies();
		QCOMPARE(policies.size(), std::size_t(MediaProperty::Count));
		std::array<bool, std::size_t(MediaProperty::Count)> seen{};
		for (const auto &policy : policies)
		{
			const auto index = std::size_t(policy.property);
			QVERIFY(index < seen.size());
			QVERIFY(!seen[index]);
			seen[index] = true;
			QCOMPARE(&MediaEngine::propertyPolicy(policy.property), &policy);
			for (const auto source : {MetadataSource::Filesystem, MetadataSource::Pmr,
				MetadataSource::Mdb, MetadataSource::Mxf, MetadataSource::Omf, MetadataSource::Avb})
			{
				const auto rank = policy.sources.priority(source);
				QVERIFY(rank >= 0 && rank <= 3);
			}
		}
		for (const auto present : seen)
			QVERIFY(present);
		QVERIFY_THROWS_EXCEPTION(std::out_of_range, MediaEngine::propertyPolicy(MediaProperty(-1)));
		QVERIFY_THROWS_EXCEPTION(std::out_of_range, MediaEngine::propertyPolicy(MediaProperty::Count));
		QVERIFY_THROWS_EXCEPTION(std::out_of_range,
			MediaEngine::propertyPolicy(MediaProperty(int(MediaProperty::Count) + 1)));
	}

	void approved_editorial_preferences()
	{
		for (const auto property : {MediaProperty::ClipName, MediaProperty::Project,
			MediaProperty::OriginalBin})
		{
			MediaEvidence evidence;
			evidence.observe(property, observation(MetadataSource::Mdb, QStringLiteral("database")));
			evidence.observe(property, observation(MetadataSource::Mxf, QStringLiteral("header")));
			evidence.observe(property, observation(MetadataSource::Pmr, QStringLiteral("index")));
			evidence.observe(property, observation(MetadataSource::Avb, QStringLiteral("bin")));
			const auto &policy = MediaEngine::propertyPolicy(property);
			const auto selected = MediaEngine::resolveProperty(evidence, policy);
			const auto expected = property == MediaProperty::Project ? QStringLiteral("index")
				: property == MediaProperty::OriginalBin ? QStringLiteral("database")
				: QStringLiteral("header");
			QCOMPARE(selected.value.toString(), expected);
			QCOMPARE(selected.rule, mediaPropertyName(property));
			QCOMPARE(evidence.observations(property).size(), 4);
		}
	}

	void zero_priority_excludes_selection_without_erasing_presence()
	{
		MediaEvidence evidence;
		const auto recorded = observation(MetadataSource::Filesystem, QStringLiteral("unqualified codec"));
		evidence.observe(MediaProperty::Compression, recorded);
		const auto selected = MediaEngine::resolveProperty(evidence,
			MediaEngine::propertyPolicy(MediaProperty::Compression));
		QVERIFY(!selected.value.isValid());
		QCOMPARE(selected.selectedObservation, -1);
		QCOMPARE(selected.agreement, PropertyAgreement::SingleSource);
		QCOMPARE(selected.readState, PropertyReadState::Present);
		QCOMPARE(evidence.readStatus(MediaProperty::Compression, recorded.snapshot).state,
			PropertyReadState::Present);
		QCOMPARE(evidence.observations(MediaProperty::Compression).first().rawValue, recorded.rawValue);
		QVERIFY(evidence.observations(MediaProperty::Compression).first().eligible);
		evidence.observe(MediaProperty::Compression,
			observation(MetadataSource::Mdb, QStringLiteral("PCM")));
		const auto fallback = MediaEngine::resolveProperty(evidence,
			MediaEngine::propertyPolicy(MediaProperty::Compression));
		QCOMPARE(fallback.value.toString(), QStringLiteral("PCM"));
		QCOMPARE(fallback.agreement, PropertyAgreement::Conflicting);
		QCOMPARE(evidence.observations(MediaProperty::Compression).size(), 2);

		// A PMR filename describes its record, not a replacement physical filename.
		MediaEvidence filename;
		filename.observe(MediaProperty::Filename,
			observation(MetadataSource::Pmr, QStringLiteral("database-name.mxf")));
		QVERIFY(!MediaEngine::resolveProperty(filename,
			MediaEngine::propertyPolicy(MediaProperty::Filename)).value.isValid());
		filename.observe(MediaProperty::Filename,
			observation(MetadataSource::Filesystem, QStringLiteral("physical-name.mxf")));
		QCOMPARE(MediaEngine::resolveProperty(filename, MediaEngine::propertyPolicy(MediaProperty::Filename))
			.value.toString(), QStringLiteral("physical-name.mxf"));
	}

	void positive_ranks_fall_back_as_sources_are_retracted()
	{
		MediaEvidence evidence;
		evidence.observe(MediaProperty::ClipName,
			observation(MetadataSource::Mxf, QStringLiteral("header")));
		evidence.observe(MediaProperty::ClipName,
			observation(MetadataSource::Mdb, QStringLiteral("database")));
		evidence.observe(MediaProperty::ClipName,
			observation(MetadataSource::Avb, QStringLiteral("bin")));
		const auto &policy = MediaEngine::propertyPolicy(MediaProperty::ClipName);
		QCOMPARE(MediaEngine::resolveProperty(evidence, policy).value.toString(), QStringLiteral("header"));
		evidence.excludeSource(MediaProperty::ClipName, MetadataSource::Mxf);
		QCOMPARE(MediaEngine::resolveProperty(evidence, policy).value.toString(), QStringLiteral("database"));
		evidence.excludeSource(MediaProperty::ClipName, MetadataSource::Mdb);
		QCOMPARE(MediaEngine::resolveProperty(evidence, policy).value.toString(), QStringLiteral("bin"));
		evidence.excludeSource(MediaProperty::ClipName, MetadataSource::Avb);
		QVERIFY(!MediaEngine::resolveProperty(evidence, policy).value.isValid());
		QCOMPARE(evidence.observations(MediaProperty::ClipName).size(), 3);
	}

	void unreadable_or_empty_preferred_text_uses_fallback()
	{
		for (const auto state : {PropertyReadState::NotRead, PropertyReadState::Absent,
			PropertyReadState::Unreadable, PropertyReadState::Present})
		{
			MediaEvidence evidence;
			auto unavailable = observation(MetadataSource::Mxf, QString{});
			unavailable.readState = state;
			evidence.observe(MediaProperty::ClipName, unavailable);
			evidence.observe(MediaProperty::ClipName,
				observation(MetadataSource::Mdb, QStringLiteral("database")));
			evidence.observe(MediaProperty::ClipName,
				observation(MetadataSource::Avb, QStringLiteral("bin")));
			const auto &policy = MediaEngine::propertyPolicy(MediaProperty::ClipName);
			QCOMPARE(MediaEngine::resolveProperty(evidence, policy).value.toString(),
				QStringLiteral("database"));
			evidence.excludeSource(MediaProperty::ClipName, MetadataSource::Mdb);
			QCOMPARE(MediaEngine::resolveProperty(evidence, policy).value.toString(), QStringLiteral("bin"));
			QCOMPARE(evidence.observations(MediaProperty::ClipName).first().readState, state);
			QCOMPARE(evidence.observations(MediaProperty::ClipName).first().rawValue,
				unavailable.rawValue);
		}
	}

	void false_and_zero_values_are_selectable()
	{
		MediaEvidence alpha;
		alpha.observe(MediaProperty::Alpha, observation(MetadataSource::Mdb, true));
		alpha.observe(MediaProperty::Alpha, observation(MetadataSource::Mxf, false));
		const auto selectedAlpha = MediaEngine::resolveProperty(alpha,
			MediaEngine::propertyPolicy(MediaProperty::Alpha));
		QVERIFY(selectedAlpha.value.isValid());
		QVERIFY(!selectedAlpha.value.toBool());
		QCOMPARE(selectedAlpha.readState, PropertyReadState::Present);
		QCOMPARE(selectedAlpha.agreement, PropertyAgreement::Conflicting);

		MediaEvidence size;
		size.observe(MediaProperty::Size, observation(MetadataSource::Filesystem, qint64(0)));
		const auto selectedSize = MediaEngine::resolveProperty(size,
			MediaEngine::propertyPolicy(MediaProperty::Size));
		QVERIFY(selectedSize.value.isValid());
		QCOMPARE(selectedSize.value.toLongLong(), qint64(0));
		QCOMPARE(selectedSize.readState, PropertyReadState::Present);
	}

	void equal_header_tiers_keep_conflicts_unresolved()
	{
		MediaEvidence evidence;
		evidence.observe(MediaProperty::Compression, observation(MetadataSource::Mxf, QStringLiteral("A")));
		evidence.observe(MediaProperty::Compression, observation(MetadataSource::Omf, QStringLiteral("B")));
		evidence.observe(MediaProperty::Compression, observation(MetadataSource::Mdb, QStringLiteral("fallback")));
		const auto selected = MediaEngine::resolveProperty(evidence,
			MediaEngine::propertyPolicy(MediaProperty::Compression));
		QVERIFY(!selected.value.isValid());
		QCOMPARE(selected.selectedObservation, -1);
		QCOMPARE(selected.agreement, PropertyAgreement::Conflicting);
		QCOMPARE(selected.readState, PropertyReadState::Present);
		QCOMPARE(evidence.observations(MediaProperty::Compression).size(), 3);
	}

	void a_preference_change_retains_original_evidence()
	{
		MediaEvidence evidence;
		evidence.observe(MediaProperty::BitDepth, observation(MetadataSource::Mdb, QStringLiteral("10-bit")));
		evidence.observe(MediaProperty::BitDepth, observation(MetadataSource::Mxf, QStringLiteral("16-bit")));
		const auto before = evidence.observations(MediaProperty::BitDepth);
		const auto &standard = MediaEngine::propertyPolicy(MediaProperty::BitDepth);
		evidence.select(MediaProperty::BitDepth, MediaEngine::resolveProperty(evidence, standard));
		QCOMPARE(evidence.selected(MediaProperty::BitDepth).value.toString(), QStringLiteral("16-bit"));

		auto changed = standard;
		changed.sources.mdb = 3;
		changed.sources.mxf = changed.sources.omf = 2;
		evidence.select(MediaProperty::BitDepth, MediaEngine::resolveProperty(evidence, changed));
		const auto selected = evidence.selected(MediaProperty::BitDepth);
		QCOMPARE(selected.value.toString(), QStringLiteral("10-bit"));
		QCOMPARE(selected.rule, mediaPropertyName(MediaProperty::BitDepth));
		QCOMPARE(selected.agreement, PropertyAgreement::Conflicting);
		QCOMPARE(standard.sources.mxf, 3);
		QCOMPARE(standard.sources.mdb, 2);

		const auto &after = evidence.observations(MediaProperty::BitDepth);
		QCOMPARE(after.size(), before.size());
		for (qsizetype index = 0; index < after.size(); ++index)
		{
			QCOMPARE(after[index].snapshot, before[index].snapshot);
			QCOMPARE(after[index].property, before[index].property);
			QCOMPARE(after[index].objectIdentity, before[index].objectIdentity);
			QCOMPARE(after[index].value, before[index].value);
			QCOMPARE(after[index].rawValue, before[index].rawValue);
			QCOMPARE(after[index].readState, before[index].readState);
			QCOMPARE(after[index].readReason, before[index].readReason);
			QCOMPARE(after[index].basis, before[index].basis);
			QCOMPARE(after[index].freshness, before[index].freshness);
			QCOMPARE(after[index].eligible, before[index].eligible);
			QCOMPARE(after[index].explanation, before[index].explanation);
		}
	}

	void a_higher_priority_cannot_admit_wrong_owner_evidence()
	{
		MediaEvidence evidence;
		auto wrongOwner = observation(MetadataSource::Mxf, QStringLiteral("other file"));
		wrongOwner.eligible = false;
		evidence.observe(MediaProperty::ClipName, wrongOwner);
		evidence.observe(MediaProperty::ClipName,
			observation(MetadataSource::Avb, QStringLiteral("associated master")));
		QCOMPARE(MediaEngine::resolveProperty(evidence, MediaEngine::propertyPolicy(MediaProperty::ClipName))
			.value.toString(), QStringLiteral("associated master"));
		QCOMPARE(evidence.observations(MediaProperty::ClipName).size(), 2);
		QVERIFY(!evidence.observations(MediaProperty::ClipName).first().eligible);
	}

	void named_rules_preserve_special_selection_boundaries()
	{
		QCOMPARE(MediaEngine::propertyPolicy(MediaProperty::MasterMobId).rule,
			MediaEngine::SelectionRule::MasterAssociations);
		QCOMPARE(MediaEngine::propertyPolicy(MediaProperty::FileDuration).rule,
			MediaEngine::SelectionRule::FileDuration);
		for (const auto field : {MediaProperty::Effect, MediaProperty::EffectCategory,
			MediaProperty::EffectSequence})
		{
			const auto &policy = MediaEngine::propertyPolicy(field);
			QCOMPARE(policy.rule, MediaEngine::SelectionRule::DerivedEffect);
			// Derivation retains the selected name's actual source, including AVB fallback.
			MediaEvidence evidence;
			auto effect = observation(MetadataSource::Avb, QStringLiteral("derived effect"));
			effect.basis = EvidenceBasis::Derived;
			evidence.observe(field, effect);
			const auto selected = MediaEngine::resolveProperty(evidence, policy);
			QCOMPARE(selected.value.toString(), QStringLiteral("derived effect"));
			QCOMPARE(evidence.observations(field)[selected.selectedObservation].basis,
				EvidenceBasis::Derived);
		}
	}
};

QTEST_APPLESS_MAIN(TestMetadataSelectionPolicy)
#include "tst_metadataselectionpolicy.moc"
