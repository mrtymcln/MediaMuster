#pragma once

#include <QtGlobal>

/// Recorded units per second. Keep the original fraction, including unreduced rates.
// MARK: - Media rate
struct MediaRate
{
	qint32 numerator = 0;
	qint32 denominator = 0;

	bool valid() const { return numerator > 0 && denominator > 0; }
	double value() const { return valid() ? double(numerator) / denominator : 0.0; }
	bool sameRate(const MediaRate &other) const
	{
		return valid() && other.valid() &&
			   qint64(numerator) * other.denominator == qint64(other.numerator) * denominator;
	}
};
