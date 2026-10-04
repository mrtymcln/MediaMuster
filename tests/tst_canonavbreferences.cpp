// Exercises selection rules using small, visible graphs. Binary decoding is
// tested separately; one complete authored bin checks their connection here.

#include "canon/avbreader.h"
#include "canon/avbreferences.h"
#include "testavb.h"

#include <QBuffer>
#include <QSet>
#include <QTest>

namespace
{
	using namespace Canon;
	using Source = QSharedPointer<ParsedSource>;
	using Issue = AvbReferenceIssue::Kind;

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
	}
	void member(Source &source, ObjectHandle handle, int index, bool userPlaced)
	{
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
	AvbScope selected(QVector<ObjectHandle> handles, qsizetype sourceIndex = 0)
	{
		return {sourceIndex, AvbScope::Kind::SelectedSequences, std::move(handles)};
	}
	QSet<QByteArray> mediaIds(const AvbResolution &result)
	{
		QSet<QByteArray> ids;
		for (const auto &media : result.media)
			ids.insert(media.mobId);
		return ids;
	}
	bool hasIssue(const AvbResolution &result, Issue kind)
	{
		for (const auto &issue : result.issues)
			if (issue.kind == kind)
				return true;
		return false;
	}
	bool hasEdge(const AvbResolution &result, AvbObjectKey origin, AvbObjectKey target)
	{
		for (const auto &edge : result.edges)
			if (edge.origin == origin && edge.target == target && edge.relationship >= 0)
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

class TestCanonAvbReferences final : public QObject
{
	Q_OBJECT
private slots:
	void selectionAndUnion();
	void sequenceIdentityIsNotItsNameOrPlacement();
	void onlyReferencedGroupIncludesAllAngles();
	void precomputedAndDisabledTracksRemainReachable();
	void cyclesKeepEdgesAndTerminate();
	void unresolvedAndNullReferences();
	void exactIdentityAndCrossBinReferences();
	void duplicateIdentityIsAmbiguous();
	void exactTerminalIdentities_data();
	void exactTerminalIdentities();
	void legacyOnlyClipIsExplicitlyUnsupported();
	void audioSuiteDependencies();
	void terminalLocatorIsNotMedia();
	void fileDescriptorRequiresUsableMediaRoute_data();
	void fileDescriptorRequiresUsableMediaRoute();
	void invalidSelectionAndIncompleteEvidence();
	void cancellation();
	void authoredBinaryGraph();
};

void TestCanonAvbReferences::selectionAndUnion()
{
	Cancellation cancel;
	AvbReferenceIndex index({twoSequences()}, cancel);
	const auto first = index.resolve({selected({2})}, cancel);
	QVERIFY(first.complete);
	QCOMPARE(mediaIds(first), QSet<QByteArray>{identity('A')});
	const auto both = index.resolve({selected({2}), selected({3})}, cancel);
	QVERIFY(both.complete);
	QCOMPARE(mediaIds(both), (QSet<QByteArray>{identity('A'), identity('B')}));
	QCOMPARE(both.media.size(), 2);
	const auto repeated = index.resolve({selected({2, 2}), selected({2})}, cancel);
	QVERIFY(repeated.complete);
	QCOMPARE(repeated.media.size(), 1);
	const auto whole = index.resolve({{0, AvbScope::Kind::EntireBin, {}}}, cancel);
	QVERIFY(whole.complete);
	QCOMPARE(mediaIds(whole), mediaIds(both));
	const auto empty = index.resolve({selected({})}, cancel);
	QVERIFY(empty.complete);
	QVERIFY(empty.roots.isEmpty() && empty.media.isEmpty() && empty.edges.isEmpty());
}

void TestCanonAvbReferences::sequenceIdentityIsNotItsNameOrPlacement()
{
	Cancellation cancel;
	AvbReferenceIndex index({twoSequences()}, cancel);
	QCOMPARE(index.sequences().size(), 2);
	const auto &a = index.sequences()[0];
	const auto &b = index.sequences()[1];
	QCOMPARE(a.name, b.name);
	QVERIFY(!(a.key == b.key));
	QVERIFY(a.mobId != b.mobId);
	QVERIFY(a.userPlaced != b.userPlaced);
	QVERIFY(!a.membership.name.isEmpty() && !b.membership.name.isEmpty());
}

void TestCanonAvbReferences::onlyReferencedGroupIncludesAllAngles()
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
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.complete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, {0, 10}, {0, 5}));
}

void TestCanonAvbReferences::precomputedAndDisabledTracksRemainReachable()
{
	auto s = twoSequences();
	edge(s, 2, 5, QStringLiteral("Component.precomputed"));
	field(s, 2, QStringLiteral("TrackGroup.tracks[0].flags"), 0);
	field(s, 2, QStringLiteral("TrackGroup.tracks[0].enabled"), false);
	Cancellation cancel;
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.complete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, {0, 2}, {0, 4}));
	QVERIFY(hasEdge(result, {0, 2}, {0, 5}));
}

void TestCanonAvbReferences::cyclesKeepEdgesAndTerminate()
{
	auto s = twoSequences();
	edge(s, 8, 2, QStringLiteral("@authored_cycle"));
	Cancellation cancel;
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.complete);
	QCOMPARE(result.media.size(), 1);
	QVERIFY(hasEdge(result, {0, 8}, {0, 2}));
	QVERIFY(result.edges.size() < 20);
}

void TestCanonAvbReferences::unresolvedAndNullReferences()
{
	auto s = twoSequences();
	edge(s, 2, 0, QStringLiteral("Component.left_bob"));
	Cancellation cancel;
	AvbReferenceIndex nullIndex({s}, cancel);
	QVERIFY(nullIndex.resolve({selected({2})}, cancel).complete);
	edge(s, 2, 99, QStringLiteral("Component.precomputed"));
	s->relationships.last().target = 0; // Reader retains the recorded invalid index.
	AvbReferenceIndex invalidIndex({s}, cancel);
	const auto invalid = invalidIndex.resolve({selected({2})}, cancel);
	QVERIFY(!invalid.complete);
	QVERIFY(hasIssue(invalid, Issue::UnresolvedReference));
	QCOMPARE(mediaIds(invalid), QSet<QByteArray>{identity('A')});
}

void TestCanonAvbReferences::exactIdentityAndCrossBinReferences()
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
	locator(b, 3, identity('A'));
	edge(b, 2, 3, QStringLiteral("Composition.descriptor"));
	// Same short material prefix, distinct complete identity: never match this.
	composition(b, 4, identity('B'));
	locator(b, 5, identity('B'));
	edge(b, 4, 5, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	AvbReferenceIndex missing({a}, cancel);
	QVERIFY(hasIssue(missing.resolve({selected({2})}, cancel), Issue::UnresolvedReference));
	AvbReferenceIndex index({a, b}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.complete);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('A')});
	QVERIFY(hasEdge(result, {0, 3}, {1, 2}));
	QCOMPARE(result.media[0].locator.source, qsizetype(1));
}

void TestCanonAvbReferences::duplicateIdentityIsAmbiguous()
{
	auto a = twoSequences();
	auto b = source();
	root(b);
	composition(b, 2, identity('A'));
	locator(b, 3, identity('A'));
	edge(b, 2, 3, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	AvbReferenceIndex index({a, b}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(!result.complete);
	QVERIFY(hasIssue(result, Issue::AmbiguousReference));
	QCOMPARE(result.media.size(), 2);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('A')});
	QVERIFY(hasEdge(result, {0, 4}, {0, 6}));
	QVERIFY(hasEdge(result, {0, 4}, {1, 2}));
}

void TestCanonAvbReferences::exactTerminalIdentities_data()
{
	QTest::addColumn<QByteArray>("id");
	QTest::addColumn<bool>("filler");
	QTest::newRow("all-zero") << QByteArray(32, '\0') << false;
	QTest::newRow("avid-null") << QByteArray::fromHex("060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80") << false;
	QTest::newRow("avid-filler") << QByteArray::fromHex("060a2b340101010101010f00130000000000000001000000060e2b347f7f2a80") << true;
}

void TestCanonAvbReferences::exactTerminalIdentities()
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
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.complete);
	QVERIFY(result.media.isEmpty());
	QVERIFY(!hasIssue(result, Issue::UnresolvedReference));
	QCOMPARE(result.terminals.size(), 1);
	QCOMPARE(result.terminals[0].kind, filler ? AvbTerminalReference::Kind::Filler : AvbTerminalReference::Kind::Null);
	QCOMPARE(result.terminals[0].object.object, ObjectHandle(3));
	QVERIFY(result.terminals[0].relationship >= 0);

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
	AvbReferenceIndex changedIndex({altered}, cancel);
	const auto changedResult = changedIndex.resolve({selected({2})}, cancel);
	QVERIFY(!changedResult.complete);
	QVERIFY(hasIssue(changedResult, Issue::UnresolvedReference));
	QVERIFY(changedResult.terminals.isEmpty());
}

void TestCanonAvbReferences::legacyOnlyClipIsExplicitlyUnsupported()
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
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(!result.complete);
	QVERIFY(hasIssue(result, Issue::UnsupportedIdentity));
	QVERIFY(result.media.isEmpty());
}

void TestCanonAvbReferences::audioSuiteDependencies()
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
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.complete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, {0, 10}, {0, 7}));

	auto legacy = Source::create(*s);
	object(legacy, 10).properties.clear();
	legacy->relationships.removeLast();
	field(legacy, 10, QStringLiteral("AudioSuitePluginEffect.legacy_word0"), quint32(0x12345678));
	field(legacy, 10, QStringLiteral("AudioSuitePluginEffect.legacy_word1"), quint32(0x87654321));
	AvbReferenceIndex legacyIndex({legacy}, cancel);
	const auto legacyResult = legacyIndex.resolve({selected({2})}, cancel);
	QVERIFY(!legacyResult.complete);
	QVERIFY(hasIssue(legacyResult, Issue::UnsupportedIdentity));
	QCOMPARE(mediaIds(legacyResult), QSet<QByteArray>{identity('A')});
}

void TestCanonAvbReferences::terminalLocatorIsNotMedia()
{
	auto s = twoSequences();
	for (auto &property : object(s, 8).properties)
		if (property.locator.name == QStringLiteral("MSMLocator.mob_id"))
			property.decoded = QByteArray::fromHex("060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80");
	Cancellation cancel;
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.media.isEmpty());
}

void TestCanonAvbReferences::fileDescriptorRequiresUsableMediaRoute_data()
{
	QTest::addColumn<bool>("pathOnly");
	QTest::newRow("missing-locator") << false;
	QTest::newRow("path-only-locator") << true;
}

void TestCanonAvbReferences::fileDescriptorRequiresUsableMediaRoute()
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
	AvbReferenceIndex index({s}, cancel);
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(!result.complete);
	QVERIFY(!result.issues.isEmpty());
	QVERIFY(result.media.isEmpty());
}

void TestCanonAvbReferences::invalidSelectionAndIncompleteEvidence()
{
	auto s = twoSequences();
	Cancellation cancel;
	AvbReferenceIndex index({s}, cancel);
	for (const auto &scope : {selected({99}), selected({4}), selected({2}, 99)})
	{
		const auto result = index.resolve({scope}, cancel);
		QVERIFY(!result.complete);
		QVERIFY(hasIssue(result, Issue::InvalidSelection));
	}
	s->outcome = ParsedSource::Outcome::Incomplete;
	s->diagnostics.append(QStringLiteral("A retained extension is unknown"));
	AvbReferenceIndex partial({s}, cancel);
	const auto result = partial.resolve({selected({2})}, cancel);
	QVERIFY(!result.complete);
	QVERIFY(hasIssue(result, Issue::IncompleteSource));
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('A')});
}

void TestCanonAvbReferences::cancellation()
{
	Cancellation initial;
	AvbReferenceIndex index({twoSequences()}, initial);
	Cancellation cancelled;
	cancelled.cancel();
	const auto result = index.resolve({selected({2})}, cancelled);
	QVERIFY(result.cancelled && !result.complete);
	QVERIFY(hasIssue(result, Issue::Cancelled));
	AvbReferenceIndex cancelledIndex({twoSequences()}, cancelled);
	const auto duringIndex = cancelledIndex.resolve({selected({2})}, initial);
	QVERIFY(!duringIndex.complete);
}

void TestCanonAvbReferences::authoredBinaryGraph()
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
	AvbReferenceIndex index({parsed}, cancel);
	QCOMPARE(index.sequences().size(), 1);
	QCOMPARE(index.sequences()[0].name, QStringLiteral("Chosen sequence"));
	const auto result = index.resolve({selected({2})}, cancel);
	QVERIFY(result.complete);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{TestAvb::Source});
	QVERIFY(hasEdge(result, {0, 3}, {0, 4}));
	QCOMPARE(result.media[0].locator.object, ObjectHandle(5));
	QVERIFY(result.media[0].property >= 0);
}

QTEST_GUILESS_MAIN(TestCanonAvbReferences)
#include "tst_canonavbreferences.moc"
