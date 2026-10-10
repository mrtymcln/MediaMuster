#pragma once

// Keeps only the MXF bytes the established reader actually obtains. File offsets
// survive so the same reader can later recover its records entirely from RAM;
// picture and sound ranges which it skipped are never filled with invented bytes.

#include <QByteArray>
#include <QIODevice>
#include <QStringList>
#include <QVector>

namespace Canon2
{
	struct MxfRange
	{
		qint64 offset = 0;
		QByteArray bytes;
	};

	class MxfImage
	{
	public:
		MxfImage() = default;
		qint64 physicalSize() const { return m_physicalSize; }
		// Acquired counts successful reads, including rereads. Stored counts each
		// physical byte once. Neither includes bytes read ahead by the source itself.
		qint64 acquiredBytes() const { return m_acquiredBytes; }
		qint64 storedBytes() const { return m_storedBytes; }
		qsizetype rangeCount() const { return m_ranges.size(); }
		bool valid() const { return m_valid; }
		const QStringList &diagnostics() const { return m_diagnostics; }
		const QVector<MxfRange> &ranges() const { return m_ranges; }

	private:
		friend class MxfCaptureDevice;
		QVector<MxfRange> m_ranges;
		QStringList m_diagnostics;
		qint64 m_physicalSize = -1;
		qint64 m_acquiredBytes = 0;
		qint64 m_storedBytes = 0;
		bool m_valid = false;
	};

	// Borrows an already-open binary source. The adapter makes no read-ahead
	// requests and never closes the source. Finalizing closes only this adapter.
	class MxfCaptureDevice final : public QIODevice
	{
	public:
		explicit MxfCaptureDevice(QIODevice &source);
		MxfImage takeImage();
		bool isSequential() const override { return false; }
		qint64 size() const override;
		qint64 bytesAvailable() const override;
		bool atEnd() const override;
		bool seek(qint64 offset) override;

	protected:
		qint64 readData(char *data, qint64 maximum) override;
		qint64 writeData(const char *, qint64) override;

	private:
		void retain(qint64 offset, const char *data, qint64 length);
		void invalidate(const QString &diagnostic);
		QIODevice &m_source;
		MxfImage m_image;
		bool m_finalized = false;
	};

	// Owns an implicitly shared image value, so replay outlives the capturing
	// device and physical file. Seeking across skipped payloads is allowed;
	// attempting to read one is an explicit error, never a disk read or zero fill.
	class MxfReplayDevice final : public QIODevice
	{
	public:
		explicit MxfReplayDevice(const MxfImage &image);
		bool isSequential() const override { return false; }
		qint64 size() const override { return m_image.physicalSize(); }
		qint64 bytesAvailable() const override;
		bool seek(qint64 offset) override;

	protected:
		qint64 readData(char *data, qint64 maximum) override;
		qint64 writeData(const char *, qint64) override;

	private:
		MxfImage m_image;
	};
}
