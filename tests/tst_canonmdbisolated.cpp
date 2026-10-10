// This target links only MDB's reader and projector implementations. A genuine
// database with copied audio summaries proves that OMF media support is optional.

#include "canon/mdbreader.h"
#include "canon/projection.h"

#include <QFile>
#include <QMap>
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

	bool hasAudioRate(const Canon::ProjectedFile &file)
	{
		for (const auto &observation : file.evidence.observations(MediaProperty::SampleRate))
		{
			const auto rate = Canon::mediaRate(observation.value);
			if (observation.eligible && observation.readState == PropertyReadState::Present &&
				rate.numerator == 48000 && rate.denominator == 1)
				return true;
		}
		return false;
	}
}

class TestCanonMdbIsolated : public QObject
{
	Q_OBJECT
private slots:
	void genuineAudioDatabase();
};

void TestCanonMdbIsolated::genuineAudioDatabase()
{
	QFile input(QStringLiteral(FIXTURES_DIR "/omf/mc2026_audio/msmMMOB.mdb"));
	QVERIFY2(input.open(QIODevice::ReadOnly), qPrintable(input.errorString()));
	Canon::Cancellation cancellation;
	const auto source = Canon::MdbReader{}.read(input, {{}, cancellation});
	QCOMPARE(source.outcome, Canon::ParsedSource::Outcome::Complete);
	QCOMPARE(source.container, Canon::ParsedSource::Container::Bento);
	QVERIFY(source.snapshot);
	QCOMPARE(source.snapshot->source, MetadataSource::Mdb);
	QVERIFY(source.embeddedSources.isEmpty());

	QSet<Canon::ObjectHandle> handles;
	int summaryCount = 0, precisionCount = 0;
	for (const auto &object : source.objects)
	{
		handles.insert(object.handle);
		QCOMPARE(object.snapshot, source.snapshot);
		for (const auto &property : object.properties)
		{
			if (property.locator.name != QLatin1String("OMFI:WAVD:Summary") &&
				property.locator.name != QLatin1String("OMFI:AIFD:Summary"))
				continue;
			++summaryCount;
			QCOMPARE(property.state, PropertyReadState::Present);
			QVERIFY(property.bento);
			QCOMPARE(property.bento->typeName, QStringLiteral("omfi:VarLenBytes"));
			QVERIFY(property.bytesRetained && !property.encoding.isEmpty());
			QByteArray recorded;
			for (const auto &range : property.locator.ranges)
			{
				QVERIFY(input.seek(range.offset));
				recorded += input.read(range.length);
			}
			QCOMPARE(recorded, property.encoding);
			const auto precisionName = property.locator.name +
				(property.encoding.startsWith("RIFF") ? QStringLiteral(".fmt .wBitsPerSample") : QStringLiteral(".COMM.sampleSize"));
			for (const auto &field : object.properties)
				if (field.locator.name == precisionName)
				{
					++precisionCount;
					QCOMPARE(field.state, PropertyReadState::Present);
					QCOMPARE(field.decoded.toInt(), 24);
					QCOMPARE(field.bento, property.bento);
				}
		}
	}
	// This database has three retained descriptor summaries for two file rows;
	// preserving the source graph must not collapse those repeated observations.
	QCOMPARE(summaryCount, 3);
	QCOMPARE(precisionCount, 3);
	QVERIFY(!source.relationships.isEmpty());
	bool descriptorRelationship = false;
	for (const auto &relationship : source.relationships)
		if (relationship.locator.name == QLatin1String("OMFI:MOBJ:PhysicalMedia"))
		{
			descriptorRelationship = true;
			QVERIFY(handles.contains(relationship.origin));
			QVERIFY(handles.contains(relationship.target));
			QVERIFY(!relationship.locator.ranges.isEmpty());
		}
	QVERIFY(descriptorRelationship);

	const auto projection = Canon::projectMdb(source, cancellation);
	QCOMPARE(projection.files.size(), 2);
	// These identities were independently pinned against the companion PMR's
	// Unicode records; this test does not invoke its reader or the OMF reader.
	const QMap<QString, QString> masters{
		{QStringLiteral("060a2b3401010101.01010f0013000000.7429976a70397047.060e2b347f7f2a80"),
		 QStringLiteral("060a2b3401010101.01010f0013000000.7429976a4e397047.060e2b347f7f2a80")},
		{QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3ec57047.060e2b347f7f2a80"),
		 QStringLiteral("060a2b3401010101.01010f0013000000.9729976a3dc57047.060e2b347f7f2a80")}};
	QSet<QString> identities;
	for (const auto &file : projection.files)
	{
		QVERIFY(masters.contains(file.fileMobId));
		QVERIFY(!identities.contains(file.fileMobId));
		identities.insert(file.fileMobId);
		QVERIFY(file.masterMobIds.contains(masters.value(file.fileMobId)));
		QVERIFY(hasValue(file, MediaProperty::BitDepth, QStringLiteral("24-bit")));
		QVERIFY(hasValue(file, MediaProperty::Channels, 1));
		QVERIFY(hasAudioRate(file));
		QVERIFY(!file.objects.isEmpty());
		for (const auto &reference : file.objects)
		{
			QCOMPARE(reference.source, source.snapshot);
			QVERIFY(handles.contains(reference.handle));
		}
		const auto &identityEvidence = file.evidence.observations(MediaProperty::FileMobId);
		QVERIFY(!identityEvidence.isEmpty());
		for (const auto &observation : identityEvidence)
		{
			QCOMPARE(observation.snapshot, source.snapshot);
			QVERIFY(!observation.rawValue.toByteArray().isEmpty());
		}
	}
}

QTEST_GUILESS_MAIN(TestCanonMdbIsolated)
#include "tst_canonmdbisolated.moc"
