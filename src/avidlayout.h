#pragma once

#include <QLatin1String>
#include <QString>
#include <QStringView>

// MARK: - AvidLayout
/// Facts about Avid's on-disk media layout: the folder names Avid uses
/// and how essence files are recognised by name. One home so the
/// scanner, rebalancer, copy-path builder, and volume detection can't
/// drift apart on what counts as Avid media. They already had: the
/// rebalance picker matched 'MXF' case-sensitively while the scanner
/// didn't (a lowercase 'mxf' share scanned fine, then Rebalance said
/// nothing was found), and only the rebalancer knew that dot-hidden
/// "._*.mxf" AppleDouble siblings aren't media.

namespace AvidLayout
{
	// MARK: - Folder names

	inline constexpr QLatin1String kAvidMediaFilesDir("Avid MediaFiles");
	inline constexpr QLatin1String kMxfDir("MXF");

	/// Case-insensitive: the case-insensitive file systems macOS and
	/// Windows default to accept any spelling, and Linux-hosted shares
	/// or hand-restored backups may carry one. Avid itself writes 'MXF'.
	inline bool isMxfRootName(QStringView name)
	{
		return name.compare(kMxfDir, Qt::CaseInsensitive) == 0;
	}

	/// An OMF root is deliberately NOT an MXF root: the scanner accepts
	/// OMF folders for scanning, but rebalancing an OMF root into
	/// MXF-numbered folders would be wrong. Keep the two distinct.
	/// The folder is "OMFI MediaFiles", and it is a TOP-LEVEL folder beside
	/// "Avid MediaFiles" — NOT a subfolder of it. Verified against Media
	/// Composer 25.12: it references "/Shared/AvidMediaComposer/OMFI
	/// MediaFiles", while no string of the form "Avid MediaFiles/OMF..."
	/// exists anywhere in its binaries, and the only subfolders it knows
	/// under "Avid MediaFiles" are Proxy and UME. (A bare "OMF" was accepted
	/// here until 2026-08-14; it came from prototyping, matched nothing Avid
	/// ever writes, and is gone.)
	///
	/// TODO — OMF support is deliberately PARTIAL (decided 2026-08-14: no
	/// OMF media in the field, so not worth building yet). What works: this
	/// name check, so a user who points MediaMuster straight at the folder
	/// gets it scanned; .omf passes hasAvidMediaExtension, so files list;
	/// and every copy/move/delete works, since those never read the file.
	/// What is missing, smallest first:
	///   1. Discovery — scanVolume Cases 1, 2 and 5 all look for
	///      kAvidMediaFilesDir only, so a volume scan never FINDS an
	///      "OMFI MediaFiles" folder at a volume root. Small fix, and
	///      without it the feature is invisible unless the user navigates
	///      to the folder themselves.
	///   2. Databases — OMF-era msmFMID.pmr is version 2 and PmrParser
	///      requires 8, so every OMF folder reports "No database" and gets
	///      no project/bin attribution. Layout is already known: 12-byte
	///      header, then per record 8-byte ID, u16 nameLen + name,
	///      u16 projLen + project, 12-byte trailer. A specimen ships at
	///      Avid's SupportingFiles/Avid_MediaFiles/msmFMID.pmr (80 records).
	///   3. Essence — no .omf parser exists. OMF is a Bento container (the
	///      same family avbparser.cpp already opens), not an MXF variant,
	///      so rows would show name/size/date and no metadata at all.
	/// Doing 1 and 2 alone would list OMF media WITH project and bin names
	/// and never touch the media files — most of the value, least of the
	/// work. Item 3 is only worth it for genuine archive customers.
	inline bool isOmfRootName(QStringView name)
	{
		return name.compare(QLatin1String("OMFI MediaFiles"), Qt::CaseInsensitive) == 0;
	}

	/// "<base>/Avid MediaFiles/MXF" — the canonical media root under a
	/// volume or search directory.
	inline QString mxfRootUnder(const QString &base)
	{
		return base + QLatin1Char('/') + kAvidMediaFilesDir + QLatin1Char('/') + kMxfDir;
	}

	// MARK: - Essence-file names

	/// Leading-dot names are never Avid media: macOS metadata and the
	/// AppleDouble "._clip.mxf" resource-fork siblings macOS writes onto
	/// SMB shares. Matched by NAME, not the OS hidden attribute — Unix
	/// enumeration hides dotfiles anyway, but Windows reading a
	/// Mac-written share enumerates them, and both platforms must agree.
	inline bool isDotHidden(QStringView fileName)
	{
		return fileName.startsWith(QLatin1Char('.'));
	}

	inline bool hasMxfExtension(QStringView fileName)
	{
		return fileName.endsWith(QLatin1String(".mxf"), Qt::CaseInsensitive);
	}

	/// Extensions that are Avid media (user ruling 2026-08-12): MXF-era
	/// essence, plus the OMF era — .omf video, with its audio living
	/// beside it as .aif/.wav files.
	inline bool hasAvidMediaExtension(QStringView fileName)
	{
		return hasMxfExtension(fileName) ||
			   fileName.endsWith(QLatin1String(".omf"), Qt::CaseInsensitive) ||
			   fileName.endsWith(QLatin1String(".aif"), Qt::CaseInsensitive) ||
			   fileName.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive);
	}

	// MARK: - MediaMuster's own temp-rename markers

	/// Copy and Move park an existing destination aside, and rebalance
	/// renames a pre-flight probe, by appending one of these plus a
	/// uuid. Defined here, used by mediamanager.cpp (the parking) and
	/// probesweep.h (the sweep), so writer and reader share one spelling.
	inline constexpr QLatin1String kCopyReplaceTag(".__copyreplace_");
	inline constexpr QLatin1String kMoveReplaceTag(".__movereplace_");
	inline constexpr QLatin1String kProbeMarker(".__rebalprobe_");

	/// Where Delete puts files on a volume whose OS trash can't be used or
	/// won't say where a file landed (network shares): a folder at the
	/// volume root. Defined here with the other names MediaMuster writes,
	/// so the delete path (mediamanager.cpp) and anything that has to
	/// recognise the folder afterwards share one spelling.
	inline constexpr QLatin1String kMediaMusterTrashDir("_MediaMuster_Trash");

	/// The name with a trailing MediaMuster temp suffix removed, so
	/// "Clip.mxf.__movereplace_ab12" reads as "Clip.mxf".
	inline QStringView withoutTempSuffix(QStringView fileName)
	{
		for (const QLatin1String tag : {kCopyReplaceTag, kMoveReplaceTag, kProbeMarker})
		{
			const qsizetype at = fileName.lastIndexOf(tag);
			if (at > 0)
				return fileName.left(at);
		}
		return fileName;
	}

	/// What the media table shows: Avid media by extension, minus
	/// dot-hidden AppleDouble twins ("._clip.mxf"). No junk denylist
	/// needed — Thumbs.db, desktop.ini, the msm databases, and stray
	/// exports all fail the extension test.
	///
	/// A file wearing one of OUR temp suffixes still counts: it is the
	/// user's own media, renamed aside by an operation that then died.
	/// Hiding it would be the one case where this rule loses a file —
	/// a crash whose journal never got to report the park (an unmounted
	/// volume at recovery time, say) leaves the table as the only place
	/// the file is findable.
	inline bool isAvidMediaName(QStringView fileName)
	{
		return !isDotHidden(fileName) && hasAvidMediaExtension(withoutTempSuffix(fileName));
	}

	/// True when a directory entry occupies Avid's per-folder file budget
	/// (see AvidLimits): an .mxf that isn't dot-hidden. Used by the
	/// rebalancer's packing and the Quarantined tally — NOT by the media
	/// table, which admits more (isAvidMediaName: non-MXF audio too, and
	/// it looks through a temp-rename suffix). The two differ on purpose;
	/// tst_avidlayout pins both.
	inline bool countsAsEssenceName(QStringView fileName)
	{
		return !isDotHidden(fileName) && hasMxfExtension(fileName);
	}
} // namespace AvidLayout