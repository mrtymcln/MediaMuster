#pragma once

#include <QDateTime>
#include <QHash>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <optional>

namespace MediaEngine
{
	enum class TextEncoding;
}

using KelpieId = quint64;

class KelpieIdExhausted : public std::runtime_error
{
public:
	KelpieIdExhausted() : std::runtime_error("Scan-session KelpieIds exhausted") {}
};

/// Owned by the scan/session coordinator, never by a folder worker.
class KelpieIdAllocator
{
public:
	KelpieId allocate()
	{
		if (m_next == 0)
			throw KelpieIdExhausted{};
		const KelpieId id = m_next;
		m_next = id == std::numeric_limits<KelpieId>::max() ? 0 : id + 1;
		return id;
	}
	void reserveThrough(KelpieId id)
	{
		if (m_next != 0 && id >= m_next)
			m_next = id == std::numeric_limits<KelpieId>::max() ? 0 : id + 1;
	}
	void reset() { m_next = 1; }

private:
	KelpieId m_next = 1;
};

// What was established about a property's value. Selection, agreement and
// freshness are separate; the read reason explains incomplete/unsupported cases.
enum class PropertyReadState
{
	NotRead,   ///< No interpreted read result established; this does not prove absence.
	Present,   ///< A value was read, including empty text, false or zero; it need not be selected.
	Absent,	   ///< A checked object or established format layout does not supply the property.
	Unreadable ///< The recognized value/source could not be read or decoded; raw bytes may remain.
};
// Compare eligible observations of the same fact, even within one source.
// Disagreement can coexist with a selected value under an explicit preference.
enum class PropertyAgreement
{
	NotCompared,  ///< No usable comparison result established.
	SingleSource, ///< One usable observation; this counts observations, not file/source kinds.
	Agreeing,	  ///< Multiple comparable observations supply equivalent values.
	Conflicting	  ///< Comparable values or qualified associations disagree.
};
// How the value was obtained, independently of whether it is correct or current.
enum class EvidenceBasis
{
	Recorded, ///< The source explicitly supplied the value.
	Derived	  ///< Calculated or inferred from evidence, with an explanation.
};
enum class MetadataSource
{
	Filesystem,
	Pmr,
	Mdb,
	Mxf,
	Omf,
	Avb
};
enum class SourceReadState
{
	NotRead,
	Complete,
	Incomplete,
	Unreadable
};
enum class SourceFreshness
{
	Unknown,
	TimestampConsistent,
	Changed
};

// A missing selected value does not explain what the reader actually checked.
// Keep that explanation separate from value agreement and field applicability.
enum class PropertyReadReason
{
	None,
	NoAssociatedSource,
	SourceNotRead,
	SourceIncomplete,
	SourceUnreadable,
	CoverageNotEstablished,
	NotStoredByFormat,
	NotPresentInObject,
	UnsupportedInterpretation,
	ValueUnreadable
};
enum class PropertyApplicability
{
	Unknown,
	Applicable,
	NotApplicable
};

// Shared application classification, independent of the presentation record.
enum class MediaType : int
{
	Unknown = -1,
	Media = 0,
	Precompute = 1
};

/// Logical properties are addressable even without allocating an empty cell.
/// This catalogue is not a whitelist of properties a reader may preserve.
enum class MediaProperty
{
	ClipName,
	Project,
	OriginalBin,
	Kind,
	FileDuration,
	ClipDuration,
	Size,
	Compression,
	NewDnx,
	OldDnx,
	ReallyOldDnx,
	Resolution,
	FrameRate,
	SampleRate,
	BitDepth,
	SampleFormat,
	Alpha,
	Type,
	PrecomputeCategory,
	EffectCategory,
	Effect,
	EffectSequence,
	Created,
	Filename,
	SourceFilename,
	SourcePath,
	SourceContainer,
	Imported,
	Location,
	Modified,
	VolumeIdentifier,
	FileMobId,
	MasterMobId,
	DatabaseStatus,
	OmfScan,
	Channels,
	CompressionLabel,
	WrappingLabel,
	PixelLayout,
	DropFrame,
	ComponentDepth,
	Count ///< Sentinel for complete policy coverage; not a metadata property.
};

inline QString mediaPropertyName(MediaProperty property)
{
	switch (property)
	{
	case MediaProperty::ClipName:
		return QStringLiteral("Clip Name");
	case MediaProperty::Project:
		return QStringLiteral("Project");
	case MediaProperty::OriginalBin:
		return QStringLiteral("Original Bin");
	case MediaProperty::Kind:
		return QStringLiteral("Kind");
	case MediaProperty::FileDuration:
		return QStringLiteral("File Duration");
	case MediaProperty::ClipDuration:
		return QStringLiteral("Clip Duration");
	case MediaProperty::Size:
		return QStringLiteral("Size");
	case MediaProperty::Compression:
		return QStringLiteral("Compression");
	case MediaProperty::NewDnx:
		return QStringLiteral("NewDnx");
	case MediaProperty::OldDnx:
		return QStringLiteral("OldDnx");
	case MediaProperty::ReallyOldDnx:
		return QStringLiteral("ReallyOldDnx");
	case MediaProperty::Resolution:
		return QStringLiteral("Resolution");
	case MediaProperty::FrameRate:
		return QStringLiteral("Frame Rate");
	case MediaProperty::SampleRate:
		return QStringLiteral("Sample Rate");
	case MediaProperty::BitDepth:
		return QStringLiteral("Bit Depth");
	case MediaProperty::SampleFormat:
		return QStringLiteral("Sample Format");
	case MediaProperty::Alpha:
		return QStringLiteral("Alpha");
	case MediaProperty::Type:
		return QStringLiteral("Type");
	case MediaProperty::PrecomputeCategory:
		return QStringLiteral("Precompute Category");
	case MediaProperty::EffectCategory:
		return QStringLiteral("Effect Category");
	case MediaProperty::Effect:
		return QStringLiteral("Effect");
	case MediaProperty::EffectSequence:
		return QStringLiteral("Effect Sequence");
	case MediaProperty::Created:
		return QStringLiteral("Date Created");
	case MediaProperty::Filename:
		return QStringLiteral("Filename");
	case MediaProperty::SourceFilename:
		return QStringLiteral("Source Filename");
	case MediaProperty::SourcePath:
		return QStringLiteral("Source Path");
	case MediaProperty::SourceContainer:
		return QStringLiteral("Source Container");
	case MediaProperty::Imported:
		return QStringLiteral("Imported");
	case MediaProperty::Location:
		return QStringLiteral("Location");
	case MediaProperty::Modified:
		return QStringLiteral("Date Modified");
	case MediaProperty::VolumeIdentifier:
		return QStringLiteral("Volume Identifier");
	case MediaProperty::FileMobId:
		return QStringLiteral("MobId");
	case MediaProperty::MasterMobId:
		return QStringLiteral("MasterMobId");
	case MediaProperty::DatabaseStatus:
		return QStringLiteral("Database Status");
	case MediaProperty::OmfScan:
		return QStringLiteral("OmfScan");
	case MediaProperty::Channels:
		return QStringLiteral("Channels");
	case MediaProperty::CompressionLabel:
		return QStringLiteral("Compression Label");
	case MediaProperty::WrappingLabel:
		return QStringLiteral("Wrapping Label");
	case MediaProperty::PixelLayout:
		return QStringLiteral("Pixel Layout");
	case MediaProperty::ComponentDepth:
		return QStringLiteral("Component Depth");
	case MediaProperty::DropFrame:
		return QStringLiteral("Drop Frame");
	case MediaProperty::Count:
		break;
	}
	return QStringLiteral("Unknown property");
}

/// Shared immutable receipt: one database snapshot can support many rows.
struct SourceSnapshot
{
	MetadataSource source = MetadataSource::Filesystem;
	QString path;
	QDateTime modified;
	SourceReadState readState = SourceReadState::NotRead;
};
using SourceSnapshotRef = QSharedPointer<const SourceSnapshot>;

struct PropertyReadResult
{
	PropertyReadState state = PropertyReadState::NotRead;
	PropertyReadReason reason = PropertyReadReason::NoAssociatedSource;
	PropertyApplicability applicability = PropertyApplicability::Unknown;
	QString explanation;
};

// One source/object receipt supplies defaults for all logical fields. Only
// explicitly checked exceptions need storage; an empty observation is not proof
// that a format or object omits a property.
struct SourceFieldCoverage
{
	SourceSnapshotRef snapshot;
	QString objectIdentity;
	PropertyReadReason defaultReason = PropertyReadReason::CoverageNotEstablished;
	QHash<MediaProperty, PropertyReadResult> fields;
	bool eligible = true;
	SourceFreshness freshness = SourceFreshness::Unknown;
};

struct MetadataObservation
{
	SourceSnapshotRef snapshot;
	QString property;		///< Actual property or explicitly qualified aggregate locator.
	QString objectIdentity; ///< Owning object; empty if the reader has not established it.
	QVariant value;			///< Interpreted semantic value, with its type retained.
	QVariant rawValue;		///< Original encoding/value when supplied by the reader.
	PropertyReadState readState = PropertyReadState::NotRead;
	PropertyReadReason readReason = PropertyReadReason::None;
	EvidenceBasis basis = EvidenceBasis::Recorded;
	SourceFreshness freshness = SourceFreshness::Unknown;
	QString explanation;
	bool eligible = true; ///< False retains evidence that belongs to a different file.
	std::optional<MediaEngine::TextEncoding> textEncoding;
	std::optional<EvidenceBasis> textEncodingBasis;
};

struct ResolvedField
{
	QVariant value;
	PropertyReadState readState = PropertyReadState::NotRead;
	PropertyAgreement agreement = PropertyAgreement::NotCompared;
	int selectedObservation = -1;
	QString rule; ///< Diagnostic selection label; MediaEngine uses the shared property name.
	QString reason;
	PropertyReadReason readReason = PropertyReadReason::NoAssociatedSource;
	PropertyApplicability applicability = PropertyApplicability::Unknown;
};

/// Sparse, implicitly shared RAM storage. Reading an unknown field never inserts it.
class MediaEvidence
{
public:
	void registerSource(const SourceSnapshotRef &snapshot, const QString &objectIdentity = {},
						PropertyReadReason defaultReason = PropertyReadReason::CoverageNotEstablished)
	{
		if (!snapshot)
			return;
		for (auto &coverage : m_coverage)
		{
			// A scheduled header replaces its unopened receipt. Distinct reads
			// with actual observations remain distinct snapshots.
			if (coverage.objectIdentity.isEmpty() && objectIdentity.isEmpty() && coverage.fields.isEmpty() &&
				coverage.snapshot->readState == SourceReadState::NotRead &&
				snapshot->readState != SourceReadState::NotRead &&
				coverage.snapshot->source == snapshot->source && coverage.snapshot->path == snapshot->path &&
				coverage.snapshot->modified == snapshot->modified)
			{
				const bool hasObservations = std::any_of(m_observations.cbegin(), m_observations.cend(),
														 [&](const QVector<MetadataObservation> &values)
														 { return std::any_of(values.cbegin(), values.cend(), [&](const MetadataObservation &value)
																			  { return value.snapshot == coverage.snapshot; }); });
				if (!hasObservations)
					coverage.snapshot = snapshot;
			}
			if (coverage.snapshot == snapshot && coverage.objectIdentity == objectIdentity)
			{
				if (defaultReason != PropertyReadReason::CoverageNotEstablished)
					coverage.defaultReason = defaultReason;
				return;
			}
		}
		m_coverage.append({snapshot, objectIdentity, defaultReason, {}, true, SourceFreshness::Unknown});
	}
	void recordReadStatus(MediaProperty property, const SourceSnapshotRef &snapshot,
						  const QString &objectIdentity, PropertyReadResult status)
	{
		registerSource(snapshot, objectIdentity);
		for (auto &coverage : m_coverage)
			if (coverage.snapshot == snapshot && coverage.objectIdentity == objectIdentity)
			{
				coverage.fields.insert(property, std::move(status));
				m_resolved.remove(property);
				return;
			}
	}
	const QVector<SourceFieldCoverage> &sourceCoverage() const { return m_coverage; }
	void appendCoverage(const SourceFieldCoverage &source, bool eligible = true)
	{
		registerSource(source.snapshot, source.objectIdentity, source.defaultReason);
		for (auto &coverage : m_coverage)
			if (coverage.snapshot == source.snapshot && coverage.objectIdentity == source.objectIdentity)
			{
				for (auto field = source.fields.cbegin(); field != source.fields.cend(); ++field)
					coverage.fields.insert(field.key(), field.value());
				coverage.eligible = source.eligible && eligible;
				coverage.freshness = source.freshness;
				m_resolved.clear();
				return;
			}
	}
	PropertyReadResult readStatus(MediaProperty property, const SourceSnapshotRef &source = {},
								  const QString &objectIdentity = {}, bool eligibleOnly = false) const
	{
		PropertyReadResult result;
		bool found = false;
		const auto rank = [](PropertyReadState state)
		{
			return state == PropertyReadState::Present ? 3 : state == PropertyReadState::Unreadable ? 2
														 : state == PropertyReadState::NotRead		? 1
																									: 0;
		};
		const auto merge = [&](const PropertyReadResult &next)
		{
			// Unchecked contexts prevent a source-wide absence conclusion. Present
			// evidence still remains present when another context failed or conflicted.
			if (!found || rank(next.state) > rank(result.state))
				result = next;
			found = true;
		};
		for (const auto &coverage : m_coverage)
		{
			if ((source && coverage.snapshot != source) ||
				(!objectIdentity.isEmpty() && coverage.objectIdentity != objectIdentity) ||
				(eligibleOnly && !coverage.eligible))
				continue;
			PropertyReadResult status;
			status.reason = coverage.defaultReason;
			if (coverage.defaultReason == PropertyReadReason::NotStoredByFormat)
			{
				status.state = PropertyReadState::Absent;
				status.explanation = QStringLiteral("This source record layout does not store this field");
			}
			else if (coverage.snapshot->readState == SourceReadState::NotRead)
				status.reason = PropertyReadReason::SourceNotRead;
			else if (coverage.snapshot->readState == SourceReadState::Unreadable)
			{
				status.state = PropertyReadState::Unreadable;
				status.reason = PropertyReadReason::SourceUnreadable;
			}
			else if (coverage.snapshot->readState == SourceReadState::Incomplete)
				status.reason = PropertyReadReason::SourceIncomplete;
			if (const auto explicitStatus = coverage.fields.constFind(property);
				explicitStatus != coverage.fields.cend())
				status = explicitStatus.value();
			bool observed = false;
			PropertyReadResult observedStatus;
			for (const auto &value : observations(property))
				if (value.snapshot == coverage.snapshot && value.objectIdentity == coverage.objectIdentity &&
					(!eligibleOnly || value.eligible))
				{
					PropertyReadResult observationStatus{value.readState,
														 value.readReason != PropertyReadReason::None		? value.readReason
														 : value.readState == PropertyReadState::Unreadable ? PropertyReadReason::ValueUnreadable
														 : value.readState == PropertyReadState::Absent		? PropertyReadReason::NotPresentInObject
														 : value.readState == PropertyReadState::NotRead	? PropertyReadReason::CoverageNotEstablished
																											: PropertyReadReason::None,
														 PropertyApplicability::Applicable, value.explanation};
					if (!observed || rank(value.readState) > rank(observedStatus.state))
						observedStatus = std::move(observationStatus);
					observed = true;
				}
			if (observed && !(status.state == PropertyReadState::Unreadable &&
							  observedStatus.state != PropertyReadState::Present))
				status = std::move(observedStatus);
			merge(status);
		}
		return result;
	}
	void observe(MediaProperty property, MetadataObservation observation)
	{
		registerSource(observation.snapshot, observation.objectIdentity);
		auto &values = m_observations[property];
		for (auto &existing : values)
			if (existing.snapshot == observation.snapshot && existing.property == observation.property &&
				existing.objectIdentity == observation.objectIdentity && existing.value == observation.value &&
				existing.rawValue == observation.rawValue && existing.readState == observation.readState)
			{
				existing.eligible = observation.eligible;
				existing.freshness = observation.freshness;
				existing.basis = observation.basis;
				existing.readReason = observation.readReason;
				existing.textEncoding = observation.textEncoding;
				existing.textEncodingBasis = observation.textEncodingBasis;
				existing.explanation = std::move(observation.explanation);
				m_resolved.remove(property);
				return;
			}
		values.append(std::move(observation));
		m_resolved.remove(property);
	}
	void select(MediaProperty property, ResolvedField field) { m_resolved.insert(property, std::move(field)); }
	ResolvedField selected(MediaProperty property) const
	{
		auto result = m_resolved.value(property);
		if (result.readState != PropertyReadState::Present)
		{
			const auto coverage = readStatus(property, {}, {}, true);
			result.readState = coverage.state;
			result.readReason = coverage.reason;
			result.applicability = coverage.applicability;
		}
		return result;
	}
	void qualifyAll(bool eligible, SourceFreshness freshness)
	{
		for (auto &coverage : m_coverage)
		{
			coverage.eligible = eligible;
			coverage.freshness = freshness;
		}
		for (auto it = m_observations.begin(); it != m_observations.end(); ++it)
			for (auto &observation : it.value())
			{
				observation.eligible = eligible;
				observation.freshness = freshness;
			}
		m_resolved.clear();
	}
	void qualifySource(const SourceSnapshotRef &source, bool eligible, SourceFreshness freshness)
	{
		for (auto &coverage : m_coverage)
			if (coverage.snapshot == source)
			{
				coverage.eligible = eligible;
				coverage.freshness = freshness;
			}
		for (auto it = m_observations.begin(); it != m_observations.end(); ++it)
			for (auto &value : it.value())
				if (value.snapshot == source)
				{
					value.eligible = eligible;
					value.freshness = freshness;
				}
		m_resolved.clear();
	}
	void qualify(MediaProperty property, const SourceSnapshotRef &source, bool eligible, SourceFreshness freshness)
	{
		auto it = m_observations.find(property);
		if (it == m_observations.end())
			return;
		for (auto &observation : it.value())
			if (observation.snapshot == source)
			{
				observation.eligible = eligible;
				observation.freshness = freshness;
			}
		m_resolved.remove(property);
	}
	const QVector<MetadataObservation> &observations(MediaProperty property) const
	{
		const auto it = m_observations.constFind(property);
		static const QVector<MetadataObservation> empty;
		return it == m_observations.cend() ? empty : it.value();
	}
	/// Exclude old file-owned observations after a header establishes a replacement.
	void excludeDatabaseMetadata()
	{
		for (auto &coverage : m_coverage)
			if (coverage.snapshot->source == MetadataSource::Pmr || coverage.snapshot->source == MetadataSource::Mdb)
				coverage.eligible = false;
		for (auto it = m_observations.begin(); it != m_observations.end(); ++it)
			for (auto &observation : it.value())
				if (observation.snapshot && (observation.snapshot->source == MetadataSource::Pmr ||
											 observation.snapshot->source == MetadataSource::Mdb))
					observation.eligible = false;
		m_resolved.clear();
	}
	void excludeSource(MetadataSource source)
	{
		for (auto &coverage : m_coverage)
			if (coverage.snapshot->source == source)
				coverage.eligible = false;
		for (auto it = m_observations.begin(); it != m_observations.end(); ++it)
			for (auto &observation : it.value())
				if (observation.snapshot && observation.snapshot->source == source)
					observation.eligible = false;
		m_resolved.clear();
	}
	void excludeSource(MediaProperty property, MetadataSource source)
	{
		auto it = m_observations.find(property);
		if (it == m_observations.end())
			return;
		for (auto &observation : it.value())
			if (observation.snapshot && observation.snapshot->source == source)
				observation.eligible = false;
		m_resolved.remove(property);
	}
	// Retract a derived interpretation before recomputing it; keep its history.
	void excludeInterpretation(MediaProperty property, const QString &locator)
	{
		auto it = m_observations.find(property);
		if (it == m_observations.end())
			return;
		for (auto &observation : it.value())
			if (observation.basis == EvidenceBasis::Derived && observation.property == locator)
				observation.eligible = false;
		m_resolved.remove(property);
	}
	template <typename Rank>
	ResolvedField resolve(MediaProperty property, Rank rank, const QString &rule) const
	{
		ResolvedField result;
		result.rule = rule;
		const auto &values = observations(property);
		int bestRank = -1;
		int present = 0;
		bool tie = false;
		QVariant first;
		bool disagree = false;
		const auto equalRate = [](const QVariant &left, const QVariant &right)
		{
			const auto a = left.toMap(), b = right.toMap();
			const qint64 an = a.value(QStringLiteral("Numerator")).toInt(), ad = a.value(QStringLiteral("Denominator")).toInt();
			const qint64 bn = b.value(QStringLiteral("Numerator")).toInt(), bd = b.value(QStringLiteral("Denominator")).toInt();
			return an > 0 && ad > 0 && bn > 0 && bd > 0 && an * bd == bn * ad;
		};
		const auto sameTrack = [&](const QVariant &leftTrack, const QVariant &rightTrack)
		{
			const auto x = leftTrack.toMap(), y = rightTrack.toMap();
			const auto xd = x.value(QStringLiteral("Duration")).toMap(), yd = y.value(QStringLiteral("Duration")).toMap();
			const auto xc = xd.value(QStringLiteral("DisplayRate")), yc = yd.value(QStringLiteral("DisplayRate"));
			return x.contains(QStringLiteral("MasterMobId")) && y.contains(QStringLiteral("MasterMobId")) &&
				   x.contains(QStringLiteral("TrackId")) && y.contains(QStringLiteral("TrackId")) &&
				   x.value(QStringLiteral("MasterMobId")) == y.value(QStringLiteral("MasterMobId")) &&
				   x.value(QStringLiteral("TrackId")) == y.value(QStringLiteral("TrackId")) &&
				   x.value(QStringLiteral("DropFrame")) == y.value(QStringLiteral("DropFrame")) &&
				   xd.contains(QStringLiteral("Units")) && yd.contains(QStringLiteral("Units")) &&
				   xd.value(QStringLiteral("Units")) == yd.value(QStringLiteral("Units")) &&
				   equalRate(xd.value(QStringLiteral("Rate")), yd.value(QStringLiteral("Rate"))) &&
				   (xc == yc || equalRate(xc, yc));
		};
		const auto uniqueTracks = [&](const QVariant &value)
		{
			QVariantList result;
			for (const auto &track : value.toList())
				if (std::none_of(result.cbegin(), result.cend(), [&](const QVariant &existing)
								 { return sameTrack(track, existing); }))
					result.append(track);
			return result;
		};
		const auto equivalent = [&](const QVariant &left, const QVariant &right)
		{
			if (left == right)
				return true;
			if (property == MediaProperty::FrameRate || property == MediaProperty::SampleRate)
				return equalRate(left, right);
			if (property == MediaProperty::FileDuration)
			{
				const auto a = left.toMap(), b = right.toMap();
				return a.contains(QStringLiteral("Units")) && b.contains(QStringLiteral("Units")) &&
					   a.value(QStringLiteral("Units")) == b.value(QStringLiteral("Units")) &&
					   equalRate(a.value(QStringLiteral("Rate")), b.value(QStringLiteral("Rate")));
			}
			if (property == MediaProperty::ClipDuration)
			{
				const auto a = uniqueTracks(left), b = uniqueTracks(right);
				// Source-local handles, repeated evidence and enumeration order do
				// not change the set of recorded master-track timing facts.
				return !a.isEmpty() && a.size() == b.size() && std::all_of(a.cbegin(), a.cend(), [&](const QVariant &track)
																		   { return std::any_of(b.cbegin(), b.cend(), [&](const QVariant &other)
																								{ return sameTrack(track, other); }); });
			}
			return false;
		};
		for (qsizetype i = 0; i < values.size(); ++i)
		{
			const auto &observation = values[i];
			if (!observation.eligible || !observation.snapshot)
				continue;
			// An explicitly empty text property remains evidence, but cannot fill
			// a display field or suppress a useful lower-priority observation.
			if (observation.readState == PropertyReadState::Present &&
				observation.value.metaType().id() == QMetaType::QString && observation.value.toString().isEmpty())
				continue;
			if (observation.readState != PropertyReadState::Present)
			{
				if (result.readState == PropertyReadState::NotRead || observation.readState == PropertyReadState::Unreadable)
					result.readState = observation.readState;
				continue;
			}
			if (present++ == 0)
				first = observation.value;
			else if (!equivalent(first, observation.value))
				disagree = true;
			const int priority = rank(observation.snapshot->source);
			// Zero excludes a display candidate, not its read result or evidence.
			if (priority <= 0)
				continue;
			if (priority > bestRank)
			{
				bestRank = priority;
				result.selectedObservation = static_cast<int>(i);
				result.value = observation.value;
				tie = false;
			}
			else if (priority == bestRank && !equivalent(result.value, observation.value))
				tie = true;
		}
		result.agreement = present == 0 ? PropertyAgreement::NotCompared : present == 1 ? PropertyAgreement::SingleSource
																	   : disagree		? PropertyAgreement::Conflicting
																						: PropertyAgreement::Agreeing;
		if (present > 0)
		{
			result.readState = PropertyReadState::Present;
			result.readReason = PropertyReadReason::None;
			result.applicability = PropertyApplicability::Applicable;
		}
		else
		{
			const auto coverage = readStatus(property, {}, {}, true);
			result.readState = coverage.state;
			result.readReason = coverage.reason;
			result.applicability = coverage.applicability;
		}
		if (tie)
		{
			result.value.clear();
			result.selectedObservation = -1;
			result.reason = QStringLiteral("Equally eligible sources disagree; no defensible winner");
		}
		else if (result.selectedObservation >= 0)
		{
			result.reason = QStringLiteral("Selected by the field-specific source priority; alternatives retained");
			if (property == MediaProperty::ClipDuration)
			{
				// Coalesce duplicate facts only in the selected value. The source
				// observations retain every object and its original provenance.
				const auto tracks = uniqueTracks(result.value);
				bool conflictingTrack = false;
				for (qsizetype i = 0; i < tracks.size(); ++i)
					for (qsizetype j = i + 1; j < tracks.size(); ++j)
					{
						const auto a = tracks[i].toMap(), b = tracks[j].toMap();
						conflictingTrack |= a.contains(QStringLiteral("MasterMobId")) && a.contains(QStringLiteral("TrackId")) &&
											a.value(QStringLiteral("MasterMobId")) == b.value(QStringLiteral("MasterMobId")) &&
											a.value(QStringLiteral("TrackId")) == b.value(QStringLiteral("TrackId"));
					}
				if (conflictingTrack)
				{
					result.value.clear();
					result.selectedObservation = -1;
					result.agreement = PropertyAgreement::Conflicting;
					result.reason = QStringLiteral("Selected source records contradictory timing for the same master track; alternatives retained");
				}
				else
					result.value = tracks;
			}
		}
		else if (present > 0)
			result.reason = QStringLiteral("Read values retained; the policy excludes their sources from selection");
		return result;
	}

private:
	QVector<SourceFieldCoverage> m_coverage;
	QHash<MediaProperty, QVector<MetadataObservation>> m_observations;
	QHash<MediaProperty, ResolvedField> m_resolved;
};

/// The agreed five-field scan receipt; neither file-object identity nor byte proof.
struct MediaScanStamp
{
	QString path;
	QString volumeIdentifier;
	QDateTime modified;
	QString mobId;
	QStringList masterMobIds;
};

struct ScanIssue
{
	enum class Kind
	{
		MissingLocalReference,
		UnmatchedDatabaseIdentity,
		MetadataConflict,
		SourceChanged
	};
	Kind kind = Kind::MissingLocalReference;
	SourceSnapshotRef source;
	QString expectedPath;
	QString fileMobId;
	QStringList matchingPaths;
	bool scopeComplete = false;
	QString explanation;
};
Q_DECLARE_METATYPE(ScanIssue)
