// Independent object graphs establish ownership and arithmetic expectations.
// Genuine fixtures then exercise the fresh reader and projector together.

#include "canon/projection.h"
#include "canon/mdbreader.h"
#include "canon/legacyreader.h"
#include "testcanonbento.h"
#include "testtypedbento.h"

#include <QBuffer>
#include <QFile>
#include <QTest>
#include <algorithm>
#include <limits>

namespace
{
	using TestCanonBento::number;
	using TestCanonBento::TypedBento;

	QByteArray uid(quint32 value, bool big = false)
	{
		return number<quint32>(42, big) + number(value, big) + number<quint32>(900, big);
	}

	void object(TypedBento &writer, quint32 handle, const char *cls)
	{
		writer.add(handle, "OMFI:ObjID", "omfi:ObjectTag", cls, true);
	}

	void fileMob(TypedBento &writer, quint32 handle, quint32 descriptor, quint32 identity)
	{
		object(writer, handle, "MOBJ");
		writer.add(handle, "OMFI:MOBJ:MobID", "omfi:UID", uid(identity, writer.metadataBig));
		writer.add(handle, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(descriptor, 1));
		object(writer, descriptor, "CDCI");
		writer.add(descriptor, "OMFI:MDFL:SampleRate", "omfi:ExactEditRate", number<qint32>(25, writer.metadataBig) + number<qint32>(1, writer.metadataBig));
		writer.add(descriptor, "OMFI:MDFL:Length", "omfi:Length64", number<qint64>(250, writer.metadataBig));
		writer.add(descriptor, "OMFI:DIDD:StoredWidth", "omfi:UInt32", number<quint32>(1920, writer.metadataBig));
		writer.add(descriptor, "OMFI:DIDD:StoredHeight", "omfi:UInt32", number<quint32>(540, writer.metadataBig));
		writer.add(descriptor, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", number<qint16>(1, writer.metadataBig));
		writer.add(descriptor, "OMFI:CDCI:ComponentWidth", "omfi:UInt32", number<quint32>(10, writer.metadataBig));
	}

	void master(TypedBento &writer, quint32 handle, quint32 track, quint32 clip, quint32 identity, quint32 sourceIdentity, QByteArray name)
	{
		object(writer, handle, "MOBJ");
		writer.add(handle, "OMFI:MOBJ:MobID", "omfi:UID", uid(identity, writer.metadataBig));
		writer.add(handle, "OMFI:MOBJ:UsageCode", "omfi:UsageCodeType", number<qint32>(7, writer.metadataBig));
		writer.add(handle, "OMFI:CPNT:Name", "omfi:String", name + '\0');
		writer.add(handle, "OMFI:TRKG:Tracks", "omfi:ObjRefArray", number<quint16>(1, writer.metadataBig) + writer.reference(track, 1));
		object(writer, track, "TRAK");
		writer.add(track, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(clip, 1));
		object(writer, clip, "SCLP");
		writer.add(clip, "OMFI:SCLP:SourceID", "omfi:UID", uid(sourceIdentity, writer.metadataBig));
	}

	Canon::ParsedSource read(QByteArray bytes, bool database = true)
	{
		QBuffer input(&bytes);
		input.open(QIODevice::ReadOnly);
		Canon::Cancellation cancellation;
		return database ? Canon::MdbReader{}.read(input, {{}, cancellation}) : Canon::LegacyReader{}.read(input, {{}, cancellation});
	}

	Canon::Projection project(const Canon::ParsedSource &source)
	{
		Canon::Cancellation cancellation;
		return Canon::projectOmf(source, cancellation);
	}

	QVariant single(const Canon::ProjectedFile &file, MediaProperty field)
	{
		const auto &values = file.evidence.observations(field);
		return values.size() == 1 ? values.first().value : QVariant{};
	}

	QByteArray chunk(const QByteArray &key, const QByteArray &value)
	{
		return key + number(quint32(value.size())) + value + QByteArray(value.size() & 1, '\0');
	}

	QByteArray wave(quint16 format, quint16 bits)
	{
		const quint16 align = quint16(bits / 8);
		const auto fmt = number(format) + number<quint16>(1) + number<quint32>(48000) + number<quint32>(48000 * align) + number(align) + number(bits) + (format == 1 ? QByteArray{} : number<quint16>(0));
		const auto chunks = chunk("fmt ", fmt) + chunk("data", QByteArray(align * 2, '\0'));
		return QByteArray("RIFF") + number(quint32(chunks.size() + 4)) + "WAVE" + chunks;
	}
}

class TestCanonOmfProjection final : public QObject
{
	Q_OBJECT
private slots:
	void ownershipAndExactFacts();
	void masterOnlyAndIncompleteFileFacts();
	void omf2MasterIdentity();
	void physicalMediaIdentitySelectsOwner();
	void repeatedDescriptorsAndMastersRemainSeparate();
	void oppositeEndianUid();
	void repeatedFactsRemainConflicting();
	void visibleResolution_data();
	void visibleResolution();
	void verifiedProxyResolution_data();
	void verifiedProxyResolution();
	void nativeAudio();
	void rgbaAlphaEvidence();
	void uncompressedAlphaRequiresExplicitEvidence_data();
	void uncompressedAlphaRequiresExplicitEvidence();
	void inferredUtf8RetainsOriginalEvidence();
	void inferredMacRomanFallback();
	void emptyAndUnreadableTextRemainEvidence();
	void unreadableImportPathQualifiesDerivedFilename();
	void checkedAbsenceRequiresCompleteNamedObject();
	void failedTechnicalInputsRemainUnreadable();
	void nativeAudioReadCoverage();
	void originalBinTextPriority_data();
	void originalBinTextPriority();
	void associatedTimecodeAndClipDuration();
	void legacySequenceClipDuration_data();
	void legacySequenceClipDuration();
	void genuineDatabaseSequenceDurations();
	void genuineVideoCodecNames_data();
	void genuineVideoCodecNames();
	void exactDnxOperatingPoint();
	void legacyDnx220NamingIsExact();
	void legacyDecimalDnxNaming_data();
	void legacyDecimalDnxNaming();
	void legacyDecimalDnxNamingRejectsUnverifiedFacts();
	void conflictingEmbeddedIdentitiesRemainVisible();
	void dnxUncompressedFlavour_data();
	void dnxUncompressedFlavour();
	void unknownLayoutDoesNotGuessRaster();
	void genuineProjectWithoutPmr();
	void cancellation();
	void genuineFiles_data();
	void genuineFiles();
};

void TestCanonOmfProjection::ownershipAndExactFacts()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	master(writer, 301, 401, 501, 21, 11, "Recorded master");
	const auto source = read(writer.build());
	QCOMPARE(source.outcome, Canon::ParsedSource::Outcome::Complete);
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &file = result.files.first();
	QCOMPARE(file.fileMobId, Canon::canonicalDatabaseId(uid(11)));
	QCOMPARE(file.masterMobIds, QStringList{Canon::canonicalDatabaseId(uid(21))});
	QCOMPARE(single(file, MediaProperty::ClipName).toString(), QStringLiteral("Recorded master"));
	QCOMPARE(single(file, MediaProperty::Kind).toInt(), 0);
	QCOMPARE(single(file, MediaProperty::Type).toInt(), 0);
	QCOMPARE(single(file, MediaProperty::Resolution).toString(), QStringLiteral("1920x1080"));
	QCOMPARE(single(file, MediaProperty::BitDepth).toString(), QStringLiteral("10-bit"));
	const auto duration = Canon::mediaDuration(single(file, MediaProperty::FileDuration));
	QCOMPARE(duration.units, 250);
	QCOMPARE(duration.rate.numerator, 25);
	QCOMPARE(duration.rate.denominator, 1);
	QCOMPARE(file.objects.size(), 3);
	const auto &evidence = file.evidence.observations(MediaProperty::FileDuration).first();
	QCOMPARE(evidence.property, QStringLiteral("OMFI:MDFL:Length"));
	QCOMPARE(evidence.rawValue.toByteArray(), number<qint64>(250));
	QCOMPARE(evidence.snapshot, source.snapshot);
}

void TestCanonOmfProjection::masterOnlyAndIncompleteFileFacts()
{
	TypedBentoBuilder writer;
	const auto master = writer.addObject("MOBJ");
	writer.set(master, "OMFI:MOBJ:MobID", uid(21));
	writer.setU32(master, "OMFI:MOBJ:UsageCode", 7);
	writer.setString(master, "OMFI:CPNT:Name", "Master without file object");
	auto result = project(read(writer.build()));
	QVERIFY(result.files.isEmpty());
	QCOMPARE(result.masters.size(), 1);
	QCOMPARE(single(result.masters.first(), MediaProperty::ClipName).toString(), QStringLiteral("Master without file object"));
	const auto file = writer.addObject("MOBJ"), descriptor = writer.addObject("WAVD");
	writer.set(file, "OMFI:MOBJ:MobID", uid(11));
	writer.setHandle(file, "OMFI:MOBJ:PhysicalMedia", descriptor);
	result = project(read(writer.build(), false));
	QCOMPARE(result.files.size(), 1);
	QCOMPARE(result.files.first().fileMobId, Canon::canonicalDatabaseId(uid(11)));
	QVERIFY(result.files.first().evidence.observations(MediaProperty::FileDuration).isEmpty());
	QVERIFY(result.files.first().masterMobIds.isEmpty());
}

void TestCanonOmfProjection::omf2MasterIdentity()
{
	TypedBentoBuilder writer(true, false, 2);
	const auto master = writer.addObject("MMOB"), file = writer.addObject("SMOB"), descriptor = writer.addObject("WAVD");
	const auto track = writer.addObject("MSLT"), clip = writer.addObject("SCLP");
	writer.set(master, "OMFI:MOBJ:MobID", uid(21));
	writer.set(file, "OMFI:MOBJ:MobID", uid(11));
	writer.setHandle(file, "OMFI:SMOB:MediaDescription", descriptor);
	writer.setHandles(master, "OMFI:MOBJ:Slots", {track});
	writer.setHandle(track, "OMFI:MSLT:Segment", clip);
	writer.set(clip, "OMFI:SCLP:SourceID", uid(11));
	const auto result = project(read(writer.build(), false));
	QCOMPARE(result.files.size(), 1);
	QCOMPARE(result.files.first().masterMobIds, QStringList{Canon::canonicalDatabaseId(uid(21))});
	QVERIFY(result.files.first().evidence.observations(MediaProperty::Type).isEmpty());
}

void TestCanonOmfProjection::physicalMediaIdentitySelectsOwner()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	fileMob(writer, 102, 202, 12);
	object(writer, 601, "IDAT");
	writer.add(601, "OMFI:MDAT:MobID", "omfi:UID", uid(12));
	const auto selected = project(read(writer.build(), false));
	QCOMPARE(selected.files.size(), 1);
	QCOMPARE(selected.files.first().fileMobId, Canon::canonicalDatabaseId(uid(12)));
	writer.add(601, "OMFI:MDAT:MobID", "omfi:UID", uid(11));
	const auto ambiguous = project(read(writer.build(), false));
	QCOMPARE(ambiguous.files.size(), 1);
	QVERIFY(ambiguous.files.first().fileMobId.isEmpty());
	const auto &evidence = ambiguous.files.first().evidence;
	const auto selectedId = evidence.resolve(MediaProperty::FileMobId, [](MetadataSource)
											 { return 1; }, {});
	QCOMPARE(selectedId.agreement, PropertyAgreement::Conflicting);
	QVERIFY(!selectedId.value.isValid());
	for (const auto &value : evidence.observations(MediaProperty::Kind))
		QVERIFY(!value.eligible);
	QVERIFY(!ambiguous.diagnostics.isEmpty());
}

void TestCanonOmfProjection::repeatedDescriptorsAndMastersRemainSeparate()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	fileMob(writer, 102, 202, 11);
	master(writer, 301, 401, 501, 21, 11, "First master");
	master(writer, 302, 402, 502, 22, 11, "Second master");
	const auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 2);
	for (const auto &file : result.files)
	{
		QCOMPARE(file.masterMobIds.size(), 2);
		QCOMPARE(file.evidence.observations(MediaProperty::ClipName).size(), 2);
	}
	QVERIFY(result.files[0].objects.first().handle != result.files[1].objects.first().handle);
}

void TestCanonOmfProjection::oppositeEndianUid()
{
	TypedBento writer;
	writer.major = 2;
	writer.containerBig = false;
	writer.metadataBig = true;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	master(writer, 301, 401, 501, 21, 11, "Big endian");
	const auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 1);
	QCOMPARE(result.files.first().fileMobId, Canon::canonicalDatabaseId(uid(11)));
	QCOMPARE(result.files.first().masterMobIds, QStringList{Canon::canonicalDatabaseId(uid(21))});
	QCOMPARE(single(result.files.first(), MediaProperty::Resolution).toString(), QStringLiteral("1920x1080"));
}

void TestCanonOmfProjection::repeatedFactsRemainConflicting()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	writer.add(201, "OMFI:CDCI:ComponentWidth", "omfi:UInt32", number<quint32>(12));
	writer.add(201, "OMFI:DIDD:StoredWidth", "omfi:UInt32", number<quint32>(1280));
	const auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 1);
	QCOMPARE(result.files.first().evidence.observations(MediaProperty::BitDepth).size(), 2);
	QVERIFY(result.files.first().evidence.observations(MediaProperty::Resolution).isEmpty());
}

void TestCanonOmfProjection::visibleResolution_data()
{
	QTest::addColumn<quint32>("width");
	QTest::addColumn<quint32>("height");
	QTest::addColumn<qint16>("layout");
	QTest::addColumn<quint32>("displayWidth");
	QTest::addColumn<quint32>("displayHeight");
	QTest::addColumn<QString>("condition");
	QTest::addColumn<QString>("expected");
	QTest::newRow("absent-optional-rectangles") << quint32(1920) << quint32(540) << qint16(1)
		<< quint32(0) << quint32(0) << QString{} << QStringLiteral("1920x1080");
	QTest::newRow("small-file-without-proxy-guess") << quint32(480) << quint32(270) << qint16(0)
		<< quint32(0) << quint32(0) << QString{} << QStringLiteral("480x270");
	QTest::newRow("recorded-display-crop") << quint32(1920) << quint32(1088) << qint16(0)
		<< quint32(1920) << quint32(1080) << QString{} << QStringLiteral("1920x1080");
	QTest::newRow("padding-without-crop-is-retained") << quint32(1920) << quint32(1088) << qint16(0)
		<< quint32(0) << quint32(0) << QString{} << QStringLiteral("1920x1088");
	QTest::newRow("separate-fields-crop") << quint32(1920) << quint32(544) << qint16(1)
		<< quint32(1920) << quint32(540) << QString{} << QStringLiteral("1920x1080");
	QTest::newRow("omf1-mixed-fields") << quint32(1920) << quint32(544) << qint16(3)
		<< quint32(1920) << quint32(540) << QString{} << QStringLiteral("1920x1080");
	QTest::newRow("single-field-is-not-doubled") << quint32(720) << quint32(248) << qint16(2)
		<< quint32(720) << quint32(243) << QStringLiteral("bottom-crop") << QStringLiteral("720x243");
	QTest::newRow("jfif-field-crop") << quint32(720) << quint32(248) << qint16(1)
		<< quint32(720) << quint32(243) << QStringLiteral("bottom-crop") << QStringLiteral("720x486");
	// Unlike MXF, OMF measures Display directly from Stored, not from Sampled.
	QTest::newRow("display-outside-sampled") << quint32(1920) << quint32(1080) << qint16(0)
		<< quint32(1920) << quint32(1080) << QStringLiteral("small-sampled") << QStringLiteral("1920x1080");
	QTest::newRow("sampled-does-not-replace-display-default") << quint32(1920) << quint32(1080) << qint16(0)
		<< quint32(0) << quint32(0) << QStringLiteral("small-sampled") << QStringLiteral("1920x1080");
	QTest::newRow("legacy-partial-display-default") << quint32(1920) << quint32(1080) << qint16(0)
		<< quint32(1280) << quint32(0) << QString{} << QStringLiteral("1280x1080");
	QTest::newRow("unexplained-proxy-sized-storage") << quint32(480) << quint32(270) << qint16(0)
		<< quint32(1920) << quint32(1080) << QString{} << QString{};
	QTest::newRow("missing-width") << quint32(1920) << quint32(1088) << qint16(0)
		<< quint32(1920) << quint32(1080) << QStringLiteral("missing-width") << QString{};
	QTest::newRow("missing-height") << quint32(1920) << quint32(1088) << qint16(0)
		<< quint32(1920) << quint32(1080) << QStringLiteral("missing-height") << QString{};
	QTest::newRow("unreadable-width") << quint32(1920) << quint32(1088) << qint16(0)
		<< quint32(1920) << quint32(1080) << QStringLiteral("unreadable-width") << QString{};
	QTest::newRow("conflicting-width") << quint32(1920) << quint32(1088) << qint16(0)
		<< quint32(1920) << quint32(1080) << QStringLiteral("conflicting-width") << QString{};
	QTest::newRow("zero-width") << quint32(0) << quint32(1088) << qint16(0)
		<< quint32(1920) << quint32(1080) << QString{} << QString{};
	for (const auto *condition : {"unreadable-offset", "conflicting-offset", "negative-offset",
		"right-edge-outside", "bottom-edge-outside"})
		QTest::newRow(condition) << quint32(1920) << quint32(1088) << qint16(0)
			<< quint32(1920) << quint32(1080) << QString::fromLatin1(condition) << QString{};
	QTest::newRow("offset-plus-width-must-not-wrap") << std::numeric_limits<quint32>::max()
		<< quint32(1088) << qint16(0) << std::numeric_limits<quint32>::max()
		<< quint32(1080) << QStringLiteral("large-offset") << QString{};
}

void TestCanonOmfProjection::visibleResolution()
{
	QFETCH(quint32, width);
	QFETCH(quint32, height);
	QFETCH(qint16, layout);
	QFETCH(quint32, displayWidth);
	QFETCH(quint32, displayHeight);
	QFETCH(QString, condition);
	QFETCH(QString, expected);
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	writer.entries.removeIf([&](const TypedBento::Entry &entry)
		{ return entry.object == 201 &&
			(entry.property == writer.property("OMFI:DIDD:StoredWidth") ||
			 entry.property == writer.property("OMFI:DIDD:StoredHeight") ||
			 entry.property == writer.property("OMFI:DIDD:FrameLayout")); });
	if (condition != QLatin1String("missing-width"))
		writer.add(201, "OMFI:DIDD:StoredWidth", "omfi:UInt32",
			condition == QLatin1String("unreadable-width") ? QByteArray(3, '\0') : number(width));
	if (condition != QLatin1String("missing-height"))
		writer.add(201, "OMFI:DIDD:StoredHeight", "omfi:UInt32", number(height));
	if (condition == QLatin1String("conflicting-width"))
		writer.add(201, "OMFI:DIDD:StoredWidth", "omfi:UInt32", number<quint32>(320));
	writer.add(201, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", number(layout));
	if (displayWidth)
		writer.add(201, "OMFI:DIDD:DisplayWidth", "omfi:UInt32", number(displayWidth));
	if (displayHeight)
		writer.add(201, "OMFI:DIDD:DisplayHeight", "omfi:UInt32", number(displayHeight));
	if (condition == QLatin1String("small-sampled"))
	{
		writer.add(201, "OMFI:DIDD:SampledWidth", "omfi:UInt32", number<quint32>(1280));
		writer.add(201, "OMFI:DIDD:SampledHeight", "omfi:UInt32", number<quint32>(720));
	}
	if (condition == QLatin1String("bottom-crop"))
		writer.add(201, "OMFI:DIDD:DisplayYOffset", "omfi:Int32", number<qint32>(5));
	else if (condition == QLatin1String("bottom-edge-outside"))
		writer.add(201, "OMFI:DIDD:DisplayYOffset", "omfi:Int32", number<qint32>(9));
	else if (condition == QLatin1String("unreadable-offset"))
		writer.add(201, "OMFI:DIDD:DisplayXOffset", "omfi:Int32", QByteArray(3, '\0'));
	else if (condition == QLatin1String("conflicting-offset"))
	{
		writer.add(201, "OMFI:DIDD:DisplayXOffset", "omfi:Int32", number<qint32>(0));
		writer.add(201, "OMFI:DIDD:DisplayXOffset", "omfi:Int32", number<qint32>(1));
	}
	else if (condition == QLatin1String("negative-offset") ||
		condition == QLatin1String("right-edge-outside") || condition == QLatin1String("large-offset"))
	{
		const qint32 offset = condition == QLatin1String("negative-offset") ? -1
			: condition == QLatin1String("large-offset") ? std::numeric_limits<qint32>::max() : 1;
		writer.add(201, "OMFI:DIDD:DisplayXOffset", "omfi:Int32", number(offset));
	}
	for (const bool database : {false, true})
	{
		const auto source = read(writer.build(), database);
		const auto result = project(source);
		QCOMPARE(result.files.size(), 1);
		const auto &observations = result.files.first().evidence.observations(MediaProperty::Resolution);
		if (expected.isEmpty())
			QVERIFY(observations.isEmpty());
		else
		{
			QCOMPARE(observations.size(), 1);
			const auto &observation = observations.first();
			QCOMPARE(observation.value.toString(), expected);
			QCOMPARE(observation.property, displayWidth ? QStringLiteral("OMFI:DIDD:DisplayWidth")
				: QStringLiteral("OMFI:DIDD:StoredWidth"));
			QCOMPARE(observation.rawValue.toByteArray(), number(displayWidth ? displayWidth : width));
			QCOMPARE(observation.snapshot, source.snapshot);
			QCOMPARE(observation.basis, EvidenceBasis::Derived);
		}
		// Projection must not replace original rectangles or discard their byte locations.
		for (const auto &object : source.objects)
			for (const auto &property : object.properties)
				if (property.locator.name.startsWith(QLatin1String("OMFI:DIDD:")))
				{
					QVERIFY(!property.locator.ranges.isEmpty());
					const auto id = writer.properties.value(property.locator.name.toLatin1());
					QVERIFY(std::any_of(writer.entries.cbegin(), writer.entries.cend(),
						[&](const TypedBento::Entry &entry)
						{ return entry.object == 201 && entry.property == id &&
							entry.bytes == property.encoding; }));
				}
	}
}

void TestCanonOmfProjection::verifiedProxyResolution_data()
{
	QTest::addColumn<quint32>("resolution");
	QTest::addColumn<quint32>("width");
	QTest::addColumn<quint32>("height");
	QTest::addColumn<quint32>("displayWidth");
	QTest::addColumn<quint32>("displayHeight");
	QTest::addColumn<qint16>("layout");
	QTest::addColumn<QString>("alteration");
	QTest::addColumn<bool>("accepted");
	QTest::newRow("1080-single-field") << quint32(3472) << quint32(480) << quint32(270)
		<< quint32(1920) << quint32(540) << qint16(2) << QString{} << true;
	QTest::newRow("1080-progressive-3484") << quint32(3484) << quint32(480) << quint32(270)
		<< quint32(1920) << quint32(1080) << qint16(0) << QString{} << true;
	QTest::newRow("1080-progressive-3487") << quint32(3487) << quint32(480) << quint32(270)
		<< quint32(1920) << quint32(1080) << qint16(0) << QString{} << true;
	QTest::newRow("720-progressive") << quint32(3488) << quint32(320) << quint32(180)
		<< quint32(1280) << quint32(720) << qint16(0) << QString{} << true;
	QTest::newRow("ntsc-single-field-3470") << quint32(3470) << quint32(352) << quint32(240)
		<< quint32(720) << quint32(240) << qint16(2) << QString{} << true;
	QTest::newRow("ntsc-single-field-3491") << quint32(3491) << quint32(352) << quint32(240)
		<< quint32(720) << quint32(240) << qint16(2) << QString{} << true;
	QTest::newRow("pal-progressive") << quint32(3483) << quint32(352) << quint32(288)
		<< quint32(720) << quint32(576) << qint16(0) << QString{} << true;
	for (const auto *alteration : {"unknown-id", "no-id", "other-coding", "no-coding", "stored-width",
		"sampled-width", "display-height", "other-layout", "display-offset", "sampled-offset"})
		QTest::newRow(alteration) << quint32(3484) << quint32(480) << quint32(270)
			<< quint32(1920) << quint32(1080) << qint16(0) << QString::fromLatin1(alteration) << false;
}

void TestCanonOmfProjection::verifiedProxyResolution()
{
	QFETCH(quint32, resolution);
	QFETCH(quint32, width);
	QFETCH(quint32, height);
	QFETCH(quint32, displayWidth);
	QFETCH(quint32, displayHeight);
	QFETCH(qint16, layout);
	QFETCH(QString, alteration);
	QFETCH(bool, accepted);
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	writer.entries.removeIf([&](const TypedBento::Entry &entry)
		{ return entry.object == 201 &&
			(entry.property == writer.property("OMFI:DIDD:StoredWidth") ||
			 entry.property == writer.property("OMFI:DIDD:StoredHeight") ||
			 entry.property == writer.property("OMFI:DIDD:FrameLayout")); });
	writer.add(201, "OMFI:DIDD:StoredWidth", "omfi:UInt32",
		number(alteration == QLatin1String("stored-width") ? width + 1 : width));
	writer.add(201, "OMFI:DIDD:StoredHeight", "omfi:UInt32", number(height));
	writer.add(201, "OMFI:DIDD:FrameLayout", "omfi:LayoutType",
		number(alteration == QLatin1String("other-layout") ? qint16(2) : layout));
	writer.add(201, "OMFI:DIDD:SampledWidth", "omfi:UInt32",
		number(alteration == QLatin1String("sampled-width") ? displayWidth + 1 : displayWidth));
	writer.add(201, "OMFI:DIDD:SampledHeight", "omfi:UInt32", number(displayHeight));
	writer.add(201, "OMFI:DIDD:DisplayWidth", "omfi:UInt32", number(displayWidth));
	writer.add(201, "OMFI:DIDD:DisplayHeight", "omfi:UInt32",
		number(alteration == QLatin1String("display-height") ? displayHeight + 1 : displayHeight));
	for (const auto *prefix : {"Sampled", "Display"})
	{
		const QByteArray base = QByteArray("OMFI:DIDD:") + prefix;
		const bool changed = (prefix == QByteArray("Sampled") && alteration == QLatin1String("sampled-offset")) ||
			(prefix == QByteArray("Display") && alteration == QLatin1String("display-offset"));
		writer.add(201, base + "XOffset", "omfi:Int32", number<qint32>(changed ? 1 : 0));
		writer.add(201, base + "YOffset", "omfi:Int32", number<qint32>(0));
	}
	if (alteration != QLatin1String("no-id"))
		writer.add(201, "OMFI:DIDD:DIDResolutionID", "omfi:UInt32",
			number(alteration == QLatin1String("unknown-id") ? quint32(3485) : resolution));
	if (alteration != QLatin1String("no-coding"))
	{
		const auto label = QByteArray::fromHex(alteration == QLatin1String("other-coding")
			? "060e2b340401010d0401020201311102" : "060e2b340401010d0401020201311101");
		// AUID's integer fields use metadata byte order; the UL's first eight octets follow them.
		const auto auid = number(qFromBigEndian<quint32>(label.constData() + 8)) +
			number(qFromBigEndian<quint16>(label.constData() + 12)) +
			number(qFromBigEndian<quint16>(label.constData() + 14)) + label.first(8);
		writer.add(201, "OMFI:DIDD:EssenceCompression", "omfi:UID", auid);
	}
	for (const bool database : {false, true})
	{
		const auto source = read(writer.build(), database);
		const auto result = project(source);
		QCOMPARE(result.files.size(), 1);
		const auto &observations = result.files.first().evidence.observations(MediaProperty::Resolution);
		if (!accepted)
			QVERIFY(observations.isEmpty());
		else
		{
			QCOMPARE(observations.size(), 1);
			const auto &observation = observations.first();
			QCOMPARE(observation.value.toString(), QStringLiteral("%1x%2").arg(width).arg(height));
			QCOMPARE(observation.property, QStringLiteral("OMFI:DIDD:StoredWidth"));
			QCOMPARE(observation.rawValue.toByteArray(), number(width));
			QCOMPARE(observation.basis, EvidenceBasis::Derived);
			QVERIFY(observation.explanation.contains(QLatin1String("qualified inference")));
		}
	}
}

void TestCanonOmfProjection::nativeAudio()
{
	for (const quint16 format : {quint16(1), quint16(3)})
	{
		const auto result = project(read(wave(format, format == 1 ? 24 : 32), false));
		QCOMPARE(result.files.size(), 1);
		const auto &file = result.files.first();
		QVERIFY(file.fileMobId.isEmpty());
		QCOMPARE(single(file, MediaProperty::Kind).toInt(), 1);
		QCOMPARE(single(file, MediaProperty::BitDepth).toString(), format == 1 ? QStringLiteral("24-bit") : QStringLiteral("32-bit"));
		QCOMPARE(single(file, MediaProperty::SampleFormat).toString(), format == 1 ? QStringLiteral("Integer") : QStringLiteral("Float"));
		QCOMPARE(Canon::mediaRate(single(file, MediaProperty::SampleRate)).numerator, 48000);
		QCOMPARE(Canon::mediaDuration(single(file, MediaProperty::FileDuration)).units, 2);
		QVERIFY(file.evidence.observations(MediaProperty::FrameRate).isEmpty());
	}
}

void TestCanonOmfProjection::rgbaAlphaEvidence()
{
	TypedBento writer;
	writer.head(1);
	object(writer, 101, "MOBJ");
	writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", uid(11));
	writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
	object(writer, 201, "RGBA");
	writer.add(201, "OMFI:RGBA:PixelLayout", "omfi:CompCodeArray", QByteArray("A\0", 2));
	writer.add(201, "OMFI:RGBA:PixelStructure", "omfi:CompSizeArray", QByteArray("\x08\0", 2));
	const auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 1);
	QCOMPARE(single(result.files.first(), MediaProperty::Alpha).toBool(), true);
	QCOMPARE(single(result.files.first(), MediaProperty::BitDepth).toString(), QStringLiteral("8-bit"));
}

void TestCanonOmfProjection::uncompressedAlphaRequiresExplicitEvidence_data()
{
	QTest::addColumn<QByteArray>("compression");
	QTest::addColumn<QByteArray>("pixels");
	QTest::addColumn<QByteArray>("depths");
	QTest::addColumn<bool>("named");
	QTest::newRow("alpha-8") << QByteArray("NONE\0", 5) << QByteArray("A\0", 2) << QByteArray("\x08\0", 2) << true;
	QTest::newRow("alpha-16") << QByteArray("NONE") << QByteArray("A") << QByteArray("\x10", 1) << true;
	QTest::newRow("compression-absent") << QByteArray{} << QByteArray("A") << QByteArray("\x08", 1) << false;
	QTest::newRow("compressed-alpha") << QByteArray("JPEG") << QByteArray("A") << QByteArray("\x08", 1) << false;
	QTest::newRow("colour-and-alpha") << QByteArray("NONE") << QByteArray("RGBA") << QByteArray(4, '\x08') << false;
	QTest::newRow("invalid-depth") << QByteArray("NONE") << QByteArray("A") << QByteArray("\xff", 1) << false;
}

void TestCanonOmfProjection::uncompressedAlphaRequiresExplicitEvidence()
{
	QFETCH(QByteArray, compression);
	QFETCH(QByteArray, pixels);
	QFETCH(QByteArray, depths);
	QFETCH(bool, named);
	TypedBento writer;
	writer.head(1);
	object(writer, 101, "MOBJ");
	writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", uid(11));
	writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
	object(writer, 201, "RGBA");
	writer.add(201, "OMFI:RGBA:PixelLayout", "omfi:CompCodeArray", pixels);
	writer.add(201, "OMFI:RGBA:PixelStructure", "omfi:CompSizeArray", depths);
	if (!compression.isEmpty())
		writer.add(201, "OMFI:DIDD:Compression", "omfi:String", compression);
	const auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 1);
	const auto codec = single(result.files.first(), MediaProperty::Codec);
	QCOMPARE(codec.toString(), named ? QStringLiteral("Uncompressed alpha") : QString{});
	if (named)
	{
		const auto &observation = result.files.first().evidence.observations(MediaProperty::Codec).first();
		QCOMPARE(observation.rawValue.toByteArray(), compression);
		QCOMPARE(observation.property, QStringLiteral("OMFI:DIDD:Compression"));
	}
}

void TestCanonOmfProjection::inferredUtf8RetainsOriginalEvidence()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	const auto bytes = QByteArray::fromHex("5465c39f74206e616d65");
	master(writer, 301, 401, 501, 21, 11, bytes);
	const auto source = read(writer.build());
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &observations = result.files.first().evidence.observations(MediaProperty::ClipName);
	QCOMPARE(observations.size(), 1);
	const auto &value = observations.first();
	QCOMPARE(value.value.toString(), QString::fromUtf8(bytes));
	QCOMPARE(value.rawValue.toByteArray(), bytes + '\0');
	QCOMPARE(value.textEncoding, Canon::TextEncoding::Utf8);
	QCOMPARE(value.textEncodingBasis, EvidenceBasis::Derived);
	QCOMPARE(value.basis, EvidenceBasis::Derived);
	QVERIFY(value.explanation.contains(QStringLiteral("inferred")));
	bool foundRaw = false;
	for (const auto &object : source.objects)
		for (const auto &property : object.properties)
			if (property.locator.name == QLatin1String("OMFI:CPNT:Name") && property.encoding == bytes + '\0')
			{
				foundRaw = true;
				QCOMPARE(property.textEncoding, Canon::TextEncoding::Unknown);
				QVERIFY(!property.decoded.isValid());
			}
	QVERIFY(foundRaw);
}

void TestCanonOmfProjection::inferredMacRomanFallback()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	master(writer, 301, 401, 501, 21, 11, QByteArray::fromHex("8ea0ff"));
	const auto source = read(writer.build());
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &observations = result.files.first().evidence.observations(MediaProperty::ClipName);
	QCOMPARE(observations.size(), 1);
	QCOMPARE(observations.first().textEncoding, Canon::TextEncoding::MacRoman);
	QCOMPARE(observations.first().textEncodingBasis, EvidenceBasis::Derived);
	QCOMPARE(observations.first().basis, EvidenceBasis::Derived);
	QCOMPARE(observations.first().rawValue.toByteArray(), QByteArray::fromHex("8ea0ff00"));
	QVERIFY(!observations.first().value.toString().isEmpty());
}

void TestCanonOmfProjection::emptyAndUnreadableTextRemainEvidence()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	master(writer, 301, 401, 501, 21, 11, {});
	auto source = read(writer.build());
	auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &empty = result.files.first().evidence.observations(MediaProperty::ClipName);
	QCOMPARE(empty.size(), 1);
	QCOMPARE(empty.first().readState, PropertyReadState::Present);
	QCOMPARE(empty.first().value.toString(), QString{});
	QCOMPARE(empty.first().rawValue.toByteArray(), QByteArray(1, '\0'));
	QCOMPARE(result.files.first().evidence.readStatus(MediaProperty::ClipName, source.snapshot,
		QStringLiteral("object:301")).state, PropertyReadState::Present);
	QVERIFY(!result.files.first().evidence.resolve(MediaProperty::ClipName, [](MetadataSource)
		{ return 1; }, QStringLiteral("Test priority")).value.isValid());
	for (auto &object : source.objects)
		for (auto &property : object.properties)
			if (object.handle == 301 && property.locator.name == QLatin1String("OMFI:CPNT:Name"))
			{
				property.state = PropertyReadState::Unreadable;
				property.decoded.clear();
			}
	result = project(source);
	const auto &damaged = result.files.first().evidence.observations(MediaProperty::ClipName);
	QCOMPARE(damaged.size(), 1);
	QCOMPARE(damaged.first().readState, PropertyReadState::Unreadable);
	QCOMPARE(damaged.first().rawValue.toByteArray(), QByteArray(1, '\0'));
	QCOMPARE(result.files.first().evidence.readStatus(MediaProperty::ClipName, source.snapshot,
		QStringLiteral("object:301")).reason, PropertyReadReason::ValueUnreadable);
}

void TestCanonOmfProjection::unreadableImportPathQualifiesDerivedFilename()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	object(writer, 601, "ATTR");
	object(writer, 602, "ATTB");
	writer.add(101, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(601, 1));
	writer.add(601, "OMFI:ATTR:AttrRefs", "omfi:ObjRefArray", number<quint16>(1) + writer.reference(602, 1));
	writer.add(602, "OMFI:ATTB:Name", "omfi:String", QByteArray("UNC Path\0", 9));
	writer.add(602, "OMFI:ATTB:Kind", "omfi:AttrKind", number<qint16>(2));
	writer.add(602, "OMFI:ATTB:StringAttribute", "omfi:String", QByteArray("/source/clip.mov\0", 17));
	auto source = read(writer.build());
	for (auto &owner : source.objects)
		for (auto &property : owner.properties)
			if (property.locator.name == QLatin1String("OMFI:ATTB:StringAttribute"))
			{
				property.state = PropertyReadState::Unreadable;
				property.decoded.clear();
			}
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &evidence = result.files.first().evidence;
	QCOMPARE(evidence.observations(MediaProperty::SourcePath).size(), 1);
	QCOMPARE(evidence.observations(MediaProperty::SourcePath).first().readState, PropertyReadState::Unreadable);
	QVERIFY(evidence.observations(MediaProperty::SourceFilename).isEmpty());
	QCOMPARE(evidence.readStatus(MediaProperty::SourceFilename, source.snapshot,
		QStringLiteral("object:602")).state, PropertyReadState::Unreadable);
	QCOMPARE(evidence.readStatus(MediaProperty::SourceFilename, source.snapshot,
		QStringLiteral("object:602")).reason, PropertyReadReason::ValueUnreadable);
}

void TestCanonOmfProjection::checkedAbsenceRequiresCompleteNamedObject()
{
	TypedBento writer;
	writer.head(1);
	object(writer, 101, "MOBJ");
	writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", uid(11));
	writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
	object(writer, 201, "WAVD");
	for (const auto &variation : {QStringLiteral("complete"), QStringLiteral("incomplete"), QStringLiteral("unnamed")})
	{
		auto source = read(writer.build());
		QCOMPARE(source.outcome, Canon::ParsedSource::Outcome::Complete);
		if (variation == QLatin1String("incomplete"))
			source.outcome = Canon::ParsedSource::Outcome::Incomplete;
		else if (variation == QLatin1String("unnamed"))
			for (auto &owner : source.objects)
				if (owner.handle == 201)
				{
					auto unknown = owner.properties.first();
					unknown.locator.name.clear();
					owner.properties.append(std::move(unknown));
				}
		const auto result = project(source);
		QCOMPARE(result.files.size(), 1);
		const auto &evidence = result.files.first().evidence;
		QVERIFY(evidence.observations(MediaProperty::Channels).isEmpty());
		const auto status = evidence.readStatus(MediaProperty::Channels, source.snapshot, QStringLiteral("object:201"));
		QCOMPARE(status.state, variation == QLatin1String("complete")
			? PropertyReadState::Absent : PropertyReadState::NotRead);
		QCOMPARE(status.reason, variation == QLatin1String("complete")
			? PropertyReadReason::NotPresentInObject : PropertyReadReason::CoverageNotEstablished);
	}
}

void TestCanonOmfProjection::failedTechnicalInputsRemainUnreadable()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	auto source = read(writer.build());
	for (auto &owner : source.objects)
		for (auto &property : owner.properties)
			if (owner.handle == 201 && (property.locator.name == QLatin1String("OMFI:MDFL:SampleRate") ||
				property.locator.name == QLatin1String("OMFI:CDCI:ComponentWidth") ||
				property.locator.name == QLatin1String("OMFI:DIDD:StoredWidth")))
			{
				property.state = PropertyReadState::Unreadable;
				property.decoded.clear();
			}
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &evidence = result.files.first().evidence;
	for (const auto field : {MediaProperty::FrameRate, MediaProperty::FileDuration, MediaProperty::ComponentDepth,
		MediaProperty::BitDepth, MediaProperty::Resolution})
	{
		QVERIFY(evidence.observations(field).isEmpty());
		QCOMPARE(evidence.readStatus(field, source.snapshot, QStringLiteral("object:201")).state,
			PropertyReadState::Unreadable);
	}
	QCOMPARE(evidence.readStatus(MediaProperty::Codec, source.snapshot, QStringLiteral("object:201")).state,
		PropertyReadState::NotRead);
	QCOMPARE(evidence.readStatus(MediaProperty::Codec, source.snapshot, QStringLiteral("object:201")).reason,
		PropertyReadReason::UnsupportedInterpretation);
}

void TestCanonOmfProjection::nativeAudioReadCoverage()
{
	const auto malformedChunks = chunk("fmt ", QByteArray(3, 'x')) + chunk("data", QByteArray(2, '\0'));
	const auto malformed = read(QByteArray("RIFF") + number(quint32(malformedChunks.size() + 4)) + "WAVE" + malformedChunks, false);
	const auto malformedResult = project(malformed);
	QCOMPARE(malformedResult.files.size(), 1);
	const auto &evidence = malformedResult.files.first().evidence;
	for (const auto field : {MediaProperty::Codec, MediaProperty::SampleRate, MediaProperty::BitDepth})
	{
		QVERIFY(evidence.observations(field).isEmpty());
		QCOMPARE(evidence.readStatus(field, malformed.snapshot, QStringLiteral("object:0")).state,
			PropertyReadState::Unreadable);
	}
	const auto unsupported = read(wave(6, 16), false);
	const auto unsupportedResult = project(unsupported);
	QCOMPARE(unsupportedResult.files.size(), 1);
	const auto &unsupportedEvidence = unsupportedResult.files.first().evidence;
	QCOMPARE(unsupportedEvidence.readStatus(MediaProperty::SampleRate, unsupported.snapshot,
		QStringLiteral("object:0")).state, PropertyReadState::Present);
	QCOMPARE(unsupportedEvidence.readStatus(MediaProperty::Codec, unsupported.snapshot,
		QStringLiteral("object:0")).state, PropertyReadState::NotRead);
	QCOMPARE(unsupportedEvidence.readStatus(MediaProperty::Codec, unsupported.snapshot,
		QStringLiteral("object:0")).reason, PropertyReadReason::UnsupportedInterpretation);
}

void TestCanonOmfProjection::originalBinTextPriority_data()
{
	QTest::addColumn<QByteArrayList>("modernNames");
	QTest::addColumn<QString>("expected");
	QTest::addColumn<bool>("modernSelected");
	QTest::newRow("legacy-only") << QByteArrayList{} << QStringLiteral("café originals") << false;
	QTest::newRow("empty-modern") << QByteArrayList{QByteArray{}} << QStringLiteral("café originals") << false;
	QTest::newRow("unreadable-modern") << QByteArrayList{QByteArray::fromHex("c328")} << QStringLiteral("café originals") << false;
	QTest::newRow("usable-modern") << QByteArrayList{QByteArray("Modern bin")} << QStringLiteral("Modern bin") << true;
	QTest::newRow("conflicting-modern") << QByteArrayList{QByteArray("First"), QByteArray("Second")} << QString{} << true;
}

void TestCanonOmfProjection::originalBinTextPriority()
{
	QFETCH(QByteArrayList, modernNames);
	QFETCH(QString, expected);
	QFETCH(bool, modernSelected);
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	master(writer, 301, 401, 501, 21, 11, "Clip");
	object(writer, 601, "ATTR");
	object(writer, 602, "ATTB");
	object(writer, 603, "MCBR");
	writer.add(301, "OMFI:CPNT:Attributes", "omfi:ObjRef", writer.reference(601, 1));
	writer.add(601, "OMFI:ATTR:AttrRefs", "omfi:ObjRefArray", number<quint16>(1) + writer.reference(602, 1));
	writer.add(602, "OMFI:ATTB:Name", "omfi:String", QByteArray("_ORG_BIN\0", 9));
	writer.add(602, "OMFI:ATTB:Kind", "omfi:AttrKind", number<qint16>(3));
	writer.add(602, "OMFI:ATTB:ObjAttribute", "omfi:ObjRef", writer.reference(603, 1));
	writer.add(603, "OMFI:MCBR:MC:binName", "omfi:String", QByteArray("caf\x8e originals\0", 15));
	for (const auto &name : modernNames)
		writer.add(603, "OMFI:MCBR:MC:binNameUTF8", "omfi:String", name + '\0');
	const auto source = read(writer.build());
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &evidence = result.files.first().evidence;
	const auto &values = evidence.observations(MediaProperty::OriginalBin);
	QCOMPARE(values.size(), modernNames.size() + 1);
	QCOMPARE(values.first().value.toString(), QStringLiteral("café originals"));
	QCOMPARE(values.first().eligible, !modernSelected);
	QCOMPARE(values.first().textEncoding, Canon::TextEncoding::MacRoman);
	QCOMPARE(values.first().textEncodingBasis, EvidenceBasis::Derived);
	for (qsizetype index = 1; index < values.size(); ++index)
	{
		QCOMPARE(values[index].rawValue.toByteArray(), modernNames[index - 1] + '\0');
		QCOMPARE(values[index].eligible, modernSelected);
		QCOMPARE(values[index].textEncoding, Canon::TextEncoding::Utf8);
	}
	const auto selected = evidence.resolve(MediaProperty::OriginalBin, [](MetadataSource)
										   { return 1; }, QStringLiteral("Test source priority"));
	QCOMPARE(selected.value.toString(), expected);
}

void TestCanonOmfProjection::associatedTimecodeAndClipDuration()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	master(writer, 301, 401, 501, 21, 11, "Linked master");
	writer.add(401, "OMFI:TRAK:LabelNumber", "omfi:Int16", number<qint16>(7));
	writer.add(501, "OMFI:CLIP:Length", "omfi:Length32", number<qint32>(375));
	writer.add(501, "OMFI:CPNT:EditRate", "omfi:Rational", number<qint32>(30000) + number<qint32>(1001));
	object(writer, 601, "TRAK");
	object(writer, 602, "TCCP");
	object(writer, 603, "TCCP"); // An unrelated contradictory flag must not participate.
	writer.add(101, "OMFI:TRKG:Tracks", "omfi:ObjRefArray", number<quint16>(1) + writer.reference(601, 1));
	writer.add(601, "OMFI:TRAK:TrackComponent", "omfi:ObjRef", writer.reference(602, 1));
	writer.add(602, "OMFI:TCCP:Flags", "omfi:Int32", number<qint32>(1));
	writer.add(603, "OMFI:TCCP:Flags", "omfi:Int32", number<qint32>(0));
	auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 1);
	const auto &file = result.files.first();
	QCOMPARE(single(file, MediaProperty::DropFrame).toBool(), true);
	const auto durations = single(file, MediaProperty::ClipDuration).toList();
	QCOMPARE(durations.size(), 1);
	const auto track = durations.first().toMap();
	QCOMPARE(track.value(QStringLiteral("MasterMobId")).toString(), Canon::canonicalDatabaseId(uid(21)));
	QCOMPARE(track.value(QStringLiteral("TrackId")).toUInt(), 7u);
	QCOMPARE(track.value(QStringLiteral("DropFrame")).toBool(), true);
	QCOMPARE(Canon::mediaDuration(track.value(QStringLiteral("Duration"))).units, 375);
	QCOMPARE(Canon::mediaDuration(track.value(QStringLiteral("Duration"))).rate.denominator, 1001);
	writer.add(602, "OMFI:TCCP:Flags", "omfi:Int32", number<qint32>(0));
	result = project(read(writer.build()));
	const auto &conflicting = result.files.first();
	QCOMPARE(conflicting.evidence.observations(MediaProperty::DropFrame).size(), 2);
	QVERIFY(!single(conflicting, MediaProperty::ClipDuration).toList().first().toMap().contains(QStringLiteral("DropFrame")));
}

void TestCanonOmfProjection::legacySequenceClipDuration_data()
{
	QTest::addColumn<QString>("variation");
	QTest::addColumn<qint64>("expected");
	QTest::newRow("segments-minus-transition") << QString{} << qint64(110);
	QTest::newRow("nested-sequence") << QStringLiteral("nested") << qint64(110);
	QTest::newRow("repeated-segment-occurrence") << QStringLiteral("repeat") << qint64(210);
	for (const auto &variation : {"missing-length", "missing-clock", "different-clock", "cycle", "missing-reference", "overflow", "negative-total", "unknown-component", "wrong-count"})
		QTest::newRow(variation) << QString::fromLatin1(variation) << qint64(-1);
}

void TestCanonOmfProjection::legacySequenceClipDuration()
{
	QFETCH(QString, variation);
	QFETCH(qint64, expected);
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	master(writer, 301, 401, 501, 21, 11, "Sequence-backed master");
	writer.add(301, "OMFI:TRKG:GroupLength", "omfi:Long", number<qint32>(999)); // Not this track's length.
	writer.add(401, "OMFI:TRAK:LabelNumber", "omfi:Short", number<qint16>(7));
	for (auto &entry : writer.entries)
		if (entry.object == 401 && entry.property == writer.properties.value("OMFI:TRAK:TrackComponent"))
			entry.bytes = writer.reference(601, 1);
	object(writer, 601, "SEQU");
	object(writer, 602, variation == QLatin1String("unknown-component") ? "PRIV" : "FILL");
	object(writer, 603, "TRAN");
	QVector<quint32> children{501, 602, 603};
	if (variation == QLatin1String("repeat"))
		children.prepend(501);
	if (variation == QLatin1String("cycle"))
		children.append(601);
	if (variation == QLatin1String("missing-reference"))
		children.append(999);
	const auto references = [&](const QVector<quint32> &handles)
	{
		QByteArray bytes = number<quint16>(handles.size() + (variation == QLatin1String("wrong-count") ? 1 : 0));
		for (const auto handle : handles)
			bytes += writer.reference(handle, 1);
		return bytes;
	};
	if (variation == QLatin1String("nested"))
	{
		object(writer, 604, "SEQU");
		writer.add(604, "OMFI:CPNT:EditRate", "omfi:ExactEditRate", number<qint32>(25) + number<qint32>(1));
		writer.add(604, "OMFI:SEQU:Sequence", "omfi:ObjRefArray", references({501, 603}));
		children = {604, 602};
	}
	writer.add(601, "OMFI:SEQU:Sequence", "omfi:ObjRefArray", references(children));
	for (const quint32 handle : {501u, 601u, 602u, 603u})
	{
		if (handle == 602 && variation == QLatin1String("missing-clock"))
			continue;
		writer.add(handle, "OMFI:CPNT:EditRate", "omfi:ExactEditRate",
				   number<qint32>(handle == 602 && variation == QLatin1String("different-clock") ? 24 : 25) + number<qint32>(1));
	}
	if (variation != QLatin1String("missing-length"))
		writer.add(501, "OMFI:CLIP:Length", "omfi:Length64", number<qint64>(variation == QLatin1String("overflow") ? std::numeric_limits<qint64>::max() : variation == QLatin1String("negative-total") ? 1
																																																	   : 100));
	writer.add(602, "OMFI:CLIP:Length", "omfi:Length32", number<qint32>(variation == QLatin1String("negative-total") ? 0 : 20));
	writer.add(603, "OMFI:TRKG:GroupLength", "omfi:Long", number<qint32>(10));
	const auto source = read(writer.build());
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto tracks = single(result.files.front(), MediaProperty::ClipDuration).toList();
	if (expected < 0)
	{
		QVERIFY(tracks.isEmpty());
		return;
	}
	QCOMPARE(tracks.size(), 1);
	const auto track = tracks.front().toMap();
	QCOMPARE(Canon::mediaDuration(track.value(QStringLiteral("Duration"))).units, expected);
	QCOMPARE(track.value(QStringLiteral("DurationProperty")).toString(), QStringLiteral("OMFI:SEQU:Sequence"));
	const auto provenance = track.value(QStringLiteral("DurationComponents")).toList();
	QVERIFY(provenance.size() >= 4);
	QVERIFY(std::any_of(provenance.cbegin(), provenance.cend(), [](const QVariant &item)
						{ return item.toMap().value(QStringLiteral("Object")).toUInt() == 603 &&
								 item.toMap().value(QStringLiteral("Property")).toString() == QLatin1String("OMFI:TRKG:GroupLength"); }));
	const auto contributions = provenance.last().toMap().value(QStringLiteral("Contributions")).toList();
	QVERIFY(!contributions.isEmpty());
}

void TestCanonOmfProjection::genuineDatabaseSequenceDurations()
{
	QFile input(QStringLiteral(FIXTURES_DIR "/omf/avid_supporting/msmMMOB.mdb"));
	QVERIFY(input.open(QIODevice::ReadOnly));
	Canon::Cancellation cancellation;
	const auto source = Canon::MdbReader{}.read(input, {{}, cancellation});
	QCOMPARE(source.outcome, Canon::ParsedSource::Outcome::Complete);
	const auto result = project(source);
	qsizetype withDuration = 0, withSequence = 0;
	for (const auto &file : result.files)
	{
		const auto tracks = single(file, MediaProperty::ClipDuration).toList();
		withDuration += !tracks.isEmpty();
		for (const auto &track : tracks)
			withSequence += !track.toMap().value(QStringLiteral("DurationComponents")).toList().isEmpty();
	}
	QCOMPARE(result.files.size(), 80);
	QCOMPARE(withDuration, 80);
	QCOMPARE(withSequence, 3);
}

void TestCanonOmfProjection::genuineVideoCodecNames_data()
{
	QTest::addColumn<QString>("filename");
	QTest::addColumn<QString>("expected");
	QTest::newRow("DV-PAL") << QStringLiteral("BLACK_720x576x1_DV420.omf") << QStringLiteral("DV 25 420 i(PAL)");
	QTest::newRow("DV-PAL-progressive") << QStringLiteral("BLACK_720x576x1_DV420P.omf") << QStringLiteral("DV 25P 420 p(PAL)");
	// These legacy slates record exactly 2997/100, not 30000/1001.
	// Preserve that clock. The independently confirmed DNx naming rule accepts
	// this exact legacy spelling; no tolerance or filename token supplies it.
	QTest::newRow("DV-decimal-clock") << QStringLiteral("BLACK_720x480x1_DV411.omf") << QStringLiteral("DV 25 411");
	QTest::newRow("DNx-decimal-clock") << QStringLiteral("BLACK_1920x540x2_AVHD_220.omf") << QStringLiteral("Avid DNx HQ [DNxHD 220]");
}

void TestCanonOmfProjection::exactDnxOperatingPoint()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	writer.add(201, "OMFI:DIDD:DIDResolutionID", "omfi:UInt32", number<quint32>(1241));
	writer.add(201, "OMFI:CDCI:HorizontalSubsampling", "omfi:UInt32", number<quint32>(2));
	writer.add(201, "OMFI:CDCI:VerticalSubsampling", "omfi:UInt32", number<quint32>(1));
	const auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 1);
	const auto &file = result.files.first();
	QCOMPARE(single(file, MediaProperty::Codec).toString(), QStringLiteral("Avid DNx HQX [DNxHD 185x]"));
	QCOMPARE(single(file, MediaProperty::NewDnx).toString(), QStringLiteral("Avid DNx HQX"));
	QCOMPARE(single(file, MediaProperty::OldDnx).toString(), QStringLiteral("DNxHD HQX"));
	QCOMPARE(single(file, MediaProperty::ReallyOldDnx).toString(), QStringLiteral("DNxHD 185x"));
}

void TestCanonOmfProjection::genuineVideoCodecNames()
{
	QFETCH(QString, filename);
	QFETCH(QString, expected);
	QFile input(QStringLiteral(FIXTURES_DIR "/omf/avid_supporting/") + filename);
	QVERIFY(input.open(QIODevice::ReadOnly));
	Canon::Cancellation cancellation;
	const auto source = Canon::LegacyReader{}.read(input, {{}, cancellation});
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	QCOMPARE(single(result.files.first(), MediaProperty::Codec).toString(), expected);
	if (filename.contains(QLatin1String("AVHD_220")))
	{
		QCOMPARE(Canon::mediaRate(single(result.files.first(), MediaProperty::FrameRate)).numerator, 2997);
		QCOMPARE(Canon::mediaRate(single(result.files.first(), MediaProperty::FrameRate)).denominator, 100);
		QCOMPARE(single(result.files.first(), MediaProperty::NewDnx).toString(), QStringLiteral("Avid DNx HQ"));
		QCOMPARE(single(result.files.first(), MediaProperty::OldDnx).toString(), QStringLiteral("DNxHD HQ"));
		QCOMPARE(single(result.files.first(), MediaProperty::ReallyOldDnx).toString(), QStringLiteral("DNxHD 220"));
	}
}

void TestCanonOmfProjection::legacyDnx220NamingIsExact()
{
	QFile input(QStringLiteral(FIXTURES_DIR "/omf/avid_supporting/BLACK_1920x540x2_AVHD_220.omf"));
	QVERIFY(input.open(QIODevice::ReadOnly));
	Canon::Cancellation cancellation;
	const auto original = Canon::LegacyReader{}.read(input, {{}, cancellation});
	const auto verifyRejected = [&](const QString &propertyName, QVariant replacement)
	{
		auto source = original;
		bool changed = false;
		for (auto &object : source.objects)
			for (auto &property : object.properties)
				if (property.locator.name == propertyName)
				{
					property.decoded = replacement;
					changed = true;
				}
		QVERIFY(changed);
		const auto result = project(source);
		QCOMPARE(result.files.size(), 1);
		QVERIFY(!single(result.files.first(), MediaProperty::ReallyOldDnx).isValid());
	};
	verifyRejected(QStringLiteral("OMFI:MDFL:SampleRate"), QVariantMap{{QStringLiteral("numerator"), 2996}, {QStringLiteral("denominator"), 100}});
	verifyRejected(QStringLiteral("OMFI:DIDD:StoredWidth"), 1280);
	verifyRejected(QStringLiteral("OMFI:DIDD:FrameLayout"), 0);
	verifyRejected(QStringLiteral("OMFI:CDCI:ComponentWidth"), 10);
	verifyRejected(QStringLiteral("OMFI:CDCI:HorizontalSubsampling"), 1);
}

void TestCanonOmfProjection::legacyDecimalDnxNaming_data()
{
	QTest::addColumn<quint32>("resolution");
	QTest::addColumn<int>("numerator");
	QTest::addColumn<int>("denominator");
	QTest::addColumn<int>("width");
	QTest::addColumn<int>("height");
	QTest::addColumn<int>("layout");
	QTest::addColumn<int>("depth");
	QTest::addColumn<QString>("expected");
	// Independent expected names from Avid's 2012 table; descriptor combinations
	// are recorded in the matched MDB/MXF evidence, plus Avid's OMF 220 slate.
	QTest::newRow("1080p-2997-hqx") << 1235u << 2997 << 100 << 1920 << 1080 << 0 << 10 << QStringLiteral("DNxHD 220x");
	QTest::newRow("1080p-2997-sq") << 1237u << 2997 << 100 << 1920 << 1080 << 0 << 8 << QStringLiteral("DNxHD 145");
	QTest::newRow("1080p-2997-hq") << 1238u << 2997 << 100 << 1920 << 1080 << 0 << 8 << QStringLiteral("DNxHD 220");
	QTest::newRow("1080i-2997-hqx") << 1241u << 2997 << 100 << 1920 << 540 << 1 << 10 << QStringLiteral("DNxHD 220x");
	QTest::newRow("1080i-2997-sq") << 1242u << 2997 << 100 << 1920 << 540 << 1 << 8 << QStringLiteral("DNxHD 145");
	QTest::newRow("1080i-2997-hq") << 1243u << 2997 << 100 << 1920 << 540 << 1 << 8 << QStringLiteral("DNxHD 220");
	QTest::newRow("1080p-2997-lb") << 1253u << 2997 << 100 << 1920 << 1080 << 0 << 8 << QStringLiteral("DNxHD 45");
	QTest::newRow("1080p-23976-hqx") << 1235u << 23976 << 1000 << 1920 << 1080 << 0 << 10 << QStringLiteral("DNxHD 175x");
	QTest::newRow("1080p-23976-sq") << 1237u << 23976 << 1000 << 1920 << 1080 << 0 << 8 << QStringLiteral("DNxHD 115");
	QTest::newRow("1080p-23976-hq") << 1238u << 23976 << 1000 << 1920 << 1080 << 0 << 8 << QStringLiteral("DNxHD 175");
	QTest::newRow("720p-23976-hqx") << 1250u << 23976 << 1000 << 1280 << 720 << 0 << 10 << QStringLiteral("DNxHD 90x");
	QTest::newRow("720p-23976-hq") << 1251u << 23976 << 1000 << 1280 << 720 << 0 << 8 << QStringLiteral("DNxHD 90");
	QTest::newRow("720p-23976-sq") << 1252u << 23976 << 1000 << 1280 << 720 << 0 << 8 << QStringLiteral("DNxHD 60");
	QTest::newRow("1080p-23976-lb") << 1253u << 23976 << 1000 << 1920 << 1080 << 0 << 8 << QStringLiteral("DNxHD 36");
}

void TestCanonOmfProjection::legacyDecimalDnxNaming()
{
	QFETCH(quint32, resolution);
	QFETCH(int, numerator);
	QFETCH(int, denominator);
	QFETCH(int, width);
	QFETCH(int, height);
	QFETCH(int, layout);
	QFETCH(int, depth);
	QFETCH(QString, expected);
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	for (auto &entry : writer.entries)
	{
		if (entry.object != 201)
			continue;
		const auto name = writer.properties.key(entry.property);
		if (name == "OMFI:MDFL:SampleRate")
			entry.bytes = number<qint32>(numerator) + number<qint32>(denominator);
		else if (name == "OMFI:DIDD:StoredWidth")
			entry.bytes = number<quint32>(width);
		else if (name == "OMFI:DIDD:StoredHeight")
			entry.bytes = number<quint32>(height);
		else if (name == "OMFI:DIDD:FrameLayout")
			entry.bytes = number<qint16>(layout);
		else if (name == "OMFI:CDCI:ComponentWidth")
			entry.bytes = number<quint32>(depth);
	}
	writer.add(201, "OMFI:DIDD:DIDResolutionID", "omfi:UInt32", number(resolution));
	writer.add(201, "OMFI:CDCI:HorizontalSubsampling", "omfi:UInt32", number<quint32>(2));
	writer.add(201, "OMFI:CDCI:VerticalSubsampling", "omfi:UInt32", number<quint32>(1));
	const auto source = read(writer.build());
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &file = result.files.first();
	QCOMPARE(single(file, MediaProperty::ReallyOldDnx).toString(), expected);
	QVERIFY(single(file, MediaProperty::Codec).toString().endsWith(QStringLiteral(" [%1]").arg(expected)));
	const auto recordedRate = Canon::mediaRate(single(file, MediaProperty::FrameRate));
	QCOMPARE(recordedRate.numerator, numerator);
	QCOMPARE(recordedRate.denominator, denominator);
	const auto duration = Canon::mediaDuration(single(file, MediaProperty::FileDuration));
	QCOMPARE(duration.units, 250);
	QCOMPARE(duration.rate.numerator, numerator);
	QCOMPARE(duration.rate.denominator, denominator);
	const auto &alias = file.evidence.observations(MediaProperty::ReallyOldDnx).first();
	QCOMPARE(alias.basis, EvidenceBasis::Derived);
	QCOMPARE(alias.rawValue.toByteArray(), number(resolution));
	QVERIFY(alias.explanation.contains(QStringLiteral("recorded %1/%2").arg(numerator).arg(denominator)));
}

void TestCanonOmfProjection::legacyDecimalDnxNamingRejectsUnverifiedFacts()
{
	TypedBento writer;
	writer.head(1);
	fileMob(writer, 101, 201, 11);
	writer.add(201, "OMFI:DIDD:DIDResolutionID", "omfi:UInt32", number<quint32>(1237));
	writer.add(201, "OMFI:CDCI:HorizontalSubsampling", "omfi:UInt32", number<quint32>(2));
	writer.add(201, "OMFI:CDCI:VerticalSubsampling", "omfi:UInt32", number<quint32>(1));
	auto original = read(writer.build());
	const auto replace = [](Canon::ParsedSource &source, const QString &name, const QVariant &value)
	{
		for (auto &object : source.objects)
			for (auto &property : object.properties)
				if (property.locator.name == name)
					property.decoded = value;
	};
	replace(original, QStringLiteral("OMFI:MDFL:SampleRate"), QVariantMap{{QStringLiteral("numerator"), 23976}, {QStringLiteral("denominator"), 1000}});
	replace(original, QStringLiteral("OMFI:DIDD:StoredHeight"), 1080);
	replace(original, QStringLiteral("OMFI:DIDD:FrameLayout"), 0);
	replace(original, QStringLiteral("OMFI:CDCI:ComponentWidth"), 8);
	QCOMPARE(single(project(original).files.first(), MediaProperty::ReallyOldDnx).toString(), QStringLiteral("DNxHD 115"));
	const auto rejected = [&](const QString &name, const QVariant &value)
	{
		auto changed = original;
		replace(changed, name, value);
		QVERIFY(!single(project(changed).files.first(), MediaProperty::ReallyOldDnx).isValid());
	};
	rejected(QStringLiteral("OMFI:MDFL:SampleRate"), QVariantMap{{QStringLiteral("numerator"), 23975}, {QStringLiteral("denominator"), 1000}});
	rejected(QStringLiteral("OMFI:DIDD:DIDResolutionID"), 1236);
	rejected(QStringLiteral("OMFI:DIDD:StoredWidth"), 1440);
	rejected(QStringLiteral("OMFI:DIDD:FrameLayout"), 1);
	rejected(QStringLiteral("OMFI:CDCI:ComponentWidth"), 10);
	rejected(QStringLiteral("OMFI:CDCI:HorizontalSubsampling"), 1);
	rejected(QStringLiteral("OMFI:CDCI:VerticalSubsampling"), QVariant{});
	// 720p/29.97 has a table row, but this decimal encoding/profile pair has
	// not been corroborated. Do not broaden the mapping to it automatically.
	replace(original, QStringLiteral("OMFI:MDFL:SampleRate"), QVariantMap{{QStringLiteral("numerator"), 2997}, {QStringLiteral("denominator"), 100}});
	replace(original, QStringLiteral("OMFI:DIDD:DIDResolutionID"), 1252);
	replace(original, QStringLiteral("OMFI:DIDD:StoredWidth"), 1280);
	replace(original, QStringLiteral("OMFI:DIDD:StoredHeight"), 720);
	QVERIFY(!single(project(original).files.first(), MediaProperty::ReallyOldDnx).isValid());
}

void TestCanonOmfProjection::conflictingEmbeddedIdentitiesRemainVisible()
{
	auto source = read(wave(1, 24), false);
	for (quint32 identity : {11u, 12u})
	{
		TypedBento writer;
		writer.head(1);
		fileMob(writer, 101, 201, identity);
		source.embeddedSources.append(read(writer.build(), false));
	}
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &file = result.files.first();
	QVERIFY(file.fileMobId.isEmpty());
	QVERIFY(!result.diagnostics.isEmpty());
	const auto rank = [](MetadataSource)
	{ return 1; };
	const auto identity = file.evidence.resolve(MediaProperty::FileMobId, rank, QStringLiteral("Physical ownership"));
	QCOMPARE(identity.agreement, PropertyAgreement::Conflicting);
	QVERIFY(!identity.value.isValid());
	QCOMPARE(file.evidence.observations(MediaProperty::FileMobId).size(), 2);
	QCOMPARE(file.evidence.observations(MediaProperty::Kind).size(), 3);
	QCOMPARE(file.evidence.resolve(MediaProperty::Kind, rank, {}).value.toInt(), 1);
	QCOMPARE(file.evidence.resolve(MediaProperty::BitDepth, rank, {}).value.toString(), QStringLiteral("24-bit"));
	for (const auto &value : file.evidence.observations(MediaProperty::Resolution))
		QVERIFY(!value.eligible);
}

void TestCanonOmfProjection::dnxUncompressedFlavour_data()
{
	QTest::addColumn<bool>("fixed");
	QTest::addColumn<quint32>("depth");
	QTest::addColumn<QString>("expected");
	QTest::newRow("standard-float") << false << quint32(254) << QStringLiteral("Avid DNxUncompressed — 32-bit Float");
	QTest::newRow("standard-half") << false << quint32(253) << QStringLiteral("Avid DNxUncompressed — 16-bit Half float");
	QTest::newRow("standard-integer") << false << quint32(10) << QStringLiteral("Avid DNxUncompressed — 10-bit Integer");
	QTest::newRow("fixed-2.14") << true << quint32(254) << QStringLiteral("Avid DNxUncompressed — 16-bit S2.14 fixed point");
	QTest::newRow("fixed-10.6") << true << quint32(10) << QStringLiteral("Avid DNxUncompressed — 16-bit 10.6 fixed point");
	QTest::newRow("fixed-12.4") << true << quint32(12) << QStringLiteral("Avid DNxUncompressed — 16-bit 12.4 fixed point");
}

void TestCanonOmfProjection::dnxUncompressedFlavour()
{
	QFETCH(bool, fixed);
	QFETCH(quint32, depth);
	QFETCH(QString, expected);
	TypedBento writer;
	writer.metadataBig = true;
	writer.head(1);
	object(writer, 101, "MOBJ");
	object(writer, 201, "CDCI");
	writer.add(101, "OMFI:MOBJ:MobID", "omfi:UID", uid(11, true));
	writer.add(101, "OMFI:MOBJ:PhysicalMedia", "omfi:ObjRef", writer.reference(201, 1));
	writer.add(201, "OMFI:CDCI:ComponentWidth", "omfi:UInt32", number(depth, true));
	const auto label = QByteArray::fromHex(fixed ? "060e2b340401010d0401020203070200" : "060e2b340401010d0401020203070100");
	writer.add(201, "OMFI:DIDD:EssenceCompression", "omfi:UID", label.mid(8) + label.first(8));
	const auto result = project(read(writer.build()));
	QCOMPARE(result.files.size(), 1);
	QCOMPARE(single(result.files.first(), MediaProperty::Codec).toString(), expected);
}

void TestCanonOmfProjection::unknownLayoutDoesNotGuessRaster()
{
	for (const bool invalid : {false, true})
	{
		TypedBento writer;
		writer.head(1);
		fileMob(writer, 101, 201, 11);
		const auto layoutId = writer.properties.value("OMFI:DIDD:FrameLayout");
		writer.entries.removeIf([&](const auto &entry)
								{ return entry.property == layoutId; });
		if (invalid)
			writer.add(201, "OMFI:DIDD:FrameLayout", "omfi:LayoutType", number<qint16>(9));
		const auto result = project(read(writer.build()));
		QCOMPARE(result.files.size(), 1);
		QVERIFY(result.files.first().evidence.observations(MediaProperty::Resolution).isEmpty());
		QCOMPARE(single(result.files.first(), MediaProperty::BitDepth).toString(), QStringLiteral("10-bit"));
	}
}

void TestCanonOmfProjection::genuineProjectWithoutPmr()
{
	QFile input(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/TONE_100A01.6A972974.039700.wav"));
	QVERIFY(input.open(QIODevice::ReadOnly));
	Canon::Cancellation cancellation;
	const auto source = Canon::LegacyReader{}.read(input, {{}, cancellation});
	const auto result = project(source);
	QCOMPARE(result.files.size(), 1);
	const auto &observations = result.files.first().evidence.observations(MediaProperty::Project);
	QVERIFY(!observations.isEmpty());
	for (const auto &value : observations)
	{
		QCOMPARE(value.value.toString(), QString::fromUtf8("zTe\xc3\x9f"
														   "t_PAL_25p"));
		QCOMPARE(value.snapshot->source, MetadataSource::Omf);
		QCOMPARE(value.textEncoding, Canon::TextEncoding::MacRoman);
		QCOMPARE(value.textEncodingBasis, EvidenceBasis::Derived);
		QCOMPARE(value.basis, EvidenceBasis::Derived);
	}
	bool foundRaw = false;
	for (const auto &part : source.embeddedSources)
		for (const auto &object : part.objects)
			for (const auto &property : object.properties)
				if (property.locator.name == QLatin1String("OMFI:ATTB:StringAttribute") &&
					property.encoding == QByteArray::fromHex("7a5465a7745f50414c5f32357000"))
				{
					foundRaw = true;
					QCOMPARE(property.textEncoding, Canon::TextEncoding::Unknown);
					QVERIFY(!property.decoded.isValid());
				}
	QVERIFY(foundRaw);
}

void TestCanonOmfProjection::cancellation()
{
	Canon::Cancellation cancelled;
	cancelled.cancel();
	QVERIFY(Canon::projectOmf({}, cancelled).files.isEmpty());
}

void TestCanonOmfProjection::genuineFiles_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<bool>("database");
	QTest::newRow("mdb") << QStringLiteral(FIXTURES_DIR "/msmMMOB.mdb") << true;
	QTest::newRow("omf-video") << QStringLiteral(FIXTURES_DIR "/omf/avid_supporting/BLACK_720x243x2_JFIF35.omf") << false;
	QTest::newRow("native-wave") << QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/TONE_100A01.6A972974.039700.wav") << false;
	QTest::newRow("native-aif") << QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/TONE_100A01.6A972997.0C53E0.aif") << false;
}

void TestCanonOmfProjection::genuineFiles()
{
	QFETCH(QString, path);
	QFETCH(bool, database);
	QFile input(path);
	QVERIFY2(input.open(QIODevice::ReadOnly), qPrintable(input.errorString()));
	Canon::Cancellation cancellation;
	const auto source = database ? Canon::MdbReader{}.read(input, {{}, cancellation}) : Canon::LegacyReader{}.read(input, {{}, cancellation});
	QCOMPARE(source.outcome, Canon::ParsedSource::Outcome::Complete);
	const auto result = project(source);
	QVERIFY2(!result.files.isEmpty(), qPrintable(result.diagnostics.join(QLatin1Char('\n'))));
	for (const auto &file : result.files)
	{
		QVERIFY(!file.fileMobId.isEmpty());
		QVERIFY(!file.evidence.observations(MediaProperty::Kind).isEmpty());
	}
	if (!database)
	{
		QCOMPARE(result.files.size(), 1);
		QVERIFY(!result.files.first().evidence.observations(MediaProperty::FileDuration).isEmpty());
		QVERIFY(!result.files.first().evidence.observations(MediaProperty::BitDepth).isEmpty());
	}
}

QTEST_GUILESS_MAIN(TestCanonOmfProjection)
#include "tst_canon_omfprojection.moc"
