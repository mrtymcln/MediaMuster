#pragma once

#include <QDateTime>
#include <QHash>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QtGlobal>
#include <limits>
#include <stdexcept>
#include <utility>

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

enum class PropertyReadState { NotRead, Present, Absent, Unreadable };
enum class PropertyAgreement { NotCompared, SingleSource, Agreeing, Conflicting };
enum class EvidenceBasis { Recorded, Derived };
enum class MetadataSource { Filesystem, Pmr, Mdb, Mxf, Omf, Avb };
enum class SourceReadState { NotRead, Complete, Incomplete, Unreadable };
enum class SourceFreshness { Unknown, TimestampConsistent, Changed };

/// Logical properties are addressable even without allocating an empty cell.
/// This catalogue is not a whitelist of properties a reader may preserve.
enum class MediaProperty
{
	ClipName, Project, OriginalBin, Kind, FileDuration, ClipDuration, Size,
	Codec, NewDnx, OldDnx, ReallyOldDnx, Resolution, FrameRate, SampleRate,
	BitDepth, SampleFormat, Alpha, Type, PrecomputeCategory, EffectCategory,
	Effect, EffectSequence, Created, Filename, SourceFilename, SourcePath,
	SourceContainer, Imported, Location, Modified, VolumeIdentifier,
	FileMobId, MasterMobId, DatabaseStatus, OmfScan, Channels,
	CompressionLabel, WrappingLabel, PixelLayout, ComponentDepth
};

/// Shared immutable receipt: one database snapshot can support many rows.
struct SourceSnapshot
{
	MetadataSource source = MetadataSource::Filesystem;
	QString path;
	QDateTime modified;
	SourceReadState readState = SourceReadState::NotRead;
};
using SourceSnapshotRef = QSharedPointer<const SourceSnapshot>;

struct MetadataObservation
{
	SourceSnapshotRef snapshot;
	QString property;       ///< Actual property or explicitly qualified aggregate locator.
	QString objectIdentity; ///< Owning object; empty if the reader has not established it.
	QVariant value;         ///< Interpreted semantic value, with its type retained.
	QVariant rawValue;      ///< Original encoding/value when supplied by the reader.
	PropertyReadState readState = PropertyReadState::NotRead;
	EvidenceBasis basis = EvidenceBasis::Recorded;
	SourceFreshness freshness = SourceFreshness::Unknown;
	QString explanation;
	bool eligible = true; ///< False retains evidence that belongs to a different file.
};

struct ResolvedField
{
	QVariant value;
	PropertyReadState readState = PropertyReadState::NotRead;
	PropertyAgreement agreement = PropertyAgreement::NotCompared;
	int selectedObservation = -1;
	QString rule;
	quint32 ruleVersion = 1;
	QString reason;
};

/// Sparse, implicitly shared RAM storage. Reading an unknown field never inserts it.
class MediaEvidence
{
public:
	void observe(MediaProperty property, MetadataObservation observation)
	{
		auto &values = m_observations[property];
		for (const auto &existing : values)
			if (existing.snapshot == observation.snapshot && existing.property == observation.property &&
				existing.objectIdentity == observation.objectIdentity && existing.value == observation.value &&
				existing.rawValue == observation.rawValue && existing.readState == observation.readState)
				return;
		values.append(std::move(observation));
		m_resolved.remove(property);
	}
	void select(MediaProperty property, ResolvedField field) { m_resolved.insert(property, std::move(field)); }
	ResolvedField selected(MediaProperty property) const { return m_resolved.value(property); }
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
		for (auto it = m_observations.begin(); it != m_observations.end(); ++it)
			for (auto &observation : it.value())
				if (observation.snapshot && (observation.snapshot->source == MetadataSource::Pmr ||
					observation.snapshot->source == MetadataSource::Mdb))
					observation.eligible = false;
		m_resolved.clear();
	}
	void excludeSource(MetadataSource source)
	{
		for (auto it = m_observations.begin(); it != m_observations.end(); ++it)
			for (auto &observation : it.value())
				if (observation.snapshot && observation.snapshot->source == source)
					observation.eligible = false;
		m_resolved.clear();
	}
	using Rank = int (*)(MetadataSource);
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
		for (qsizetype i = 0; i < values.size(); ++i)
		{
			const auto &observation = values[i];
			if (!observation.eligible || !observation.snapshot)
				continue;
			if (observation.readState != PropertyReadState::Present)
			{
				if (result.readState == PropertyReadState::NotRead || observation.readState == PropertyReadState::Unreadable)
					result.readState = observation.readState;
				continue;
			}
			if (present++ == 0)
				first = observation.value;
			else if (first != observation.value)
				disagree = true;
			const int priority = rank(observation.snapshot->source);
			if (priority > bestRank)
			{
				bestRank = priority;
				result.selectedObservation = static_cast<int>(i);
				result.value = observation.value;
				tie = false;
			}
			else if (priority == bestRank && result.value != observation.value)
				tie = true;
		}
		result.agreement = present == 0 ? PropertyAgreement::NotCompared :
			present == 1 ? PropertyAgreement::SingleSource :
			disagree ? PropertyAgreement::Conflicting : PropertyAgreement::Agreeing;
		if (present > 0)
			result.readState = PropertyReadState::Present;
		if (tie)
		{
			result.value.clear();
			result.selectedObservation = -1;
			result.reason = QStringLiteral("Equally eligible sources disagree; no defensible winner");
		}
		else if (result.selectedObservation >= 0)
			result.reason = QStringLiteral("Selected by the field-specific source priority; alternatives retained");
		return result;
	}
private:
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
	enum class Kind { MissingLocalReference, UnmatchedDatabaseIdentity, MetadataConflict, SourceChanged };
	Kind kind = Kind::MissingLocalReference;
	SourceSnapshotRef source;
	QString expectedPath;
	QString fileMobId;
	QStringList matchingPaths;
	bool scopeComplete = false;
	QString explanation;
};
Q_DECLARE_METATYPE(ScanIssue)
