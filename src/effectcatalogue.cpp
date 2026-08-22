#include "effectcatalogue.h"
#include "logging.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace
{
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

	/// Loaded once, on first use, from the resource. The file is ~60 KB and
	/// ~890 rows; parsing it is a few milliseconds.
	const Table &table()
	{
		static const Table t = []
		{
			Table out;
			QFile f(QStringLiteral(":/res/avid-effects.tsv"));
			if (!f.open(QIODevice::ReadOnly))
			{
				qCWarning(lcScanner) << "effect catalogue resource missing";
				return out;
			}
			bool header = true;
			while (!f.atEnd())
			{
				const QString line = QString::fromUtf8(f.readLine()).trimmed();
				if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
					continue;
				if (header) // EffectName	Category	Tier	de_DE	es_ES	fr_FR	it_IT
				{
					header = false;
					continue;
				}
				const QStringList cols = line.split(QLatin1Char('\t'));
				if (cols.size() < 2 || cols[0].isEmpty())
					continue;
				const int idx = out.rows.size();
				out.rows.append({cols[0], cols[1]});
				// English name, then every localised spelling that exists.
				QSet<QString> keys{EffectCatalogue::mangle(cols[0])};
				for (int c = 3; c < cols.size(); ++c)
					if (!cols[c].isEmpty())
						keys.insert(EffectCatalogue::mangle(cols[c]));
				for (const QString &k : keys)
					out.byKey[k].append(idx);
			}
			return out;
		}();
		return t;
	}
} // namespace

QString EffectCatalogue::mangle(const QString &displayName)
{
	QString s = displayName;
	s.replace(QLatin1Char(' '), QLatin1Char('_'));
	s.replace(QLatin1Char(':'), QLatin1Char('_'));
	return s;
}

int EffectCatalogue::size()
{
	return table().rows.size();
}

EffectCatalogue::Hit EffectCatalogue::lookup(const QString &clipName)
{
	Hit hit;
	// `<sequence>,<Effect>+<N>`: split on the LAST comma — the separator is
	// not escaped and a sequence name may contain one.
	QString token = clipName;
	const int comma = clipName.lastIndexOf(QLatin1Char(','));
	if (comma >= 0)
	{
		hit.sequence = clipName.left(comma);
		token = clipName.mid(comma + 1);
	}
	static const QRegularExpression kInstance(QStringLiteral("\\+(\\d+)$"));
	const QRegularExpressionMatch m = kInstance.match(token);
	if (m.hasMatch())
	{
		hit.instance = m.captured(1).toInt();
		token.chop(m.capturedLength(0));
	}

	const Table &t = table();
	const auto it = t.byKey.constFind(token);
	if (it == t.byKey.constEnd() || it->isEmpty())
	{
		hit.name = token;
		hit.category = QStringLiteral("Not a standard Avid effect");
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
