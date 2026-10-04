# Fresh Canon AVB reader

Recorded 4 October 2026. This document describes the new bin reader work and
its evidence. The user confirmed **engine first**: implement and verify the
reader and resolver now; do not change the live UI in this stage. Both engines
are implemented, built and tested. The existing production `AvbParser` remains
a separate implementation. Live filtering, physical-file matching and the new
picker are subsequent integration work.

## Purpose and boundaries

An AVB is a graph of bin objects and references. Reading every locator in a bin
cannot establish which media belongs to one selected sequence. The fresh reader
therefore keeps the graph first; the sequence resolver follows that graph, and a
later matching step associates the resulting file identities with scanned
physical-file rows.

```text
Explicitly loaded AVB
    |
    v
AvbReader
    +-- bin entries and source-local object numbers
    +-- raw and decoded properties, with byte positions and text encodings
    +-- full recorded MobIDs and original encodings
    +-- native object references and source-Mob references
    +-- unsupported fields/tails and incomplete-read explanations
    |
    v
Sequence resolver: Entire bin OR Selected sequences
    +-- all referenced group angles
    +-- renders AND their source inputs
    +-- muted/disabled tracks included
    +-- evidence of how each terminal identity was reached
    |
    v
Match against scanned media, preserving every physical copy
    |
    v
Explicit Intersect / Add / Subtract
```

In the planned UI, loading a bin must not apply a filter. The agreed flow and dependency
policies are recorded in [sequence selection](avb-sequence-selection.md).
The user subsequently approved **allowing usable partial results with a persistent
warning and Console details**, superseding blanket blocking. Keep completeness
truthful; an unreadable bin, invalid selection or cancelled operation cannot be
made usable by a warning. See the current [release gate and partial-result policy](avb-sequence-selection.md#release-gate-and-partial-result-policy).
A bin object is not an inventory row or a KelpieId. Several physical files can
match one recorded Avid identity and must remain separate rows.

## What the reader retains

- Original object class, file location, native object number and source ownership.
  Object numbers are meaningful within their AVB; matching equal numbers from
  different bins would be incorrect.
- Individual property occurrences, including repeated attribute names, repeated
  extension values and inherited descriptor sections. An attribute list is an
  ordered collection, not a dictionary that overwrites repeated names.
- Scalar bytes and decoded values where their grammar is established. Exact
  rate mantissa/exponent pairs remain available rather than only a rounded float.
- Legacy and modern text fields independently, such as `FileLocator.path` and
  `FileLocator.path_utf8`, or `BinRef.name` and `BinRef.name_utf8`. `TextEncoding`
  describes each field's decoding. The reader does not replace the legacy value
  with the Unicode value or choose which one should be displayed.
- Original MobID/UUID bytes alongside the reader's canonical identity form.
  Native AVB object indices and explicit dependency MobIDs remain distinct kinds
  of reference. SourceClip and AudioSuite source-master links have verified
  dependency meanings; a property being a MobID does not by itself give it that
  meaning. Equal-looking names are not identity evidence.
- Byte ranges and an explicit explanation for layouts the reader cannot decode.
  A recognized field is bounded by its enclosing object; an unknown extension
  does not authorize guessing the width of the following fields. Unknown whole
  object classes can remain range-only evidence instead of loading their payloads.

The descriptor grammar describes media facts; it does not decide a displayed
codec, naming priority, conflict winner, file duplicate policy or sequence
membership. Those decisions belong to the appropriate later stages.

## Descriptor and supporting-object coverage

The fresh descriptor grammar covers the following source layouts. A listed class
means a grammar is implemented; it does not mean every possible version or
extension of that class has been verified.

| Family | Classes |
| --- | --- |
| Base and physical-media descriptors | `MDES`, `MDTP`, `MDFM`, `MDNG`, `MDFL`, `MULD` |
| Audio descriptors and summaries | `WAVE`, `AIFC`, `PCMA`, `MPGA` |
| Image descriptors | `DIDD`, `CDCI`, `MPGI`, `JPED`, `RGBA` |
| Data descriptors | `DATD`, `ANCD` |
| Locators and bin/Mob references | `FILE`, `WINF`, `URLL`, `MSML`, `MCBR`, `MCMR` |
| Position and marker objects | `APOS`, `ABOB`, `DIDP`, `MPGP`, `TMBC` |
| Attributes and parameter lists | `ATTR`, `PRLS`, `TMCS`, `PRIT`, `AVUP`, `FXPS` |
| Graphics and settings | `GRFX`, `SHLP`, `CCFX`, `TKMN`, `TKDS`, `TKPS`, `TKDA`, `TKPA` |

Component, track-group and bin-root grammars are separate parts of the reader.
Supporting an attribute or effect-settings object means retaining its recorded
fields or opaque data, not interpreting every vendor plug-in payload inside it.

Specific boundaries are intentional:

- Nonempty RGBA palette variants remain unsupported; their layout is not guessed.
- The `MediaDescriptor.wchar` extension remains raw because the consulted grammar
  does not establish enough encoding detail to select a text decoder confidently.
- RIFF/WAVE and FORM/AIFF summary sizes use the embedded format's byte order,
  independently of the surrounding bin's byte order.
- The consulted `DATD` reader and writer do not add a terminal `0x03` for a direct
  DATD object. Do not add one merely to make it resemble adjacent classes.
- New or unsupported extensions preserve the remaining object evidence and
  qualify the read. They must not turn into an empty, apparently complete result.

## PCMA extension 02

The supplied pyavb reader knows PCMA extensions 1 and 3 but rejects extension 2.
This occurred in the real sequence-bin investigation. Native Media Composer
methods and the recorded bytes establish the following additional grammar:

```text
01 02              extension marker, extension number 2
47                 signed-32 property type marker (decimal 71)
<4 bytes>          subframe alignment, in the bin's scalar byte order
```

The following `03` in the observed examples terminates the descriptor; it is not
part of the alignment value. For example, object 90808 in `01_SEQ.avb` records
`01 02 47 8a 07 00 00 03` at file byte 7,519,427, giving alignment value 1,930 in
that little-endian bin. A bounded search of PCMA object bodies found this form
in 270 objects. The subsequent grammar probe consumed all 363 PCMA objects in
that bin without leftover bytes.

The arm64 native implementation in `libameLibrary.dylib` independently provides
the meaning and width:

- `PCMAudioDescriptor::Get(AStream*, AIODesc*)`, beginning at `0x3e8b8c`, dispatches
  extension 2 at `0x3e9088`. It uses the same stream read slot as signed-32 fields,
  sign-extends the 32-bit result, then calls
  `AudioMediaDesc::SetSubframeAlignment(long long)` at `0x3e90b4`.
- `PCMAudioDescriptor::Put(AStream*, AIODesc*)`, beginning at `0x3e86d4`, obtains
  subframe alignment at `0x3e8a90`, emits extension 2, and uses the same stream
  write slot as the descriptor's 32-bit fields through `0x3e8ad8`.

The new field is retained as `PCMADescriptor.sub_frame_alignment`. No display
column, sample-rate conversion or editing policy follows from this discovery.
These are native-implementation and specimen findings, not a claim of a published
Avid format specification or proof about every historical release.

Archived evidence:
[native-library receipt](evidence/avb-pcma-native-evidence-2026-10-04.json),
[observed PCMA objects and offsets](evidence/avb-pcma-extension2-observations-2026-10-04.json).
The native receipt includes a SHA-256 of the inspected library. The bin observation
receipt identifies paths, object numbers and byte positions; it does not claim a
cryptographic identity check of those bins.

## Source-reference terminals and dependency meanings

The original sequence probe reported two unresolved, sentinel-shaped IDs. Native
Avid functions now establish their meaning. The exact bytes below use the Canon
material-UUID-little-endian representation; the original serialized bytes remain
separately retained.

| Recorded identity | Meaning established by native evidence |
| --- | --- |
| `060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80` | Avid null MobID: no further source-Mob dependency |
| `060a2b340101010101010f00130000000000000001000000060e2b347f7f2a80` | Avid filler MobID: a filler terminal, not a missing media file |

In the inspected arm64 library, `IsNullMobID` at `0x2353dc` checks native material
numeric fields at bytes 16–23 for zero. `IsFillerMobID` at `0x235d94` constructs
the second exact identity above and compares it using `EqualMobID`. Literal
bytes at `0x10ec848` and `0x10ec850` establish the Avid tail and prefix. These
findings support the two observed full IDs without requiring a search for a
nonexistent target composition.

Canon deliberately recognizes those **exact full null/filler identities**, plus
an explicitly recorded all-32-bytes-zero ID. Preserve the original identity and
the terminal reason. Do not copy the native broad zero-material test to arbitrary
non-Avid identifiers, and do not treat every ID accepted by the native
`IsWellKnownMobID` helper as a terminal: that helper alone does not establish the
meaning of the other reserved values.

For related primary-format context, AAF distinguishes an explicit zero SourceID
(original-source terminal) from an omitted SourceID (reference within the same
Mob). This does not turn an absent AVB extension into a null or a same-Mob link;
AVB must be interpreted using its own established grammar. See
[AAF Object Specification 1.0.1, sections 7.6–7.7, printed pages 45–46](https://aafassociation.org/html/specs/aafspec-v1.0.1.pdf).

AudioSuite has another explicit dependency. The native extension description
names ASPI extension 1 `SourceMasterClipOMFMobID`, extension 8
`SourceMasterClipAAFMobID`, and extension 7 as a retired symbol. The native
property string is `OMFI:ASPI:sourceMasterClipMobID`. `AudioSuitePlugInEffect::Put`
writes the extension-8 field through `Write_AAFMobID` at `0x7b1030`. This establishes
`AudioSuitePluginEffect.mob_id` as a source-master link, which belongs in the
approved source-input traversal. Its target need not be a SourceMob; it can be a
MasterMob. The ASPI field is not itself proof of a physical file or render locator.

Other MobID-valued fields must not inherit that dependency meaning:

- The real `01_SEQ.avb` probe found 141 `MCMR` objects reached through `_MATCH`
  and `_SYNC` attributes (99 and 42 respectively). Ninety-six of their IDs resolve
  to CompositionMobs in that bin. Those navigation/synchronization associations
  must not automatically pull additional compositions into the selected edit.
- `DIDP` positions are reached through `left_bob`, `right_bob` and track `bob_data`.
  Their MobIDs predominantly match SourceMobs in this specimen. Cached position
  evidence alone does not establish another edit-dependency rule.
- Marker/MobRef/Position IDs remain recorded evidence. No blanket rule follows
  them merely because their bytes decode as a MobID.

Archived evidence: [native receipt with SHA-256, addresses, file offsets and bytes](evidence/avb-reference-semantics-native-evidence-2026-10-04.json),
[reference-context probe](evidence/avb-mobvalued-links-probe-2026-10-04.py), and
[observed object IDs and parent fields](evidence/avb-mobvalued-links-observations-2026-10-04.json).
The probe contains no ASPI examples; the source-master meaning comes from native
evidence. Controlled resolver tests now exercise its modern and legacy-only
dependencies. The probe's
parent paths are observational, may include nested-object paths, and are not a
complete independent dependency oracle.

## Remaining resolution limits

- **Legacy SourceClip identities:** when only the legacy numeric words are
  present and the full typed MobID extension is absent, the words remain recorded.
  Legacy identity resolution is not implemented in this stage; do not fabricate
  a complete identity, silently classify it as null, or claim a complete graph.
- **Duplicate MobIDs:** loaded bins may contain several object occurrences with
  the same MobID, including repeated or differing snapshots across bins. Keep
  each source/object occurrence. An identity alone does not justify selecting
  the first candidate, merging the objects or treating a match as unique.
  Unsettled ambiguity must remain visible in the partial-result warning; do not
  promote uncertain candidates to confirmed matches.
- **Unknown dependencies:** retained raw fields are valuable evidence but do not
  establish dependency completeness until their relevant meanings are verified.
- **Physical matches:** a resolved file identity can match several scanned copies.
  This is separate from ambiguous bin-object resolution; all established physical
  matches retain their rows and KelpieIds.
- **Path-only media:** supported leaf file descriptors must supply an established
  MSML identity route. Multiple descriptors delegate to their children; physical
  base/tape descriptors and historical paths are not mistaken for managed files.
  The supplied `ROUGH` contains three multiple-descriptor groups with six leaf
  descriptors pointing through physical descriptors to `Scene04Rough.mp4`, an
  external linked source. Neither their leaves nor parents supply MSML locators.
  Its 232 resolved full media IDs are therefore a partial result. Under the
  revised policy this can feed a filter carrying the required warning when the
  live adapter is connected. No external MP4 scanning or path-based matching was added.

The sequence catalogue retains bin membership and `user_placed` separately.
It recognises bin-member CMPO objects with recorded mob type 1 and usage code 0,
not names or positional guesses. This is the supported sequence classification;
it does not claim every historical/private composition usage is selectable.
Sources are shared immutable parsed graphs. Object keys are source-index plus
native object number within that `AvbReferenceIndex`; do not reuse keys after
constructing a different index. Reachable edges retain their source relationship
index, so full inclusion paths can be reconstructed without duplicating every
path in RAM. An explicitly empty sequence selection stays empty.

## Checks completed and checks remaining

The descriptor implementation passed a C++17 syntax check against Qt 6.5.3 with
`-Wall -Wextra -Wconversion`. A separate read-only probe exercised the current
cursor and descriptor/misc grammars against the supplied `01_SEQ.avb` and
`02_SEQ_LOCK.avb` files. Every handled object was fully consumed, with zero
unsupported, malformed or leftover-tail results among those handled classes.
This includes all 363 and 325 PCMA objects respectively.

The probe intentionally did not treat other classes as supported. Its fixed
first-object offset of 119 was independently checked with pyavb for these two
files; it is a specimen-harness choice, not a production AVB framing rule.

See the [probe source](evidence/avb-descriptor-probe-2026-10-04.cpp) and
[per-class results and limitations](evidence/avb-descriptor-probe-results-2026-10-04.json).
The probe requires the normal Qt runtime environment. The sandboxed attempt
failed during Qt CPU-feature detection before it read the bins; the normal-host
read-only run completed successfully.

These checks establish agreement with the handled object boundaries in these
specimens. They do not establish all property meanings, all historical AVB
variants, complete reference resolution, scanned-file matches, filter safety,
UI behaviour, performance or RAM improvements. The broader checks below are
separate evidence, not conclusions drawn from that descriptor-only probe.

### Reader and resolver verification

See the [nine-bin corpus check](avb-corpus-check-2026-10-04.md) for the final
per-file results, sequence counts, measured costs and reproducible probe receipts.

- Universal arm64/x86_64 Debug application and test build: passed with Qt 6.5.3.
- Full CTest regression run: **35/35 suites passed**. After final cancellation
  guards and descriptor-coverage cases, both AVB suites were rebuilt and passed
  again. The old production bin-filter tests remain unchanged and pass.
  The final reader suite reports 35 passing cases and one optional external-input
  test skipped by default; those nine external inputs were separately exercised
  by the archived corpus probe. The final resolver suite reports 21 passing cases.
- Reader tests cover both byte orders, malformed/truncated input, hostile counts,
  invalid references and roots, encoding failures, duplicate attributes, source
  receipts/changes, cancellation and unknown extensions. A guarded synthetic
  2 GiB unknown payload verifies that opaque chunks remain range-only.
- Resolver tests cover selected/whole-bin scope and unions, duplicate sequence
  names, group angles without unrelated groups, renders plus inputs, disabled
  tracks, cycles, exact cross-bin identities, ambiguity, explicit null/filler,
  AudioSuite references, path-only file descriptors and cancellation.
- All **nine supplied AVBs** read with `Complete` outcomes: **132,165 objects**,
  **2,988,319 object-property occurrences** and **14,464,589 source bytes**.
  Retained raw-byte totals equal each file's size. No unknown object tails or
  unreadable fields were encountered in these specimens. Size and modification
  time stayed unchanged; this is not a hash-based identity assertion.
- `Testsequenz` and `FINE_01`–`FINE_06` resolve completely under the implemented
  rules. `FINE_06` reaches **511 unique full media IDs**, compared with **517** in
  its whole bin. These are identity counts, not physical-file counts.

The largest bin initially measured about **1.08 GB peak process RSS after reading**
and **1.11 GB including reference analysis** in a standalone x86_64/Rosetta probe
linked to the Debug library. The detailed corpus receipt records exact timings
and memory values for the final run. This is substantial RAM for retained field
evidence and needs allocation/representation profiling before live integration.
There is no application memory cap, and no claim that this is a reduction from
the old app. It is not comparable to the user's whole-app scan screenshot or
3,774 ms scan baseline. Raw evidence and graph rows must not be discarded simply
to improve a benchmark.

## Sources and attribution

The implemented object layouts were checked against the supplied
[pyavb project](https://github.com/markreidvfx/pyavb), specifically
`src/avb/essence.py`, `misc.py`, `attributes.py`, and the relevant cursor helpers
in `ioctx.py`. The component reader also consults `components.py` and
`trackgroups.py`. pyavb is an independent format implementation, not an
Avid-published specification or endorsement.

**Copyright (c) 2022 Mark Reid.** pyavb is distributed under the MIT License.
The complete required copyright, permission and warranty notice is archived in
[pyavb-LICENSE.txt](evidence/pyavb-LICENSE.txt). Keep that notice with copies or
substantial portions of the source-derived material. Avid native evidence is
separately identified above and is not covered by pyavb's licence.
