// Genuine Avid media proves RAM-only graph/evidence restoration. The controlled
// device alters I/O timing and failures, never authors an alternative MXF layout.
// A small selection of genuine excerpts checks incomplete-read fallback; the
// existing archive suites already cover that storage path across the full corpus.

#include "canon2/mxfsource.h"
#include "canon/mxfreader.h"
#include "canon2fingerprint.h"
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cstring>
#include <utility>

namespace
{
	using Outcome = Canon::ParsedSource::Outcome;
	const QString readReason = QStringLiteral("No usable database match; read media header");

	QByteArray graphFingerprint(const Canon::ParsedSource &source)
	{
		Canon2Proof::Fingerprint fingerprint;
		fingerprint.graph(source);
		return fingerprint.result();
	}

	QByteArray projectionFingerprint(const Canon::Projection &projection)
	{
		Canon2Proof::Fingerprint fingerprint;
		fingerprint.projection(projection);
		return fingerprint.result();
	}

	void mapPublishedReceipt(Canon::ParsedSource &source, const SourceSnapshotRef &published)
	{
		QVERIFY(source.snapshot);
		QVERIFY(published);
		QCOMPARE(source.snapshot->source, published->source);
		QCOMPARE(source.snapshot->path, published->path);
		QCOMPARE(source.snapshot->modified, published->modified);
		QCOMPARE(source.snapshot->readState, published->readState);
		const auto original = source.snapshot;
		source.snapshot = published;
		for (auto &object : source.objects)
			if (object.snapshot == original)
				object.snapshot = published;
	}

	void comparePublishedReceipts(const Canon::ParsedSource &source, const Canon::ParsedSource &restored)
	{
		QCOMPARE(source.snapshot.data(), restored.snapshot.data());
		QCOMPARE(source.objects.size(), restored.objects.size());
		for (qsizetype index = 0; index < source.objects.size(); ++index)
			QCOMPARE(source.objects[index].snapshot.data(), restored.objects[index].snapshot.data());
		QCOMPARE(source.embeddedSources.size(), restored.embeddedSources.size());
		for (qsizetype index = 0; index < source.embeddedSources.size(); ++index)
			comparePublishedReceipts(source.embeddedSources[index], restored.embeddedSources[index]);
	}

	SourceSnapshotRef receipt(const Canon::SourceCandidate &candidate)
	{
		return QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, candidate.path, candidate.modified, SourceReadState::NotRead});
	}

	QByteArray fixtureBytes()
	{
		QFile fixture(QStringLiteral(FIXTURES_DIR) + QStringLiteral("/TONE_100A01.EA7D504A.611740.mxf"));
		if (!fixture.open(QIODevice::ReadOnly))
			return {};
		return fixture.readAll();
	}

	class ControlledDevice final : public QIODevice
	{
	public:
		explicit ControlledDevice(QByteArray bytes) : m_bytes(std::move(bytes))
		{
			open(QIODevice::ReadOnly | QIODevice::Unbuffered);
		}
		qint64 acquired = 0;
		qint64 maximumRead = 113;
		qint64 failAt = -1;
		qint64 cancelAt = -1;
		bool changeSize = false;
		bool changeReread = false;
		Canon::Cancellation *cancellation = nullptr;
		qint64 size() const override { return m_bytes.size() + (changeSize && acquired > 0 ? 1 : 0); }
		bool isSequential() const override { return false; }
		bool atEnd() const override { return pos() >= m_bytes.size(); }
		bool seek(qint64 offset) override { return offset >= 0 && QIODevice::seek(offset); }

	protected:
		qint64 readData(char *destination, qint64 maximum) override
		{
			if (failAt >= 0 && acquired >= failAt)
			{
				setErrorString(QStringLiteral("Controlled metadata read failure"));
				return -1;
			}
			const auto offset = pos();
			if (offset >= m_bytes.size())
				return 0;
			qint64 count = std::min({maximum, maximumRead, qint64(m_bytes.size()) - offset});
			if (failAt >= 0)
				count = std::min(count, failAt - acquired);
			std::memcpy(destination, m_bytes.constData() + offset, size_t(count));
			// UL byte 7 is a registry-version byte. Changing it on a reread checks
			// conflicting acquisition without constructing a new file format.
			if (changeReread && offset == 0 && count > 7 && m_startReads++ > 0)
				destination[7] ^= 1;
			acquired += count;
			if (cancellation && cancelAt >= 0 && acquired >= cancelAt)
				cancellation->cancel();
			return count;
		}
		qint64 writeData(const char *, qint64) override { return -1; }

	private:
		QByteArray m_bytes;
		int m_startReads = 0;
	};
}

class TestCanon2MxfSource final : public QObject
{
	Q_OBJECT
private slots:
	void genuineSourcesRestoreFromOwnedRam_data()
	{
		QTest::addColumn<QString>("relative");
		QTest::addColumn<bool>("complete");
		QTest::newRow("full-Avid-tone") << QStringLiteral("TONE_100A01.EA7D504A.611740.mxf") << true;
		for (const auto &relative : {
			QStringLiteral("avid_headers/A01.E683CD73_FF4BEFF4BE934A.mxf"),
			QStringLiteral("avid_headers/A02.E683C414_F82F4F82F462CA.mxf"),
			QStringLiteral("avid_headers/V01.E683CD72_FF4BEFF4BE92DV.mxf"),
			QStringLiteral("avid_headers/V01.E684299A_4533345333DF2V.mxf"),
			QStringLiteral("corpus_headers/A01.E68E86B6_CCE460CCE463DA.mxf"),
			QStringLiteral("corpus_headers/A02.E69CEE7D_F99D0F99D03E7A.mxf"),
			QStringLiteral("corpus_headers/V01.E68C0042_3ABA203ABA24EV.mxf"),
			QStringLiteral("corpus_headers/V01.E69BC935_1B2851B28565FV.mxf")})
			QTest::newRow(qPrintable(relative)) << relative << false;
	}

	void genuineSourcesRestoreFromOwnedRam()
	{
		QFETCH(QString, relative);
		QFETCH(bool, complete);
		QFile fixture(QStringLiteral(FIXTURES_DIR) + '/' + relative);
		QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
		const QByteArray originalBytes = fixture.readAll();
		QVERIFY(!originalBytes.isEmpty());
		fixture.close();
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const auto path = temporary.filePath(QFileInfo(relative).fileName());
		QVERIFY(QFile::copy(fixture.fileName(), path));
		const Canon::SourceCandidate candidate{Canon::SourceCandidate::ReaderHint::Mxf,
			path, QFileInfo(path).lastModified(), 73};
		const Canon::Cancellation active;
		const auto prepared = Canon2::prepareMxf(candidate, readReason, active);
		const auto *native = dynamic_cast<const Canon2::MxfSource *>(prepared.source.storage.data());
		if (complete)
		{
			QVERIFY(native);
			QVERIFY(!prepared.source.archive);
			QVERIFY(!prepared.source.unfinishedGraph);
			QVERIFY(native->image().valid());
			QCOMPARE(native->image().physicalSize(), qint64(originalBytes.size()));
			QVERIFY(native->image().storedBytes() > 0);
			QVERIFY(native->image().storedBytes() < native->image().physicalSize());
			for (const auto &range : native->image().ranges())
				QCOMPARE(range.bytes, originalBytes.sliced(range.offset, range.bytes.size()));
		}
		else
		{
			QVERIFY(!native);
			QVERIFY(!prepared.source.storage);
			QVERIFY(prepared.source.archive);
			QVERIFY(!prepared.source.unfinishedGraph);
		}
		QBuffer direct;
		direct.setData(originalBytes);
		QVERIFY(direct.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
		auto expected = Canon::MxfReader{}.read(direct, {receipt(candidate), active});
		expected.readReason = readReason;
		mapPublishedReceipt(expected, prepared.source.snapshot);
		if (QTest::currentTestFailed())
			return;
		QCOMPARE(prepared.source.outcome, expected.outcome);
		QCOMPARE(prepared.source.outcome, complete ? Outcome::Complete : Outcome::Incomplete);
		QCOMPARE(prepared.source.container, expected.container);
		QCOMPARE(prepared.source.diagnostics, expected.diagnostics);
		const auto expectedProjection = Canon::projectMxf(expected, active);
		QCOMPARE(projectionFingerprint(prepared.projection), projectionFingerprint(expectedProjection));
		QVERIFY(!expected.objects.isEmpty());

		QFile replacement(path);
		QVERIFY(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(replacement.write(QByteArray(257, 'x')), qint64(257));
		replacement.close();
		QVERIFY(QFile::remove(path));
		QVERIFY(!QFileInfo::exists(path));
		for (int repetition = 0; repetition < 2; ++repetition)
		{
			const auto restored = prepared.source.restore(active);
			QVERIFY(restored);
			QCOMPARE(graphFingerprint(*restored), graphFingerprint(expected));
			comparePublishedReceipts(expected, *restored);
			QCOMPARE(projectionFingerprint(Canon::projectMxf(*restored, active)), projectionFingerprint(expectedProjection));
		}
		Canon::Cancellation cancelled;
		cancelled.cancel();
		QVERIFY(!prepared.source.restore(cancelled));
	}

	void controlledAcquisitionPreservesActualOutcome_data()
	{
		QTest::addColumn<int>("control");
		QTest::newRow("short-reads") << 0;
		QTest::newRow("I-O-failure") << 1;
		QTest::newRow("cancelled-read") << 2;
		QTest::newRow("changing-length") << 3;
		QTest::newRow("conflicting-reread") << 4;
	}

	void controlledAcquisitionPreservesActualOutcome()
	{
		QFETCH(int, control);
		const auto bytes = fixtureBytes();
		QVERIFY(!bytes.isEmpty());
		Canon::Cancellation acquisition, expectedAcquisition;
		ControlledDevice input(bytes), direct(bytes);
		const auto configure = [&](ControlledDevice &device, Canon::Cancellation &cancellation) {
			if (control == 1) device.failAt = 1024;
			if (control == 2) { device.cancelAt = 1024; device.cancellation = &cancellation; }
			if (control == 3) device.changeSize = true;
			if (control == 4) device.changeReread = true;
		};
		configure(input, acquisition);
		configure(direct, expectedAcquisition);
		const Canon::SourceCandidate candidate{Canon::SourceCandidate::ReaderHint::Mxf,
			QStringLiteral("/unavailable/original-tone.mxf"), {}, 73};
		auto expected = Canon::MxfReader{}.read(direct, {receipt(candidate), expectedAcquisition});
		expected.readReason = readReason;
		const auto prepared = Canon2::prepareMxf(input, candidate, readReason, acquisition);
		QVERIFY(input.isOpen());
		QCOMPARE(input.acquired, direct.acquired);
		QCOMPARE(prepared.source.outcome, expected.outcome);
		QCOMPARE(prepared.source.diagnostics, expected.diagnostics);
		QCOMPARE(prepared.source.readReason, readReason);
		mapPublishedReceipt(expected, prepared.source.snapshot);
		if (QTest::currentTestFailed())
			return;
		const auto *native = dynamic_cast<const Canon2::MxfSource *>(prepared.source.storage.data());
		if (control == 0)
		{
			QCOMPARE(prepared.source.outcome, Outcome::Complete);
			QVERIFY(native);
		}
		else
		{
			if (control == 4)
				QCOMPARE(prepared.source.outcome, Outcome::Complete);
			QVERIFY(!native);
			QVERIFY(!prepared.source.storage);
			if (control == 2)
			{
				QCOMPARE(prepared.source.outcome, Outcome::Cancelled);
				QVERIFY(prepared.source.unfinishedGraph);
			}
			else
				QVERIFY(prepared.source.archive);
		}
		const Canon::Cancellation inspect;
		const auto restored = prepared.source.restore(inspect);
		QVERIFY(restored);
		QCOMPARE(graphFingerprint(*restored), graphFingerprint(expected));
		comparePublishedReceipts(expected, *restored);
		if (control != 2)
			QCOMPARE(projectionFingerprint(prepared.projection), projectionFingerprint(Canon::projectMxf(expected, inspect)));
	}

	void alreadyCancelledDoesNotTouchInput()
	{
		ControlledDevice input(fixtureBytes());
		Canon::Cancellation cancelled;
		cancelled.cancel();
		const Canon::SourceCandidate candidate{Canon::SourceCandidate::ReaderHint::Mxf,
			QStringLiteral("/unavailable/tone.mxf"), {}, 73};
		const auto prepared = Canon2::prepareMxf(input, candidate, readReason, cancelled);
		QCOMPARE(input.acquired, qint64(0));
		QCOMPARE(prepared.source.outcome, Outcome::Cancelled);
		QCOMPARE(prepared.source.snapshot->readState, SourceReadState::NotRead);
		QVERIFY(prepared.source.diagnostics.isEmpty());
		QVERIFY(prepared.source.unfinishedGraph);
		QVERIFY(!prepared.source.storage);
		const Canon::Cancellation inspect;
		const auto restored = prepared.source.restore(inspect);
		QVERIFY(restored);
		QCOMPARE(restored->outcome, Outcome::Cancelled);
		QVERIFY(restored->objects.isEmpty());
		QVERIFY(prepared.projection.files.isEmpty());
	}

	void openFailureKeepsFilesystemDiagnostic()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const auto path = temporary.filePath(QStringLiteral("missing.mxf"));
		QFile reference(path);
		QVERIFY(!reference.open(QIODevice::ReadOnly));
		const Canon::SourceCandidate candidate{Canon::SourceCandidate::ReaderHint::Mxf, path, {}, 73};
		const Canon::Cancellation active;
		const auto prepared = Canon2::prepareMxf(candidate, readReason, active);
		QCOMPARE(prepared.source.outcome, Outcome::IoError);
		QCOMPARE(prepared.source.container, Canon::ParsedSource::Container::Unknown);
		QCOMPARE(prepared.source.snapshot->readState, SourceReadState::Unreadable);
		QCOMPARE(prepared.source.diagnostics, QStringList{reference.errorString()});
		QVERIFY(prepared.source.archive);
		const auto restored = prepared.source.restore(active);
		QVERIFY(restored);
		QCOMPARE(restored->snapshot.data(), prepared.source.snapshot.data());
		QCOMPARE(restored->diagnostics, prepared.source.diagnostics);
		QVERIFY(restored->objects.isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestCanon2MxfSource)
#include "tst_canon2mxfsource.moc"
