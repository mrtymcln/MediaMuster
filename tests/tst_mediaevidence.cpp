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
