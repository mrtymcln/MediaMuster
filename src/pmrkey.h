#pragma once

#include <QString>

/// Shared filename matching for PMR entries and discovered files.

namespace PmrKey
{
	/// NFC and lowercase make case and Unicode composition irrelevant to matching.
	inline QString primary(const QString &filename)
	{
		return filename.normalized(QString::NormalizationForm_C).toLower();
	}
} // namespace PmrKey
