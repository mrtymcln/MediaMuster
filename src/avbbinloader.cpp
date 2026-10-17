// Resolves a bin's file references for filtering. Parsed objects and detailed
// resolution records stay local to the load; the dialog receives compact values.
#include "avbbinloader.h"
#include "mediaengine/avbreader.h"
#include "mediaengine/avbreferences.h"
#include "diagnostics.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <string_view>

namespace
{
	using namespace std::string_view_literals;
	constexpr auto littleHeader = "\x06\x00"
								  "DomainDJBO\x07\x00"
								  "AObjDoc"sv;
	constexpr auto bigHeader = "\x00\x06"
							   "DomainOBJD\x00\x07"
							   "AObjDoc"sv;
	using Object = MediaEngine::AvidObject;
}

AvbHeaderResult AvbBinLoader::inspectHeader(const QString &path)
{
	const QFileInfo info(path);
	if (!info.exists())
		return {false, QStringLiteral("This bin file no longer exists.")};
	if (!info.isFile())
		return {false, QStringLiteral("This path is not a regular file.")};
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
		return {false, file.errorString()};
	std::array<char, littleHeader.size()> headerBytes{};
	if (file.read(headerBytes.data(), headerBytes.size()) != qint64(headerBytes.size()))
		return {false, file.error() == QFileDevice::NoError ? QStringLiteral("This file is not an Avid bin.") : file.errorString()};
	const std::string_view signature(headerBytes.data(), headerBytes.size());
	return signature == littleHeader || signature == bigHeader ? AvbHeaderResult{true, {}}
															   : AvbHeaderResult{false, QStringLiteral("This file is not an Avid bin.")};
}

AvbBin AvbBinLoader::load(const QString &path, const std::atomic_bool *cancelled)
{
	AvbBin bin;
	bin.filePath = path;
	bin.displayName = QFileInfo(path).completeBaseName();
	MediaEngine::Cancellation cancel(cancelled);
	if (cancel.cancelled())
	{
		bin.error = QStringLiteral("Bin reading cancelled.");
		return bin;
	}
	if (!QFileInfo(path).isFile())
	{
		bin.error = QStringLiteral("AVB path is not a regular file.");
		return bin;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
	{
		bin.error = file.errorString();
		return bin;
	}
	const auto originalSize = file.size();
	const auto originalModified = file.fileTime(QFileDevice::FileModificationTime);
	auto snapshot = QSharedPointer<SourceSnapshot>::create();
	snapshot->path = path;
	snapshot->source = MetadataSource::Avb;
	const auto source = QSharedPointer<MediaEngine::ParsedSource>::create(MediaEngine::AvbReader{}.read(file, {snapshot, cancel}));
	using Outcome = MediaEngine::ParsedSource::Outcome;
	MediaEngine::ObjectHandle root = 0;
	for (const auto &property : source->unownedProperties)
		if (property.locator.name == QLatin1String("Header.root_index") && property.state == PropertyReadState::Present)
			root = property.decoded.toULongLong();
	const bool readable = std::any_of(source->objects.cbegin(), source->objects.cend(), [root](const auto &binObject)
									  { return root && binObject.handle == root && binObject.avb && (binObject.avb->classId == "ABIN" || binObject.avb->classId == "BINF") &&
											   std::any_of(binObject.properties.cbegin(), binObject.properties.cend(), [](const auto &property)
														   { return property.locator.name == QLatin1String("Bin.version") && property.state == PropertyReadState::Present; }); });
	const auto changed = [&]
	{ return file.size() != originalSize || file.fileTime(QFileDevice::FileModificationTime) != originalModified; };
	if (!readable || cancel.cancelled() || changed())
	{
		bin.error = cancel.cancelled() ? QStringLiteral("Bin reading cancelled.") : changed() ? QStringLiteral("AVB file changed while reading; load it again.")
																							  : source->diagnostics.join(QStringLiteral("; "));
		if (bin.error.isEmpty())
			bin.error = QStringLiteral("The bin document could not be read.");
		qCWarning(lcAvb) << "cannot parse" << path << bin.error;
		return bin;
	}
	bin.warnings = source->diagnostics;
	const MediaEngine::AvbReferenceIndex index(source, cancel);
	const auto resolution = index.resolveReferences(cancel);
	for (const auto &issue : resolution.issues)
		bin.warnings.append(QStringLiteral("Object %1: %2").arg(issue.objectHandle).arg(issue.explanation));
	QHash<MediaEngine::ObjectHandle, const Object *> objects;
	for (const auto &object : source->objects)
		objects.insert(object.handle, &object);
	for (const auto &relation : source->relationships)
	{
		if (cancel.cancelled())
			break;
		if (!relation.target)
			continue;
		const auto *target = objects.value(relation.target);
		const auto &name = relation.locator.name;
		const QByteArray expected = name.startsWith(QLatin1String("Bin.items[")) && name.endsWith(QLatin1String("].mob")) ? QByteArrayLiteral("CMPO")
									: name == QLatin1String("Component.attributes")										  ? QByteArrayLiteral("ATTR")
																														  : QByteArray{};
		if (!expected.isEmpty() && target && (!target->avb || target->avb->classId != expected))
		{
			bin.warnings.append(QStringLiteral("AVB %1 refers to object %2 with an incompatible class; the expected reference class was not established.").arg(name).arg(relation.target));
		}
	}
	for (const auto &reference : resolution.mediaReferences)
	{
		if (cancel.cancelled())
			break;
		if (reference.fileMobId.size() == 32)
			bin.mediaFileIds.add(AvbFileId::fromMobId(MobId::format(reference.fileMobId)));
		else if (reference.legacyId.size() == 8)
			bin.mediaFileIds.add(AvbFileId::fromLegacyWords(qFromLittleEndian<quint32>(reference.legacyId.constData()), qFromLittleEndian<quint32>(reference.legacyId.constData() + 4)));
	}
	if (cancel.cancelled() || resolution.cancelled || !bin.error.isEmpty() || changed())
	{
		bin.mediaFileIds = {};
		if (bin.error.isEmpty())
			bin.error = changed() ? QStringLiteral("AVB file changed while reading; load it again.") : QStringLiteral("Bin reading cancelled.");
		return bin;
	}
	bin.warnings.removeDuplicates();
	bin.usable = true;
	bin.coverageComplete = source->outcome == Outcome::Complete && resolution.coverageComplete && bin.warnings.isEmpty();
	return bin;
}
