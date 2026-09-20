#pragma once

#include "conventions.h"

#include <QDir>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <optional>

// Shared, lexical rules for managed media placement. These functions never
// inspect the filesystem; callers resolve aliases and check accessibility.
namespace AvidMediaLayout
{
	enum class Family
	{
		Mxf,
		Omf
	};

	struct MxfFolderName
	{
		QString prefix; ///< Workstation name, without the separating dot.
		QString digits; ///< Original positive ASCII number, including any padding.
	};

	struct Location
	{
		Family family;
		QString rootPath;
		QString folderName;
		bool isQuarantined = false;
	};

	namespace Detail
	{
		inline bool isLeafName(QStringView name)
		{
			return !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..") &&
				   !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) &&
				   !name.contains(QLatin1Char('\0'));
		}

		inline QString cleanAbsolutePath(const QString &path)
		{
			if (path.isEmpty() || path.contains(QLatin1Char('\0')) || !QDir::isAbsolutePath(path))
				return {};
			return QDir::cleanPath(QDir::fromNativeSeparators(path));
		}

		inline bool hasUmeAncestor(const QStringList &parts)
		{
			for (qsizetype i = 1; i < parts.size(); ++i)
				if (parts.at(i - 1).compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0 &&
					parts.at(i).compare(QLatin1String("UME"), Qt::CaseInsensitive) == 0)
					return true;
			return false;
		}
	} // namespace Detail

	[[nodiscard]] inline bool isInsideUmeRoot(const QString &path)
	{
		const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
		return Detail::hasUmeAncestor(clean.split(QLatin1Char('/'), Qt::SkipEmptyParts));
	}

	[[nodiscard]] inline bool isOmfWorkstationFolderName(QStringView name)
	{
		return Detail::isLeafName(name) && !Conventions::isDotHidden(name) &&
			   !Conventions::isCreatingFolderName(name) &&
			   name.compare(Conventions::kQuarantinedDir, Qt::CaseInsensitive) != 0;
	}

	[[nodiscard]] inline std::optional<MxfFolderName> parseMxfFolderName(QStringView name)
	{
		if (!Detail::isLeafName(name) || Conventions::isDotHidden(name))
			return std::nullopt;
		const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
		const QStringView prefix = dot < 0 ? QStringView{} : name.first(dot);
		const QStringView digits = dot < 0 ? name : name.sliced(dot + 1);
		if ((dot >= 0 && prefix.isEmpty()) || digits.isEmpty())
			return std::nullopt;
		bool positive = false;
		for (const QChar digit : digits)
		{
			if (digit < QLatin1Char('0') || digit > QLatin1Char('9'))
				return std::nullopt;
			positive |= digit != QLatin1Char('0');
		}
		if (!positive)
			return std::nullopt;
		return MxfFolderName{prefix.toString(), digits.toString()};
	}

	[[nodiscard]] inline bool isMxfRoot(const QString &path)
	{
		const QStringList parts = Detail::cleanAbsolutePath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
		// An OMF tree cannot contain another managed media family.
		for (const QString &part : parts)
			if (Conventions::isOmfRootName(part))
				return false;
		return parts.size() >= 2 && !Detail::hasUmeAncestor(parts) &&
			   Conventions::isMxfRootName(parts.last()) &&
			   parts.at(parts.size() - 2).compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0;
	}

	[[nodiscard]] inline bool isOmfRoot(const QString &path)
	{
		const QStringList parts = Detail::cleanAbsolutePath(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
		if (parts.isEmpty() || !Conventions::isOmfRootName(parts.last()))
			return false;
		// OMFI MediaFiles is a separate tree, never inside Avid MediaFiles.
		for (const QString &part : parts)
			if (part.compare(Conventions::kAvidMediaFilesDir, Qt::CaseInsensitive) == 0)
				return false;
		return true;
	}

	[[nodiscard]] inline bool acceptsFileName(Family family, QStringView name)
	{
		return Detail::isLeafName(name) && !Conventions::isDotHidden(name) &&
			   (family == Family::Mxf ? Conventions::hasMxfExtension(name) : Conventions::hasOmfEraExtension(name));
	}

	[[nodiscard]] inline std::optional<Location> locateMediaFolder(const QString &path)
	{
		const QString clean = Detail::cleanAbsolutePath(path);
		if (clean.isEmpty())
			return std::nullopt;
		const QString name = clean.section(QLatin1Char('/'), -1);
		if (isOmfRoot(clean))
			return Location{Family::Omf, clean, name};
		const QString parent = clean.left(clean.lastIndexOf(QLatin1Char('/')));
		if (isMxfRoot(parent))
		{
			const bool quarantined = name.compare(Conventions::kQuarantinedDir, Qt::CaseInsensitive) == 0;
			if (quarantined || parseMxfFolderName(name))
				return Location{Family::Mxf, parent, name, quarantined};
		}
		if (isOmfRoot(parent) && isOmfWorkstationFolderName(name))
			return Location{Family::Omf, parent, name};
		return std::nullopt;
	}
} // namespace AvidMediaLayout
