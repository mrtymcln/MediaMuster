#pragma once

#include "testbento.h"
#include "testbento2.h"

// Constructed semantic coverage, not a captured SDII recording. Property
// names/roles follow OMF 2.1 Appendix A and Avid's SD2D registrations.
// Bento1 is intentional: semantic OMF version and container version differ.
namespace TestOmf
{
	class Writer
	{
	public:
		Writer(bool compact, bool big) : two(big), compact(compact), big(big) {}
		quint32 addObject(const char *cls) { return compact ? two.addObject(cls) : one.addObject(cls); }
		QByteArray word(quint32 v) const { return two.word(v); }
		QByteArray half(quint16 v) const { return two.half(v); }
		QByteArray wide(quint64 v) const { return big ? word(quint32(v >> 32)) + word(quint32(v)) : word(quint32(v)) + word(quint32(v >> 32)); }
		void set(quint32 o, const char *p, const QByteArray &v) { if (compact) two.set(o, p, v); else one.set(o, p, v); }
		void setImmediate(quint32 o, const char *p, const QByteArray &v) { if (compact) two.set(o, p, v, true); else one.setImmediate(o, p, v); }
		void setU32(quint32 o, const char *p, quint32 v) { setImmediate(o, p, word(v)); }
		void setU16(quint32 o, const char *p, quint16 v) { setImmediate(o, p, half(v)); }
		void setString(quint32 o, const char *p, const QByteArray &v) { set(o, p, v + '\0'); }
		void setRational(quint32 o, const char *p, quint32 n, quint32 d) { set(o, p, word(n) + word(d)); }
		void setHandle(quint32 o, const char *p, quint32 target) { set(o, p, word(target) + word(0)); }
		void setHandles(quint32 o, const char *p, const QVector<quint32> &targets)
		{
			QByteArray raw = half(quint16(targets.size()));
			for (quint32 target : targets) raw += word(target) + word(0);
			set(o, p, raw);
		}
		QByteArray build() const { return compact ? two.build() : one.build(); }
	private:
		BentoBuilder one;
		Bento2Builder two;
		bool compact, big;
	};
	inline QByteArray uid(quint32 number)
	{
		return BentoBuilder::le32(42) + BentoBuilder::le32(number) + BentoBuilder::le32(7);
	}
	inline QByteArray le64(quint64 number)
	{
		return BentoBuilder::le32(quint32(number)) + BentoBuilder::le32(quint32(number >> 32));
	}
	inline QByteArray sdii(bool omf2, bool ambiguousMaster = false, bool badDescriptor = false,
						   bool compact = false, bool big = false)
	{
		Writer w(compact, big);
		auto encodedUid = [&](quint32 n) { return w.word(42) + w.word(n) + w.word(7); };
		auto object = [&](const char *cls) {
			const quint32 obj = w.addObject(cls);
			if (omf2)
				w.setImmediate(obj, "OMFI:OOBJ:ObjClass", QByteArray(cls, 4));
			return obj;
		};
		auto ref = [&](quint32 obj, const char *prop, quint32 target) {
			if (omf2)
				w.set(obj, prop, w.word(target));
			else
				w.setHandle(obj, prop, target);
		};
		auto refs = [&](quint32 obj, const char *prop, quint32 target) {
			if (omf2)
				w.set(obj, prop, w.half(1) + w.word(target));
			else
				w.setHandles(obj, prop, {target});
		};
		const quint32 head = 1;
		w.setImmediate(head, omf2 ? "OMFI:OOBJ:ObjClass" : "OMFI:ObjID", QByteArray("HEAD", 4));
		w.setImmediate(head, omf2 ? "OMFI:HEAD:Version" : "OMFI:Version",
					   QByteArray::fromHex(omf2 ? "0200" : "0100"));
		w.setImmediate(head, omf2 ? "OMFI:HEAD:ByteOrder" : "OMFI:ByteOrder", QByteArray(big ? "MM" : "II", 2));
		const quint32 composition = object(omf2 ? "CMOB" : "MOBJ");
		w.set(composition, "OMFI:MOBJ:MobID", encodedUid(99));
		w.setString(composition, omf2 ? "OMFI:MOBJ:Name" : "OMFI:CPNT:Name", "Sequence, not master");
		const quint32 master = object(omf2 ? "MMOB" : "MOBJ");
		const quint32 file = object(omf2 ? "SMOB" : "MOBJ");
		const quint32 source = object(omf2 ? "SMOB" : "MOBJ");
		const quint32 desc = object("SD2D");
		const quint32 tape = object(omf2 ? "MDTP" : "MDES");
		for (auto pair : {qMakePair(master, 1u), qMakePair(file, 2u), qMakePair(source, 3u)})
			w.set(pair.first, "OMFI:MOBJ:MobID", encodedUid(pair.second));
		w.setString(master, omf2 ? "OMFI:MOBJ:Name" : "OMFI:CPNT:Name", "SDII clip");
		if (!omf2)
			w.setU32(master, "OMFI:MOBJ:UsageCode", 7);
		ref(file, omf2 ? "OMFI:SMOB:MediaDescription" : "OMFI:MOBJ:PhysicalMedia", badDescriptor ? 0xfefefefe : desc);
		ref(source, omf2 ? "OMFI:SMOB:MediaDescription" : "OMFI:MOBJ:PhysicalMedia", tape);
		const quint32 attrs = object("ATTR"), attr = object("ATTB"), loc = object("WINL");
		ref(source, omf2 ? "OMFI:MOBJ:UserAttributes" : "OMFI:CPNT:Attributes", attrs);
		refs(attrs, "OMFI:ATTR:AttrRefs", attr);
		w.setString(attr, "OMFI:ATTB:Name", "_PJ");
		w.setU32(attr, "OMFI:ATTB:Kind", 2);
		w.setString(attr, "OMFI:ATTB:StringAttribute", "SDII project");
		refs(tape, "OMFI:MDES:Locator", loc);
		w.setString(loc, "OMFI:WINL:PathName", "C:\\Original\\session.sd2");
		w.setU16(desc, "OMFI:SD2D:BitsPerSample", 24);
		w.setU16(desc, "OMFI:SD2D:NumChannels", 2);
		w.setRational(desc, "OMFI:MDFL:SampleRate", 48000, 1);
		w.set(desc, "OMFI:MDFL:Length", omf2 ? w.wide(96000) : w.word(96000));
		const quint32 data = object("SD2D");
		w.set(data, omf2 ? "OMFI:MDAT:MobID" : "OMFI:SD2D:MobID", encodedUid(2));
		auto track = [&](quint32 mob, quint32 component) {
			const quint32 slot = object(omf2 ? "MSLT" : "TRAK");
			refs(mob, omf2 ? "OMFI:MOBJ:Slots" : "OMFI:TRKG:Tracks", slot);
			ref(slot, omf2 ? "OMFI:MSLT:Segment" : "OMFI:TRAK:TrackComponent", component);
			w.setRational(omf2 ? slot : mob, omf2 ? "OMFI:MSLT:EditRate" : "OMFI:CPNT:EditRate", 25, 1);
		};
		auto link = [&](quint32 mob, quint32 target) {
			const quint32 clip = object("SCLP"), seq = object("SEQU");
			w.set(clip, "OMFI:SCLP:SourceID", encodedUid(target));
			refs(seq, omf2 ? "OMFI:SEQU:Components" : "OMFI:SEQU:Sequence", clip);
			track(mob, seq);
		};
		link(master, 2);
		link(file, 3);
		const quint32 tc = object("TCCP");
		w.set(tc, omf2 ? "OMFI:TCCP:Start" : "OMFI:TCCP:StartTC",
			  omf2 ? w.wide(0x10000002aULL) : w.word(90000));
		w.setU16(tc, "OMFI:TCCP:FPS", 25);
		w.set(tc, omf2 ? "OMFI:TCCP:Drop" : "OMFI:TCCP:Flags", omf2 ? QByteArray(1, '\0') : w.word(0));
		track(source, tc);
		if (ambiguousMaster)
		{
			const quint32 second = object(omf2 ? "MMOB" : "MOBJ");
			w.set(second, "OMFI:MOBJ:MobID", encodedUid(4));
			if (!omf2)
				w.setU32(second, "OMFI:MOBJ:UsageCode", 7);
			link(second, 2);
		}
		return w.build();
	}
}
