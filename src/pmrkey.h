#pragma once

#include <QString>

/// The PMR file-record lookup key, shared by PmrParser (table builder)
/// and MediaScanner (table querier). Centralised here so the two sides
/// can't drift out of sync.

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