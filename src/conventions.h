#pragma once

#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QtGlobal>
#include <array>

// MARK: - Conventions
/// Shared folder names, filename rules and capacity thresholds.
/// Keep external Avid spellings distinct from MediaMuster's own choices.
/// Format evidence and supported scope: docs/parser-compatibility.md and
/// docs/release-feature-gates.md. Values used by only one feature belong there.

namespace Conventions
{
	// ═══════════════════════════════════════════════════════════════
	// MARK: - Avid's folder names
	// ═══════════════════════════════════════════════════════════════

	/// [AVID] The folder Media Composer creates at a
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

	/// Legacy media uses a separate OMFI MediaFiles tree, with files directly
	/// inside it or one workstation-folder level below. It must not inherit
	/// MXF numbered-folder rules; Rebalance excludes this family.
	/// Format details: docs/parser-compatibility.md.
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

	/// [AVID] The staging subfolder Media Composer makes
	/// inside a media root while it is writing new media, in BOTH eras
	/// (under "Avid MediaFiles/MXF" and under "OMFI MediaFiles"). Its
	/// contents are half-written files that will be renamed into a real
	/// folder when capture finishes, so nothing here counts as media, fits
	/// a folder budget, or should be copied.
	inline constexpr QLatin1String kCreatingDir("Creating");

	/// Avid's destination for quarantined media; the scanner inventories
	/// this named folder under MXF separately from numbered media folders.
	inline constexpr QLatin1String kQuarantinedDir("Quarantined Files");

	/// Case-insensitive, for the same reasons as isMxfRootName.
	inline bool isCreatingFolderName(QStringView name)
	{
		return name.compare(kCreatingDir, Qt::CaseInsensitive) == 0;
	}

	// MARK: - Where Avid puts media on the system drive

	/// Fixed system-drive bases probed alongside mounted drive roots.
	/// Other intact media trees must be added explicitly; automatic scans
	/// do not search arbitrary descendants. C:/ retains the legacy Windows
	/// root location because volume discovery skips the boot volume.
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

	/// [AVID] The per-folder index and clip database, in
	/// both spellings Media Composer writes: msm* for media it manages,
	/// ama* for AMA-linked folders. A folder may hold either or both; the
	/// scanner reads every one present and merges. Same names in both eras;
	/// OMF databases live in the root or a shared workstation folder.
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

	/// Legacy media candidates: .omf may contain audio or video; .aif and
	/// .wav are the observed managed audio suffixes.
	/// These suffixes select the OMF reader, not proof of a valid container.
	inline bool hasOmfEraExtension(QStringView fileName)
	{
		return fileName.endsWith(QLatin1String(".omf"), Qt::CaseInsensitive) ||
			   fileName.endsWith(QLatin1String(".aif"), Qt::CaseInsensitive) ||
			   fileName.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive);
	}

	/// Combined filename allowlist; layout and the OMF gate narrow scan admission.
	inline bool hasAvidMediaExtension(QStringView fileName)
	{
		return hasMxfExtension(fileName) || hasOmfEraExtension(fileName);
	}

	// ═══════════════════════════════════════════════════════════════
	// MARK: - Avid's per-folder file budget
	// ═══════════════════════════════════════════════════════════════

	/// Reference ceiling used for folder warnings and the rebalance target.
	/// This is a file-count budget, not a byte-size limit.
	inline constexpr int kFolderMax = 5000;

	/// [MEDIAMUSTER] What the Rebalancer packs folders up to.
	/// One below Avid's ceiling, sourced from it so the two can't drift.
	inline constexpr int kFolderTarget = kFolderMax - 1; // 4999

	/// [MEDIAMUSTER] Where the folder-card bar turns red.
	inline constexpr int kFolderCritical = 4800;

	/// [MEDIAMUSTER] Where it turns amber.
	inline constexpr int kFolderWarn = 4500;

	// ═══════════════════════════════════════════════════════════════
	// MARK: - Names MediaMuster writes to disk
	// ═══════════════════════════════════════════════════════════════

	/// Delete's retained-file folder, beside the Avid media tree on the same
	/// filesystem. All locations and original paths are journalled.
	inline constexpr QLatin1String kMediaMusterTrashDir("_MediaMuster_Trash");

	// ═══════════════════════════════════════════════════════════════
	// MARK: - What counts as media
	// ═══════════════════════════════════════════════════════════════

	/// True when a directory entry occupies Avid's per-folder file budget
	/// (kFolderMax above): an .mxf that isn't dot-hidden. Used by the
	/// rebalancer's packing and the Quarantined tally. Scanner admission
	/// also requires a supported managed location and its matching family.
	inline bool countsAsEssenceName(QStringView fileName)
	{
		return !isDotHidden(fileName) && hasMxfExtension(fileName);
	}
} // namespace Conventions
