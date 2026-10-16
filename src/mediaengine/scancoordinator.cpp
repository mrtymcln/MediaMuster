#include "scancoordinator.h"
#include "databasesource.h"
#include "mxfsource.h"
#include "discoveryengine.h"
#include "projection.h"
#include "metadataselectionpolicy.h"
#include "omfreader.h"
#include "pmrkey.h"
#include "mobid.h"
#include "featureflags.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <utility>

namespace MediaEngine
{
	namespace
	{
		QString folderKey(const QString &path)
		{
			const QFileInfo info(path);
			const QString canonical = info.canonicalFilePath();
			return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
		}
		bool safeName(const QString &name)
		{
			return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..") &&
				   !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'));
		}
		MetadataSource sourceKind(SourceCandidate::ReaderHint hint)
		{
			switch (hint)
			{
			case SourceCandidate::ReaderHint::Pmr:
				return MetadataSource::Pmr;
			case SourceCandidate::ReaderHint::Mdb:
				return MetadataSource::Mdb;
			case SourceCandidate::ReaderHint::Mxf:
				return MetadataSource::Mxf;
			case SourceCandidate::ReaderHint::LegacyMedia:
				return MetadataSource::Omf;
			}
			return MetadataSource::Filesystem;
		}
		ParsedSource readLegacy(const SourceCandidate &candidate, const Cancellation &cancellation)
		{
			const auto snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Omf, candidate.path, candidate.modified, SourceReadState::NotRead});
			if (cancellation.cancelled())
			{
				ParsedSource result;
				result.snapshot = snapshot;
				result.outcome = ParsedSource::Outcome::Cancelled;
				return result;
			}
			QFile input(candidate.path);
			if (!input.open(QIODevice::ReadOnly))
			{
				snapshot->readState = SourceReadState::Unreadable;
				ParsedSource result;
				result.snapshot = snapshot;
				result.outcome = ParsedSource::Outcome::IoError;
				result.diagnostics.append(input.errorString());
				return result;
			}
			const ReaderContext context{snapshot, cancellation};
			return OmfReader{}.read(input, context);
		}
		void attachObjects(MediaFile &file, const ProjectedFile &facts)
		{
			for (const auto &object : facts.objects)
				if (std::none_of(file.objects.cbegin(), file.objects.cend(), [&](const ObjectReference &existing)
								 { return existing.source == object.source && existing.handle == object.handle; }))
					file.objects.append(object);
		}
		void attach(MediaFile &file, const ProjectedFile &facts, bool eligible)
		{
			appendEvidence(file.evidence, facts.evidence, eligible);
			attachObjects(file, facts);
		}
		void attachMasterDetails(MediaFile &file, const ProjectedFile &facts, bool eligible)
		{
			appendMasterMetadata(file.evidence, facts.evidence, eligible);
			attachObjects(file, facts);
		}

		QStringList fileIdentityClaims(const ProjectedFile &facts)
		{
			if (!facts.fileMobId.isEmpty())
				return {facts.fileMobId};
			QStringList claims;
			for (const auto &observation : facts.evidence.observations(MediaProperty::FileMobId))
				if (observation.eligible && observation.readState == PropertyReadState::Present)
				{
					const auto id = observation.value.toString();
					if (!id.isEmpty() && !MobId::isAllZero(id) && !claims.contains(id))
						claims.append(id);
				}
			return claims;
		}
		bool hasPresentFact(const ProjectedFile &facts, MediaProperty property, const QString &value)
		{
			for (const auto &item : facts.evidence.observations(property))
				if (item.eligible && item.readState == PropertyReadState::Present && item.value.toString() == value)
					return true;
			return false;
		}
		bool hasPmrReference(const ProjectedFile &facts, const QString &filename, bool exact)
		{
			if (facts.fileMobId.isEmpty() || MobId::isAllZero(facts.fileMobId) ||
				!hasPresentFact(facts, MediaProperty::FileMobId, facts.fileMobId))
				return false;
			for (const auto &recorded : facts.filenames)
				if ((exact ? recorded == filename : PmrKey::primary(recorded) == PmrKey::primary(filename)) &&
					hasPresentFact(facts, MediaProperty::Filename, recorded))
					return true;
			return false;
		}

		void selectMatchingEffects(MediaEvidence &evidence)
		{
			// Keep the existing interpretation history while matching adds sources.
			// Only these two fields feed effects; the other fields can wait.
			for (const auto property : {MediaProperty::ClipName, MediaProperty::Type})
				evidence.select(property, resolveProperty(evidence, propertyPolicy(property)));
			selectEffectMetadata(evidence);
		}

		QStringList requiredTableMetadata(const MediaEvidence &evidence)
		{
			QStringList missing;
			const auto value = [&](MediaProperty property)
			{ return evidence.selected(property).value; };
			const auto require = [&](MediaProperty property, bool usable)
			{
				if (!usable)
					missing.append(mediaPropertyName(property));
			};
			for (const auto property : {MediaProperty::ClipName, MediaProperty::Project, MediaProperty::Compression,
										MediaProperty::BitDepth, MediaProperty::FileMobId})
				require(property, value(property).isValid() && !value(property).toString().isEmpty());
			require(MediaProperty::MasterMobId, !value(MediaProperty::MasterMobId).toString().isEmpty());
			const auto kind = value(MediaProperty::Kind);
			require(MediaProperty::Kind, kind.isValid() && (kind.toInt() == 0 || kind.toInt() == 1));
			const auto type = value(MediaProperty::Type);
			require(MediaProperty::Type, type.isValid() && (type.toInt() == int(MediaType::Media) || type.toInt() == int(MediaType::Precompute)));
			const auto duration = mediaDuration(value(MediaProperty::FileDuration));
			require(MediaProperty::FileDuration, duration.units >= 0 && duration.rate.valid() && duration.displayRate.valid());
			if (kind.isValid() && kind.toInt() == 0)
			{
				require(MediaProperty::Resolution, !value(MediaProperty::Resolution).toString().isEmpty());
				require(MediaProperty::FrameRate, mediaRate(value(MediaProperty::FrameRate)).valid());
			}
			else if (kind.isValid() && kind.toInt() == 1)
				require(MediaProperty::SampleRate, mediaRate(value(MediaProperty::SampleRate)).valid());
			if (FeatureFlags::kPrecomputeFilter && type.isValid() && type.toInt() == int(MediaType::Precompute))
				require(MediaProperty::PrecomputeCategory, value(MediaProperty::PrecomputeCategory).toInt() > 0);
			const auto sourcePath = evidence.selected(MediaProperty::SourcePath);
			if (sourcePath.applicability != PropertyApplicability::NotApplicable)
				require(MediaProperty::SourcePath, sourcePath.value.isValid() && !sourcePath.value.toString().isEmpty());
			// Bin names and effect details may legitimately be absent.
			// A recorded disagreement is still worth a header read; absence alone
			// is not. Internal sample representation and channels do not gate I/O.
			for (const auto property : {MediaProperty::OriginalBin,
										MediaProperty::Effect, MediaProperty::EffectCategory, MediaProperty::EffectSequence})
			{
				const auto selected = evidence.selected(property);
				if (!selected.value.isValid() && selected.agreement == PropertyAgreement::Conflicting)
					missing.append(mediaPropertyName(property));
			}
			return missing;
		}
	}

	void selectMetadata(MediaEvidence &evidence)
	{
		for (const auto &policy : propertyPolicies())
		{
			// Effects depend on the final selected name and type, so derive them last.
			if (policy.rule == SelectionRule::DerivedEffect)
				continue;
			auto field = resolveProperty(evidence, policy);
			if (policy.rule == SelectionRule::FileDuration && field.value.isValid())
			{
				// A display clock does not change the recorded length. Only clocks
				// attached to equivalent durations can supplement the selected one.
				const auto selected = mediaDuration(field.value);
				MediaEvidence clocks;
				for (auto item : evidence.observations(policy.property))
				{
					const auto candidate = mediaDuration(item.value);
					if (!item.eligible || candidate.units != selected.units ||
						!candidate.rate.sameRate(selected.rate) || !candidate.displayRate.valid())
						continue;
					item.value = rateValue(candidate.displayRate);
					clocks.observe(MediaProperty::FrameRate, std::move(item));
				}
				auto clockPolicy = policy;
				clockPolicy.property = MediaProperty::FrameRate;
				const auto clock = resolveProperty(clocks, clockPolicy);
				auto value = field.value.toMap();
				value.insert(QStringLiteral("DisplayRate"), clock.value);
				field.value = value;
				field.reason = EvidenceExplanation::withDisplayClock(field.reason, clock.reason);
			}
			evidence.select(policy.property, std::move(field));
		}
		selectEffectMetadata(evidence);
	}

	MediaEvidence selectedProjectedEvidence(const ProjectedFile &file, const Projection &projection)
	{
		MediaEvidence result = file.evidence;
		const QString masterMobId = resolveProperty(result, propertyPolicy(MediaProperty::MasterMobId)).value.toString();
		if (!masterMobId.isEmpty())
			for (const auto &master : projection.masters)
				if (masterGroupMatchesAssociation(result, master, masterMobId))
					appendMasterMetadata(result, master.evidence);
		selectMetadata(result);
		return result;
	}

	ScanResult ScanCoordinator::scan(const ScanRequest &request, const Cancellation &cancellation,
									 const ScanCallbacks &callbacks) const
	{
		ScanResult result = DiscoveryEngine{}.discover(request, cancellation, callbacks.discovering);
		const bool discoveryCancelled = result.cancelled;
		// Finish the CPU-only inventory and matching even after Cancel. Only
		// acquisition and freshness checks may request more filesystem work.
		// Resolve folder spellings before acquisition, so final matching never
		// needs to canonicalize a path after cancellation.
		QHash<QString, QString> folderKeys;
		const auto cachedFolderKey = [&](const QString &path)
		{
			if (const auto found = folderKeys.constFind(path); found != folderKeys.cend())
				return found.value();
			// This can only be uncaptured when acquisition never began. Keep
			// distinct discovered folders separate without accessing the disk.
			return QDir::cleanPath(path);
		};
		QVector<Projection> projections;
		QHash<KelpieId, qsizetype> headers;
		QHash<QString, QVector<qsizetype>> databases;
		QHash<qsizetype, QHash<QString, QVector<qsizetype>>> pmrByName;
		QHash<QString, QVector<QPair<qsizetype, qsizetype>>> mdbById;
		QHash<QString, QVector<QPair<qsizetype, qsizetype>>> mastersById;
		QVector<bool> unchanged;
		// Unknown checks are distinct from a proven unchanged source. A
		// preparation is qualified only after projection and its post-read check.
		QVector<bool> freshnessChecked(result.candidates.size(), false);
		QVector<bool> qualified(result.candidates.size(), false);
		for (const auto &issue : result.discoveryIssues)
			if (callbacks.warning)
				callbacks.warning(QStringLiteral("%1: %2").arg(issue.path, issue.explanation));
		// Candidate and source positions stay aligned, including media headers
		// deliberately left unopened by the database-first scheduler.
		projections.resize(result.candidates.size());
		unchanged.fill(true, result.candidates.size());
		QVector<bool> scheduled(result.candidates.size(), false);
		for (qsizetype index = 0; index < result.candidates.size(); ++index)
		{
			const auto &candidate = result.candidates[index];
			const QString folder = QFileInfo(candidate.path).absolutePath();
			if (!folderKeys.contains(folder) && !cancellation.cancelled())
				folderKeys.insert(folder, folderKey(folder));
			SourceReceipt source;
			source.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				sourceKind(candidate.hint), candidate.path, candidate.modified, SourceReadState::NotRead});
			result.sources.append(std::move(source));
			if (candidate.kelpieId)
				headers.insert(candidate.kelpieId, index);
			else
				databases[cachedFolderKey(folder)].append(index);
		}
		const auto checkUnchanged = [&](qsizetype index)
		{
			if (!unchanged[index] || cancellation.cancelled())
				return false;
			const auto &candidate = result.candidates[index];
			const QFileInfo current(candidate.path);
			const bool currentUnchanged = current.exists() && current.lastModified() == candidate.modified;
			freshnessChecked[index] = true;
			if (currentUnchanged)
				return true;
			unchanged[index] = false;
			ScanIssue issue;
			issue.kind = ScanIssue::Kind::SourceChanged;
			issue.source = result.sources[index].snapshot;
			issue.expectedPath = candidate.path;
			issue.explanation = QStringLiteral("Source changed after discovery; observations retained but excluded from selection");
			result.reconciliationIssues.append(std::move(issue));
			for (auto *subjects : {&projections[index].files, &projections[index].masters})
				for (auto &facts : *subjects)
					facts.evidence.qualifyAll(false, SourceFreshness::Changed);
			return false;
		};
		const auto sourceUsable = [&](qsizetype index)
		{
			return qualified[index] && unchanged[index];
		};
		const auto readCandidate = [&](qsizetype index, const QString &reason)
		{
			if (cancellation.cancelled())
				return;
			const auto &candidate = result.candidates[index];
			if (callbacks.reading)
				callbacks.reading(candidate);
			if (cancellation.cancelled())
				return;
			PreparedSource prepared;
			if (candidate.hint == SourceCandidate::ReaderHint::Pmr ||
				candidate.hint == SourceCandidate::ReaderHint::Mdb)
				prepared = prepareDatabase(candidate, reason, cancellation);
			else if (candidate.hint == SourceCandidate::ReaderHint::Mxf)
				prepared = prepareMxf(candidate, reason, cancellation);
			else
			{
				auto parsed = readLegacy(candidate, cancellation);
				parsed.readReason = reason;
				if (!cancellation.cancelled())
					prepared.projection = projectOmf(parsed, cancellation);
				prepared.source = SourceReceipt::fromParsed(std::move(parsed));
			}
			projections[index] = std::move(prepared.projection);
			result.sources[index] = std::move(prepared.source);
			if (cancellation.cancelled())
			{
				// A cut-short projection can precede its final ownership checks.
				// Its source receipt survives, but it supplies no qualified facts.
				projections[index].files.clear();
				projections[index].masters.clear();
				projections[index].fileReferencesComplete = false;
				return;
			}
			const auto &source = result.sources[index];
			qualified[index] = checkUnchanged(index);
			if (!qualified[index] && unchanged[index])
			{
				// Cancel may have arrived before the post-read change check.
				projections[index].files.clear();
				projections[index].masters.clear();
				projections[index].fileReferencesComplete = false;
			}
			else if (!unchanged[index])
				for (auto *subjects : {&projections[index].files, &projections[index].masters})
					for (auto &facts : *subjects)
						facts.evidence.qualifyAll(false, SourceFreshness::Changed);
			scheduled[index] = !cancellation.cancelled();
			if (callbacks.warning)
			{
				for (const auto &message : source.diagnostics)
					callbacks.warning(QStringLiteral("%1: %2").arg(candidate.path, message));
				for (const auto &message : projections[index].diagnostics)
					callbacks.warning(QStringLiteral("%1: %2").arg(candidate.path, message));
			}
		};
		int processed = 0;
		for (qsizetype sourceIndex = 0; sourceIndex < result.candidates.size(); ++sourceIndex)
		{
			const auto &candidate = result.candidates[sourceIndex];
			if (candidate.kelpieId)
				continue;
			if (cancellation.cancelled())
				break;
			if (callbacks.progress)
				callbacks.progress(processed++, int(result.candidates.size()), candidate.path);
			if (cancellation.cancelled())
				break;
			readCandidate(sourceIndex, QStringLiteral("Read database before deciding which media headers are needed"));
			if (candidate.hint == SourceCandidate::ReaderHint::Pmr)
			{
				auto &names = pmrByName[sourceIndex];
				for (qsizetype record = 0; record < projections[sourceIndex].files.size(); ++record)
				{
					for (const auto &filename : projections[sourceIndex].files[record].filenames)
						if (safeName(filename))
							names[PmrKey::primary(filename)].append(record);
				}
			}
			if (candidate.hint == SourceCandidate::ReaderHint::Mdb)
				for (qsizetype record = 0; record < projections[sourceIndex].files.size(); ++record)
				{
					for (const auto &id : fileIdentityClaims(projections[sourceIndex].files[record]))
						if (!MobId::isAllZero(id))
							mdbById[id].append({sourceIndex, record});
				}
			if (candidate.hint == SourceCandidate::ReaderHint::Mdb)
				for (qsizetype record = 0; record < projections[sourceIndex].masters.size(); ++record)
				{
					for (const auto &id : projections[sourceIndex].masters[record].masterMobIds)
						if (!id.isEmpty() && !MobId::isAllZero(id))
							mastersById[id].append({sourceIndex, record});
				}
		}
		for (const auto &indices : databases)
			for (const auto index : indices)
			{
				if (cancellation.cancelled())
					break;
				checkUnchanged(index);
			}
		QHash<QString, QStringList> pathsById;
		QHash<QString, QSet<QString>> identitiesByFolder;
		QHash<QString, QHash<QString, QStringList>> namesByFolder;
		for (const auto &file : result.files)
		{
			const QFileInfo info(file.path);
			namesByFolder[cachedFolderKey(info.absolutePath())][PmrKey::primary(info.fileName())].append(info.fileName());
		}
		// Refresh the shared folder observation before a matching pass. Matching
		// itself only borrows captured receipts; cancellation never starts a stat.
		// A folder without a captured receipt remains explicitly incomplete.
		QHash<QString, SourceSnapshotRef> folderReceipts;
		QSet<QString> refreshedFolders;
		const auto refreshFolderReceipt = [&](const QString &folder)
		{
			if (refreshedFolders.contains(folder) || cancellation.cancelled())
				return;
			const SourceSnapshotRef snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Filesystem, folder, QFileInfo(folder).lastModified(), SourceReadState::Complete});
			folderReceipts.insert(folder, snapshot);
			refreshedFolders.insert(folder);
		};
		const auto folderReceipt = [&](const QString &folder) -> SourceSnapshotRef
		{
			if (const auto found = folderReceipts.constFind(folder); found != folderReceipts.cend())
				return found.value();
			const SourceSnapshotRef snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Filesystem, folder, {}, SourceReadState::Incomplete});
			folderReceipts.insert(folder, snapshot);
			return snapshot;
		};
		const auto matchFile = [&](MediaFile &file, bool reportIssues)
		{
			const QString physicalFolder = QFileInfo(file.path).absolutePath();
			const QString folder = cachedFolderKey(physicalFolder);
			const QString filename = QFileInfo(file.path).fileName();
			const QString name = PmrKey::primary(filename);
			const bool physicalChanged = headers.contains(file.kelpieId) && !unchanged[headers.value(file.kelpieId)];
			const auto headerIndex = headers.value(file.kelpieId);
			// A skipped header needs the completed physical check; an attempted
			// header also needs its preparation's post-read check to have finished.
			const bool physicalUsable = headers.contains(file.kelpieId) && freshnessChecked[headerIndex] &&
				unchanged[headerIndex] && (result.sources[headerIndex].outcome == ParsedSource::Outcome::NotRead || qualified[headerIndex]);
			if (headers.contains(file.kelpieId))
			{
				file.evidence.registerSource(result.sources[headers.value(file.kelpieId)].snapshot);
				for (const auto &facts : projections[headers.value(file.kelpieId)].files)
					attach(file, facts, sourceUsable(headers.value(file.kelpieId)));
				if (physicalChanged)
					file.evidence.qualifySource(result.sources[headers.value(file.kelpieId)].snapshot,
												false, SourceFreshness::Changed);
			}
			selectMatchingEffects(file.evidence);
			const auto headerSelection = resolveProperty(file.evidence, propertyPolicy(MediaProperty::FileMobId));
			const QString headerId = headerSelection.value.toString();
			const bool conflictingHeader = headerSelection.agreement == PropertyAgreement::Conflicting && headerId.isEmpty();
			struct PmrMatch
			{
				qsizetype source;
				qsizetype record;
				bool exact;
				bool complete;
				bool referenceValid;
			};
			QVector<PmrMatch> pmrMatches;
			QVariantList databaseReceipts;
			QSet<qsizetype> completedPmrMisses;
			bool listed = false, usablePmrMatch = false, hasExact = false;
			for (const auto sourceIndex : databases.value(folder))
			{
				const auto &source = result.sources[sourceIndex];
				const bool complete = source.outcome == ParsedSource::Outcome::Complete && sourceUsable(sourceIndex);
				databaseReceipts.append(QVariantMap{{QStringLiteral("Path"), source.snapshot->path},
													{QStringLiteral("Outcome"), int(source.outcome)},
													{QStringLiteral("UnchangedDuringRead"), sourceUsable(sourceIndex)}});
				if (source.snapshot->source != MetadataSource::Pmr)
					continue;
				if (complete && physicalUsable)
					completedPmrMisses.insert(sourceIndex);
				for (const auto record : pmrByName.value(sourceIndex).value(name))
				{
					const auto &facts = projections[sourceIndex].files[record];
					const bool exact = facts.filenames.contains(filename);
					hasExact |= exact;
					pmrMatches.append({sourceIndex, record, exact, complete, hasPmrReference(facts, filename, exact)});
				}
			}
			// A normalized spelling is only a fallback. It cannot assign one PMR
			// entry to two case-distinct physical files, or bypass an exact name.
			const bool ambiguousName = !hasExact && namesByFolder.value(folder).value(name).size() != 1;
			QSet<QString> compatibleIds;
			QSet<QString> compatibleReferenceIds;
			for (const auto &match : pmrMatches)
			{
				const auto &facts = projections[match.source].files[match.record];
				if (match.complete && (!hasExact || match.exact) && !facts.fileMobId.isEmpty() &&
					(headerId.isEmpty() || headerId == facts.fileMobId))
					compatibleIds.insert(facts.fileMobId);
				if (sourceUsable(match.source) && match.referenceValid && (!hasExact || match.exact) &&
					(headerId.isEmpty() || headerId == facts.fileMobId))
					compatibleReferenceIds.insert(facts.fileMobId);
			}
			const bool ambiguousIdentity = compatibleIds.size() > 1;
			for (const auto &match : pmrMatches)
			{
				const auto &facts = projections[match.source].files[match.record];
				const bool chosenName = !hasExact || match.exact;
				const bool sameIdentity = !conflictingHeader && (headerId.isEmpty() || headerId == facts.fileMobId);
				const bool eligible = match.complete && chosenName && sameIdentity &&
									  !ambiguousName && !ambiguousIdentity && physicalUsable;
				attach(file, facts, eligible);
				usablePmrMatch |= eligible;
				// A partial source can prove these two fields without supplying
				// usable metadata or permitting a skipped media-header read.
				listed |= sourceUsable(match.source) && match.referenceValid && chosenName && sameIdentity &&
					!ambiguousName && compatibleReferenceIds.size() == 1 && physicalUsable;
				// A possible match that cannot be qualified is not proof of absence.
				// A known different header identity does establish that this entry
				// belongs to another file, even when its filename matches.
				if (chosenName && !(match.referenceValid && !headerId.isEmpty() && headerId != facts.fileMobId))
					completedPmrMisses.remove(match.source);
				if (match.complete && chosenName && (!sameIdentity || ambiguousName || ambiguousIdentity))
				{
					ScanIssue issue;
					issue.kind = ScanIssue::Kind::MetadataConflict;
					issue.source = result.sources[match.source].snapshot;
					issue.expectedPath = file.path;
					issue.fileMobId = facts.fileMobId;
					issue.explanation = ambiguousName		? QStringLiteral("Normalized PMR filename matches multiple physical files; database observations retained but excluded")
										: ambiguousIdentity ? QStringLiteral("PMR filename has multiple compatible file identities; database observations retained but excluded")
															: QStringLiteral("PMR filename matches, but its identity disagrees with the media header; database observations excluded");
					if (reportIssues)
						result.reconciliationIssues.append(issue);
				}
			}
			selectMatchingEffects(file.evidence);
			const QString identity = resolveProperty(file.evidence, propertyPolicy(MediaProperty::FileMobId)).value.toString();
			bool completeMdbMatch = false;
			QSet<QString> candidateIds;
			for (const auto &item : file.evidence.observations(MediaProperty::FileMobId))
				if (item.readState == PropertyReadState::Present && !item.value.toString().isEmpty())
					candidateIds.insert(item.value.toString());
			QSet<QPair<qsizetype, qsizetype>> attachedMdbFacts;
			for (const auto &candidateId : candidateIds)
				for (const auto &entry : mdbById.value(candidateId))
				{
					if (attachedMdbFacts.contains(entry))
						continue;
					attachedMdbFacts.insert(entry);
					const auto &facts = projections[entry.first].files[entry.second];
					const bool eligible = sourceUsable(entry.first) && physicalUsable &&
										  fileIdentityClaims(facts).contains(identity);
					attach(file, facts, eligible);
					completeMdbMatch |= eligible && result.sources[entry.first].outcome == ParsedSource::Outcome::Complete;
				}
			selectMatchingEffects(file.evidence);
			const QString selectedMaster = resolveProperty(file.evidence, propertyPolicy(MediaProperty::MasterMobId)).value.toString();
			if (!selectedMaster.isEmpty())
			{
				if (headers.contains(file.kelpieId))
				{
					const auto sourceIndex = headers.value(file.kelpieId);
					for (const auto &master : projections[sourceIndex].masters)
					{
						if (masterGroupMatchesAssociation(file.evidence, master, selectedMaster))
							attachMasterDetails(file, master, sourceUsable(sourceIndex) && physicalUsable);
					}
				}
				for (const auto &entry : mastersById.value(selectedMaster))
				{
					attachMasterDetails(file, projections[entry.first].masters[entry.second],
										sourceUsable(entry.first) && physicalUsable);
				}
			}
			selectMetadata(file.evidence);
			file.stamp.fileMobId = file.evidence.selected(MediaProperty::FileMobId).value.toString();
			file.stamp.masterMobId = file.evidence.selected(MediaProperty::MasterMobId).value.toString();
			const bool knownIdentity = !file.stamp.fileMobId.isEmpty() && !MobId::isAllZero(file.stamp.fileMobId);
			QSet<qsizetype> listedMdbSources;
			if (knownIdentity && physicalUsable)
				for (const auto &entry : attachedMdbFacts)
				{
					const auto &source = result.sources[entry.first];
					const auto &facts = projections[entry.first].files[entry.second];
					if (sourceUsable(entry.first) && facts.fileMobId == file.stamp.fileMobId &&
						cachedFolderKey(QFileInfo(source.snapshot->path).absolutePath()) == folder)
						listedMdbSources.insert(entry.first);
				}
			listed |= !listedMdbSources.isEmpty();
			bool completedMiss = !completedPmrMisses.isEmpty();
			for (const auto sourceIndex : databases.value(folder))
			{
				const auto &projection = projections[sourceIndex];
				if (result.sources[sourceIndex].snapshot->source == MetadataSource::Mdb &&
					sourceUsable(sourceIndex) && physicalUsable && projection.fileReferencesComplete &&
					(knownIdentity || projection.files.isEmpty()) && !listedMdbSources.contains(sourceIndex))
					completedMiss = true;
			}
			// DatabaseStatus uses the UI's stable values: Listed/NoReference/NoDatabase/DbUnreadable.
			const int status = listed ? 0 : completedMiss ? 1
				: databases.value(folder).isEmpty() && !discoveryCancelled && folderKeys.contains(physicalFolder) ? 2 : 3;
			MetadataObservation dbStatus;
			dbStatus.snapshot = folderReceipt(folder);
			dbStatus.property = QStringLiteral("Local PMR/MDB physical-file reference checks");
			dbStatus.value = status;
			dbStatus.rawValue = databaseReceipts;
			dbStatus.readState = PropertyReadState::Present;
			dbStatus.basis = EvidenceBasis::Derived;
			dbStatus.explanation = QStringLiteral("A validated local PMR or MDB reference wins, then a completed local reference check with no match, then an unresolved check or absent databases; source problems remain in Console. This is not proof of database freshness.");
			file.evidence.observe(MediaProperty::DatabaseStatus, std::move(dbStatus));
			// This filesystem membership observation changes only its own field.
			file.evidence.select(MediaProperty::DatabaseStatus,
								 resolveProperty(file.evidence, propertyPolicy(MediaProperty::DatabaseStatus)));

			for (int field = int(MediaProperty::ClipName); field < int(MediaProperty::Count); ++field)
			{
				const auto selected = file.evidence.selected(MediaProperty(field));
				if (selected.agreement != PropertyAgreement::Conflicting)
					continue;
				const auto property = MediaProperty(field);
				if (selected.value.isValid() && (property == MediaProperty::ClipName || property == MediaProperty::Project || property == MediaProperty::OriginalBin))
					continue; // A lower-priority editorial alternative remains evidence, without a warning per row.
				ScanIssue issue;
				issue.kind = ScanIssue::Kind::MetadataConflict;
				issue.expectedPath = file.path;
				issue.fileMobId = file.stamp.fileMobId;
				issue.explanation = QStringLiteral("%1: %2").arg(mediaPropertyName(property), selected.reason.text());
				if (reportIssues)
					result.reconciliationIssues.append(issue);
			}
			return usablePmrMatch && completeMdbMatch && physicalUsable;
		};

		const auto decideHeader = [&](const MediaFile &file)
		{
			if (cancellation.cancelled())
				return;
			const auto sourceIndex = headers.value(file.kelpieId);
			const bool current = checkUnchanged(sourceIndex);
			if (cancellation.cancelled())
				return;
			// Without a local PMR filename candidate, database-only matching cannot
			// supply this file. Avoid building and selecting a throwaway row.
			const QFileInfo info(file.path);
			const QString folder = cachedFolderKey(info.absolutePath());
			bool canMatchPmr = false;
			if (const auto local = databases.constFind(folder); local != databases.cend())
			{
				const QString name = PmrKey::primary(info.fileName());
				for (const auto index : local.value())
				{
					const auto names = pmrByName.constFind(index);
					if (names != pmrByName.cend() && names.value().contains(name))
					{
						canMatchPmr = true;
						break;
					}
				}
			}
			if (cancellation.cancelled())
				return;
			if (!current || !canMatchPmr)
			{
				readCandidate(sourceIndex, !current ? QStringLiteral("Header read: physical file changed after discovery")
													: QStringLiteral("Header read: no usable database match"));
				return;
			}
			MediaFile databaseFile = file;
			refreshFolderReceipt(folder);
			const bool matched = matchFile(databaseFile, false);
			if (cancellation.cancelled())
				return;
			const QStringList missing = requiredTableMetadata(databaseFile.evidence);
			if (current && matched && missing.isEmpty())
			{
				result.sources[sourceIndex].readReason = QStringLiteral("Header not read: usable database match supplies required table metadata; database freshness remains unknown");
				scheduled[sourceIndex] = true;
			}
			else
			{
				const QString reason = !current	  ? QStringLiteral("Header read: physical file changed after discovery")
									   : !matched ? QStringLiteral("Header read: no usable database match")
												  : QStringLiteral("Header read: required table metadata missing or conflicting: %1").arg(missing.join(QStringLiteral(", ")));
				readCandidate(sourceIndex, reason);
			}
		};
		for (const auto &file : result.files)
		{
			if (cancellation.cancelled())
				break;
			if (callbacks.progress)
				callbacks.progress(processed++, int(result.candidates.size()), file.path);
			if (cancellation.cancelled())
				break;
			decideHeader(file);
		}
		// Recheck even deliberately unopened media: a database match is not
		// permission to retain metadata for a file replaced during the scan.
		for (qsizetype index = 0; index < result.candidates.size(); ++index)
		{
			if (cancellation.cancelled())
				break;
			checkUnchanged(index);
		}
		// A database changed after an earlier skip can invalidate that decision.
		// Make one bounded fallback pass; continuing source changes are reported
		// and excluded, rather than making the scan chase a moving database.
		if (unchanged.contains(false))
		{
			refreshedFolders.clear();
			for (const auto &file : result.files)
			{
				if (cancellation.cancelled())
					break;
				const auto index = headers.value(file.kelpieId);
				if (scheduled[index] && result.sources[index].outcome == ParsedSource::Outcome::NotRead)
					decideHeader(file);
			}
			for (qsizetype index = 0; index < result.candidates.size(); ++index)
			{
				if (cancellation.cancelled())
					break;
				checkUnchanged(index);
			}
		}
		result.parsingComplete = !cancellation.cancelled() &&
								 std::all_of(scheduled.cbegin(), scheduled.cend(), [](bool complete)
											 { return complete; });
		// Observe final folder changes after the phase callback on a normal run.
		// Cancel instead reuses earlier receipts or an explicitly incomplete one.
		if (!cancellation.cancelled() && callbacks.finalising)
			callbacks.finalising();
		refreshedFolders.clear();
		for (const auto &file : result.files)
		{
			if (cancellation.cancelled())
				break;
			refreshFolderReceipt(cachedFolderKey(QFileInfo(file.path).absolutePath()));
		}
		for (auto &file : result.files)
		{
			matchFile(file, true);
			if (!file.stamp.fileMobId.isEmpty())
			{
				pathsById[file.stamp.fileMobId].append(file.path);
				identitiesByFolder[cachedFolderKey(QFileInfo(file.path).absolutePath())].insert(file.stamp.fileMobId);
			}
		}
		for (qsizetype index = 0; index < result.sources.size(); ++index)
		{
			const auto &source = result.sources[index];
			if (source.snapshot->source != MetadataSource::Pmr && source.snapshot->source != MetadataSource::Mdb)
				continue;
			const QString folder = cachedFolderKey(QFileInfo(source.snapshot->path).absolutePath());
			QSet<QString> reported;
			for (const auto &facts : projections[index].files)
			{
				QStringList missing;
				for (const auto &name : facts.filenames)
				{
					const auto names = namesByFolder.value(folder).value(PmrKey::primary(name));
					if (safeName(name) && !names.contains(name) && names.size() != 1)
						missing.append(QDir(folder).filePath(name));
				}
				const bool localIdentity = identitiesByFolder.value(folder).contains(facts.fileMobId);
				if (source.snapshot->source == MetadataSource::Mdb && !facts.fileMobId.isEmpty() && !localIdentity)
					missing.append(QString{});
				for (const auto &path : missing)
				{
					const QString key = path + QLatin1Char('|') + facts.fileMobId;
					if (reported.contains(key))
						continue;
					reported.insert(key);
					ScanIssue issue;
					issue.kind = path.isEmpty() ? ScanIssue::Kind::UnmatchedDatabaseIdentity : ScanIssue::Kind::MissingLocalReference;
					issue.source = source.snapshot;
					issue.expectedPath = path;
					issue.fileMobId = facts.fileMobId;
					issue.matchingPaths = pathsById.value(facts.fileMobId);
					issue.scopeComplete = result.discoveryComplete && result.parsingComplete && !cancellation.cancelled();
					issue.explanation = path.isEmpty() ? QStringLiteral("Database file identity has no matching media in its folder") : QStringLiteral("Database entry has no media at its recorded local path");
					result.reconciliationIssues.append(issue);
				}
			}
		}
		result.cancelled = cancellation.cancelled();
		result.reconciliationComplete = result.parsingComplete && !result.cancelled;
		if (result.cancelled)
			for (auto &issue : result.reconciliationIssues)
				issue.scopeComplete = false;
		return result;
	}
}
