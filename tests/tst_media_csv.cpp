#include "media/media_csv.h"
#include "test_media_file_record.h"

#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

// Export headings and row values must keep the same column order.

// MARK: - Test helpers

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

	MediaFileRecord sampleRow()
	{
		MediaFileRecord f;
		f.kind = MediaFileRecord::Kind::Video;
		f.type = MediaFileRecord::Type::Media;
		f.clipName = QStringLiteral("Scene 1 - Take 3");
		f.fileName = QStringLiteral("A11B22C33D44.mxf");
		f.project = QStringLiteral("MyFilm");
		f.originalBin = QStringLiteral("Rushes");
		f.compression = QStringLiteral("DNx HQX [DNxHD 175x]");
		TestMediaFile::setFrameRate(f, 25);
		TestMediaFile::setDuration(f, 250, 25);
		f.bellaValues->resolution = Bella::MatchDimensions{1920, 1080};
		f.sizeBytes = 850'000'000;
		f.created = QDateTime(QDate(2026, 7, 20), QTime(12, 30));
		return f;
	}
} // namespace

// MARK: - Test fixture

class TestMediaCsv : public QObject
{
	Q_OBJECT
private slots:
	void blank_row_has_a_field_for_every_column();
	void selected_master_and_physical_row_identity_are_exported();
	void header_order_and_values_follow_the_export_schema();
	void database_flags_are_unquoted_booleans_data();
	void database_flags_are_unquoted_booleans();
	void unknown_created_date_is_blank();
	void sample_rate_and_bit_depth_are_exported_data();
	void sample_rate_and_bit_depth_are_exported();

	void file_path_round_trips_csv_data();
	void file_path_round_trips_csv();
	void formula_injection_is_neutralised_data();
	void formula_injection_is_neutralised();
	void write_produces_header_plus_one_line_per_row();
	void write_failure_preserves_existing_export();
	void unknown_classification_is_exported_without_guessing();
	void effect_details_are_explicit_and_quoted();
	void non_precompute_effect_fields_stay_blank();
	void precompute_categories_preserve_unknown_details();
};

// MARK: - Export schema and row values

void TestMediaCsv::selected_master_and_physical_row_identity_are_exported()
{
	MediaFileRecord file = sampleRow();
	file.kelpieId = 42;
	file.fileMobId = QStringLiteral("file-id");
	file.masterMobId = QStringLiteral("selected-master");
	file.omfEra = true;
	const auto headers = readCsvRecord(MediaCsv::headerLine());
	const auto line = MediaCsv::rowLine(file);
	const auto values = readCsvRecord(line);
	QCOMPARE(headers.count(QStringLiteral("FileMobId")), 1);
	QCOMPARE(headers.count(QStringLiteral("MasterMobId")), 1);
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("FileMobId"))), QStringLiteral("file-id"));
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("MasterMobId"))), QStringLiteral("selected-master"));
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("KelpieId"))), QStringLiteral("42"));
	QCOMPARE(values.at(headers.indexOf(QStringLiteral("OmfScan"))), QStringLiteral("true"));
	QVERIFY(line.endsWith(QStringLiteral(",true\n")));
}

void TestMediaCsv::blank_row_has_a_field_for_every_column()
{
	for (const bool precomputes : {false, true})
	{
		const MediaCsv::Options options{precomputes};
		QCOMPARE(readCsvRecord(MediaCsv::rowLine(MediaFileRecord{}, options)).size(),
				 readCsvRecord(MediaCsv::headerLine(options)).size());
	}
}

void TestMediaCsv::header_order_and_values_follow_the_export_schema()
{
	MediaFileRecord f = sampleRow();
	TestMediaFile::setSampleRate(f, 48000);
	TestMediaFile::setBitDepth(f, 10);
	f.type = MediaFileRecord::Type::Precompute;
	f.precomputeCategory = MediaFileRecord::PrecomputeCategory::RenderedEffects;
	f.effectCategory = QStringLiteral("Image");
	f.effect = QStringLiteral("Color Correction");
	f.modified = QDateTime(QDate(2026, 9, 5), QTime(10, 15));
	f.volumeName = QStringLiteral("EDIT");
	f.mediaFolderName = QStringLiteral("1");
	f.mediaFilePath = QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/1/A11B22C33D44.mxf");
	f.dbStatus = MediaFileRecord::DbStatus::NoReference;
	f.fileMobId = QStringLiteral("file-mob-id");
	f.masterMobId = QStringLiteral("master-mob-id");
	for (const bool precomputes : {false, true})
	{
		QStringList expectedHeaders = QStringLiteral(
										  "Clip Name,Bin,Project,Duration,Kind,Type,Compression,Resolution,Frame Rate,Sample Rate,Bit Depth")
										  .split(QLatin1Char(','));
		QStringList expectedFields{
			QStringLiteral("Scene 1 - Take 3"), QStringLiteral("Rushes"), QStringLiteral("MyFilm"),
			QStringLiteral("00:00:10:00"), QStringLiteral("Video"), QStringLiteral("Precompute"),
			QStringLiteral("DNx HQX [DNxHD 175x]"), QStringLiteral("1920x1080"), QStringLiteral("25.00"),
			QStringLiteral("48 kHz"), QStringLiteral("10-bit")};
		if (precomputes)
		{
			expectedHeaders.append({QStringLiteral("Precompute Category"), QStringLiteral("Effect Category"), QStringLiteral("Effect")});
			expectedFields.append({QStringLiteral("Rendered Effects"), QStringLiteral("Image"), QStringLiteral("Color Correction")});
		}
		expectedHeaders.append(QStringLiteral(
								   "Size (MB),Date Created,File Path,FileMobId,MasterMobId,KelpieId,No Database,No Reference,OmfScan")
								   .split(QLatin1Char(',')));
		expectedFields.append({QStringLiteral("850.0"), QStringLiteral("2026-07-20 12:30"),
							   QStringLiteral("/Volumes/EDIT/Avid MediaFiles/MXF/1/A11B22C33D44.mxf"),
							   QStringLiteral("file-mob-id"), QStringLiteral("master-mob-id"), QStringLiteral("0"),
							   QStringLiteral("false"), QStringLiteral("true"), QStringLiteral("false")});
		QCOMPARE(readCsvRecord(MediaCsv::headerLine({precomputes})), expectedHeaders);
		QCOMPARE(readCsvRecord(MediaCsv::rowLine(f, {precomputes})), expectedFields);
	}
}

void TestMediaCsv::database_flags_are_unquoted_booleans_data()
{
	QTest::addColumn<MediaFileRecord::DbStatus>("status");
	QTest::addColumn<QString>("noDatabase");
	QTest::addColumn<QString>("noReference");
	QTest::newRow("listed") << MediaFileRecord::DbStatus::Listed << QStringLiteral("false") << QStringLiteral("false");
	QTest::newRow("checked-missing-reference") << MediaFileRecord::DbStatus::NoReference << QStringLiteral("false") << QStringLiteral("true");
	QTest::newRow("no-local-databases") << MediaFileRecord::DbStatus::NoDatabase << QStringLiteral("true") << QStringLiteral("false");
	QTest::newRow("unresolved-local-checks") << MediaFileRecord::DbStatus::DbUnreadable << QStringLiteral("true") << QStringLiteral("false");
}

void TestMediaCsv::database_flags_are_unquoted_booleans()
{
	QFETCH(MediaFileRecord::DbStatus, status);
	QFETCH(QString, noDatabase);
	QFETCH(QString, noReference);
	MediaFileRecord file = sampleRow();
	file.dbStatus = status;
	for (const bool precomputes : {false, true})
	{
		const MediaCsv::Options options{precomputes};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const int databaseColumn = headers.indexOf(QStringLiteral("No Database"));
		const int referenceColumn = headers.indexOf(QStringLiteral("No Reference"));
		// This row has no embedded commas: inspect raw cells to retain the
		// unquoted lowercase boolean format used by OmfScan.
		const auto rawValues = MediaCsv::rowLine(file, options).chopped(1).split(QLatin1Char(','));
		QCOMPARE(rawValues.value(databaseColumn), noDatabase);
		QCOMPARE(rawValues.value(referenceColumn), noReference);
	}
}

void TestMediaCsv::sample_rate_and_bit_depth_are_exported_data()
{
	QTest::addColumn<MediaFileRecord::Kind>("kind");
	QTest::addColumn<int>("sampleRate");
	QTest::addColumn<quint32>("bits");
	QTest::addColumn<QString>("bitDepthLabel");
	QTest::addColumn<QString>("sampleRateLabel");
	QTest::newRow("audio-44.1-khz") << MediaFileRecord::Kind::Audio << 44100 << quint32(16) << QStringLiteral("16-bit") << QStringLiteral("44.1 kHz");
	QTest::newRow("audio-48-khz") << MediaFileRecord::Kind::Audio << 48000 << quint32(24) << QStringLiteral("24-bit") << QStringLiteral("48 kHz");
	QTest::newRow("audio-float") << MediaFileRecord::Kind::Audio << 96000 << quint32(32) << QStringLiteral("32-bit") << QStringLiteral("96 kHz");
	QTest::newRow("video-bit-depth") << MediaFileRecord::Kind::Video << 0 << quint32(10) << QStringLiteral("10-bit") << QString();
	QTest::newRow("unknown") << MediaFileRecord::Kind::Unknown << 0 << quint32(0) << QString() << QString();
}

void TestMediaCsv::sample_rate_and_bit_depth_are_exported()
{
	QFETCH(MediaFileRecord::Kind, kind);
	QFETCH(int, sampleRate);
	QFETCH(quint32, bits);
	QFETCH(QString, bitDepthLabel);
	QFETCH(QString, sampleRateLabel);
	MediaFileRecord f = sampleRow();
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
		QCOMPARE(fields.at(frameRateIndex), f.frameRateDisplay());
		QCOMPARE(fields.at(frameRateIndex + 1), sampleRateLabel);
		QCOMPARE(fields.at(frameRateIndex + 1), f.sampleRateDisplay());
		QCOMPARE(fields.at(frameRateIndex + 2), bitDepthLabel);
	}
}

void TestMediaCsv::file_path_round_trips_csv_data()
{
	QTest::addColumn<QString>("filePath");
	QTest::newRow("posix") << QStringLiteral("/Volumes/Editorial/Avid MediaFiles/MXF/1/interview.mxf");
	QTest::newRow("windows") << QStringLiteral("C:/Avid MediaFiles/MXF/1/interview.mxf");
	QTest::newRow("unicode-and-literal-percent") << QStringLiteral("/Volumes/Café%20Workspace/Avid MediaFiles/MXF/1/interview.mxf");
	QTest::newRow("csv-characters") << QStringLiteral("/Volumes/Day,\"Quoted\"\nCamera/Avid MediaFiles/MXF/1/interview.mxf");
	QTest::newRow("unavailable") << QString{};
}

void TestMediaCsv::file_path_round_trips_csv()
{
	QFETCH(QString, filePath);
	auto file = sampleRow();
	file.mediaFilePath = filePath;
	file.fileName = filePath.section(QLatin1Char('/'), -1);
	for (const bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto fields = readCsvRecord(MediaCsv::rowLine(file, options));
		QCOMPARE(fields.size(), headers.size());
		QCOMPARE(headers.count(QStringLiteral("File Path")), 1);
		QCOMPARE(fields.at(headers.indexOf(QStringLiteral("File Path"))), filePath);
		QVERIFY(!headers.contains(QStringLiteral("Filename")));
	}
}

void TestMediaCsv::unknown_created_date_is_blank()
{
	MediaFileRecord f = sampleRow();
	f.created = QDateTime(); // filesystem records no birth time
	f.modified = QDateTime(QDate(2026, 7, 21), QTime(9, 45));
	const auto headers = readCsvRecord(MediaCsv::headerLine());
	const auto fields = readCsvRecord(MediaCsv::rowLine(f));
	QVERIFY(fields.at(headers.indexOf(QStringLiteral("Date Created"))).isEmpty());
}

// MARK: - CSV protection and atomic writing

void TestMediaCsv::formula_injection_is_neutralised_data()
{
	QTest::addColumn<QString>("clipName");
	QTest::newRow("equals") << QStringLiteral("=SUM(1,2)");
	QTest::newRow("plus") << QStringLiteral("+1+2");
	QTest::newRow("minus") << QStringLiteral("-1+2");
	QTest::newRow("at") << QStringLiteral("@SUM(1,2)");
	QTest::newRow("tab") << QStringLiteral("\t=SUM(1,2)");
	QTest::newRow("carriage-return") << QStringLiteral("\r=SUM(1,2)");
}

void TestMediaCsv::formula_injection_is_neutralised()
{
	QFETCH(QString, clipName);
	MediaFileRecord f = sampleRow();
	f.clipName = clipName;
	const QString line = MediaCsv::rowLine(f);
	QVERIFY(line.startsWith(QStringLiteral("\"'")));
	QCOMPARE(readCsvRecord(line).first(), QLatin1Char('\'') + clipName);
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

// MARK: - Unknown classification and effect details

void TestMediaCsv::unknown_classification_is_exported_without_guessing()
{
	for (bool enabled : {false, true})
	{
		const MediaCsv::Options options{enabled};
		const auto headers = readCsvRecord(MediaCsv::headerLine(options));
		const auto unknown = readCsvRecord(MediaCsv::rowLine(MediaFileRecord{}, options));
		QCOMPARE(unknown.at(headers.indexOf(QStringLiteral("Kind"))), QStringLiteral("unknown"));
		QCOMPARE(unknown.at(headers.indexOf(QStringLiteral("Type"))), QStringLiteral("unknown"));
		const auto known = readCsvRecord(MediaCsv::rowLine(sampleRow(), options));
		QCOMPARE(known.at(headers.indexOf(QStringLiteral("Kind"))), QStringLiteral("Video"));
		QCOMPARE(known.at(headers.indexOf(QStringLiteral("Type"))), QStringLiteral("Media"));
	}
}

void TestMediaCsv::effect_details_are_explicit_and_quoted()
{
	MediaFileRecord f = sampleRow();
	f.type = MediaFileRecord::Type::Precompute;
	f.precomputeCategory = MediaFileRecord::PrecomputeCategory::RenderedEffects;
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
		}
		else
		{
			QVERIFY(!headers.contains(QStringLiteral("Precompute Category")));
			QVERIFY(!headers.contains(QStringLiteral("Effect")));
			QVERIFY(!headers.contains(QStringLiteral("Effect Category")));
			QVERIFY(!MediaCsv::rowLine(f, options).contains(QStringLiteral("Custom")));
		}
	}
}

void TestMediaCsv::non_precompute_effect_fields_stay_blank()
{
	const MediaCsv::Options options{true};
	const auto headers = readCsvRecord(MediaCsv::headerLine(options));
	for (auto type : {MediaFileRecord::Type::Media, MediaFileRecord::Type::Unknown})
	{
		MediaFileRecord f = sampleRow();
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
	for (auto category : {MediaFileRecord::PrecomputeCategory::RenderedEffects,
						  MediaFileRecord::PrecomputeCategory::TitlesAndMatteKeys, MediaFileRecord::PrecomputeCategory::Unknown})
	{
		MediaFileRecord file;
		file.type = MediaFileRecord::Type::Precompute;
		file.precomputeCategory = category;
		const auto fields = readCsvRecord(MediaCsv::rowLine(file, options));
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Type"))], QStringLiteral("Precompute"));
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Precompute Category"))], expected[row++]);
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect Category"))], QStringLiteral("unknown"));
		QCOMPARE(fields[headers.indexOf(QStringLiteral("Effect"))], QStringLiteral("unknown"));
	}
}

QTEST_APPLESS_MAIN(TestMediaCsv)
#include "tst_media_csv.moc"
