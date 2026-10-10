// Genuine Avid database/header joins. Read each source independently and verify
// matching identities, shared facts and explicitly recorded differences.
// No source is an oracle for another source's bytes or clock.
#include "mediaengine/mdbreader.h"
#include "mediaengine/mxfreader.h"
#include "mediaengine/omfreader.h"
#include "mediaengine/pmrreader.h"
#include "mediaengine/projection.h"
#include "mediaengine/scancoordinator.h"
#include "pmrkey.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QtEndian>
#include <algorithm>

namespace
{
	MediaEngine::ParsedSource read(const MediaEngine::SourceReader &reader, const QString &path, MetadataSource kind)
	{
		QFile input(path);
		input.open(QIODevice::ReadOnly);
		const auto receipt = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			kind, path, QFileInfo(path).lastModified(), SourceReadState::NotRead});
		const MediaEngine::Cancellation cancellation;
		return reader.read(input, {receipt, cancellation});
	}

	const MediaEngine::ProjectedFile *findFile(const QVector<MediaEngine::ProjectedFile> &files, const QString &identity)
	{
		const auto match = std::find_if(files.cbegin(), files.cend(), [&](const auto &file)
									  { return file.fileMobId == identity; });
		return match == files.cend() ? nullptr : &*match;
	}

	const MediaEngine::ProjectedFile *findMaster(const QVector<MediaEngine::ProjectedFile> &masters, const QString &identity)
	{
		const auto match = std::find_if(masters.cbegin(), masters.cend(), [&](const auto &master)
									  { return master.masterMobIds.contains(identity); });
		return match == masters.cend() ? nullptr : &*match;
	}

	bool hasRaw(const MediaEngine::ParsedSource &source, const MediaEngine::ProjectedFile &file,
				const char *name, const QByteArray &bytes)
	{
		return std::any_of(source.objects.cbegin(), source.objects.cend(), [&](const auto &object)
		{
			const bool linked = std::any_of(file.objects.cbegin(), file.objects.cend(), [&](const auto &reference)
										   { return reference.handle == object.handle; });
			return linked && std::any_of(object.properties.cbegin(), object.properties.cend(), [&](const auto &property)
			{
				return property.locator.name == QLatin1String(name) && property.state == PropertyReadState::Present && property.encoding == bytes;
			});
		});
	}

	QByteArray rateBytes(MediaRate rate, bool bigEndian)
	{
		QByteArray result(8, '\0');
		if (bigEndian)
		{
			qToBigEndian(rate.numerator, result.data());
			qToBigEndian(rate.denominator, result.data() + 4);
		}
		else
		{
			qToLittleEndian(rate.numerator, result.data());
			qToLittleEndian(rate.denominator, result.data() + 4);
		}
		return result;
	}

}

class TestReaderRegressions final : public QObject
{
	Q_OBJECT
private slots:
	void pmrFilenameSpellingsMatch();
	void genuineDatabaseHeaderJoins_data();
	void genuineDatabaseHeaderJoins();
};

void TestReaderRegressions::pmrFilenameSpellingsMatch()
{
	QCOMPARE(PmrKey::primary(QStringLiteral("MyClip.MXF")), QStringLiteral("myclip.mxf"));
	const QString decomposed = QString::fromUtf8("cafe\xCC\x81.mxf");
	const QString composed = QString::fromUtf8("caf\xC3\xA9.mxf");
	QCOMPARE(PmrKey::primary(decomposed), composed);
	QCOMPARE(PmrKey::primary(decomposed), PmrKey::primary(composed));
}

void TestReaderRegressions::genuineDatabaseHeaderJoins_data()
{
	QTest::addColumn<QString>("folder");
	QTest::addColumn<QString>("pmrName");
	QTest::addColumn<QString>("mdbName");
	QTest::addColumn<int>("records");
	QTest::addColumn<bool>("omf");
	QTest::newRow("mxf-rounds-1-2") << QStringLiteral("corpus_headers") << QStringLiteral("msmFMID.pmr") << QStringLiteral("msmMMOB.mdb") << 435 << false;
	QTest::newRow("mxf-round-3") << QStringLiteral("corpus_headers") << QStringLiteral("msmFMID_round3.pmr") << QStringLiteral("msmMMOB_round3.mdb") << 360 << false;
	QTest::newRow("omf-video-slates") << QStringLiteral("omf/avid_supporting") << QStringLiteral("msmFMID.pmr") << QStringLiteral("msmMMOB.mdb") << 80 << true;
	QTest::newRow("legacy-audio") << QStringLiteral("omf/mc2026_audio") << QStringLiteral("msmFMID.pmr") << QStringLiteral("msmMMOB.mdb") << 2 << true;
}

void TestReaderRegressions::genuineDatabaseHeaderJoins()
{
	QFETCH(QString, folder);
	QFETCH(QString, pmrName);
	QFETCH(QString, mdbName);
	QFETCH(int, records);
	QFETCH(bool, omf);
	const QDir directory(QStringLiteral(FIXTURES_DIR "/") + folder);
	const MediaEngine::Cancellation cancellation;
	const auto pmr = read(MediaEngine::PmrReader{}, directory.filePath(pmrName), MetadataSource::Pmr);
	const auto mdb = read(MediaEngine::MdbReader{}, directory.filePath(mdbName), MetadataSource::Mdb);
	QCOMPARE(pmr.outcome, MediaEngine::ParsedSource::Outcome::Complete);
	QCOMPARE(mdb.outcome, MediaEngine::ParsedSource::Outcome::Complete);
	const auto filenames = MediaEngine::projectPmr(pmr, cancellation);
	const auto database = MediaEngine::projectMdb(mdb, cancellation);
	QCOMPARE(pmr.recordSets.size(), 2);
	QCOMPARE(pmr.recordSets.last().pmrFileSet, MediaEngine::PmrFileSet::Unicode);
	QCOMPARE(pmr.recordSets.last().objects.size(), records);
	int joined = 0;
	int missingCompression = 0;
	int recordedRateDifferences = 0;
	int compressionDifferences = 0;
	int mpegOwnership = 0;
	for (const auto &record : filenames.files)
	{
		QVERIFY(!record.objects.isEmpty());
		if (!pmr.recordSets.last().objects.contains(record.objects.first().handle))
			continue; // Test the recorded Unicode set once, not both spelling sets.
		QCOMPARE(record.filenames.size(), 1);
		QCOMPARE(record.masterMobIds.size(), 1);
		const QString path = directory.filePath(record.filenames.first());
		QVERIFY2(QFile::exists(path), qPrintable(path));
		const auto *file = findFile(database.files, record.fileMobId);
		const auto *master = findMaster(database.masters, record.masterMobIds.first());
		QVERIFY2(file, qPrintable(path + QStringLiteral(": MDB file identity")));
		QVERIFY2(master, qPrintable(path + QStringLiteral(": MDB master identity")));
		MediaEvidence databaseEvidence = file->evidence;
		MediaEngine::appendEvidence(databaseEvidence, master->evidence);
		MediaEngine::selectMetadata(databaseEvidence);
		const auto source = omf ? read(MediaEngine::OmfReader{}, path, MetadataSource::Omf)
								: read(MediaEngine::MxfReader{}, path, MetadataSource::Mxf);
		QVERIFY2(source.outcome == MediaEngine::ParsedSource::Outcome::Complete || (!omf && source.outcome == MediaEngine::ParsedSource::Outcome::Incomplete),
				 qPrintable(path + ": " + source.diagnostics.join('\n')));
		const auto header = omf ? MediaEngine::projectOmf(source, cancellation) : MediaEngine::projectMxf(source, cancellation);
		QCOMPARE(header.files.size(), 1);
		if (omf && record.filenames.first().endsWith(QLatin1String("_MPEG50.omf")))
		{
			// Avid's private MPEG class records the same root membership and
			// MDAT identity property as its standard media-data classes.
			bool retainedRootIdentity = false;
			for (const auto &object : source.objects)
			{
				const bool mpeg = std::any_of(object.properties.cbegin(), object.properties.cend(), [](const auto &property)
				{
					return property.locator.name == QLatin1String("OMFI:ObjID") && property.encoding == "MPEG";
				});
				if (!mpeg)
					continue;
				const bool listed = std::any_of(source.relationships.cbegin(), source.relationships.cend(), [&](const auto &edge)
				{
					return edge.origin == 1 && edge.target == object.handle && edge.locator.name == QLatin1String("OMFI:ObjectSpine");
				});
				const bool indexed = std::any_of(source.relationships.cbegin(), source.relationships.cend(), [&](const auto &edge)
				{
					return edge.origin == 1 && edge.target == object.handle && edge.locator.name == QLatin1String("OMFI:MediaData");
				});
				for (const auto &property : object.properties)
					if (listed && indexed && property.locator.name == QLatin1String("OMFI:MDAT:MobID") && property.bento)
						retainedRootIdentity = MediaEngine::canonicalDatabaseId(property.encoding, property.bento->metadataBigEndian.value_or(false)) == record.fileMobId;
			}
			QVERIFY2(retainedRootIdentity, qPrintable(path));
			++mpegOwnership;
		}
		QCOMPARE(header.files.first().fileMobId, record.fileMobId);
		QVERIFY2(header.files.first().masterMobIds.contains(record.masterMobIds.first()), qPrintable(path + QStringLiteral(": header master identity")));
		MediaEvidence headerEvidence = header.files.first().evidence;
		MediaEngine::selectMetadata(headerEvidence);
		QCOMPARE(databaseEvidence.selected(MediaProperty::ClipName).value, headerEvidence.selected(MediaProperty::ClipName).value);
		QCOMPARE(databaseEvidence.selected(MediaProperty::Kind).value, headerEvidence.selected(MediaProperty::Kind).value);
		QCOMPARE(databaseEvidence.selected(MediaProperty::Type).value, headerEvidence.selected(MediaProperty::Type).value);
		const auto compression = databaseEvidence.selected(MediaProperty::Compression).value;
		if (!compression.isValid() || compression.toString().isEmpty())
		{
			if (!omf)
			{
				// This real MPEG audio descriptor has no recorded MDB coding label.
				QCOMPARE(record.filenames.first(), QStringLiteral("A01.E68C35B3_2C34B2C34B61AA.mxf"));
				QCOMPARE(headerEvidence.selected(MediaProperty::Compression).value.toString(), QStringLiteral("MP2"));
			}
			else
			{
				// These three DV100 slates retain typed private identifiers;
				// no verified compression-name mapping currently covers them.
				QVERIFY(record.filenames.first().endsWith(QLatin1String("_1280x720x1_DV100_90.omf")));
				QVERIFY(!headerEvidence.selected(MediaProperty::Compression).value.isValid());
				QVERIFY(hasRaw(source, header.files.first(), "OMFI:DIDD:Compression", QByteArray("DV/C\0", 5)));
				QVERIFY(hasRaw(source, header.files.first(), "OMFI:DIDD:DIDResolutionID", QByteArray::fromHex("c6090000")));
			}
			++missingCompression;
		}
		for (const auto property : {MediaProperty::Compression, MediaProperty::Resolution, MediaProperty::FrameRate,
								   MediaProperty::BitDepth, MediaProperty::SampleRate, MediaProperty::Channels})
		{
			const auto fromDatabase = databaseEvidence.selected(property).value;
			const auto fromHeader = headerEvidence.selected(property).value;
			const QByteArray context = (record.filenames.first() + ": " + mediaPropertyName(property)).toUtf8();
			if (property == MediaProperty::FrameRate && fromDatabase != fromHeader)
			{
				// MDB actually stores rounded fractions. They are not equal to
				// MXF's exact fractions; neither original rate is rewritten.
				QCOMPARE(folder, QStringLiteral("corpus_headers"));
				const auto databaseRate = MediaEngine::mediaRate(fromDatabase), headerRate = MediaEngine::mediaRate(fromHeader);
				QVERIFY2((databaseRate.sameRate({2997, 100}) && headerRate.sameRate({30000, 1001})) ||
						 (databaseRate.sameRate({23976, 1000}) && headerRate.sameRate({24000, 1001})), context.constData());
				QVERIFY(hasRaw(mdb, *file, "OMFI:MDFL:SampleRate", rateBytes(databaseRate, false)));
				QVERIFY(hasRaw(source, header.files.first(), "FileDescriptor.SampleRate", rateBytes(headerRate, true)));
				++recordedRateDifferences;
			}
			else if (property == MediaProperty::Compression && fromDatabase != fromHeader && fromDatabase.isValid())
			{
				if (omf)
				{
					// The regenerated MDB explicitly describes uncompressed media;
					// the original OMF stores an unsupported DV/C identifier.
					QVERIFY(record.filenames.first().endsWith(QLatin1String("_1920x540x2_DV100_115.omf")));
					QCOMPARE(fromDatabase.toString(), QStringLiteral("1:1 YCbCr 8bit"));
					QVERIFY(!fromHeader.isValid());
					QVERIFY(hasRaw(mdb, *file, "OMFI:DIDD:Compression", QByteArray("MXF1\0", 5)));
					QVERIFY(hasRaw(mdb, *file, "OMFI:DIDD:EssenceCompression", QByteArray::fromHex("0102010400000000060e2b3404010101")));
					QVERIFY(hasRaw(source, header.files.first(), "OMFI:DIDD:Compression", QByteArray("DV/C\0", 5)));
					QVERIFY(hasRaw(source, header.files.first(), "OMFI:DIDD:DIDResolutionID", QByteArray::fromHex("c4090000")));
				}
				else
				{
					// Four J2K HD specimens retain rounded MDB rates, so the MDB
					// cannot qualify the same exact HD operating point as their MXF.
					QCOMPARE(records, 360);
					QCOMPARE(fromDatabase.toString(), QStringLiteral("JPEG2000"));
					QCOMPARE(fromHeader.toString(), QStringLiteral("J2K HD"));
					const auto coding = QByteArray::fromHex("060e2b34040101070401020203010100");
					QCOMPARE(databaseEvidence.selected(MediaProperty::CompressionLabel).value.toByteArray(), coding);
					QCOMPARE(headerEvidence.selected(MediaProperty::CompressionLabel).value.toByteArray(), coding);
					QVERIFY(hasRaw(mdb, *file, "OMFI:DIDD:Compression", QByteArray("J2KT\0", 5)));
				}
				++compressionDifferences;
			}
			else if (property != MediaProperty::Compression || compression.isValid())
				QVERIFY2(fromDatabase == fromHeader, context.constData());
		}
		const auto databaseDuration = MediaEngine::mediaDuration(databaseEvidence.selected(MediaProperty::FileDuration).value);
		const auto headerDuration = MediaEngine::mediaDuration(headerEvidence.selected(MediaProperty::FileDuration).value);
		QCOMPARE(databaseDuration.displayFrames(), headerDuration.displayFrames());
		++joined;
	}
	QCOMPARE(joined, records);
	QCOMPARE(missingCompression, records == 435 ? 1 : records == 80 ? 3 : 0);
	QCOMPARE(recordedRateDifferences, records == 435 ? 45 : records == 360 ? 113 : 0);
	QCOMPARE(compressionDifferences, records == 360 ? 4 : records == 80 ? 3 : 0);
	QCOMPARE(mpegOwnership, records == 80 ? 6 : 0);
}

QTEST_GUILESS_MAIN(TestReaderRegressions)
#include "tst_readerregressions.moc"
