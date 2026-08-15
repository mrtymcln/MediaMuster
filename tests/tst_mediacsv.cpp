#include "mediacsv.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// The CSV export. Its header string and its field chain are two parallel
// lists that must stay column-for-column aligned: insert a field in one
// and not the other and every later column shifts under the wrong
// heading — silently, in a file the user hands to production.

namespace
{
	int fieldCount(const QString &line)
	{
		// Split on commas outside quotes; CsvUtil quotes every string
		// column, and quoted values may contain commas.
		int n = 1;
		bool inQuotes = false;
		for (const QChar c : line)
		{
			if (c == QLatin1Char('"'))
				inQuotes = !inQuotes;
			else if (c == QLatin1Char(',') && !inQuotes)
				++n;
		}
		return n;
	}

	MediaFile sampleRow()
	{
		MediaFile f;
		f.clipName = QStringLiteral("Scene 1 - Take 3");
		f.fileName = QStringLiteral("A11B22C33D44.mxf");
		f.project = QStringLiteral("MyFilm");
		f.originalBin = QStringLiteral("Rushes");
		f.codec = QStringLiteral("Avid DNx SQ (DNxHD 145)");
		f.resolution = QStringLiteral("1920x1080");
		f.fps = QStringLiteral("25");
		f.durationFrames = 250;
		f.timecodeBase = 25;
		f.sizeBytes = 850'000'000;
		f.created = QDateTime(QDate(2026, 7, 20), QTime(12, 30));
		return f;
	}
} // namespace

class TestMediaCsv : public QObject
{
	Q_OBJECT
private slots:
	void header_and_row_have_the_same_field_count();
	void created_date_carries_time_of_day();
	void unknown_created_date_is_blank();
	void size_column_matches_the_table();
	void formula_injection_is_neutralised();
	void write_produces_header_plus_one_line_per_row();
};

void TestMediaCsv::header_and_row_have_the_same_field_count()
{
	// The alignment guard: this fails the moment someone adds a field to
	// one list and forgets the other.
	const int headerFields = fieldCount(MediaCsv::headerLine().trimmed());
	QCOMPARE(headerFields, 22);
	QCOMPARE(fieldCount(MediaCsv::rowLine(sampleRow()).trimmed()), headerFields);
	// An all-defaults row must line up too — no field may collapse when empty.
	QCOMPARE(fieldCount(MediaCsv::rowLine(MediaFile{}).trimmed()), headerFields);
}

void TestMediaCsv::created_date_carries_time_of_day()
{
	// Ruled 2026-08-12: the export matches what the table shows, rather
	// than silently dropping the time as it used to.
	QVERIFY(MediaCsv::rowLine(sampleRow()).contains(QStringLiteral("2026-07-20 12:30")));
}

void TestMediaCsv::unknown_created_date_is_blank()
{
	MediaFile f = sampleRow();
	f.created = QDateTime(); // filesystem records no birth time
	const QString line = MediaCsv::rowLine(f);
	QVERIFY(!line.contains(QStringLiteral("2026")));
	// Trailing empty field: the line ends with the separator, then EOL.
	QVERIFY(line.endsWith(QStringLiteral(",\n")));
}

void TestMediaCsv::size_column_matches_the_table()
{
	// Same helper the Size column uses, so export and screen agree.
	QVERIFY(MediaCsv::rowLine(sampleRow()).contains(sampleRow().sizeMBDisplay()));
	QCOMPARE(sampleRow().sizeMBDisplay(), QStringLiteral("850.0"));
}

void TestMediaCsv::formula_injection_is_neutralised()
{
	MediaFile f = sampleRow();
	f.clipName = QStringLiteral("=cmd|'/c calc'!A1");
	const QString line = MediaCsv::rowLine(f);
	// CsvUtil prefixes a quote so a spreadsheet reads it as text.
	QVERIFY(line.startsWith(QStringLiteral("\"'=cmd")));
}

void TestMediaCsv::write_produces_header_plus_one_line_per_row()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = tmp.path() + QStringLiteral("/export.csv");

	QVERIFY(MediaCsv::write(path, {sampleRow(), sampleRow()}));

	QFile f(path);
	QVERIFY(f.open(QIODevice::ReadOnly));
	const QByteArray raw = f.readAll();
	// UTF-8 BOM keeps Excel on Windows from mojibaking non-Latin names.
	QVERIFY(raw.startsWith("\xEF\xBB\xBF"));
	QCOMPARE(QString::fromUtf8(raw).count(QLatin1Char('\n')), 3); // header + 2 rows
}

QTEST_APPLESS_MAIN(TestMediaCsv)
#include "tst_mediacsv.moc"
