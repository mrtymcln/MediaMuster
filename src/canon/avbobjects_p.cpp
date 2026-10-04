// Bounded AVB field decoding. The object grammars describe layouts; this cursor
// keeps bytes, offsets and encodings together and never follows a reference.

#include "avbobjects_p.h"
#include "avidtext.h"
#include <QStringDecoder>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace Canon::Detail
{
	AvbCursor::AvbCursor(QByteArrayView bytes, qint64 offset, bool bigEndian,
						 ParsedSource &source, AvidObject *object, const Cancellation &cancellation)
		: m_bytes(bytes), m_offset(offset), m_bigEndian(bigEndian), m_source(source),
		  m_object(object), m_cancellation(cancellation)
	{
	}

	QVector<RawProperty> &AvbCursor::properties()
	{
		return m_object ? m_object->properties : m_source.unownedProperties;
	}

	qint64 AvbCursor::remaining() const { return m_bytes.size() - m_position; }
	qint64 AvbCursor::position() const { return m_offset + m_position; }

	void AvbCursor::checkCancellation() const
	{
		if (m_cancellation.cancelled())
			throw AvbFailure{ParsedSource::Outcome::Cancelled, QStringLiteral("AVB read cancelled.")};
	}

	void AvbCursor::requireCount(qint64 count, qint64 minimumWidth) const
	{
		checkCancellation();
		if (count < 0 || minimumWidth <= 0 || count > remaining() / minimumWidth)
			malformed(QStringLiteral("Count exceeds its enclosing AVB object at byte %1.").arg(position()));
	}

	[[noreturn]] void AvbCursor::unsupported(const QString &reason) const
	{
		throw AvbFailure{ParsedSource::Outcome::Unsupported, reason};
	}

	[[noreturn]] void AvbCursor::malformed(const QString &reason) const
	{
		throw AvbFailure{ParsedSource::Outcome::Malformed, reason};
	}

	RawProperty &AvbCursor::capture(const QString &name, qint64 count)
	{
		checkCancellation();
		if (count < 0)
			malformed(QStringLiteral("Negative width for %1.").arg(name));
		auto &p = properties().emplaceBack();
		p.locator.name = name;
		p.locator.objectNumber = m_object ? m_object->handle : 0;
		const auto available = static_cast<qsizetype>(std::min(count, remaining()));
		p.locator.ranges.append({position(), available});
		p.encoding = QByteArray(m_bytes.data() + m_position, available);
		m_position += available;
		p.state = count == available ? PropertyReadState::Present : PropertyReadState::Unreadable;
		if (count != available)
		{
			p.interpretation = QStringLiteral("Field extends beyond its enclosing AVB object.");
			malformed(QStringLiteral("Truncated %1 at byte %2.").arg(name).arg(position()));
		}
		return p;
	}

	template <typename T>
	T AvbCursor::integer(const QString &name)
	{
		auto &p = capture(name, sizeof(T));
		const T value = m_bigEndian ? qFromBigEndian<T>(p.encoding.constData())
									: qFromLittleEndian<T>(p.encoding.constData());
		if constexpr (std::is_signed_v<T>)
			p.decoded = QVariant::fromValue(qint64(value));
		else
			p.decoded = QVariant::fromValue(quint64(value));
		return value;
	}

	quint8 AvbCursor::u8(const QString &name) { return integer<quint8>(name); }
	qint8 AvbCursor::s8(const QString &name) { return integer<qint8>(name); }
	quint16 AvbCursor::u16(const QString &name) { return integer<quint16>(name); }
	qint16 AvbCursor::s16(const QString &name) { return integer<qint16>(name); }
	quint32 AvbCursor::u32(const QString &name) { return integer<quint32>(name); }
	qint32 AvbCursor::s32(const QString &name) { return integer<qint32>(name); }
	quint64 AvbCursor::u64(const QString &name) { return integer<quint64>(name); }
	qint64 AvbCursor::s64(const QString &name) { return integer<qint64>(name); }

	bool AvbCursor::boolean(const QString &name)
	{
		const auto value = u8(name);
		auto &p = properties().last();
		if (value > 1)
		{
			p.state = PropertyReadState::Unreadable;
			p.decoded.clear();
			malformed(QStringLiteral("Invalid boolean in %1.").arg(name));
		}
		p.decoded = value == 1;
		return value == 1;
	}

	double AvbCursor::f64(const QString &name)
	{
		const auto bits = u64(name);
		double value;
		static_assert(sizeof(value) == sizeof(bits));
		std::memcpy(&value, &bits, sizeof(value));
		properties().last().decoded = value;
		return value;
	}

	double AvbCursor::exp10(const QString &name)
	{
		// Preserve the exact signed pair even when a floating approximation overflows.
		const auto mantissa = s32(name + QStringLiteral(".mantissa"));
		const auto exponent = s16(name + QStringLiteral(".exponent"));
		return double(mantissa) * std::pow(10.0, double(exponent));
	}

	QByteArray AvbCursor::raw(const QString &name, qint64 count)
	{
		return capture(name, count).encoding;
	}

	QString AvbCursor::decode(RawProperty &p, QByteArrayView bytes, TextEncoding encoding)
	{
		p.textEncoding = encoding;
		p.textEncodingBasis = EvidenceBasis::Recorded;
		QString value;
		if (encoding == TextEncoding::MacRoman)
		{
			value.reserve(bytes.size());
			for (char c : bytes)
			{
				const auto b = static_cast<quint8>(c);
				value.append(QChar(b < 128 ? char16_t(b) : AvidText::kMacRomanHigh[b - 128]));
			}
		}
		else if (encoding == TextEncoding::Ascii)
		{
			if (std::any_of(bytes.begin(), bytes.end(), [](char b)
							{ return quint8(b) > 127; }))
			{
				p.state = PropertyReadState::Unreadable;
				malformed(QStringLiteral("Non-ASCII byte in %1.").arg(p.locator.name));
			}
			value = QString::fromLatin1(bytes);
		}
		else if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::Utf16LE || encoding == TextEncoding::Utf16BE)
		{
			const auto codec = encoding == TextEncoding::Utf8	   ? QStringDecoder::Utf8
							   : encoding == TextEncoding::Utf16LE ? QStringDecoder::Utf16LE
																   : QStringDecoder::Utf16BE;
			QStringDecoder decoder(codec, QStringConverter::Flag::Stateless);
			value = decoder.decode(bytes);
			if (decoder.hasError())
			{
				p.state = PropertyReadState::Unreadable;
				malformed(QStringLiteral("Invalid text encoding in %1.").arg(p.locator.name));
			}
		}
		else
			unsupported(QStringLiteral("Undeclared text encoding in %1.").arg(p.locator.name));
		p.decoded = value;
		return value;
	}

	QString AvbCursor::text(const QString &name, qint64 count, TextEncoding encoding)
	{
		auto &p = capture(name, count);
		return decode(p, p.encoding, encoding);
	}

	QString AvbCursor::string(const QString &name, TextEncoding encoding)
	{
		// Count and prefix stay in the value's evidence. 0xffff is a null string,
		// distinct from a recorded zero-length string.
		if (remaining() < 2)
			return text(name, 2, encoding); // capture retains the truncated field before failing.
		const auto size = m_bigEndian ? qFromBigEndian<quint16>(m_bytes.data() + m_position)
									  : qFromLittleEndian<quint16>(m_bytes.data() + m_position);
		auto &p = capture(name, 2 + (size == 0xffff ? 0 : size));
		p.textEncoding = encoding;
		p.textEncodingBasis = EvidenceBasis::Recorded;
		if (size == 0xffff)
		{
			p.state = PropertyReadState::Absent;
			p.interpretation = QStringLiteral("Recorded AVB null-string sentinel (0xffff).");
			return {};
		}
		QByteArrayView bytes(p.encoding.constData() + 2, size);
		// AVB counted strings allow outer NUL padding; retain it in encoding.
		if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::MacRoman || encoding == TextEncoding::Ascii)
		{
			while (!bytes.empty() && bytes.front() == '\0')
				bytes = bytes.sliced(1);
			while (!bytes.empty() && bytes.back() == '\0')
				bytes = bytes.first(bytes.size() - 1);
		}
		return decode(p, bytes, encoding);
	}

	QByteArray AvbCursor::fourcc(const QString &name)
	{
		auto &p = capture(name, 4);
		QByteArray id = p.encoding;
		if (!m_bigEndian)
			std::reverse(id.begin(), id.end());
		p.decoded = id;
		return id;
	}

	QByteArray AvbCursor::rawUuid(const QString &name)
	{
		auto &p = capture(name, 16);
		QByteArray id = p.encoding;
		if (m_bigEndian)
		{
			std::reverse(id.begin(), id.begin() + 4);
			std::reverse(id.begin() + 4, id.begin() + 6);
			std::reverse(id.begin() + 6, id.begin() + 8);
		}
		p.decoded = id;
		p.interpretation = QStringLiteral("UUID numeric fields normalized to little endian; original encoding retained.");
		return id;
	}

	QByteArray AvbCursor::uuid(const QString &name)
	{
		auto &p = capture(name, 24);
		const auto &b = p.encoding;
		const auto size = m_bigEndian ? qFromBigEndian<qint32>(b.constData() + 12) : qFromLittleEndian<qint32>(b.constData() + 12);
		if (quint8(b[0]) != 72 || quint8(b[5]) != 70 || quint8(b[8]) != 70 || quint8(b[11]) != 65 || size != 8)
		{
			p.state = PropertyReadState::Unreadable;
			malformed(QStringLiteral("Invalid typed UUID in %1.").arg(name));
		}
		QByteArray value = b.mid(1, 4) + b.mid(6, 2) + b.mid(9, 2) + b.mid(16, 8);
		if (m_bigEndian)
		{
			std::reverse(value.begin(), value.begin() + 4);
			std::reverse(value.begin() + 4, value.begin() + 6);
			std::reverse(value.begin() + 6, value.begin() + 8);
		}
		p.decoded = value;
		p.interpretation = QStringLiteral("Typed AVB UUID; numeric fields normalized to little endian.");
		return value;
	}

	QByteArray AvbCursor::mobId(const QString &name)
	{
		auto &p = capture(name, 49);
		const auto &b = p.encoding;
		const auto word = [&](int offset)
		{ return m_bigEndian ? qFromBigEndian<qint32>(b.constData() + offset) : qFromLittleEndian<qint32>(b.constData() + offset); };
		if (quint8(b[0]) != 65 || word(1) != 12 || quint8(b[17]) != 68 || quint8(b[19]) != 68 ||
			quint8(b[21]) != 68 || quint8(b[23]) != 68 || quint8(b[25]) != 72 || quint8(b[30]) != 70 ||
			quint8(b[33]) != 70 || quint8(b[36]) != 65 || word(37) != 8)
		{
			p.state = PropertyReadState::Unreadable;
			malformed(QStringLiteral("Invalid typed MobID in %1.").arg(name));
		}
		QByteArray value = b.mid(5, 12);
		for (int offset : {18, 20, 22, 24})
			value.append(b[offset]);
		value += b.mid(26, 4) + b.mid(31, 2) + b.mid(34, 2) + b.mid(41, 8);
		if (m_bigEndian)
		{
			std::reverse(value.begin() + 16, value.begin() + 20);
			std::reverse(value.begin() + 20, value.begin() + 22);
			std::reverse(value.begin() + 22, value.begin() + 24);
		}
		p.decoded = value;
		p.interpretation = QStringLiteral("Typed AVB MobID; material UUID numeric fields normalized to little endian.");
		return value;
	}

	quint32 AvbCursor::ref(const QString &name)
	{
		const auto value = u32(name);
		auto &r = m_source.relationships.emplaceBack();
		r.origin = m_object ? m_object->handle : 0;
		r.target = 0; // Assigned only after the referenced chunk has been encountered.
		r.recordedReference = value;
		r.referenceEncoding = QStringLiteral("AVB.ObjectIndex");
		r.locator = properties().last().locator;
		r.explanation = value == 0 ? QStringLiteral("Recorded null reference.") : QStringLiteral("Recorded AVB object index; validated after indexing.");
		return value;
	}

	void AvbCursor::mobReference(const QString &name, const QByteArray &id)
	{
		auto &r = m_source.relationships.emplaceBack();
		r.origin = m_object ? m_object->handle : 0;
		r.recordedReference = id;
		r.referenceEncoding = QStringLiteral("AVB.MobId.MaterialLE");
		for (auto it = properties().crbegin(); it != properties().crend(); ++it)
			if (it->locator.name == name)
			{
				r.locator = it->locator;
				break;
			}
		r.explanation = QStringLiteral("Recorded source MobID; identity matching belongs to reference resolution.");
	}

	void AvbCursor::tag(quint8 expected)
	{
		const auto actual = u8(QStringLiteral("@tag"));
		if (actual != expected)
			unsupported(QStringLiteral("Unrecognised AVB layout tag %1 (expected %2) at byte %3.").arg(actual).arg(expected).arg(position() - 1));
	}

	int AvbCursor::extension()
	{
		checkCancellation();
		if (remaining() == 0 || peek() != 1)
			return -1;
		tag(1);
		return u8(QStringLiteral("@extension"));
	}

	quint8 AvbCursor::peek() const
	{
		checkCancellation();
		if (remaining() == 0)
			malformed(QStringLiteral("Missing AVB field at byte %1.").arg(position()));
		return quint8(m_bytes[m_position]);
	}

	void AvbCursor::retainTail(const QString &reason)
	{
		if (remaining() == 0)
			return;
		auto &p = capture(QStringLiteral("UnparsedTail"), remaining());
		p.interpretation = reason;
	}

	void AvbCursor::role(AvidObject::Role value)
	{
		if (m_object)
			m_object->role = value;
	}

	void AvbCursor::identity(const QByteArray &value, const QString &encoding)
	{
		if (!m_object)
			return;
		if (!m_object->recordedIdentity.isEmpty() && m_object->recordedIdentity != value)
			malformed(QStringLiteral("Conflicting identities in one AVB object."));
		m_object->recordedIdentity = value;
		m_object->identityEncoding = encoding;
	}
}
