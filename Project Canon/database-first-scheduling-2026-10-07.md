# Database-first scheduling — 7 October 2026

The user confirmed this policy: **read the databases first; read a media header
only when there is no usable database match, or required table metadata is
missing or conflicting.** This replaces the initial Canon connection's practice
of opening every media header. It does not impose a memory cap.

## When the header is needed

[The Canon scheduler](../src/canon/scanengine.cpp) first reads discovered PMR/MDB
sources. A skip requires a complete, unchanged local PMR with an unambiguous
filename-to-FileMobId match, joined to a complete, unchanged MDB file projection.
Master metadata alone is insufficient. Exact filenames take precedence over
normalized spellings; a normalized spelling cannot identify two distinct files.
An unreadable, ambiguous or unusable match falls back to the header.

The following are the implemented sufficiency checks after selecting eligible
database observations. They describe scan scheduling, not the complete property
catalogue.

| Applicability | Required selected metadata |
| --- | --- |
| Every media file | Clip Name, Project, Codec, Bit Depth, FileMobId, at least one MasterMobId, known Kind and Type, and File Duration. |
| Video | Resolution and a valid exact Frame Rate. |
| Audio | A valid exact Sample Rate. |
| Precompute, while `kPrecomputeFilter` is enabled | A known Precompute Category. |
| While `kClipDuration` is enabled | A nonempty set of clip-track durations, each with nonnegative units and valid exact and display rates. |

File Duration likewise needs nonnegative units and valid exact and display rates.
Kind must be Audio or Video; Type must be Media or Precompute. Unknown values do
not satisfy these checks. The implementation uses the existing feature-flag
symbols above; the user-facing planned terminology remains `PrecomputesFilter`.

Original Bin, Source Filename and effect details may legitimately be absent.
Their absence does not force a header read, but an unresolved conflict does.
Filesystem creation time may be unavailable. Internal Sample Format and Channels
do not gate header reads. A lower-priority alternative does not force a read when
the approved selection rules already resolve the displayed value.

Selection priority and reading priority are separate. Among sources actually
read, Clip Name still prefers header → MDB → AVB; Project prefers PMR → MDB →
header; Original Bin prefers MDB → header → AVB. Technical metadata prefers the
header over eligible MDB observations. An unopened header supplies no competing
observation. Raw evidence from sources that were read remains retained.

## Honest receipts and changes during a scan

Each discovered source gets a receipt. A deliberately unopened header stays
`ParsedSource::Outcome::NotRead` and `SourceReadState::NotRead`; it is not recorded
as absent, successfully parsed or checked against the database. Database
freshness remains **Unknown**. Matching names and Avid identities, or unchanged
filesystem timestamps during this scan, do not prove that a database describes
the current file. No interpretation of the unidentified PMR timestamp word is
used to claim freshness.

`ParsedSource::readReason` records the decision for audit/measurement without
adding a Console message for every file:

| Decision | Recorded reason |
| --- | --- |
| Database read | Read database before deciding which media headers are needed. |
| Header skipped | Usable database match supplies required table metadata; database freshness remains unknown. |
| Header fallback | No usable database match; required table metadata missing/conflicting with the property names; or physical file changed after discovery. |

Sources are checked again after scheduling, including unopened media. If a
database change invalidates an earlier skip, the scheduler makes **one bounded
fallback pass** over skipped headers, then checks sources again. Continued
changes are reported and excluded from selection; the scan does not retry
indefinitely. Cancellation remains an incomplete scan. `parsingComplete` means
the scheduled read/skip decisions finished, not that every header was opened.
Physical copies retain separate rows and KelpieIds. Missing local database
references and cross-location matches remain reconciliation results.

## OMF 1 sequence-backed clip durations

Real MDB master tracks often reference `SEQU` objects without their own
`CLIP:Length`. The old projection missed these recorded durations, unnecessarily
forcing header reads. The original [OMF Toolkit's `ommobget.c`](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/ommobget.c)
provides the rule: `Get1xSequLength` sums each sequence component occurrence and
subtracts transitions. `omfiComponentGetLength` obtains OMF 1/IMA clip lengths
from `CLIP:Length`, track-group lengths from `TRKG:GroupLength`, and sequence
lengths through that calculation. The locally inspected toolkit source contains
these functions at lines 1206–1240 and 1275–1310 respectively.

[Canon's projection](../src/canon/omfprojection.cpp) now follows that rule for
supported components, requiring complete references, established lengths and
equivalent exact component clocks. It guards cycles, overflow and negative
totals; repeated references count once per occurrence. Transition lengths use
their recorded `TRKG:GroupLength`. It does not substitute an owning master's
GroupLength for an unresolved track. The OMF 2 explicit-length path is unchanged.

`DurationComponents` retains the contributing object handles, property names,
units, rates and addition/subtraction contributions, alongside `DurationObject`
and `DurationProperty`. [Clip Duration comparison](../src/mediaevidence.h) compares
the master/track identities and timing facts independently of source-local object
numbers and list order. Equivalent rational rates agree without rewriting either
observation's original rate. Identical master/track/timing facts are coalesced in
the selected list; source observations retain every repeated entry and its
provenance. Different masters and tracks remain separate. Contradictory timing
for the same master and track makes the selected field conflicting and unresolved,
including when both versions occur in one source. This coalescing does not change
the sequence calculation: repeated component occurrences still contribute to
length individually.

A real legacy Tone MDB contains two master objects with the same MobID, each
recording track label 1 and 1500 units at 25/1. These produced duplicate display
entries before selected-value coalescing. The [compact source evidence](evidence/clip-duration-database-evidence-2026-10-07.json)
preserves both observations and the source fingerprints.

Track numbers also need their source context. For
`A01.E6966CE6_A3C580A3C589AA.mxf`, the MDB records `TRAK:LabelNumber = 1`; the MXF
records `GenericTrack.TrackID = 2` and `GenericTrack.TrackNumber = 1` for the same
master/file association. The toolkit's `omfiTrackGetInfo` reads both LabelNumber
and TrackKind before converting OMF 1 numbering through `CvtTrackNumtoID`.
Canon currently presents the recorded source number under the existing Track
label; it does not invent a cross-format mapping. Its Clip Duration list includes
tracks explicitly referencing this physical file. The older parser could include
all material-package tracks once the package contained any reference to the file.

## Focused evidence and limits

These are read-only MDB projection checks, **not whole-scan performance results**:

| MDB | Projected file objects | With Clip Duration before | After |
| --- | ---: | ---: | ---: |
| `/Volumes/EDIT/Avid MediaFiles/MXF/1/msmMMOB.mdb` | 392 | 82 | 359 |
| [SupportingFiles fixture](../tests/fixtures/omf/avid_supporting/msmMMOB.mdb) | 80 | 77 | 80 |

The fixture has three sequence-backed results. One inspected EDIT example has
master 68160, track 68133 and sequence 68131: recorded component lengths
`0 + 4857 + 0` at `24/1` yield 4857 units. The component evidence is retained.

An inspected remaining EDIT case references private `RSET` components. Although
they record a GroupLength, this narrow calculation does not assume their
inheritance or timing semantics. Their properties remain retained and the header
fallback remains available. This example does not classify all 33 remaining
missing durations.

Focused validation passed: [Canon scan tests](../tests/tst_canonscan.cpp),
19 passed and one filesystem-dependent skip; [OMF projection tests](../tests/tst_canon_omfprojection.cpp),
58 passed. Cases cover actual skipped headers, missing/conflicting metadata,
source changes after a skip, equivalent and contradictory durations, sequence
transitions, nested/repeated components, incomplete references, clocks, cycles
and overflow. `genuineDatabaseSequenceDurations` exercises the real fixture.

See [retained-memory changes](database-first-and-memory-2026-10-07.md) for the
separate allocation work and [the live-connection report](live-connection-2026-10-04.md)
for integration context. The final whole-volume comparison is reported
separately; intermediate scan measurements are not presented here as final.
