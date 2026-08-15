#include "icons.h"
#include "mediafile.h"
#include "volumemanager.h"

#include <QApplication>
#include <QFileInfo>
#include <QStorageInfo>
#include <QStyle>

namespace Icons
{

	QIcon forVolumeType(const QString &volumeType, const QString &path)
	{
		auto *style = QApplication::style();

		// Manually-added volumes carry no type. Ask the same detector the
		// volume list uses, so a hand-added SMB share and the same share
		// auto-detected can't get different icons (this used to keep its own
		// filesystem list here, and the two had already drifted apart).
		QString type = volumeType;
		if (type.isEmpty() && !path.isEmpty())
			type = VolumeManager::detectVolumeType(QFileInfo(path).fileName(), path,
												   QStorageInfo(path));
		const QString t = type.toLower();

		// Nexis and Network both take the network-drive icon.
		if (t.contains("network") || t.contains("nexis"))
			return style->standardIcon(QStyle::SP_DriveNetIcon);

		// Internal (and anything unrecognised): the internal hard-drive icon.
		return style->standardIcon(QStyle::SP_DriveHDIcon);
	}

	QIcon forProject(const QString &projectName)
	{
		auto *style = QApplication::style();

		// 'No reference' is the headline cleanup category — warning icon.
		if (projectName.compare(ProjectLabel::noReference(), Qt::CaseInsensitive) == 0)
			return style->standardIcon(QStyle::SP_MessageBoxWarning);

		// 'No database' means status unknown, not bad — question icon.
		if (projectName.compare(ProjectLabel::noDatabase(), Qt::CaseInsensitive) == 0)
			return style->standardIcon(QStyle::SP_MessageBoxQuestion);

		return style->standardIcon(QStyle::SP_DirIcon);
	}

} // namespace Icons