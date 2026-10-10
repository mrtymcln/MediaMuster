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
	void compact_reasons_preserve_diagnostic_wording_and_parameters()
	{
		using Reason = EvidenceExplanation::Reason;
		QVERIFY(sizeof(EvidenceExplanation) < sizeof(QString));
		const EvidenceExplanation preferred(Reason::PreferredSource);
		QCOMPARE(preferred.text(), QStringLiteral("Selected by the field-specific source priority; alternatives retained"));
		QCOMPARE(preferred.reason(), Reason::PreferredSource);
		QCOMPARE(EvidenceExplanation(Reason::EqualRankConflict).text(),
			QStringLiteral("Equally eligible sources disagree; no defensible winner"));
		const auto checked = EvidenceExplanation::checkedInputs(Reason::CheckedInputsAbsent,
			QStringLiteral("OMFI:A, OMFI:B"));
		QCOMPARE(checked.text(), QStringLiteral("The complete owning object has none of the checked input properties. Checked properties: OMFI:A, OMFI:B"));
		const auto copied = checked;
		QCOMPARE(copied.text(), checked.text());
		const auto clock = EvidenceExplanation::withDisplayClock(preferred,
			EvidenceExplanation(Reason::EqualRankConflict));
		QCOMPARE(clock.text(), preferred.text() +
			QStringLiteral("; display clock resolved independently from matching duration observations: Equally eligible sources disagree; no defensible winner"));
		const auto emptyClock = EvidenceExplanation::withDisplayClock(preferred, {});
		QCOMPARE(emptyClock.text(), preferred.text() +
			QStringLiteral("; display clock resolved independently from matching duration observations: "));
		const EvidenceExplanation unusual(QStringLiteral("Reader-specific text: \u4f60\u597d; bytes retained."));
		QCOMPARE(unusual.reason(), Reason::Custom);
		QCOMPARE(unusual.text(), QStringLiteral("Reader-specific text: \u4f60\u597d; bytes retained."));
		const auto customClock = EvidenceExplanation::withDisplayClock(unusual, preferred);
		QCOMPARE(customClock.text(), unusual.text() +
			QStringLiteral("; display clock resolved independently from matching duration observations: ") + preferred.text());
		const auto nestedClock = EvidenceExplanation::withDisplayClock(preferred, clock);
		QCOMPARE(nestedClock.text(), preferred.text() +
			QStringLiteral("; display clock resolved independently from matching duration observations: ") + clock.text());
		QVERIFY(EvidenceExplanation{}.isEmpty());
		QVERIFY(EvidenceExplanation(QString{}).isEmpty());
		QVERIFY(EvidenceExplanation{}.text().isNull());
		QVERIFY(EvidenceExplanation(QString{}).text().isNull());
		const EvidenceExplanation recordedEmpty(QStringLiteral(""));
		QVERIFY(recordedEmpty.isEmpty());
		QVERIFY(!recordedEmpty.text().isNull());
		QCOMPARE(recordedEmpty.reason(), Reason::Custom);
	}
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
		evidence.observe(MediaProperty::Compression, observation(MetadataSource::Mdb, QStringLiteral("old codec")));
		evidence.excludeDatabaseMetadata();
		QCOMPARE(evidence.observations(MediaProperty::Compression).size(), 1);
		QVERIFY(!evidence.resolve(MediaProperty::Compression, priority, {}).value.isValid());
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
		QCOMPARE(evidence.readStatus(MediaProperty::Compression, unopened).reason, PropertyReadReason::SourceNotRead);
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
		original.observe(MediaProperty::Compression, observation(MetadataSource::Mdb, QStringLiteral("codec")));
		MediaEvidence copy = original;
		copy.excludeDatabaseMetadata();
		QVERIFY(original.observations(MediaProperty::Compression).first().eligible);
		QVERIFY(!copy.observations(MediaProperty::Compression).first().eligible);
		QCOMPARE(original.observations(MediaProperty::Compression).first().snapshot,
			copy.observations(MediaProperty::Compression).first().snapshot);
	}
	void object_context_text_is_shared_within_its_source()
	{
		MediaEvidence evidence;
		const auto source = observation(MetadataSource::Mxf, {}).snapshot;
		const QString firstOwner = QString::fromLatin1("object:42");
		const QString secondOwner = QString::fromLatin1("object:42");
		QVERIFY(firstOwner.constData() != secondOwner.constData());
		auto first = observation(MetadataSource::Mxf, QStringLiteral("clip"));
		first.snapshot = source;
		first.objectIdentity = firstOwner;
		evidence.observe(MediaProperty::ClipName, first);
		auto second = first;
		second.value = QStringLiteral("project");
		second.objectIdentity = secondOwner;
		evidence.observe(MediaProperty::Project, second);
		const auto *sharedText = evidence.observations(MediaProperty::ClipName).first().objectIdentity.constData();
		QCOMPARE(evidence.observations(MediaProperty::Project).first().objectIdentity.constData(), sharedText);
		QCOMPARE(evidence.sourceCoverage().first().objectIdentity.constData(), sharedText);

		// Growing the coverage list must not leave observations borrowing its entries.
		for (int handle = 100; handle < 228; ++handle)
			evidence.registerSource(source, QStringLiteral("object:%1").arg(handle));
		QCOMPARE(evidence.observations(MediaProperty::ClipName).first().objectIdentity, firstOwner);
		QCOMPARE(evidence.observations(MediaProperty::ClipName).first().objectIdentity.constData(), sharedText);
		MediaEvidence copy = evidence;
		second.objectIdentity = QString::fromLatin1("object:42");
		copy.observe(MediaProperty::OriginalBin, second);
		QCOMPARE(copy.observations(MediaProperty::OriginalBin).first().objectIdentity.constData(), sharedText);
		QVERIFY(evidence.observations(MediaProperty::OriginalBin).isEmpty());
	}
	void object_context_sharing_preserves_distinct_receipts_and_spellings()
	{
		MediaEvidence evidence;
		const auto firstSource = observation(MetadataSource::Mxf, {}).snapshot;
		const auto secondSource = QSharedPointer<SourceSnapshot>::create(*firstSource);
		auto first = observation(MetadataSource::Mxf, QStringLiteral("first read"));
		first.snapshot = firstSource;
		first.objectIdentity = QString::fromLatin1("object:42");
		evidence.observe(MediaProperty::ClipName, first);
		auto second = first;
		second.snapshot = secondSource;
		second.value = QStringLiteral("second read");
		second.objectIdentity = QString::fromLatin1("object:42");
		evidence.observe(MediaProperty::ClipName, second);
		auto distinctSpelling = first;
		distinctSpelling.objectIdentity = QString::fromLatin1("42");
		evidence.observe(MediaProperty::ClipName, distinctSpelling);
		const auto &values = evidence.observations(MediaProperty::ClipName);
		QCOMPARE(values.size(), 3);
		QCOMPARE(evidence.sourceCoverage().size(), 3);
		QCOMPARE(values[0].snapshot, firstSource);
		QCOMPARE(values[1].snapshot, SourceSnapshotRef(secondSource));
		QVERIFY(values[0].objectIdentity.constData() != values[1].objectIdentity.constData());
		QCOMPARE(values[0].objectIdentity, QStringLiteral("object:42"));
		QCOMPARE(values[2].objectIdentity, QStringLiteral("42"));
		evidence.qualifySource(firstSource, false, SourceFreshness::Changed);
		QVERIFY(!evidence.observations(MediaProperty::ClipName)[0].eligible);
		QVERIFY(evidence.observations(MediaProperty::ClipName)[1].eligible);
		QVERIFY(!evidence.observations(MediaProperty::ClipName)[2].eligible);
	}
	void object_context_sharing_preserves_null_and_explicitly_empty_text()
	{
		const QString nullOwner;
		const QString emptyOwner = QStringLiteral("");
		QVERIFY(nullOwner.isNull());
		QVERIFY(!emptyOwner.isNull());
		for (const bool nullFirst : {true, false})
		{
			MediaEvidence evidence;
			auto first = observation(MetadataSource::Mxf, QStringLiteral("clip"));
			first.objectIdentity = nullFirst ? nullOwner : emptyOwner;
			evidence.observe(MediaProperty::ClipName, first);
			auto second = first;
			second.value = QStringLiteral("project");
			second.objectIdentity = nullFirst ? emptyOwner : nullOwner;
			evidence.observe(MediaProperty::Project, second);
			// Context grouping still uses QString equality; each observation keeps
			// the nullness supplied by its own reader.
			QCOMPARE(evidence.sourceCoverage().size(), 1);
			QCOMPARE(evidence.sourceCoverage().first().objectIdentity.isNull(), nullFirst);
			QCOMPARE(evidence.observations(MediaProperty::ClipName).first().objectIdentity.isNull(), nullFirst);
			QCOMPARE(evidence.observations(MediaProperty::Project).first().objectIdentity.isNull(), !nullFirst);
		}
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
