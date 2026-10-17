// Exercises Whole Bin references using small, visible graphs. Binary decoding is
// tested separately; one complete authored bin checks their connection here.

#include "mediaengine/avbreader.h"
#include "mediaengine/avbreferences.h"
#include "testavb.h"

#include <QBuffer>
#include <QSet>
#include <QTest>
#include <algorithm>

namespace
{
	using namespace MediaEngine;
	using Source = QSharedPointer<ParsedSource>;
	using Issue = AvbResolutionIssue::Kind;
	constexpr int pictureKind = 1;
	constexpr int audioKind = 2;

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
	void removeField(Source &source, ObjectHandle owner, const QString &name)
	{
		object(source, owner).properties.removeIf([&](const auto &property)
												   { return property.locator.name == name; });
	}
	void replaceField(Source &source, ObjectHandle owner, const QString &name, const QVariant &value)
	{
		removeField(source, owner, name);
		field(source, owner, name, value);
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
		field(source, handle, QStringLiteral("TrackGroup.track_count"), quint32(0));
	}
	void clip(Source &source, ObjectHandle handle, const QByteArray &id,
			  int trackId = 1, int mediaKind = pictureKind)
	{
		add(source, handle, "SCLP");
		field(source, handle, QStringLiteral("SourceClip.mob_id"), id);
		field(source, handle, QStringLiteral("SourceClip.track_id"), trackId);
		field(source, handle, QStringLiteral("Component.media_kind_id"), mediaKind);
		auto &relation = source->relationships.emplaceBack();
		relation.origin = handle;
		relation.recordedReference = id;
		relation.referenceEncoding = QStringLiteral("AVB.MobId.MaterialLE");
		relation.locator.name = QStringLiteral("SourceClip.mob_id");
		relation.locator.objectNumber = handle;
	}
	void track(Source &source, ObjectHandle owner, int slot, int trackId,
			   ObjectHandle component, int mediaKind = pictureKind)
	{
		auto &properties = object(source, owner).properties;
		const auto countName = QStringLiteral("TrackGroup.track_count");
		auto count = std::find_if(properties.begin(), properties.end(), [&](const auto &property)
								  { return property.locator.name == countName; });
		if (count == properties.end())
			field(source, owner, countName, quint32(slot + 1));
		else
			count->decoded = qMax(count->decoded.toUInt(), quint32(slot + 1));
		const auto prefix = QStringLiteral("TrackGroup.tracks[%1]").arg(slot);
		field(source, owner, prefix + QStringLiteral(".flags"), quint16(0x0005));
		field(source, owner, prefix + QStringLiteral(".index"), trackId);
		field(source, component, QStringLiteral("Component.media_kind_id"), mediaKind);
		edge(source, owner, component, prefix + QStringLiteral(".component"));
	}
	void locator(Source &source, ObjectHandle handle, const QByteArray &id)
	{
		add(source, handle, "MSML");
		field(source, handle, QStringLiteral("MSMLocator.mob_id"), id);
	}
	void sequence(Source &source, ObjectHandle handle, const QVector<ObjectHandle> &components)
	{
		add(source, handle, "SEQU");
		field(source, handle, QStringLiteral("Sequence.component_count"), quint32(components.size()));
		for (qsizetype i = 0; i < components.size(); ++i)
			edge(source, handle, components[i], QStringLiteral("Sequence.components[%1]").arg(i));
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
	Source listedItems(bool includeSecond = false)
	{
		auto result = source();
		root(result);
		composition(result, 2, identity('S'), 1, QStringLiteral("Same name"));
		composition(result, 3, identity('T'), 1, QStringLiteral("Same name"));
		member(result, 2, 0, true);
		if (includeSecond)
			member(result, 3, 1, false);
		clip(result, 4, identity('A'));
		clip(result, 5, identity('B'));
		composition(result, 6, identity('A'));
		composition(result, 7, identity('B'));
		locator(result, 8, identity('A'));
		locator(result, 9, identity('B'));
		track(result, 2, 0, 1, 4);
		track(result, 3, 0, 1, 5);
		edge(result, 6, 8, QStringLiteral("Composition.descriptor"));
		edge(result, 7, 9, QStringLiteral("Composition.descriptor"));
		return result;
	}
	Source sourceTrackGraph()
	{
		auto result = source();
		root(result);
		composition(result, 2, identity('S'), 1);
		member(result, 2, 0, true);
		clip(result, 3, identity('M'), 7, audioKind);
		clip(result, 4, identity('M'), 2, audioKind);
		composition(result, 5, identity('M'), 2);
		clip(result, 6, identity('A'), 1, audioKind);
		clip(result, 7, identity('B'), 1, audioKind);
		clip(result, 8, identity('V'), 1, pictureKind);
		track(result, 2, 0, 1, 3, audioKind);
		// Slot order is not the source-track number, and audio/video may use
		// the same number. A request for A7 must follow neither A2 nor V7.
		track(result, 5, 0, 7, 6, audioKind);
		track(result, 5, 1, 2, 7, audioKind);
		track(result, 5, 2, 7, 8, pictureKind);
		composition(result, 9, identity('A'));
		locator(result, 10, identity('A'));
		edge(result, 9, 10, QStringLiteral("Composition.descriptor"));
		composition(result, 11, identity('B'));
		locator(result, 12, identity('B'));
		edge(result, 11, 12, QStringLiteral("Composition.descriptor"));
		composition(result, 13, identity('V'));
		locator(result, 14, identity('V'));
		edge(result, 13, 14, QStringLiteral("Composition.descriptor"));
		return result;
	}
}

class TestMediaEngineAvbReferences final : public QObject
{
	Q_OBJECT
private slots:
	void wholeBinCombinesListedItems();
	void wholeBinFollowsNestedTrackBeyondRecordedExcerpt();
	void referencedGroupIncludesAllAngles();
	void precomputedAndDisabledTracksRemainReachable();
	void sourceTrackMatchesNumberAndKind();
	void sourceTrackRequestsUnionAndPreserveOutputOccurrences();
	void sourceTrackContextCyclesTerminate();
	void sourceTrackUncertainty_data();
	void sourceTrackUncertainty();
	void uncertainTrackCannotLeakDescriptorMedia();
	void descriptorOnlySourceNeedsNoTrackBranch();
	void knownEmptyTrackSlotsDoNotAddCandidates_data();
	void knownEmptyTrackSlotsDoNotAddCandidates();
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
	void invalidSourceAndIncompleteEvidence();
	void cancellation();
	void authoredBinaryGraph();
};

void TestMediaEngineAvbReferences::wholeBinCombinesListedItems()
{
	auto s = listedItems(true);
	Cancellation cancel;
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(result.roots, (QVector<ObjectHandle>{2, 3}));
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QCOMPARE(result.mediaReferences.size(), 2);
	// Repeated listing does not duplicate roots or file references. A listed
	// non-user-placed item contributes exactly as the first listed item does.
	member(s, 2, 2, true);
	const auto repeated = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(repeated.coverageComplete);
	QCOMPARE(repeated.roots, result.roots);
	QCOMPARE(mediaIds(repeated), mediaIds(result));
	QCOMPARE(repeated.mediaReferences.size(), 2);
}

void TestMediaEngineAvbReferences::wholeBinFollowsNestedTrackBeyondRecordedExcerpt()
{
	auto s = listedItems();
	composition(s, 10, identity('N'), 1);
	clip(s, 12, identity('A'));
	clip(s, 13, identity('B'));
	sequence(s, 11, {12, 13});
	track(s, 10, 0, 1, 11);
	// The referring clip records only one unit at the beginning. Whole Bin
	// follows the entire linked track, including B later in the nested edit.
	field(s, 4, QStringLiteral("Clip.length"), quint32(1));
	field(s, 4, QStringLiteral("SourceClip.start_time"), qint32(0));
	for (const auto handle : {ObjectHandle(12), ObjectHandle(13)})
		field(s, handle, QStringLiteral("Clip.length"), quint32(100));
	replaceField(s, 4, QStringLiteral("SourceClip.mob_id"), identity('N'));
	for (auto &edge : s->relationships)
		if (edge.origin == 4 && edge.locator.name == QLatin1String("SourceClip.mob_id"))
			edge.recordedReference = identity('N');
	// Broad linked-track traversal still excludes disconnected internal records.
	composition(s, 14, identity('C'));
	locator(s, 15, identity('C'));
	edge(s, 14, 15, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(result.roots, QVector<ObjectHandle>{2});
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, 11, 13));
	QVERIFY(!hasEdge(result, 14, 15));
}

void TestMediaEngineAvbReferences::referencedGroupIncludesAllAngles()
{
	auto s = listedItems();
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
	track(s, 10, 0, 1, 4);
	track(s, 10, 1, 2, 5);
	field(s, 10, QStringLiteral("Selector.selected"), 0);
	field(s, 10, QStringLiteral("Selector.is_ganged"), false);
	track(s, 11, 0, 1, 12);
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, 10, 5));
}

void TestMediaEngineAvbReferences::precomputedAndDisabledTracksRemainReachable()
{
	auto s = listedItems();
	edge(s, 2, 5, QStringLiteral("Component.precomputed"));
	// This authored disabled marker checks reachability, not native mute decoding.
	field(s, 2, QStringLiteral("TrackGroup.tracks[0].enabled"), false);
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, 2, 4));
	QVERIFY(hasEdge(result, 2, 5));
}

void TestMediaEngineAvbReferences::sourceTrackMatchesNumberAndKind()
{
	Cancellation cancel;
	for (const auto mediaKind : {audioKind, pictureKind})
	{
		auto s = sourceTrackGraph();
		replaceField(s, 3, QStringLiteral("Component.media_kind_id"), mediaKind);
		const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
		QVERIFY(result.coverageComplete);
		QCOMPARE(mediaIds(result), QSet<QByteArray>{identity(mediaKind == audioKind ? 'A' : 'V')});
		QCOMPARE(result.mediaReferences.size(), 1);
		QVERIFY(hasEdge(result, 3, 5));
		QCOMPARE(hasEdge(result, 5, 6), mediaKind == audioKind);
		QCOMPARE(hasEdge(result, 5, 8), mediaKind == pictureKind);
		QVERIFY(!hasEdge(result, 5, 7));
	}
	// A2 is in slot 1; source-track numbers are never array positions.
	auto s = sourceTrackGraph();
	replaceField(s, 3, QStringLiteral("SourceClip.track_id"), 2);
	const auto second = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(second.coverageComplete);
	QCOMPARE(mediaIds(second), QSet<QByteArray>{identity('B')});
	QVERIFY(hasEdge(second, 5, 7));
	QVERIFY(!hasEdge(second, 5, 6) && !hasEdge(second, 5, 8));

	// Whole Bin also starts at the listed master itself, with all of its tracks.
	member(s, 5, 1, true);
	const auto whole = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(whole.coverageComplete);
	QCOMPARE(mediaIds(whole), (QSet<QByteArray>{identity('A'), identity('B'), identity('V')}));
	QCOMPARE(whole.mediaReferences.size(), 3);
}

void TestMediaEngineAvbReferences::sourceTrackRequestsUnionAndPreserveOutputOccurrences()
{
	auto s = sourceTrackGraph();
	track(s, 2, 1, 2, 4, audioKind);
	// Repeated requests still describe the same reachable edge and locator.
	clip(s, 15, identity('M'), 7, audioKind);
	track(s, 2, 2, 3, 15, audioKind);
	clip(s, 16, QByteArray(32, '\0'));
	edge(s, 5, 16, QStringLiteral("Component.precomputed"));
	// The descriptor is independent of the requested track and is reached twice.
	edge(s, 5, 10, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QCOMPARE(result.mediaReferences.size(), 2);
	QCOMPARE(result.terminalReferences.size(), 1);
	QCOMPARE(result.terminalReferences[0].objectHandle, ObjectHandle(16));
	QVERIFY(hasEdge(result, 5, 6) && hasEdge(result, 5, 7));
	QVERIFY(!hasEdge(result, 5, 8));
	QSet<QPair<qsizetype, ObjectHandle>> occurrences;
	for (const auto &resolved : result.edges)
	{
		const auto occurrence = qMakePair(resolved.relationshipIndex, resolved.target);
		QVERIFY(!occurrences.contains(occurrence));
		occurrences.insert(occurrence);
	}
}

void TestMediaEngineAvbReferences::sourceTrackContextCyclesTerminate()
{
	auto s = sourceTrackGraph();
	// A7 leads back to this master asking for A2; A2 reaches physical media.
	for (auto &relation : s->relationships)
		if (relation.origin == 6 && relation.locator.name == QStringLiteral("SourceClip.mob_id"))
			relation.recordedReference = identity('M');
	replaceField(s, 6, QStringLiteral("SourceClip.mob_id"), identity('M'));
	replaceField(s, 6, QStringLiteral("SourceClip.track_id"), 2);
	clip(s, 15, identity('M'), 7, audioKind);
	edge(s, 5, 15, QStringLiteral("Component.precomputed"));
	Cancellation cancel;
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('B')});
	QCOMPARE(result.mediaReferences.size(), 1);
	QVERIFY(hasEdge(result, 6, 5));
	QVERIFY(hasEdge(result, 5, 6) && hasEdge(result, 5, 7));
	QVERIFY(result.edges.size() < 20);
}

void TestMediaEngineAvbReferences::sourceTrackUncertainty_data()
{
	QTest::addColumn<QString>("damage");
	QTest::addColumn<bool>("keepsFirst");
	QTest::addColumn<bool>("complete");
	for (const auto *damage : {"source-track-missing", "source-track-zero", "source-track-conflict", "source-track-unreadable",
							  "source-kind-missing", "source-kind-unsupported", "source-kind-conflict", "source-kind-unreadable",
							  "target-index-missing", "target-index-conflict", "target-index-unreadable", "target-kind-missing",
							  "target-kind-conflict", "target-kind-unreadable", "target-duplicate", "target-count-missing",
							  "target-count-conflict", "target-count-unreadable", "target-missing-slot", "target-component-missing",
							  "target-flags-missing", "target-flags-conflict", "target-flags-no-index", "target-flags-no-component"})
		QTest::newRow(damage) << QString::fromLatin1(damage) << false << false;
	QTest::newRow("partial-source-known-facts") << QStringLiteral("partial-source-known-facts") << true << false;
	QTest::newRow("partial-target-known-facts") << QStringLiteral("partial-target-known-facts") << true << false;
	QTest::newRow("nonmatching-number-without-kind") << QStringLiteral("nonmatching-number-without-kind") << true << true;
	QTest::newRow("nonmatching-kind-without-number") << QStringLiteral("nonmatching-kind-without-number") << true << true;
}

void TestMediaEngineAvbReferences::sourceTrackUncertainty()
{
	QFETCH(QString, damage);
	QFETCH(bool, keepsFirst);
	QFETCH(bool, complete);
	auto s = sourceTrackGraph();
	// This route is independent of the uncertain master-track request.
	clip(s, 15, identity('B'), 1, audioKind);
	track(s, 2, 1, 2, 15, audioKind);
	const auto sourceTrack = QStringLiteral("SourceClip.track_id");
	const auto mediaKind = QStringLiteral("Component.media_kind_id");
	const auto targetIndex = QStringLiteral("TrackGroup.tracks[0].index");
	const auto targetFlags = QStringLiteral("TrackGroup.tracks[0].flags");
	const auto count = QStringLiteral("TrackGroup.track_count");
	if (damage.endsWith(QStringLiteral("-unreadable")))
	{
		const auto owner = damage.startsWith(QStringLiteral("source-")) ? 3
						 : damage == QStringLiteral("target-kind-unreadable") ? 6 : 5;
		const auto name = damage == QStringLiteral("source-track-unreadable") ? sourceTrack
						: damage.contains(QStringLiteral("kind")) ? mediaKind
						: damage.contains(QStringLiteral("index")) ? targetIndex : count;
		for (auto &property : object(s, owner).properties)
			if (property.locator.name == name)
				property.state = PropertyReadState::Unreadable;
	}
	else if (damage == QStringLiteral("source-track-missing"))
		removeField(s, 3, sourceTrack);
	else if (damage == QStringLiteral("source-track-zero"))
		replaceField(s, 3, sourceTrack, 0);
	else if (damage == QStringLiteral("source-track-conflict"))
		field(s, 3, sourceTrack, 2);
	else if (damage == QStringLiteral("source-kind-missing"))
		removeField(s, 3, mediaKind);
	else if (damage == QStringLiteral("source-kind-unsupported"))
		replaceField(s, 3, mediaKind, 9);
	else if (damage == QStringLiteral("source-kind-conflict"))
		field(s, 3, mediaKind, pictureKind);
	else if (damage == QStringLiteral("target-index-missing"))
		removeField(s, 5, targetIndex);
	else if (damage == QStringLiteral("target-index-conflict"))
		field(s, 5, targetIndex, 2);
	else if (damage == QStringLiteral("target-kind-missing"))
		removeField(s, 6, mediaKind);
	else if (damage == QStringLiteral("target-kind-conflict"))
		field(s, 6, mediaKind, pictureKind);
	else if (damage == QStringLiteral("target-duplicate"))
		replaceField(s, 8, mediaKind, audioKind);
	else if (damage == QStringLiteral("target-count-missing"))
		removeField(s, 5, count);
	else if (damage == QStringLiteral("target-count-conflict"))
		field(s, 5, count, quint32(4));
	else if (damage == QStringLiteral("target-flags-missing"))
		removeField(s, 5, targetFlags);
	else if (damage == QStringLiteral("target-flags-conflict"))
		field(s, 5, targetFlags, quint16(0));
	else if (damage == QStringLiteral("target-flags-no-index"))
		replaceField(s, 5, targetFlags, quint16(0x0004));
	else if (damage == QStringLiteral("target-flags-no-component"))
		replaceField(s, 5, targetFlags, quint16(0x0001));
	else if (damage == QStringLiteral("target-missing-slot"))
		replaceField(s, 5, count, quint32(4));
	else if (damage == QStringLiteral("target-component-missing"))
	{
		removeField(s, 5, QStringLiteral("TrackGroup.tracks[0].component"));
		s->relationships.removeIf([](const auto &relation)
								  { return relation.origin == 5 && relation.locator.name == QStringLiteral("TrackGroup.tracks[0].component"); });
	}
	else if (damage.startsWith(QStringLiteral("partial-")))
	{
		auto &value = object(s, damage == QStringLiteral("partial-source-known-facts") ? 3 : 5);
		auto context = QSharedPointer<AvbObjectContext>::create(*value.avb);
		context->interpretationComplete = false;
		value.avb = context;
	}
	else if (damage == QStringLiteral("nonmatching-number-without-kind"))
		removeField(s, 7, mediaKind);
	else
		removeField(s, 5, QStringLiteral("TrackGroup.tracks[2].index"));
	Cancellation cancel;
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QCOMPARE(result.coverageComplete, complete);
	const auto expected = keepsFirst ? QSet<QByteArray>{identity('A'), identity('B')} : QSet<QByteArray>{identity('B')};
	QCOMPARE(mediaIds(result), expected);
	QCOMPARE(result.mediaReferences.size(), expected.size());
	QCOMPARE(hasEdge(result, 5, 6), keepsFirst);
	QVERIFY(!hasEdge(result, 5, 7) && !hasEdge(result, 5, 8));
	if (!complete)
		QVERIFY(!result.issues.isEmpty());
}

void TestMediaEngineAvbReferences::uncertainTrackCannotLeakDescriptorMedia()
{
	Cancellation cancel;
	for (const auto *damage : {"none", "source-track-missing", "target-no-match", "target-ambiguous",
							  "target-list-unknown", "source-id-conflict", "source-id-mismatch"})
	{
		auto s = sourceTrackGraph();
		locator(s, 25, identity('D'));
		edge(s, 5, 25, QStringLiteral("Composition.descriptor"));
		clip(s, 26, identity('B'), 1, audioKind);
		track(s, 2, 1, 2, 26, audioKind);
		const auto change = QLatin1String(damage);
		if (change == QLatin1String("source-track-missing"))
			removeField(s, 3, QStringLiteral("SourceClip.track_id"));
		else if (change == QLatin1String("target-no-match"))
			replaceField(s, 3, QStringLiteral("SourceClip.track_id"), 9);
		else if (change == QLatin1String("target-ambiguous"))
			replaceField(s, 8, QStringLiteral("Component.media_kind_id"), audioKind);
		else if (change == QLatin1String("target-list-unknown"))
			removeField(s, 5, QStringLiteral("TrackGroup.track_count"));
		else if (change == QLatin1String("source-id-conflict"))
			field(s, 3, QStringLiteral("SourceClip.mob_id"), identity('X'));
		else if (change == QLatin1String("source-id-mismatch"))
			replaceField(s, 3, QStringLiteral("SourceClip.mob_id"), identity('X'));
		const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
		const bool known = change == QLatin1String("none");
		QCOMPARE(result.coverageComplete, known);
		const auto expected = known ? QSet<QByteArray>{identity('A'), identity('B'), identity('D')}
									: QSet<QByteArray>{identity('B')};
		QCOMPARE(mediaIds(result), expected);
		QCOMPARE(result.mediaReferences.size(), expected.size());
		QCOMPARE(hasEdge(result, 5, 25), known);
		if (!known)
			QVERIFY(!result.issues.isEmpty());
	}
}

void TestMediaEngineAvbReferences::descriptorOnlySourceNeedsNoTrackBranch()
{
	auto s = listedItems();
	Cancellation cancel;
	const auto known = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(known.coverageComplete);
	QCOMPARE(mediaIds(known), QSet<QByteArray>{identity('A')});
	// The file identity remains proven even if the requesting clip's track
	// number is not understood; uncertainty must still be reported.
	replaceField(s, 4, QStringLiteral("SourceClip.track_id"), 0);
	const auto unknown = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(!unknown.coverageComplete);
	QVERIFY(!unknown.issues.isEmpty());
	QCOMPARE(mediaIds(unknown), QSet<QByteArray>{identity('A')});
}

void TestMediaEngineAvbReferences::knownEmptyTrackSlotsDoNotAddCandidates_data()
{
	QTest::addColumn<bool>("nullComponent");
	QTest::newRow("no-component-recorded") << false;
	QTest::newRow("recorded-null-component") << true;
}

void TestMediaEngineAvbReferences::knownEmptyTrackSlotsDoNotAddCandidates()
{
	QFETCH(bool, nullComponent);
	auto s = sourceTrackGraph();
	replaceField(s, 5, QStringLiteral("TrackGroup.track_count"), quint32(4));
	const auto prefix = QStringLiteral("TrackGroup.tracks[3]");
	field(s, 5, prefix + QStringLiteral(".flags"), quint16(nullComponent ? 0x0005 : 0));
	if (nullComponent)
	{
		field(s, 5, prefix + QStringLiteral(".index"), 7);
		edge(s, 5, 0, prefix + QStringLiteral(".component"));
	}
	Cancellation cancel;
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('A')});
	QCOMPARE(result.mediaReferences.size(), 1);
}

void TestMediaEngineAvbReferences::cyclesKeepEdgesAndTerminate()
{
	auto s = listedItems();
	// Use an actual SourceClip identity loop, with another placed track
	// supplying proven media, rather than an invented locator-side pointer.
	replaceField(s, 4, QStringLiteral("SourceClip.mob_id"), identity('S'));
	for (auto &edge : s->relationships)
		if (edge.origin == 4 && edge.locator.name == QLatin1String("SourceClip.mob_id"))
			edge.recordedReference = identity('S');
	track(s, 2, 1, 2, 5);
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(result.mediaReferences.size(), 1);
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('B')});
	QVERIFY(hasEdge(result, 4, 2));
	QVERIFY(result.edges.size() < 20);
}

void TestMediaEngineAvbReferences::unresolvedAndNullReferences()
{
	auto s = listedItems();
	edge(s, 2, 0, QStringLiteral("Component.left_bob"));
	Cancellation cancel;
	AvbReferenceIndex nullIndex(s, cancel);
	QVERIFY(nullIndex.resolveReferences(cancel).coverageComplete);
	edge(s, 2, 99, QStringLiteral("Component.precomputed"));
	s->relationships.last().target = 0; // Reader retains the recorded invalid index.
	AvbReferenceIndex invalidIndex(s, cancel);
	const auto invalid = invalidIndex.resolveReferences(cancel);
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
	track(a, 2, 0, 1, 3);
	auto b = source();
	root(b);
	composition(b, 2, identity('A'));
	member(b, 2, 0, true);
	locator(b, 3, identity('A'));
	edge(b, 2, 3, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	AvbReferenceIndex first(a, cancel);
	const auto before = first.resolveReferences(cancel);
	QVERIFY(!before.coverageComplete);
	QVERIFY(hasIssue(before, Issue::UnresolvedReference));
	QVERIFY(before.mediaReferences.isEmpty());
	AvbReferenceIndex other(b, cancel);
	const auto independent = other.resolveReferences(cancel);
	QVERIFY(independent.coverageComplete);
	QCOMPARE(mediaIds(independent), QSet<QByteArray>{identity('A')});
	const auto after = first.resolveReferences(cancel);
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
	const auto nearMatch = AvbReferenceIndex(a, cancel).resolveReferences(cancel);
	QVERIFY(!nearMatch.coverageComplete);
	QVERIFY(hasIssue(nearMatch, Issue::UnresolvedReference));
	QVERIFY(nearMatch.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::wholeBinFollowsOnlyListedItems()
{
	auto s = listedItems(true);
	// Both the listed sequence and the listed master contribute their references.
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
	track(s, 12, 0, 1, 13);
	Cancellation cancel;
	for (const auto &classId : {QByteArray("ABIN"), QByteArray("BINF")})
	{
		auto context = QSharedPointer<AvbObjectContext>::create(*object(s, 1).avb);
		context->classId = classId;
		object(s, 1).avb = context;
		AvbReferenceIndex index(s, cancel);
		const auto result = index.resolveReferences(cancel);
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
	auto s = listedItems(true);
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
	const auto result = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
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
	const auto empty = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(empty.coverageComplete);
	QVERIFY(empty.roots.isEmpty() && empty.mediaReferences.isEmpty());
	member(s, 0, 0, false);
	const auto nullSlot = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(nullSlot.coverageComplete);
	QVERIFY(nullSlot.roots.isEmpty() && nullSlot.mediaReferences.isEmpty());
	object(s, 1).properties.clear();
	const auto unknown = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(!unknown.coverageComplete);
	QVERIFY(!unknown.issues.isEmpty());
	QVERIFY(unknown.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::duplicateIdentityIsAmbiguous()
{
	auto s = listedItems();
	composition(s, 10, identity('A'));
	locator(s, 11, identity('A'));
	edge(s, 10, 11, QStringLiteral("Composition.descriptor"));
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::AmbiguousReference));
	QVERIFY(result.mediaReferences.isEmpty());
	QVERIFY(!hasEdge(result, 4, 6));
	QVERIFY(!hasEdge(result, 4, 10));
	// Listing an object proves its own route independently of the ambiguous
	// source-clip link; it does not make that link uniquely resolvable.
	member(s, 3, 1, false);
	member(s, 6, 2, true);
	const auto whole = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(!whole.coverageComplete);
	QCOMPARE(mediaIds(whole), (QSet<QByteArray>{identity('A'), identity('B')}));
	QCOMPARE(whole.mediaReferences.size(), 2);
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
	track(s, 2, 0, 1, 3);
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(cancel);
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
	const auto changedResult = changedIndex.resolveReferences(cancel);
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
	track(s, 2, 0, 1, 3);
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::UnsupportedIdentity));
	QVERIFY(result.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::audioSuiteDependencies()
{
	auto s = listedItems();
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
	const auto result = index.resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(mediaIds(result), (QSet<QByteArray>{identity('A'), identity('B')}));
	QVERIFY(hasEdge(result, 10, 7));

	auto legacy = Source::create(*s);
	object(legacy, 10).properties.clear();
	legacy->relationships.removeLast();
	field(legacy, 10, QStringLiteral("AudioSuitePluginEffect.legacy_word0"), quint32(0x12345678));
	field(legacy, 10, QStringLiteral("AudioSuitePluginEffect.legacy_word1"), quint32(0x87654321));
	AvbReferenceIndex legacyIndex(legacy, cancel);
	const auto legacyResult = legacyIndex.resolveReferences(cancel);
	QVERIFY(!legacyResult.coverageComplete);
	QVERIFY(hasIssue(legacyResult, Issue::UnsupportedIdentity));
	QCOMPARE(mediaIds(legacyResult), QSet<QByteArray>{identity('A')});
}

void TestMediaEngineAvbReferences::terminalLocatorIsNotMedia()
{
	auto s = listedItems();
	for (auto &property : object(s, 8).properties)
		if (property.locator.name == QStringLiteral("MSMLocator.mob_id"))
			property.decoded = QByteArray::fromHex("060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80");
	Cancellation cancel;
	AvbReferenceIndex index(s, cancel);
	const auto result = index.resolveReferences(cancel);
	QVERIFY(result.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::incomplete_locator_cannot_assert_modern_identity_absent()
{
	auto s = listedItems();
	auto &value = object(s, 8);
	value.properties.clear();
	auto context = QSharedPointer<AvbObjectContext>::create(*value.avb);
	context->interpretationComplete = false;
	value.avb = context;
	field(s, 8, QStringLiteral("MSMLocator.legacy_word0"), quint32(0x12345678));
	field(s, 8, QStringLiteral("MSMLocator.legacy_word1"), quint32(0x87654321));
	Cancellation cancel;
	const auto partial = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
	QVERIFY(!partial.coverageComplete);
	QVERIFY(partial.mediaReferences.isEmpty());
	QVERIFY(hasIssue(partial, Issue::UnsupportedIdentity));
	// A typed ID that was read before a later failure is independent evidence.
	field(s, 8, QStringLiteral("MSMLocator.mob_id"), identity('A'));
	const auto known = AvbReferenceIndex(s, cancel).resolveReferences(cancel);
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
	auto s = listedItems();
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
	const auto result = index.resolveReferences(cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(!result.issues.isEmpty());
	QVERIFY(result.mediaReferences.isEmpty());
}

void TestMediaEngineAvbReferences::invalidSourceAndIncompleteEvidence()
{
	auto s = listedItems();
	Cancellation cancel;
	const auto unavailable = AvbReferenceIndex({}, cancel).resolveReferences(cancel);
	QVERIFY(!unavailable.coverageComplete);
	QVERIFY(hasIssue(unavailable, Issue::InvalidSource));
	s->outcome = ParsedSource::Outcome::Incomplete;
	s->diagnostics.append(QStringLiteral("A retained extension is unknown"));
	AvbReferenceIndex partial(s, cancel);
	const auto result = partial.resolveReferences(cancel);
	QVERIFY(!result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::IncompleteSource));
	QCOMPARE(mediaIds(result), QSet<QByteArray>{identity('A')});
}

void TestMediaEngineAvbReferences::cancellation()
{
	Cancellation initial;
	AvbReferenceIndex index(listedItems(), initial);
	Cancellation cancelled;
	cancelled.cancel();
	const auto result = index.resolveReferences(cancelled);
	QVERIFY(result.cancelled && !result.coverageComplete);
	QVERIFY(hasIssue(result, Issue::Cancelled));
	AvbReferenceIndex cancelledIndex(listedItems(), cancelled);
	const auto duringIndex = cancelledIndex.resolveReferences(initial);
	QVERIFY(!duringIndex.coverageComplete);
}

void TestMediaEngineAvbReferences::authoredBinaryGraph()
{
	TestAvb::Document document;
	document.objects = {
		{"ABIN", TestAvb::bin(false, {2, 4})},
		{"CMPO", TestAvb::composition(false, TestAvb::Master, "Listed sequence", 0, {3}, 0, true, 0, 1)},
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
	QCOMPARE(parsed->objects[1].role, AvidObject::Role::Composition);
	const auto result = index.resolveReferences(cancel);
	QVERIFY(result.coverageComplete);
	QCOMPARE(result.roots, (QVector<ObjectHandle>{2, 4}));
	QCOMPARE(mediaIds(result), QSet<QByteArray>{TestAvb::Source});
	QVERIFY(hasEdge(result, 3, 4));
	QCOMPARE(result.mediaReferences[0].objectHandle, ObjectHandle(5));
	QVERIFY(result.mediaReferences[0].propertyIndex >= 0);
}

QTEST_GUILESS_MAIN(TestMediaEngineAvbReferences)
#include "tst_mediaengineavbreferences.moc"
