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
		QString line = QStringLiteral("Clip Name,Project,Bin,Kind,Duration,");
		if (options.includeClipDuration)
			line += QStringLiteral("Clip Duration,");
		line += QStringLiteral("Size (MB),Codec,Resolution,Frame Rate,Sample Rate,Bit Depth,Type,");
		if (options.includePrecomputeDetails)
			line += QStringLiteral("Precompute Category,Effect Category,Effect,Effect Sequence,");
		line += QStringLiteral("Date Created,Filename,Source Filename,Location,Database Status,MobId,MasterMobId");
		return line + QLatin1Char('\n');
	}

	QString rowLine(const MediaFile &f, Options options)
	{
		QString line;
		QTextStream out(&line);
		out << CsvUtil::quoted(f.clipName) << ',' << CsvUtil::quoted(f.projectDisplay()) << ','
			<< CsvUtil::quoted(f.originalBin) << ',' << CsvUtil::quoted(f.kindDisplay()) << ','
			<< CsvUtil::quoted(f.durationDisplay()) << ',';
		if (options.includeClipDuration)
			out << CsvUtil::quoted(f.clipDurationDisplay()) << ',';
		out << f.sizeMBDisplay() << ',' << CsvUtil::quoted(f.codec) << ',' << CsvUtil::quoted(f.resolution) << ','
			<< CsvUtil::quoted(f.frameRate) << ',' << CsvUtil::quoted(f.sampleRateDisplay()) << ','
			<< CsvUtil::quoted(f.bitDepth) << ',' << CsvUtil::quoted(f.typeDisplay()) << ',';
		if (options.includePrecomputeDetails)
		{
			const bool precompute = f.type == MediaFile::Type::Precompute;
			out << CsvUtil::quoted(f.precomputeCategoryDisplay()) << ','
				<< CsvUtil::quoted(f.effectCategoryDisplay()) << ','
				<< CsvUtil::quoted(f.effectDisplay()) << ','
				<< CsvUtil::quoted(precompute ? f.effectSequence : QString()) << ',';
		}
		out << f.createdDisplay() << ','
			<< CsvUtil::quoted(f.fileName) << ',' << CsvUtil::quoted(f.sourceFileName) << ','
			<< CsvUtil::quoted(f.mediaFilePath) << ',' << CsvUtil::quoted(f.dbStatusText().label) << ','
			<< CsvUtil::quoted(f.fileMobId) << ',' << CsvUtil::quoted(f.masterMobId);
		out << '\n';
		return line;
	}

	bool write(const QString &path, const QVector<MediaFile> &rows, Options options)
	{
		QSaveFile file(path);
		// Binary, deliberately: headerLine()/rowLine() already end every
		// line with '\n', and QIODevice::Text would rewrite those to CRLF
		// on Windows — including the newlines inside quoted effect fields.
		// Without it the export is byte-identical on every platform.
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
