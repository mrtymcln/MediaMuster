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
	QStringList readCsvRecord(QString line)
	{
		if (line.endsWith(QLatin1Char('\n')))
			line.chop(1);
		QStringList fields;
		QString field;
		bool quoted = false;
		for (qsizetype i = 0; i < line.size(); ++i)
		{
			const QChar c = line[i];
			if (c == QLatin1Char('"'))
			{
				if (quoted && i + 1 < line.size() && line[i + 1] == c)
				{
					field += c;
					++i;
				}
				else quoted = !quoted;
			}
			else if (c == QLatin1Char(',') && !quoted)
			{
				fields.append(field);
				field.clear();
			}
			else field += c;
		}
		fields.append(field);
		return fields;
	}

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
		f.kind = MediaFile::Kind::Video;
		f.type = MediaFile::Type::Media;
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

	// Location columns (2026-08-18): the export carries the volume by name
	// and the whole path, and nothing else. The folder the clip sits in is
	// already inside the path, so a separate column for it only invited
	// the two to disagree.
	void volume_and_location_columns_carry_name_and_full_path();
	void formula_injection_is_neutralised();
	void write_produces_header_plus_one_line_per_row();
	void unknown_classification_is_exported_without_guessing();
	void effect_details_are_explicit_and_quoted();
	void non_precompute_effect_fields_stay_blank();
	void precompute_categories_preserve_unknown_details();
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
	const MediaCsv::Options enabled{true};
	QCOMPARE(fieldCount(MediaCsv::headerLine(enabled)), 26);
	QCOMPARE(fieldCount(MediaCsv::rowLine(sampleRow(), enabled)), 26);
	QCOMPARE(fieldCount(MediaCsv::rowLine(MediaFile{}, enabled)), 26);
}

void TestMediaCsv::volume_and_location_columns_carry_name_and_full_path()
{
	const QStringList headers = MediaCsv::headerLine().trimmed().split(QLatin1Char(','));
	QCOMPARE(headers.count(QStringLiteral("Volume")), 1);
	QCOMPARE(headers.count(QStringLiteral("Location")), 1);
	QVERIFY2(!headers.contains(QStringLiteral("Folder")), "Folder is inside Location now");
	QVERIFY2(!headers.contains(QStringLiteral("Path")), "Path was renamed to Location");

	MediaFile f = sampleRow();
	f.volumeName = QStringLiteral("EDIT");
	f.mxfFolder = QStringLiteral("8646");
	f.filePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/8646/A11B22C33D44.mxf");

	const QStringList fields = MediaCsv::rowLine(f).trimmed().split(QLatin1Char(','));
	QCOMPARE(fields.size(), headers.size());
	// Values are CsvUtil::quoted, so they arrive wrapped.
	QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Volume"))), QStringLiteral("\"EDIT\""));
	QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Location"))),
			 QLatin1Char('"') + f.filePath + QLatin1Char('"'));
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

	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		QVERIFY(MediaCsv::write(path, {sampleRow(), sampleRow()}, options));
		QFile f(path);
		QVERIFY(f.open(QIODevice::ReadOnly));
		const QByteArray raw = f.readAll();
		// UTF-8 BOM keeps Excel on Windows from mojibaking non-Latin names.
		QVERIFY(raw.startsWith("\xEF\xBB\xBF"));
		QCOMPARE(raw.mid(3), (MediaCsv::headerLine(options) +
			MediaCsv::rowLine(sampleRow(), options) + MediaCsv::rowLine(sampleRow(), options)).toUtf8());
		QCOMPARE(QString::fromUtf8(raw).count(QLatin1Char('\n')), 3);
	}
}

void TestMediaCsv::unknown_classification_is_exported_without_guessing()
{
	const QStringList headers = MediaCsv::headerLine().trimmed().split(QLatin1Char(','));
	const auto unknown = MediaCsv::rowLine(MediaFile{}).trimmed().split(QLatin1Char(','));
	QCOMPARE(unknown.at(headers.indexOf(QStringLiteral("Kind"))), QStringLiteral("\"\u2014\""));
	QCOMPARE(unknown.at(headers.indexOf(QStringLiteral("Type"))), QStringLiteral("\"\u2014\""));

	const auto known = MediaCsv::rowLine(sampleRow()).trimmed().split(QLatin1Char(','));
	QCOMPARE(known.at(headers.indexOf(QStringLiteral("Kind"))), QStringLiteral("\"Video\""));
	QCOMPARE(known.at(headers.indexOf(QStringLiteral("Type"))), QStringLiteral("\"Media\""));
}

void TestMediaCsv::effect_details_are_explicit_and_quoted()
{
	MediaFile f = sampleRow();
	f.type = MediaFile::Type::Precompute;
	f.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	f.effect = QStringLiteral("=Custom,\"Quoted\"\nEffect");
	f.effectCategory = QStringLiteral("@Category,\"Quoted\"");
	f.effectSequence = QStringLiteral("+Sequence,\"Quoted\"\nNext");
	f.codecHex = QStringLiteral("raw-debug-value");
	f.modified = QDateTime(QDate(2026, 9, 5), QTime(10, 15));
	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto fields = readCsvRecord(MediaCsv::rowLine(f, options));
		QCOMPARE(fields.size(), headers.size());
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Type"))], QStringLiteral("Precompute"));
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Codec"))], f.codec);
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Date Created"))], f.createdDisplay());
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Date Modified"))], f.modifiedDisplay());
		if (enabled)
		{
			QCOMPARE(fields[headers.indexOf(QStringLiteral("Precompute Category"))], QStringLiteral("Rendered Effects"));
			QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect"))], QLatin1Char('\'') + f.effect);
			QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect Category"))], QLatin1Char('\'') + f.effectCategory);
			QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect Sequence"))], QLatin1Char('\'') + f.effectSequence);
		}
		else
		{
			QVERIFY(!headers.contains(QStringLiteral("Precompute Category")));
			QVERIFY(!headers.contains(QStringLiteral("Effect")));
			QVERIFY(!headers.contains(QStringLiteral("Effect Category")));
			QVERIFY(!headers.contains(QStringLiteral("Effect Sequence")));
			QVERIFY(!MediaCsv::rowLine(f, options).contains(QStringLiteral("Custom")));
		}
	}
}

void TestMediaCsv::non_precompute_effect_fields_stay_blank()
{
	const MediaCsv::Options options{true};
	const auto headers = readCsvRecord(MediaCsv::headerLine(options));
	for (auto type : {MediaFile::Type::Media, MediaFile::Type::Unknown})
	{
		MediaFile f = sampleRow();
		f.type = type;
		f.effect = f.effectCategory = f.effectSequence = QStringLiteral("stale detail");
		const auto fields = readCsvRecord(MediaCsv::rowLine(f, options));
		for (const auto &header : {QStringLiteral("Precompute Category"), QStringLiteral("Effect"), QStringLiteral("Effect Category"), QStringLiteral("Effect Sequence")})
			QVERIFY(fields[headers.indexOf(header)].isEmpty());
	}
}

void TestMediaCsv::precompute_categories_preserve_unknown_details()
{
	const MediaCsv::Options options{true};
	const auto headers = readCsvRecord(MediaCsv::headerLine(options));
	const QStringList expected{QStringLiteral("Rendered Effects"), QStringLiteral("Titles and Matte Keys"), QStringLiteral("unknown")};
	int row = 0;
	for (auto category : {MediaFile::PrecomputeCategory::RenderedEffects,
		MediaFile::PrecomputeCategory::TitlesAndMatteKeys, MediaFile::PrecomputeCategory::Unknown})
	{
		MediaFile file;
		file.type = MediaFile::Type::Precompute;
		file.precomputeCategory = category;
		const auto fields = readCsvRecord(MediaCsv::rowLine(file, options));
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Type"))], QStringLiteral("Precompute"));
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Precompute Category"))], expected[row++]);
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect Category"))], QStringLiteral("unknown"));
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect"))], QStringLiteral("unknown"));
	}
}

QTEST_APPLESS_MAIN(TestMediaCsv)
#include "tst_mediacsv.moc"
