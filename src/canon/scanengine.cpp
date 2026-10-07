#include "scanengine.h"
#include "discoveryengine.h"
#include "projection.h"
#include "pmrreader.h"
#include "mdbreader.h"
#include "mxfreader.h"
#include "legacyreader.h"
#include "pmrkey.h"
#include "mobid.h"
#include "featureflags.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>

namespace Canon
{
	namespace
	{
		int technicalRank(MetadataSource source)
		{
			switch (source)
			{
			case MetadataSource::Filesystem:
				return 6;
			case MetadataSource::Mxf:
			case MetadataSource::Omf:
				return 5;
			case MetadataSource::Mdb:
				return 4;
			case MetadataSource::Pmr:
				return 3;
			case MetadataSource::Avb:
				return 2;
			}
			return 0;
		}
		int projectRank(MetadataSource source)
		{
			return source == MetadataSource::Pmr ? 6 : source == MetadataSource::Mdb ? 5
																					 : technicalRank(source) - 2;
		}
		int binRank(MetadataSource source)
		{
			return source == MetadataSource::Mdb ? 6 : technicalRank(source);
		}
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
		ParsedSource readSource(const SourceCandidate &candidate, const Cancellation &cancellation)
		{
			const auto snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				sourceKind(candidate.hint), candidate.path, candidate.modified, SourceReadState::NotRead});
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
			switch (candidate.hint)
			{
			case SourceCandidate::ReaderHint::Pmr:
				return PmrReader{}.read(input, context);
			case SourceCandidate::ReaderHint::Mdb:
				return MdbReader{}.read(input, context);
			case SourceCandidate::ReaderHint::Mxf:
				return MxfReader{}.read(input, context);
			case SourceCandidate::ReaderHint::LegacyMedia:
				return LegacyReader{}.read(input, context);
			}
			return {};
		}
		Projection project(const ParsedSource &source, const Cancellation &cancellation)
		{
			if (!source.snapshot)
				return {};
			switch (source.snapshot->source)
			{
			case MetadataSource::Pmr:
				return projectPmr(source, cancellation);
			case MetadataSource::Mxf:
				return projectMxf(source, cancellation);
			case MetadataSource::Mdb:
			case MetadataSource::Omf:
				return projectOmf(source, cancellation);
			default:
				return {};
			}
		}
		void attach(MediaFile &file, const ProjectedFile &facts, bool eligible)
		{
			appendEvidence(file.evidence, facts.evidence, eligible);
			for (const auto &object : facts.objects)
				if (std::none_of(file.objects.cbegin(), file.objects.cend(), [&](const ObjectReference &existing)
								 { return existing.source == object.source && existing.handle == object.handle; }))
					file.objects.append(object);
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
			for (const auto property : {MediaProperty::ClipName, MediaProperty::Project, MediaProperty::Codec,
										MediaProperty::BitDepth, MediaProperty::FileMobId})
				require(property, value(property).isValid() && !value(property).toString().isEmpty());
			require(MediaProperty::MasterMobId, !value(MediaProperty::MasterMobId).toStringList().isEmpty());
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
			if (FeatureFlags::kClipDuration)
			{
				const auto tracks = value(MediaProperty::ClipDuration).toList();
				const bool usable = !tracks.isEmpty() && std::all_of(tracks.cbegin(), tracks.cend(), [](const QVariant &track)
																	 {
					const auto duration = mediaDuration(track.toMap().value(QStringLiteral("Duration")));
					return duration.units >= 0 && duration.rate.valid() && duration.displayRate.valid(); });
				require(MediaProperty::ClipDuration, usable);
			}
			// Bin/source names and effect details may legitimately be absent.
			// A recorded disagreement is still worth a header read; absence alone
			// is not. Internal sample representation and channels do not gate I/O.
			for (const auto property : {MediaProperty::OriginalBin, MediaProperty::SourceFilename,
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
		for (int index = int(MediaProperty::ClipName); index <= int(MediaProperty::ComponentDepth); ++index)
		{
			const auto property = MediaProperty(index);
			const auto rank = property == MediaProperty::Project ? projectRank : property == MediaProperty::OriginalBin ? binRank
																														: technicalRank;
			evidence.select(property, evidence.resolve(property, rank, QStringLiteral("Canon field priorities v1")));
		}
		// Display clocks do not change a file's recorded length. Resolve that
		// optional clock separately, retaining the duration's original units.
		auto duration = evidence.selected(MediaProperty::FileDuration);
		if (duration.value.isValid())
		{
			const auto selected = mediaDuration(duration.value);
			MediaEvidence clocks;
			for (auto item : evidence.observations(MediaProperty::FileDuration))
			{
				const auto candidate = mediaDuration(item.value);
				if (!item.eligible || candidate.units != selected.units || !candidate.rate.sameRate(selected.rate) || !candidate.displayRate.valid())
					continue;
				item.value = rateValue(candidate.displayRate);
				clocks.observe(MediaProperty::FrameRate, std::move(item));
			}
			const auto clock = clocks.resolve(MediaProperty::FrameRate, technicalRank, QStringLiteral("Recorded duration display clock"));
			auto value = duration.value.toMap();
			value.insert(QStringLiteral("DisplayRate"), clock.value);
			duration.value = value;
			duration.reason += QStringLiteral("; display clock resolved independently from matching duration observations: ") + clock.reason;
			evidence.select(MediaProperty::FileDuration, duration);
		}
		// Master associations are a set of recorded relationships, not rival scalar values.
		QStringList masters;
		for (const auto &observation : evidence.observations(MediaProperty::MasterMobId))
			if (observation.eligible && observation.readState == PropertyReadState::Present)
			{
				if (observation.value.metaType().id() == QMetaType::QStringList)
					masters.append(observation.value.toStringList());
				else if (!observation.value.toString().isEmpty())
					masters.append(observation.value.toString());
			}
		masters.removeDuplicates();
		std::sort(masters.begin(), masters.end());
		if (!masters.isEmpty())
		{
			ResolvedField field;
			field.value = masters;
			field.readState = PropertyReadState::Present;
			field.rule = QStringLiteral("Preserve every eligible master association");
			field.reason = QStringLiteral("Multiple masters do not merge physical files");
			evidence.select(MediaProperty::MasterMobId, field);
		}
		selectEffectMetadata(evidence);
	}

	ScanResult ScanEngine::scan(const ScanRequest &request, const Cancellation &cancellation,
								const ScanCallbacks &callbacks) const
	{
		ScanResult result = DiscoveryEngine{}.discover(request, cancellation);
		QVector<Projection> projections;
		QHash<KelpieId, qsizetype> headers;
		QHash<QString, QVector<qsizetype>> databases;
		QHash<qsizetype, QHash<QString, QVector<qsizetype>>> pmrByName;
		QHash<QString, QVector<QPair<qsizetype, qsizetype>>> mdbById;
		QHash<QString, QVector<QPair<qsizetype, qsizetype>>> mastersById;
		QVector<bool> unchanged;
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
			ParsedSource source;
			source.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				sourceKind(candidate.hint), candidate.path, candidate.modified, SourceReadState::NotRead});
			result.sources.append(std::move(source));
			if (candidate.kelpieId)
				headers.insert(candidate.kelpieId, index);
			else
				databases[folderKey(QFileInfo(candidate.path).absolutePath())].append(index);
		}
		const auto checkUnchanged = [&](qsizetype index)
		{
			if (!unchanged[index])
				return false;
			const auto &candidate = result.candidates[index];
			const QFileInfo current(candidate.path);
			if (current.exists() && current.lastModified() == candidate.modified)
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
		const auto readCandidate = [&](qsizetype index, const QString &reason)
		{
			const auto &candidate = result.candidates[index];
			result.sources[index] = readSource(candidate, cancellation);
			auto &source = result.sources[index];
			source.readReason = reason;
			projections[index] = project(source, cancellation);
			if (!checkUnchanged(index))
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
			readCandidate(sourceIndex, QStringLiteral("Read database before deciding which media headers are needed"));
			if (candidate.hint == SourceCandidate::ReaderHint::Pmr)
			{
				auto &names = pmrByName[sourceIndex];
				for (qsizetype record = 0; record < projections[sourceIndex].files.size(); ++record)
				{
					if (cancellation.cancelled())
						break;
					for (const auto &filename : projections[sourceIndex].files[record].filenames)
						if (safeName(filename))
							names[PmrKey::primary(filename)].append(record);
				}
			}
			if (candidate.hint == SourceCandidate::ReaderHint::Mdb)
				for (qsizetype record = 0; record < projections[sourceIndex].files.size(); ++record)
				{
					const auto &id = projections[sourceIndex].files[record].fileMobId;
					if (!id.isEmpty() && !MobId::isAllZero(id))
						mdbById[id].append({sourceIndex, record});
				}
			if (candidate.hint == SourceCandidate::ReaderHint::Mdb)
				for (qsizetype record = 0; record < projections[sourceIndex].masters.size(); ++record)
					for (const auto &id : projections[sourceIndex].masters[record].masterMobIds)
						if (!id.isEmpty() && !MobId::isAllZero(id))
							mastersById[id].append({sourceIndex, record});
		}
		for (const auto &indices : databases)
			for (const auto index : indices)
				checkUnchanged(index);
		QHash<QString, QStringList> pathsById;
		QHash<QString, QHash<QString, QStringList>> namesByFolder;
		for (const auto &file : result.files)
		{
			const QFileInfo info(file.path);
			namesByFolder[folderKey(info.absolutePath())][PmrKey::primary(info.fileName())].append(info.fileName());
		}
		const auto matchFile = [&](MediaFile &file, bool reportIssues)
		{
			const QString folder = folderKey(QFileInfo(file.path).absolutePath());
			const QString filename = QFileInfo(file.path).fileName();
			const QString name = PmrKey::primary(filename);
			const bool physicalChanged = headers.contains(file.kelpieId) && !unchanged[headers.value(file.kelpieId)];
			if (headers.contains(file.kelpieId))
				for (const auto &facts : projections[headers.value(file.kelpieId)].files)
					attach(file, facts, unchanged[headers.value(file.kelpieId)]);
			selectMetadata(file.evidence);
			const auto headerSelection = file.evidence.selected(MediaProperty::FileMobId);
			const QString headerId = headerSelection.value.toString();
			const bool conflictingHeader = headerSelection.agreement == PropertyAgreement::Conflicting && headerId.isEmpty();
			struct PmrMatch
			{
				qsizetype source;
				qsizetype record;
				bool exact;
				bool complete;
			};
			QVector<PmrMatch> pmrMatches;
			QVariantList databaseReceipts;
			bool hasPmr = false, failedDatabase = false, listed = false, hasExact = false;
			for (const auto sourceIndex : databases.value(folder))
			{
				if (cancellation.cancelled())
					break;
				const auto &source = result.sources[sourceIndex];
				const bool complete = source.outcome == ParsedSource::Outcome::Complete && unchanged[sourceIndex];
				failedDatabase |= !complete;
				databaseReceipts.append(QVariantMap{{QStringLiteral("Path"), source.snapshot->path},
													{QStringLiteral("Outcome"), int(source.outcome)},
													{QStringLiteral("UnchangedDuringRead"), unchanged[sourceIndex]}});
				if (source.snapshot->source != MetadataSource::Pmr)
					continue;
				hasPmr = true;
				for (const auto record : pmrByName.value(sourceIndex).value(name))
				{
					const auto &facts = projections[sourceIndex].files[record];
					const bool exact = facts.filenames.contains(filename);
					hasExact |= exact;
					pmrMatches.append({sourceIndex, record, exact, complete});
				}
			}
			// A normalized spelling is only a fallback. It cannot assign one PMR
			// entry to two case-distinct physical files, or bypass an exact name.
			const bool ambiguousName = !hasExact && namesByFolder.value(folder).value(name).size() != 1;
			QSet<QString> compatibleIds;
			for (const auto &match : pmrMatches)
			{
				const auto &facts = projections[match.source].files[match.record];
				if (match.complete && (!hasExact || match.exact) && !facts.fileMobId.isEmpty() &&
					(headerId.isEmpty() || headerId == facts.fileMobId))
					compatibleIds.insert(facts.fileMobId);
			}
			const bool ambiguousIdentity = compatibleIds.size() > 1;
			for (const auto &match : pmrMatches)
			{
				if (cancellation.cancelled())
					break;
				const auto &facts = projections[match.source].files[match.record];
				const bool chosenName = !hasExact || match.exact;
				const bool sameIdentity = !conflictingHeader && (headerId.isEmpty() || headerId == facts.fileMobId);
				const bool eligible = match.complete && chosenName && sameIdentity &&
									  !ambiguousName && !ambiguousIdentity && !physicalChanged;
				attach(file, facts, eligible);
				listed |= eligible;
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
			selectMetadata(file.evidence);
			const QString identity = file.evidence.selected(MediaProperty::FileMobId).value.toString();
			bool completeMdbMatch = false;
			QSet<QString> candidateIds;
			for (const auto &item : file.evidence.observations(MediaProperty::FileMobId))
				if (item.readState == PropertyReadState::Present && !item.value.toString().isEmpty())
					candidateIds.insert(item.value.toString());
			for (const auto &candidateId : candidateIds)
				for (const auto &entry : mdbById.value(candidateId))
				{
					const bool eligible = unchanged[entry.first] && !physicalChanged && candidateId == identity;
					attach(file, projections[entry.first].files[entry.second], eligible);
					completeMdbMatch |= eligible && result.sources[entry.first].outcome == ParsedSource::Outcome::Complete;
				}
			selectMetadata(file.evidence);
			const auto selectedMasters = file.evidence.selected(MediaProperty::MasterMobId).value.toStringList();
			QSet<QString> candidateMasters;
			for (const auto &item : file.evidence.observations(MediaProperty::MasterMobId))
				if (item.readState == PropertyReadState::Present)
				{
					const auto identities = item.value.metaType().id() == QMetaType::QStringList ? item.value.toStringList() : QStringList{item.value.toString()};
					for (const auto &master : identities)
						if (!master.isEmpty())
							candidateMasters.insert(master);
				}
			for (const auto &master : candidateMasters)
				for (const auto &entry : mastersById.value(master))
					attach(file, projections[entry.first].masters[entry.second],
						   unchanged[entry.first] && !physicalChanged && selectedMasters.contains(master));
			selectMetadata(file.evidence);
			file.stamp.mobId = file.evidence.selected(MediaProperty::FileMobId).value.toString();
			file.stamp.masterMobIds = file.evidence.selected(MediaProperty::MasterMobId).value.toStringList();
			// DatabaseStatus uses the UI's stable values: Listed/NoReference/NoDatabase/DbUnreadable.
			const int status = listed ? 0 : failedDatabase ? 3
										: hasPmr		   ? 1
														   : 2;
			MetadataObservation dbStatus;
			dbStatus.snapshot = QSharedPointer<SourceSnapshot>::create(SourceSnapshot{
				MetadataSource::Filesystem, folder, QFileInfo(folder).lastModified(), SourceReadState::Complete});
			dbStatus.property = QStringLiteral("Local PMR membership and database source outcomes");
			dbStatus.value = status;
			dbStatus.rawValue = databaseReceipts;
			dbStatus.readState = PropertyReadState::Present;
			dbStatus.basis = EvidenceBasis::Derived;
			dbStatus.explanation = QStringLiteral("Derived from local database enumeration, read outcomes and unambiguous filename/identity matching; not proof of database freshness.");
			file.evidence.observe(MediaProperty::DatabaseStatus, std::move(dbStatus));
			selectMetadata(file.evidence);

			for (int field = int(MediaProperty::ClipName); field <= int(MediaProperty::ComponentDepth); ++field)
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
				issue.fileMobId = file.stamp.mobId;
				issue.explanation = QStringLiteral("%1: %2").arg(mediaPropertyName(property), selected.reason);
				if (reportIssues)
					result.reconciliationIssues.append(issue);
			}
			return listed && completeMdbMatch && !physicalChanged;
		};

		const auto decideHeader = [&](const MediaFile &file)
		{
			const auto sourceIndex = headers.value(file.kelpieId);
			const bool current = checkUnchanged(sourceIndex);
			MediaFile databaseFile = file;
			const bool matched = matchFile(databaseFile, false);
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
			decideHeader(file);
		}
		// Recheck even deliberately unopened media: a database match is not
		// permission to retain metadata for a file replaced during the scan.
		for (qsizetype index = 0; index < result.candidates.size(); ++index)
			if (!cancellation.cancelled())
				checkUnchanged(index);
		// A database changed after an earlier skip can invalidate that decision.
		// Make one bounded fallback pass; continuing source changes are reported
		// and excluded, rather than making the scan chase a moving database.
		if (unchanged.contains(false))
		{
			for (const auto &file : result.files)
			{
				if (cancellation.cancelled())
					break;
				const auto index = headers.value(file.kelpieId);
				if (scheduled[index] && result.sources[index].outcome == ParsedSource::Outcome::NotRead)
					decideHeader(file);
			}
			for (qsizetype index = 0; index < result.candidates.size(); ++index)
				if (!cancellation.cancelled())
					checkUnchanged(index);
		}
		result.parsingComplete = !cancellation.cancelled() &&
								 std::all_of(scheduled.cbegin(), scheduled.cend(), [](bool complete)
											 { return complete; });
		if (callbacks.finalising)
			callbacks.finalising();
		for (auto &file : result.files)
		{
			if (cancellation.cancelled())
				break;
			matchFile(file, true);
			if (!file.stamp.mobId.isEmpty())
				pathsById[file.stamp.mobId].append(file.path);
		}
		for (qsizetype index = 0; index < result.sources.size(); ++index)
		{
			if (cancellation.cancelled())
				break;
			const auto &source = result.sources[index];
			if (source.snapshot->source != MetadataSource::Pmr && source.snapshot->source != MetadataSource::Mdb)
				continue;
			const QString folder = folderKey(QFileInfo(source.snapshot->path).absolutePath());
			QSet<QString> reported;
			for (const auto &facts : projections[index].files)
			{
				if (cancellation.cancelled())
					break;
				QStringList missing;
				for (const auto &name : facts.filenames)
				{
					const auto names = namesByFolder.value(folder).value(PmrKey::primary(name));
					if (safeName(name) && !names.contains(name) && names.size() != 1)
						missing.append(QDir(folder).filePath(name));
				}
				bool localIdentity = false;
				for (const auto &path : pathsById.value(facts.fileMobId))
					localIdentity |= folderKey(QFileInfo(path).absolutePath()) == folder;
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
		return result;
	}
}
