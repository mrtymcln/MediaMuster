#pragma once

// MC 26.8 brDisplayable (arm64 0x100db60c0): after proving a precompute
// master, a direct _IMPORTSETTING object attribute and >=2 direct video
// tracks select Titles and Matte Keys; the defined alternatives are renders.
// A failed read is not evidence that the attribute is absent.
namespace AvidPrecompute
{
	enum class Category
	{
		Unknown,
		RenderedEffects,
		TitlesAndMatteKeys
	};
	enum class ImportAttribute
	{
		Unknown,
		Absent,
		Present,
		Conflicting
	};
	struct Evidence
	{
		ImportAttribute importAttribute = ImportAttribute::Unknown;
		int videoTrackCount = -1; // -1 means the direct track count is not established.
	};

	// Call only for a master already classified as a precompute. Avid skips
	// the track count when the import object is confirmed absent.
	constexpr Category classify(const Evidence &evidence)
	{
		if (evidence.importAttribute == ImportAttribute::Absent)
			return Category::RenderedEffects;
		if (evidence.importAttribute != ImportAttribute::Present || evidence.videoTrackCount < 0)
			return Category::Unknown;
		return evidence.videoTrackCount >= 2 ? Category::TitlesAndMatteKeys : Category::RenderedEffects;
	}
}
