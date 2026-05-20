#include "mdbparser.h"
#include "mobid.h"
#include <QFile>
#include <QSet>
#include <QStringList>
#include <QDebug>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <algorithm>
#include <cstring>

// Off by default — flood prone on busy projects.
// Flip on when debugging the MDB format:
//   QT_LOGGING_RULES="mediamuster.mdb.debug=true" ./MediaMuster
Q_LOGGING_CATEGORY(lcMdb, "mediamuster.mdb", QtWarningMsg)

// `msmMMOB.mdb` is an AAF property-based structure with named
// properties (`_ORG_BIN`, `_IMPORTSETTING`, `_COLUMN_START`,
// `_SRCFILE`, `UNC Path`, ...). The layout may shift between Avid
// versions, so we can't rely on fixed offsets. Instead:
//
//   1. Find every 32-byte MOB pattern by byte signature.
//   2. Extract every printable-ASCII string in the file.
//   3. For each unique MOB, build a 'cluster' of strings that sit
//      within 1 KB of any occurrence of that MOB. Inside the
//      cluster we look for marker → value pairs and signature
//      strings (paths, timecodes, container hints).
//
// Bin name is the one exception to the cluster rule: `_ORG_BIN`
// markers can sit further than 1 KB from their clip's first MOB
// reference, so we do a file-wide `_ORG_BIN` scan and pick the
// marker closest by byte offset to each MOB's first occurrence.
//
// MARK: - Field sources
//
//   clipName        — value after an AAF 'Name' column label.
//                     Fallback: source basename with media extension
//                     stripped.
//   bin             — bin name from the `_ORG_BIN` marker closest by
//                     byte offset to this MOB's first occurrence.
//   startTimecode   — value after `_COLUMN_START`, or any bare
//                     HH:MM:SS:FF match in the cluster.
//   sourceFilePath  — first path-shaped string in the cluster
//   sourceFileName  — basename of sourceFilePath.
//   sourceContainer — first QTFF/MXF/MOV/... token in the cluster.
//   isImported      — true if any `_IMPORTSETTING` marker is in the
//                     cluster.

namespace
{
// MARK: - Constants

constexpr qint64 kClusterWindow = 1024;

/// Bin name sits 7–20 bytes before the `_ORG_BIN` marker;
/// 64 caps off the AAF noise that surrounds it.
constexpr qint64 kBinAdjacency = 64;

const QStringList kMediaExtensions = {
    ".mxf", ".wav", ".aif", ".aiff", ".bwf", ".omf", ".aaf",
    ".mov", ".mp4", ".avi", ".mkv", ".jpeg", ".jpg", ".png", ".tif",
    ".tiff", ".dpx", ".exr", ".bmp", ".gif", ".heic", ".heif",
    ".r3d", ".braw", ".arri", ".cin"};

const QString kMarkerImportSetting = QStringLiteral("_IMPORTSETTING");
const QString kMarkerOrgBin = QStringLiteral("_ORG_BIN");
const QString kMarkerNameCol = QStringLiteral("Name");
const QString kMarkerStartCol = QStringLiteral("_COLUMN_START");

const QSet<QString> kContainerHints = {
    QStringLiteral("QTFF"), QStringLiteral("MXF"), QStringLiteral("MOV"),
    QStringLiteral("AVI"), QStringLiteral("MP4"), QStringLiteral("WAV"),
    QStringLiteral("AIFF")};

const QRegularExpression kTimecodeRe(
    QStringLiteral("^\\d{2}:\\d{2}:\\d{2}[:;]\\d{2}$"));

// MARK: - String-shape helpers

/// Strip the leading `z` (0x7A) that AAF sometimes prepends to
/// string properties as a type tag. We only strip when the next
/// char looks like the real start of a value (uppercase, digit,
/// or `_`) so genuine z-words aren't truncated.
QString stripZPrefix(const QString &s)
{
	if (s.length() > 3 && s[0] == QLatin1Char('z'))
	{
		const QChar next = s[1];
		if (next.isUpper() || next.isDigit() || next == QLatin1Char('_'))
			return s.mid(1);
	}
	return s;
}

/// True if `s` looks like a filepath recorded for an imported source.
bool looksLikeSourcePath(const QString &s)
{
	if (s.startsWith(QStringLiteral("/Users/")) ||
	    s.startsWith(QStringLiteral("/Volumes/")))
		return true;
	if (s.startsWith(QStringLiteral("\\\\")))
		return true;
	if (s.length() >= 3 && s[1] == QLatin1Char(':') &&
	    (s[2] == QLatin1Char('\\') || s[2] == QLatin1Char('/')))
		return true;
	return false;
}

/// True if `s` plausibly is a human-typed label rather than
/// an internal Avid identifier or property key.
/// Excludes paths, names starting with markers (`_`, `&`,
/// `omfi:`, `ATN_`, `0x`), and strings too short/long to be real.
bool looksLikeHumanLabel(const QString &s)
{
	if (s.contains(QLatin1Char('/')) || s.contains(QLatin1Char('\\')))
		return false;
	if (s.startsWith(QLatin1Char('_')) || s.startsWith(QLatin1Char('&')))
		return false;
	if (s.startsWith(QStringLiteral("omfi:")) ||
	    s.startsWith(QStringLiteral("OMFI:")))
		return false;
	if (s.startsWith(QStringLiteral("ATN_")) ||
	    s.startsWith(QStringLiteral("0x")))
		return false;
	if (s.length() < 2 || s.length() > 128)
		return false;
	// Must contain at least one letter — rules out digits
	// and punctuation strings that pass the length check.
	for (QChar c : s)
	{
		if (c.isLetter())
			return true;
	}
	return false;
}

/// Remove a trailing file extension if present.
QString stripMediaExtension(const QString &name)
{
	const QString lower = name.toLower();
	for (const QString &ext : kMediaExtensions)
	{
		if (lower.endsWith(ext))
			return name.left(name.length() - ext.length());
	}
	return name;
}

/// Basename component of a path, handling both `/` and `\`.
QString pathBasename(const QString &path)
{
	const int slash = qMax(path.lastIndexOf(QLatin1Char('/')),
	                       path.lastIndexOf(QLatin1Char('\\')));
	return (slash >= 0) ? path.mid(slash + 1) : path;
}

// MARK: - String extraction

/// A printable ASCII string in the file plus the byte offset it
/// started at. Stored offset-sorted so we can binary search a
/// window in `buildCluster`.
struct ExtractedString
{
	qint64 offset;
	QString value;
};

/// Walk the buffer collecting every run of printable ASCII (0x20–
/// 0x7E) at least `minLen` chars long. Linear and allocation-frugal
/// as the cluster builder leans on the sorted-by-offset property.
[[nodiscard]] QVector<ExtractedString> extractAllStrings(const QByteArray &data, int minLen = 3)
{
	const auto isPrintable = [](uchar c)
	{ return c >= 0x20 && c < 0x7F; };

	QVector<ExtractedString> out;
	out.reserve(2048);
	const qint64 n = data.size();
	const char *ptr = data.constData();
	qint64 i = 0;
	while (i < n)
	{
		if (!isPrintable(static_cast<uchar>(ptr[i])))
		{
			++i;
			continue;
		}
		const qint64 start = i;
		while (i < n && isPrintable(static_cast<uchar>(ptr[i])))
			++i;
		if (const int len = int(i - start); len >= minLen)
			out.append({start, QString::fromLatin1(ptr + start, len)});
	}
	return out;
}

// MARK: - _ORG_BIN markers

/// Byte offset of an `_ORG_BIN` marker plus the bin-name string
/// that sits next to it. Used to map a MOB back to the bin it
/// was originally imported into.
struct OrgBinMarker
{
	qint64 offset;
	QString binName;
};

/// Find every `_ORG_BIN` marker and pair it with the nearest
/// 'human' string within `kBinAdjacency` bytes. Gate by byte
/// distance, not list index.
[[nodiscard]] QVector<OrgBinMarker> findOrgBinMarkers(const QVector<ExtractedString> &strings)
{
	QVector<OrgBinMarker> out;
	for (int i = 0; i < strings.size(); ++i)
	{
		if (strings[i].value != kMarkerOrgBin)
			continue;
		const qint64 markerOff = strings[i].offset;
		QString binName;
		for (int j = i - 1; j >= 0 && j >= i - 20; --j)
		{
			const qint64 prevEnd = strings[j].offset + strings[j].value.length();
			if (markerOff - prevEnd > kBinAdjacency)
				break;
			const QString cand = stripZPrefix(strings[j].value);
			if (!looksLikeHumanLabel(cand))
				continue;
			// Duplicates are fine — key and value carry the same
			// text in the AAF representation.
			binName = cand;
		}
		if (!binName.isEmpty())
			out.append({markerOff, binName});
	}
	return out;
}

// MARK: - MOB offsets

/// Find every byte position where a 32-byte MOB pattern starts.
/// Avid MDB content uses the `06 0a 2b 34` prefix exclusively
/// (never the SMPTE `06 0e 2b 34` form) so we only scan for the
/// `0a` variant.
[[nodiscard]] QVector<qint64> findMobOffsets(const QByteArray &data)
{
	QVector<qint64> out;
	const qint64 n = data.size();
	if (n < 32)
		return out;

	const auto *ptr = reinterpret_cast<const uchar *>(data.constData());
	// `memchr` uses SIMD to skip to the next 0x06 byte.
	// Cap at n-32 so a 32-byte MOB still fits.
	const uchar *const end = ptr + (n - 32);
	const uchar *p = ptr;
	while (p <= end)
	{
		p = static_cast<const uchar *>(std::memchr(p, 0x06, end - p + 1));
		if (!p)
			break;
		if (p[1] == 0x0a && p[2] == 0x2b && p[3] == 0x34)
		{
			out.append(p - ptr);
			p += 32;
		}
		else
		{
			++p;
		}
	}
	return out;
}

// MARK: - Cluster building

/// Build the per-MOB cluster: every printable string within
/// `kClusterWindow` bytes of any MOB occurrence. Strings are
/// keyed by offset, so duplicates sort adjacent and `std::unique`
/// collapses them in one pass.
[[nodiscard]] QVector<ExtractedString> buildCluster(
    const QVector<qint64> &mobOffsets,
    const QVector<ExtractedString> &strings)
{
	QVector<ExtractedString> cluster;
	cluster.reserve(64);
	for (qint64 off : mobOffsets)
	{
		const qint64 lo = off - kClusterWindow;
		const qint64 hi = off + kClusterWindow;
		// Binary search to jump straight to the window start —
		// strings is offset-sorted by construction.
		const auto start = std::lower_bound(strings.cbegin(), strings.cend(), lo,
		                                    [](const ExtractedString &es, qint64 val)
		                                    { return es.offset < val; });
		for (auto it = start; it != strings.cend() && it->offset <= hi; ++it)
			cluster.append(*it);
	}
	std::sort(cluster.begin(), cluster.end(),
	          [](const ExtractedString &a, const ExtractedString &b)
	          { return a.offset < b.offset; });
	cluster.erase(std::unique(cluster.begin(), cluster.end(),
	                          [](const ExtractedString &a, const ExtractedString &b)
	                          { return a.offset == b.offset; }),
	              cluster.end());
	return cluster;
}

// MARK: - Per-MOB extraction

/// Build one MdbRecord from one MOB's cluster. Walks the cluster
/// looking for marker-then-value pairs (Name → clip name,
/// _COLUMN_START → start timecode), then runs a few standalone
/// heuristics (path-shape, timecode regex, container token,
/// import marker presence).
[[nodiscard]] MdbRecord extractMobRecord(
    const QByteArray &rawMobId,
    const QString &mobHex,
    const QVector<qint64> &mobOccurrences,
    const QVector<ExtractedString> &allStrings,
    const QVector<OrgBinMarker> &orgBinMarkers)
{
	MdbRecord rec;
	rec.mobId = rawMobId;
	rec.mobIdHex = mobHex;

	const QVector<ExtractedString> cluster = buildCluster(mobOccurrences, allStrings);

	// First pass: harvest marker → value pairs (adjacent strings
	// in AAF; a `Name` marker is followed by the clip name).
	for (int i = 0; i < cluster.size(); ++i)
	{
		const QString &raw = cluster[i].value;

		if (raw == kMarkerImportSetting)
		{
			rec.isImported = true;
		}
		else if (raw == kMarkerNameCol && rec.clipName.isEmpty() && i + 1 < cluster.size())
		{
			const QString nxt = stripZPrefix(cluster[i + 1].value);
			if (nxt != kMarkerNameCol && !nxt.startsWith(QLatin1Char('_')) && looksLikeHumanLabel(nxt))
				rec.clipName = nxt;
		}
		else if (raw == kMarkerStartCol && rec.startTimecode.isEmpty() && i + 1 < cluster.size())
		{
			const QString &nxt = cluster[i + 1].value;
			if (kTimecodeRe.match(nxt).hasMatch())
				rec.startTimecode = nxt;
		}
	}

	// Start-timecode fallback: any bare HH:MM:SS:FF anywhere in
	// the cluster. Some Avid versions don't write the
	// `_COLUMN_START` marker but do leave the timecode string.
	if (rec.startTimecode.isEmpty())
	{
		for (const ExtractedString &es : cluster)
		{
			if (kTimecodeRe.match(es.value).hasMatch())
			{
				rec.startTimecode = es.value;
				break;
			}
		}
	}

	// Source file path: first path-shaped string in offset order.
	for (const ExtractedString &es : cluster)
	{
		const QString s = stripZPrefix(es.value);
		if (looksLikeSourcePath(s))
		{
			rec.sourceFilePath = s;
			rec.sourceFileName = pathBasename(s);
			break;
		}
	}

	// Clip-name fallback: strip the media extension off the
	// source filename.
	if (rec.clipName.isEmpty() && !rec.sourceFileName.isEmpty())
		rec.clipName = stripMediaExtension(rec.sourceFileName);

	// Bin name: nearest `_ORG_BIN` marker (by file offset) to this
	// MOB's earliest occurrence. Markers regularly sit >1 KB from
	// their MOB reference, so cluster membership won't catch them.
	if (!orgBinMarkers.isEmpty() && !mobOccurrences.isEmpty())
	{
		const qint64 firstOcc = *std::min_element(mobOccurrences.cbegin(), mobOccurrences.cend());
		const OrgBinMarker *best = nullptr;
		qint64 bestDist = 0;
		for (const OrgBinMarker &m : orgBinMarkers)
		{
			const qint64 d = qAbs(m.offset - firstOcc);
			if (!best || d < bestDist)
			{
				best = &m;
				bestDist = d;
			}
		}
		if (best)
			rec.bin = best->binName;
	}

	// Source container: first recognised token (`QTFF`, `MXF`,
	// `MOV`, ...) in the cluster.
	for (const ExtractedString &es : cluster)
	{
		if (kContainerHints.contains(es.value))
		{
			rec.sourceContainer = es.value;
			break;
		}
	}

	return rec;
}

} // namespace

// MARK: - Public API

QVector<MdbRecord> MdbParser::parse(const QString &mdbFilePath)
{
	QVector<MdbRecord> records;

	QFile file(mdbFilePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning() << "MDB: Cannot open" << mdbFilePath;
		return records;
	}
	const QByteArray data = file.readAll();
	file.close();

	// Sanity check — anything under 64 bytes not likely a real MDB.
	if (data.size() < 64)
		return records;

	const QVector<qint64> mobOffsets = findMobOffsets(data);
	const QVector<ExtractedString> strings = extractAllStrings(data, 3);
	const QVector<OrgBinMarker> orgBins = findOrgBinMarkers(strings);

	qCDebug(lcMdb) << "MDB:" << mdbFilePath
	               << "size:" << data.size()
	               << "mobs:" << mobOffsets.size()
	               << "strings:" << strings.size()
	               << "_ORG_BIN markers:" << orgBins.size();
	for (const OrgBinMarker &m : orgBins)
		qCDebug(lcMdb) << "  _ORG_BIN @" << m.offset << ":" << m.binName;

	// Group MOB occurrences by hex form — the same MOB typically
	// appears in multiple places in the file (source / master /
	// referenced clips) and we want one record per unique MOB.
	QHash<QString, QVector<qint64>> mobOccurrences;
	QHash<QString, QByteArray> mobRawBytes;
	for (qint64 off : mobOffsets)
	{
		const QByteArray mob = data.mid(qsizetype(off), 32);
		const QString hex = MobId::format(mob);
		mobOccurrences[hex].append(off);
		if (!mobRawBytes.contains(hex))
			mobRawBytes.insert(hex, mob);
	}

	records.reserve(mobOccurrences.size());
	for (auto it = mobOccurrences.cbegin(); it != mobOccurrences.cend(); ++it)
	{
		records.append(extractMobRecord(
		    mobRawBytes.value(it.key()), it.key(), it.value(), strings, orgBins));
	}

	qCDebug(lcMdb) << "MDB: built" << records.size() << "per-MOB records";
	return records;
}

MdbParser::RecordMap MdbParser::buildMobMap(const QString &mdbFilePath)
{
	RecordMap map;
	const auto records = parse(mdbFilePath);
	map.reserve(records.size());
	for (const auto &r : records)
	{
		if (!r.mobIdHex.isEmpty())
			map.insert(r.mobIdHex, r);
	}
	return map;
}