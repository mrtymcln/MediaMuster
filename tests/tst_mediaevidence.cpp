#include "mediaevidence.h"
#include <QTest>

namespace
{
	int priority(MetadataSource source)
	{
		return source == MetadataSource::Mxf ? 2 : 1;
	}
	MetadataObservation observation(MetadataSource source, const QVariant &value)
	{
		MetadataObservation result;
		result.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{source, QStringLiteral("source"), {}, SourceReadState::Complete});
		result.value = value;
		result.rawValue = QStringLiteral("original encoding");
		result.readState = PropertyReadState::Present;
		return result;
	}
}

class TestMediaEvidence : public QObject
{
	Q_OBJECT
private slots:
	void retains_conflicting_values_and_prefers_validated_header()
	{
		MediaEvidence evidence;
		evidence.observe(MediaProperty::BitDepth, observation(MetadataSource::Mdb, QStringLiteral("10-bit")));
		evidence.observe(MediaProperty::BitDepth, observation(MetadataSource::Mxf, QStringLiteral("16-bit")));
		const auto selected = evidence.resolve(MediaProperty::BitDepth, priority, QStringLiteral("test rule"));
		QCOMPARE(selected.value.toString(), QStringLiteral("16-bit"));
		QCOMPARE(selected.agreement, PropertyAgreement::Conflicting);
		QCOMPARE(evidence.observations(MediaProperty::BitDepth).size(), 2);
		QCOMPARE(evidence.observations(MediaProperty::BitDepth).first().rawValue.toString(), QStringLiteral("original encoding"));
	}
	void tied_values_remain_unresolved_regardless_of_majority()
	{
		MediaEvidence evidence;
		for (int i = 0; i < 3; ++i)
			evidence.observe(MediaProperty::Project, observation(MetadataSource::Pmr, QStringLiteral("A")));
		evidence.observe(MediaProperty::Project, observation(MetadataSource::Pmr, QStringLiteral("B")));
		const auto selected = evidence.resolve(MediaProperty::Project, priority, QStringLiteral("test rule"));
		QVERIFY(!selected.value.isValid());
		QCOMPARE(selected.selectedObservation, -1);
		QCOMPARE(selected.agreement, PropertyAgreement::Conflicting);
	}
	void false_absent_unreadable_and_not_read_are_distinct()
	{
		MediaEvidence evidence;
		QCOMPARE(evidence.selected(MediaProperty::Alpha).readState, PropertyReadState::NotRead);
		auto absent = observation(MetadataSource::Pmr, {});
		absent.readState = PropertyReadState::Absent;
		evidence.observe(MediaProperty::Alpha, absent);
		QCOMPARE(evidence.resolve(MediaProperty::Alpha, priority, {}).readState, PropertyReadState::Absent);
		auto unreadable = observation(MetadataSource::Mxf, {});
		unreadable.readState = PropertyReadState::Unreadable;
		evidence.observe(MediaProperty::Alpha, unreadable);
		QCOMPARE(evidence.resolve(MediaProperty::Alpha, priority, {}).readState, PropertyReadState::Unreadable);
		evidence.observe(MediaProperty::Alpha, observation(MetadataSource::Mxf, false));
		const auto selected = evidence.resolve(MediaProperty::Alpha, priority, {});
		QCOMPARE(selected.readState, PropertyReadState::Present);
		QVERIFY(selected.value.isValid());
		QCOMPARE(selected.value.toBool(), false);
	}
	void excluded_evidence_is_retained_without_selecting_it()
	{
		MediaEvidence evidence;
		evidence.observe(MediaProperty::Codec, observation(MetadataSource::Mdb, QStringLiteral("old codec")));
		evidence.excludeDatabaseMetadata();
		QCOMPARE(evidence.observations(MediaProperty::Codec).size(), 1);
		QVERIFY(!evidence.resolve(MediaProperty::Codec, priority, {}).value.isValid());
	}
	void source_reads_do_not_imply_property_absence()
	{
		MediaEvidence evidence;
		const auto complete = observation(MetadataSource::Mxf, {}).snapshot;
		evidence.registerSource(complete);
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, complete).state, PropertyReadState::NotRead);
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, complete).reason, PropertyReadReason::CoverageNotEstablished);
		const auto missing = observation(MetadataSource::Mdb, {}).snapshot;
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, missing).reason, PropertyReadReason::NoAssociatedSource);
		const auto failed = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Omf, QStringLiteral("failed.omf"), {}, SourceReadState::Unreadable});
		evidence.registerSource(failed);
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, failed).state, PropertyReadState::Unreadable);
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, failed).reason, PropertyReadReason::SourceUnreadable);
	}
	void recorded_empty_false_and_zero_are_present_without_inventing_a_selected_value()
	{
		MediaEvidence evidence;
		const auto empty = observation(MetadataSource::Mxf, QString{});
		evidence.observe(MediaProperty::ClipName, empty);
		QCOMPARE(evidence.readStatus(MediaProperty::ClipName, empty.snapshot).state, PropertyReadState::Present);
		const auto selected = evidence.resolve(MediaProperty::ClipName, priority, {});
		QCOMPARE(selected.readState, PropertyReadState::Present);
		QVERIFY(!selected.value.isValid());
		evidence.observe(MediaProperty::Alpha, observation(MetadataSource::Mxf, false));
		evidence.observe(MediaProperty::Channels, observation(MetadataSource::Mxf, 0));
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha).state, PropertyReadState::Present);
		QCOMPARE(evidence.readStatus(MediaProperty::Channels).state, PropertyReadState::Present);
	}
	void absence_is_scoped_and_qualification_keeps_history()
	{
		MediaEvidence evidence;
		const auto source = observation(MetadataSource::Mxf, {}).snapshot;
		evidence.recordReadStatus(MediaProperty::Alpha, source, QStringLiteral("descriptor:1"),
			{PropertyReadState::Absent, PropertyReadReason::NotPresentInObject, PropertyApplicability::Applicable, {}});
		evidence.registerSource(source, QStringLiteral("descriptor:2"));
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, source, QStringLiteral("descriptor:1")).state, PropertyReadState::Absent);
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, source).state, PropertyReadState::NotRead);
		MediaEvidence copy = evidence;
		copy.qualifyAll(false, SourceFreshness::Changed);
		QCOMPARE(copy.sourceCoverage().first().freshness, SourceFreshness::Changed);
		QVERIFY(!copy.sourceCoverage().first().eligible);
		QVERIFY(evidence.sourceCoverage().first().eligible);
		QCOMPARE(copy.readStatus(MediaProperty::Alpha, source, QStringLiteral("descriptor:1")).state, PropertyReadState::Absent);
		QCOMPARE(copy.readStatus(MediaProperty::Alpha, source, {}, true).reason, PropertyReadReason::NoAssociatedSource);
	}
	void reading_a_scheduled_header_replaces_only_its_unopened_receipt()
	{
		MediaEvidence evidence;
		const auto unopened = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("header.mxf"), {}, SourceReadState::NotRead});
		const auto read = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, QStringLiteral("header.mxf"), {}, SourceReadState::Complete});
		evidence.registerSource(unopened);
		QCOMPARE(evidence.readStatus(MediaProperty::Codec, unopened).reason, PropertyReadReason::SourceNotRead);
		evidence.registerSource(read);
		QCOMPARE(evidence.sourceCoverage().size(), 1);
		QCOMPARE(evidence.sourceCoverage().first().snapshot, SourceSnapshotRef(read));
		evidence.registerSource(read);
		QCOMPARE(evidence.sourceCoverage().size(), 1);
		const auto anotherRead = QSharedPointer<SourceSnapshot>::create(*read);
		evidence.registerSource(anotherRead);
		QCOMPARE(evidence.sourceCoverage().size(), 2);
	}
	void repeated_read_states_are_order_independent()
	{
		const auto source = observation(MetadataSource::Mxf, {}).snapshot;
		for (const bool reversed : {false, true})
		{
			MediaEvidence evidence;
			auto absent = observation(MetadataSource::Mxf, {});
			absent.snapshot = source;
			absent.readState = PropertyReadState::Absent;
			auto unread = absent;
			unread.property = QStringLiteral("second occurrence");
			unread.readState = PropertyReadState::NotRead;
			evidence.observe(MediaProperty::Alpha, reversed ? unread : absent);
			evidence.observe(MediaProperty::Alpha, reversed ? absent : unread);
			QCOMPARE(evidence.readStatus(MediaProperty::Alpha, source).state, PropertyReadState::NotRead);
			evidence.recordReadStatus(MediaProperty::Alpha, source, {},
				{PropertyReadState::Unreadable, PropertyReadReason::ValueUnreadable, PropertyApplicability::Applicable, {}});
			QCOMPARE(evidence.readStatus(MediaProperty::Alpha, source).state, PropertyReadState::Unreadable);
		}
	}
	void attaching_more_context_does_not_erase_previously_checked_fields()
	{
		const auto source = observation(MetadataSource::Mxf, {}).snapshot;
		MediaEvidence evidence, additional;
		const QString owner = QStringLiteral("descriptor:1");
		evidence.recordReadStatus(MediaProperty::Alpha, source, owner,
			{PropertyReadState::Absent, PropertyReadReason::NotPresentInObject, PropertyApplicability::Applicable, {}});
		additional.recordReadStatus(MediaProperty::Channels, source, owner,
			{PropertyReadState::Unreadable, PropertyReadReason::ValueUnreadable, PropertyApplicability::Applicable, {}});
		evidence.appendCoverage(additional.sourceCoverage().first());
		QCOMPARE(evidence.sourceCoverage().size(), 1);
		QCOMPARE(evidence.readStatus(MediaProperty::Alpha, source, owner).state, PropertyReadState::Absent);
		QCOMPARE(evidence.readStatus(MediaProperty::Channels, source, owner).state, PropertyReadState::Unreadable);
	}
	void copy_on_write_keeps_original_evidence_unchanged()
	{
		MediaEvidence original;
		original.observe(MediaProperty::Codec, observation(MetadataSource::Mdb, QStringLiteral("codec")));
		MediaEvidence copy = original;
		copy.excludeDatabaseMetadata();
		QVERIFY(original.observations(MediaProperty::Codec).first().eligible);
		QVERIFY(!copy.observations(MediaProperty::Codec).first().eligible);
		QCOMPARE(original.observations(MediaProperty::Codec).first().snapshot,
			copy.observations(MediaProperty::Codec).first().snapshot);
	}
	void id_allocation_never_wraps_or_reuses_removed_ids()
	{
		KelpieIdAllocator ids;
		QCOMPARE(ids.allocate(), KelpieId(1));
		ids.reserveThrough(10);
		QCOMPARE(ids.allocate(), KelpieId(11));
		ids.reserveThrough(std::numeric_limits<KelpieId>::max() - 1);
		QCOMPARE(ids.allocate(), std::numeric_limits<KelpieId>::max());
		QVERIFY_THROWS_EXCEPTION(KelpieIdExhausted, ids.allocate());
		ids.reset();
		QCOMPARE(ids.allocate(), KelpieId(1));
	}
};
QTEST_APPLESS_MAIN(TestMediaEvidence)
#include "tst_mediaevidence.moc"
