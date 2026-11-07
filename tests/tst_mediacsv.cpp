#include "mediacsv.h"
#include "testmediafile.h"

#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

// Export headings and row values must keep the same column order.

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
				else
					quoted = !quoted;
			}
			else if (c == QLatin1Char(',') && !quoted)
			{
				fields.append(field);
				field.clear();
			}
			else
				field += c;
		}
		fields.append(field);
		return fields;
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
		f.compression = QStringLiteral("Avid DNx SQ (DNxHD 145)");
		TestMediaFile::setFrameRate(f, 25);
		TestMediaFile::setDuration(f, 250, 25);
		f.bellaValues->resolution = Bella::MatchDimensions{1920, 1080};
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
	void selected_master_and_physical_row_identity_are_exported();
	void header_order_and_values_follow_the_export_schema();
	void database_flags_replace_the_status_label_data();
	void database_flags_replace_the_status_label();
	void created_date_carries_time_of_day();
	void unknown_created_date_is_blank();
	void size_column_matches_the_table();
	void sample_rate_and_bit_depth_are_exported_data();
	void sample_rate_and_bit_depth_are_exported();

	void physical_path_is_retained_and_removed_columns_are_omitted();
	void file_path_is_exported_with_csv_protection_data();
	void file_path_is_exported_with_csv_protection();
	void formula_injection_is_neutralised();
	void write_produces_header_plus_one_line_per_row();
	void write_failure_preserves_existing_export();
	void unknown_classification_is_exported_without_guessing();
	void effect_details_are_explicit_and_quoted();
	void non_precompute_effect_fields_stay_blank();
	void precompute_categories_preserve_unknown_details();
};

void TestMediaCsv::selected_master_and_physical_row_identity_are_exported()
{
	MediaFile file = sampleRow();
	file.kelpieId = 42;
	file.fileMobId = QStringLiteral("file-id");
	file.masterMobId = QStringLiteral("selected-master");
	file.omfEra = true;
	const auto headers = readCsvRecord(MediaCsv::headerLine());
	const auto values = readCsvRecord(MediaCsv::rowLine(file));
	QCOMPARE(headers.count(QStringLiteral("FileMobId")), 1);
	QCOMPARE(headers.count(QStringLiteral("MasterMobId")), 1);
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("FileMobId"))), QStringLiteral("file-id"));
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("MasterMobId"))), QStringLiteral("selected-master"));
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("KelpieId"))), QStringLiteral("42"));
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("OmfScan"))), QStringLiteral("true"));
	QVERIFY(!headers.contains(QStringLiteral("Sample Format")));
	QVERIFY(!headers.contains(QStringLiteral("Channels")));
}

void TestMediaCsv::header_and_row_have_the_same_field_count()
{
	const auto headerFields = readCsvRecord(MediaCsv::headerLine()).size();
	QCOMPARE(headerFields, 21);
	QCOMPARE(readCsvRecord(MediaCsv::rowLine(sampleRow())).size(), headerFields);
	// An all-defaults row must line up too — no field may collapse when empty.
	QCOMPARE(readCsvRecord(MediaCsv::rowLine(MediaFile{})).size(), headerFields);
	const MediaCsv::Options enabled{true};
	QCOMPARE(readCsvRecord(MediaCsv::headerLine(enabled)).size(), 24);
	QCOMPARE(readCsvRecord(MediaCsv::rowLine(sampleRow(), enabled)).size(), 24);
	QCOMPARE(readCsvRecord(MediaCsv::rowLine(MediaFile{}, enabled)).size(), 24);
}

void TestMediaCsv::header_order_and_values_follow_the_export_schema()
{
	MediaFile f = sampleRow();
	TestMediaFile::setSampleRate(f, 48000);
	TestMediaFile::setBitDepth(f, 10);
	f.type = MediaFile::Type::Precompute;
	f.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	f.effectCategory = QStringLiteral("Image");
	f.effect = QStringLiteral("Color Correction");
	f.modified = QDateTime(QDate(2026, 9, 5), QTime(10, 15));
	f.mediaFilePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/1/A11B22C33D44.mxf");
	f.dbStatus = MediaFile::DbStatus::NoReference;
	f.fileMobId = QStringLiteral("file-mob-id");
	f.masterMobId = QStringLiteral("master-mob-id");
	for (bool enabled : {false, true})
	{
		QStringList expectedHeaders = QStringLiteral(
										  "Clip Name,Project,Bin,Kind,Duration,Size (MB),Compression,Resolution,Frame Rate,Sample Rate,Bit Depth,Type,"
										  "Date Created,Filename,File Path,No Database,No Reference,FileMobId,MasterMobId,KelpieId,OmfScan")
										  .split(QLatin1Char(','));
		QStringList expectedFields{
			QStringLiteral("Scene 1 - Take 3"), QStringLiteral("MyFilm"), QStringLiteral("Rushes"),
			QStringLiteral("Video"), QStringLiteral("00:00:10:00"), QStringLiteral("850.0"),
			QStringLiteral("Avid DNx SQ (DNxHD 145)"), QStringLiteral("1920x1080"), QStringLiteral("25"),
			QStringLiteral("48 kHz"), QStringLiteral("10-bit"), QStringLiteral("Precompute"),
			QStringLiteral("2026-07-20 12:30"),
			QStringLiteral("A11B22C33D44.mxf"),
			QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/1/A11B22C33D44.mxf"),
			QStringLiteral("false"), QStringLiteral("true"), QStringLiteral("file-mob-id"), QStringLiteral("master-mob-id"), QStringLiteral("0"), QStringLiteral("false")};
		if (enabled)
		{
			const QStringList detailHeaders{QStringLiteral("Precompute Category"), QStringLiteral("Effect Category"),
											QStringLiteral("Effect")};
			const QStringList detailFields{QStringLiteral("Rendered Effects"), QStringLiteral("Image"),
										   QStringLiteral("Color Correction")};
			for (int i = 0; i < detailHeaders.size(); ++i)
			{
				expectedHeaders.insert(12 + i, detailHeaders[i]);
				expectedFields.insert(12 + i, detailFields[i]);
			}
		}
		const MediaCsv::Options options{enabled};
		QCOMPARE(readCsvRecord(MediaCsv::headerLine(options)), expectedHeaders);
		QCOMPARE(readCsvRecord(MediaCsv::rowLine(f, options)), expectedFields);
	}
}

void TestMediaCsv::database_flags_replace_the_status_label_data()
{
	QTest::addColumn<MediaFile::DbStatus>("status");
	QTest::addColumn<QString>("noDatabase");
	QTest::addColumn<QString>("noReference");
	QTest::newRow("listed") << MediaFile::DbStatus::Listed << QStringLiteral("false") << QStringLiteral("false");
	QTest::newRow("checked-missing-reference") << MediaFile::DbStatus::NoReference << QStringLiteral("false") << QStringLiteral("true");
	QTest::newRow("no-local-databases") << MediaFile::DbStatus::NoDatabase << QStringLiteral("true") << QStringLiteral("false");
	QTest::newRow("unresolved-local-checks") << MediaFile::DbStatus::DbUnreadable << QStringLiteral("true") << QStringLiteral("false");
}

void TestMediaCsv::database_flags_replace_the_status_label()
{
	QFETCH(MediaFile::DbStatus, status);
	QFETCH(QString, noDatabase);
	QFETCH(QString, noReference);
	MediaFile file = sampleRow();
	file.dbStatus = status;
	for (const bool precomputes : {false, true})
	{
		const MediaCsv::Options options{precomputes};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const QString line = MediaCsv::rowLine(file, options);
		const auto values = readCsvRecord(line);
		QVERIFY(!headers.contains(QStringLiteral("Database Status")));
		QCOMPARE(headers.count(QStringLiteral("No Database")), 1);
		QCOMPARE(headers.count(QStringLiteral("No Reference")), 1);
		const int databaseColumn = headers.indexOf(QStringLiteral("No Database"));
		const int referenceColumn = headers.indexOf(QStringLiteral("No Reference"));
		QCOMPARE(databaseColumn, headers.indexOf(QStringLiteral("File Path")) + 1);
		QCOMPARE(referenceColumn, databaseColumn + 1);
		QCOMPARE(headers.indexOf(QStringLiteral("FileMobId")), referenceColumn + 1);
		QCOMPARE(values.at(databaseColumn), noDatabase);
		QCOMPARE(values.at(referenceColumn), noReference);
		// This row has no embedded commas: inspect raw cells to retain the
		// unquoted lowercase boolean format used by OmfScan.
		const auto rawValues = line.split(QLatin1Char(','));
		QCOMPARE(rawValues.at(databaseColumn), noDatabase);
		QCOMPARE(rawValues.at(referenceColumn), noReference);
	}
}

void TestMediaCsv::sample_rate_and_bit_depth_are_exported_data()
{
	QTest::addColumn<MediaFile::Kind>("kind");
	QTest::addColumn<int>("sampleRate");
	QTest::addColumn<quint32>("bits");
	QTest::addColumn<QString>("bitDepthLabel");
	QTest::addColumn<QString>("sampleRateLabel");
	QTest::newRow("audio-44.1-khz") << MediaFile::Kind::Audio << 44100 << quint32(16) << QStringLiteral("16-bit") << QStringLiteral("44.1 kHz");
	QTest::newRow("audio-48-khz") << MediaFile::Kind::Audio << 48000 << quint32(24) << QStringLiteral("24-bit") << QStringLiteral("48 kHz");
	QTest::newRow("audio-float") << MediaFile::Kind::Audio << 96000 << quint32(32) << QStringLiteral("32-bit") << QStringLiteral("96 kHz");
	QTest::newRow("video-bit-depth") << MediaFile::Kind::Video << 0 << quint32(10) << QStringLiteral("10-bit") << QString();
	QTest::newRow("unknown") << MediaFile::Kind::Unknown << 0 << quint32(0) << QString() << QString();
}

void TestMediaCsv::sample_rate_and_bit_depth_are_exported()
{
	QFETCH(MediaFile::Kind, kind);
	QFETCH(int, sampleRate);
	QFETCH(quint32, bits);
	QFETCH(QString, bitDepthLabel);
	QFETCH(QString, sampleRateLabel);
	MediaFile f = sampleRow();
	f.kind = kind;
	TestMediaFile::setSampleRate(f, sampleRate);
	TestMediaFile::setBitDepth(f, bits ? std::optional<quint32>(bits) : std::nullopt);
	if (QTest::currentDataTag() == QByteArray("audio-float"))
		f.bellaValues->bitDepth.representation = Bella::MatchDepthRepresentation::Float;
	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto fields = readCsvRecord(MediaCsv::rowLine(f, options));
		QCOMPARE(fields.size(), headers.size());
		const int frameRateIndex = headers.indexOf(QStringLiteral("Frame Rate"));
		QVERIFY(frameRateIndex >= 0);
		QCOMPARE(headers.at(frameRateIndex + 1), QStringLiteral("Sample Rate"));
		QCOMPARE(headers.at(frameRateIndex + 2), QStringLiteral("Bit Depth"));
		QCOMPARE(headers.at(frameRateIndex + 3), QStringLiteral("Type"));
		QCOMPARE(fields.at(frameRateIndex), f.frameRateDisplay());
		QCOMPARE(fields.at(frameRateIndex + 1), sampleRateLabel);
		QCOMPARE(fields.at(frameRateIndex + 1), f.sampleRateDisplay());
		QCOMPARE(fields.at(frameRateIndex + 2), bitDepthLabel);
		QCOMPARE(fields.at(frameRateIndex + 3), f.typeDisplay());
	}
}

void TestMediaCsv::physical_path_is_retained_and_removed_columns_are_omitted()
{
	MediaFile f = sampleRow();
	f.volumeName = QStringLiteral("EDIT");
	f.mediaFolderName = QStringLiteral("8646");
	f.mediaFilePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/8646/A11B22C33D44.mxf");
	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto fields = readCsvRecord(MediaCsv::rowLine(f, options));
		QCOMPARE(fields.size(), headers.size());
		QCOMPARE(headers.count(QStringLiteral("File Path")), 1);
		QCOMPARE(fields.at(headers.indexOf(QStringLiteral("File Path"))), f.mediaFilePath);
		QVERIFY(!headers.contains(QStringLiteral("Source Path")));
		QVERIFY(!headers.contains(QStringLiteral("Location")));
		QVERIFY(!headers.contains(QStringLiteral("Effect Sequence")));
		for (const auto &removed : {QStringLiteral("Volume"), QStringLiteral("Source Filename"),
									QStringLiteral("Source Container"), QStringLiteral("Imported")})
			QVERIFY(!headers.contains(removed));
		QVERIFY(!fields.contains(f.volumeName));
	}
}

void TestMediaCsv::file_path_is_exported_with_csv_protection_data()
{
	QTest::addColumn<QString>("filePath");
	QTest::addColumn<QString>("expected");
	QTest::newRow("posix") << QStringLiteral("/Recorded Imports/Camera A/interview.mov")
		<< QStringLiteral("/Recorded Imports/Camera A/interview.mov");
	QTest::newRow("windows") << QStringLiteral("C:\\Recorded Imports\\Camera A\\interview.mov")
		<< QStringLiteral("C:\\Recorded Imports\\Camera A\\interview.mov");
	QTest::newRow("url") << QStringLiteral("file:///Recorded%20Imports/Camera%20A/interview.mov")
		<< QStringLiteral("file:///Recorded%20Imports/Camera%20A/interview.mov");
	QTest::newRow("csv-characters") << QStringLiteral("/imports/Day,\"Quoted\"\nCamera/interview.mov")
		<< QStringLiteral("/imports/Day,\"Quoted\"\nCamera/interview.mov");
	QTest::newRow("formula-prefix") << QStringLiteral("=HYPERLINK(\"file:///imports/interview.mov\")")
		<< QStringLiteral("'=HYPERLINK(\"file:///imports/interview.mov\")");
	QTest::newRow("at-prefix") << QStringLiteral("@imports/interview.mov") << QStringLiteral("'@imports/interview.mov");
	QTest::newRow("tab-prefix") << QStringLiteral("\t/imports/interview.mov") << QStringLiteral("'\t/imports/interview.mov");
	QTest::newRow("unavailable") << QString{} << QString{};
}

void TestMediaCsv::file_path_is_exported_with_csv_protection()
{
	QFETCH(QString, filePath);
	QFETCH(QString, expected);
	auto file = sampleRow();
	file.mediaFilePath = filePath;
	for (const bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto fields = readCsvRecord(MediaCsv::rowLine(file, options));
		QCOMPARE(fields.size(), headers.size());
		QCOMPARE(headers.count(QStringLiteral("File Path")), 1);
		QVERIFY(!headers.contains(QStringLiteral("Source Filename")));
		QCOMPARE(fields.at(headers.indexOf(QStringLiteral("File Path"))), expected);
		QCOMPARE(fields.at(headers.indexOf(QStringLiteral("Filename"))), file.fileName);
	}
}

void TestMediaCsv::created_date_carries_time_of_day()
{
	// Export the same date and time shown in the table.
	QVERIFY(MediaCsv::rowLine(sampleRow()).contains(QStringLiteral("2026-07-20 12:30")));
}

void TestMediaCsv::unknown_created_date_is_blank()
{
	MediaFile f = sampleRow();
	f.created = QDateTime(); // filesystem records no birth time
	const auto headers = readCsvRecord(MediaCsv::headerLine());
	const auto fields = readCsvRecord(MediaCsv::rowLine(f));
	QVERIFY(fields.at(headers.indexOf(QStringLiteral("Date Created"))).isEmpty());
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
	// Prefix formula-like text with an apostrophe so spreadsheets treat it as text.
	QVERIFY(line.startsWith(QStringLiteral("\"'=cmd")));
}

void TestMediaCsv::write_produces_header_plus_one_line_per_row()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = tmp.path() + QStringLiteral("/export.csv");
	{
		QFile previous(path);
		QVERIFY(previous.open(QIODevice::WriteOnly));
		QCOMPARE(previous.write(QByteArray(4096, '#')), qint64(4096));
	}

	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		QVERIFY(MediaCsv::write(path, {sampleRow(), sampleRow()}, options));
		QFile f(path);
		QVERIFY(f.open(QIODevice::ReadOnly));
		const QByteArray raw = f.readAll();
		// The UTF-8 BOM lets Excel on Windows recognize non-Latin text.
		QVERIFY(raw.startsWith("\xEF\xBB\xBF"));
		QCOMPARE(raw.mid(3), (MediaCsv::headerLine(options) +
							  MediaCsv::rowLine(sampleRow(), options) + MediaCsv::rowLine(sampleRow(), options))
								 .toUtf8());
		QCOMPARE(QString::fromUtf8(raw).count(QLatin1Char('\n')), 3);
	}
}

void TestMediaCsv::write_failure_preserves_existing_export()
{
	QTemporaryDir tmp;
	QVERIFY(tmp.isValid());
	const QString path = tmp.filePath(QStringLiteral("export.csv"));
	const QByteArray original("previous export\n");
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write(original), qint64(original.size()));
	file.close();
	const auto permissions = file.permissions();
	const auto restorePermissions = qScopeGuard([&]
												{ file.setPermissions(permissions); });
	QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser));
	QVERIFY(!MediaCsv::write(path, {sampleRow()}));
	QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(file.readAll(), original);
}

void TestMediaCsv::unknown_classification_is_exported_without_guessing()
{
	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto unknown = readCsvRecord(MediaCsv::rowLine(MediaFile{}, options));
		QCOMPARE(unknown.at(headers.indexOf(QStringLiteral("Kind"))), QStringLiteral("unknown"));
		QCOMPARE(unknown.at(headers.indexOf(QStringLiteral("Type"))), QString{});
		const auto known = readCsvRecord(MediaCsv::rowLine(sampleRow(), options));
		QCOMPARE(known.at(headers.indexOf(QStringLiteral("Kind"))), QStringLiteral("Video"));
		QCOMPARE(known.at(headers.indexOf(QStringLiteral("Type"))), QStringLiteral("Media"));
	}
}

void TestMediaCsv::effect_details_are_explicit_and_quoted()
{
	MediaFile f = sampleRow();
	f.type = MediaFile::Type::Precompute;
	f.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
	f.effect = QStringLiteral("=Custom,\"Quoted\"\nEffect");
	f.effectCategory = QStringLiteral("@Category,\"Quoted\"");
	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto fields = readCsvRecord(MediaCsv::rowLine(f, options));
		QCOMPARE(fields.size(), headers.size());
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Compression"))], f.compression);
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Date Created"))], f.createdDisplay());
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Type"))], QStringLiteral("Precompute"));
		if (enabled)
		{
			QCOMPARE(fields[headers.indexOf(QStringLiteral("Precompute Category"))], QStringLiteral("Rendered Effects"));
			QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect"))], QLatin1Char('\'') + f.effect);
			QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect Category"))], QLatin1Char('\'') + f.effectCategory);
			QVERIFY(!headers.contains(QStringLiteral("Effect Sequence")));
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
		f.effect = f.effectCategory = QStringLiteral("stale detail");
		const auto fields = readCsvRecord(MediaCsv::rowLine(f, options));
		for (const auto &header : {QStringLiteral("Precompute Category"), QStringLiteral("Effect"), QStringLiteral("Effect Category")})
			QVERIFY(fields[headers.indexOf(header)].isEmpty());
	}
}

void TestMediaCsv::precompute_categories_preserve_unknown_details()
{
	const MediaCsv::Options options{true};
	const auto headers = readCsvRecord(MediaCsv::headerLine(options));
	const QStringList expected{QStringLiteral("Rendered Effects"), QStringLiteral("Titles and Matte Keys"), QString{}};
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
