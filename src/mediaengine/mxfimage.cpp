// Captures successful logical reads as adjacent byte ranges. Overlapping reads
// are checked against the first observation; inconsistent sources use the normal
// graph archive rather than silently replacing evidence with a later reread.

#include "mxfimage.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace MediaEngine
{
	namespace
	{
		qint64 endOf(const MxfRange &range)
		{
			return range.offset + range.bytes.size();
		}
	}

	MxfCaptureDevice::MxfCaptureDevice(QIODevice &source) : m_source(source)
	{
		if (!source.isOpen() || !source.isReadable() || source.isSequential() || source.isTextModeEnabled())
		{
			invalidate(QStringLiteral("MXF capture input must be open, readable, seekable and binary."));
			setErrorString(m_image.diagnostics().last());
			return;
		}
		m_image.m_physicalSize = source.size();
		if (m_image.m_physicalSize < 0 || source.pos() < 0)
		{
			invalidate(QStringLiteral("Cannot determine MXF capture extent or position: %1").arg(source.errorString()));
			setErrorString(source.errorString());
			return;
		}
		m_image.m_valid = true;
		QIODevice::open(ReadOnly | Unbuffered);
		QIODevice::seek(source.pos());
	}

	void MxfCaptureDevice::invalidate(const QString &diagnostic)
	{
		m_image.m_valid = false;
		if (!m_image.m_diagnostics.contains(diagnostic))
			m_image.m_diagnostics.append(diagnostic);
	}

	qint64 MxfCaptureDevice::size() const
	{
		return m_source.size(); // The reader must still notice a changing source extent.
	}

	qint64 MxfCaptureDevice::bytesAvailable() const
	{
		return isOpen() ? m_source.bytesAvailable() : 0;
	}

	bool MxfCaptureDevice::atEnd() const
	{
		return m_source.atEnd();
	}

	bool MxfCaptureDevice::seek(qint64 offset)
	{
		if (!isOpen())
			return false;
		if (!m_source.seek(offset))
		{
			setErrorString(m_source.errorString());
			invalidate(QStringLiteral("MXF capture seek failed: %1").arg(m_source.errorString()));
			return false;
		}
		return QIODevice::seek(offset);
	}

	qint64 MxfCaptureDevice::readData(char *data, qint64 maximum)
	{
		const qint64 offset = m_source.pos();
		const qint64 received = m_source.read(data, maximum);
		if (received <= 0)
		{
			setErrorString(m_source.errorString());
			if (received < 0 || !m_source.atEnd())
				invalidate(QStringLiteral("MXF capture read failed: %1").arg(m_source.errorString()));
		}
		else
			retain(offset, data, received);
		return received;
	}

	qint64 MxfCaptureDevice::writeData(const char *, qint64)
	{
		setErrorString(QStringLiteral("MXF capture is read-only."));
		return -1;
	}

	void MxfCaptureDevice::retain(qint64 offset, const char *data, qint64 length)
	{
		constexpr qint64 maximum = std::numeric_limits<qint64>::max();
		constexpr qint64 maximumArray = static_cast<qint64>(std::numeric_limits<qsizetype>::max() - 1);
		if (offset < 0 || length > maximum - offset || length > maximum - m_image.m_acquiredBytes || length > maximumArray)
		{
			invalidate(QStringLiteral("MXF capture offsets or byte count cannot be represented on this platform."));
			return;
		}
		m_image.m_acquiredBytes += length;
		const qint64 end = offset + length;
		auto &ranges = m_image.m_ranges;
		auto first = std::lower_bound(ranges.begin(), ranges.end(), offset,
									  [](const MxfRange &range, qint64 position)
									  { return endOf(range) < position; });
		if (first == ranges.end() || first->offset > end)
		{
			ranges.insert(first, MxfRange{offset, QByteArray(data, static_cast<qsizetype>(length))});
			m_image.m_storedBytes += length;
			return;
		}

		// Most MXF reads extend the final range by a tag, length or value. Appending
		// uses QByteArray's spare capacity instead of allocating for each small read.
		auto last = first;
		qint64 mergedEnd = std::max(end, endOf(*first));
		while (last + 1 != ranges.end() && (last + 1)->offset <= mergedEnd)
		{
			++last;
			mergedEnd = std::max(mergedEnd, endOf(*last));
		}
		for (auto range = first; range != last + 1; ++range)
		{
			const qint64 overlapStart = std::max(offset, range->offset);
			const qint64 overlapEnd = std::min(end, endOf(*range));
			if (overlapEnd > overlapStart &&
				std::memcmp(range->bytes.constData() + (overlapStart - range->offset),
							data + (overlapStart - offset), static_cast<size_t>(overlapEnd - overlapStart)) != 0)
				invalidate(QStringLiteral("MXF bytes changed when reread; first acquired bytes were retained."));
		}
		if (first == last && first->offset <= offset)
		{
			const qint64 tail = end - endOf(*first);
			if (tail > 0)
			{
				if (tail > maximumArray - first->bytes.size())
				{
					invalidate(QStringLiteral("A captured MXF range cannot be represented by a RAM byte array on this platform."));
					return;
				}
				first->bytes.append(data + length - tail, static_cast<qsizetype>(tail));
				m_image.m_storedBytes += tail;
			}
			return;
		}

		const qint64 mergedStart = std::min(offset, first->offset);
		const qint64 mergedLength = mergedEnd - mergedStart;
		if (mergedLength > maximumArray)
		{
			invalidate(QStringLiteral("A captured MXF range cannot be represented by a RAM byte array on this platform."));
			return;
		}
		QByteArray bytes(static_cast<qsizetype>(mergedLength), Qt::Uninitialized);
		std::memcpy(bytes.data() + offset - mergedStart, data, static_cast<size_t>(length));
		qint64 replacedBytes = 0;
		for (auto range = first; range != last + 1; ++range)
		{
			std::memcpy(bytes.data() + range->offset - mergedStart, range->bytes.constData(),
						static_cast<size_t>(range->bytes.size()));
			replacedBytes += range->bytes.size();
		}
		const qsizetype index = first - ranges.begin();
		ranges.erase(first, last + 1);
		ranges.insert(index, MxfRange{mergedStart, std::move(bytes)});
		m_image.m_storedBytes += mergedLength - replacedBytes;
	}

	MxfImage MxfCaptureDevice::takeImage()
	{
		if (m_finalized)
		{
			MxfImage result;
			result.m_diagnostics.append(QStringLiteral("MXF capture was already finalized."));
			return result;
		}
		if (m_source.size() != m_image.m_physicalSize)
			invalidate(QStringLiteral("MXF length changed during capture; acquired bytes cannot reproduce a stable source."));
		for (auto &range : m_image.m_ranges)
			range.bytes.squeeze();
		m_image.m_ranges.squeeze();
		m_finalized = true;
		close();
		return std::move(m_image);
	}

	MxfReplayDevice::MxfReplayDevice(const MxfImage &image) : m_image(image)
	{
		if (image.valid())
			QIODevice::open(ReadOnly | Unbuffered);
		else
			setErrorString(QStringLiteral("Cannot replay an invalid MXF image: %1").arg(image.diagnostics().join(QStringLiteral("; "))));
	}

	qint64 MxfReplayDevice::bytesAvailable() const
	{
		return isOpen() ? std::max(qint64(0), size() - pos()) : 0;
	}

	bool MxfReplayDevice::seek(qint64 offset)
	{
		return isOpen() && QIODevice::seek(offset);
	}

	qint64 MxfReplayDevice::readData(char *data, qint64 maximum)
	{
		const qint64 offset = pos();
		if (offset >= size() || maximum == 0)
			return 0;
		const qint64 length = std::min(maximum, size() - offset);
		const auto &ranges = m_image.ranges();
		const auto next = std::upper_bound(ranges.cbegin(), ranges.cend(), offset,
										   [](qint64 position, const MxfRange &range)
										   { return position < range.offset; });
		if (next == ranges.cbegin() || endOf(*(next - 1)) < offset + length)
		{
			setErrorString(QStringLiteral("MXF RAM image has no acquired bytes for requested range %1..%2.")
							   .arg(offset)
							   .arg(offset + length));
			return -1;
		}
		const auto &range = *(next - 1);
		std::memcpy(data, range.bytes.constData() + (offset - range.offset), static_cast<size_t>(length));
		return length;
	}

	qint64 MxfReplayDevice::writeData(const char *, qint64)
	{
		setErrorString(QStringLiteral("MXF RAM replay is read-only."));
		return -1;
	}
}
