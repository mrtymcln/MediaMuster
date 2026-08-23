#include "mediacsv.h"
#include "csvutil.h"

#include <QFile>
#include <QTextStream>

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