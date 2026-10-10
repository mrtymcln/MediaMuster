// Checks the RAM archive against genuine reader output, including source shape,
// raw bytes, typed values and downstream interpretation. Authored value controls
// below test serialization only; they make no claim about an Avid file format.

#include "mediaengine/sourcearchive.h"
#include "mediaengine/avbreader.h"
#include "mediaengine/avbreferences.h"
#include "mediaengine/omfreader.h"
#include "mediaengine/mdbreader.h"
#include "mediaengine/mxfreader.h"
#include "mediaengine/pmrreader.h"
#include "mediaengine/projection.h"
#include "mediaengine/scancoordinator.h"

#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QTest>
#include <cstring>
#include <exception>
#include <limits>
#include <thread>

struct UnstreamableArchiveValue
{
	int value = 0;
};
Q_DECLARE_METATYPE(UnstreamableArchiveValue)

namespace
{
	void compareBytes(const QByteArray &left, const QByteArray &right)
	{
		QCOMPARE(left, right);
		QCOMPARE(left.isNull(), right.isNull());
	}
	void compareText(const QString &left, const QString &right)
	{
		QCOMPARE(left, right);
		QCOMPARE(left.isNull(), right.isNull());
	}
	void compareVariant(const QVariant &left, const QVariant &right)
	{
		QCOMPARE(left.metaType().id(), right.metaType().id());
		QCOMPARE(left.isValid(), right.isValid());
		QCOMPARE(left.isNull(), right.isNull());
		if (!left.isValid())
			return;
		switch (left.metaType().id())
		{
		case QMetaType::QVariantList:
		{
			const auto a = left.toList(), b = right.toList();
			QCOMPARE(a.size(), b.size());
			for (qsizetype i = 0; i < a.size(); ++i)
				compareVariant(a[i], b[i]);
			return;
		}
		case QMetaType::QVariantMap:
		{
			const auto a = left.toMap(), b = right.toMap();
			QCOMPARE(a.keys(), b.keys());
			for (auto it = a.cbegin(); it != a.cend(); ++it)
				compareVariant(it.value(), b.value(it.key()));
			return;
		}
		case QMetaType::QVariantHash:
		{
			const auto a = left.toHash(), b = right.toHash();
			QCOMPARE(a.size(), b.size());
			for (auto it = a.cbegin(); it != a.cend(); ++it)
			{
				QVERIFY(b.contains(it.key()));
				compareVariant(it.value(), b.value(it.key()));
			}
			return;
		}
		case QMetaType::Float:
			QVERIFY(std::memcmp(left.constData(), right.constData(), sizeof(float)) == 0);
			return;
		case QMetaType::Double:
			QVERIFY(std::memcmp(left.constData(), right.constData(), sizeof(double)) == 0);
			return;
		case QMetaType::QByteArray:
			compareBytes(left.toByteArray(), right.toByteArray());
			return;
		case QMetaType::QString:
			compareText(left.toString(), right.toString());
			return;
		default:
			QCOMPARE(left, right);
		}
	}
	void compareRange(const MediaEngine::ByteRange &a, const MediaEngine::ByteRange &b)
	{
		QCOMPARE(a.offset, b.offset);
		QCOMPARE(a.length, b.length);
	}
	void compareRanges(const QVector<MediaEngine::ByteRange> &a, const QVector<MediaEngine::ByteRange> &b)
	{
		QCOMPARE(a.size(), b.size());
		for (qsizetype i = 0; i < a.size(); ++i)
			compareRange(a[i], b[i]);
	}
	void compareLocator(const MediaEngine::PropertyLocator &a, const MediaEngine::PropertyLocator &b)
	{
		compareText(a.name, b.name);
		compareBytes(a.key, b.key);
		QCOMPARE(a.objectNumber, b.objectNumber);
		compareRanges(a.ranges, b.ranges);
	}
	void compareProperty(const MediaEngine::RawProperty &a, const MediaEngine::RawProperty &b)
	{
		compareLocator(a.locator, b.locator);
		compareBytes(a.encoding, b.encoding);
		compareVariant(a.decoded, b.decoded);
		QCOMPARE(a.state, b.state);
		compareText(a.interpretation, b.interpretation);
		QVERIFY(a.textEncoding == b.textEncoding);
		QVERIFY(a.textEncodingBasis == b.textEncodingBasis);
		QCOMPARE(a.bytesRetained, b.bytesRetained);
		QCOMPARE(bool(a.bento), bool(b.bento));
		if (a.bento && b.bento)
		{
			QCOMPARE(a.bento->property, b.bento->property);
			QCOMPARE(a.bento->type, b.bento->type);
			QCOMPARE(a.bento->generation, b.bento->generation);
			QCOMPARE(a.bento->referenceListObject, b.bento->referenceListObject);
			compareText(a.bento->typeName, b.bento->typeName);
			compareRanges(a.bento->tocRanges, b.bento->tocRanges);
			QVERIFY(a.bento->metadataBigEndian == b.bento->metadataBigEndian);
		}
		QCOMPARE(bool(a.mxf), bool(b.mxf));
		if (a.mxf && b.mxf)
		{
			QCOMPARE(a.mxf->localTag, b.mxf->localTag);
			QCOMPARE(a.mxf->primerOffset, b.mxf->primerOffset);
			compareBytes(a.mxf->mappedAuid, b.mxf->mappedAuid);
			compareText(a.mxf->typeName, b.mxf->typeName);
			compareBytes(a.mxf->framingBytes, b.mxf->framingBytes);
			compareRanges(a.mxf->framingRanges, b.mxf->framingRanges);
		}
	}
	void compareSource(const MediaEngine::ParsedSource &a, const MediaEngine::ParsedSource &b)
	{
		QCOMPARE(a.outcome, b.outcome);
		compareText(a.readReason, b.readReason);
		// Observations/references use receipt identity, not merely equal path text.
		QCOMPARE(a.snapshot.data(), b.snapshot.data());
		QCOMPARE(a.container, b.container);
		QVERIFY(a.omfRevision == b.omfRevision);
		compareLocator(a.embedding, b.embedding);
		QCOMPARE(a.embeddedSources.size(), b.embeddedSources.size());
		for (qsizetype i = 0; i < a.embeddedSources.size(); ++i)
			compareSource(a.embeddedSources[i], b.embeddedSources[i]);
		QCOMPARE(a.recordSets.size(), b.recordSets.size());
		for (qsizetype i = 0; i < a.recordSets.size(); ++i)
		{
			const auto &x = a.recordSets[i], &y = b.recordSets[i];
			compareText(x.name, y.name);
			QVERIFY(x.pmrFileSet == y.pmrFileSet);
			QCOMPARE(x.version, y.version);
			QCOMPARE(x.declaredCount, y.declaredCount);
			QCOMPARE(x.objects, y.objects);
			QCOMPARE(x.framingComplete, y.framingComplete);
		}
		QCOMPARE(a.objects.size(), b.objects.size());
		for (qsizetype i = 0; i < a.objects.size(); ++i)
		{
			const auto &x = a.objects[i], &y = b.objects[i];
			QCOMPARE(x.handle, y.handle);
			QCOMPARE(x.role, y.role);
			QCOMPARE(x.snapshot.data(), y.snapshot.data());
			compareBytes(x.recordedIdentity, y.recordedIdentity);
			compareText(x.identityEncoding, y.identityEncoding);
			QCOMPARE(bool(x.mxf), bool(y.mxf));
			if (x.mxf && y.mxf)
			{
				compareBytes(x.mxf->key, y.mxf->key);
				compareText(x.mxf->name, y.mxf->name);
				QCOMPARE(x.mxf->partitionOffset, y.mxf->partitionOffset);
				compareRange(x.mxf->framing, y.mxf->framing);
				compareRange(x.mxf->value, y.mxf->value);
			}
			QCOMPARE(bool(x.avb), bool(y.avb));
			if (x.avb && y.avb)
			{
				compareBytes(x.avb->classId, y.avb->classId);
				compareRange(x.avb->framing, y.avb->framing);
				compareRange(x.avb->value, y.avb->value);
				QCOMPARE(x.avb->bigEndian, y.avb->bigEndian);
				QCOMPARE(x.avb->interpretationComplete, y.avb->interpretationComplete);
			}
			QCOMPARE(x.properties.size(), y.properties.size());
			for (qsizetype p = 0; p < x.properties.size(); ++p)
				compareProperty(x.properties[p], y.properties[p]);
		}
		QCOMPARE(a.relationships.size(), b.relationships.size());
		for (qsizetype i = 0; i < a.relationships.size(); ++i)
		{
			const auto &x = a.relationships[i], &y = b.relationships[i];
			QCOMPARE(x.origin, y.origin);
			QCOMPARE(x.target, y.target);
			compareLocator(x.locator, y.locator);
			compareVariant(x.recordedReference, y.recordedReference);
			compareText(x.referenceEncoding, y.referenceEncoding);
			QCOMPARE(x.basis, y.basis);
			compareText(x.explanation, y.explanation);
		}
		QCOMPARE(a.unownedProperties.size(), b.unownedProperties.size());
		for (qsizetype i = 0; i < a.unownedProperties.size(); ++i)
			compareProperty(a.unownedProperties[i], b.unownedProperties[i]);
		QCOMPARE(a.diagnostics, b.diagnostics);
	}
	void appendContextPointers(const MediaEngine::ParsedSource &source, QVector<const void *> &pointers)
	{
		const auto appendProperty = [&pointers](const MediaEngine::RawProperty &property) {
			pointers.append(property.bento.data());
			pointers.append(property.mxf.data());
		};
		for (const auto &object : source.objects)
		{
			pointers.append(object.mxf.data());
			pointers.append(object.avb.data());
			for (const auto &property : object.properties)
				appendProperty(property);
		}
		for (const auto &property : source.unownedProperties)
			appendProperty(property);
		for (const auto &child : source.embeddedSources)
			appendContextPointers(child, pointers);
	}
	void compareContextAliases(const MediaEngine::ParsedSource &a, const MediaEngine::ParsedSource &b)
	{
		QVector<const void *> left, right;
		appendContextPointers(a, left);
		appendContextPointers(b, right);
		QCOMPARE(left.size(), right.size());
		QHash<const void *, const void *> forward, reverse;
		for (qsizetype i = 0; i < left.size(); ++i)
		{
			QCOMPARE(left[i] == nullptr, right[i] == nullptr);
			if (!left[i])
				continue;
			// Shared native framing stays shared; distinct contexts stay distinct.
			if (forward.contains(left[i]))
				QCOMPARE(forward.value(left[i]), right[i]);
			if (reverse.contains(right[i]))
				QCOMPARE(reverse.value(right[i]), left[i]);
			forward.insert(left[i], right[i]);
			reverse.insert(right[i], left[i]);
		}
	}
	void compareReadResult(const PropertyReadResult &a, const PropertyReadResult &b)
	{
		QCOMPARE(a.state, b.state);
		QCOMPARE(a.reason, b.reason);
		QCOMPARE(a.applicability, b.applicability);
		compareText(a.explanation.text(), b.explanation.text());
	}
	void compareEvidence(MediaEvidence a, MediaEvidence b)
	{
		const auto &ac = a.sourceCoverage(), &bc = b.sourceCoverage();
		QCOMPARE(ac.size(), bc.size());
		for (qsizetype i = 0; i < ac.size(); ++i)
		{
			QCOMPARE(ac[i].snapshot.data(), bc[i].snapshot.data());
			compareText(ac[i].objectIdentity, bc[i].objectIdentity);
			QCOMPARE(ac[i].defaultReason, bc[i].defaultReason);
			QCOMPARE(ac[i].eligible, bc[i].eligible);
			QCOMPARE(ac[i].freshness, bc[i].freshness);
			QCOMPARE(ac[i].fields.size(), bc[i].fields.size());
			for (auto it = ac[i].fields.cbegin(); it != ac[i].fields.cend(); ++it)
			{
				QVERIFY(bc[i].fields.contains(it.key()));
				compareReadResult(it.value(), bc[i].fields.value(it.key()));
			}
		}
		for (int i = 0; i < int(MediaProperty::Count); ++i)
		{
			const auto property = MediaProperty(i);
			const auto &ao = a.observations(property), &bo = b.observations(property);
			QCOMPARE(ao.size(), bo.size());
			for (qsizetype j = 0; j < ao.size(); ++j)
			{
				const auto &x = ao[j], &y = bo[j];
				QCOMPARE(x.snapshot.data(), y.snapshot.data());
				compareText(x.property, y.property);
				compareText(x.objectIdentity, y.objectIdentity);
				compareVariant(x.value, y.value);
				compareVariant(x.rawValue, y.rawValue);
				QCOMPARE(x.readState, y.readState);
				QCOMPARE(x.readReason, y.readReason);
				QCOMPARE(x.basis, y.basis);
				QCOMPARE(x.freshness, y.freshness);
				QCOMPARE(x.eligible, y.eligible);
				compareText(x.explanation, y.explanation);
				QVERIFY(x.textEncoding == y.textEncoding);
				QVERIFY(x.textEncodingBasis == y.textEncodingBasis);
			}
			compareReadResult(a.readStatus(property), b.readStatus(property));
		}
		MediaEngine::selectMetadata(a);
		MediaEngine::selectMetadata(b);
		for (int i = 0; i < int(MediaProperty::Count); ++i)
		{
			const auto x = a.selected(MediaProperty(i)), y = b.selected(MediaProperty(i));
			compareVariant(x.value, y.value);
			QCOMPARE(x.readState, y.readState);
			QCOMPARE(x.agreement, y.agreement);
			QCOMPARE(x.selectedObservation, y.selectedObservation);
			compareText(x.rule, y.rule);
			compareText(x.reason.text(), y.reason.text());
			QCOMPARE(x.readReason, y.readReason);
			QCOMPARE(x.applicability, y.applicability);
		}
	}
	void compareProjectedFiles(const QVector<MediaEngine::ProjectedFile> &a, const QVector<MediaEngine::ProjectedFile> &b)
	{
		QCOMPARE(a.size(), b.size());
		for (qsizetype i = 0; i < a.size(); ++i)
		{
			compareText(a[i].fileMobId, b[i].fileMobId);
			QCOMPARE(a[i].masterMobIds, b[i].masterMobIds);
			QCOMPARE(a[i].filenames, b[i].filenames);
			QCOMPARE(a[i].objects.size(), b[i].objects.size());
			for (qsizetype j = 0; j < a[i].objects.size(); ++j)
			{
				QCOMPARE(a[i].objects[j].source.data(), b[i].objects[j].source.data());
				QCOMPARE(a[i].objects[j].handle, b[i].objects[j].handle);
			}
			compareEvidence(a[i].evidence, b[i].evidence);
		}
	}
	MediaEngine::Projection project(const MediaEngine::ParsedSource &source, const MediaEngine::Cancellation &cancellation)
	{
		if (source.container == MediaEngine::ParsedSource::Container::Pmr)
			return MediaEngine::projectPmr(source, cancellation);
		if (source.container == MediaEngine::ParsedSource::Container::Mxf)
			return MediaEngine::projectMxf(source, cancellation);
		if (source.snapshot && source.snapshot->source == MetadataSource::Mdb)
			return MediaEngine::projectMdb(source, cancellation);
		return MediaEngine::projectOmf(source, cancellation);
	}
	void compareAvbResolution(const MediaEngine::ParsedSource &a, const MediaEngine::ParsedSource &b,
							  const MediaEngine::Cancellation &cancellation)
	{
		const MediaEngine::AvbReferenceIndex ai({QSharedPointer<MediaEngine::ParsedSource>::create(a)}, cancellation);
		const MediaEngine::AvbReferenceIndex bi({QSharedPointer<MediaEngine::ParsedSource>::create(b)}, cancellation);
		QCOMPARE(ai.sequences().size(), bi.sequences().size());
		for (qsizetype i = 0; i < ai.sequences().size(); ++i)
		{
			const auto &x = ai.sequences()[i], &y = bi.sequences()[i];
			QVERIFY(x.key == y.key);
			compareText(x.name, y.name);
			compareBytes(x.mobId, y.mobId);
			QCOMPARE(x.userPlaced, y.userPlaced);
			compareLocator(x.membership, y.membership);
		}
		const QVector<MediaEngine::AvbScope> scopes{{0, MediaEngine::AvbScope::Kind::EntireBin, {}}};
		const auto x = ai.resolve(scopes, cancellation), y = bi.resolve(scopes, cancellation);
		QCOMPARE(x.complete, y.complete);
		QCOMPARE(x.cancelled, y.cancelled);
		QVERIFY(x.roots == y.roots);
		QCOMPARE(x.edges.size(), y.edges.size());
		for (qsizetype i = 0; i < x.edges.size(); ++i)
		{
			QVERIFY(x.edges[i].origin == y.edges[i].origin);
			QVERIFY(x.edges[i].target == y.edges[i].target);
			QCOMPARE(x.edges[i].relationship, y.edges[i].relationship);
		}
		QCOMPARE(x.media.size(), y.media.size());
		for (qsizetype i = 0; i < x.media.size(); ++i)
		{
			QVERIFY(x.media[i].locator == y.media[i].locator);
			QCOMPARE(x.media[i].property, y.media[i].property);
			compareBytes(x.media[i].mobId, y.media[i].mobId);
			compareBytes(x.media[i].legacyId, y.media[i].legacyId);
		}
		QCOMPARE(x.terminals.size(), y.terminals.size());
		for (qsizetype i = 0; i < x.terminals.size(); ++i)
		{
			QVERIFY(x.terminals[i].object == y.terminals[i].object);
			QCOMPARE(x.terminals[i].relationship, y.terminals[i].relationship);
			QCOMPARE(x.terminals[i].kind, y.terminals[i].kind);
		}
		QCOMPARE(x.issues.size(), y.issues.size());
		for (qsizetype i = 0; i < x.issues.size(); ++i)
		{
			QCOMPARE(x.issues[i].kind, y.issues[i].kind);
			QVERIFY(x.issues[i].object == y.issues[i].object);
			compareLocator(x.issues[i].property, y.issues[i].property);
			compareText(x.issues[i].explanation, y.issues[i].explanation);
		}
	}
}

class TestSourceArchive final : public QObject
{
	Q_OBJECT
private slots:
	void genuine_reader_output_round_trips_data()
	{
		QTest::addColumn<QString>("relative");
		QTest::addColumn<int>("reader");
		QTest::newRow("PMR") << QStringLiteral("msmFMID.pmr") << 0;
		QTest::newRow("legacy-PMR") << QStringLiteral("omf/avid_supporting/msmFMID.pmr") << 0;
		QTest::newRow("MDB") << QStringLiteral("msmMMOB.mdb") << 1;
		QTest::newRow("MacRoman-MDB") << QStringLiteral("msmMMOB_macroman.mdb") << 1;
		QTest::newRow("modern-audio-MDB") << QStringLiteral("omf/mc2026_audio/msmMMOB.mdb") << 1;
		QTest::newRow("MXF") << QStringLiteral("TONE_100A01.EA7D504A.611740.mxf") << 2;
		QTest::newRow("MXF-header-excerpt") << QStringLiteral("avid_headers/V01.E68429EE_4572545725368V.mxf") << 2;
		QTest::newRow("OMF") << QStringLiteral("omf/avid_supporting/BLACK_720x576x1_DV411.omf") << 3;
		QTest::newRow("native-WAV") << QStringLiteral("omf/mc2026_audio/TONE_100A01.6A972974.039700.wav") << 3;
		QTest::newRow("native-AIFF-C") << QStringLiteral("omf/mc2026_audio/TONE_100A01.6A972997.0C53E0.aif") << 3;
		QTest::newRow("WAVE-AVB") << QStringLiteral("omf/mc2026_audio/bins/WAVE(OMF).avb") << 4;
		QTest::newRow("AIFF-AVB") << QStringLiteral("omf/mc2026_audio/bins/AIFF-C(OMF).avb") << 4;
	}
	void genuine_reader_output_round_trips()
	{
		QFETCH(QString, relative);
		QFETCH(int, reader);
		QFile input(QStringLiteral(FIXTURES_DIR) + '/' + relative);
		QVERIFY2(input.open(QIODevice::ReadOnly), qPrintable(input.errorString()));
		const auto kind = reader == 0 ? MetadataSource::Pmr : reader == 1 ? MetadataSource::Mdb
						: reader == 2 ? MetadataSource::Mxf : reader == 4 ? MetadataSource::Avb : MetadataSource::Omf;
		const SourceSnapshotRef receipt = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			kind, input.fileName(), QFileInfo(input).lastModified(), SourceReadState::NotRead});
		const MediaEngine::Cancellation cancellation;
		const MediaEngine::ReaderContext context{receipt, cancellation};
		MediaEngine::ParsedSource original;
		switch (reader)
		{
		case 0: original = MediaEngine::PmrReader{}.read(input, context); break;
		case 1: original = MediaEngine::MdbReader{}.read(input, context); break;
		case 2: original = MediaEngine::MxfReader{}.read(input, context); break;
		case 3: original = MediaEngine::OmfReader{}.read(input, context); break;
		case 4: original = MediaEngine::AvbReader{}.read(input, context); break;
		}
		// Native audio keeps its chunks at the container level; its OMF
		// objects live in an embedded source with a separate handle namespace.
		QVERIFY(!original.objects.isEmpty() || !original.unownedProperties.isEmpty() || !original.embeddedSources.isEmpty());
		if (relative.endsWith(QStringLiteral(".wav")) || relative.endsWith(QStringLiteral(".aif")))
		{
			QVERIFY(original.container == MediaEngine::ParsedSource::Container::Wave || original.container == MediaEngine::ParsedSource::Container::Aiff);
			QVERIFY(!original.embeddedSources.isEmpty());
		}
		const auto archive = MediaEngine::SourceArchive::pack(original, cancellation);
		QVERIFY(archive);
		QVERIFY(archive->compressedBytes() > 0);
		QVERIFY(archive->serializedBytes() > 0);
		QVERIFY(archive->blockCount() > 0);
		const auto restored = archive->restore(cancellation);
		QVERIFY(restored);
		compareSource(original, *restored);
		compareContextAliases(original, *restored);
		if (QTest::currentTestFailed())
			return;
		if (reader == 4)
			compareAvbResolution(original, *restored, cancellation);
		else
		{
			const auto a = project(original, cancellation), b = project(*restored, cancellation);
			QVERIFY(!a.files.isEmpty() || !a.masters.isEmpty());
			QCOMPARE(a.diagnostics, b.diagnostics);
			compareProjectedFiles(a.files, b.files);
			compareProjectedFiles(a.masters, b.masters);
		}
	}
	void authored_archive_values_keep_types_optionals_and_namespaces()
	{
		// A serializer control, not a fabricated OMF/MXF specimen or parser rule.
		MediaEngine::ParsedSource original;
		original.outcome = MediaEngine::ParsedSource::Outcome::Incomplete;
		original.readReason = QStringLiteral("serialization value control");
		original.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mdb, QStringLiteral("archive-control"), QDateTime::fromMSecsSinceEpoch(123456789, Qt::UTC), SourceReadState::Incomplete});
		original.container = MediaEngine::ParsedSource::Container::Bento;
		original.omfRevision = MediaEngine::OmfRevision::V2;
		original.recordSets.append({QStringLiteral("set"), MediaEngine::PmrFileSet::Unicode, -7, 2, {1, 9}, false});
		original.embedding = {QStringLiteral("embedded"), QByteArray("a\0b", 3), 9, {{-1, 0}, {27, 3}}};
		original.diagnostics = {QStringLiteral("read evidence retained")};
		MediaEngine::AvidObject object;
		object.handle = 1;
		object.role = MediaEngine::AvidObject::Role::Unknown;
		object.snapshot = original.snapshot;
		object.recordedIdentity = QByteArray("i\0d", 3);
		object.identityEncoding = QStringLiteral("uninterpreted");
		object.mxf = QSharedPointer<MediaEngine::MxfSetContext>::create(MediaEngine::MxfSetContext{
			QByteArray("set-key"), QStringLiteral("set context"), -1, {42, 3}, {45, 7}});
		object.avb = QSharedPointer<MediaEngine::AvbObjectContext>::create(MediaEngine::AvbObjectContext{
			QByteArray("TEST"), {100, 8}, {108, 20}, false, false});
		quint32 floatBits = 0x7fc01234u;
		quint64 doubleBits = Q_UINT64_C(0x7ff8000000005678);
		float floatNan;
		double doubleNan;
		std::memcpy(&floatNan, &floatBits, sizeof(floatNan));
		std::memcpy(&doubleNan, &doubleBits, sizeof(doubleNan));
		const QVariantList values{QVariant{}, QVariant(QMetaType::fromType<QString>()), QString(), QStringLiteral(""),
			QByteArray(), QByteArray(""), QVariant(QMetaType::fromType<qint64>()), false,
			qint32(0), quint32(0), qint64(-1), std::numeric_limits<quint64>::max(),
			QVariant::fromValue(floatNan), doubleNan, QVariant::fromValue(-0.0f), -0.0,
			QVariantList{false, QStringLiteral("nested")}, QVariantMap{{QStringLiteral("key"), QByteArray("\0", 1)}},
			QVariantHash{{QStringLiteral("key"), qint64(12)}}, QStringList{QString{}, QStringLiteral("")},
			QDateTime{}, QDateTime::fromMSecsSinceEpoch(123456789, Qt::UTC)};
		for (qsizetype i = 0; i < values.size(); ++i)
		{
			MediaEngine::RawProperty value;
			value.locator = {QStringLiteral("value%1").arg(i), QByteArray("k\0", 2), 1, {{qint64(i), 1}}};
			value.encoding = QByteArray("\0raw\0", 5);
			value.decoded = values[i];
			value.state = PropertyReadState(i % 4);
			value.interpretation = i % 2 ? QStringLiteral("interpreted") : QString{};
			if (i % 2)
			{
				value.textEncoding = MediaEngine::TextEncoding::Unknown;
				value.textEncodingBasis = EvidenceBasis::Derived;
			}
			value.bytesRetained = i % 2 == 0;
			value.bento = QSharedPointer<MediaEngine::BentoPropertyContext>::create(MediaEngine::BentoPropertyContext{
				7, 8, 9, 10, QStringLiteral("omfi:control"), {{22, 3}, {40, 2}},
				i % 3 == 0 ? std::optional<bool>{} : std::optional<bool>{i % 3 == 2}});
			value.mxf = QSharedPointer<MediaEngine::MxfPropertyContext>::create(MediaEngine::MxfPropertyContext{
				0x8001, -1, QByteArray("auid"), QStringLiteral("local control"), QByteArray("\0\1", 2), {{88, 2}}});
			object.properties.append(std::move(value));
		}
		original.objects.append(object);
		original.unownedProperties.append(object.properties.front());
		original.relationships.append({1, 0, original.embedding, QVariantList{quint64(9), QByteArray("ref")},
			QStringLiteral("control reference"), EvidenceBasis::Derived, QStringLiteral("unresolved retained")});
		MediaEngine::ParsedSource child = original;
		child.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Omf, QStringLiteral("child-control"), {}, SourceReadState::Complete});
		child.objects[0].snapshot = child.snapshot;
		child.objects[0].recordedIdentity = QByteArray("different-id");
		child.container = MediaEngine::ParsedSource::Container::Omf;
		child.omfRevision.reset();
		original.embeddedSources.append(std::move(child));
		const MediaEngine::Cancellation cancellation;
		const auto archive = MediaEngine::SourceArchive::pack(original, cancellation);
		QVERIFY(archive);
		const auto restored = archive->restore(cancellation);
		QVERIFY(restored);
		compareSource(original, *restored);
		compareContextAliases(original, *restored);
		if (QTest::currentTestFailed())
			return;
		QCOMPARE(restored->objects[0].handle, restored->embeddedSources[0].objects[0].handle);
		QVERIFY(restored->objects[0].snapshot != restored->embeddedSources[0].objects[0].snapshot);
	}
	void authored_value_crosses_blocks_and_cancellation_keeps_published_archive_usable()
	{
		// Large retained metadata exercises archive blocks only; this is not an
		// invented media layout or a performance benchmark for real databases.
		constexpr qsizetype payloadBytes = 8 * 1024 * 1024;
		MediaEngine::ParsedSource original;
		original.snapshot = QSharedPointer<SourceSnapshot>::create();
		MediaEngine::RawProperty property;
		property.locator = {QStringLiteral("archive block control"), {}, 0, {{0, payloadBytes}}};
		property.encoding = QByteArray(payloadBytes, '\0');
		for (qsizetype i = 0; i < payloadBytes; ++i)
			property.encoding[i] = char(i % 251);
		property.decoded = property.encoding;
		property.state = PropertyReadState::Present;
		original.unownedProperties.append(std::move(property));
		const MediaEngine::Cancellation active;
		const auto archive = MediaEngine::SourceArchive::pack(original, active);
		QVERIFY(archive);
		QVERIFY(archive->blockCount() > 1);
		const auto restored = archive->restore(active);
		QVERIFY(restored);
		compareSource(original, *restored);

		// Cancellation races with real packing/restoration on a joined worker.
		// If completion wins the race, the only acceptable result is a complete
		// graph/archive. No test-only callback is added to production storage.
		for (const bool packing : {true, false})
		{
			std::atomic_bool stop{false}, finished{false};
			const MediaEngine::Cancellation cancellation(&stop);
			QSemaphore entered;
			QSharedPointer<const MediaEngine::SourceArchive> packed;
			std::optional<MediaEngine::ParsedSource> unpacked;
			std::exception_ptr error;
			std::thread worker([&] {
				entered.release();
				try
				{
					if (packing)
						packed = MediaEngine::SourceArchive::pack(original, cancellation);
					else
						unpacked = archive->restore(cancellation);
				}
				catch (...)
				{
					error = std::current_exception();
				}
				finished.store(true);
			});
			entered.acquire();
			const bool wasActive = !finished.load();
			stop.store(true);
			worker.join();
			QVERIFY(!error);
			if (!wasActive)
				qInfo("Archive operation completed before the cancellation request.");
			if (packed)
			{
				const auto completed = packed->restore(active);
				QVERIFY(completed);
				compareSource(original, *completed);
			}
			if (unpacked)
				compareSource(original, *unpacked);
		}
		const auto afterCancellation = archive->restore(active);
		QVERIFY(afterCancellation);
		compareSource(original, *afterCancellation);
	}
	void unsupported_authored_value_reports_an_archive_error()
	{
		// A codec failure is distinct from cancellation and never changes a
		// source's format outcome or quietly discards its unstreamable value.
		MediaEngine::ParsedSource original;
		MediaEngine::RawProperty property;
		property.decoded = QVariant::fromValue(UnstreamableArchiveValue{7});
		original.unownedProperties.append(std::move(property));
		const MediaEngine::Cancellation cancellation;
		QVERIFY_THROWS_EXCEPTION(MediaEngine::SourceArchiveError, MediaEngine::SourceArchive::pack(original, cancellation));
	}
	void stored_sources_release_expanded_graph_and_keep_cancelled_facts()
	{
		// Storage lifecycle control: cancellation must keep facts already read.
		MediaEngine::ParsedSource expected;
		expected.snapshot = QSharedPointer<SourceSnapshot>::create();
		expected.outcome = MediaEngine::ParsedSource::Outcome::Incomplete;
		expected.readReason = QStringLiteral("archive lifecycle control");
		MediaEngine::RawProperty value;
		value.encoding = QByteArray("retained bytes");
		value.decoded = false;
		value.state = PropertyReadState::Present;
		expected.unownedProperties.append(std::move(value));
		const MediaEngine::Cancellation active;
		auto expanded = expected;
		const auto stored = MediaEngine::StoredSource::store(std::move(expanded), active);
		QVERIFY(stored.archive);
		QVERIFY(!stored.unfinishedGraph);
		QVERIFY(!expanded.snapshot);
		QVERIFY(expanded.unownedProperties.isEmpty());
		const auto restored = stored.restore(active);
		QVERIFY(restored);
		compareSource(expected, *restored);

		MediaEngine::Cancellation cancelled;
		cancelled.cancel();
		expanded = expected;
		const auto unfinished = MediaEngine::StoredSource::store(std::move(expanded), cancelled);
		QVERIFY(!unfinished.archive);
		QVERIFY(unfinished.unfinishedGraph);
		QVERIFY(!unfinished.restore(cancelled));
		const auto retained = unfinished.restore(active);
		QVERIFY(retained);
		compareSource(expected, *retained);
	}
	void cancellation_does_not_publish_a_partial_archive_or_source()
	{
		MediaEngine::ParsedSource original;
		original.snapshot = QSharedPointer<SourceSnapshot>::create();
		const MediaEngine::Cancellation active;
		const auto archive = MediaEngine::SourceArchive::pack(original, active);
		QVERIFY(archive);
		MediaEngine::Cancellation cancelled;
		cancelled.cancel();
		QVERIFY(!MediaEngine::SourceArchive::pack(original, cancelled));
		QVERIFY(!archive->restore(cancelled));
		// Cancellation cannot alter an already published immutable archive.
		const auto restored = archive->restore(active);
		QVERIFY(restored);
		compareSource(original, *restored);
	}
};

QTEST_APPLESS_MAIN(TestSourceArchive)
#include "tst_sourcearchive.moc"
