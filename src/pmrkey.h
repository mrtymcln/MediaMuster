#pragma once

#include <QString>

/// Matching PMR filenames and discovered files uses the same normalisation.
/// Keeping it here prevents the two lookups from drifting apart.

namespace PmrKey
{
	/// The canonical lookup: NFC-normalised, lower-cased filename.
	///
	/// Avid sometimes serialises Unicode in NFD form ('café' as
	/// `c`,`a`,`f`,`e`,combining-acute instead of `c`,`a`,`f`,
	/// precomposed-é). Comparing NFC-on-NFC avoids false negatives.
	inline QString primary(const QString &filename)
	{
		return filename.normalized(QString::NormalizationForm_C).toLower();
	}
} // namespace PmrKey