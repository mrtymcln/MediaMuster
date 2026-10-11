#pragma once

// Private AVB field reader shared by the object grammars. Each successful read
// keeps its original bytes and location; references remain source-local facts.

#include "scanmodel.h"
#include <QByteArrayView>

namespace MediaEngine::Detail
{
	struct AvbReadFailure
	{
		ParsedSource::Outcome outcome;
		QString explanation;
	};

	class AvbFieldReader
	{
	public:
		AvbFieldReader(QByteArrayView bytes, qint64 offset, bool bigEndian, ParsedSource &source,
				  AvidObject *object, const Cancellation &cancellation);
		quint8 u8(const QString &name);
		qint8 s8(const QString &name);
		quint16 u16(const QString &name);
		qint16 s16(const QString &name);
		quint32 u32(const QString &name);
		qint32 s32(const QString &name);
		quint64 u64(const QString &name);
		qint64 s64(const QString &name);
		bool boolean(const QString &name);
		double f64(const QString &name);
		double readMantissaExponent(const QString &name);
		QByteArray readBytes(const QString &name, qint64 count);
		QString string(const QString &name, TextEncoding encoding = TextEncoding::MacRoman);
		QString readText(const QString &name, qint64 count, TextEncoding encoding);
		QByteArray readFourcc(const QString &name);
		QByteArray readTypedUuid(const QString &name);
		QByteArray readBinaryUuid(const QString &name);
		QByteArray readMobId(const QString &name);
		quint32 readObjectReference(const QString &name);
		void recordMobReference(const QString &name, const QByteArray &identity);
		void expectTag(quint8 expected);
		int readExtensionTag(); // Consumes 0x01 + tag; returns -1 without consuming the next non-extension byte.
		quint8 peek() const;
		qint64 remainingBytes() const;
		qint64 fileOffset() const;
		bool isBigEndian() const { return m_bigEndian; }
		void requireCount(qint64 count, qint64 minimumWidth) const;
		void checkCancellation() const;
		[[noreturn]] void failUnsupported(const QString &reason) const;
		[[noreturn]] void failMalformed(const QString &reason) const;
		void retainUnparsedTail(const QString &reason);
		void setObjectRole(AvidObject::Role value);
		void setObjectIdentity(const QByteArray &value, const QString &encoding);

	private:
		RawProperty &captureField(const QString &name, qint64 count);
		QVector<RawProperty> &properties();
		QString decodeText(RawProperty &property, QByteArrayView bytes, TextEncoding encoding);
		template <typename T>
		T readInteger(const QString &name);
		QByteArrayView m_bytes;
		qint64 m_baseOffset;
		qsizetype m_relativeOffset = 0;
		bool m_bigEndian;
		ParsedSource &m_source;
		AvidObject *m_currentObject;
		const Cancellation &m_cancellation;
	};

	bool supportsAvbComponent(QByteArrayView classId);
	bool supportsAvbDescriptor(QByteArrayView classId);
	bool readAvbComponent(AvbFieldReader &cursor, QByteArrayView classId);
	bool readAvbDescriptor(AvbFieldReader &cursor, QByteArrayView classId);
}
