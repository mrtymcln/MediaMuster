// This target links only OMF's reader and projector implementations. Genuine
// OMF video and Avid WAV/AIF files exercise relationships and deferred samples
// without loading or linking an MDB reader.

#include "canon/omfreader.h"
#include "canon/projection.h"

#include <QFile>
#include <QSet>
#include <QTest>

namespace
{
	bool hasValue(const Canon::ProjectedFile &file, MediaProperty field, const QVariant &expected)
	{
		for (const auto &observation : file.evidence.observations(field))
			if (observation.eligible && observation.readState == PropertyReadState::Present && observation.value == expected)
				return true;
		return false;
	}
}

class TestCanonOmfIsolated : public QObject
{
	Q_OBJECT
private slots:
	void genuineMedia_data();
	void genuineMedia();
};

void TestCanonOmfIsolated::genuineMedia_data()
{
	QTest::addColumn<QString>("path");
	QTest::addColumn<int>("container");
	QTest::addColumn<QString>("fileId");
	QTest::addColumn<QString>("masterId");
	QTest::addColumn<QString>("compression");
	using Container = Canon::ParsedSource::Container;
	QTest::newRow("OMF-DV-PAL") << QStringLiteral("omf/avid_supporting/BLACK_720x576x1_DV420.omf")
		<< int(Container::Omf) << QString{} << QString{} << QStringLiteral("IEC-DV PAL 25Mbps 4:2:0");
	QTest::newRow("Avid-WAVE") << QStringLiteral("omf/mc2026_audio/TONE_100A01.6A972974.039700.wav")
		<< int(Container::Wave)
		<< QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80")
		<< QStringLiteral("060a2b3401010101.01010f0013000000.7429976a4e397047.060e2b347f7f2a80")
		<< QStringLiteral("PCM");
	QTest::newRow("Avid-AIFC") << QStringLiteral("omf/mc2026_audio/TONE_100A01.6A972997.0C53E0.aif")
		<< int(Container::Aiff)
		<< QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3ec57047.060e2b347f7f2a80")
		<< QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3dc57047.060e2b347f7f2a80")
		<< QStringLiteral("PCM");
}

void TestCanonOmfIsolated::genuineMedia()
{
	QFETCH(QString, path);
	QFETCH(int, container);
	QFETCH(QString, fileId);
	QFETCH(QString, masterId);
	QFETCH(QString, compression);
	QFile input(QStringLiteral(FIXTURES_DIR) + '/' + path);
	QVERIFY2(input.open(QIODevice::ReadOnly), qPrintable(input.errorString()));
	Canon::Cancellation cancellation;
	const auto source = Canon::OmfReader{}.read(input, {{}, cancellation});
	QCOMPARE(source.outcome, Canon::ParsedSource::Outcome::Complete);
	QCOMPARE(int(source.container), container);
	QVERIFY(source.snapshot);
	QCOMPARE(source.snapshot->source, MetadataSource::Omf);
	const bool audio = container != int(Canon::ParsedSource::Container::Omf);
	if (audio)
		QCOMPARE(source.embeddedSources.size(), 1);
	const auto &graph = audio ? source.embeddedSources.first() : source;
	QCOMPARE(graph.outcome, Canon::ParsedSource::Outcome::Complete);
	QCOMPARE(graph.container, Canon::ParsedSource::Container::Omf);
	QCOMPARE(graph.snapshot->source, MetadataSource::Omf);
	QSet<Canon::ObjectHandle> handles;
	bool typedIdentity = false, deferredSamples = false;
	for (const auto &object : graph.objects)
	{
		handles.insert(object.handle);
		QCOMPARE(object.snapshot, graph.snapshot);
		for (const auto &property : object.properties)
		{
			if (property.locator.name == QLatin1String("OMFI:MOBJ:MobID"))
			{
				typedIdentity = true;
				QVERIFY(property.bento);
				QCOMPARE(property.bento->typeName, QStringLiteral("omfi:UID"));
				QVERIFY(!property.encoding.isEmpty());
			}
			if (property.locator.name == QLatin1String("OMFI:IDAT:ImageData") ||
				property.locator.name == QLatin1String("OMFI:WAVE:Data") ||
				property.locator.name == QLatin1String("OMFI:AIFC:Data"))
			{
				deferredSamples = true;
				QVERIFY(!property.bytesRetained);
				QVERIFY(property.encoding.isEmpty());
				QVERIFY(!property.locator.ranges.isEmpty());
			}
		}
	}
	QVERIFY(typedIdentity && deferredSamples);
	QVERIFY(!graph.relationships.isEmpty());
	bool descriptorRelationship = false;
	for (const auto &relationship : graph.relationships)
		if (relationship.locator.name == QLatin1String("OMFI:MOBJ:PhysicalMedia"))
		{
			descriptorRelationship = true;
			QVERIFY(handles.contains(relationship.origin));
			QVERIFY(handles.contains(relationship.target));
			QVERIFY(!relationship.locator.ranges.isEmpty());
		}
	QVERIFY(descriptorRelationship);

	const auto projection = Canon::projectOmf(source, cancellation);
	QCOMPARE(projection.files.size(), 1);
	const auto &file = projection.files.first();
	QVERIFY(!file.fileMobId.isEmpty());
	QVERIFY(hasValue(file, MediaProperty::Compression, compression));
	QVERIFY(!file.objects.isEmpty());
	QVERIFY(!file.evidence.observations(MediaProperty::FileMobId).isEmpty());
	if (audio)
	{
		// Identity expectations come from the companion PMR's actual bytes.
		QCOMPARE(file.fileMobId, fileId);
		QVERIFY(file.masterMobIds.contains(masterId));
		QVERIFY(hasValue(file, MediaProperty::Channels, 1));
		QVERIFY(hasValue(file, MediaProperty::BitDepth, QStringLiteral("24-bit")));
		bool rate48000 = false;
		for (const auto &observation : file.evidence.observations(MediaProperty::SampleRate))
		{
			const auto rate = Canon::mediaRate(observation.value);
			rate48000 |= observation.eligible && observation.readState == PropertyReadState::Present &&
				rate.numerator == 48000 && rate.denominator == 1;
		}
		QVERIFY(rate48000);
	}
	else
		QVERIFY(hasValue(file, MediaProperty::Resolution, QStringLiteral("720x576")));
}

QTEST_GUILESS_MAIN(TestCanonOmfIsolated)
#include "tst_canonomfisolated.moc"
