#include "bella_media_mapper.h"

#include "scanner/audio_format_decoder_p.h"
#include "scanner/bella_scanner.h"
#include "media/media_file.h"
#include "media/omf_uid.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <type_traits>
#include <utility>

// MARK: - Timing conversion helpers

namespace
{
	std::optional<Bella::TimingRate> positiveRate(Bella::MatchRateWords words)
	{
		if (words.numerator <= 0 || words.denominator <= 0)
			return std::nullopt;
		return Bella::TimingRate{quint64(words.numerator), quint64(words.denominator)};
	}

	std::optional<Bella::TimingRate> videoRate(const std::optional<Bella::MatchFrameRate> &rate)
	{
		if (!rate)
			return std::nullopt;
		if (rate->meaning == Bella::MatchRateMeaning::AvidExactEditRate)
			return Bella::interpretAvidEditRate(rate->words.numerator, rate->words.denominator);
		return positiveRate(rate->words);
	}

	std::optional<Bella::TimingRate> audioRate(const std::optional<Bella::MatchAudioFrequency> &rate)
	{
		if (!rate)
			return std::nullopt;
		return std::visit([](const auto &value) -> std::optional<Bella::TimingRate>
						  {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, Bella::MatchRateWords>)
					return positiveRate(value);
				else if constexpr (std::is_same_v<T, quint32>)
				{
					if (value)
						return Bella::TimingRate{value, 1};
				}
				else if (const auto frequency = Bella::Detail::exactAiffFrequency(value))
					return Bella::TimingRate{frequency->numerator, frequency->denominator};
				return std::nullopt; }, *rate);
	}
}

// MARK: - File identities and operation safety

void applyBellaOperationHandoff(MediaFile &row, const Bella::ScanEntry &entry)
{
	const QFileInfo file(entry.path);
	const auto &metadata = entry.match.metadata;
	row.mediaFilePath = entry.path;
	row.fileName = file.fileName();
	row.mediaFolderName = file.dir().dirName();
	row.omfEra = entry.omfEra;
	row.sizeBytes = entry.sizeBytes.value_or(-1);
	row.modified = entry.modified;
	row.clipName = metadata.clipName;
	row.fileMobId = metadata.fileMobId ? OmfUid::toIdText(metadata.fileMobId->bytes()) : QString{};
	row.masterMobId = metadata.masterMobId ? OmfUid::toIdText(metadata.masterMobId->bytes()) : QString{};
	row.scanStamp = {entry.path, entry.volumeIdentifier, entry.modified, row.fileMobId, row.masterMobId};

	OperationScanReceipt receipt;
	if (const auto *verify = std::get_if<Bella::VerifyFileId>(&entry.operationSafety))
	{
		const auto id = OmfUid::toIdText(verify->id.bytes());
		if (verify->origin == Bella::FileIdOrigin::Database)
			receipt.databaseFileMobIdToVerify = id;
		else
			receipt.headerIdentity.fileMobId = id;
	}
	else if (const auto *reason = std::get_if<Bella::RescanReason>(&entry.operationSafety))
	{
		switch (*reason)
		{
		case Bella::RescanReason::FileChanged:
			receipt.headerIdentity.unavailableReason = QStringLiteral("The media file changed during scanning. Rescan before proceeding.");
			break;
		case Bella::RescanReason::FilePresenceUnconfirmed:
			receipt.headerIdentity.unavailableReason = QStringLiteral("The scan could not confirm that the media file was still present. Rescan before proceeding.");
			break;
		}
	}
	// A physical-only instruction is an explicit empty receipt.
	row.operationScanReceipt = std::move(receipt);
}

// MARK: - Prepared media row

MediaFile bellaMediaFile(const Bella::ScanEntry &entry)
{
	MediaFile row;
	applyBellaOperationHandoff(row, entry);
	row.kelpieId = entry.kelpieId;
	row.created = entry.created;
	row.volumeName = entry.volumeName;
	row.volumePath = entry.volumePath;
	row.isQuarantined = entry.quarantined;
	const auto allowed = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._- ,()[]+='~@#%&");
	row.isNonPortable = std::any_of(row.fileName.cbegin(), row.fileName.cend(),
									[&](QChar c)
									{ return !allowed.contains(c); });

	const auto &metadata = entry.match.metadata;
	row.project = metadata.project;
	row.originalBin = metadata.originalBin;
	row.compression = metadata.compression.compressionName;
	row.effect = metadata.effects.effectDisplayText;
	row.effectCategory = metadata.effects.effectCategoryText;
	switch (metadata.kind)
	{
	case Bella::MatchKind::Unknown:
		break;
	case Bella::MatchKind::Audio:
		row.kind = MediaFile::Kind::Audio;
		break;
	case Bella::MatchKind::Video:
		row.kind = MediaFile::Kind::Video;
		break;
	}
	switch (metadata.type)
	{
	case Bella::MatchType::Unknown:
		break;
	case Bella::MatchType::Media:
		row.type = MediaFile::Type::Media;
		break;
	case Bella::MatchType::Precompute:
		row.type = MediaFile::Type::Precompute;
		break;
	}
	switch (metadata.precomputeCategory)
	{
	case Bella::MatchPrecomputeCategory::Unknown:
		break;
	case Bella::MatchPrecomputeCategory::RenderedEffects:
		row.precomputeCategory = MediaFile::PrecomputeCategory::RenderedEffects;
		break;
	case Bella::MatchPrecomputeCategory::TitlesAndMatteKeys:
		row.precomputeCategory = MediaFile::PrecomputeCategory::TitlesAndMatteKeys;
		break;
	}
	switch (entry.match.localReference)
	{
	case Bella::LocalReference::Unresolved:
		row.dbStatus = MediaFile::DbStatus::DbUnreadable;
		break;
	case Bella::LocalReference::Listed:
		row.dbStatus = MediaFile::DbStatus::Listed;
		break;
	case Bella::LocalReference::NoReference:
		row.dbStatus = MediaFile::DbStatus::NoReference;
		break;
	case Bella::LocalReference::NoDatabase:
		row.dbStatus = MediaFile::DbStatus::NoDatabase;
		break;
	}

	auto &values = row.bellaValues.emplace();
	values.resolution = metadata.resolution;
	values.frameRate = metadata.frameRate;
	values.sampleRate = metadata.sampleRate;
	values.bitDepth = metadata.bitDepth;
	values.timing = metadata.timing;
	values.frameRateValue = videoRate(values.frameRate);
	values.sampleRateValue = audioRate(values.sampleRate);
	if (values.timing.duration && values.timing.display)
	{
		const auto &display = *values.timing.display;
		if (const auto timecode = Bella::durationTimecode(*values.timing.duration, display.displayRate,
														  display.nominalBase, display.mode))
			values.durationTimecode = *timecode;
	}
	return row;
}
