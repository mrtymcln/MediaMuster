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

namespace OmfObjects
{
	// MARK: - Property ids, resolved once per file

	Props::Props(const BentoFile &b)
		: mobId(b.propertyId("OMFI:MOBJ:MobID")), usage(b.propertyId("OMFI:MOBJ:UsageCode")),
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
		  lastKnownVolume(b.propertyId("OMFI:MSML:LastKnownVolume"))
	{
	}

	// MARK: - Descriptor classes

	bool isAudioClass(const QByteArray &cls)
	{
		return cls == "PCMA" || cls == "MPGA" || cls == "WAVE" ||
			   cls == "WAVD" || cls == "AIFD" || cls == "SD2D"; // OMF-era: the essence-file audio descriptors.
	}

	bool isMediaClass(const QByteArray &cls)
	{
		return isAudioClass(cls) || cls == "CDCI" || cls == "MPGI" || cls == "RGBA" || cls == "JPED";
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
				return s; // the essence — nothing useful lies past it
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
				s.sampleRate = int(std::llround(extendedToDouble(c + 8)));
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
		return path;
	}

	void walkAttributes(const BentoFile &b, const Props &p, quint32 attrObj, Attributes &a, QSet<quint32> &seen,
						int depth)
	{
		if (attrObj == 0 || depth > 4 || seen.contains(attrObj))
			return;
		seen.insert(attrObj);
		const QVector<quint32> attbs = BentoFile::handles(b.bytes(attrObj, p.attrRefs));
		for (quint32 attb : attbs)
		{
			const QString name = BentoFile::string(b.bytes(attb, p.attbName));
			const quint32 kind = BentoFile::uint(b.bytes(attb, p.attbKind));
			if (kind == 3)
			{
				const quint32 target = BentoFile::handle(b.bytes(attb, p.attbObj));
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

	quint32 findTimecodeComponent(const BentoFile &b, const Props &p, quint32 mob, const ObjectByMob &objectByMob,
								  QSet<quint32> &seen, int depth)
	{
		if (mob == 0 || depth > 6 || seen.contains(mob))
			return 0;
		seen.insert(mob);
		for (quint32 track : BentoFile::handles(b.bytes(mob, p.tracks)))
		{
			QVector<quint32> stack{BentoFile::handle(b.bytes(track, p.trackComp))};
			while (!stack.isEmpty())
			{
				const quint32 c = stack.takeLast();
				if (c == 0 || seen.contains(c))
					continue;
				seen.insert(c);
				const QByteArray cls = b.objectClass(c);
				if (cls == "TCCP")
					return c;
				if (cls == "SEQU")
					stack += BentoFile::handles(b.bytes(c, p.sequence));
				else if (cls == "SCLP")
				{
					const QByteArray src = b.bytes(c, p.sourceId);
					if (isSourceIdWidth(src.size()))
					{
						const quint32 srcMob = objectByMob.value(src, 0);
						if (const quint32 t = findTimecodeComponent(b, p, srcMob, objectByMob, seen, depth + 1))
							return t;
					}
				}
			}
		}
		return 0;
	}

	quint32 findSourceMob(const BentoFile &b, const Props &p, quint32 mob, const ObjectByMob &objectByMob)
	{
		if (mob == 0)
			return 0;
		QSet<quint32> seen;
		for (quint32 track : BentoFile::handles(b.bytes(mob, p.tracks)))
		{
			QVector<quint32> stack{BentoFile::handle(b.bytes(track, p.trackComp))};
			while (!stack.isEmpty())
			{
				const quint32 c = stack.takeLast();
				if (c == 0 || seen.contains(c))
					continue;
				seen.insert(c);
				const QByteArray cls = b.objectClass(c);
				if (cls == "SEQU")
					stack += BentoFile::handles(b.bytes(c, p.sequence));
				else if (cls == "SCLP")
				{
					const QByteArray src = b.bytes(c, p.sourceId);
					if (isSourceIdWidth(src.size()))
						if (const quint32 srcMob = objectByMob.value(src, 0); srcMob != 0 && srcMob != mob)
							return srcMob;
				}
			}
		}
		return 0;
	}

	Timecode readTimecode(const BentoFile &b, const Props &p, quint32 tccp)
	{
		Timecode t;
		if (tccp == 0)
			return t;
		t.found = true;
		t.dropFrame = BentoFile::uint(b.bytes(tccp, p.tcFlags)) != 0;
		// OMF-era: start and rate, surfaced by the essence-file reader.
		const QByteArray start = b.bytes(tccp, p.tcStart);
		if (!start.isEmpty())
			t.start = BentoFile::uint(start);
		t.fps = int(BentoFile::uint(b.bytes(tccp, p.tcFps)));
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

		// Codec label: the stored AUID, else rebuilt from the resolution id.
		const quint32 resId = BentoFile::uint(b.bytes(desc, p.resId));
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
		if (codecKnown)
			*codecKnown = !label.isEmpty() || tableHit;

		// Rate. Video goes through the same label matcher as tag 0x3001.
		qint32 num = 0, den = 0;
		if (BentoFile::rational(b.bytes(desc, p.sampleRate), num, den) && den > 0 && num > 0)
			MxfParser::applyEditRate(e, quint32(num), quint32(den));

		const quint32 length = BentoFile::uint(b.bytes(desc, p.length));
		if (e.isAudio)
		{
			// OMF-era: the codec column shows Avid's own label for legacy
			// audio — "WAVE (OMF)", "AIFF-C (OMF)", "SDII" (see
			// OmfResolutions::audioName). Pre-filled here because finalise
			// only names a codec that is still empty, which is also what
			// keeps MXF-era audio (PCMA / WAVE / MPGA descriptors) on "PCM".
			if (e.codec.isEmpty())
				e.codec = OmfResolutions::audioName(cls);

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
				// UNVERIFIED: property names from the MC binary; no SD2 specimen exists.
				blobBits = int(BentoFile::uint(b.bytes(desc, p.sd2dBits)));
				blobChannels = int(BentoFile::uint(b.bytes(desc, p.sd2dChannels)));
			}
			if (e.sampleRate <= 0 && blobRate > 0)
				e.sampleRate = blobRate; // OMF-era: the blob's rate when MDFL:SampleRate is absent.

			// Length is samples. The frame count comes from the mob's own edit
			// rate; finalise then re-derives the timecode base from these two
			// exactly as it does for a header (frames × rate ÷ samples).
			e.descriptorDuration = length;
			qint32 erNum = 0, erDen = 0;
			if (BentoFile::rational(b.bytes(mobObj, p.editRate), erNum, erDen) && erDen > 0 && erNum > 0 &&
				length > 0 && e.sampleRate > 0)
				e.durationFrames = qRound(double(length) * erNum / erDen / e.sampleRate);
			if (const quint32 bits = BentoFile::uint(b.bytes(desc, p.bits)))
				e.bitDepth = MxfParser::bitDepthLabel(bits);
			e.channels = int(BentoFile::uint(b.bytes(desc, p.channels)));
			// OMF-era: the MDAU properties above are absent on the legacy
			// descriptors, so the blob supplies what they left empty.
			if (e.bitDepth.isEmpty() && blobBits > 0)
				e.bitDepth = MxfParser::bitDepthLabel(quint32(blobBits));
			if (e.channels <= 0 && blobChannels > 0)
				e.channels = blobChannels;
		}
		else
		{
			e.durationFrames = length;
			e.width = int(BentoFile::uint(b.bytes(desc, p.width)));
			int height = int(BentoFile::uint(b.bytes(desc, p.height)));
			const QByteArray layoutV = b.bytes(desc, p.layout);
			const int layout = layoutV.isEmpty() ? -1 : int(BentoFile::uint(layoutV));
			// The MDB stores one FIELD height for layouts 1 and 3 (the MXF only
			// for 1). Normalise here and say so; finalise keeps its own
			// layout-1 rule for headers and the 1088/544 padding rule for both.
			if (height > 0 && (layout == 1 || layout == 3))
				height *= 2;
			e.height = height;
			e.frameLayout = layout;
			e.heightIsFrameHeight = height > 0;
			if (const quint32 bits = BentoFile::uint(b.bytes(desc, p.compWidth)))
				e.bitDepth = MxfParser::bitDepthLabel(bits);
		}

		// Drop frame: the timecode component is on a source mob, reached
		// through the file mob's SCLP references.
		QSet<quint32> seen;
		if (const quint32 tccp = findTimecodeComponent(b, p, mobObj, objectByMob, seen, 0))
			e.dropFrame = BentoFile::uint(b.bytes(tccp, p.tcFlags)) != 0;

		MxfParser::finalise(e);
		return true;
	}
} // namespace OmfObjects
