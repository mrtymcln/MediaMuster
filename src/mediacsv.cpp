#include "mediacsv.h"

#include <QFile>
#include <QLatin1Char>
#include <QTextStream>

// MARK: - Field escaping
//
// Folded in from csvutil.h (2026-08-31): this exporter is the one
// producer of CSV in the app, so the escaping rules live beside the
// columns they protect. Covered by tst_mediacsv through rowLine().
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
	QString headerLine()
	{
		return QStringLiteral("Clip Name,Filename,Project,Bin,Kind,Codec,Resolution,FPS,"
							  "Duration,Source File,Source Path,Source Container,"
							  "Imported,Size (MB),Volume,Location,MOB ID,Master MOB,"
							  "Database Status,Type,Effect,Effect Category,Effect Sequence,"
							  "Date Created,Date Modified\n");
	}

	QString rowLine(const MediaFile &f)
	{
		QString line;
		QTextStream out(&line);
		out << CsvUtil::quoted(f.clipName) << ',' << CsvUtil::quoted(f.fileName) << ','
			<< CsvUtil::quoted(f.projectDisplay()) << ',' << CsvUtil::quoted(f.originalBin) << ','
			<< CsvUtil::quoted(f.kindDisplay()) << ','
			<< CsvUtil::quoted(f.codec) << ',' << CsvUtil::quoted(f.resolution) << ','
			<< CsvUtil::quoted(f.fps) << ',' << CsvUtil::quoted(f.durationDisplay()) << ','
			<< CsvUtil::quoted(f.sourceFileName) << ','
			<< CsvUtil::quoted(f.sourceFilePath) << ',' << CsvUtil::quoted(f.sourceContainer) << ','
			<< (f.isImported ? "yes" : "no") << ',' << f.sizeMBDisplay() << ','
			<< CsvUtil::quoted(f.volumeName) << ',' << CsvUtil::quoted(f.filePath) << ','
			<< CsvUtil::quoted(f.mobId) << ','
			<< CsvUtil::quoted(f.masterMobId) << ','
			<< CsvUtil::quoted(f.dbStatusText().label) << ','
			<< CsvUtil::quoted(f.typeDisplay()) << ','
			<< CsvUtil::quoted(f.effect) << ',' << CsvUtil::quoted(f.effectCategory) << ','
			<< CsvUtil::quoted(f.effectSequence) << ','
			<< f.createdDisplay() << ',' << f.modifiedDisplay()
			<< '\n';
		return line;
	}

	bool write(const QString &path, const QVector<MediaFile> &rows)
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
			return false;
		QTextStream out(&file);
		out.setGenerateByteOrderMark(true);

		out << headerLine();
		for (const MediaFile &f : rows)
			out << rowLine(f);
		return out.status() == QTextStream::Ok;
	}
} // namespace MediaCsv