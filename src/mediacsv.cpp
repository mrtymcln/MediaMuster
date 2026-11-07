#include "mediacsv.h"

#include <QSaveFile>
#include <QLatin1Char>
#include <QTextStream>

// MARK: - Field escaping

namespace CsvUtil
{
	/// Doubles every literal `"` inside the field to escape it.
	QString escape(QString field)
	{
		field.replace(QLatin1Char('"'), QStringLiteral("\"\""));
		return field;
	}

	/// Neutralise spreadsheet formula injection. Prefixing a single
	/// quote forces it to be read as text.
	QString neutralise(const QString &field)
	{
		if (field.isEmpty())
			return field;
		const QChar c = field.front();
		if (c == QLatin1Char('=') || c == QLatin1Char('+') || c == QLatin1Char('-') ||
			c == QLatin1Char('@') || c == QLatin1Char('\t') || c == QLatin1Char('\r'))
			return QLatin1Char('\'') + field;
		return field;
	}

	/// Wraps the field in quotes after neutralising any formula lead-in and
	/// escaping internal quotes. Use this for every string column.
	QString quoted(const QString &field)
	{
		return QLatin1Char('"') + escape(neutralise(field)) + QLatin1Char('"');
	}
} // namespace CsvUtil

namespace MediaCsv
{
	QString headerLine(Options options)
	{
		const bool legacy = options.presentation == MediaPresentation::OldScanner;
		QString line = QStringLiteral("Clip Name,Project,Bin,Kind,Duration,");
		line += QStringLiteral("Size (MB),Compression,Resolution,Frame Rate,Sample Rate,Bit Depth,Type,");
		if (options.includePrecomputeDetails)
		{
			line += QStringLiteral("Precompute Category,Effect Category,Effect,");
			if (legacy)
				line += QStringLiteral("Effect Sequence,");
		}
		line += QStringLiteral("Date Created,Filename,");
		line += legacy ? QStringLiteral("Source Path,Location,") : QStringLiteral("File Path,");
		line += QStringLiteral("No Database,No Reference,FileMobId,MasterMobId,KelpieId,OmfScan");
		return line + QLatin1Char('\n');
	}

	QString rowLine(const MediaFile &f, Options options)
	{
		const auto presentation = options.presentation;
		const bool legacy = presentation == MediaPresentation::OldScanner;
		QString line;
		QTextStream out(&line);
		out << CsvUtil::quoted(f.clipName) << ',' << CsvUtil::quoted(f.projectDisplay()) << ','
			<< CsvUtil::quoted(f.originalBin) << ',' << CsvUtil::quoted(f.kindDisplay(presentation)) << ','
			<< CsvUtil::quoted(f.durationDisplay()) << ',';
		out << f.sizeMBDisplay(presentation) << ',' << CsvUtil::quoted(f.compression) << ',' << CsvUtil::quoted(f.resolutionDisplay()) << ','
			<< CsvUtil::quoted(f.frameRateDisplay()) << ',' << CsvUtil::quoted(f.sampleRateDisplay()) << ','
			<< CsvUtil::quoted(f.bitDepthDisplay()) << ',' << CsvUtil::quoted(f.typeDisplay(presentation)) << ',';
		if (options.includePrecomputeDetails)
		{
			out << CsvUtil::quoted(f.precomputeCategoryDisplay(presentation)) << ','
				<< CsvUtil::quoted(f.effectCategoryDisplay()) << ','
				<< CsvUtil::quoted(f.effectDisplay()) << ',';
			if (legacy)
				out << CsvUtil::quoted(f.type == MediaFile::Type::Precompute ? f.effectSequence : QString()) << ',';
		}
		out << f.createdDisplay() << ',' << CsvUtil::quoted(f.fileName) << ',';
		if (legacy)
			out << CsvUtil::quoted(f.sourceFilePath) << ',';
		out << CsvUtil::quoted(f.mediaFilePath) << ','
			<< (f.isNoDatabase() ? QStringLiteral("true") : QStringLiteral("false")) << ','
			<< (f.isNoReference() ? QStringLiteral("true") : QStringLiteral("false")) << ','
			<< CsvUtil::quoted(f.fileMobId) << ',' << CsvUtil::quoted(f.masterMobId) << ',' << f.kelpieId << ','
			<< (f.omfEra ? QStringLiteral("true") : QStringLiteral("false"));
		out << '\n';
		return line;
	}

	bool write(const QString &path, const QVector<MediaFile> &rows, Options options)
	{
		QSaveFile file(path);
		// Binary mode preserves LF endings, including newlines inside quoted fields on Windows.
		if (!file.open(QIODevice::WriteOnly))
			return false;
		QTextStream out(&file);
		out.setGenerateByteOrderMark(true);

		out << headerLine(options);
		for (const MediaFile &f : rows)
			out << rowLine(f, options);
		out.flush();
		return out.status() == QTextStream::Ok && file.commit();
	}
} // namespace MediaCsv
