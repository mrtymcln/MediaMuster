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
	// MARK: - Avid's folder names

	/// Avid's managed-media tree and its MXF subfolder.
	inline constexpr QLatin1String kAvidMediaFilesDir("Avid MediaFiles");
	inline constexpr QLatin1String kMxfDir("MXF");

	/// Accept case variants on shares and restored media trees.
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

	/// "<base>/OMFI MediaFiles"; OMF has no MXF subfolder.
	inline QString omfRootUnder(const QString &base)
	{
		return base + QLatin1Char('/') + kOmfMediaFilesDir;
	}

	// MARK: - Avid's transient capture folder

	/// In-progress capture folder in MXF and OMF trees; exclude its unfinished files
	/// from scans, transfers and folder budgets.
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

	// MARK: - Avid's database file names

	/// Per-folder databases: msm* for managed media and ama* for AMA-linked folders.
	/// Scan all present spellings in either media family; OMF databases may also
	/// be directly inside OMFI MediaFiles.
	inline constexpr std::array<QLatin1String, 2> kPmrFileNames = {
		QLatin1String("msmFMID.pmr"), QLatin1String("amaFMID.pmr")};
	inline constexpr std::array<QLatin1String, 2> kMdbFileNames = {
		QLatin1String("msmMMOB.mdb"), QLatin1String("amaMMOB.mdb")};

	// MARK: - Avid's essence-file names

	/// Exclude leading-dot names, including AppleDouble sidecars, consistently across platforms.
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

	// MARK: - Avid's per-folder file budget

	/// Avid's automatic rollover threshold, used for folder warnings and Rebalance.
	/// This is a file-count budget, not a byte-size limit.
	inline constexpr int kFolderMax = 5000;

	/// [MEDIAMUSTER] Where the folder-card bar turns red.
	inline constexpr int kFolderCritical = 4800;

	/// [MEDIAMUSTER] Where it turns amber.
	inline constexpr int kFolderWarn = 4500;

	// MARK: - Names MediaMuster writes to disk

	/// Delete's retained-file folder, beside the Avid media tree on the same
	/// filesystem. All locations and original paths are journalled.
	inline constexpr QLatin1String kMediaMusterTrashDir("_MediaMuster_Trash");

	// MARK: - What counts as media

	/// True when a directory entry occupies Avid's per-folder file budget
	/// (kFolderMax above): an .mxf that isn't dot-hidden. Used by the
	/// rebalancer's packing and the Quarantined tally. Scanner admission
	/// also requires a supported managed location and its matching family.
	inline bool countsAsEssenceName(QStringView fileName)
	{
		return !isDotHidden(fileName) && hasMxfExtension(fileName);
	}
} // namespace Conventions
