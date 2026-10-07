// These graphs are authored independently of the old MXF parser. They test
// ownership and semantic boundaries; the reader tests cover binary framing.

#include "canon/projection.h"
#include "canon/mxfreader.h"

#include <QFile>
#include <QTest>
#include <limits>

namespace
{
	using namespace Canon;

	QByteArray umid(char material)
	{
		return QByteArray::fromHex("060a2b340101010501010f1013000000") + QByteArray(16, material);
	}

	struct Graph
	{
		ParsedSource source;
		ObjectHandle ecd = 0, file = 0, descriptor = 0, preface = 0;
		QByteArray fileId = umid('f');

		Graph(const char *descriptorClass = "CDCIEssenceDescriptor", qint64 partition = 0)
		{
			source.container = ParsedSource::Container::Mxf;
			source.outcome = ParsedSource::Outcome::Complete;
			source.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Mxf, QStringLiteral("test.mxf"), {}, SourceReadState::Complete});
			preface = object("Preface", partition);
			property(preface, "Preface.ProjectEditRate", rateValue({24, 1}));
			ecd = object("EssenceContainerData", partition);
			property(ecd, "EssenceContainerData.LinkedPackageUID", fileId);
			file = object("SourcePackage", partition);
			property(file, "GenericPackage.PackageUID", fileId);
			descriptor = object(descriptorClass, partition);
			edge(file, "SourcePackage.Descriptor", descriptor);
		}

		ObjectHandle object(const char *name, qint64 partition = 0)
		{
			AvidObject value;
			value.handle = quint64(source.objects.size()) + 1;
			value.snapshot = source.snapshot;
			auto context = QSharedPointer<MxfSetContext>::create();
			context->name = QString::fromLatin1(name);
			context->partitionOffset = partition;
			context->value.offset = qint64(value.handle) * 10000;
			value.mxf = context;
			source.objects.append(value);
			property(value.handle, "InterchangeObject.InstanceUID", QByteArray(16, char(value.handle)));
			return value.handle;
		}

		AvidObject &at(ObjectHandle handle) { return source.objects[qsizetype(handle - 1)]; }

		void property(ObjectHandle handle, const char *name, const QVariant &value)
		{
			auto &object = at(handle);
			RawProperty property;
			property.locator.name = QString::fromLatin1(name);
			property.locator.objectNumber = handle;
			property.decoded = value;
			property.state = PropertyReadState::Present;
			property.encoding = value.metaType().id() == QMetaType::QByteArray ? value.toByteArray() : QByteArray(4, 'v');
			auto set = QSharedPointer<MxfSetContext>::create(*object.mxf);
			auto context = QSharedPointer<MxfPropertyContext>::create();
			context->mappedAuid = QByteArray(16, 'k');
			const qint64 offset = set->value.offset + set->value.length;
			context->framingRanges.append({offset, 4});
			property.locator.ranges.append({offset + 4, property.encoding.size()});
			property.mxf = context;
			set->value.length += 4 + property.encoding.size();
			object.mxf = set;
			object.properties.append(property);
		}

		void edge(ObjectHandle from, const char *name, ObjectHandle to)
		{
			property(from, name, QByteArray(16, char(to)));
			Relationship relationship;
			relationship.origin = from;
			relationship.target = to;
			relationship.locator.name = QString::fromLatin1(name);
			source.relationships.append(relationship);
		}

		void replace(ObjectHandle handle, const char *name, const QVariant &value)
		{
			for (auto &property : at(handle).properties)
				if (property.locator.name == QLatin1String(name))
					property.decoded = value;
		}

		ObjectHandle master(char id = 'm')
		{
			const auto partition = at(file).mxf->partitionOffset;
			const auto result = object("MaterialPackage", partition);
			property(result, "GenericPackage.PackageUID", umid(id));
			property(result, "GenericPackage.Name", QStringLiteral("Clip %1").arg(QChar::fromLatin1(id)));
			const auto track = object("Track", partition), clip = object("SourceClip", partition);
			property(track, "GenericTrack.TrackID", 1u);
			property(track, "Track.EditRate", rateValue({24, 1}));
			property(clip, "SourceClip.SourcePackageID", fileId);
			property(clip, "StructuralComponent.Duration", qint64(48));
			edge(result, "GenericPackage.Tracks", track);
			edge(track, "GenericTrack.Sequence", clip);
			return result;
		}

		void picture(const QByteArray &coding, bool hasDisplay = true)
		{
			property(descriptor, "FileDescriptor.SampleRate", rateValue({24, 1}));
			property(descriptor, "FileDescriptor.ContainerDuration", qint64(240));
			property(descriptor, "GenericPictureEssenceDescriptor.PictureEssenceCoding", coding);
			property(descriptor, "GenericPictureEssenceDescriptor.StoredWidth", 1920u);
			property(descriptor, "GenericPictureEssenceDescriptor.StoredHeight", 1088u);
			if (hasDisplay)
			{
				property(descriptor, "GenericPictureEssenceDescriptor.DisplayWidth", 1920u);
				property(descriptor, "GenericPictureEssenceDescriptor.DisplayHeight", 1080u);
			}
			property(descriptor, "GenericPictureEssenceDescriptor.FrameLayout", 0u);
			property(descriptor, "CDCIEssenceDescriptor.ComponentDepth", 10u);
			property(descriptor, "CDCIEssenceDescriptor.HorizontalSubsampling", 2u);
			property(descriptor, "CDCIEssenceDescriptor.VerticalSubsampling", 1u);
		}
	};

	QVariant value(const ProjectedFile &file, MediaProperty field)
	{
		const auto &values = file.evidence.observations(field);
		return values.isEmpty() ? QVariant{} : values.first().value;
	}

	Projection project(const Graph &graph)
	{
		Cancellation cancellation;
		return projectMxf(graph.source, cancellation);
	}
}

class TestCanonMxfProjection : public QObject
{
	Q_OBJECT

private slots:
	void legacy_aafsdk_ids_follow_the_source_encoding()
	{
		// Recorded in 1042.WAVA01.D77B775B553A6FA.mxf and its local PMR.
		// The prefix-42 suffix is an identity family, not a byte-order flag.
		const QByteArray wire = QByteArray::fromHex("060a2b340101010101010f00130000005b553a6f83d50a6e060e2b347f7f2a80");
		const QByteArray database = QByteArray::fromHex("060a2b340101010101010f00130000006f3a555bd5836e0a060e2b347f7f2a80");
		const QString expected = QStringLiteral("060a2b3401010101.01010f0013000000.6f3a555bd5836e0a.060e2b347f7f2a80");
		QCOMPARE(canonicalMxfId(wire), expected);
		QCOMPARE(canonicalDatabaseId(database), expected);
		QCOMPARE(canonicalDatabaseId(database.mid(16, 8)), expected);
		QCOMPARE(canonicalMxfId(QByteArray(32, '\0')),
				 QStringLiteral("0000000000000000.0000000000000000.0000000000000000.0000000000000000"));

		Graph graph;
		graph.fileId = wire;
		for (const auto handle : {graph.ecd, graph.file})
			for (auto &property : graph.at(handle).properties)
				if (property.locator.name.endsWith(QStringLiteral("PackageUID")))
				{
					property.encoding = wire;
					property.decoded = wire;
				}
		const auto master = graph.master();
		for (auto &property : graph.at(master).properties)
			if (property.locator.name == QStringLiteral("GenericPackage.PackageUID"))
			{
				property.encoding = QByteArray::fromHex("060a2b340101010101010f00130000005b553a6a6e260a6e060e2b347f7f2a80");
				property.decoded = property.encoding;
			}
		const auto result = project(graph);
		QCOMPARE(result.files.size(), 1);
		QCOMPARE(result.files.first().fileMobId, expected);
		QCOMPARE(result.files.first().masterMobIds,
				 QStringList{QStringLiteral("060a2b3401010101.01010f0013000000.6a3a555b266e6e0a.060e2b347f7f2a80")});
		const auto &observations = result.files.first().evidence.observations(MediaProperty::FileMobId);
		QVERIFY(!observations.isEmpty());
		for (const auto &observation : observations)
		{
			QCOMPARE(observation.value.toString(), expected);
			QCOMPARE(observation.rawValue.toByteArray(), wire);
		}
	}

	void ownership_requires_an_explicit_unique_package()
	{
		Graph graph;
		graph.master();
		QCOMPARE(project(graph).files.size(), 1);
		graph.at(graph.ecd).properties.removeLast();
		QVERIFY(project(graph).files.isEmpty());
		graph.property(graph.ecd, "EssenceContainerData.LinkedPackageUID", graph.fileId);
		const auto duplicate = graph.object("SourcePackage");
		graph.property(duplicate, "GenericPackage.PackageUID", graph.fileId);
		QVERIFY(project(graph).files.isEmpty());
	}

	void masters_and_repeated_partitions_remain_separate()
	{
		Graph graph;
		graph.master('m');
		graph.master('n');
		const auto unrelated = graph.object("MaterialPackage");
		graph.property(unrelated, "GenericPackage.PackageUID", umid('x'));
		graph.property(unrelated, "GenericPackage.Name", QStringLiteral("Wrong clip"));
		auto result = project(graph);
		QCOMPARE(result.files.size(), 1);
		QCOMPARE(result.files.first().masterMobIds.size(), 2);
		QCOMPARE(value(result.files.first(), MediaProperty::ClipDuration).toList().size(), 2);
		const auto ecd = graph.object("EssenceContainerData", 999);
		graph.property(ecd, "EssenceContainerData.LinkedPackageUID", graph.fileId);
		const auto package = graph.object("SourcePackage", 999);
		graph.property(package, "GenericPackage.PackageUID", graph.fileId);
		result = project(graph);
		QCOMPARE(result.files.size(), 2);
		QVERIFY(result.files.last().masterMobIds.isEmpty());
	}

	void drop_frame_comes_from_associated_timecode_only()
	{
		Graph graph;
		const auto master = graph.master();
		QVERIFY(!value(project(graph).files.first(), MediaProperty::DropFrame).isValid());
		const auto track = graph.object("Track"), timecode = graph.object("TimecodeComponent");
		graph.property(timecode, "TimecodeComponent.DropFrame", true);
		graph.edge(track, "GenericTrack.Sequence", timecode);
		const auto unrelated = graph.object("MaterialPackage");
		graph.edge(unrelated, "GenericPackage.Tracks", track);
		QVERIFY(!value(project(graph).files.first(), MediaProperty::DropFrame).isValid());
		graph.edge(master, "GenericPackage.Tracks", track);
		const auto result = project(graph).files.first();
		QCOMPARE(value(result, MediaProperty::DropFrame).toBool(), true);
		QCOMPARE(value(result, MediaProperty::ClipDuration).toList().first().toMap().value(QStringLiteral("DropFrame")).toBool(), true);
	}

	void audio_keeps_sample_units_and_does_not_invent_video_rate()
	{
		Graph graph("WaveAudioDescriptor");
		graph.property(graph.descriptor, "FileDescriptor.SampleRate", rateValue({48000, 1}));
		graph.property(graph.descriptor, "FileDescriptor.ContainerDuration", qint64(96000));
		graph.property(graph.descriptor, "GenericSoundEssenceDescriptor.AudioSamplingRate", rateValue({48000, 1}));
		graph.property(graph.descriptor, "GenericSoundEssenceDescriptor.QuantizationBits", 24u);
		graph.property(graph.descriptor, "GenericSoundEssenceDescriptor.ChannelCount", 1u);
		const auto result = project(graph);
		const auto &file = result.files.first();
		QCOMPARE(value(file, MediaProperty::Codec).toString(), QStringLiteral("PCM"));
		QCOMPARE(value(file, MediaProperty::Kind).toInt(), 1);
		QCOMPARE(value(file, MediaProperty::Channels).toInt(), 1);
		QVERIFY(!value(file, MediaProperty::FrameRate).isValid());
		const auto duration = mediaDuration(value(file, MediaProperty::FileDuration));
		QCOMPARE(duration.units, 96000);
		QVERIFY(duration.rate.sameRate({48000, 1}));
		QVERIFY(duration.displayRate.sameRate({24, 1}));
		QCOMPARE(duration.displayFrames(), 48);
	}

	void registered_sound_properties_work_on_an_unknown_descriptor_class()
	{
		Graph graph("");
		graph.property(graph.descriptor, "GenericSoundEssenceDescriptor.SoundEssenceCompression",
					   QByteArray::fromHex("060e2b34040101010402020203020500"));
		const auto result = project(graph);
		QCOMPARE(value(result.files.first(), MediaProperty::Codec).toString(), QStringLiteral("MP2"));
		QCOMPARE(value(result.files.first(), MediaProperty::Kind).toInt(), 1);
	}

	void dnx_alias_needs_the_exact_profile_and_operating_point()
	{
		Graph hd;
		hd.picture(QByteArray::fromHex("060e2b340401010a0401020271010000"));
		const auto result = project(hd);
		const auto &file = result.files.first();
		// A valid display crop removes storage padding. DNx naming still
		// requires its own established profile and exact operating point.
		QCOMPARE(value(file, MediaProperty::Resolution).toString(), QStringLiteral("1920x1080"));
		QCOMPARE(value(file, MediaProperty::Codec).toString(), QStringLiteral("Avid DNx HQX [DNxHD 175x]"));
		QCOMPARE(value(file, MediaProperty::OldDnx).toString(), QStringLiteral("DNxHD HQX"));
		Graph hr;
		hr.picture(QByteArray::fromHex("060e2b340401010d0401020271250000"));
		const auto independent = project(hr);
		QCOMPARE(value(independent.files.first(), MediaProperty::OldDnx).toString(), QStringLiteral("DNxHR HQX"));
		QVERIFY(!value(independent.files.first(), MediaProperty::ReallyOldDnx).isValid());
		hd.property(hd.descriptor, "FileDescriptor.SampleRate", rateValue({23976, 1000}));
		QVERIFY(!value(project(hd).files.first(), MediaProperty::ReallyOldDnx).isValid());
	}

	void full_resolution_requires_a_known_frame_layout()
	{
		Graph graph;
		graph.picture(QByteArray::fromHex("060e2b340401010a0401020271010000"));
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.FrameLayout", 99u);
		QVERIFY(!value(project(graph).files.first(), MediaProperty::Resolution).isValid());
		auto &properties = graph.at(graph.descriptor).properties;
		properties.removeIf([](const RawProperty &item)
							{ return item.locator.name == QStringLiteral("GenericPictureEssenceDescriptor.FrameLayout"); });
		QVERIFY(!value(project(graph).files.first(), MediaProperty::Resolution).isValid());
	}

	void visible_resolution_data()
	{
		QTest::addColumn<qint64>("width");
		QTest::addColumn<qint64>("height");
		QTest::addColumn<int>("layout");
		QTest::addColumn<qint64>("displayHeight");
		QTest::addColumn<QString>("damage");
		QTest::addColumn<QString>("expected");
		QTest::newRow("progressive") << qint64(1920) << qint64(1080) << 0 << qint64(1080) << QString{} << QStringLiteral("1920x1080");
		QTest::newRow("single-field") << qint64(1920) << qint64(540) << 2 << qint64(540) << QString{} << QStringLiteral("1920x540");
		QTest::newRow("separate-fields") << qint64(1920) << qint64(544) << 1 << qint64(540) << QString{} << QStringLiteral("1920x1080");
		QTest::newRow("padding-cropped") << qint64(1920) << qint64(1088) << 0 << qint64(1080) << QString{} << QStringLiteral("1920x1080");
		QTest::newRow("padding-without-crop") << qint64(1920) << qint64(1088) << 0 << qint64(1088) << QStringLiteral("no-display") << QStringLiteral("1920x1088");
		QTest::newRow("unverified-small-raster") << qint64(480) << qint64(270) << 0 << qint64(1080) << QString{} << QString{};
		QTest::newRow("missing-width") << qint64(1920) << qint64(1080) << 0 << qint64(1080) << QStringLiteral("missing-width") << QString{};
		QTest::newRow("missing-height") << qint64(1920) << qint64(1080) << 0 << qint64(1080) << QStringLiteral("missing-height") << QString{};
		QTest::newRow("conflicting-width") << qint64(1920) << qint64(1080) << 0 << qint64(1080) << QStringLiteral("conflicting-width") << QString{};
		QTest::newRow("zero-width") << qint64(0) << qint64(1080) << 0 << qint64(1080) << QString{} << QString{};
		QTest::newRow("height-overflow") << qint64(1920) << std::numeric_limits<qint64>::max() << 1 << std::numeric_limits<qint64>::max() << QStringLiteral("no-display") << QString{};
	}

	void visible_resolution()
	{
		QFETCH(qint64, width);
		QFETCH(qint64, height);
		QFETCH(int, layout);
		QFETCH(qint64, displayHeight);
		QFETCH(QString, damage);
		QFETCH(QString, expected);
		Graph graph;
		graph.picture(QByteArray::fromHex("060e2b340401010d0401020201311101"), damage != QLatin1String("no-display"));
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.StoredWidth", width);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.StoredHeight", height);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.FrameLayout", layout);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayHeight", displayHeight);
		auto &properties = graph.at(graph.descriptor).properties;
		if (damage == QLatin1String("missing-width"))
			properties.removeIf([](const RawProperty &item)
								{ return item.locator.name.endsWith(QLatin1String(".StoredWidth")); });
		else if (damage == QLatin1String("missing-height"))
			properties.removeIf([](const RawProperty &item)
								{ return item.locator.name.endsWith(QLatin1String(".StoredHeight")); });
		else if (damage == QLatin1String("conflicting-width"))
			graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.StoredWidth", 320u);
		const auto original = properties;
		const auto result = project(graph);
		const auto &observations = result.files.first().evidence.observations(MediaProperty::Resolution);
		if (expected.isEmpty())
			QVERIFY(observations.isEmpty());
		else
		{
			QCOMPARE(observations.size(), 1);
			QCOMPARE(observations.first().value.toString(), expected);
			QCOMPARE(observations.first().property, damage == QLatin1String("no-display")
				? QStringLiteral("GenericPictureEssenceDescriptor.StoredWidth")
				: QStringLiteral("GenericPictureEssenceDescriptor.DisplayWidth"));
			QCOMPARE(observations.first().snapshot, graph.source.snapshot);
		}
		QCOMPARE(properties.size(), original.size());
		for (qsizetype index = 0; index < original.size(); ++index)
		{
			QCOMPARE(properties[index].decoded, original[index].decoded);
			QCOMPARE(properties[index].encoding, original[index].encoding);
			QCOMPARE(properties[index].locator.name, original[index].locator.name);
		}
	}

	void nested_crop_offsets_data()
	{
		QTest::addColumn<QString>("damage");
		QTest::addColumn<QString>("expected");
		QTest::newRow("valid-at-edges") << QString{} << QStringLiteral("1280x720");
		QTest::newRow("sampled-default-display") << QStringLiteral("no-display") << QStringLiteral("1800x1080");
		QTest::newRow("sampled-outside-stored") << QStringLiteral("sampled-outside") << QString{};
		QTest::newRow("display-outside-sampled") << QStringLiteral("display-outside") << QString{};
		QTest::newRow("negative-offset") << QStringLiteral("negative-offset") << QString{};
		QTest::newRow("huge-offset") << QStringLiteral("huge-offset") << QString{};
		QTest::newRow("unreadable-offset") << QStringLiteral("unreadable-offset") << QString{};
		QTest::newRow("conflicting-offset") << QStringLiteral("conflicting-offset") << QString{};
	}

	void nested_crop_offsets()
	{
		QFETCH(QString, damage);
		QFETCH(QString, expected);
		Graph graph;
		graph.picture(QByteArray::fromHex("060e2b340401010a0401020271010000"),
			damage != QLatin1String("no-display"));
		graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.SampledWidth", 1800u);
		graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.SampledHeight", 1080u);
		graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.SampledXOffset", 120);
		graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.SampledYOffset", 8);
		if (damage != QLatin1String("no-display"))
		{
			// Display offsets are relative to Sampled, not Stored. Both crops
			// fit exactly at their parent's lower-right edge.
			graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayWidth", 1280u);
			graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayHeight", 720u);
			graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayXOffset", 520);
			graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayYOffset", 360);
		}
		if (damage == QLatin1String("sampled-outside"))
			graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.SampledXOffset", 121);
		else if (damage == QLatin1String("display-outside"))
			graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayXOffset", 521);
		else if (damage == QLatin1String("negative-offset"))
			graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayYOffset", -1);
		else if (damage == QLatin1String("huge-offset"))
			graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayYOffset",
				std::numeric_limits<qint64>::max());
		else if (damage == QLatin1String("unreadable-offset"))
		{
			for (auto &property : graph.at(graph.descriptor).properties)
				if (property.locator.name.endsWith(QLatin1String(".DisplayYOffset")))
					property.state = PropertyReadState::Unreadable;
		}
		else if (damage == QLatin1String("conflicting-offset"))
			graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayYOffset", 0);
		const auto selected = value(project(graph).files.first(), MediaProperty::Resolution);
		if (expected.isEmpty())
			QVERIFY(!selected.isValid());
		else
			QCOMPARE(selected.toString(), expected);
	}

	void verified_proxy_configurations_data()
	{
		QTest::addColumn<int>("id");
		QTest::addColumn<int>("width");
		QTest::addColumn<int>("height");
		QTest::addColumn<int>("displayWidth");
		QTest::addColumn<int>("displayHeight");
		QTest::addColumn<int>("layout");
		// Independently probed local files: see Canon's proxy evidence. Their
		// upscaled descriptor rectangles cannot be treated as literal crops.
		QTest::newRow("3472") << 3472 << 480 << 270 << 1920 << 540 << 2;
		QTest::newRow("3484") << 3484 << 480 << 270 << 1920 << 1080 << 0;
		QTest::newRow("3487") << 3487 << 480 << 270 << 1920 << 1080 << 0;
		QTest::newRow("3488") << 3488 << 320 << 180 << 1280 << 720 << 0;
		QTest::newRow("3470") << 3470 << 352 << 240 << 720 << 240 << 2;
		QTest::newRow("3491") << 3491 << 352 << 240 << 720 << 240 << 2;
		QTest::newRow("3483") << 3483 << 352 << 288 << 720 << 576 << 0;
	}

	void verified_proxy_configurations()
	{
		QFETCH(int, id);
		QFETCH(int, width);
		QFETCH(int, height);
		QFETCH(int, displayWidth);
		QFETCH(int, displayHeight);
		QFETCH(int, layout);
		Graph graph;
		graph.picture(QByteArray::fromHex("060e2b340401010d0401020201311101"));
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.StoredWidth", width);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.StoredHeight", height);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.FrameLayout", layout);
		graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.ResolutionID", id);
		for (const auto *prefix : {"Sampled", "Display"})
		{
			const QByteArray base = QByteArray("GenericPictureEssenceDescriptor.") + prefix;
			if (QLatin1String(prefix) == QLatin1String("Display"))
			{
				graph.replace(graph.descriptor, (base + "Width").constData(), displayWidth);
				graph.replace(graph.descriptor, (base + "Height").constData(), displayHeight);
			}
			else
			{
				graph.property(graph.descriptor, (base + "Width").constData(), displayWidth);
				graph.property(graph.descriptor, (base + "Height").constData(), displayHeight);
			}
			graph.property(graph.descriptor, (base + "XOffset").constData(), 0);
			graph.property(graph.descriptor, (base + "YOffset").constData(), 0);
		}
		const auto result = project(graph);
		const auto &observations = result.files.first().evidence.observations(MediaProperty::Resolution);
		QCOMPARE(observations.size(), 1);
		QCOMPARE(observations.first().value.toString(), QStringLiteral("%1x%2").arg(width).arg(height));
		QCOMPARE(observations.first().basis, EvidenceBasis::Derived);
		QVERIFY(observations.first().explanation.contains(QStringLiteral("qualified inference")));

		// Near matches must not turn a contradictory descriptor into a proxy.
		for (int damage = 0; damage < 5; ++damage)
		{
			Graph different = graph;
			switch (damage)
			{
			case 0: different.replace(different.descriptor, "GenericPictureEssenceDescriptor.ResolutionID", 9999); break;
			case 1: different.replace(different.descriptor, "GenericPictureEssenceDescriptor.PictureEssenceCoding", QByteArray(16, 'x')); break;
			case 2: different.replace(different.descriptor, "GenericPictureEssenceDescriptor.SampledWidth", displayWidth + 1); break;
			case 3: different.replace(different.descriptor, "GenericPictureEssenceDescriptor.DisplayXOffset", 1); break;
			case 4: different.replace(different.descriptor, "GenericPictureEssenceDescriptor.FrameLayout", 4); break;
			}
			QVERIFY(!value(project(different).files.first(), MediaProperty::Resolution).isValid());
		}
	}

	void fixed_and_float_sentinels_and_alpha_are_independent()
	{
		for (const bool fixed : {false, true})
		{
			Graph graph;
			graph.property(graph.descriptor, "GenericPictureEssenceDescriptor.PictureEssenceCoding",
						   QByteArray::fromHex(fixed ? "060e2b340401010d0401020203070200" : "060e2b340401010d0401020203070100"));
			graph.property(graph.descriptor, "CDCIEssenceDescriptor.ComponentDepth", 254u);
			const auto result = project(graph);
			QCOMPARE(value(result.files.first(), MediaProperty::BitDepth).toString(), fixed ? QStringLiteral("16-bit") : QStringLiteral("32-bit"));
			QCOMPARE(value(result.files.first(), MediaProperty::SampleFormat).toString(), fixed ? QStringLiteral("S2.14 fixed point") : QStringLiteral("Float"));
			QVERIFY(!value(result.files.first(), MediaProperty::Alpha).isValid());
		}
		Graph rgba("RGBAEssenceDescriptor");
		QVariantList layout;
		layout.append(QVariantMap{{QStringLiteral("Code"), int('A')}, {QStringLiteral("Depth"), 16}});
		while (layout.size() < 8)
			layout.append(QVariantMap{{QStringLiteral("Code"), 0}, {QStringLiteral("Depth"), 0}});
		rgba.property(rgba.descriptor, "RGBAEssenceDescriptor.PixelLayout", layout);
		const auto result = project(rgba);
		QVERIFY(value(result.files.first(), MediaProperty::Alpha).toBool());
		QCOMPARE(value(result.files.first(), MediaProperty::BitDepth).toString(), QStringLiteral("16-bit"));
	}

	void historical_dnx_operating_points_data()
	{
		QTest::addColumn<QByteArray>("coding");
		QTest::addColumn<int>("width");
		QTest::addColumn<int>("height");
		QTest::addColumn<int>("layout");
		QTest::addColumn<int>("numerator");
		QTest::addColumn<int>("denominator");
		QTest::addColumn<int>("depth");
		QTest::addColumn<int>("horizontal");
		QTest::addColumn<QString>("expected");
		QTest::newRow("SQ1080p23.976") << QByteArray::fromHex("060e2b340401010a0401020271030000") << 1920 << 1080 << 0 << 24000 << 1001 << 8 << 2 << QStringLiteral("DNxHD 115");
		QTest::newRow("HQ1080p60") << QByteArray::fromHex("060e2b340401010a0401020271040000") << 1920 << 1080 << 0 << 60 << 1 << 8 << 2 << QStringLiteral("DNxHD 440");
		QTest::newRow("HQX1080i59.94") << QByteArray::fromHex("060e2b340401010a0401020271070000") << 1920 << 540 << 1 << 30000 << 1001 << 10 << 2 << QStringLiteral("DNxHD 220x");
		QTest::newRow("SQ720p50") << QByteArray::fromHex("060e2b340401010a0401020271120000") << 1280 << 720 << 0 << 50 << 1 << 8 << 2 << QStringLiteral("DNxHD 115");
		QTest::newRow("HQ720p25") << QByteArray::fromHex("060e2b340401010a0401020271110000") << 1280 << 720 << 0 << 25 << 1 << 8 << 2 << QStringLiteral("DNxHD 90");
		QTest::newRow("LB1080p25") << QByteArray::fromHex("060e2b340401010a0401020271130000") << 1920 << 1080 << 0 << 25 << 1 << 8 << 2 << QStringLiteral("DNxHD 36");
		QTest::newRow("4441080p24") << QByteArray::fromHex("060e2b340401010d0401020271160000") << 1920 << 1080 << 0 << 24 << 1 << 10 << 1 << QStringLiteral("DNxHD 350x");
		QTest::newRow("unlisted1080p30") << QByteArray::fromHex("060e2b340401010a0401020271030000") << 1920 << 1080 << 0 << 30 << 1 << 8 << 2 << QString{};
		QTest::newRow("unlisted720p24") << QByteArray::fromHex("060e2b340401010a0401020271100000") << 1280 << 720 << 0 << 24 << 1 << 10 << 2 << QString{};
		QTest::newRow("wrong-profile-raster") << QByteArray::fromHex("060e2b340401010a0401020271030000") << 1280 << 720 << 0 << 25 << 1 << 8 << 2 << QString{};
	}

	void historical_dnx_operating_points()
	{
		QFETCH(QByteArray, coding);
		QFETCH(int, width);
		QFETCH(int, height);
		QFETCH(int, layout);
		QFETCH(int, numerator);
		QFETCH(int, denominator);
		QFETCH(int, depth);
		QFETCH(int, horizontal);
		QFETCH(QString, expected);
		Graph graph;
		graph.picture(coding);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayWidth", width);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.DisplayHeight", height);
		graph.replace(graph.descriptor, "GenericPictureEssenceDescriptor.FrameLayout", layout);
		graph.replace(graph.descriptor, "FileDescriptor.SampleRate", rateValue({numerator, denominator}));
		graph.replace(graph.descriptor, "CDCIEssenceDescriptor.ComponentDepth", depth);
		graph.replace(graph.descriptor, "CDCIEssenceDescriptor.HorizontalSubsampling", horizontal);
		const auto result = project(graph);
		QCOMPARE(value(result.files.first(), MediaProperty::ReallyOldDnx).toString(), expected);
	}

	void import_url_filename_matches_native_path_data()
	{
		QTest::addColumn<QString>("url");
		QTest::addColumn<QString>("nativePath");
		QTest::addColumn<QString>("expected");
		QTest::newRow("spaces") << QStringLiteral("file:///Macintosh%20HD/Users/editor/Apple%20ProRes%20422%20Proxy.mov")
								<< QStringLiteral("/Users/editor/Apple ProRes 422 Proxy.mov") << QStringLiteral("Apple ProRes 422 Proxy.mov");
		QTest::newRow("literal-percent-decoded-once") << QStringLiteral("file:///source/Literal%2520Name.mov")
													  << QStringLiteral("/source/Literal%20Name.mov") << QStringLiteral("Literal%20Name.mov");
		QTest::newRow("utf8") << QStringLiteral("file:///source/%E4%BD%A0%E5%A5%BD%20caf%C3%A9.mov")
							  << QStringLiteral("/source/你好 café.mov") << QStringLiteral("你好 café.mov");
		QTest::newRow("true-conflict") << QStringLiteral("file:///source/other.mov")
									   << QStringLiteral("/source/recorded.mov") << QString{};
	}

	void import_url_filename_matches_native_path()
	{
		QFETCH(QString, url);
		QFETCH(QString, nativePath);
		QFETCH(QString, expected);
		Graph graph;
		const auto origin = graph.object("SourcePackage"), descriptor = graph.object("ImportDescriptor"), locator = graph.object("NetworkLocator");
		graph.property(origin, "GenericPackage.PackageUID", umid('i'));
		graph.edge(origin, "SourcePackage.Descriptor", descriptor);
		graph.edge(descriptor, "GenericDescriptor.Locators", locator);
		graph.property(locator, "NetworkLocator.URLString", url);
		const auto track = graph.object("Track"), clip = graph.object("SourceClip");
		graph.property(clip, "SourceClip.SourcePackageID", umid('i'));
		graph.edge(track, "GenericTrack.Sequence", clip);
		graph.edge(graph.file, "GenericPackage.Tracks", track);
		const auto attribute = graph.object("TaggedValue");
		graph.property(attribute, "TaggedValue.Name", QStringLiteral("UNC Path"));
		graph.property(attribute, "TaggedValue.Value", nativePath);
		graph.edge(graph.file, "GenericPackage.MobAttributeList", attribute);
		const auto result = project(graph);
		QCOMPARE(result.files.size(), 1);
		const auto &evidence = result.files.first().evidence;
		QCOMPARE(evidence.observations(MediaProperty::SourceFilename).size(), 2);
		const auto selected = evidence.resolve(MediaProperty::SourceFilename, [](MetadataSource)
											   { return 1; }, QStringLiteral("Equal source rank"));
		QCOMPARE(selected.value.toString(), expected);
		QCOMPARE(selected.agreement, expected.isEmpty() ? PropertyAgreement::Conflicting : PropertyAgreement::Agreeing);
		QStringList recordedPaths;
		for (const auto &observation : evidence.observations(MediaProperty::SourcePath))
			recordedPaths.append(observation.value.toString());
		QVERIFY(recordedPaths.contains(url));
		QVERIFY(recordedPaths.contains(nativePath));
	}

	void absent_usage_needs_a_complete_owning_set_not_complete_essence()
	{
		Graph graph;
		const auto master = graph.master();
		graph.source.outcome = ParsedSource::Outcome::Incomplete;
		QCOMPARE(value(project(graph).files.first(), MediaProperty::Type).toInt(), 0);
		auto context = QSharedPointer<MxfSetContext>::create(*graph.at(master).mxf);
		context->value.length += 4;
		graph.at(master).mxf = context;
		QVERIFY(!value(project(graph).files.first(), MediaProperty::Type).isValid());
	}

	void unreadable_coding_never_becomes_pcm_by_absence()
	{
		Graph graph("WaveAudioDescriptor");
		graph.property(graph.descriptor, "GenericSoundEssenceDescriptor.SoundEssenceCompression", QByteArray(2, 'x'));
		graph.at(graph.descriptor).properties.last().state = PropertyReadState::Unreadable;
		const auto result = project(graph);
		QVERIFY(!value(result.files.first(), MediaProperty::Codec).isValid());
		QCOMPARE(result.files.first().evidence.observations(MediaProperty::CompressionLabel).first().readState, PropertyReadState::Unreadable);
		const auto status = result.files.first().evidence.readStatus(MediaProperty::Codec, graph.source.snapshot,
			QStringLiteral("object:%1").arg(graph.descriptor));
		QCOMPARE(status.state, PropertyReadState::Unreadable);
		QCOMPARE(status.reason, PropertyReadReason::ValueUnreadable);
	}
	void absent_field_requires_complete_owning_object()
	{
		Graph graph("WaveAudioDescriptor");
		auto result = project(graph);
		const QString owner = QStringLiteral("object:%1").arg(graph.descriptor);
		auto status = result.files.first().evidence.readStatus(MediaProperty::Channels, graph.source.snapshot, owner);
		QCOMPARE(status.state, PropertyReadState::Absent);
		QCOMPARE(status.reason, PropertyReadReason::NotPresentInObject);
		const auto resolution = result.files.first().evidence.readStatus(MediaProperty::Resolution, graph.source.snapshot, owner);
		QCOMPARE(resolution.applicability, PropertyApplicability::NotApplicable);
		auto incomplete = QSharedPointer<MxfSetContext>::create(*graph.at(graph.descriptor).mxf);
		incomplete->value.length += 4;
		graph.at(graph.descriptor).mxf = incomplete;
		result = project(graph);
		status = result.files.first().evidence.readStatus(MediaProperty::Channels, graph.source.snapshot, owner);
		QCOMPARE(status.state, PropertyReadState::NotRead);
		QCOMPARE(status.reason, PropertyReadReason::CoverageNotEstablished);
		QVERIFY(!value(result.files.first(), MediaProperty::Codec).isValid());
		QVERIFY(!value(result.files.first(), MediaProperty::SampleFormat).isValid());
	}
	void unsupported_numeric_format_retains_inputs_without_claiming_absence()
	{
		Graph graph;
		graph.property(graph.descriptor, "CDCIEssenceDescriptor.ComponentDepth", 252u);
		const auto result = project(graph);
		const auto status = result.files.first().evidence.readStatus(MediaProperty::BitDepth, graph.source.snapshot,
			QStringLiteral("object:%1").arg(graph.descriptor));
		QCOMPARE(status.state, PropertyReadState::NotRead);
		QCOMPARE(status.reason, PropertyReadReason::UnsupportedInterpretation);
		QCOMPARE(result.files.first().evidence.observations(MediaProperty::ComponentDepth).first().value.toUInt(), 252u);
		QVERIFY(!result.files.first().evidence.observations(MediaProperty::ComponentDepth).first().rawValue.toByteArray().isEmpty());
	}

	void precompute_category_requires_the_established_master_and_readable_attributes()
	{
		Graph graph;
		const auto master = graph.master();
		graph.property(master, "GenericPackage.AppCode", 1);
		auto result = project(graph);
		QCOMPARE(value(result.files.first(), MediaProperty::Type).toInt(), 1);
		QCOMPARE(value(result.files.first(), MediaProperty::PrecomputeCategory).toInt(), 1);
		graph.property(master, "GenericPackage.MobAttributeList", QByteArray(16, 'z'));
		result = project(graph);
		QCOMPARE(value(result.files.first(), MediaProperty::Type).toInt(), 1);
		QVERIFY(!value(result.files.first(), MediaProperty::PrecomputeCategory).isValid());
	}

	void cancelled_projection_is_empty()
	{
		Graph graph;
		Cancellation cancellation;
		cancellation.cancel();
		QVERIFY(projectMxf(graph.source, cancellation).files.isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestCanonMxfProjection)
#include "tst_canon_mxfprojection.moc"
