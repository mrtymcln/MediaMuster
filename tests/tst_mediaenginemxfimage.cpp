// Checks byte acquisition and RAM-only replay using genuine MXF metadata.
// Authored byte devices exercise Qt I/O and sparse storage only; they are not
// simulated MXF formats or evidence for additional format-recovery behaviour.

#include "mediaengine/mxfimage.h"
#include "mediaengine/mxfreader.h"
#include "mediaenginefingerprint.h"

#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace
{
	class ByteDevice final : public QIODevice
	{
	public:
		explicit ByteDevice(QByteArray bytes) : extent(bytes.size()), m_bytes(std::move(bytes))
		{
			open(ReadOnly | Unbuffered);
		}
		qint64 size() const override { return extent; }
		bool seek(qint64 offset) override
		{
			if (offset == failedSeek)
			{
				setErrorString(QStringLiteral("Controlled seek failure."));
				return false;
			}
			return QIODevice::seek(offset);
		}
		qint64 extent;
		qint64 failedRead = -1;
		qint64 emptyRead = -1;
		qint64 failedSeek = -1;
		qint64 readLimit = std::numeric_limits<qint64>::max();
		QVector<qint64> requests;

	protected:
		qint64 readData(char *data, qint64 maximum) override
		{
			requests.append(maximum);
			if (pos() == failedRead || pos() == emptyRead)
			{
				setErrorString(QStringLiteral("Controlled read failure."));
				return pos() == failedRead ? -1 : 0;
			}
			const qint64 count = std::min({maximum, readLimit, std::max(qint64(0), m_bytes.size() - pos())});
			if (count > 0)
				std::memcpy(data, m_bytes.constData() + pos(), static_cast<size_t>(count));
			return count;
		}
		qint64 writeData(const char *, qint64) override { return -1; }

	private:
		QByteArray m_bytes;
	};

	QByteArray graphHash(const MediaEngine::ParsedSource &source)
	{
		MediaEngineProof::Fingerprint fingerprint;
		fingerprint.graph(source);
		return fingerprint.sink.result();
	}
}

class TestMediaEngineMxfImage : public QObject
{
	Q_OBJECT

private slots:
	void small_reads_coalesce_without_adapter_read_ahead()
	{
		const QByteArray bytes(4096, 'x');
		ByteDevice source(bytes);
		MediaEngine::MxfCaptureDevice capture(source);
		QVERIFY(capture.isOpen());
		QVERIFY(capture.openMode().testFlag(QIODevice::Unbuffered));
		for (int i = 0; i < 1000; ++i)
		{
			char value = 0;
			QCOMPARE(capture.read(&value, 1), qint64(1));
			QCOMPARE(value, 'x');
		}
		const auto image = capture.takeImage();
		QVERIFY(image.valid());
		QVERIFY(source.isOpen());
		QVERIFY(!capture.isOpen());
		QCOMPARE(source.pos(), qint64(1000));
		QVERIFY(std::all_of(source.requests.cbegin(), source.requests.cend(), [](qint64 requested) { return requested == 1; }));
		QCOMPARE(image.physicalSize(), qint64(bytes.size()));
		QCOMPARE(image.acquiredBytes(), qint64(1000));
		QCOMPARE(image.storedBytes(), qint64(1000));
		QCOMPARE(image.rangeCount(), qsizetype(1));
		QCOMPARE(image.ranges().first().offset, qint64(0));
		QCOMPARE(image.ranges().first().bytes, bytes.first(1000));
		QCOMPARE(image.ranges().first().bytes.capacity(), image.ranges().first().bytes.size());
		QVERIFY(!capture.takeImage().valid());
	}

	void sparse_ranges_never_supply_missing_bytes()
	{
		ByteDevice source(QByteArrayLiteral("abcdefghijklmnopqrst"));
		MediaEngine::MxfCaptureDevice capture(source);
		QCOMPARE(capture.read(3), QByteArrayLiteral("abc"));
		QVERIFY(capture.seek(17));
		QCOMPARE(capture.read(3), QByteArrayLiteral("rst"));
		const auto image = capture.takeImage();
		QVERIFY(image.valid());
		QCOMPARE(image.rangeCount(), qsizetype(2));
		QCOMPARE(image.storedBytes(), qint64(6));
		MediaEngine::MxfReplayDevice replay(image);
		QVERIFY(replay.isOpen());
		QCOMPARE(replay.size(), qint64(20));
		QCOMPARE(replay.read(3), QByteArrayLiteral("abc"));
		QVERIFY(replay.seek(0));
		char bytes[20]{};
		QCOMPARE(replay.read(bytes, 20), qint64(-1)); // Includes a skipped gap.
		QVERIFY(replay.errorString().contains(QStringLiteral("no acquired bytes")));
		QVERIFY(replay.seek(10));
		QCOMPARE(replay.read(bytes, 1), qint64(-1));
		QVERIFY(replay.seek(17));
		QCOMPARE(replay.read(3), QByteArrayLiteral("rst"));
		QVERIFY(replay.atEnd());
		QCOMPARE(replay.read(bytes, 1), qint64(0));
		QVERIFY(replay.seek(100)); // QFile also permits seeks beyond the physical end.
		QCOMPARE(replay.read(bytes, 1), qint64(0));
	}

	void overlapping_and_bridging_reads_preserve_original_bytes()
	{
		const QByteArray bytes = QByteArrayLiteral("abcdefghijklmnopqrst");
		ByteDevice source(bytes);
		MediaEngine::MxfCaptureDevice capture(source);
		QVERIFY(capture.seek(10));
		QCOMPARE(capture.read(5), bytes.sliced(10, 5));
		QVERIFY(capture.seek(0));
		QCOMPARE(capture.read(5), bytes.first(5));
		QVERIFY(capture.seek(3));
		QCOMPARE(capture.read(10), bytes.sliced(3, 10));
		QVERIFY(capture.seek(0));
		QCOMPARE(capture.read(15), bytes.first(15));
		const auto image = capture.takeImage();
		QVERIFY(image.valid());
		QCOMPARE(image.rangeCount(), qsizetype(1));
		QCOMPARE(image.storedBytes(), qint64(15));
		QCOMPARE(image.acquiredBytes(), qint64(35));
		QCOMPARE(image.ranges().first().offset, qint64(0));
		QCOMPARE(image.ranges().first().bytes, bytes.first(15));
	}

	void changed_reread_is_returned_but_does_not_replace_first_evidence()
	{
		QByteArray bytes = QByteArrayLiteral("abcdefgh");
		QBuffer source(&bytes);
		QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
		MediaEngine::MxfCaptureDevice capture(source);
		QCOMPARE(capture.read(8), QByteArrayLiteral("abcdefgh"));
		bytes[3] = 'X';
		QVERIFY(capture.seek(2));
		QCOMPARE(capture.read(4), QByteArrayLiteral("cXef"));
		const auto image = capture.takeImage();
		QVERIFY(!image.valid());
		QCOMPARE(image.ranges().first().bytes, QByteArrayLiteral("abcdefgh"));
		QCOMPARE(image.acquiredBytes(), qint64(12));
		QCOMPARE(image.storedBytes(), qint64(8));
		QVERIFY(image.diagnostics().join(' ').contains(QStringLiteral("changed when reread")));
		MediaEngine::MxfReplayDevice replay(image);
		QVERIFY(!replay.isOpen());
	}

	void original_io_errors_and_extent_changes_remain_visible()
	{
		ByteDevice source(QByteArrayLiteral("abcdefgh"));
		MediaEngine::MxfCaptureDevice capture(source);
		QCOMPARE(capture.read(4), QByteArrayLiteral("abcd"));
		source.failedRead = 4;
		char value = 0;
		QCOMPARE(capture.read(&value, 1), qint64(-1));
		QCOMPARE(capture.errorString(), source.errorString());
		source.failedRead = -1;
		source.emptyRead = 4;
		QCOMPARE(capture.read(&value, 1), qint64(0));
		QVERIFY(!capture.atEnd());
		QCOMPARE(capture.errorString(), source.errorString());
		source.failedSeek = 7;
		QVERIFY(!capture.seek(7));
		QCOMPARE(capture.errorString(), source.errorString());
		QCOMPARE(capture.pos(), source.pos());
		source.extent = 7;
		QCOMPARE(capture.size(), qint64(7));
		const auto image = capture.takeImage();
		QVERIFY(!image.valid());
		QCOMPARE(image.physicalSize(), qint64(8));
		QCOMPARE(image.ranges().first().bytes, QByteArrayLiteral("abcd"));
		QVERIFY(source.isOpen());
	}

	void positive_short_reads_keep_all_obtained_bytes()
	{
		const QByteArray bytes = QByteArrayLiteral("abcdefghijklm");
		ByteDevice source(bytes);
		source.readLimit = 3;
		MediaEngine::MxfCaptureDevice capture(source);
		QByteArray obtained;
		while (!capture.atEnd())
		{
			const auto block = capture.read(8);
			QVERIFY(!block.isEmpty());
			obtained.append(block);
		}
		QCOMPARE(obtained, bytes);
		const auto image = capture.takeImage();
		QVERIFY(image.valid());
		QCOMPARE(image.ranges().first().bytes, bytes);
		QCOMPARE(image.storedBytes(), qint64(bytes.size()));
		QCOMPARE(image.acquiredBytes(), qint64(bytes.size()));
	}

	void acquisition_starts_at_the_borrowed_devices_current_position()
	{
		ByteDevice source(QByteArrayLiteral("abcdefgh"));
		QVERIFY(source.seek(3));
		MediaEngine::MxfCaptureDevice capture(source);
		QCOMPARE(capture.pos(), qint64(3));
		QCOMPARE(capture.read(4), QByteArrayLiteral("defg"));
		const auto image = capture.takeImage();
		QVERIFY(image.valid());
		QCOMPARE(image.ranges().first().offset, qint64(3));
		QCOMPARE(image.ranges().first().bytes, QByteArrayLiteral("defg"));
		MediaEngine::MxfReplayDevice replay(image);
		QVERIFY(replay.seek(3));
		QCOMPARE(replay.read(4), QByteArrayLiteral("defg"));
	}

	void finalized_image_and_replay_outlive_the_capture()
	{
		std::unique_ptr<MediaEngine::MxfReplayDevice> replay;
		{
			ByteDevice source(QByteArrayLiteral("retained bytes"));
			MediaEngine::MxfCaptureDevice capture(source);
			QCOMPARE(capture.readAll(), QByteArrayLiteral("retained bytes"));
			replay = std::make_unique<MediaEngine::MxfReplayDevice>(capture.takeImage());
		}
		QCOMPARE(replay->readAll(), QByteArrayLiteral("retained bytes"));
	}

	void genuine_mxf_graph_replays_after_the_input_is_removed()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString original = QStringLiteral(FIXTURES_DIR "/TONE_100A01.EA7D504A.611740.mxf");
		const QString copy = temporary.filePath(QStringLiteral("genuine.mxf"));
		QVERIFY(QFile::copy(original, copy));
		QFile source(copy);
		QVERIFY(source.open(QIODevice::ReadOnly));
		const MediaEngine::Cancellation cancellation;
		const auto receipt = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
			MetadataSource::Mxf, copy, {}, SourceReadState::NotRead});
		const MediaEngine::ReaderContext context{receipt, cancellation};
		const auto direct = MediaEngine::MxfReader{}.read(source, context);
		QVERIFY(!direct.objects.isEmpty());
		QVERIFY(source.seek(0));
		MediaEngine::MxfCaptureDevice capture(source);
		const auto captured = MediaEngine::MxfReader{}.read(capture, context);
		QCOMPARE(graphHash(captured), graphHash(direct));
		const auto image = capture.takeImage();
		QVERIFY(image.valid());
		QVERIFY(image.storedBytes() > 0);
		for (const auto &range : image.ranges())
		{
			QVERIFY(source.seek(range.offset));
			QCOMPARE(source.read(range.bytes.size()), range.bytes);
		}
		source.close();
		QVERIFY(QFile::remove(copy));
		MediaEngine::MxfReplayDevice replay(image);
		const auto restored = MediaEngine::MxfReader{}.read(replay, context);
		QCOMPARE(graphHash(restored), graphHash(direct));
	}
};

QTEST_GUILESS_MAIN(TestMediaEngineMxfImage)
#include "tst_mediaenginemxfimage.moc"
