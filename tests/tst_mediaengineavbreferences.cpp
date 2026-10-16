// Exercises selection rules using small, visible graphs. Binary decoding is
// tested separately; one complete authored bin checks their connection here.

#include "mediaengine/avbreader.h"
#include "mediaengine/avbreferences.h"
#include "testavb.h"

#include <QBuffer>
#include <QSet>
#include <QTest>

namespace
{
	using namespace MediaEngine;
	using Source = QSharedPointer<ParsedSource>;
	using Issue = AvbResolutionIssue::Kind;

	QByteArray identity(char suffix)
	{
		auto value = TestAvb::Source;
		value[value.size() - 1] = suffix;
		return value;
	}
	Source source()
	{
		auto result = Source::create();
		result->container = ParsedSource::Container::Avb;
		result->outcome = ParsedSource::Outcome::Complete;
		return result;
	}
	AvidObject &add(Source &source, ObjectHandle handle, const QByteArray &classId)
	{
		auto &object = source->objects.emplaceBack();
		object.handle = handle;
		auto context = QSharedPointer<AvbObjectContext>::create();
		context->classId = classId;
		context->interpretationComplete = true;
		object.avb = context;
		return object;
	}
	AvidObject &object(Source &source, ObjectHandle handle)
	{
		for (auto &candidate : source->objects)
			if (candidate.handle == handle)
				return candidate;
		qFatal("Authored AVB graph has a missing object");
	}
	void field(Source &source, ObjectHandle owner, const QString &name, const QVariant &value)
	{
		RawProperty property;
		property.locator.name = name;
		property.locator.objectNumber = owner;
		property.decoded = value;
		property.state = PropertyReadState::Present;
		if (owner)
			object(source, owner).properties.append(property);
		else
			source->unownedProperties.append(property);
	}
	void edge(Source &source, ObjectHandle owner, ObjectHandle target, const QString &name)
	{
		field(source, owner, name, QVariant::fromValue(quint32(target)));
		auto &relation = source->relationships.emplaceBack();
		relation.origin = owner;
		relation.target = target;
		relation.recordedReference = quint32(target);
		relation.referenceEncoding = QStringLiteral("AVB.ObjectIndex");
		relation.locator.name = name;
		relation.locator.objectNumber = owner;
	}
	void root(Source &source)
	{
		add(source, 1, "ABIN");
		edge(source, 0, 1, QStringLiteral("Header.root_index"));
		field(source, 1, QStringLiteral("Bin.version"), quint32(14));
		field(source, 1, QStringLiteral("Bin.item_count"), quint32(0));
	}
	void member(Source &source, ObjectHandle handle, int index, bool userPlaced)
	{
		for (auto &property : object(source, 1).properties)
			if (property.locator.name == QStringLiteral("Bin.item_count"))
				property.decoded = qMax(property.decoded.toUInt(), quint32(index + 1));
		const auto prefix = QStringLiteral("Bin.items[%1]").arg(index);
		edge(source, 1, handle, prefix + QStringLiteral(".mob"));
		field(source, 1, prefix + QStringLiteral(".user_placed"), userPlaced);
	}
	void composition(Source &source, ObjectHandle handle, const QByteArray &id,
					 int type = 3, const QString &name = QStringLiteral("Source"), int usage = 0)
	{
		auto &value = add(source, handle, "CMPO");
		value.recordedIdentity = id;
		value.identityEncoding = QStringLiteral("AVB.MobID.MaterialLE");
		value.role = type == 1	 ? AvidObject::Role::Composition
					 : type == 2 ? AvidObject::Role::Master
								 : AvidObject::Role::FileSource;
		field(source, handle, QStringLiteral("Composition.mob_id"), id);
		field(source, handle, QStringLiteral("Composition.mob_type"), type);
		field(source, handle, QStringLiteral("Composition.usage_code"), usage);
		field(source, handle, QStringLiteral("Component.name"), name);
	}
	void clip(Source &source, ObjectHandle handle, const QByteArray &id)
	{
		add(source, handle, "SCLP");
		field(source, handle, QStringLiteral("SourceClip.mob_id"), id);
		auto &relation = source->relationships.emplaceBack();
		relation.origin = handle;
		relation.recordedReference = id;
		relation.referenceEncoding = QStringLiteral("AVB.MobId.MaterialLE");
		relation.locator.name = QStringLiteral("SourceClip.mob_id");
		relation.locator.objectNumber = handle;
	}
	void locator(Source &source, ObjectHandle handle, const QByteArray &id)
	{
		add(source, handle, "MSML");
		field(source, handle, QStringLiteral("MSMLocator.mob_id"), id);
	}
	AvbSelection selected(QVector<ObjectHandle> handles)
	{
		return {AvbSelection::Kind::SelectedSequences, std::move(handles)};
	}
	QSet<QByteArray> mediaIds(const AvbReferenceResult &result)
	{
		QSet<QByteArray> ids;
		for (const auto &media : result.mediaReferences)
			ids.insert(media.fileMobId);
		return ids;
	}
	bool hasIssue(const AvbReferenceResult &result, Issue kind)
	{
		for (const auto &issue : result.issues)
			if (issue.kind == kind)
				return true;
		return false;
	}
	bool hasEdge(const AvbReferenceResult &result, ObjectHandle origin, ObjectHandle target)
	{
		for (const auto &edge : result.edges)
			if (edge.origin == origin && edge.target == target && edge.relationshipIndex >= 0)
				return true;
		return false;
	}
	Source twoSequences()
	{
		auto result = source();
		root(result);
		composition(result, 2, identity('S'), 1, QStringLiteral("Same name"));
		composition(result, 3, identity('T'), 1, QStringLiteral("Same name"));
		member(result, 2, 0, true);
		member(result, 3, 1, false);
		clip(result, 4, identity('A'));
		clip(result, 5, identity('B'));
		composition(result, 6, identity('A'));
		composition(result, 7, identity('B'));
		locator(result, 8, identity('A'));
		locator(result, 9, identity('B'));
		edge(result, 2, 4, QStringLiteral("TrackGroup.tracks[0].component"));
		edge(result, 3, 5, QStringLiteral("TrackGroup.tracks[0].component"));
		edge(result, 6, 8, QStringLiteral("Composition.descriptor"));
		edge(result, 7, 9, QStringLiteral("Composition.descriptor"));
		return result;
	}
}

class TestMediaEngineAvbReferences final : public QObject
{
	Q_OBJECT
private slots:
	void selectionAndUnion();
	void sequenceIdentityIsNotItsNameOrPlacement();
	void onlyReferencedGroupIncludesAllAngles();
	void precomputedAndDisabledTracksRemainReachable();
	void cyclesKeepEdgesAndTerminate();
	void unresolvedAndNullReferences();
	void eachBinResolvesIndependently();
	void wholeBinFollowsOnlyListedItems();
	void wholeBinIncompleteMembership_data();
	void wholeBinIncompleteMembership();
	void emptyBinRequiresEstablishedMembership();
	void duplicateIdentityIsAmbiguous();
	void exactTerminalIdentities_data();
	void exactTerminalIdentities();
	void legacyOnlyClipIsExplicitlyUnsupported();
	void audioSuiteDependencies();
	void terminalLocatorIsNotMedia();
	void incomplete_locator_cannot_assert_modern_identity_absent();
	void fileDescriptorRequiresUsableMediaRoute_data();
	void fileDescriptorRequiresUsableMediaRoute();
	void invalidSelectionAndIncompleteEvidence();
	void cancellation();
	void authoredBinaryGraph();
};

void TestMediaEngineAvbReferences::selectionAndUnion()
{
	Cancellation cancel;
	AvbReferenceIndex index(twoSequences(), cancel);
	const auto first = index.resolveReferences(selected({2}), cancel);
	QVERIFY(first.coverageComplete);
	QCOMPARE(mediaIds(first), QSet<QByteArray>{identity('A')});
	const auto both = index.resolveReferences(selected({2, 3}), cancel);
	QVERIFY(both.coverageComplete);
	QCOMPARE(mediaIds(both), (QSet<QByteArray>{identity('A'), identity('B')}));
	QCOMPARE(both.mediaReferences.size(), 2);
	const auto repeated = index.resolveReferences(selected({2, 2, 2}), cancel);
	QVERIFY(repeated.coverageComplete);
	QCOMPARE(repeated.mediaReferences.size(), 1);
	const auto whole = index.resolveReferences({}, cancel);
	QVERIFY(whole.coverageComplete);
	QCOMPARE(mediaIds(whole), mediaIds(both));
	const auto empty = index.resolveReferences(selected({}), cancel);
	QVERIFY(empty.coverageComplete);
	QVERIFY(empty.roots.isEmpty() && empty.mediaReferences.isEmpty() && empty.edges.isEmpty());
}

void TestMediaEngineAvbReferences::sequenceIdentityIsNotItsNameOrPlacement()
{
	Cancellation cancel;
	AvbReferenceIndex index(twoSequences(), cancel);
	QCOMPARE(index.sequences().size(), 2);
	const auto &a = index.sequences()[0];
	const auto &b = index.sequences()[1];
	QCOMPARE(a.name, b.name);
	QVERIFY(!(a.objectHandle == b.objectHandle));
	QVERIFY(a.compositionMobId != b.compositionMobId);
	QVERIFY(a.userPlaced != b.userPlaced);
	QVERIFY(!a.membershipEvidence.name.isEmpty() && !b.membershipEvidence.name.isEmpty());
}

void TestMediaEngineAvbReferences::onlyReferencedGroupIncludesAllAngles()
{
	auto s = twoSequences();
	add(s, 10, "SLCT");
	add(s, 11, "SLCT");
	clip(s, 12, identity('C'));
	composition(s, 13, identity('C'));
	locator(s, 14, identity('C'));
	edge(s, 13, 14, QStringLiteral("Composition.descriptor"));
	for (auto &relation : s->relationships)
		if (relation.origin == 2 && relation.target == 4)
		{
			relation.target = 10;
			relation.recordedReference = quint32(10);
		}
	for (auto &property : object(s, 2).properties)
		if (property.locator.name == QStringLiteral("TrackGroup.tracks[0].component"))
			property.decoded = quint32(10);
	edge(s, 10, 4, QStringLiteral("TrackGroup.tracks[0].component"));
	edge(s, 10, 5, QStringLiteral("TrackGroup.tracks[1].component"));
	field(s, 10, QStringLiteral("Selector.selected"), 0);
	edge(s, 11, 12, QStringLiteral("TrackGroup.tracks[0].component"));
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, 10, 5));
}

void TestMediaEngineAvbReferences::precomputedAndDisabledTracksRemainReachable()
{
	auto s = twoSequences();
	edge(s, 2, 5, QStringLiteral("Component.precomputed"));
	field(s, 2, QStringLiteral("TrackGroup.tracks[0].flags"), 0);
	field(s, 2, QStringLiteral("TrackGroup.tracks[0].enabled"), false);
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, 2, 4));
	QVERIFY(hasEdge(result, 2, 5));
}

void TestMediaEngineAvbReferences::cyclesKeepEdgesAndTerminate()
{
	auto s = twoSequences();
	edge(s, 8, 2, QStringLiteral("@authored_cycle"));
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(result.mediaReferences.size(), 1);
	QVERIFY(hasEdge(result, 8, 2));
	QVERIFY(result.edges.size() < 20);
}

void TestMediaEngineAvbReferences::unresolvedAndNullReferences()
{
	auto s = twoSequences();
	edge(s, 2, 0, QStringLiteral("Component.left_bob"));
	Cancellation cancel;
	AvbReferenceIndex nullIndex(s, cancel);
	QVERIFY(nullIndex.resolveReferences(selected({2}), cancel).coverageComplete);
	edge(s, 2, 99, QStringLiteral("Component.precomputed"));
	s->relationships.last().target = 0; // Reader retains the recorded invalid index.
	AvbReferenceIndex invalidIndex(s, cancel);
	const auto invalid = invalidIndex.resolveReferences(selected({2}), cancel);
	QVERIFY(!invalid.coverageComplete);
	QVERIFY(hasIssue(invalid, Issue::UnresolvedReference));
	QCOMPARE(mediaIds(invalid), QSet<QByteArray>{identity('A')});
}

void TestMediaEngineAvbReferences::eachBinResolvesIndependently()
{
	auto a = source();
	root(a);
	composition(a, 2, identity('S'), 1);
	member(a, 2, 0, true);
	clip(a, 3, identity('A'));
	edge(a, 2, 3, QStringLiteral("TrackGroup.tracks[0].component"));
	auto b = source();
	root(b);
	composition(b, 2, identity('A'));
	member(b, 2, 0, true);
	locator(b, 3, identity('A'));
	edge(b, 2, 3, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	AvbReferenceIndex first(a, cancel);
	const auto before = first.resolveReferences({}, cancel);
	QVERIFY(!before.coverageComplete);
	QVERIFY(hasIssue(before, Issue::UnresolvedReference));
	QVERIFY(before.mediaReferences.isEmpty());
	AvbReferenceIndex other(b, cancel);
	const auto independent = other.resolveReferences({}, cancel);
	QVERIFY(independent.coverageComplete);
	QCOMPARE(mediaIds(independent), QSet<QByteArray>{identity('A')});
	const auto after = first.resolveReferences({}, cancel);
	QCOMPARE(after.coverageComplete, before.coverageComplete);
	QCOMPARE(after.roots, before.roots);
	QCOMPARE(after.edges.size(), before.edges.size());
	QCOMPARE(after.issues.size(), before.issues.size());
	QVERIFY(after.mediaReferences.isEmpty());
	QVERIFY(hasIssue(after, Issue::UnresolvedReference));

	// A local object with a similar short ID is no substitute for the full ID.
	composition(a, 4, identity('B'));
	locator(a, 5, identity('B'));
	edge(a, 4, 5, QStringLiteral("Composition.descriptor"));
	const auto nearMatch = AvbReferenceIndex(a, cancel).resolveReferences({}, cancel);
	QVERIFY(!nearMatch.coverageComplete);
	QVERIFY(hasIssue(nearMatch, Issue::UnresolvedReference));
	QVERIFY(nearMatch.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::wholeBinFollowsOnlyListedItems()
{
	auto s = twoSequences();
	// The second listed item is a master clip, not a selectable sequence.
	for (auto &property : object(s, 3).properties)
		if (property.locator.name == QStringLiteral("Composition.mob_type"))
			property.decoded = 2;
	object(s, 3).role = AvidObject::Role::Master;
	// These file-source dependencies are not listed, but are reached from the
	// listed sequence and master. A repeated list slot does not duplicate them.
	member(s, 2, 2, true);
	composition(s, 10, identity('C'));
	locator(s, 11, identity('C'));
	edge(s, 10, 11, QStringLiteral("Composition.descriptor"));
	composition(s, 12, identity('U'), 1);
	clip(s, 13, identity('C'));
	edge(s, 12, 13, QStringLiteral("TrackGroup.tracks[0].component"));
	Cancellation cancel;
	for (const auto &classId : {QByteArray("ABIN"), QByteArray("BINF")})
	{
		auto context = QSharedPointer<AvbObjectContext>::create(*object(s, 1).avb);
		context->classId = classId;
		object(s, 1).avb = context;
		AvbReferenceIndex index(s, cancel);
		QCOMPARE(index.sequences().size(), 1);
		const auto result = index.resolveReferences({}, cancel);
		QVERIFY(result.coverageComplete);
		QCOMPARE(result.roots, (QVector<ObjectHandle>{2, 3}));
		QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
		QCOMPARE(result.mediaReferences.size(), 2);
		QVERIFY(hasEdge(result, 4, 6));
		QVERIFY(hasEdge(result, 5, 7));
		QVERIFY(!hasEdge(result, 12, 13));
	}
}

void TestMediaEngineAvbReferences::wholeBinIncompleteMembership_data()
{
	QTest::addColumn<QString>("damage");
	QTest::addColumn<bool>("retainsMatches");
	QTest::newRow("missing-root") << QStringLiteral("missing-root") << false;
	QTest::newRow("wrong-root-class") << QStringLiteral("wrong-root-class") << false;
	QTest::newRow("missing-item-count") << QStringLiteral("missing-item-count") << true;
	QTest::newRow("missing-list-slot") << QStringLiteral("missing-list-slot") << true;
	QTest::newRow("missing-listed-object") << QStringLiteral("missing-listed-object") << true;
	QTest::newRow("wrong-listed-class") << QStringLiteral("wrong-listed-class") << true;
	QTest::newRow("repeated-list-position") << QStringLiteral("repeated-list-position") << true;
	QTest::newRow("unsupported-list-reference") << QStringLiteral("unsupported-list-reference") << true;
	QTest::newRow("conflicting-item-count") << QStringLiteral("conflicting-item-count") << true;
	QTest::newRow("out-of-range-list-position") << QStringLiteral("out-of-range-list-position") << true;
}

void TestMediaEngineAvbReferences::wholeBinIncompleteMembership()
{
	QFETCH(QString, damage);
	QFETCH(bool, retainsMatches);
	auto s = twoSequences();
	if (damage == QStringLiteral("missing-root"))
		s->unownedProperties.clear();
	else if (damage == QStringLiteral("wrong-root-class"))
	{
		auto context = QSharedPointer<AvbObjectContext>::create(*object(s, 1).avb);
		context->classId = "SCLP";
		object(s, 1).avb = context;
	}
	else if (damage == QStringLiteral("missing-item-count"))
	{
		auto &properties = object(s, 1).properties;
		properties.removeIf([](const auto &property)
							{ return property.locator.name == QStringLiteral("Bin.item_count"); });
	}
	else if (damage == QStringLiteral("missing-list-slot"))
	{
		for (auto &property : object(s, 1).properties)
			if (property.locator.name == QStringLiteral("Bin.item_count"))
				property.decoded = quint32(3);
	}
	else if (damage == QStringLiteral("missing-listed-object"))
		member(s, 99, 2, true);
	else if (damage == QStringLiteral("wrong-listed-class"))
		member(s, 4, 2, true);
	else if (damage == QStringLiteral("repeated-list-position"))
		member(s, 2, 0, true);
	else if (damage == QStringLiteral("conflicting-item-count"))
		field(s, 1, QStringLiteral("Bin.item_count"), quint32(3));
	else
	{
		member(s, 2, 2, true);
		if (damage == QStringLiteral("unsupported-list-reference"))
			s->relationships.last().referenceEncoding = QStringLiteral("AVB.UnknownReference");
		else
		{
			for (auto &property : object(s, 1).properties)
				if (property.locator.name == QStringLiteral("Bin.item_count"))
					property.decoded = quint32(2);
		}
	}
	Cancellation cancel;
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences({}, cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.issues.isEmpty());
	if (retainsMatches)
		QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	else
		QVERIFY(result.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::emptyBinRequiresEstablishedMembership()
{
	auto s = source();
	root(s);
	// A proven empty list is complete, including an explicitly recorded null slot.
	Cancellation cancel;
	const auto empty = AvbReferenceIndex(s, cancel).resolveReferences({}, cancel);
	QVERIFY(empty.coverageComplete);
	QVERIFY(empty.roots.isEmpty() && empty.mediaReferences.isEmpty());
	member(s, 0, 0, false);
	const auto nullSlot = AvbReferenceIndex(s, cancel).resolveReferences({}, cancel);
	QVERIFY(nullSlot.coverageComplete);
	QVERIFY(nullSlot.roots.isEmpty() && nullSlot.mediaReferences.isEmpty());
	object(s, 1).properties.clear();
	const auto unknown = AvbReferenceIndex(s, cancel).resolveReferences({}, cancel);
	QVERIFY(!unknown.coverageComplete);
	QVERIFY(!unknown.issues.isEmpty());
	QVERIFY(unknown.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::duplicateIdentityIsAmbiguous()
{
	auto s = twoSequences();
	composition(s, 10, identity('A'));
	locator(s, 11, identity('A'));
	edge(s, 10, 11, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::AmbiguousReference));
	QCOMPARE(result.mediaReferences.size(), 2);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('A')});
	QVERIFY(hasEdge(result, 4, 6));
	QVERIFY(hasEdge(result, 4, 10));
}

void TestMediaEngineAvbReferences::exactTerminalIdentities_data()
{
	QTest::addColumn<QByteArray>("id");
	QTest::addColumn<bool>("filler");
	QTest::newRow("all-zero") << QByteArray(32, '\0') << false;
	QTest::newRow("avid-null") << QByteArray::fromHex("060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80") << false;
	QTest::newRow("avid-filler") << QByteArray::fromHex("060a2b340101010101010f00130000000000000001000000060e2b347f7f2a80") << true;
}

void TestMediaEngineAvbReferences::exactTerminalIdentities()
{
	QFETCH(QByteArray, id);
	QFETCH(bool, filler);
	auto s = source();
	root(s);
	composition(s, 2, identity('S'), 1);
	member(s, 2, 0, true);
	clip(s, 3, id);
	edge(s, 2, 3, QStringLiteral("TrackGroup.tracks[0].component"));
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(result.coverageComplete);
	QVERIFY(result.mediaReferences.isEmpty());
	QVERIFY(!hasIssue(result, Issue::UnresolvedReference));
	QCOMPARE(result.terminalReferences.size(), 1);
	QCOMPARE(result.terminalReferences[0].kind, filler ? AvbTerminalReference::Kind::Filler : AvbTerminalReference::Kind::Null);
	QCOMPARE(result.terminalReferences[0].objectHandle, ObjectHandle(3));
	QVERIFY(result.terminalReferences[0].relationshipIndex >= 0);

	// A different identifier which happens to resemble a sentinel is not null.
	auto altered = Source::create(*s);
	auto changed = id;
	changed[31] = char(quint8(changed[31]) ^ 1);
	for (auto &relation : altered->relationships)
		if (relation.origin == 3)
			relation.recordedReference = changed;
	for (auto &property : object(altered, 3).properties)
		if (property.locator.name == QStringLiteral("SourceClip.mob_id"))
			property.decoded = changed;
	AvbReferenceIndex changedIndex(altered, cancel);
	const auto changedResult = changedIndex.resolveReferences(selected({2}), cancel);
	QVERIFY(!changedResult.coverageComplete);
	QVERIFY(hasIssue(changedResult, Issue::UnresolvedReference));
	QVERIFY(changedResult.terminalReferences.isEmpty());
}

void TestMediaEngineAvbReferences::legacyOnlyClipIsExplicitlyUnsupported()
{
	auto s = source();
	root(s);
	composition(s, 2, identity('S'), 1);
	member(s, 2, 0, true);
	add(s, 3, "SCLP");
	field(s, 3, QStringLiteral("SourceClip.legacy_word0"), quint32(0x12345678));
	field(s, 3, QStringLiteral("SourceClip.legacy_word1"), quint32(0x87654321));
	edge(s, 2, 3, QStringLiteral("TrackGroup.tracks[0].component"));
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::UnsupportedIdentity));
	QVERIFY(result.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::audioSuiteDependencies()
{
	auto s = twoSequences();
	add(s, 10, "ASPI");
	edge(s, 2, 10, QStringLiteral("Component.precomputed"));
	field(s, 10, QStringLiteral("AudioSuitePluginEffect.mob_id"), identity('B'));
	auto &reference = s->relationships.emplaceBack();
	reference.origin = 10;
	reference.recordedReference = identity('B');
	reference.referenceEncoding = QStringLiteral("AVB.MobId.MaterialLE");
	reference.locator.name = QStringLiteral("AudioSuitePluginEffect.mob_id");
	reference.locator.objectNumber = 10;
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, 10, 7));

	auto legacy = Source::create(*s);
	object(legacy, 10).properties.clear();
	legacy->relationships.removeLast();
	field(legacy, 10, QStringLiteral("AudioSuitePluginEffect.legacy_word0"), quint32(0x12345678));
	field(legacy, 10, QStringLiteral("AudioSuitePluginEffect.legacy_word1"), quint32(0x87654321));
	AvbReferenceIndex legacyIndex(legacy, cancel);
	const auto legacyResult = legacyIndex.resolveReferences(selected({2}), cancel);
	QVERIFY(!legacyResult.coverageComplete);
	QVERIFY(hasIssue(legacyResult, Issue::UnsupportedIdentity));
	QCOMPARE(mediaIds(legacyResult), QSet<QByteArray>{identity('A')});
}

void TestMediaEngineAvbReferences::terminalLocatorIsNotMedia()
{
	auto s = twoSequences();
	for (auto &property : object(s, 8).properties)
		if (property.locator.name == QStringLiteral("MSMLocator.mob_id"))
			property.decoded = QByteArray::fromHex("060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80");
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(result.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::incomplete_locator_cannot_assert_modern_identity_absent()
{
	auto s = twoSequences();
	auto &value = object(s, 8);
	value.properties.clear();
	auto context = QSharedPointer<AvbObjectContext>::create(*value.avb);
	context->interpretationComplete = false;
	value.avb = context;
	field(s, 8, QStringLiteral("MSMLocator.legacy_word0"), quint32(0x12345678));
	field(s, 8, QStringLiteral("MSMLocator.legacy_word1"), quint32(0x87654321));
	Cancellation cancel;
	const auto partial = AvbReferenceIndex(s, cancel).resolveReferences(selected({2}), cancel);
	QVERIFY(!partial.coverageComplete);
	QVERIFY(partial.mediaReferences.isEmpty());
	QVERIFY(hasIssue(partial, Issue::UnsupportedIdentity));
	// A typed ID that was read before a later failure is independent evidence.
	field(s, 8, QStringLiteral("MSMLocator.mob_id"), identity('A'));
	const auto known = AvbReferenceIndex(s, cancel).resolveReferences(selected({2}), cancel);
	QVERIFY(!known.coverageComplete);
	QCOMPARE(mediaIds(known), QSet<QByteArray>{identity('A')});
}

void TestMediaEngineAvbReferences::fileDescriptorRequiresUsableMediaRoute_data()
{
	QTest::addColumn<bool>("pathOnly");
	QTest::newRow("missing-locator") << false;
	QTest::newRow("path-only-locator") << true;
}

void TestMediaEngineAvbReferences::fileDescriptorRequiresUsableMediaRoute()
{
	QFETCH(bool, pathOnly);
	auto s = twoSequences();
	add(s, 10, "CDCI");
	field(s, 10, QStringLiteral("MediaFileDescriptor.length"), 100);
	for (auto &relation : s->relationships)
		if (relation.origin == 6 && relation.target == 8)
		{
			relation.target = 10;
			relation.recordedReference = quint32(10);
		}
	for (auto &property : object(s, 6).properties)
		if (property.locator.name == QStringLiteral("Composition.descriptor"))
			property.decoded = quint32(10);
	if (pathOnly)
	{
		add(s, 11, "URLL");
		field(s, 11, QStringLiteral("URLLocator.path"), QStringLiteral("file:///unverified.mov"));
		edge(s, 10, 11, QStringLiteral("MediaDescriptor.locator"));
	}
	else
		edge(s, 10, 0, QStringLiteral("MediaDescriptor.locator"));
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.issues.isEmpty());
	QVERIFY(result.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::invalidSelectionAndIncompleteEvidence()
{
	auto s = twoSequences();
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto unavailable = AvbReferenceIndex({}, cancel).resolveReferences({}, cancel);
	QVERIFY(!unavailable.coverageComplete);
	QVERIFY(hasIssue(unavailable, Issue::InvalidSelection));
	for (const auto &scope : {selected({99}), selected({4})})
	{
		const auto result = index.resolveReferences(scope, cancel);
		QVERIFY(!result.coverageComplete);
		QVERIFY(hasIssue(result, Issue::InvalidSelection));
	}
	s->outcome = ParsedSource::Outcome::Incomplete;
	s->diagnostics.append(QStringLiteral("A retained extension is unknown"));
	AvbReferenceIndex partial(s, cancel);
	const auto result = partial.resolveReferences(selected({2}), cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::IncompleteSource));
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('A')});
}

void TestMediaEngineAvbReferences::cancellation()
{
	Cancellation initial;
	AvbReferenceIndex index(twoSequences(), initial);
	Cancellation cancelled;
	cancelled.cancel();
	const auto result = index.resolveReferences(selected({2}), cancelled);
	QVERIFY(result.cancelled && !result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::Cancelled));
	AvbReferenceIndex cancelledIndex(twoSequences(), cancelled);
	const auto duringIndex = cancelledIndex.resolveReferences(selected({2}), initial);
	QVERIFY(!duringIndex.coverageComplete);
}

void TestMediaEngineAvbReferences::authoredBinaryGraph()
{
	TestAvb::Document document;
	document.objects = {
		{"ABIN", TestAvb::bin(false, {2, 4})},
		{"CMPO", TestAvb::composition(false, TestAvb::Master, "Chosen sequence", 0, {3}, 0, true, 0, 1)},
		{"SCLP", TestAvb::sourceClip(false)},
		{"CMPO", TestAvb::composition(false, TestAvb::Source, "File source", 0, {}, 0, true, 5, 3)},
		{"MSML", TestAvb::mediaLocator(false)}};
	auto bytes = document.bytes();
	QBuffer device(&bytes);
	device.open(QIODevice::ReadOnly);
	Cancellation cancel;
	auto parsed = Source::create(AvbReader{}.read(device, {{}, cancel}));
	QCOMPARE(parsed->outcome, ParsedSource::Outcome::Complete);
	AvbReferenceIndex index(parsed, cancel);
	QCOMPARE(index.sequences().size(), 1);
	QCOMPARE(index.sequences()[0].name, QStringLiteral("Chosen sequence"));
	const auto result = index.resolveReferences(selected({2}), cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{TestAvb::Source});
	QVERIFY(hasEdge(result, 3, 4));
	QCOMPARE(result.mediaReferences[0].objectHandle, ObjectHandle(5));
	QVERIFY(result.mediaReferences[0].propertyIndex >= 0);
}

QTEST_GUILESS_MAIN(TestMediaEngineAvbReferences)
#include "tst_mediaengineavbreferences.moc"
