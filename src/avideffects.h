#pragma once

#include <QString>

// MARK: - AvidEffects

/// Names a rendered effect from its precompute's clip name.
///
/// Observed names include `<sequence>,<Effect>+<N>` and
/// `<sequence>,<Effect>,<N>[.new.<N>...]`. Exact known display spellings use
/// spaces/colons as underscores; sequence names can contain commas.
/// This lookup labels rows already classified as precomputes by metadata.
/// A clip name is editable, so a match is a label, not proof of effect identity.
///
/// Names/categories come from the installed Media Composer 26.8.0.58987 ARM64
/// registrations and shipped resources. Distinct AlphaFlex variants and
/// ambiguous categories are retained. See docs/evidence/avid-effects-26.8.
namespace AvidEffects
{
	struct Hit
	{
		QString name;	  ///< Avid's display name ("Color Correction"), or the raw token when unknown.
		QString category; ///< Avid category/group ("Image", "Timewarp"...), "A / B" when ambiguous,
						  ///< "unknown" when unknown (a user-typed title,
						  ///< an unregistered plug-in, or a renamed template).
		QString sequence; ///< The text preceding the effect token, as Avid wrote it (mangled).
		int instance = 0; ///< The +N or comma-N instance; 0 when absent or invalid.
		bool matched = false;
	};

	/// Parse a precompute clip name and look the effect up. An unrecognized
	/// effect token is returned verbatim, unmatched. Malformed or out-of-range
	/// numeric suffixes remain in the token rather than being silently lost.
	[[nodiscard]] Hit lookup(const QString &clipName);

	/// The clip-name spelling of a display name: space and colon → underscore.
	[[nodiscard]] QString mangle(const QString &displayName);

	/// Distinct name/category pairs compiled into the catalogue.
	[[nodiscard]] int size();
} // namespace AvidEffects
