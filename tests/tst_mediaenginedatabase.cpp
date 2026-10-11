// Checks temporary database acquisition and owned projections against genuine
// reader output. Authored byte controls below exercise acquisition failures only;
// they do not make claims about PMR or MDB framing.

#include "mediaengine/databaseimage.h"
#include "mediaengine/databasesource.h"
#include "mediaengine/mdbreader.h"
#include "mediaengine/pmrreader.h"
#include "mediaengine/scancoordinator.h"
#include "mediaenginefingerprint.h"

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
	void compareProjectedFiles(const QVector<MediaEngine::ProjectedFile> &a, const QVector<MediaEngine::ProjectedFile> &b)
	{
		QCOMPARE(a.size(), b.size());
		for (qsizetype i = 0; i < a.size(); ++i)
		{
			QCOMPARE(a[i].objects.size(), b[i].objects.size());
			for (qsizetype j = 0; j < a[i].objects.size(); ++j)
			{
				QCOMPARE(a[i].objects[j].source.data(), b[i].objects[j].source.data());
				QCOMPARE(a[i].objects[j].handle, b[i].objects[j].handle);
			}
			MediaEngineProof::Fingerprint left, right;
			left.projection({{a[i]}, {}, {}});
			right.projection({{b[i]}, {}, {}});
			QCOMPARE(left.result(), right.result());
		}
	}

	void compareSnapshot(const SourceSnapshotRef &a, const SourceSnapshotRef &b)
	{
		QCOMPARE(bool(a), bool(b));
		if (!a || !b)
			return;
		QCOMPARE(a->source, b->source);
		QCOMPARE(a->path, b->path);
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
		const auto add = [](const char *name, const QString &relative, bool pmr)
		{
			QTest::newRow(name) << relative << pmr;
		};
		add("PMR", QStringLiteral("msmFMID.pmr"), true);
		add("corpus-PMR", QStringLiteral("corpus_headers/msmFMID.pmr"), true);
		add("round3-PMR", QStringLiteral("corpus_headers/msmFMID_round3.pmr"), true);
		add("legacy-PMR", QStringLiteral("omf/avid_supporting/msmFMID.pmr"), true);
		add("modern-audio-PMR", QStringLiteral("omf/mc2026_audio/msmFMID.pmr"), true);
		add("MDB", QStringLiteral("msmMMOB.mdb"), false);
		add("MacRoman-MDB", QStringLiteral("msmMMOB_macroman.mdb"), false);
		add("corpus-MDB", QStringLiteral("corpus_headers/msmMMOB.mdb"), false);
		add("round3-MDB", QStringLiteral("corpus_headers/msmMMOB_round3.mdb"), false);
		add("legacy-MDB", QStringLiteral("omf/avid_supporting/msmMMOB.mdb"), false);
		add("modern-audio-MDB", QStringLiteral("omf/mc2026_audio/msmMMOB.mdb"), false);
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

	void genuineDatabaseSourcesPreserveProjectedFacts_data() { addDatabaseFixtures(); }
	void genuineDatabaseSourcesPreserveProjectedFacts()
	{
		QFETCH(QString, relative);
		QFETCH(bool, pmr);
		QFile fixture(QStringLiteral(FIXTURES_DIR) + '/' + relative);
		QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
		QByteArray expectedBytes = fixture.readAll();
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
		auto expectedProjection = project(original, active);
		QVERIFY(!expectedProjection.files.isEmpty() || !expectedProjection.masters.isEmpty());
		QCOMPARE(prepared.projection.diagnostics, expectedProjection.diagnostics);
		compareProjectedFiles(prepared.projection.files, expectedProjection.files);
		compareProjectedFiles(prepared.projection.masters, expectedProjection.masters);
		if (QTest::currentTestFailed())
			return;

		// Remove every input and temporary graph before checking the owned facts.
		QFile replacement(path);
		QVERIFY(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(replacement.write(acquisitionControl(257)), qint64(257));
		replacement.close();
		QVERIFY(QFile::remove(path));
		QVERIFY(!QFileInfo::exists(path));
		MediaEngineProof::Fingerprint before;
		before.projection(expectedProjection);
		const auto expectedDigest = before.result();
		original = {};
		expectedProjection = {};
		directInput.close();
		directInput.setData(QByteArray{});
		expectedBytes.clear();
		MediaEngineProof::Fingerprint after;
		after.projection(prepared.projection);
		QCOMPARE(after.result(), expectedDigest);
	}
};

QTEST_GUILESS_MAIN(TestMediaEngineDatabase)
#include "tst_mediaenginedatabase.moc"
