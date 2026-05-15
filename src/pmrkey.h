#pragma once

#include <QString>

// PMR (msmFMID.pmr) file-record lookup keys, shared by PmrParser when it
// builds the lookup table and by MediaScanner when it queries the table.
// Both sides need to compute the same string from a filename or both lookups
// will silently miss; centralising the rule keeps them in sync.

namespace PmrKey
{
    // Canonical primary lookup: NFC-normalised, lower-cased filename. Avid
    // sometimes serialises macOS Unicode in NFD form; comparing NFC-on-NFC
    // is the only way these match without false negatives.
    inline QString primary(const QString &filename)
    {
        return filename.normalized(QString::NormalizationForm_C).toLower();
    }

    // Fallback for mismatches where the on-disk filename and the PMR's
    // recorded name diverge on extension or dot handling — strip the
    // extension and convert remaining dots to underscores. Pass an already
    // primary()-formatted string in (lower-case is assumed).
    inline QString fallback(const QString &primaryKey)
    {
        const int lastDot = primaryKey.lastIndexOf(QLatin1Char('.'));
        QString base = (lastDot > 0) ? primaryKey.left(lastDot) : primaryKey;
        return base.replace(QLatin1Char('.'), QLatin1Char('_'));
    }
}
