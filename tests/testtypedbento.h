#pragma once

// Scanner fixtures need real OMF types, a HEAD and byte-order declarations.
// The older untyped BentoBuilder remains useful for low-level container tests;
// this writer declares the schema that the semantic tests intend to exercise.

#include "testcanonbento.h"
#include <cstring>

class TypedBentoBuilder
{
public:
	explicit TypedBentoBuilder(bool compact = false, bool big = false, int revision = 1)
		: m_revision(revision)
	{
		m_writer.major = compact ? 2 : 1;
		m_writer.containerBig = big;
		m_writer.metadataBig = big;
		m_writer.head(revision);
	}

	quint32 addObject(const char *cls)
	{
		if (std::strcmp(cls, "HEAD") == 0)
			return 1;
		const auto handle = m_nextObject++;
		setImmediate(handle, m_revision == 1 ? "OMFI:ObjID" : "OMFI:OOBJ:ObjClass", QByteArray(cls, 4));
		return handle;
	}
	QByteArray word(quint32 value) const { return TestCanonBento::number(value, m_writer.metadataBig); }
	QByteArray half(quint16 value) const { return TestCanonBento::number(value, m_writer.metadataBig); }
	QByteArray wide(quint64 value) const { return TestCanonBento::number(value, m_writer.metadataBig); }
	void set(quint32 object, const char *property, const QByteArray &bytes, quint16 flags = 0)
	{
		m_writer.add(object, property, type(property, bytes), bytes, flags & 1, flags & 2);
	}
	void setImmediate(quint32 object, const char *property, const QByteArray &bytes)
	{
		m_writer.add(object, property, type(property, bytes), bytes, true);
	}
	void setU32(quint32 object, const char *property, quint32 value) { setImmediate(object, property, word(value)); }
	void setU16(quint32 object, const char *property, quint16 value) { setImmediate(object, property, half(value)); }
	void setString(quint32 object, const char *property, const QByteArray &value) { set(object, property, value + '\0'); }
	void setRational(quint32 object, const char *property, qint32 numerator, qint32 denominator)
	{
		set(object, property, word(quint32(numerator)) + word(quint32(denominator)));
	}
	void setHandle(quint32 object, const char *property, quint32 target)
	{
		set(object, property, m_writer.reference(target, m_revision));
	}
	void setHandles(quint32 object, const char *property, const QVector<quint32> &targets)
	{
		QByteArray bytes = half(quint16(targets.size()));
		for (const auto target : targets)
			bytes += m_writer.reference(target, m_revision);
		set(object, property, bytes);
	}
	void removeProperty(quint32 object, const char *property)
	{
		const auto id = m_writer.properties.value(property);
		m_writer.entries.removeIf([&](const auto &entry)
			{ return entry.object == object && entry.property == id; });
	}
	QByteArray build() const { return m_writer.build(); }

private:
	static QByteArray type(const QByteArray &name, const QByteArray &bytes)
	{
		if (name == "OMFI:ObjID")
			return "omfi:ObjectTag";
		if (name == "OMFI:OOBJ:ObjClass")
			return "omfi:ClassID";
		if (name == "OMFI:Version" || name == "OMFI:HEAD:Version")
			return "omfi:VersionType";
		if (name == "OMFI:ByteOrder" || name == "OMFI:HEAD:ByteOrder")
			return "omfi:Int16";
		if (name.endsWith(":MobID") || name == "OMFI:SCLP:SourceID")
			return "omfi:UID";
		if (name == "OMFI:MOBJ:UsageCode")
			return "omfi:UsageCodeType";
		if (name == "OMFI:ATTB:Kind")
			return "omfi:AttrKind";
		if (name == "OMFI:CPNT:TrackKind")
			return "omfi:TrackType";
		if (name.endsWith(":EditRate") || name == "OMFI:MDFL:SampleRate")
			return "omfi:Rational";
		if (name == "OMFI:MDFL:Length")
			return bytes.size() == 8 ? "omfi:Length64" : "omfi:Length32";
		if (name == "OMFI:MDAU:BitsPerSample" || name == "OMFI:MDAU:NumChannels")
			return "omfi:UInt16";
		if (name == "OMFI:ATTR:AttrRefs" || name == "OMFI:TRKG:Tracks" || name == "OMFI:MOBJ:Slots" ||
			name == "OMFI:ObjectSpine" || name == "OMFI:HEAD:Mobs" || name == "OMFI:HEAD:MediaData" || name == "OMFI:HEAD:PrimaryMobs")
			return "omfi:ObjRefArray";
		if (name == "OMFI:MOBJ:PhysicalMedia" || name == "OMFI:SMOB:MediaDescription" ||
			name == "OMFI:CPNT:Attributes" || name == "OMFI:MOBJ:UserAttributes" ||
			name == "OMFI:ATTB:ObjAttribute" || name == "OMFI:TRAK:TrackComponent" || name == "OMFI:MSLT:Segment")
			return "omfi:ObjRef";
		if (name == "OMFI:CPNT:Name" || name == "OMFI:MOBJ:Name" || name == "OMFI:ATTB:Name" ||
			name == "OMFI:ATTB:StringAttribute" || name == "OMFI:MCBR:MC:binName")
			return "omfi:String";
		if (name == "OMFI:WAVD:Summary")
			return "omfi:DataValue";
		return "TestOpaque";
	}
	TestCanonBento::TypedBento m_writer;
	int m_revision;
	quint32 m_nextObject = 68000;
};
