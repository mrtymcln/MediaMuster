#include "matching.h"
#include "audio_format_decoder_p.h"

#include <QFileInfo>
#include <numeric>
#include <type_traits>
#include <utility>

namespace Bella
{
	namespace
	{
		// MARK: - Metadata comparison and selection

		template <typename T>
		bool same(const T &a, const T &b) { return a == b; }
		bool same(const MatchRateWords &a, const MatchRateWords &b)
		{
			return a.numerator == b.numerator && a.denominator == b.denominator;
		}
		bool same(const MatchFrameRate &a, const MatchFrameRate &b)
		{
			return same(a.words, b.words) && a.meaning == b.meaning;
		}
		std::optional<TimingRate> interpretFrameRate(const MatchFrameRate &rate)
		{
			const auto [numerator, denominator] = rate.words;
			if (rate.meaning == MatchRateMeaning::AvidExactEditRate)
				return interpretAvidEditRate(numerator, denominator);
			if (numerator <= 0 || denominator <= 0)
				return std::nullopt;
			return TimingRate{quint64(numerator), quint64(denominator)};
		}
		bool same(const MatchAudioFrequency &a, const MatchAudioFrequency &b)
		{
			if (a.index() != b.index())
				return false;
			return std::visit([&](const auto &value)
							  { return same(value, std::get<std::decay_t<decltype(value)>>(b)); }, a);
		}
		bool same(const MatchDimensions &a, const MatchDimensions &b)
		{
			return a.width == b.width && a.height == b.height;
		}
		bool same(const MatchBitDepth &a, const MatchBitDepth &b)
		{
			return a.bits == b.bits && a.storageBits == b.storageBits && a.representation == b.representation;
		}
		bool same(const TimingRate &a, const TimingRate &b)
		{
			if (!a.denominator || !b.denominator)
				return false;
			const auto x = std::gcd(a.numerator, a.denominator), y = std::gcd(b.numerator, b.denominator);
			return a.numerator / x == b.numerator / y && a.denominator / x == b.denominator / y;
		}
		bool same(const ExactDuration &a, const ExactDuration &b) { return a.count == b.count && same(a.rate, b.rate); }
		bool same(const TimecodeInputs &a, const TimecodeInputs &b)
		{
			return same(a.displayRate, b.displayRate) && a.nominalBase == b.nominalBase && a.mode == b.mode;
		}

		// A disagreement is terminal within a priority level. It must not turn
		// into an ordinary missing value that a lower-priority source can replace.
		template <typename T>
		struct Choice
		{
			std::optional<T> value;
			bool conflict = false;
			void offer(const T &candidate)
			{
				if (value && !same(*value, candidate))
					conflict = true;
				else if (!value)
					value = candidate;
			}
			void offer(const std::optional<T> &candidate)
			{
				if (candidate)
					offer(*candidate);
			}
			std::optional<T> selected() const { return conflict ? std::nullopt : value; }
			std::optional<T> orElse(const Choice &fallback) const
			{
				return conflict || value ? selected() : fallback.selected();
			}
		};

		struct TimingChoice
		{
			Choice<ExactDuration> duration;
			Choice<TimecodeInputs> display;
			bool paired = false;
			void offer(const FileTiming &timing)
			{
				duration.offer(timing.duration);
				display.offer(timing.display);
				paired |= timing.duration.has_value() && timing.display.has_value();
			}
			FileTiming selected() const
			{
				return {duration.selected(), !duration.conflict && (!duration.value || paired) ? display.selected() : std::nullopt};
			}
		};
		struct CompressionChoice
		{
			Choice<QString> compression, newDnx, oldDnx, reallyOldDnx;
			void offer(const CompressionNames &names)
			{
				if (!names.compressionName.isEmpty())
					compression.offer(names.compressionName);
				if (!names.newDnx.isEmpty())
					newDnx.offer(names.newDnx);
				if (!names.oldDnx.isEmpty())
					oldDnx.offer(names.oldDnx);
				if (!names.reallyOldDnx.isEmpty())
					reallyOldDnx.offer(names.reallyOldDnx);
			}
			bool present() const { return compression.value || newDnx.value || oldDnx.value || reallyOldDnx.value; }
			CompressionNames selected() const
			{
				return {compression.selected().value_or(QString{}), newDnx.selected().value_or(QString{}),
						oldDnx.selected().value_or(QString{}), reallyOldDnx.selected().value_or(QString{})};
			}
			CompressionNames orElse(const CompressionChoice &fallback) const
			{
				return present() ? selected() : fallback.selected();
			}
		};

		// MARK: - Candidate metadata

		struct FileChoices
		{
			Choice<QString> project;
			Choice<MatchKind> kind;
			Choice<MatchDimensions> resolution;
			Choice<MatchFrameRate> frameRate;
			Choice<MatchAudioFrequency> sampleRate;
			Choice<MatchBitDepth> bitDepth;
			CompressionChoice compression;
			TimingChoice timing;
		};
		struct ClipChoices
		{
			Choice<QString> name, bin;
			Choice<MatchType> type;
			Choice<MatchPrecomputeCategory> category;
		};

		// MARK: - Metadata conversion

		template <typename Enum>
		MatchKind kindFor(Enum value)
		{
			switch (value)
			{
			case Enum::Audio:
				return MatchKind::Audio;
			case Enum::Video:
				return MatchKind::Video;
			default:
				return MatchKind::Unknown;
			}
		}
		template <typename Enum>
		MatchType typeFor(Enum value)
		{
			switch (value)
			{
			case Enum::Media:
				return MatchType::Media;
			case Enum::Precompute:
				return MatchType::Precompute;
			default:
				return MatchType::Unknown;
			}
		}
		template <typename Enum>
		MatchPrecomputeCategory categoryFor(Enum value)
		{
			switch (value)
			{
			case Enum::RenderedEffects:
				return MatchPrecomputeCategory::RenderedEffects;
			case Enum::TitlesAndMatteKeys:
				return MatchPrecomputeCategory::TitlesAndMatteKeys;
			default:
				return MatchPrecomputeCategory::Unknown;
			}
		}
		template <typename Enum>
		MatchDepthRepresentation depthFor(Enum value)
		{
			switch (value)
			{
			case Enum::Integer:
				return MatchDepthRepresentation::Integer;
			case Enum::HalfFloat:
				return MatchDepthRepresentation::HalfFloat;
			case Enum::Float:
				return MatchDepthRepresentation::Float;
			case Enum::S2_14Fixed:
				return MatchDepthRepresentation::S2_14Fixed;
			case Enum::Fixed10_6:
				return MatchDepthRepresentation::Fixed10_6;
			case Enum::Fixed12_4:
				return MatchDepthRepresentation::Fixed12_4;
			default:
				return MatchDepthRepresentation::Unknown;
			}
		}
		std::optional<quint32> interpretBitDepth(const MatchBitDepth &depth)
		{
			switch (depth.representation)
			{
			case MatchDepthRepresentation::Fixed10_6:
				return 10;
			case MatchDepthRepresentation::Fixed12_4:
				return 12;
			default:
				return depth.bits;
			}
		}

		// MARK: - Candidate collection

		void offerText(Choice<QString> &choice, const QString &text)
		{
			if (!text.isEmpty())
				choice.offer(text);
		}
		template <typename File>
		void offerProject(FileChoices &out, const File &file)
		{
			using Selection = decltype(file.projectSelection);
			if (file.projectSelection == Selection::Conflict)
				out.project.conflict = true;
			else
				offerText(out.project, file.project);
		}
		bool hasNames(const CompressionNames &names)
		{
			return !names.compressionName.isEmpty() || !names.newDnx.isEmpty() || !names.oldDnx.isEmpty() || !names.reallyOldDnx.isEmpty();
		}
		void offerDepth(FileChoices &out, MatchBitDepth depth)
		{
			if (depth.bits || depth.storageBits || depth.representation != MatchDepthRepresentation::Unknown)
				out.bitDepth.offer(depth);
		}
		template <typename File>
		void offerTechnical(FileChoices &out, const File &file)
		{
			const auto kind = kindFor(file.kind);
			if (kind != MatchKind::Unknown)
				out.kind.offer(kind);
			if (file.resolution)
				out.resolution.offer(MatchDimensions{file.resolution->dimensions.width, file.resolution->dimensions.height});
			const auto names = compressionNamesFor(file);
			if (hasNames(names))
				out.compression.offer(names);
			out.timing.offer(timingFor(file));
		}
		template <typename Rate>
		MatchFrameRate frameRateFor(const Rate &rate)
		{
			using Encoding = decltype(rate.encoding);
			return {{rate.words.numerator, rate.words.denominator}, rate.encoding == Encoding::ExactEditRate ? MatchRateMeaning::AvidExactEditRate : MatchRateMeaning::Literal};
		}
		template <typename Frequency>
		MatchAudioFrequency frequencyFor(const Frequency &frequency)
		{
			return std::visit([](const auto &value) -> MatchAudioFrequency
							  {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, quint32> || std::is_same_v<T, std::array<quint8, 10>>) return value;
				else return MatchRateWords{value.numerator, value.denominator}; }, frequency);
		}
		template <typename File>
		void offerLegacyRates(FileChoices &out, const File &file)
		{
			if (kindFor(file.kind) == MatchKind::Video && file.sampleRate)
				out.frameRate.offer(frameRateFor(*file.sampleRate));
			if (kindFor(file.kind) == MatchKind::Audio && file.audioFrequency)
				out.sampleRate.offer(frequencyFor(*file.audioFrequency));
		}
		void offerMdb(FileChoices &out, const MdbFileRecord &file)
		{
			offerProject(out, file);
			if (!file.ownDescriptorQualified)
				return;
			offerTechnical(out, file);
			offerLegacyRates(out, file);
			offerDepth(out, {file.bitDepth.bits, file.bitDepth.storageBits, depthFor(file.bitDepth.representation)});
		}
		template <typename Clip>
		void offerClip(ClipChoices &out, const Clip &clip, bool details)
		{
			offerText(out.name, clip.clipName);
			const auto type = typeFor(clip.type);
			if (type != MatchType::Unknown)
				out.type.offer(type);
			const auto category = categoryFor(clip.precomputeCategory);
			if (details && type == MatchType::Precompute && category != MatchPrecomputeCategory::Unknown)
				out.category.offer(category);
		}
		bool usable(const PmrReport &report)
		{
			return report.reading != PmrReadResult::NotRead && report.reading != PmrReadResult::Changed && report.sizeDateCheck != PmrCheckResult::Changed;
		}
		bool usable(const MdbReport &report)
		{
			return report.reading != MdbReadResult::NotRead && report.reading != MdbReadResult::Changed && report.sizeDateCheck != MdbCheckResult::Changed;
		}
		bool usable(const MxfReport &report)
		{
			return report.reading != MxfReadResult::NotRead && report.reading != MxfReadResult::Changed && report.sizeDateCheck != MxfCheckResult::Changed;
		}
		bool usable(OmfReadResult result) { return result != OmfReadResult::NotRead && result != OmfReadResult::NotPresent; }
		std::optional<TimingRate> frequencyRate(const MatchAudioFrequency &frequency)
		{
			return std::visit([](const auto &value) -> std::optional<TimingRate>
							  {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, std::array<quint8, 10>>)
				{
					const auto rate = Detail::exactAiffFrequency(value);
					if (rate) return TimingRate{rate->numerator, rate->denominator};
				}
				else if constexpr (std::is_same_v<T, quint32>)
				{
					if (value) return TimingRate{value, 1};
				}
				else if (value.numerator > 0 && value.denominator > 0)
					return TimingRate{quint64(value.numerator), quint64(value.denominator)};
				return std::nullopt; }, frequency);
		}
		void offerNative(FileChoices &out, const OmfNativeAudioInformation &native)
		{
			if (native.kind != OmfKind::Unknown)
				out.kind.offer(kindFor(native.kind));
			if (native.samplingRate)
			{
				const MatchAudioFrequency frequency = MatchRateWords{native.samplingRate->numerator, native.samplingRate->denominator};
				if (out.sampleRate.value)
				{
					const auto a = frequencyRate(*out.sampleRate.value), b = frequencyRate(frequency);
					if (!a || !b || !same(*a, *b))
						out.sampleRate.conflict = true;
				}
				else
					out.sampleRate.offer(frequency);
			}
			const auto representation = native.encoding == OmfAudioEncoding::IeeeFloat ? MatchDepthRepresentation::Float
										: native.encoding == OmfAudioEncoding::Pcm	   ? MatchDepthRepresentation::Integer
																					   : MatchDepthRepresentation::Unknown;
			if (native.bitDepth || representation != MatchDepthRepresentation::Unknown)
			{
				if (out.bitDepth.value)
				{
					auto &depth = *out.bitDepth.value;
					if (native.bitDepth && depth.bits && *native.bitDepth != *depth.bits)
						out.bitDepth.conflict = true;
					if (representation != MatchDepthRepresentation::Unknown && depth.representation != MatchDepthRepresentation::Unknown &&
						representation != depth.representation)
						out.bitDepth.conflict = true;
					if (!depth.bits && native.bitDepth)
						depth.bits = *native.bitDepth;
					if (depth.representation == MatchDepthRepresentation::Unknown)
						depth.representation = representation;
				}
				else
					offerDepth(out, {native.bitDepth ? std::optional<quint32>(*native.bitDepth) : std::nullopt, {}, representation});
			}
			const auto names = compressionNamesFor(native);
			if (hasNames(names))
				out.compression.offer(names);
			const auto timing = timingFor(native);
			if (timing.duration)
			{
				out.timing = {};
				out.timing.offer(timing);
			}
		}

		// MARK: - Header metadata

		struct HeaderValues
		{
			bool changed = false;
			std::optional<MobId> fileId, masterId;
			FileChoices file;
			ClipChoices clip;
		};
		HeaderValues headerValues(HeaderInput input, const QString &path, bool details)
		{
			HeaderValues out;
			if (const auto *ref = std::get_if<std::reference_wrapper<const MxfReport>>(&input))
			{
				const auto &report = ref->get();
				if (report.path != path)
					return out;
				out.changed = report.sizeDateCheck == MxfCheckResult::Changed || report.reading == MxfReadResult::Changed;
				if (!usable(report))
					return out;
				const auto &file = report.fileInformation;
				out.fileId = file.fileMobId;
				out.masterId = report.associatedClipInformation.masterMobId;
				offerProject(out.file, file);
				offerTechnical(out.file, file);
				if (file.kind == MxfKind::Video && file.frameRateQualified && file.sampleRate)
					out.file.frameRate.offer(MatchFrameRate{{file.sampleRate->numerator, file.sampleRate->denominator}, MatchRateMeaning::Literal});
				if (file.kind == MxfKind::Audio && file.audioSamplingRate)
					out.file.sampleRate.offer(MatchAudioFrequency(MatchRateWords{file.audioSamplingRate->numerator, file.audioSamplingRate->denominator}));
				offerDepth(out.file, {file.bitDepth.bits, {}, depthFor(file.bitDepth.representation)});
				offerClip(out.clip, report.associatedClipInformation, details);
			}
			else if (const auto *ref = std::get_if<std::reference_wrapper<const OmfReport>>(&input))
			{
				const auto &report = ref->get();
				if (report.path != path)
					return out;
				out.changed = report.sizeDateCheck == OmfCheckResult::Changed;
				if (out.changed)
					return out;
				if (usable(report.omfReading))
				{
					const auto &file = report.fileInformation;
					out.fileId = file.fileMobId;
					out.masterId = report.associatedClipInformation.masterMobId;
					offerProject(out.file, file);
					offerTechnical(out.file, file);
					offerLegacyRates(out.file, file);
					offerDepth(out.file, {file.bitDepth.bits, {}, depthFor(file.bitDepth.representation)});
					offerClip(out.clip, report.associatedClipInformation, details);
					offerText(out.clip.bin, report.associatedClipInformation.originalBin);
				}
				// Native and embedded facts are independent. Prefer the whole native
				// timing answer; never borrow embedded display settings into it.
				if (usable(report.nativeAudioReading))
					offerNative(out.file, report.nativeAudioInformation);
			}
			return out;
		}
	}

	// MARK: - Database indexes

	const QVector<PmrReport> &MatchContext::pmrSources() const noexcept { return m_pmrs; }
	const QVector<MdbReport> &MatchContext::mdbSources() const noexcept { return m_mdbs; }

	MatchContext prepareMatchContext(QVector<PmrReport> pmrs, QVector<MdbReport> mdbs, QSet<QString> completedFolders)
	{
		MatchContext context;
		context.m_pmrs = std::move(pmrs);
		context.m_mdbs = std::move(mdbs);
		context.m_completedFolders = std::move(completedFolders);
		for (qsizetype i = 0; i < context.m_pmrs.size(); ++i)
		{
			const auto &report = context.m_pmrs[i];
			const auto folder = QFileInfo(report.source.path).absolutePath();
			context.m_folders[folder].pmrs.append(i);
			if (usable(report) && report.records.knownSet)
				for (qsizetype j = 0; j < report.records.knownSet->entries.size(); ++j)
					context.m_pmrNames[folder][report.records.knownSet->entries[j].filename].append({i, j});
		}
		for (qsizetype i = 0; i < context.m_mdbs.size(); ++i)
		{
			const auto &report = context.m_mdbs[i];
			context.m_folders[QFileInfo(report.path).absolutePath()].mdbs.append(i);
			if (!usable(report))
				continue;
			for (qsizetype j = 0; j < report.catalogue.files.size(); ++j)
				context.m_files[report.catalogue.files[j].fileMobId].append({i, j});
			for (qsizetype j = 0; j < report.catalogue.masters.size(); ++j)
				context.m_masters[report.catalogue.masters[j].masterMobId].append({i, j});
		}
		return context;
	}

	// MARK: - File matching

	MatchResult matchFile(const MatchContext &context, const QString &path, bool physicalFactsEligible, HeaderInput header, bool details)
	{
		MatchResult result;
		const QFileInfo physical(path);
		const auto folder = physical.absolutePath();
		const auto local = context.m_folders.constFind(folder);
		const bool noDatabase = local == context.m_folders.cend();
		if (noDatabase && context.m_completedFolders.contains(folder))
			result.localReference = LocalReference::NoDatabase;
		const auto ownHeader = headerValues(header, path, details);
		if (!physicalFactsEligible || ownHeader.changed)
			return result;

		Choice<MobId> fileId, explicitMaster;
		Choice<QString> project;
		bool positive = false, negative = false;
		const auto pmrFolder = context.m_pmrNames.constFind(folder);
		if (pmrFolder != context.m_pmrNames.cend())
		{
			const auto entries = pmrFolder->constFind(physical.fileName());
			if (entries != pmrFolder->cend())
				for (const auto &position : *entries)
				{
					positive = true;
					const auto &source = context.m_pmrs[position.source];
					if (!source.records.canUsePmrMetadata)
						continue;
					const auto &entry = source.records.knownSet->entries[position.record];
					fileId.offer(entry.fileMobId);
					explicitMaster.offer(entry.masterMobId);
					offerText(project, entry.project);
				}
		}
		result.metadata.fileMobId = fileId.conflict || fileId.value ? fileId.selected() : ownHeader.fileId;
		// Header values with an incompatible identity cannot enrich a PMR match.
		const bool headerCompatible = !fileId.conflict && (!result.metadata.fileMobId || !ownHeader.fileId || *result.metadata.fileMobId == *ownHeader.fileId);
		FileChoices database;
		Choice<MobId> derivedMaster;
		bool safeDescriptor = false;
		if (result.metadata.fileMobId)
		{
			const auto files = context.m_files.constFind(*result.metadata.fileMobId);
			if (files != context.m_files.cend())
				for (const auto &position : *files)
				{
					const auto &source = context.m_mdbs[position.source];
					const auto &file = source.catalogue.files[position.record];
					if (!noDatabase && local->mdbs.contains(position.source))
						positive = true;
					offerMdb(database, file);
					safeDescriptor |= file.ownDescriptorQualified;
					if (!file.masterLinksComplete)
						derivedMaster.conflict = true;
					for (const auto index : file.directMasterRecords)
					{
						if (index < 0 || index >= source.catalogue.masters.size())
							derivedMaster.conflict = true;
						else
							derivedMaster.offer(source.catalogue.masters[index].masterMobId);
					}
				}
		}
		result.needsHeader = std::holds_alternative<std::monostate>(header) && (!fileId.selected() || !safeDescriptor);

		// Presence and negative coverage are independent of metadata usability.
		if (!noDatabase)
		{
			for (const auto i : local->pmrs)
			{
				const auto &source = context.m_pmrs[i];
				negative |= usable(source) && source.sizeDateCheck == PmrCheckResult::Verified && source.records.referenceCoverageComplete;
			}
			for (const auto i : local->mdbs)
			{
				const auto &source = context.m_mdbs[i];
				if (!usable(source))
					continue;
				negative |= source.sizeDateCheck == MdbCheckResult::Verified && source.catalogue.fileReferencesComplete &&
							(result.metadata.fileMobId.has_value() || source.catalogue.files.isEmpty());
			}
		}
		if (positive)
			result.localReference = LocalReference::Listed;
		else if (negative)
			result.localReference = LocalReference::NoReference;
		if (fileId.conflict)
			return result;

		if (explicitMaster.value || explicitMaster.conflict)
			result.metadata.masterMobId = explicitMaster.selected();
		else if (derivedMaster.value || derivedMaster.conflict)
			result.metadata.masterMobId = derivedMaster.selected();
		else if (headerCompatible)
			result.metadata.masterMobId = ownHeader.masterId;
		ClipChoices clip;
		if (result.metadata.masterMobId)
		{
			const auto masters = context.m_masters.constFind(*result.metadata.masterMobId);
			if (masters != context.m_masters.cend())
				for (const auto &position : *masters)
				{
					const auto &master = context.m_mdbs[position.source].catalogue.masters[position.record];
					offerClip(clip, master, details);
					offerText(clip.bin, master.originalBin);
				}
		}
		const FileChoices emptyFile;
		const auto &fallback = headerCompatible ? ownHeader.file : emptyFile;
		const ClipChoices emptyClip;
		const auto &editorial = headerCompatible && result.metadata.masterMobId && ownHeader.masterId &&
										*result.metadata.masterMobId == *ownHeader.masterId
									? ownHeader.clip
									: emptyClip;
		auto &metadata = result.metadata;
		if (project.conflict || project.value)
			metadata.project = project.selected().value_or(QString{});
		else
			metadata.project = database.project.orElse(fallback.project).value_or(QString{});
		metadata.clipName = clip.name.orElse(editorial.name).value_or(QString{});
		metadata.originalBin = clip.bin.orElse(editorial.bin).value_or(QString{});
		metadata.type = clip.type.orElse(editorial.type).value_or(MatchType::Unknown);
		if (details && metadata.type == MatchType::Precompute)
			metadata.precomputeCategory = clip.category.orElse(editorial.category).value_or(MatchPrecomputeCategory::Unknown);
		metadata.kind = database.kind.orElse(fallback.kind).value_or(MatchKind::Unknown);
		metadata.resolution = database.resolution.orElse(fallback.resolution);
		if (metadata.kind == MatchKind::Video)
		{
			metadata.frameRate = database.frameRate.orElse(fallback.frameRate);
			if (metadata.frameRate)
				metadata.exactFrameRate = interpretFrameRate(*metadata.frameRate);
		}
		if (metadata.kind == MatchKind::Audio)
		{
			metadata.sampleRate = database.sampleRate.orElse(fallback.sampleRate);
			if (metadata.sampleRate)
				metadata.exactSampleRate = frequencyRate(*metadata.sampleRate);
		}
		metadata.bitDepth = database.bitDepth.orElse(fallback.bitDepth).value_or(MatchBitDepth{});
		metadata.interpretedBitDepth = interpretBitDepth(metadata.bitDepth);
		metadata.compression = fallback.compression.orElse(database.compression);
		metadata.timing = safeDescriptor ? database.timing.selected() : fallback.timing.selected();
		if (details)
			metadata.effects = effectNamesFor(metadata.type == MatchType::Precompute, metadata.clipName);
		return result;
	}
}
