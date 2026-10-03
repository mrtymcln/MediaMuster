# Metadata selection policy

Status: proposed architecture following the user's request for a central table that
chooses displayed values from retained source observations. The refactor should give
users the same information as today's app while organising collection and selection
more clearly. Intentional corrections to existing behaviour must be identified and
validated separately rather than hidden inside the architectural refactor.

## Completeness requirement and user decisions

The current-priority matrix and illustrative tables are **reference only**, not a
list of allowed properties. The user requires the new matching/selection model to
reflect source properties and values 1:1, including fields that current readers miss.
Do not preserve current displayed results by excluding additional source evidence.

For PMR/MDB/MXF/OMF/AVB, preserve property identity, subject/object context, source
snapshot/locator, repeated values and ordering where meaningful, original type and
encoding where needed, decoded value when supported, and recorded references. A
single display cell must not flatten away multiple tracks, descriptors, objects or
occurrences. Normalized semantic fields link back to those source observations.

Unknown/private properties need a raw/uninterpreted representation rather than silent
dropping or invented semantics. For an unsupported structure whose properties cannot
yet be enumerated, retain its locator/available raw evidence and report incomplete
coverage; do not manufacture a list of absent fields. Metadata completeness is not
a requirement to load picture/audio essence payloads into RAM as metadata values.

When additional properties or previously unsupported value meanings are discovered:

1. Preserve what the source actually records and where it occurs.
2. Establish the format/object role and interpretation from available evidence;
   keep uncertainty explicit rather than presenting a guess as established.
3. Ask the user how the finding should be represented, named, associated and selected
   before adopting a new semantic/display-selection policy. Present plain-language
   examples, evidence and a recommendation; group related discoveries sensibly.
4. Continue independent collection/research while that choice is pending. Retain the
   observation as uninterpreted or unresolved; do not silently ignore or choose it.
5. Add the agreed mapping/rule and a meaningful coverage check to Project Canon.

"Every possible property/value" is the completeness objective, not a claim that a
finite current corpus or the existing parsers enumerate every historical/future
private extension. Track coverage per format/revision/object/property; explicitly
report what remains unsupported. Future extension must be possible without discarding
old observations or rebuilding every MediaFile/UI consumer. Normal reads retain actual
values encountered, not every hypothetical value in a property's allowed domain.

## Separate collection from selection

Readers collect typed observations and their source/property context. They do not
destructively overwrite one another's observations to implement display priorities.
A shared resolver applies per-field policy and produces the selected value and its
supporting observation references. The existing table/filters/CSV consume that result.

```text
PMR reader ------+
MDB reader ------+--> retained MetadataObservations --> MetadataResolver
Header reader ---+                                       |
Loaded AVB ------+                  MetadataSelectionPolicy table
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

## Two different tables

An observation table shows what was collected for one record. A policy table tells
the resolver how to choose. Do not encode illustrative letters or literal displayed
values in the policy.

Illustrative observations for `Codec` (not an observed real-media disagreement):

| Source | Retained observation | Read outcome |
| --- | --- | --- |
| PMR | No codec value | `Absent`, reason `NotStoredByFormat`; no invented value |
| MDB descriptor | Codec B | `Present` |
| Media header's relevant descriptor | Codec C | `Present` |
| Loaded AVB | Current AVB metadata fallback does not supply codec | No invented observation |
| Selected result | Codec C, linked to its header observation | Competing MDB observation retained |

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

Illustrative policy table starting from the current principal display priorities:

| Field | Preferred eligible source | Fallback | Special requirements |
| --- | --- | --- | --- |
| Codec | Selected media-header descriptor | Eligible MDB descriptor | Correct file identity; usable typed value; descriptor context |
| Bit depth | Selected media-header descriptor | Eligible MDB descriptor | Keep sample encoding separate; preserve competing evidence |
| Clip name | Selected material/master-package name | MDB master name, then agreed loaded AVB master name | Do not substitute physical/source names; equal-priority handling explicit |
| Project | PMR entry | MDB master, MDB file, then usable header project | Join must refer to the right file/object |
| Original bin | MDB master | Supported OMF header, then agreed AVB fallback | Availability differs by source/parser; retain editorial context |

This table is not the final verified specification. Eligibility and conflict rules
must reproduce or deliberately correct the actual field behaviour documented in
[current matching and selection](current-matching-and-selection.md). The resolver
must not impose a blanket "header always wins" policy.

The expanded [current source priority matrix](current-matching-and-selection.md#source-priority-matrix)
records the inspected implementation, including its nonempty/positive-value gates,
identity replacement, recovery paths and flags that only become true. It is the
compatibility baseline to review, not automatic endorsement of every existing rule.

## Proposed code terminology and responsibilities

- `MetadataField`: identifies a semantic field such as codec, bit depth, or project.
- `MetadataSelectionPolicy`: the shared per-field rule collection; keep it in code
  for v1, using typed source roles and rule identifiers rather than arbitrary strings.
- `MetadataResolver`: selects from retained observations using that policy.
- `ResolvedField`: stores the result, selected/supporting observation references,
  policy version/rule identifier, reason and agreement status.

The coder edits one policy definition to alter source priority for a field. Fields
with genuine special logic can refer to a named rule implemented in one place;
do not force every ambiguity into a simplistic ordering list or distribute the
same decision among parsers, scanner, table and export code.

A rule should specify: subject/field, source/object roles, usable-value conditions,
identity match requirements, relevant freshness requirements, preferred/fallback
sources, tie/conflict handling, and a reproducible reason identifier. Preserve
`PropertyReadState`, `PropertyAgreement`, recorded/derived basis and freshness as
separate facts. No eligible value means unresolved, not a guessed default.

Changing policy can recompute selected values from retained observations without
rereading the sources, provided those observations cover the new rule's needs.
Missing source coverage requires another read; a policy cannot recover information
that was never collected. Apply one coherent policy version to dependent table,
filter and export results. A selection update does not change KelpieIds or fold rows.

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
