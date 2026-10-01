#pragma once

#include "mobid.h"
#include "omfuid.h"

#include <QSet>
#include <QString>
#include <QtEndian>
#include <array>

/// One file identity used by the bin filter. Legacy records only identify
/// eight bytes; an Avid OMF wrapper preserves that same legacy identity.
struct BinMediaId
{
	QString fullId;
	QString legacyKey;
	bool legacy = false;

	/// Expects a canonical dotted full ID, as returned by MobId::format.
	static QString legacyPart(const QString &canonical)
	{
		// The 16-byte prefix occupies two dotted groups before the core.
		constexpr int kCoreHexOffset = 2 * static_cast<int>(sizeof OmfUid::kPrefix) + 2;
		return canonical.mid(kCoreHexOffset, 2 * OmfUid::kPmrSize);
	}

	static BinMediaId fromMobId(const QString &mobId)
	{
		QString hex = mobId;
		if (hex.size() == 2 * MobId::kRawSize + 3)
		{
			for (int position : {16, 33, 50})
				if (hex[position] != QLatin1Char('.'))
					return {};
			hex.remove(QLatin1Char('.'));
		}
		if (hex.size() != 2 * MobId::kRawSize)
			return {};
		for (const auto c : hex)
			if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
				  (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
				  (c >= QLatin1Char('A') && c <= QLatin1Char('F'))))
				return {};
		if (MobId::isAllZero(hex))
			return {};
		const QString full = MobId::format(QByteArray::fromHex(hex.toLatin1()));
		const QString key = legacyPart(full);
		const bool old = OmfUid::isWrappedOmfId(full);
		if (old && MobId::isAllZero(key))
			return {};
		return {full, key, old};
	}

	static BinMediaId fromLegacyWords(quint32 low, quint32 high)
	{
		if (low == 0 && high == 0)
			return {};
		std::array<uchar, OmfUid::kPmrSize> bytes{};
		qToLittleEndian(low, bytes.data());
		qToLittleEndian(high, bytes.data() + sizeof(low));
		return {{}, QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(bytes.data()),
												 OmfUid::kPmrSize).toHex()), true};
	}
};

/// File references from MSML locators. Modern IDs require full equality.
/// The short comparison is available only when either identity is legacy.
struct BinMediaIds
{
	QSet<QString> fullIds;
	QSet<QString> legacyKeys;

	bool isEmpty() const { return fullIds.isEmpty() && legacyKeys.isEmpty(); }

	void add(const BinMediaId &id)
	{
		if (!id.fullId.isEmpty())
			fullIds.insert(id.fullId);
		if (id.legacy && !id.legacyKey.isEmpty())
			legacyKeys.insert(id.legacyKey);
	}

	void unite(const BinMediaIds &other)
	{
		fullIds.unite(other.fullIds);
		legacyKeys.unite(other.legacyKeys);
	}

	bool contains(const BinMediaId &file) const
	{
		if (!file.fullId.isEmpty() && fullIds.contains(file.fullId))
			return true;
		if (file.legacyKey.isEmpty())
			return false;
		if (legacyKeys.contains(file.legacyKey))
			return true;
		// Older media can only compare its known section with a full bin ID.
		if (file.legacy)
			for (const QString &id : fullIds)
				if (BinMediaId::legacyPart(id) == file.legacyKey)
					return true;
		return false;
	}
};
