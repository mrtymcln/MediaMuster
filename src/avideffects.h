#pragma once

#include <QString>

// MARK: - AvidEffects

/// Names a rendered effect from its precompute's clip name.
///
/// Observed names include `<sequence>,<Effect>+<N>` and
/// `<sequence>,<Effect>,<N>[.new.<N>...]`: the effect's display name with
/// spaces and colons turned into underscores, its sequence (which may
/// contain commas), and a numeric instance, optionally followed by rename
/// suffixes. The structural effect id is NOT in the media (verified:
/// no OperationGroup in any of 795 headers; the MDB's EffectID is a constant),
/// so the name is the only carrier — which is why this is consulted ONLY for
/// rows the usage code has already proven to be a precompute: it labels,
/// it never decides.
///
/// The catalogue is Avid's own: 887 effects extracted from a Media Composer
/// 2025.12 install and compiled straight into the binary — there is no data
/// file to find or lose (the table sits in avideffects.cpp, with its
/// provenance beside it).
/// Names are keyed in all five shipped languages, because a German Media
/// Composer writes "Blende" where an English one writes "Dissolve". 68 keys
/// are ambiguous across categories ("Bottom to Top" is a Conceal, a Peel, a
/// Push and a Squeeze); those report every category.
namespace AvidEffects
{
	struct Hit
	{
		QString name;	  ///< Avid's display name ("Color Correction"), or the raw token when unknown.
		QString category; ///< Palette group ("Image", "Timewarp"...), "A / B" when ambiguous,
						  ///< "Not a standard Avid effect" when unknown (a user-typed title,
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

	/// Rows loaded from the resource (for tests and the About box).
	[[nodiscard]] int size();
} // namespace AvidEffects
