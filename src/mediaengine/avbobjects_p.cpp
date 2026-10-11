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

namespace MediaEngine::Detail
{
	AvbFieldReader::AvbFieldReader(QByteArrayView bytes, qint64 offset, bool bigEndian,
						 ParsedSource &source, AvidObject *object, const Cancellation &cancellation)
		: m_bytes(bytes), m_baseOffset(offset), m_bigEndian(bigEndian), m_source(source),
		  m_currentObject(object), m_cancellation(cancellation)
	{
	}

	QVector<RawProperty> &AvbFieldReader::properties()
	{
		return m_currentObject ? m_currentObject->properties : m_source.unownedProperties;
	}

	qint64 AvbFieldReader::remainingBytes() const { return m_bytes.size() - m_relativeOffset; }
	qint64 AvbFieldReader::fileOffset() const { return m_baseOffset + m_relativeOffset; }

	void AvbFieldReader::checkCancellation() const
	{
		if (m_cancellation.cancelled())
			throw AvbReadFailure{ParsedSource::Outcome::Cancelled, QStringLiteral("AVB read cancelled.")};
	}

	void AvbFieldReader::requireCount(qint64 count, qint64 minimumWidth) const
	{
		checkCancellation();
		if (count < 0 || minimumWidth <= 0 || count > remainingBytes() / minimumWidth)
			failMalformed(QStringLiteral("Count exceeds its enclosing AVB object at byte %1.").arg(fileOffset()));
	}

	[[noreturn]] void AvbFieldReader::failUnsupported(const QString &reason) const
	{
		throw AvbReadFailure{ParsedSource::Outcome::Unsupported, reason};
	}

	[[noreturn]] void AvbFieldReader::failMalformed(const QString &reason) const
	{
		throw AvbReadFailure{ParsedSource::Outcome::Malformed, reason};
	}

	RawProperty &AvbFieldReader::captureField(const QString &name, qint64 count)
	{
		checkCancellation();
		if (count < 0)
			failMalformed(QStringLiteral("Negative width for %1.").arg(name));
		auto &p = properties().emplaceBack();
		p.locator.name = name;
		p.locator.objectNumber = m_currentObject ? m_currentObject->handle : 0;
		const auto available = static_cast<qsizetype>(std::min(count, remainingBytes()));
		p.locator.ranges.append({fileOffset(), available});
		p.encoding = QByteArray(m_bytes.data() + m_relativeOffset, available);
		m_relativeOffset += available;
		p.state = count == available ? PropertyReadState::Present : PropertyReadState::Unreadable;
		if (count != available)
		{
			p.interpretation = QStringLiteral("Field extends beyond its enclosing AVB object.");
			failMalformed(QStringLiteral("Truncated %1 at byte %2.").arg(name).arg(fileOffset()));
		}
		return p;
	}

	template <typename T>
	T AvbFieldReader::readInteger(const QString &name)
	{
		auto &p = captureField(name, sizeof(T));
		const T value = m_bigEndian ? qFromBigEndian<T>(p.encoding.constData())
									: qFromLittleEndian<T>(p.encoding.constData());
		if constexpr (std::is_signed_v<T>)
			p.decoded = QVariant::fromValue(qint64(value));
		else
			p.decoded = QVariant::fromValue(quint64(value));
		return value;
	}

	quint8 AvbFieldReader::u8(const QString &name) { return readInteger<quint8>(name); }
	qint8 AvbFieldReader::s8(const QString &name) { return readInteger<qint8>(name); }
	quint16 AvbFieldReader::u16(const QString &name) { return readInteger<quint16>(name); }
	qint16 AvbFieldReader::s16(const QString &name) { return readInteger<qint16>(name); }
	quint32 AvbFieldReader::u32(const QString &name) { return readInteger<quint32>(name); }
	qint32 AvbFieldReader::s32(const QString &name) { return readInteger<qint32>(name); }
	quint64 AvbFieldReader::u64(const QString &name) { return readInteger<quint64>(name); }
	qint64 AvbFieldReader::s64(const QString &name) { return readInteger<qint64>(name); }

	bool AvbFieldReader::boolean(const QString &name)
	{
		const auto value = u8(name);
		auto &p = properties().last();
		if (value > 1)
		{
			p.state = PropertyReadState::Unreadable;
			p.decoded.clear();
			failMalformed(QStringLiteral("Invalid boolean in %1.").arg(name));
		}
		p.decoded = value == 1;
		return value == 1;
	}

	double AvbFieldReader::f64(const QString &name)
	{
		const auto bits = u64(name);
		double value;
		static_assert(sizeof(value) == sizeof(bits));
		std::memcpy(&value, &bits, sizeof(value));
		properties().last().decoded = value;
		return value;
	}

	double AvbFieldReader::readMantissaExponent(const QString &name)
	{
		// Preserve the exact signed pair even when a floating approximation overflows.
		const auto mantissa = s32(name + QStringLiteral(".mantissa"));
		const auto exponent = s16(name + QStringLiteral(".exponent"));
		return double(mantissa) * std::pow(10.0, double(exponent));
	}

	QByteArray AvbFieldReader::readBytes(const QString &name, qint64 count)
	{
		return captureField(name, count).encoding;
	}

	QString AvbFieldReader::decodeText(RawProperty &p, QByteArrayView bytes, TextEncoding encoding)
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
				failMalformed(QStringLiteral("Non-ASCII byte in %1.").arg(p.locator.name));
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
				failMalformed(QStringLiteral("Invalid text encoding in %1.").arg(p.locator.name));
			}
		}
		else
			failUnsupported(QStringLiteral("Undeclared text encoding in %1.").arg(p.locator.name));
		p.decoded = value;
		return value;
	}

	QString AvbFieldReader::readText(const QString &name, qint64 count, TextEncoding encoding)
	{
		auto &p = captureField(name, count);
		return decodeText(p, p.encoding, encoding);
	}

	QString AvbFieldReader::string(const QString &name, TextEncoding encoding)
	{
		// Count and prefix stay in the value's evidence. 0xffff is a null string,
		// distinct from a recorded zero-length string.
		if (remainingBytes() < 2)
			return readText(name, 2, encoding); // capture retains the truncated field before failing.
		const auto size = m_bigEndian ? qFromBigEndian<quint16>(m_bytes.data() + m_relativeOffset)
									  : qFromLittleEndian<quint16>(m_bytes.data() + m_relativeOffset);
		auto &p = captureField(name, 2 + (size == 0xffff ? 0 : size));
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
		return decodeText(p, bytes, encoding);
	}

	QByteArray AvbFieldReader::readFourcc(const QString &name)
	{
		auto &p = captureField(name, 4);
		QByteArray id = p.encoding;
		if (!m_bigEndian)
			std::reverse(id.begin(), id.end());
		p.decoded = id;
		return id;
	}

	QByteArray AvbFieldReader::readBinaryUuid(const QString &name)
	{
		auto &p = captureField(name, 16);
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

	QByteArray AvbFieldReader::readTypedUuid(const QString &name)
	{
		auto &p = captureField(name, 24);
		const auto &b = p.encoding;
		const auto size = m_bigEndian ? qFromBigEndian<qint32>(b.constData() + 12) : qFromLittleEndian<qint32>(b.constData() + 12);
		if (quint8(b[0]) != 72 || quint8(b[5]) != 70 || quint8(b[8]) != 70 || quint8(b[11]) != 65 || size != 8)
		{
			p.state = PropertyReadState::Unreadable;
			failMalformed(QStringLiteral("Invalid typed UUID in %1.").arg(name));
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

	QByteArray AvbFieldReader::readMobId(const QString &name)
	{
		auto &p = captureField(name, 49);
		const auto &b = p.encoding;
		const auto word = [&](int offset)
		{ return m_bigEndian ? qFromBigEndian<qint32>(b.constData() + offset) : qFromLittleEndian<qint32>(b.constData() + offset); };
		if (quint8(b[0]) != 65 || word(1) != 12 || quint8(b[17]) != 68 || quint8(b[19]) != 68 ||
			quint8(b[21]) != 68 || quint8(b[23]) != 68 || quint8(b[25]) != 72 || quint8(b[30]) != 70 ||
			quint8(b[33]) != 70 || quint8(b[36]) != 65 || word(37) != 8)
		{
			p.state = PropertyReadState::Unreadable;
			failMalformed(QStringLiteral("Invalid typed MobID in %1.").arg(name));
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

	quint32 AvbFieldReader::readObjectReference(const QString &name)
	{
		const auto value = u32(name);
		auto &r = m_source.relationships.emplaceBack();
		r.origin = m_currentObject ? m_currentObject->handle : 0;
		r.target = 0; // Assigned only after the referenced chunk has been encountered.
		r.recordedReference = value;
		r.referenceEncoding = QStringLiteral("AVB.ObjectIndex");
		r.locator = properties().last().locator;
		r.explanation = value == 0 ? QStringLiteral("Recorded null reference.") : QStringLiteral("Recorded AVB object index; validated after indexing.");
		return value;
	}

	void AvbFieldReader::recordMobReference(const QString &name, const QByteArray &id)
	{
		auto &r = m_source.relationships.emplaceBack();
		r.origin = m_currentObject ? m_currentObject->handle : 0;
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

	void AvbFieldReader::expectTag(quint8 expected)
	{
		const auto actual = u8(QStringLiteral("@tag"));
		if (actual != expected)
			failUnsupported(QStringLiteral("Unrecognised AVB layout tag %1 (expected %2) at byte %3.").arg(actual).arg(expected).arg(fileOffset() - 1));
	}

	int AvbFieldReader::readExtensionTag()
	{
		checkCancellation();
		if (remainingBytes() == 0 || peek() != 1)
			return -1;
		expectTag(1);
		return u8(QStringLiteral("@extension"));
	}

	quint8 AvbFieldReader::peek() const
	{
		checkCancellation();
		if (remainingBytes() == 0)
			failMalformed(QStringLiteral("Missing AVB field at byte %1.").arg(fileOffset()));
		return quint8(m_bytes[m_relativeOffset]);
	}

	void AvbFieldReader::retainUnparsedTail(const QString &reason)
	{
		if (remainingBytes() == 0)
			return;
		auto &p = captureField(QStringLiteral("UnparsedTail"), remainingBytes());
		p.interpretation = reason;
	}

	void AvbFieldReader::setObjectRole(AvidObject::Role value)
	{
		if (m_currentObject)
			m_currentObject->role = value;
	}

	void AvbFieldReader::setObjectIdentity(const QByteArray &value, const QString &encoding)
	{
		if (!m_currentObject)
			return;
		if (!m_currentObject->recordedIdentity.isEmpty() && m_currentObject->recordedIdentity != value)
			failMalformed(QStringLiteral("Conflicting identities in one AVB object."));
		m_currentObject->recordedIdentity = value;
		m_currentObject->identityEncoding = encoding;
	}
}
