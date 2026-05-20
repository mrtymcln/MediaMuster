#pragma once

#include <QString>

/// PMR file-record lookup keys, are shared by PmrParser
/// (table builder) and MediaScanner (table querier).
/// Centralised here so the two sides can't drift out of sync.

namespace PmrKey
{
// MARK: - Primary key

/// The canonical lookup: NFC-normalised, lower-cased filename.
///
/// Avid sometimes serialises Unicode in NFD form ("café" as
/// `c`,`a`,`f`,`e`,combining-acute instead of `c`,`a`,`f`,
/// precomposed-é). Comparing NFC-on-NFC avoids false negatives.
inline QString primary(const QString &filename)
{
	return filename.normalized(QString::NormalizationForm_C).toLower();
}

// MARK: - Fallback key

/// Used when the on-disk filename and the PMR's recorded name
/// diverge on extension or dot handling — strip the extension and
/// turn remaining dots into underscores. So `myclip.tail.mxf` and
/// `myclip_tail` both fall back to the same key.
///
/// Caller passes an already `primary()`-formatted string in.
inline QString fallback(const QString &primaryKey)
{
	const int lastDot = primaryKey.lastIndexOf(QLatin1Char('.'));
	QString base = (lastDot > 0) ? primaryKey.left(lastDot) : primaryKey;
	return base.replace(QLatin1Char('.'), QLatin1Char('_'));
}
} // namespace PmrKey