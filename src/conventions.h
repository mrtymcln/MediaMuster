#pragma once

#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QtGlobal>
#include <array>

// MARK: - Conventions
/// The one home for the agreed spellings and numbers that more than one
/// feature has to read the same way: what Avid calls its folders, what
/// counts as media, how full a folder may get, and the names MediaMuster
/// writes onto a user's drive. One home because they have already drifted
/// once — the rebalance picker matched 'MXF' case-sensitively while the
/// scanner didn't (a lowercase 'mxf' share scanned fine, then Rebalance
/// said nothing was found), and only the rebalancer knew that dot-hidden
/// "._*.mxf" AppleDouble siblings aren't media.
///
/// TWO KINDS LIVE HERE, and the difference matters more than it looks:
///
///   [AVID — DO NOT CHANGE]  Facts about someone else's software. Editing
///                           one doesn't change a policy, it tells a lie:
///                           the value stops describing what Media
///                           Composer actually does. Change these only to
///                           correct a mistake, with evidence.
///
///   [OURS — SAFE TO CHANGE] Choices MediaMuster made. Changing one is a
///                           real decision with real consequences (older
///                           files on disk keep the old spelling), but it
///                           breaks nothing outside this app.
///
/// Membership test, so this stays a reference and not a junk drawer:
/// something belongs here only if it is (a) a DECISION rather than a
/// computation, and (b) read by two or more unrelated places. Logic that
/// derives an answer belongs with its algorithm; a value only one file
/// reads belongs in that file.

namespace Conventions
{
	// ═══════════════════════════════════════════════════════════════
	// MARK: - Avid's folder names
	// ═══════════════════════════════════════════════════════════════

	/// [AVID — DO NOT CHANGE] The folder Media Composer creates at a
	/// volume root, and the essence folder inside it.
	inline constexpr QLatin1String kAvidMediaFilesDir("Avid MediaFiles");
	inline constexpr QLatin1String kMxfDir("MXF");

	/// Case-insensitive: the case-insensitive file systems macOS and
	/// Windows default to accept any spelling, and Linux-hosted shares
	/// or hand-restored backups may carry one. Avid itself writes 'MXF'.
	inline bool isMxfRootName(QStringView name)
	{
		return name.compare(kMxfDir, Qt::CaseInsensitive) == 0;
	}

	// MARK: - OMF-era folder

	/// OMF-era: [AVID — DO NOT CHANGE] The legacy (pre-MXF) media folder.
	/// It is a TOP-LEVEL folder beside "Avid MediaFiles" — NOT a subfolder
	/// of it — and it is FLAT: media sits directly inside it with ONE
	/// msmFMID.pmr / msmMMOB.mdb pair at the same level, no numbered
	/// subfolders. Verified against Media Composer 25.12 and 26.8: the
	/// binary references "/Shared/AvidMediaComposer/OMFI MediaFiles", no
	/// string of the form "Avid MediaFiles/OMF..." exists in it, and Avid
	/// KB en273303 names the two folders as siblings at a volume root. (A
	/// bare "OMF" was accepted here until 2026-08-14; it came from
	/// prototyping, matched nothing Avid ever writes, and is gone.)
	///
	/// What makes the era different, so nobody re-derives it from MXF
	/// assumptions (facts corrected 2026-09-02 against real MC 26.8 output):
	///   - msmFMID.pmr is VERSION 2 — the version-8 grammar with 8-byte
	///     MOBs, the same optional Unicode set (which carries the MOB in
	///     Avid's wrapped 32-byte form — see OmfUid), a 4-byte MASTER
	///     trailer, and a file trailer that is the file's mtime in Unix
	///     seconds. Avid ships an 80-PAIR specimen in
	///     SupportingFiles/Avid_MediaFiles/msmFMID.pmr.
	///   - msmMMOB.mdb keys its mobs by a 12-byte omfi:UID, not a 32-byte
	///     UMID (MC 2026 also writes a UMID on the physical mob).
	///   - The essence (.omf video; .aif/.wav/.sd2 audio) is an object store
	///     in an Apple Bento container — essence first, TOC at the tail —
	///     the same container msmMMOB.mdb uses, and nothing like a bin.
	/// The readers for all three landed 2026-09-02 (PmrParser's version-2
	/// path, MdbParser through OmfObjects, and OmfParser for the essence),
	/// so an OMF folder now scans exactly like an MXF one — databases
	/// first, the Bento tail only for rows they leave undescribed.
	///
	/// An OMF root is deliberately NOT an MXF root: the scanner scans one,
	/// but rebalancing an OMF root into MXF-numbered folders would be
	/// wrong, and Rebalancer::parseFolderName rejects it. Keep the two
	/// distinct; tst_conventions pins that they never collide.
	inline constexpr QLatin1String kOmfMediaFilesDir("OMFI MediaFiles");

	/// OMF-era: case-insensitive like isMxfRootName, for the same reasons.
	inline bool isOmfRootName(QStringView name)
	{
		return name.compare(kOmfMediaFilesDir, Qt::CaseInsensitive) == 0;
	}

	/// "<base>/Avid MediaFiles/MXF" — the canonical media root under a
	/// volume or search directory.
	inline QString mxfRootUnder(const QString &base)
	{
		return base + QLatin1Char('/') + kAvidMediaFilesDir + QLatin1Char('/') + kMxfDir;
	}

	/// OMF-era: "<base>/OMFI MediaFiles" — one level, not two, because the
	/// OMF folder is the media root itself; there is no "MXF" inside it.
	inline QString omfRootUnder(const QString &base)
	{
		return base + QLatin1Char('/') + kOmfMediaFilesDir;
	}

	// MARK: - Avid's transient capture folder

	/// [AVID — DO NOT CHANGE] The staging subfolder Media Composer makes
	/// inside a media root while it is writing new media, in BOTH eras
	/// (under "Avid MediaFiles/MXF" and under "OMFI MediaFiles"). Its
	/// contents are half-written files that will be renamed into a real
	/// folder when capture finishes, so nothing here counts as media, fits
	/// a folder budget, or should be copied.
	inline constexpr QLatin1String kCreatingDir("Creating");

	/// Case-insensitive, for the same reasons as isMxfRootName.
	inline bool isCreatingFolderName(QStringView name)
	{
		return name.compare(kCreatingDir, Qt::CaseInsensitive) == 0;
	}

	// MARK: - Where Avid puts media on the system drive

	/// [AVID — DO NOT CHANGE] Avid's placement rule (user ruling
	/// 2026-09-02, matching what the MC binary hard-codes): media lives at
	/// the ROOT of an external drive, or in one fixed place on the system
	/// drive. These are the fixed places — the bases under which BOTH
	/// kAvidMediaFilesDir and kOmfMediaFilesDir are probed. A volume scan
	/// looks exactly here and at drive roots and nowhere deeper; the
	/// two-level search survives only for folders a user adds by hand.
	///
	/// Windows keeps the legacy root "C:/" as a base because older Media
	/// Composers wrote "C:\Avid MediaFiles" directly, and the scanner's
	/// boot-volume skip would otherwise never look there.
	inline QStringList systemDriveMediaBases()
	{
		QStringList bases;
#if defined(Q_OS_MAC)
		bases << QStringLiteral("/Users/Shared/AvidMediaComposer");
#elif defined(Q_OS_WIN)
		bases << QStringLiteral("C:/Users/Public/Documents/Avid Media Composer")
			  << QStringLiteral("C:/");
#endif
		return bases;
	}

	// ═══════════════════════════════════════════════════════════════
	// MARK: - Avid's database file names
	// ═══════════════════════════════════════════════════════════════

	/// [AVID — DO NOT CHANGE] The per-folder index and clip database, in
	/// both spellings Media Composer writes: msm* for media it manages,
	/// ama* for AMA-linked folders. A folder may hold either or both; the
	/// scanner reads every one present and merges. Same names in both eras
	/// (the OMF root holds its single pair at the top level).
	inline constexpr std::array<QLatin1String, 2> kPmrFileNames = {
		QLatin1String("msmFMID.pmr"), QLatin1String("amaFMID.pmr")};
	inline constexpr std::array<QLatin1String, 2> kMdbFileNames = {
		QLatin1String("msmMMOB.mdb"), QLatin1String("amaMMOB.mdb")};

	// ═══════════════════════════════════════════════════════════════
	// MARK: - Avid's essence-file names
	// ═══════════════════════════════════════════════════════════════

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

	/// OMF-era: the legacy essence extensions — .omf video, with audio
	/// living beside it as .aif/.wav, and .sd2 (Sound Designer II, the
	/// third audio container Media Composer's binary still names; no
	/// specimen exists). Kept as its own predicate because the scanner
	/// dispatches these to the OMF-era reader, never to MxfParser.
	inline bool hasOmfEraExtension(QStringView fileName)
	{
		return fileName.endsWith(QLatin1String(".omf"), Qt::CaseInsensitive) ||
			   fileName.endsWith(QLatin1String(".aif"), Qt::CaseInsensitive) ||
			   fileName.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive) ||
			   fileName.endsWith(QLatin1String(".sd2"), Qt::CaseInsensitive);
	}

	/// Extensions that are Avid media (user ruling 2026-08-12): MXF-era
	/// essence, plus the OMF era.
	inline bool hasAvidMediaExtension(QStringView fileName)
	{
		// OMF-era: the legacy set is admitted here; this is the one gate
		// that lets an OMF row into the table at all.
		return hasMxfExtension(fileName) || hasOmfEraExtension(fileName);
	}

	// ═══════════════════════════════════════════════════════════════
	// MARK: - Avid's per-folder file budget
	// ═══════════════════════════════════════════════════════════════

	/// [AVID — DO NOT CHANGE] Media Composer's own ceiling. A folder
	/// should never reach this; past it, MC slows down badly.
	inline constexpr int kFolderMax = 5000;

	/// [OURS — SAFE TO CHANGE] What the Rebalancer packs folders up to.
	/// One below Avid's ceiling, sourced from it so the two can't drift.
	inline constexpr int kFolderTarget = kFolderMax - 1; // 4999

	/// [OURS — SAFE TO CHANGE] Where the folder-card bar turns red.
	inline constexpr int kFolderCritical = 4800;

	/// [OURS — SAFE TO CHANGE] Where it turns amber.
	inline constexpr int kFolderWarn = 4500;

	// ═══════════════════════════════════════════════════════════════
	// MARK: - Names MediaMuster writes to disk
	// ═══════════════════════════════════════════════════════════════

	/// [OURS — SAFE TO CHANGE] Copy and Move park an existing destination
	/// aside by appending one of these plus a uuid. Used by the ops engine
	/// (the parking, in oprunner.cpp) and by the scanner, which must
	/// recognise a stranded park as temp-renamed media rather than hiding
	/// it. Changing these leaves any park already on disk unrecognised.
	inline constexpr QLatin1String kCopyReplaceTag(".__copyreplace_");
	inline constexpr QLatin1String kMoveReplaceTag(".__movereplace_");

	/// [OURS — SAFE TO CHANGE] Where Delete puts files on a volume whose
	/// OS trash can't be used or won't say where a file landed (network
	/// shares): a folder at the volume root. The delete path (TrashRouter)
	/// and anything that has to recognise the folder afterwards share this
	/// one spelling.
	inline constexpr QLatin1String kMediaMusterTrashDir("_MediaMuster_Trash");

	/// The name with a trailing MediaMuster temp suffix removed, so
	/// "Clip.mxf.__movereplace_ab12" reads as "Clip.mxf".
	inline QStringView withoutTempSuffix(QStringView fileName)
	{
		for (const QLatin1String tag : {kCopyReplaceTag, kMoveReplaceTag})
		{
			const qsizetype at = fileName.lastIndexOf(tag);
			if (at > 0)
				return fileName.left(at);
		}
		return fileName;
	}

	// ═══════════════════════════════════════════════════════════════
	// MARK: - What counts as media
	// ═══════════════════════════════════════════════════════════════

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
	/// (kFolderMax above): an .mxf that isn't dot-hidden. Used by the
	/// rebalancer's packing and the Quarantined tally — NOT by the media
	/// table, which admits more (isAvidMediaName: non-MXF audio too, and
	/// it looks through a temp-rename suffix). The two differ on purpose;
	/// tst_conventions pins both.
	inline bool countsAsEssenceName(QStringView fileName)
	{
		return !isDotHidden(fileName) && hasMxfExtension(fileName);
	}
} // namespace Conventions
