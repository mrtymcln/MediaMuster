# Metadata selection policy

Status: implemented 8 October 2026. The shared
[policy table](../src/mediaengine/metadataselectionpolicy.cpp) selects displayed values
from retained source observations; scanning and bin enrichment use the same rules.
MediaEngine-backed rows refresh their semantic values from the final selections.
Verification results are recorded separately after the coordinated build.

Each row says which sources may supply one semantic field and which source takes
priority. The same choice feeds the table, filters and CSV. These preferences are
compiled into the application; the user rejected rule-version tracking and custom
rule-name tags.

Format corrections require their own evidence; passing tests alone do not establish format authority.

## What justifies a decision

Byte decoding, object ownership and format interpretation need primary format
definitions, Media Composer evidence or genuine specimens supporting that exact
interpretation. A lack of observed failures does not justify retaining a rule. Unsupported inherited assumptions need reassessment and
evidenced replacement or removal. In particular, MobId storage width has not been
established as a codec-selection criterion. The
[8 October correction](legacy-compression-and-audio-summaries-2026-10-08.md) replaces
it with typed descriptor context, verified private compression/resolution pairs
and exact compatible coding labels, with applicability and evidence limits recorded.

Display preferences are explicit MediaMuster product decisions. A format can record
different names in different places without prescribing which name our table shows.
The approved per-property preferences below are such decisions, not invented Avid
requirements. Changing a preference cannot establish an unknown encoding, authorize
a wrong-owner join or make incomparable facts interchangeable.

## Completeness requirement and user decisions

The current-priority matrix and illustrative tables are **reference only**, not a
list of allowed properties. Newly discovered fields still require investigation and
user review; current displayed values must not define which format evidence matters.

The approved [source lifetimes](source-lifetimes.md) supersede the original goal of
retaining every source record. Normal PMR/MDB/MXF/OMF scans keep all supported
observations and alternatives, their original value bytes, property/object
identifiers, source receipts, read states and selection evidence, plus associations
needed by the application. Full source copies, unused graphs and unknown/unprojected
properties are temporary reading storage. Loaded AVB graphs remain available because
bin filtering, reference resolution and enrichment use them.

A single display cell must not flatten away supported observations of multiple
tracks, descriptors, objects or occurrences. Normalized semantic fields link back
to their retained source observations. An unsupported input to a defined field
keeps its coverage/read reason; it is not proof that the property is absent. Do not
invent a meaning or manufacture absent-field results for an unenumerated structure.
Investigation can inspect unknown/private records directly from the source for review;
normal scan RAM is not a permanent archive of those unused records. Picture/audio
essence payloads are not loaded as metadata values.

When additional properties or previously unsupported value meanings are discovered:

1. Record what the source actually stores and where it occurs in investigation
   evidence; inspect the source directly when complete original records are needed.
2. Establish the format/object role and interpretation from available evidence;
   keep uncertainty explicit rather than presenting a guess as established.
3. Ask the user how the finding should be represented, named, associated and selected
   before adopting a new semantic/display-selection policy. Present plain-language
   examples, evidence and a recommendation; group related discoveries sensibly.
4. Continue independent collection/research while that choice is pending. Document
   unresolved findings without inventing semantics. Retain existing supported
   observations and their uncertainty; unused unknown records need not remain in
   normal scan RAM while a future field is being considered.
5. Add the agreed mapping/rule and a meaningful coverage check to Project Canon.

Track coverage per format/revision/object/property and explicitly report what remains
unsupported. A finite corpus cannot establish every historical/future private
extension. Adding an agreed field must preserve existing supported observations and
use the shared selection model. Information not extracted during a normal scan
requires another read; it cannot be recovered from a discarded original graph.

## Separate collection from selection

Readers collect typed observations and their source/property context. They do not
destructively overwrite one another's observations to implement display priorities.
A shared resolver applies per-field policy and produces the selected value and its
supporting observation references. The existing table/filters/CSV consume that result.

```text
PMR reader ------+
MDB reader ------+--> retained MetadataObservations --> resolveProperty
Header reader ---+                                       |
Loaded AVB ------+                         PropertyPolicy table
                                                        |
                                                        v
                                             ResolvedField for MediaFile
                                              value, evidence, reason
                                                        |
                                                        v
                                           existing display/filter/export
```

This is an internal developer-controlled policy table, not a new user-facing editor,
new persistent database, or a requirement for a "Why this value?" UI in v1.

Compression naming has its own [shared catalogue](compression-name-catalogue-2026-10-08.md).
It translates one source's established format facts into a readable name before
this policy chooses between sources. Editing a name mapping does not change source
priority or authorize additional reads. Supported identifier/descriptor observations
and their original value bytes remain retained; uncertain variants receive only an
established general name.

## Two different tables

An observation table shows what was collected for one record. A policy table tells
the resolver how to choose. Do not encode illustrative letters or literal displayed
values in the policy.

Illustrative observations for `Compression` (not an observed real-media disagreement):

| Source | Retained observation | Read outcome |
| --- | --- | --- |
| PMR | No codec value | `Absent`, reason `NotStoredByFormat`; no invented value |
| MDB descriptor | Compression B | `Present` |
| Media header's relevant descriptor | Compression C | `Present` |
| Loaded AVB | Current AVB metadata fallback does not supply codec | No invented observation |
| Selected result | Compression C, linked to its header observation | Competing MDB observation retained |

An unattempted header/bin read must remain `NotRead`, not `Absent`. Known omission
by the format is `Absent` with its reason, as for PMR codec/sample rate. A source that
does not expose this field through the current reader is not automatically evidence
that the underlying format lacks the property. Property-read state, source availability,
field applicability and reader/format capabilities are different; retain scope and reasons.

Every MediaFile must expose every defined metadata field in its logical table, even
when no value is available or the field is not applicable. Do not implement this as
only a list of successful observations. Shared immutable definitions and implicit
documented defaults may provide complete logical coverage without allocating redundant
empty per-source value objects for each physical record.

Summary of the implemented principal display priorities:

| Field | Preferred eligible source | Fallback | Special requirements |
| --- | --- | --- | --- |
| Compression | Selected media-header descriptor | Eligible MDB descriptor | Correct file identity; usable typed value; descriptor context |
| Bit depth | Selected media-header descriptor | Eligible MDB descriptor | Keep sample encoding separate; preserve competing evidence |
| Clip name | Selected material/master-package name | MDB master name, then agreed loaded AVB master name | Do not substitute physical/source names; equal-priority handling explicit |
| Project | PMR entry | MDB master, MDB file, then usable header project | Join must refer to the right file/object |
| Original bin | MDB master | Supported OMF header, then agreed AVB fallback | Availability differs by source/parser; retain editorial context |

These are display preferences, not a format specification. Ownership, interpretation
and eligibility require separate evidence. The resolver does not impose a blanket
"header always wins" policy.

The compiled policy table is the current source of display priorities. Ownership,
format interpretation and header-read scheduling remain separate decisions.

## Code terminology and responsibilities

- `MediaProperty`: the existing typed catalogue of semantic properties; do not add
  a second `MetadataField` enum for the same purpose.
- `PropertyPolicy` and `prefer(...)`: the shared per-field rows and source groups
  in `metadataselectionpolicy.cpp`. `SourceRanks` holds their internal priorities.
- `resolveProperty`: selects from retained observations using one policy row.
- `ResolvedField`: stores the result, selected observation index, semantic field
  name, reason and agreement status. Its `rule` label comes from
  `mediaPropertyName(policy.property)`; there is no rule version or custom rule ID.

`NewDnx`, `OldDnx` and `ReallyOldDnx` are the agreed typed semantic fields for the
three DNx naming schemes. Use those standard names directly. Retained source
observations keep their original property names and object identifiers; a semantic
field does not rename the recorded property. `CompressionLabel` holds the normalized
coding UL used to interpret the human-readable `Compression` value. It can be
derived from `DIDResolutionID` or AUID normalization; its supporting observations
retain their original value bytes. It is an internal evidence field, without a
direct table column. `SourceContainer` describes an import/source-container tag;
`WrappingLabel` carries the essence-container UL. Keep those different facts separate.

Use `Compression` for the table and CSV heading, `MediaProperty::Compression`
for the selected semantic property, and `MediaFile::compression` for the displayed
value. This replaces the former `Codec` names. Recorded source-property names and
codec identifiers remain unchanged; the rename does not change their interpretation.

The coder edits one policy definition to alter source priority for a field. Fields
with genuine special logic use a typed `SelectionRule` implemented in one place;
do not force every ambiguity into a simplistic ordering list or distribute the
same decision among parsers, scanner, table and export code.

A rule should specify: subject/field, source/object roles, usable-value conditions,
identity match requirements, relevant freshness requirements, preferred/fallback
sources, tie/conflict handling, and an explanation linked to the field and inputs.
Preserve `PropertyReadState`, `PropertyAgreement`, recorded/derived basis and freshness as
separate facts. No eligible value means unresolved, not a guessed default.

Changing policy can recompute selected values from retained observations without
rereading the sources, provided those observations cover the new rule's needs.
Missing source coverage requires another read; a policy cannot recover information
that was never collected. Apply the same current policy to dependent table,
filter and export results. A selection update does not change KelpieIds or fold rows.

## Shared C++17 policy table

The [header](../src/mediaengine/metadataselectionpolicy.h) and
[implementation](../src/mediaengine/metadataselectionpolicy.cpp) define one
developer-controlled constant table. Its 41 explicit rows cover every defined
`MediaProperty`, including properties kept only in RAM. `MediaProperty::Count`
sizes the array; compile-time validation requires exactly one row in catalogue
order and priorities from 0 through 3. Missing, duplicated or misplaced rows fail
validation rather than receiving a default rule.

Four illustrative rows are shown below. Read the named source groups from left
to right; no numeric source positions or preference legend are needed. The linked
[production table](../src/mediaengine/metadataselectionpolicy.cpp) is the complete
41-row definition; this excerpt does not duplicate it:

```cpp
using Source = MetadataSource;

// Four illustrative rows; the linked production table contains all 41.
constexpr PropertyPolicy examplePolicies[] =
{
 // Clip name: header first, MDB second, loaded AVB third.
 {MediaProperty::ClipName, SelectionRule::PreferredValue,
  prefer({Source::Mxf, Source::Omf}, {Source::Mdb}, {Source::Avb})},
 // Project: PMR first, MDB second, header third.
 {MediaProperty::Project, SelectionRule::PreferredValue,
  prefer({Source::Pmr}, {Source::Mdb}, {Source::Mxf, Source::Omf})},
 // Original bin: MDB first, header second, loaded AVB third.
 {MediaProperty::OriginalBin, SelectionRule::PreferredValue,
  prefer({Source::Mdb}, {Source::Mxf, Source::Omf}, {Source::Avb})},
 // Master associations: retain every eligible association, rather than one winner.
 {MediaProperty::MasterMobId, SelectionRule::MasterAssociations, {}}
};
```

The first group is first choice, the second is fallback, and the third is the
final fallback. Sources in one group have equal preference; incompatible values
at the preferred available group remain unresolved. A source left out cannot
supply the selected value for that row.

The small C++17 `prefer(...)` helper produces the resolver's existing `SourceRanks`
at compile time. Numeric priorities are an internal representation rather than
something a coder needs to look up when editing a policy row.
The OMF source kind also covers the legacy reader's native WAV/AIFF observations;
the retained container/property context still distinguishes them.
Original observations and read states remain retained for omitted sources.
Agreement still compares their qualified Present observations; an omitted-source
observation can remain Present and SingleSource while its selected display value
is blank. Omission from preferences is not proof of format absence.
`MasterAssociations` gathers every eligible association without a preference list.

Source groups select displayed values; they are not read-order commands. Database-first
scheduling and required-field coverage still control media-header reads. A priority
cannot recover evidence from an unopened source or establish an unknown interpretation.

The same preferences can be presented to maintainers as:

| Property | First choice | Fallback | Final fallback | Selection |
| --- | --- | --- | --- | --- |
| Clip Name | Media header | MDB | AVB | Preferred comparable value |
| Project | PMR | MDB | Media header | Preferred comparable value |
| Original Bin | MDB | Media header | AVB | Preferred comparable value |
| MasterMobId | All eligible recorded associations | — | — | Retain the complete association set |

Those rows summarize the table rather than limiting source retention. Physical
facts such as current path/size use this file's filesystem evidence. KelpieId is a
scan-session identity rather than a competing Avid value; its allocator does not
participate in source ranking. Derived metadata uses a typed rule with retained
supporting observations and an explanation. File durations retain their units/rate
and a separately selected compatible display clock. The compatible clock uses the
File Duration row's own source ranks,
not the independent Frame Rate preference. Clip durations retain master/track
contexts through the resolver's equivalence and conflict checks. Effects are derived
after the final Clip Name and Type selection and retain the name observation's
actual MDB/MXF/OMF/AVB snapshot; those source kinds share one preference group in
the effect rows.

The implemented shared API is:

```cpp
const PropertyPolicy &propertyPolicy(MediaProperty property);
const PropertyPolicies &propertyPolicies() noexcept;
ResolvedField resolveProperty(const MediaEvidence &evidence,
 const PropertyPolicy &policy);
void selectMetadata(MediaEvidence &evidence);
bool applyResolvedMetadata(::MediaFile &file);
```

The policy accessors and resolution functions are in `MediaEngine`;
`applyResolvedMetadata` is the presentation helper in
[MediaEngine adapter](../src/mediaengineadapter.h). `MediaEvidence::resolve` accepts a callable
for table-backed ranking, retaining its equivalent-value and unresolved-conflict
rules. Ownership, association and freshness qualification remain separate from
ranking. Checked lookup throws `std::out_of_range` for an invalid property enum.
Dispatch uses typed rules/properties. The selected result's `rule` is the existing
semantic property name, rather than a separately maintained rule-name tag.

Both scan selection and bin enrichment call the shared policy. MediaEngine-backed display
rows then refresh from the final selected results, including provenance flags and
clearing unresolved values. `applyResolvedMetadata` returns whether a semantic row
value changed and preserves physical row identity, filesystem/volume/family details
and operation stamps. Compatibility-only callers retain their separate fill-only
behaviour. Dependent effects are recomputed before refreshing table, filter and CSV
values, so a changed preference cannot leave an older nonempty semantic cell behind.

The per-file structure remains the existing `MediaEvidence`: observations include
source snapshot, property/object, original value, decoded value, read state, basis,
freshness and qualification. `ResolvedField` currently holds the selected value, one
`selectedObservation` index, semantic property name, reason and agreement. Aggregate
rules must retain provenance for all included values; multiple selected support references
would be a focused extension if required, not an already implemented capability.
Do not copy the policy table into each MediaFile or constrain each property to one
observation per source kind; several databases, bins or objects can contribute
observations of that kind.

The user confirmed developer edits for a new build: keep the preferences in compiled
C++ and apply them after rebuilding. No rule-version tracking, custom rule-name
strings, runtime preference editor or persistent configuration store is required.
Future live editing is outside this approved scope.

## Retention shape

Each `MediaFile` must provide access to all observations relevant to its selected
fields, directly or by links to shared scan RAM collections. Do not require a fixed
one-value-per-PMR/MDB/header/AVB layout: there can be multiple loaded bins, descriptors,
database snapshots and competing observations from the same source type.

Preserve the distinction between underlying recorded values, decoded/derived values
and display formatting. Two labels for the same codec need not be a semantic conflict;
comparison requires typed, equivalent context. Keep original encodings where needed
to explain or correct interpretation later.

## Acceptance checks

- Capture today's selected metadata for controlled fixtures and representative real
  files. Compare the refactored results field by field with identical source coverage.
- Test each fallback and invalid/unreadable/preferred-source case meaningfully.
- Change one policy priority in a controlled check: selected value and its reason
  change while raw observations, physical rows and KelpieIds remain unchanged.
- Conflicting values, multiple observations from one source kind and ambiguous
  object references remain inspectable in RAM with the detail UI absent.
- Verify filters and exports use the same resolution as the table.
- Verify complete defined-field coverage for audio/video records, PMR format-absent
  fields, unattempted sources and unsupported interpretations; no state is inferred
  solely from an empty display string.
- Flag deliberate format-correctness changes separately from compatibility-preserving
  restructuring, with evidence supporting the correction.
