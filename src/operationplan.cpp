#include "operationplan.h"
#include "opfile.h"
#include "conventions.h"
#include <QFileInfo>
#include <QStorageInfo>
#include <limits>

namespace OperationPlan
{
	QString destinationPath(const QString &fileName, const QString &mediaFolderName, const QString &destinationRoot,
							bool preserveAvidStructure, bool omfEra)
	{
		if (preserveAvidStructure && omfEra)
			return Conventions::omfRootUnder(destinationRoot) + '/' + fileName;
		if (preserveAvidStructure)
			return Conventions::mxfRootUnder(destinationRoot) + '/' + mediaFolderName + '/' + fileName;
		return destinationRoot + '/' + fileName;
	}

	std::optional<QString> findKeepBothPath(const QString &path)
	{
		const QFileInfo info(path);
		for (int n = 2; n <= 999; ++n)
		{
			const auto candidate = info.absolutePath() + '/' + info.completeBaseName() +
								   QStringLiteral(" (%1)").arg(n) +
								   (info.suffix().isEmpty() ? QString() : '.' + info.suffix());
			const QFileInfo occupant(candidate);
			if (!occupant.exists() && !occupant.isSymLink())
				return candidate;
		}
		return {};
	}

	bool sameVolumeForRename(const QString &source, const QString &destination)
	{
		QString parent = QFileInfo(destination).absolutePath();
		while (!QFileInfo::exists(parent) && QFileInfo(parent).absolutePath() != parent)
			parent = QFileInfo(parent).absolutePath();
		const QStorageInfo sourceStorage(source), destinationStorage(parent);
		return sourceStorage.isValid() && sourceStorage.isReady() &&
			   destinationStorage.isValid() && destinationStorage.isReady() &&
			   !sourceStorage.device().isEmpty() && sourceStorage.device() == destinationStorage.device();
	}

	bool alreadyAtDestination(const QString &source, const QString &destination)
	{
		if (!OpFile::occupied(destination))
			return false;
		QString error;
		auto file = OpFile::open(source, false, error);
		if (!file)
			return false;
		const auto expected = file->stamp();
		return file->stillAt(source, expected) && file->stillAt(destination, expected);
	}

	CopyMoveAssessment assessCopyMove(const OpRequest &request, const CanRelocate &canRelocate,
									  bool forceCopy, const SameFile &sameFile)
	{
		CopyMoveAssessment out;
		if (request.kind != OpKind::Copy && request.kind != OpKind::Move)
			return out;
		out.copyThenRemove = request.kind == OpKind::Move && forceCopy;
		qint64 requiredBytes = 0;
		for (const auto &item : request.items)
		{
			if (item.policy == "skip")
				continue;
			const auto destination = destinationPath(
				item.name, item.mediaFolderName, request.destRoot, request.preserve, item.omfEra);
			if (sameFile(item.src, destination))
				continue;
			if (item.bytes > 0)
			{
				const qint64 maximum = (std::numeric_limits<qint64>::max)();
				requiredBytes = item.bytes > maximum - requiredBytes ? maximum : requiredBytes + item.bytes;
			}
			if (request.kind == OpKind::Move && !out.copyThenRemove && !canRelocate(item.src, destination))
				out.copyThenRemove = true;
		}
		if (request.kind == OpKind::Copy || out.copyThenRemove)
			out.requiredCopyBytes = requiredBytes;
		return out;
	}
}
