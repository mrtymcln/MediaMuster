#include "avideffects.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <iterator>

namespace
{
	// Current registration facts and shipped translation aliases, extracted from
	// Media Composer 26.8.0.58987. The generated table is compiled into the app.
	// Evidence and regeneration: docs/evidence/avid-effects-26.8/README.md.
	// A registered name does not prove the plug-in is installed or enabled.
	struct Entry
	{
		const char *name;
		const char *category;
		const char *localised[7]; // de, es, fr, it, ja, zh, ru
	};
	const Entry kEffects[] = {
#include "avideffectscatalogue.inc"
	};

	struct Row
	{
		QString name;
		QString category;
	};

	struct Table
	{
		QVector<Row> rows;
		QHash<QString, QVector<int>> byKey; ///< mangled name (any language) → rows
	};

	/// Built once, on first use, from the compiled-in catalogue. Names are
	/// mangled and hashed, a few milliseconds. The table itself costs nothing
	/// to load — it is string literals in the binary, not a file to find.
	const Table &table()
	{
		static const Table t = []
		{
			Table out;
			out.rows.reserve(std::size(kEffects));
			for (const Entry &e : kEffects)
			{
				const int idx = out.rows.size();
				out.rows.append({QString::fromUtf8(e.name), QString::fromUtf8(e.category)});
				// English name, then every localised spelling Avid ships.
				QSet<QString> keys{AvidEffects::mangle(out.rows[idx].name)};
				for (const char *localised : e.localised)
					if (localised && *localised)
						keys.insert(AvidEffects::mangle(QString::fromUtf8(localised)));
				for (const QString &k : keys)
					out.byKey[k].append(idx);
			}
			// Observed render spelling, corroborated by the current 3D Warp
			// registration and 2026 Effects Guide. This explicit compatibility
			// alias does not identify the producer's AlphaFlex execution variant.
			out.byKey.insert(QStringLiteral("3DWarp"),
							 out.byKey.value(QStringLiteral("3D_Warp")));
			return out;
		}();
		return t;
	}
} // namespace

QString AvidEffects::mangle(const QString &displayName)
{
	QString s = displayName;
	s.replace(QLatin1Char(' '), QLatin1Char('_'));
	s.replace(QLatin1Char(':'), QLatin1Char('_'));
	return s;
}

int AvidEffects::size()
{
	return table().rows.size();
}

AvidEffects::Hit AvidEffects::lookup(const QString &clipName)
{
	Hit hit;
	// Both observed render names keep the effect at the right-hand end:
	// `<sequence>,<Effect>+<N>` and `<sequence>,<Effect>,<N>[.new.<N>...]`.
	// Validate the latter's entire numeric suffix before using the preceding
	// comma; earlier commas still belong to the sequence. This labels only
	// already-classified precomputes and does not identify media by name.
	QString token = clipName;
	const int comma = clipName.lastIndexOf(QLatin1Char(','));
	if (comma >= 0)
	{
		hit.sequence = clipName.left(comma);
		token = clipName.mid(comma + 1);
	}
	bool commaInstance = false;
	static const QRegularExpression kCommaInstance(QStringLiteral("\\A[0-9]+(?:\\.new\\.[0-9]+)*\\z"));
	if (comma > 0 && kCommaInstance.match(token).hasMatch())
	{
		const int effectComma = clipName.lastIndexOf(QLatin1Char(','), comma - 1);
		if (effectComma >= 0 && effectComma + 1 < comma)
		{
			const QStringList numbers = token.split(QStringLiteral(".new."));
			bool valid = true;
			int instance = 0;
			for (qsizetype i = 0; i < numbers.size(); ++i)
			{
				bool ok = false;
				const int value = numbers[i].toInt(&ok);
				if (!ok)
				{
					valid = false;
					break;
				}
				if (i == 0)
					instance = value;
			}
			if (valid)
			{
				hit.sequence = clipName.left(effectComma);
				token = clipName.mid(effectComma + 1, comma - effectComma - 1);
				hit.instance = instance;
				commaInstance = true;
			}
		}
	}
	if (!commaInstance)
	{
		static const QRegularExpression kInstance(QStringLiteral("\\+([0-9]+)\\z"));
		const QRegularExpressionMatch m = kInstance.match(token);
		if (m.hasMatch() && m.capturedStart(0) > 0)
		{
			bool ok = false;
			const int instance = m.captured(1).toInt(&ok);
			if (ok)
			{
				hit.instance = instance;
				token.chop(m.capturedLength(0));
			}
		}
	}

	const Table &t = table();
	const auto it = t.byKey.constFind(token);
	if (it == t.byKey.constEnd() || it->isEmpty())
	{
		hit.name = token;
		hit.category = QStringLiteral("Unrecognized effect");
		return hit;
	}
	hit.matched = true;
	hit.name = t.rows[it->first()].name;
	// One name can live in several palette groups ("Bottom to Top" is a
	// Conceal, a Peel, a Push and a Squeeze); say all of them, in file order.
	QStringList categories;
	for (int idx : *it)
		if (!categories.contains(t.rows[idx].category))
			categories << t.rows[idx].category;
	hit.category = categories.join(QStringLiteral(" / "));
	return hit;
}
