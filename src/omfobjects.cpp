// OMF-era (legacy Avid media, pre-MXF). The OMF Interchange object walker
// shared by the MXF-era MDB reader and the OMF-era readers — see
// omfobjects.h for what it covers. The walks below are the ones MdbParser
// carried privately until 2026-09-02, moved here and then taught the
// OMF-era cases (each tagged at the line); the decode notes that justify
// the MXF-era rules stay in mdbparser.cpp's header comment, which is where
// they were verified.
//
// MARK: - OMF-era facts the additions rest on (read from the fixtures)
//
//  - Codec. The .omf files carry NO EssenceCompression; their identity is
//    (DIDResolutionID, Compression 4CC). The msmMMOB.mdb MC 26.8 regenerates
//    beside them back-fills a label — Avid-private JFIF labels the app's
//    table does not know, and a SMPTE "uncompressed" label on ids 151/152/
//    2500 — so for an OMF-era (12-byte) file mob the OmfResolutions table
//    is consulted FIRST and the label only when the table has no row. That
//    keeps the database and the file naming a clip identically. An MXF-era
//    (32-byte) mob never reaches the table: its behaviour is unchanged.
//  - Audio. WAVD / AIFD carry no MDAU properties; channels, bits and rate
//    sit in OMFI:WAVD:Summary (a RIFF header, `bext` chunk first, then
//    `fill`, then `fmt `) and OMFI:AIFD:Summary (FORM/AIFC, `bext` then
//    `COMM`). Both are chunk-walked. SD2D has two u16 properties instead
//    (built to the MC binary's names; no specimen exists — UNVERIFIED).
//  - Timecode. The file mob's SCLP points at a source mob whose MobID is
//    12 bytes (2021 files) or a 32-byte UMID (the physical mob MC 2026
//    writes), so the hop accepts either width.
//  - Project. _PJ sits on the source mob in the 2021 slates and on the
//    file mob in MC 2026's files; the MDB reader tries the file mob first.

#include "omfobjects.h"
#include "mobid.h"
#include "omfresolutions.h"
#include "omfuid.h"

#include <QVector>
#include <QtEndian>
#include <cmath>
#include <limits>

namespace OmfObjects
{
	Revision revision(const BentoFile &b)
	{
		// VersionType is two bytes (major, minor), never byte-swapped:
		// public OMF toolkit omFile.c registration OMVersionType/kNeverSwab.
		for (const char *name : {"OMFI:HEAD:Version", "OMFI:Version"})
		{
			const int prop = b.propertyId(name);
			for (quint32 obj : b.objectsWithProperty(prop))
			{
				if (b.objectClass(obj) != "HEAD")
					continue;
				const QByteArray v = b.bytes(obj, prop);
				if (v.size() != 2)
					return Revision::Unknown;
				if (v[0] == 1)
					return Revision::Omf1;
				if (v[0] == 2)
					return Revision::Omf2;
				// Avid's legacy CloseContainer writes the native halfword
				// 0x0100 as two immediate bytes (MC26.8 arm64: 0xe28e8,
				// 0xe2a90–0xe2ab8), producing 00 01 in its OMF1 files.
				// The public toolkit also routes this pre-2 value through
				// its OMF1 reader. Recognise only that observed legacy form;
				// do not swap standard major/minor bytes or infer other aliases.
				if (qstrcmp(name, "OMFI:Version") == 0 && v == QByteArray::fromHex("0001") &&
					b.hasProperty(obj, b.propertyId("OMFI:ObjID")) &&
					!b.hasProperty(obj, b.propertyId("OMFI:OOBJ:ObjClass")))
					return Revision::Omf1;
				return Revision::Unknown;
			}
		}
		return Revision::Unknown; // schema can still be read from its named properties
	}

	QByteArray normalizedMobId(const BentoFile &b, QByteArrayView raw)
	{
		QByteArray out = raw.toByteArray();
		if (out.size() == OmfUid::kUidSize && b.isBigEndian())
			for (int offset = 0; offset < OmfUid::kUidSize; offset += 4)
				qToLittleEndian<quint32>(qFromBigEndian<quint32>(out.constData() + offset), out.data() + offset);
		return out;
	}

	bool isMobClass(const QByteArray &cls)
	{
		return cls == "MOBJ" || cls == "MMOB" || cls == "SMOB" || cls == "CMOB";
	}

	// MARK: - Property ids, resolved once per file

	Props::Props(const BentoFile &b)
		: revision(OmfObjects::revision(b)),
		  omf2(revision == Revision::Omf2 || (revision == Revision::Unknown && b.propertyId("OMFI:OOBJ:ObjClass") >= 0)),
		  mobId(b.propertyId("OMFI:MOBJ:MobID")), usage(b.propertyId("OMFI:MOBJ:UsageCode")),
		  name(b.propertyId("OMFI:CPNT:Name")), editRate(b.propertyId("OMFI:CPNT:EditRate")),
		  attrs(b.propertyId("OMFI:CPNT:Attributes")), physMedia(b.propertyId("OMFI:MOBJ:PhysicalMedia")),
		  attrRefs(b.propertyId("OMFI:ATTR:AttrRefs")), attbName(b.propertyId("OMFI:ATTB:Name")),
		  attbKind(b.propertyId("OMFI:ATTB:Kind")), attbInt(b.propertyId("OMFI:ATTB:IntAttribute")),
		  attbString(b.propertyId("OMFI:ATTB:StringAttribute")), attbObj(b.propertyId("OMFI:ATTB:ObjAttribute")),
		  binNameUtf8(b.propertyId("OMFI:MCBR:MC:binNameUTF8")), binName(b.propertyId("OMFI:MCBR:MC:binName")),
		  posixPath(b.propertyId("OMFI:FL:POSIXPathName")), pathUtf8(b.propertyId("OMFI:FL:PathNameUTF8")),
		  pathName(b.propertyId("OMFI:FL:PathName")), essComp(b.propertyId("OMFI:DIDD:EssenceCompression")),
		  resId(b.propertyId("OMFI:DIDD:DIDResolutionID")), width(b.propertyId("OMFI:DIDD:StoredWidth")),
		  height(b.propertyId("OMFI:DIDD:StoredHeight")), layout(b.propertyId("OMFI:DIDD:FrameLayout")),
		  sampleRate(b.propertyId("OMFI:MDFL:SampleRate")), length(b.propertyId("OMFI:MDFL:Length")),
		  compWidth(b.propertyId("OMFI:CDCI:ComponentWidth")), bits(b.propertyId("OMFI:MDAU:BitsPerSample")),
		  channels(b.propertyId("OMFI:MDAU:NumChannels")), tracks(b.propertyId("OMFI:TRKG:Tracks")),
		  trackComp(b.propertyId("OMFI:TRAK:TrackComponent")), sequence(b.propertyId("OMFI:SEQU:Sequence")),
		  sourceId(b.propertyId("OMFI:SCLP:SourceID")), tcFlags(b.propertyId("OMFI:TCCP:Flags")),
		  // OMF-era: the properties only the legacy descriptors and essence files carry.
		  compression(b.propertyId("OMFI:DIDD:Compression")), wavdSummary(b.propertyId("OMFI:WAVD:Summary")),
		  aifdSummary(b.propertyId("OMFI:AIFD:Summary")), sd2dBits(b.propertyId("OMFI:SD2D:BitsPerSample")),
		  sd2dChannels(b.propertyId("OMFI:SD2D:NumChannels")), tcFps(b.propertyId("OMFI:TCCP:FPS")),
		  tcStart(b.propertyId("OMFI:TCCP:StartTC")), mdatMobId(b.propertyId("OMFI:MDAT:MobID")),
		  waveMobId(b.propertyId("OMFI:WAVE:MobID")), aifcMobId(b.propertyId("OMFI:AIFC:MobID")),
		  sd2mMobId(b.propertyId("OMFI:SD2M:MobID")), mobKind(b.propertyId("OMFI:MDES:MobKind")),
		  unxlPath(b.propertyId("OMFI:UNXL:PathName")), locator(b.propertyId("OMFI:MDES:Locator")),
		  lastKnownVolumeUtf8(b.propertyId("OMFI:MSML:LastKnownVolumeUTF8")),
		  lastKnownVolume(b.propertyId("OMFI:MSML:LastKnownVolume")),
		  sd2dMobId(b.propertyId("OMFI:SD2D:MobID")), slotRate(b.propertyId("OMFI:MSLT:EditRate")),
		  nestedSlots(b.propertyId("OMFI:NEST:Slots")), selected(b.propertyId("OMFI:SLCT:Selected")),
		  choices(b.propertyId("OMFI:MGRP:Choices")), inputSegment(b.propertyId("OMFI:ERAT:InputSegment")),
		  winlPath(b.propertyId("OMFI:WINL:PathName")), maclPath(b.propertyId("OMFI:MACL:PathName")),
		  tiffSummary(b.propertyId("OMFI:TIFD:Summary")),
		  rgbaLayout(b.propertyId("OMFI:RGBA:PixelLayout")),
		  rgbaStructure(b.propertyId("OMFI:RGBA:PixelStructure"))
	{
		if (omf2)
		{
			name = b.propertyId("OMFI:MOBJ:Name");
			attrs = b.propertyId("OMFI:MOBJ:UserAttributes");
			physMedia = b.propertyId("OMFI:SMOB:MediaDescription");
			tracks = b.propertyId("OMFI:MOBJ:Slots");
			trackComp = b.propertyId("OMFI:MSLT:Segment");
			sequence = b.propertyId("OMFI:SEQU:Components");
			tcFlags = b.propertyId("OMFI:TCCP:Drop");
			tcStart = b.propertyId("OMFI:TCCP:Start");
		}
	}

	// MARK: - Descriptor classes

	bool isAudioClass(const QByteArray &cls)
	{
		return cls == "PCMA" || cls == "MPGA" || cls == "WAVE" ||
			   cls == "WAVD" || cls == "AIFD" || cls == "SD2D"; // OMF-era: the essence-file audio descriptors.
	}

	bool isMediaClass(const QByteArray &cls)
	{
		return isAudioClass(cls) || cls == "CDCI" || cls == "MPGI" || cls == "RGBA" || cls == "JPED" || cls == "TIFD";
	}

	bool isLocatorClass(const QByteArray &cls)
	{
		return cls == "MACL" || cls == "WINL" || cls == "UNXL"; // OMF-era: WINL/UNXL beside the MDB's MACL.
	}

	// MARK: - Codec label

	QByteArray auidToUl(QByteArrayView auid)
	{
		if (auid.size() != 16)
			return {};
		QByteArray ul;
		ul.reserve(16);
		ul.append(auid.data() + 8, 8);
		for (int i = 3; i >= 0; --i)
			ul.append(auid[i]);
		ul.append(auid[5]);
		ul.append(auid[4]);
		ul.append(auid[7]);
		ul.append(auid[6]);
		return ul;
	}

	QByteArray ulFromResId(quint32 resId)
	{
		if (resId <= 1234 || resId >= 1490)
			return {};
		// Gap fix: 1244 (DNx TR, 1440x540i) was registered under version
		// byte 0x0D, not 0x0A — the only DNxHD id whose 0x0A spelling names
		// nothing in MxfParser's table. Both eras hit this.
		if (resId == 1244)
			return QByteArray::fromHex("060E2B340401010D04010202710A0000");
		QByteArray ul = QByteArray::fromHex("060e2b340401010a04010202");
		ul.append(char(0x71));
		ul.append(char(resId - 1234));
		ul.append(char(0));
		ul.append(char(0));
		return ul;
	}

	// MARK: - OMF-era audio header blobs

	namespace
	{
		struct TiffSummary
		{
			int width = 0, height = 0, bits = 0, layout = -1;
			QString codec;
		};

		TiffSummary readTiffSummary(QByteArrayView raw)
		{
			TiffSummary result;
			if (raw.size() < 8 || (raw.first(2) != QByteArrayView("II") && raw.first(2) != QByteArrayView("MM")))
				return result;
			const bool big = raw.first(2) == QByteArrayView("MM");
			auto u16 = [&](qsizetype p)
			{ return big ? qFromBigEndian<quint16>(raw.data() + p) : qFromLittleEndian<quint16>(raw.data() + p); };
			auto u32 = [&](qsizetype p)
			{ return big ? qFromBigEndian<quint32>(raw.data() + p) : qFromLittleEndian<quint32>(raw.data() + p); };
			if (u16(2) != 42)
				return result;
			const qsizetype ifd = u32(4);
			if (ifd > raw.size() - 2)
				return result;
			const quint16 count = u16(ifd);
			if (qsizetype(count) * 12 > raw.size() - ifd - 2)
				return result;
			bool avid = false;
			for (int n = 0; n < count; ++n)
			{
				const auto tag = u16(ifd + 2 + n * 12);
				avid |= tag >= 34432 && tag <= 34436;
			}
			// Avid/toolkit TIFF writes immediate SHORTs in the low numeric
			// half of a LONG, including big-endian files (omcTIFF.c ReadIFD).
			for (int n = 0; n < count; ++n)
			{
				const qsizetype e = ifd + 2 + n * 12;
				const quint16 tag = u16(e), type = u16(e + 2);
				const quint32 items = u32(e + 4), value = u32(e + 8);
				if (tag == 258 && type == 3 && items > 0 && items <= 16)
				{
					const qsizetype start = items * 2 <= 4 ? e + 8 : value;
					if (start <= raw.size() && items * 2 <= quint64(raw.size() - start))
					{
						const int bits = items == 1 && avid ? int(value) : int(u16(start));
						bool uniform = true;
						for (quint32 i = 1; i < items; ++i)
							uniform &= u16(start + i * 2) == bits;
						if (uniform)
							result.bits = bits;
					}
				}
				if (items != 1 || (type != 3 && type != 4))
					continue;
				const quint32 scalar = type == 3 && !avid ? u16(e + 8) : value;
				if (scalar > quint32(std::numeric_limits<int>::max()))
					continue;
				if (tag == 256)
					result.width = int(scalar);
				else if (tag == 257)
					result.height = int(scalar);
				else if (tag == 259 && scalar == 1)
					result.codec = QStringLiteral("Uncompressed (TIFF)");
				else if (tag == 259 && scalar == 6)
					result.codec = QStringLiteral("JPEG (TIFF)");
				else if (tag == 34434)
					result.layout = scalar == 4 ? 1 : scalar == 1			   ? 0
												  : scalar == 2 || scalar == 3 ? 2
																			   : -1;
			}
			return result;
		}

		/// OMF-era: an 80-bit IEEE extended (the AIFF sample-rate field) as
		/// a double: sign | 15-bit exponent | 64-bit mantissa with the
		/// integer bit explicit. 48 kHz is 400E BB80 0000 0000 0000.
		double extendedToDouble(const unsigned char *x)
		{
			const int exponent = ((x[0] & 0x7F) << 8) | x[1];
			const quint64 mantissa = qFromBigEndian<quint64>(x + 2);
			if (exponent == 0 && mantissa == 0)
				return 0.0;
			double v = std::ldexp(double(mantissa), exponent - 16383 - 63);
			return (x[0] & 0x80) ? -v : v;
		}
	} // namespace

	WaveSummary readWaveSummary(QByteArrayView blob)
	{
		WaveSummary s;
		if (blob.size() < 12 || !blob.startsWith(QByteArrayView("RIFF")) || blob.mid(8, 4) != QByteArrayView("WAVE"))
			return s;
		const auto *d = reinterpret_cast<const unsigned char *>(blob.data());
		qsizetype pos = 12;
		while (pos + 8 <= blob.size())
		{
			const QByteArrayView id = blob.mid(pos, 4);
			const quint32 size = qFromLittleEndian<quint32>(d + pos + 4);
			if (id == QByteArrayView("fmt "))
			{
				if (size < 16 || pos + 8 + 16 > blob.size())
					return s;
				const unsigned char *f = d + pos + 8;
				s.formatTag = qFromLittleEndian<quint16>(f);
				s.channels = qFromLittleEndian<quint16>(f + 2);
				s.sampleRate = int(qFromLittleEndian<quint32>(f + 4));
				s.bits = qFromLittleEndian<quint16>(f + 14);
				s.valid = s.channels > 0 && s.sampleRate > 0;
				return s;
			}
			if (id == QByteArrayView("data"))
				return s;							 // the essence — nothing useful lies past it
			pos += 8 + qsizetype(size) + (size & 1); // RIFF pads odd chunks
		}
		return s;
	}

	AifcSummary readAifcSummary(QByteArrayView blob)
	{
		AifcSummary s;
		if (blob.size() < 12 || !blob.startsWith(QByteArrayView("FORM")))
			return s;
		const QByteArrayView form = blob.mid(8, 4);
		if (form != QByteArrayView("AIFC") && form != QByteArrayView("AIFF"))
			return s;
		const auto *d = reinterpret_cast<const unsigned char *>(blob.data());
		qsizetype pos = 12;
		while (pos + 8 <= blob.size())
		{
			const QByteArrayView id = blob.mid(pos, 4);
			const quint32 size = qFromBigEndian<quint32>(d + pos + 4);
			if (id == QByteArrayView("COMM"))
			{
				// i16 channels | u32 frames | i16 bits | 80-bit rate [| 4CC compType | pstring name]
				if (size < 18 || pos + 8 + 18 > blob.size())
					return s;
				const unsigned char *c = d + pos + 8;
				s.channels = qFromBigEndian<qint16>(c);
				s.frames = qFromBigEndian<quint32>(c + 2);
				s.bits = qFromBigEndian<qint16>(c + 6);
				const double rate = extendedToDouble(c + 8);
				if (std::isfinite(rate) && rate > 0 && rate <= std::numeric_limits<int>::max())
					s.sampleRate = int(std::llround(rate));
				if (size >= 22 && pos + 8 + 22 <= blob.size())
					s.compressionType = QByteArray(reinterpret_cast<const char *>(c + 18), 4);
				s.valid = s.channels > 0 && s.sampleRate > 0;
				return s;
			}
			if (id == QByteArrayView("SSND"))
				return s;
			pos += 8 + qsizetype(size) + (size & 1); // IFF pads odd chunks
		}
		return s;
	}

	// MARK: - Attribute trees

	QString locatorPath(const BentoFile &b, const Props &p, quint32 locator)
	{
		QString path = BentoFile::string(b.bytes(locator, p.posixPath));
		if (path.isEmpty())
			path = BentoFile::utf8String(b.bytes(locator, p.pathUtf8));
		if (path.isEmpty())
			path = BentoFile::string(b.bytes(locator, p.pathName));
		if (path.isEmpty())
			path = BentoFile::string(b.bytes(locator, p.unxlPath)); // OMF-era: UNXL keeps its own property.
		if (path.isEmpty() && b.objectClass(locator) == "WINL")
			path = BentoFile::string(b.bytes(locator, p.winlPath));
		if (path.isEmpty() && b.objectClass(locator) == "MACL")
			path = BentoFile::string(b.bytes(locator, p.maclPath));
		return path;
	}

	void walkAttributes(const BentoFile &b, const Props &p, quint32 attrObj, Attributes &a, QSet<quint32> &seen,
						int depth)
	{
		if (attrObj == 0 || depth > 4 || seen.contains(attrObj))
			return;
		seen.insert(attrObj);
		const QVector<quint32> attbs = b.refs(attrObj, p.attrRefs);
		for (quint32 attb : attbs)
		{
			const QString name = BentoFile::string(b.bytes(attb, p.attbName));
			const quint32 kind = b.uintValue(b.bytes(attb, p.attbKind));
			if (kind == 3)
			{
				const quint32 target = b.ref(attb, p.attbObj);
				if (target == 0)
					continue;
				const QByteArray cls = b.objectClass(target);
				if (name == QLatin1String("_ORG_BIN") && cls == "MCBR")
				{
					if (a.bin.isEmpty())
					{
						a.bin = BentoFile::utf8String(b.bytes(target, p.binNameUtf8));
						if (a.bin.isEmpty())
							a.bin = BentoFile::string(b.bytes(target, p.binName));
					}
				}
				else if (name == QLatin1String("_IMPORTSETTING"))
				{
					a.isImported = true;
					if (cls == "ATTR")
						walkAttributes(b, p, target, a, seen, depth + 1);
				}
				else if (name == QLatin1String("_SRCFILE") && (cls == "MACL" || (a.omfEra && isLocatorClass(cls))))
				{
					// OMF-era: WINL/UNXL are admitted only for an OMF-era mob
					// (Attributes::omfEra); the MXF-era rule stays MACL-only.
					if (a.sourceFilePath.isEmpty())
						a.sourceFilePath = locatorPath(b, p, target);
				}
				else if (name == QLatin1String("_MEDIAFILE") && isLocatorClass(cls))
				{
					// OMF-era: the essence file's own locator (diagnostic only).
					if (a.mediaFilePath.isEmpty())
						a.mediaFilePath = locatorPath(b, p, target);
				}
				else if (name == QLatin1String("_USER") && cls == "ATTR")
				{
					walkAttributes(b, p, target, a, seen, depth + 1);
				}
			}
			else if (kind == 2)
			{
				if (name == QLatin1String("Video") || name == QLatin1String("Audio"))
				{
					if (a.sourceContainer.isEmpty())
						a.sourceContainer = BentoFile::string(b.bytes(attb, p.attbString));
				}
				else if (name == QLatin1String("UNC Path"))
				{
					if (a.sourceFilePath.isEmpty())
						a.sourceFilePath = BentoFile::string(b.bytes(attb, p.attbString));
				}
				else if (name == QLatin1String("_PJ"))
				{
					// OMF-era: the project name, on the file or source mob of an OMF file
					// (an MXF-era row gets it from the PMR instead).
					if (a.project.isEmpty())
						a.project = BentoFile::string(b.bytes(attb, p.attbString));
				}
			}
		}
	}

	namespace
	{
		// Unlike the permissive metadata walkers, a classification cannot turn
		// unreadable or null array entries into evidence that an item is absent.
		bool completeRefs(const BentoFile &b, quint32 object, int property, QVector<quint32> &out)
		{
			BentoFile::ReadStatus status;
			out = b.refs(object, property, &status);
			if (status != BentoFile::ReadStatus::Ok)
				return false;
			const auto raw = b.read(object, property);
			return raw.ok() && raw.data.size() >= 2 &&
				   out.size() == b.uintValue(QByteArrayView(raw.data).first(2));
		}

		AvidPrecompute::ImportAttribute directImportAttribute(const BentoFile &b, const Props &p, quint32 master)
		{
			using Import = AvidPrecompute::ImportAttribute;
			BentoFile::ReadStatus status;
			const quint32 attrs = b.ref(master, p.attrs, &status);
			if (status == BentoFile::ReadStatus::Missing || (status == BentoFile::ReadStatus::Ok && attrs == 0))
				return Import::Absent;
			if (status != BentoFile::ReadStatus::Ok || b.objectClass(attrs) != "ATTR")
				return Import::Unknown;
			QVector<quint32> references;
			if (!completeRefs(b, attrs, p.attrRefs, references))
				return Import::Unknown;
			Import result = Import::Absent;
			bool foundName = false;
			for (quint32 attr : references)
			{
				if (b.objectClass(attr) != "ATTB")
					return Import::Unknown;
				const auto name = b.read(attr, p.attbName);
				if (!name.ok() || name.data.isEmpty() || name.data.indexOf('\0') != name.data.size() - 1)
					return Import::Unknown;
				if (name.data != QByteArray("_IMPORTSETTING\0", 15))
					continue;
				const auto kind = b.read(attr, p.attbKind);
				if (!kind.ok() || kind.data.size() != 2)
					return Import::Unknown;
				// Old toolkit readers sometimes infer a zero kind from its
				// payload. That legacy conversion is not verified for MC26.8.
				if (b.uintValue(kind.data) == 0)
					return Import::Unknown;
				const Import current = b.uintValue(kind.data) == 3 ? Import::Present : Import::Absent;
				if (foundName && result != current)
					return Import::Conflicting;
				foundName = true;
				result = current;
				// MC's FindObject returns its found byte for exact name + kind 3;
				// the object payload may be null. Neither it nor nested _USER
				// attributes enter the Media Tool subdivision predicate.
			}
			return result;
		}

		int directVideoTrackCount(const BentoFile &b, const Props &p, quint32 master)
		{
			QVector<quint32> tracks;
			if (!completeRefs(b, master, p.tracks, tracks))
				return -1;
			const int trackKind = b.propertyId("OMFI:CPNT:TrackKind");
			int count = 0;
			for (quint32 track : tracks)
			{
				if (b.objectClass(track) != "TRAK")
					return -1;
				BentoFile::ReadStatus status;
				const quint32 component = b.ref(track, p.trackComp, &status);
				if (status != BentoFile::ReadStatus::Ok || component == 0 || b.objectClass(component).isEmpty())
					return -1;
				const auto kind = b.read(component, trackKind);
				if (!kind.ok() || kind.data.size() != 2)
					return -1;
				if (b.uintValue(kind.data) == 1)
					++count;
			}
			return count;
		}
	} // namespace

	AvidPrecompute::Category precomputeCategory(const BentoFile &b, const Props &p,
												const QVector<quint32> &masters)
	{
		using Category = AvidPrecompute::Category;
		// The verified binary mapping is CPNT:TrackKind -> AComponent::GetType.
		// OMF2 MOBJ:UserAttributes is not proven equivalent to this direct
		// list: toolkit omfiMobAppendComment maps it from OMF1's nested _USER
		// attributes. Do not infer the import-marker predicate from that alias.
		if (p.revision != Revision::Omf1 || p.omf2 || masters.isEmpty())
			return Category::Unknown;
		Category result = Category::Unknown;
		bool first = true;
		for (quint32 master : masters)
		{
			const auto usage = b.read(master, p.usage);
			if (b.objectClass(master) != "MOBJ" || b.hasProperty(master, p.physMedia) ||
				!usage.ok() || usage.data.size() != 4 || b.uintValue(usage.data) != 1)
				return Category::Unknown;
			AvidPrecompute::Evidence evidence;
			evidence.importAttribute = directImportAttribute(b, p, master);
			// Match MC's short circuit: confirmed absence settles Rendered
			// Effects even when the video-track count is not available.
			if (evidence.importAttribute == AvidPrecompute::ImportAttribute::Present)
				evidence.videoTrackCount = directVideoTrackCount(b, p, master);
			const Category current = AvidPrecompute::classify(evidence);
			if (current == Category::Unknown || (!first && result != current))
				return Category::Unknown;
			result = current;
			first = false;
		}
		return result;
	}

	// MARK: - Track walk (timecode, source mob)

	namespace
	{
		/// OMF-era: a SourceID is written in the width of the mob it names —
		/// 12 bytes for an omfi:UID, 32 for the UMID MC 2026 puts on the
		/// physical mob — so both widths are valid hop keys.
		bool isSourceIdWidth(qsizetype size)
		{
			return size == MobId::kRawSize || size == OmfUid::kUidSize;
		}
	} // namespace

	namespace
	{
		QVector<quint32> components(const BentoFile &b, const Props &p, quint32 mob)
		{
			QVector<quint32> stack, out;
			for (quint32 track : b.refs(mob, p.tracks))
				stack.append(b.ref(track, p.trackComp));
			QSet<quint32> seen;
			while (!stack.isEmpty() && seen.size() < 100000)
			{
				const quint32 obj = stack.takeLast();
				if (!obj || seen.contains(obj))
					continue;
				seen.insert(obj);
				out.append(obj);
				const QByteArray cls = b.objectClass(obj);
				if (cls == "SEQU")
					stack += b.refs(obj, p.sequence);
				else if (p.omf2 && cls == "NEST")
					stack += b.refs(obj, p.nestedSlots);
				else if (p.omf2 && cls == "SLCT")
					stack.append(b.ref(obj, p.selected));
				else if (p.omf2 && cls == "MGRP")
					stack += b.refs(obj, p.choices);
				else if (p.omf2 && cls == "ERAT")
					stack.append(b.ref(obj, p.inputSegment));
			}
			return stack.isEmpty() ? out : QVector<quint32>(); // never treat a bounded partial walk as complete
		}
	}

	QVector<quint32> sourceMobs(const BentoFile &b, const Props &p, quint32 mob, const ObjectByMob &objectByMob)
	{
		QVector<quint32> out;
		for (quint32 c : components(b, p, mob))
		{
			if (b.objectClass(c) != "SCLP")
				continue;
			const QByteArray src = normalizedMobId(b, b.bytes(c, p.sourceId));
			if (!isSourceIdWidth(src.size()) || src == QByteArray(src.size(), '\0'))
				continue; // 0-0-0 is the original source, not a mob reference
			const quint32 target = objectByMob.value(src, 0);
			if (target && target != mob && !out.contains(target))
				out.append(target);
		}
		return out;
	}

	quint32 findTimecodeComponent(const BentoFile &b, const Props &p, quint32 mob, const ObjectByMob &objectByMob,
								  QSet<quint32> &seen, int depth)
	{
		if (!mob || depth > 64 || seen.contains(mob))
			return 0;
		seen.insert(mob);
		for (quint32 c : components(b, p, mob))
			if (b.objectClass(c) == "TCCP")
				return c;
		for (quint32 source : sourceMobs(b, p, mob, objectByMob))
			if (const quint32 tc = findTimecodeComponent(b, p, source, objectByMob, seen, depth + 1))
				return tc;
		return 0;
	}

	quint32 findSourceMob(const BentoFile &b, const Props &p, quint32 mob, const ObjectByMob &objectByMob)
	{
		const auto sources = sourceMobs(b, p, mob, objectByMob);
		return sources.size() == 1 ? sources.first() : 0;
	}

	bool mobEditRate(const BentoFile &b, const Props &p, quint32 mob, qint32 &num, qint32 &den)
	{
		if (!p.omf2)
			return b.rationalValue(b.bytes(mob, p.editRate), num, den);
		// Slots own rates in OMF2. A mob has no single rate when they disagree.
		bool found = false;
		for (quint32 slot : b.refs(mob, p.tracks))
		{
			qint32 n = 0, d = 0;
			if (!b.rationalValue(b.bytes(slot, p.slotRate), n, d) || n <= 0 || d <= 0)
				continue;
			if (found && qint64(num) * d != qint64(n) * den)
				return false;
			num = n;
			den = d;
			found = true;
		}
		return found;
	}

	Timecode readTimecode(const BentoFile &b, const Props &p, quint32 tccp)
	{
		Timecode t;
		if (tccp == 0)
			return t;
		t.found = true;
		t.dropFrame = b.uintValue(b.bytes(tccp, p.tcFlags)) != 0;
		// OMF-era: start and rate, surfaced by the essence-file reader.
		const QByteArray start = b.bytes(tccp, p.tcStart);
		if (!start.isEmpty())
			t.start = b.int64Value(start);
		t.fps = int(b.uintValue(b.bytes(tccp, p.tcFps)));
		return t;
	}

	// MARK: - Descriptor

	bool readDescriptor(const BentoFile &b, const Props &p, quint32 mobObj, quint32 desc,
						const ObjectByMob &objectByMob, MxfMetadata &e, bool *codecKnown)
	{
		if (codecKnown)
			*codecKnown = false;
		const QByteArray cls = b.objectClass(desc);
		if (!isMediaClass(cls))
			return false;
		e.isAudio = isAudioClass(cls);
		e.pcmDescriptor = cls == "PCMA" || cls == "WAVE" || cls == "SD2D";

		// Codec label: the stored AUID, else rebuilt from the resolution id.
		const quint32 resId = b.uintValue(b.bytes(desc, p.resId));
		QByteArray label = auidToUl(b.bytes(desc, p.essComp));
		if (label.isEmpty())
			label = ulFromResId(resId);

		// OMF-era: for a 12-byte (omfi:UID) file mob the (resolution id, 4CC)
		// pair is the codec's identity and the OmfResolutions table wins —
		// the .omf carries no label, and the label MC 26.8 back-fills in the
		// MDB is one the app's table cannot name. The bare name is pre-filled
		// with the label left empty, which is exactly the case finalise keeps
		// `codec` for. A 32-byte (MXF-era) mob never reaches this branch.
		bool tableHit = false;
		if (!e.isAudio && b.bytes(mobObj, p.mobId).size() == OmfUid::kUidSize)
		{
			const QString bare = OmfResolutions::name(resId, b.bytes(desc, p.compression));
			if (!bare.isEmpty())
			{
				e.codec = bare;
				label.clear();
				tableHit = true;
			}
		}
		e.essenceContainerLabel = label;
		// Avid alpha-only database descriptors explicitly store NONE and
		// separate A / 8 component arrays. This works with either MobID width
		// and OMF revision; missing or unreadable compression is not NONE.
		if (cls == "RGBA" && e.codec.isEmpty() && label.isEmpty() &&
			!b.hasProperty(desc, p.essComp))
		{
			const auto compression = b.read(desc, p.compression);
			const auto pixels = b.read(desc, p.rgbaLayout, 16);
			const auto depths = b.read(desc, p.rgbaStructure, 16);
			const auto oneComponent = [](const QByteArray &value, char expected)
			{
				return value == QByteArray(1, expected) || value == QByteArray(1, expected) + '\0';
			};
			if (compression.ok() && BentoFile::string(compression.data) == QLatin1String("NONE") &&
				pixels.ok() && depths.ok() && oneComponent(pixels.data, 'A') && oneComponent(depths.data, 8))
			{
				e.codec = QStringLiteral("Uncompressed alpha");
				e.bitDepth = QStringLiteral("8-bit");
				tableHit = true;
			}
		}
		// OMF2 DIDD compression is a named algorithm. The specification
		// explicitly defines an absent property as uncompressed; apply that
		// only to these standard image descriptors, not unknown video classes.
		if (p.omf2 && e.codec.isEmpty() && label.isEmpty() && (cls == "CDCI" || cls == "RGBA"))
		{
			const auto compression = b.read(desc, p.compression);
			if (compression.status == BentoFile::ReadStatus::Missing)
				e.codec = QStringLiteral("Uncompressed");
			else if (compression.ok() && BentoFile::string(compression.data) == QLatin1String("JPEG"))
				e.codec = QStringLiteral("JPEG");
			tableHit = !e.codec.isEmpty();
		}
		if (codecKnown)
			*codecKnown = !label.isEmpty() || tableHit || e.pcmDescriptor;

		// Rate. Video goes through the same label matcher as tag 0x3001.
		qint32 num = 0, den = 0;
		if (b.rationalValue(b.bytes(desc, p.sampleRate), num, den) && den > 0 && num > 0)
			MxfParser::applyEditRate(e, quint32(num), quint32(den));

		const qint64 length = b.int64Value(b.bytes(desc, p.length));
		if (e.isAudio)
		{
			// OMF-era: the codec column shows Avid's own label for legacy
			// audio — "WAVE (OMF)", "AIFF-C (OMF)", "SDII" (see
			// OmfResolutions::audioName). Pre-filled here because finalise
			// only names a codec that is still empty, which is also what
			// keeps verified PCMA/WAVE descriptors on PCM while MPGA stays unknown.
			if (e.codec.isEmpty())
				e.codec = OmfResolutions::audioName(cls);
			if (codecKnown && !e.codec.isEmpty())
				*codecKnown = true;

			// OMF-era: WAVD / AIFD keep channels, bits and rate in a header
			// blob; SD2D in two properties of its own. Read them up front so
			// a missing MDFL:SampleRate can still fall back to the blob's
			// rate before the duration maths; every field only fills a gap.
			int blobRate = 0, blobBits = 0, blobChannels = 0;
			if (cls == "WAVD")
			{
				const WaveSummary s = readWaveSummary(b.bytes(desc, p.wavdSummary));
				if (s.valid)
				{
					blobRate = s.sampleRate;
					blobBits = s.bits;
					blobChannels = s.channels;
				}
			}
			else if (cls == "AIFD")
			{
				const AifcSummary s = readAifcSummary(b.bytes(desc, p.aifdSummary));
				if (s.valid)
				{
					blobRate = s.sampleRate;
					blobBits = s.bits;
					blobChannels = s.channels;
				}
			}
			else if (cls == "SD2D")
			{
				// SD2D fields registered by Media Composer; constructed coverage, no real SDII specimen yet.
				blobBits = int(b.uintValue(b.bytes(desc, p.sd2dBits)));
				blobChannels = int(b.uintValue(b.bytes(desc, p.sd2dChannels)));
			}
			if (e.sampleRate <= 0 && blobRate > 0)
				e.sampleRate = blobRate; // OMF-era: the blob's rate when MDFL:SampleRate is absent.

			// Length is samples. The frame count comes from the mob's own edit
			// rate; finalise then re-derives the timecode base from these two
			// exactly as it does for a header (frames × rate ÷ samples).
			e.descriptorDuration = length;
			qint32 erNum = 0, erDen = 0;
			if (mobEditRate(b, p, mobObj, erNum, erDen) && erDen > 0 && erNum > 0 &&
				length > 0 && e.sampleRate > 0)
			{
				const long double frames = static_cast<long double>(length) * erNum / erDen / e.sampleRate;
				if (frames <= std::numeric_limits<qint64>::max() - 1.0L)
					e.durationFrames = qint64(std::round(frames));
			}
			if (const quint32 bits = b.uintValue(b.bytes(desc, p.bits)))
				e.bitDepth = MxfParser::bitDepthLabel(bits);
			e.channels = int(b.uintValue(b.bytes(desc, p.channels)));
			// OMF-era: the MDAU properties above are absent on the legacy
			// descriptors, so the blob supplies what they left empty.
			if (e.bitDepth.isEmpty() && blobBits > 0)
				e.bitDepth = MxfParser::bitDepthLabel(quint32(blobBits));
			if (e.channels <= 0 && blobChannels > 0)
				e.channels = blobChannels;
		}
		else
		{
			const TiffSummary tiff = cls == "TIFD" ? readTiffSummary(b.bytes(desc, p.tiffSummary)) : TiffSummary();
			e.durationFrames = length;
			e.width = int(b.uintValue(b.bytes(desc, p.width)));
			int height = int(b.uintValue(b.bytes(desc, p.height)));
			const QByteArray layoutV = b.bytes(desc, p.layout);
			const int layout = layoutV.isEmpty() ? tiff.layout : int(b.uintValue(layoutV));
			if (cls == "TIFD")
			{
				e.width = tiff.width;
				height = tiff.height;
				e.codec = tiff.codec;
				if (tiff.bits > 0)
					e.bitDepth = MxfParser::bitDepthLabel(quint32(tiff.bits));
				if (codecKnown)
					*codecKnown = !e.codec.isEmpty();
			}
			// Captured Avid OMF1 MDBs store half heights for layouts1 and3.
			// Standard OMF2 only separates fields for layout1; mixed fields
			// are atomic full frames (OMF2.1 section Properties Describing Interleaving).
			if (height > 0 && (layout == 1 || (!p.omf2 && layout == 3)))
				height = height <= std::numeric_limits<int>::max() / 2 ? height * 2 : 0;
			e.height = height;
			e.frameLayout = layout;
			e.heightIsFrameHeight = height > 0;
			if (const quint32 bits = b.uintValue(b.bytes(desc, p.compWidth)))
				e.bitDepth = MxfParser::bitDepthLabel(bits);
		}

		// Drop frame: the timecode component is on a source mob, reached
		// through the file mob's SCLP references.
		QSet<quint32> seen;
		if (const quint32 tccp = findTimecodeComponent(b, p, mobObj, objectByMob, seen, 0))
			e.dropFrame = b.uintValue(b.bytes(tccp, p.tcFlags)) != 0;

		MxfParser::finalise(e);
		return true;
	}
} // namespace OmfObjects
