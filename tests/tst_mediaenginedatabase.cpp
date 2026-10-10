// Checks exact database acquisition and RAM-only restoration against genuine
// reader output. Authored byte controls below exercise acquisition failures only;
// they do not make claims about PMR or MDB framing.

#include "mediaengine/databaseimage.h"
#include "mediaengine/databasesource.h"
#include "mediaengine/mdbreader.h"
#include "mediaengine/pmrreader.h"
#include "mediaengine/scancoordinator.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace
{
	using Outcome = MediaEngine::ParsedSource::Outcome;
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

	void compareSnapshot(const SourceSnapshotRef &a, const SourceSnapshotRef &b)
	{
		QCOMPARE(bool(a), bool(b));
		if (!a || !b)
			return;
		QCOMPARE(a->source, b->source);
		compareText(a->path, b->path);
		QCOMPARE(a->modified, b->modified);
		QCOMPARE(a->readState, b->readState);
	}

	void usePublishedRootReceipt(MediaEngine::ParsedSource &source, const SourceSnapshotRef &published)
	{
		// Independent reader calls mint their own receipt. Compare its contents,
		// then map only references sharing that root receipt to the published one.
		// Any distinct object/embedded receipt remains distinct and is still tested.
		const SourceSnapshotRef original = source.snapshot;
		compareSnapshot(original, published);
		source.snapshot = published;
		for (auto &object : source.objects)
			if (object.snapshot == original)
				object.snapshot = published;
	}

	MediaEngine::Projection project(const MediaEngine::ParsedSource &source, const MediaEngine::Cancellation &cancellation)
	{
		return source.container == MediaEngine::ParsedSource::Container::Pmr
			? MediaEngine::projectPmr(source, cancellation) : MediaEngine::projectMdb(source, cancellation);
	}

	void addDatabaseFixtures()
	{
		QTest::addColumn<QString>("relative");
		QTest::addColumn<bool>("pmr");
		QTest::newRow("PMR") << QStringLiteral("msmFMID.pmr") << true;
		QTest::newRow("corpus-PMR") << QStringLiteral("corpus_headers/msmFMID.pmr") << true;
		QTest::newRow("round3-PMR") << QStringLiteral("corpus_headers/msmFMID_round3.pmr") << true;
		QTest::newRow("legacy-PMR") << QStringLiteral("omf/avid_supporting/msmFMID.pmr") << true;
		QTest::newRow("modern-audio-PMR") << QStringLiteral("omf/mc2026_audio/msmFMID.pmr") << true;
		QTest::newRow("MDB") << QStringLiteral("msmMMOB.mdb") << false;
		QTest::newRow("MacRoman-MDB") << QStringLiteral("msmMMOB_macroman.mdb") << false;
		QTest::newRow("corpus-MDB") << QStringLiteral("corpus_headers/msmMMOB.mdb") << false;
		QTest::newRow("round3-MDB") << QStringLiteral("corpus_headers/msmMMOB_round3.mdb") << false;
		QTest::newRow("legacy-MDB") << QStringLiteral("omf/avid_supporting/msmMMOB.mdb") << false;
		QTest::newRow("modern-audio-MDB") << QStringLiteral("omf/mc2026_audio/msmMMOB.mdb") << false;
	}

	QByteArray acquisitionControl(qsizetype size)
	{
		QByteArray bytes(size, '\0');
		for (qsizetype i = 0; i < size; ++i)
			bytes[i] = static_cast<char>(i % 256);
		return bytes;
	}

	// This device controls I/O outcomes, not database structure. Unbuffered reads
	// let its acquired prefix correspond directly to bytes exposed to the caller.
	class ControlledDevice final : public QIODevice
	{
	public:
		enum class Failure { None, Eof, Error, Stall };

		explicit ControlledDevice(QByteArray bytes)
			: reportedSize(bytes.size()), m_bytes(std::move(bytes))
		{
			open(QIODevice::ReadOnly | QIODevice::Unbuffered);
		}

		qint64 reportedSize = 0;
		qint64 maxReadSize = 65536;
		qint64 stopAt = -1;
		Failure failure = Failure::None;
		qint64 changeSizeAfterRead = 0;
		MediaEngine::Cancellation *cancellation = nullptr;
		qint64 cancelAt = -1;
		bool sequential = false;
		bool failSeek = false;
		qint64 acquiredBytes = 0;
		qint64 maximumRequested = 0;

		bool isSequential() const override { return sequential; }
		qint64 size() const override { return reportedSize; }
		bool atEnd() const override
		{
			return m_cursor >= m_bytes.size() ||
				(failure == Failure::Eof && stopAt >= 0 && m_cursor >= stopAt);
		}
		bool seek(qint64 offset) override
		{
			if (failSeek || offset < 0 || offset > m_bytes.size())
			{
				setErrorString(QStringLiteral("Controlled seek failure"));
				return false;
			}
			m_cursor = offset;
			return QIODevice::seek(offset);
		}

	protected:
		qint64 readData(char *destination, qint64 maximum) override
		{
			maximumRequested = std::max(maximumRequested, maximum);
			if (stopAt >= 0 && m_cursor >= stopAt)
			{
				if (failure == Failure::Error)
				{
					setErrorString(QStringLiteral("Controlled read failure"));
					return -1;
				}
				if (failure == Failure::Eof || failure == Failure::Stall)
					return 0;
			}
			qint64 count = std::min({maximum, maxReadSize, qint64(m_bytes.size()) - m_cursor});
			if (stopAt >= 0 && failure != Failure::None)
				count = std::min(count, stopAt - m_cursor);
			if (count > 0)
			{
				std::memcpy(destination, m_bytes.constData() + m_cursor, static_cast<size_t>(count));
				m_cursor += count;
				acquiredBytes += count;
				if (changeSizeAfterRead)
				{
					reportedSize += changeSizeAfterRead;
					changeSizeAfterRead = 0;
				}
				if (cancellation && cancelAt >= 0 && acquiredBytes >= cancelAt)
					cancellation->cancel();
			}
			return count;
		}
		qint64 writeData(const char *, qint64) override { return -1; }

	private:
		QByteArray m_bytes;
		qint64 m_cursor = 0;
	};
}

class TestMediaEngineDatabase final : public QObject
{
	Q_OBJECT

private slots:
	void genuineImagesAreByteExact_data() { addDatabaseFixtures(); }
	void genuineImagesAreByteExact()
	{
		QFETCH(QString, relative);
		QFETCH(bool, pmr);
		Q_UNUSED(pmr);
		QFile input(QStringLiteral(FIXTURES_DIR) + '/' + relative);
		QVERIFY2(input.open(QIODevice::ReadOnly), qPrintable(input.errorString()));
		const QByteArray expected = input.readAll(); // Fixtures only, independent of capture.
		QVERIFY(!expected.isEmpty());
		const MediaEngine::Cancellation cancellation;
		const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
		QCOMPARE(image.bytes(), expected);
		QCOMPARE(image.expectedSize(), qint64(expected.size()));
		QCOMPARE(image.outcome(), Outcome::Complete);
		QVERIFY(image.acquisitionComplete());
		QVERIFY(image.diagnostics().isEmpty());
		QVERIFY(input.isOpen());
		QCOMPARE(input.pos(), qint64(expected.size()));
	}

	void captureOwnsAllBinaryBytesAfterDeviceDestruction()
	{
		const QByteArray expected = acquisitionControl(3 * 1024 * 1024 + 257);
		std::optional<MediaEngine::DatabaseImage> image;
		QPointer<QBuffer> device;
		{
			auto input = std::make_unique<QBuffer>();
			device = input.get();
			input->setData(expected);
			QVERIFY(input->open(QIODevice::ReadOnly));
			QVERIFY(input->seek(17));
			const MediaEngine::Cancellation cancellation;
			image.emplace(MediaEngine::DatabaseImage::read(*input, cancellation));
			QCOMPARE(image->outcome(), Outcome::Complete);
			QCOMPARE(image->bytes(), expected);
			QVERIFY(image->bytes().constData() != input->buffer().constData());
			input->close();
			input->buffer().fill('x');
			QCOMPARE(image->bytes(), expected);
		}
		QVERIFY(device.isNull());
		QCOMPARE(image->bytes(), expected);
		QVERIFY(image->acquisitionComplete());
	}

	void emptyBinaryCaptureIsComplete()
	{
		QBuffer input;
		QVERIFY(input.open(QIODevice::ReadOnly));
		const MediaEngine::Cancellation cancellation;
		const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
		QVERIFY(image.bytes().isEmpty());
		QCOMPARE(image.expectedSize(), qint64(0));
		QCOMPARE(image.outcome(), Outcome::Complete);
		QVERIFY(image.acquisitionComplete());
	}

	void alreadyCancelledDoesNotRead()
	{
		ControlledDevice input(acquisitionControl(1024));
		MediaEngine::Cancellation cancellation;
		cancellation.cancel();
		const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
		QCOMPARE(image.outcome(), Outcome::Cancelled);
		QVERIFY(!image.acquisitionComplete());
		QVERIFY(image.bytes().isEmpty());
		QCOMPARE(input.acquiredBytes, qint64(0));
		QCOMPARE(input.maximumRequested, qint64(0));
		QVERIFY(!image.diagnostics().isEmpty());
	}

	void cancelledChunkedCaptureRetainsExactPrefix_data()
	{
		QTest::addColumn<qint64>("cancelAt");
		QTest::newRow("during-capture") << qint64(131072);
		QTest::newRow("final-read") << qint64(3 * 65536);
	}
	void cancelledChunkedCaptureRetainsExactPrefix()
	{
		QFETCH(qint64, cancelAt);
		const QByteArray expected = acquisitionControl(3 * 65536);
		ControlledDevice input(expected);
		MediaEngine::Cancellation cancellation;
		input.cancellation = &cancellation;
		input.cancelAt = cancelAt;
		const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
		QCOMPARE(image.outcome(), Outcome::Cancelled);
		QVERIFY(!image.acquisitionComplete());
		QCOMPARE(image.expectedSize(), qint64(expected.size()));
		QCOMPARE(input.acquiredBytes, cancelAt);
		QCOMPARE(image.bytes(), expected.first(static_cast<qsizetype>(cancelAt)));
		QVERIFY(input.maximumRequested <= 1024 * 1024);
		QVERIFY(!image.diagnostics().isEmpty());
	}

	void shortReadsStillComplete()
	{
		const QByteArray expected = acquisitionControl(1024 * 1024 + 13);
		ControlledDevice input(expected);
		input.maxReadSize = 113;
		const MediaEngine::Cancellation cancellation;
		const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
		QCOMPARE(image.outcome(), Outcome::Complete);
		QVERIFY(image.acquisitionComplete());
		QCOMPARE(image.bytes(), expected);
		QCOMPARE(input.acquiredBytes, qint64(expected.size()));
	}

	void failedReadRetainsPrefix_data()
	{
		QTest::addColumn<int>("failure");
		QTest::addColumn<int>("expectedOutcome");
		QTest::newRow("short-EOF") << int(ControlledDevice::Failure::Eof) << int(Outcome::Incomplete);
		QTest::newRow("read-error") << int(ControlledDevice::Failure::Error) << int(Outcome::IoError);
		QTest::newRow("zero-without-EOF") << int(ControlledDevice::Failure::Stall) << int(Outcome::IoError);
	}
	void failedReadRetainsPrefix()
	{
		QFETCH(int, failure);
		QFETCH(int, expectedOutcome);
		const QByteArray expected = acquisitionControl(3 * 65536);
		ControlledDevice input(expected);
		input.stopAt = 65536 + 29;
		input.failure = static_cast<ControlledDevice::Failure>(failure);
		const MediaEngine::Cancellation cancellation;
		const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
		QCOMPARE(image.outcome(), static_cast<Outcome>(expectedOutcome));
		QVERIFY(!image.acquisitionComplete());
		QCOMPARE(image.expectedSize(), qint64(expected.size()));
		QCOMPARE(image.bytes(), expected.first(static_cast<qsizetype>(input.stopAt)));
		QCOMPARE(input.acquiredBytes, input.stopAt);
		QVERIFY(!image.diagnostics().isEmpty());
		if (input.failure == ControlledDevice::Failure::Error)
			QVERIFY(image.diagnostics().join(' ').contains(QStringLiteral("Controlled read failure")));
	}

	void changingLengthNeverClaimsComplete_data()
	{
		QTest::addColumn<qint64>("change");
		QTest::newRow("growth") << qint64(1);
		QTest::newRow("shrinkage") << qint64(-1);
	}
	void changingLengthNeverClaimsComplete()
	{
		QFETCH(qint64, change);
		const QByteArray expected = acquisitionControl(2 * 65536);
		ControlledDevice input(expected);
		input.changeSizeAfterRead = change;
		const MediaEngine::Cancellation cancellation;
		const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
		QCOMPARE(image.outcome(), Outcome::Incomplete);
		QVERIFY(!image.acquisitionComplete());
		QCOMPARE(image.expectedSize(), qint64(expected.size()));
		QCOMPARE(image.bytes(), expected);
		QVERIFY(!image.diagnostics().isEmpty());
	}

	void unsupportedDevicesAndInvalidExtentsDoNotRead()
	{
		const QByteArray bytes = acquisitionControl(1024);
		const MediaEngine::Cancellation cancellation;
		QBuffer closed;
		QCOMPARE(MediaEngine::DatabaseImage::read(closed, cancellation).outcome(), Outcome::IoError);
		QBuffer writeOnly;
		QVERIFY(writeOnly.open(QIODevice::WriteOnly));
		QCOMPARE(MediaEngine::DatabaseImage::read(writeOnly, cancellation).outcome(), Outcome::IoError);
		for (int variant = 0; variant < 5; ++variant)
		{
			ControlledDevice input(bytes);
			if (variant == 0) input.sequential = true;
			if (variant == 1) input.setTextModeEnabled(true);
			if (variant == 2) input.failSeek = true;
			if (variant == 3) input.reportedSize = -1;
			if (variant == 4) input.reportedSize = std::numeric_limits<qint64>::max();
			const auto image = MediaEngine::DatabaseImage::read(input, cancellation);
			QCOMPARE(image.outcome(), Outcome::IoError);
			QVERIFY(!image.acquisitionComplete());
			QVERIFY(image.bytes().isEmpty());
			QCOMPARE(input.acquiredBytes, qint64(0));
			QCOMPARE(input.maximumRequested, qint64(0));
			QVERIFY(!image.diagnostics().isEmpty());
		}
	}

	void genuineDatabaseGraphsRestoreFromOwnedRam_data() { addDatabaseFixtures(); }
	void genuineDatabaseGraphsRestoreFromOwnedRam()
	{
		QFETCH(QString, relative);
		QFETCH(bool, pmr);
		QFile fixture(QStringLiteral(FIXTURES_DIR) + '/' + relative);
		QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
		const QByteArray expectedBytes = fixture.readAll();
		QVERIFY(!expectedBytes.isEmpty());
		fixture.close();

		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString path = temporary.filePath(QFileInfo(relative).fileName());
		QVERIFY(QFile::copy(fixture.fileName(), path));
		const MediaEngine::SourceCandidate candidate{
			pmr ? MediaEngine::SourceCandidate::ReaderHint::Pmr : MediaEngine::SourceCandidate::ReaderHint::Mdb,
			path, QFileInfo(path).lastModified(), 0};
		const QString readReason = QStringLiteral("Database-first RAM comparison");
		const MediaEngine::Cancellation active;
		const auto prepared = MediaEngine::prepareDatabase(candidate, readReason, active);
		QVERIFY(prepared.source.storage);
		QVERIFY(!prepared.source.archive);
		QVERIFY(!prepared.source.unfinishedGraph);
		const auto *database = dynamic_cast<const MediaEngine::DatabaseSource *>(prepared.source.storage.data());
		QVERIFY(database);
		QCOMPARE(database->image().bytes(), expectedBytes);
		QCOMPARE(database->image().expectedSize(), qint64(expectedBytes.size()));
		QCOMPARE(database->image().outcome(), Outcome::Complete);
		QVERIFY(database->image().acquisitionComplete());
		const char *const imageBacking = database->image().bytes().constData();

		QBuffer directInput;
		directInput.setData(expectedBytes);
		QVERIFY(directInput.open(QIODevice::ReadOnly));
		const SourceSnapshotRef directReceipt = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			pmr ? MetadataSource::Pmr : MetadataSource::Mdb, path, candidate.modified, SourceReadState::NotRead});
		const MediaEngine::ReaderContext context{directReceipt, active};
		auto original = pmr ? MediaEngine::PmrReader{}.read(directInput, context) : MediaEngine::MdbReader{}.read(directInput, context);
		original.readReason = readReason;
		QVERIFY(!original.objects.isEmpty());
		usePublishedRootReceipt(original, prepared.source.snapshot);
		if (QTest::currentTestFailed())
			return;
		QCOMPARE(prepared.source.outcome, original.outcome);
		QCOMPARE(prepared.source.container, original.container);
		QCOMPARE(prepared.source.readReason, original.readReason);
		QCOMPARE(prepared.source.diagnostics, original.diagnostics);
		const auto expectedProjection = project(original, active);
		QVERIFY(!expectedProjection.files.isEmpty() || !expectedProjection.masters.isEmpty());
		QCOMPARE(prepared.projection.diagnostics, expectedProjection.diagnostics);
		compareProjectedFiles(prepared.projection.files, expectedProjection.files);
		compareProjectedFiles(prepared.projection.masters, expectedProjection.masters);
		if (QTest::currentTestFailed())
			return;

		// Replace and remove the only input copy before restoring. The stored graph
		// must come from the captured image, with its original published receipt.
		QFile replacement(path);
		QVERIFY(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(replacement.write(acquisitionControl(257)), qint64(257));
		replacement.close();
		QVERIFY(QFile::remove(path));
		QVERIFY(!QFileInfo::exists(path));
		const auto restored = prepared.source.restore(active);
		QVERIFY(restored);
		QCOMPARE(restored->snapshot.data(), prepared.source.snapshot.data());
		compareSource(original, *restored);
		compareContextAliases(original, *restored);
		if (QTest::currentTestFailed())
			return;
		const auto restoredProjection = project(*restored, active);
		QCOMPARE(restoredProjection.diagnostics, expectedProjection.diagnostics);
		compareProjectedFiles(restoredProjection.files, expectedProjection.files);
		compareProjectedFiles(restoredProjection.masters, expectedProjection.masters);

		MediaEngine::Cancellation cancelled;
		cancelled.cancel();
		QVERIFY(!prepared.source.restore(cancelled));
		QVERIFY(database->image().bytes().constData() == imageBacking);
		QCOMPARE(database->image().bytes(), expectedBytes);
		QCOMPARE(database->image().outcome(), Outcome::Complete);
		QVERIFY(database->image().acquisitionComplete());
		const auto restoredAgain = prepared.source.restore(active);
		QVERIFY(restoredAgain);
		compareSource(original, *restoredAgain);
		compareContextAliases(original, *restoredAgain);
	}
};

QTEST_GUILESS_MAIN(TestMediaEngineDatabase)
#include "tst_mediaenginedatabase.moc"
