#include "operationplan.h"
#include "opfile.h"
#include "conventions.h"
#include <QFileInfo>
#include <QStorageInfo>
#include <limits>

namespace OperationPlan
{
QString destinationPath(const QString &name, const QString &folder, const QString &root,
	bool preserve, bool omfEra)
{
	if (preserve && omfEra)
		return Conventions::omfRootUnder(root) + '/' + name;
	if (preserve)
		return Conventions::mxfRootUnder(root) + '/' + folder + '/' + name;
	return root + '/' + name;
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
	const QStorageInfo a(source), b(parent);
	return a.isValid() && a.isReady() && b.isValid() && b.isReady() && !a.device().isEmpty() &&
		a.device() == b.device();
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
			item.name, item.folder, request.destRoot, request.preserve, item.omfEra);
		if (sameFile(item.src, destination))
			continue;
		if (item.bytes > 0)
		{
			const qint64 maximum = (std::numeric_limits<qint64>::max)();
			requiredBytes = item.bytes > maximum - requiredBytes ? maximum : requiredBytes + item.bytes;
		}
		if (request.kind == OpKind::Move && !canRelocate(item.src, destination))
			out.copyThenRemove = true;
	}
	if (request.kind == OpKind::Copy || out.copyThenRemove)
		out.temporaryBytes = requiredBytes;
	return out;
}
}
