#pragma once

// Private AVB field reader shared by the object grammars. Each successful read
// keeps its original bytes and location; references remain source-local facts.

#include "scanmodel.h"
#include <QByteArrayView>

namespace MediaEngine::Detail
{
	struct AvbFailure
	{
		ParsedSource::Outcome outcome;
		QString explanation;
	};

	class AvbCursor
	{
	public:
		AvbCursor(QByteArrayView bytes, qint64 offset, bool bigEndian, ParsedSource &source,
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
		double exp10(const QString &name);
		QByteArray raw(const QString &name, qint64 count);
		QString string(const QString &name, TextEncoding encoding = TextEncoding::MacRoman);
		QString text(const QString &name, qint64 count, TextEncoding encoding);
		QByteArray fourcc(const QString &name);
		QByteArray uuid(const QString &name);
		QByteArray rawUuid(const QString &name);
		QByteArray mobId(const QString &name);
		quint32 ref(const QString &name);
		void mobReference(const QString &name, const QByteArray &identity);
		void tag(quint8 expected);
		int extension(); // Consumes 0x01 + tag; returns -1 without consuming the next non-extension byte.
		quint8 peek() const;
		qint64 remaining() const;
		qint64 position() const;
		bool bigEndian() const { return m_bigEndian; }
		void requireCount(qint64 count, qint64 minimumWidth) const;
		void checkCancellation() const;
		[[noreturn]] void unsupported(const QString &reason) const;
		[[noreturn]] void malformed(const QString &reason) const;
		void retainTail(const QString &reason);
		void role(AvidObject::Role value);
		void identity(const QByteArray &value, const QString &encoding);

	private:
		RawProperty &capture(const QString &name, qint64 count);
		QVector<RawProperty> &properties();
		QString decode(RawProperty &property, QByteArrayView bytes, TextEncoding encoding);
		template <typename T>
		T integer(const QString &name);
		QByteArrayView m_bytes;
		qint64 m_offset;
		qsizetype m_position = 0;
		bool m_bigEndian;
		ParsedSource &m_source;
		AvidObject *m_object;
		const Cancellation &m_cancellation;
	};

	bool supportsAvbComponent(QByteArrayView classId);
	bool supportsAvbDescriptor(QByteArrayView classId);
	bool readAvbComponent(AvbCursor &cursor, QByteArrayView classId);
	bool readAvbDescriptor(AvbCursor &cursor, QByteArrayView classId);
}
