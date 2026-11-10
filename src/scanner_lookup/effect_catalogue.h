#pragma once

#include <QString>

/// Looks up labels for clips already identified as precomputes.
/// Clip names can be edited, so a match does not prove which effect was used.
namespace AvidEffects
{
	struct Hit
	{
		QString name;     ///< Avid's display name, or the unrecognised token.
		QString category; ///< Matching groups joined with " / ", or "unknown".
		QString sequence; ///< Everything before the effect token, left as written.
		int instance = 0; ///< The +N or comma-N instance; 0 when absent or invalid.
		bool matched = false;
	};

	/// Handles +N and comma-N instances, including repeated .new.N suffixes.
	/// Sequence names can contain commas. Unknown tokens and invalid numeric
	/// suffixes stay visible in the result.
	[[nodiscard]] Hit lookup(const QString &clipName);

	/// Replaces spaces and colons with underscores, as Avid does in clip names.
	[[nodiscard]] QString mangle(const QString &displayName);

	/// Returns the number of name/category entries in this catalogue.
	[[nodiscard]] int size();
} // namespace AvidEffects
