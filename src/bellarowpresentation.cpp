#include "bellarowpresentation.h"

#include "bella/audioformat_p.h"
#include "bella/bellascanner.h"
#include "bellaoperationhandoff.h"
#include "mediafile.h"

#include <algorithm>
#include <type_traits>

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
	row.compression = metadata.compression.compression;
	row.effect = metadata.effects.effect;
	row.effectCategory = metadata.effects.category;
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
