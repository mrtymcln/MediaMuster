#include "mediametadata.h"

QString MediaMetadataUtil::sourceFileBaseName(const QString &path)
{
	const auto slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
	return slash < 0 ? path : path.mid(slash + 1);
}
