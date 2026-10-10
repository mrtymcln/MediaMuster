# Select sequences directly from an Avid bin

Recorded 4 October 2026. Requested direction: let a user load an original AVB,
see its sequences and select one or more to filter the scanned media table.
This avoids first copying those sequences into temporary bins. The fresh reader
and sequence resolver are now implemented and tested. The user confirmed **engine
first**: this stage implements and verifies those engines without changing the
live UI. These requirements do not mean that the production scanner has been
switched over. See
[AVB reader evidence and implementation scope](avb-reader.md) for the current
checks and their limits.

## Agreed user flow

Extend the existing bin-filter window, retaining Intersect, Subtract and Add.
First choose **Entire bin** or **Selected sequences**. For Selected sequences,
choose one or more sequence roots. Then explicitly apply Intersect, Add or
Subtract. Loading a bin or changing a selection must not automatically apply a
filter. The illustrative selections below are not saved user choices.

```text
01_SEQ.avb
  Scope: Selected sequences
    [ ] ROUGH
    [ ] FINE_01
    [ ] FINE_02
    [ ] FINE_03
    [x] FINE_04
    [x] FINE_05

  Intersect: show media referenced by either selected sequence
  Subtract:  hide media referenced by either selected sequence
  Add:       bring those matching media rows back into the result
```

Use the bin/source identity and sequence object/identity internally; names are
labels and may repeat or change. Preserve which selected sequence and reference
chain caused a match. Multiple selected sequences share one union of references
for a filter step; physical copies remain distinct rows and retain their KelpieIds.
Sequence selection must not silently fall back to all references in the bin.
No source AVB is rewritten or temporary Avid bin required.

Exact control placement and the presentation of mixed clip/sequence bins remain
UI details. They must preserve the agreed explicit scope and apply steps above.

## Release gate and partial-result policy

User update, 4 October 2026: **gate selecting individual sequences behind a
feature flag**, so it can be withheld until a later v2 release. Keep whole-bin
filtering available independently. The approved name is **`SequenceFilter`**,
implemented as `FeatureFlags::kSequenceFilter = false`. The existing precompute
flag is now **`PrecomputeFilter`**, implemented as
`FeatureFlags::kPrecomputeFilter = true`; its behaviour is unchanged. The
sequence flag is defined for the later UI integration. When enabled, it should
gate both the sequence controls and the selected-sequence apply path. The shared
reader, evidence storage and whole-bin functionality need not be disabled.

The user explicitly approved **allow partial results with a warning**. This
supersedes the earlier blanket rule blocking incomplete sequence filters:

- A readable selected sequence that yields usable media references can be used
  for Intersect, Add or Subtract, even when some dependencies are external,
  unresolved, unreadable or ambiguous. Keep the sequence available for selection.
- Show a persistent **Results may be incomplete** warning with the affected
  result, and write a Console summary naming the bin/sequence and the reasons.
  The warning must cover uncertain matches as well as omitted references;
  ambiguous candidate evidence must not silently become a confirmed match.
- Preserve specific reasons internally and in Console details. Do not flatten
  all issues into AMA. Confirmed external sources remain outside the agreed
  managed-folder scan scope; retain their paths and evidence without opening
  or adding external media to the physical inventory.
- A completely unreadable bin, invalid selection or cancelled operation is
  unavailable. This change does not turn a failed operation into a usable result.
  A partial read with no usable media references cannot manufacture an operand.
- Keep dependency completeness separate from filter eligibility. Leave
  `AvbResolution.complete` false when issues remain. The future adapter must
  validate the selected roots, cancellation and usable references, carry the
  warning into the applied result and retain the underlying issues.
- A known managed identity not found in the current scan is a separate scan
  coverage issue; it does not prove an unreadable bin or that media is absent
  everywhere. Preserve that distinction in warning details.

Currently the engine retains each recognised sequence in its catalogue and
returns reference issues with `complete = false`; it does not discard the
sequence. There is no new picker, greyed-out state or Console integration yet.
The resolver's evidence and completeness flags are unchanged by this policy
update. The warning UI and application eligibility belong to the deferred live
adapter; no live bin-filter behaviour is changed in this stage.

### Evidence for identifying external/AMA references

The categories describe different dimensions and can coexist: an AMA-linked
source can also be offline, unreadable or ambiguous. In `ROUGH`, recorded
descriptor bytes contain `Avid Generic Plug-In` and `Q7_MediaContainer`, and the
associated physical-source locators record an external `Scene04Rough.mp4` path.
Those combined source-context observations support the linked-media finding for
this specimen. They are not a universal, implemented `isAma` flag or a guarantee
that the external file still exists. See the [descriptor evidence](evidence/avb-rough-descriptors-2026-10-04.json)
and [parent/source locators](evidence/avb-rough-parent-locators-2026-10-04.json).

Do not infer AMA merely from a missing MSML locator, a filename extension or an
old original-source path. Avid's [MXF AMA Plug-in Guide, pages 1–3](https://resources.avid.com/SupportFiles/attach/MXF_AMA_v6.0_v10.pdf)
explicitly supports linked MXF media, so MXF is not synonymous with managed
media. Avid also [documents stored AMA-link paths](https://kb.avid.com/pkb/articles/en_US/Knowledge/rebuild-AMA-Management-folder).
These sources establish workflow distinctions, not a universal AVB byte-level
classifier. Unknown interpretations remain unknown even when filtering is allowed.

## Agreed dependency scope

The existing parser collects all supported MSML locators from the bin. It does
not prove that they are reachable from a chosen sequence. The fresh reader must
retain source properties and relationships; a separate resolver will follow
references from the selected sequence roots to qualified file identities.

These rules describe the media the user wants available for further editing.
They do not try to reproduce only the currently audible or visible playback.
The following decisions were approved on 4 October 2026:

- **All referenced group angles:** include every referenced angle of a group
  reached from a selected sequence, preserving the ability to switch angles
  later. A sequence using one angle from a four-camera group therefore includes
  the referenced media for all four angles, where it can be resolved and found
  in the scan. Keep sequence-to-group-to-angle evidence. Do not include unrelated
  groups merely because they are stored in the same bin.
- **Renders and inputs:** include referenced render/precompute media and the
  original source media feeding the relevant effect or composition. Reaching a
  render is not a reason to stop following its recorded input relationships.
- **Muted and disabled tracks:** include their referenced media. Retain the
  recorded flags as evidence; those flags do not exclude dependencies.
- **Nested dependencies:** continue through recorded nested compositions,
  effects and group relationships under those same rules. Follow verified
  reference fields, not equal names or a guess based on object proximity.
- File rows represent whole physical files. This proposal does not trim media
  down to the frames or samples used by an edit.
- **Allow partial filters with a persistent warning:** use the revised policy
  above. Incomplete results do not establish that unmatched files are unused.
- **Keep ambiguous bin objects separate:** several loaded bins or object
  occurrences may carry the same MobID. Do not silently choose the first or merge
  them. Unsettled identity ambiguity qualifies the result and appears in its
  warning. This is distinct from several physical file copies that
  correctly match one established identity.

## Evidence from the supplied sequence bins

Read-only inspection using the supplied pyavb source found:

| Bin | User-placed composition sequences with usage code 0 | All Bin.items / composition objects |
| --- | --- | ---: |
| `01_SEQ.avb` | `ROUGH`, `FINE_01`, `FINE_02`, `FINE_03`, `FINE_04`, `FINE_05` | 1,981 |
| `02_SEQ_LOCK.avb` | `FINE_06` | 1,264 |

Both files also contain non-user-placed dependent objects. All composition
objects found in these two files occur in Bin.items. These observations are not
a universal specification for identifying every historical kind of sequence.

Representative verified paths:

```text
ROUGH (object 99712)
  -> SourceClip 90574
  -> MasterMob "3 8 1" (99685)
  -> picture SourceClip 88715
  -> SourceMob "A002_C016_0101GA" (99684)
  -> CDCI descriptor 97323
  -> MSML locator 88700, volume EDIT

FINE_06 (object 25088)
  -> precomputed SourceClip 11014
  -> MasterMob "FINE_06,Title,99" (23967)
  -> SourceClip 19382
  -> SourceMob 23962
  -> CDCI descriptor 19369
  -> MSML locator 12170, volume EDIT (/Volumes/EDIT)
```

Each terminal locator's full identity matches its associated SourceMob. This
proves representative reference chains, not a complete physical-media match or
complete playback analysis. The original pyavb walk encounters an unsupported PCMA
extension `0x02`. The new descriptor grammar now supports that extension using
independent native Avid Get/Put evidence and observed field bytes; see
[the PCMA evidence](avb-reader.md#pcma-extension-02). This resolves that particular
field-framing gap, not every outstanding reference question. The two formerly
unresolved sentinel-shaped SourceClip identities are now verified as the exact
Avid null and filler MobIDs by native `IsNullMobID` and `IsFillerMobID` evidence.
They are terminals with recorded reasons, not missing media. See
[source-reference terminals and dependency meanings](avb-reader.md#source-reference-terminals-and-dependency-meanings)
for the full bytes, native addresses and limits. A visited set prevents loops
but does not establish that the source has no cycles.

The ASPI extension-8 MobID is independently established as a source-master
dependency. MobRef, marker and cached Position IDs do not acquire that meaning
merely by sharing the MobID type. Legacy SourceClips with only numeric words and
no full typed identity remain unresolved in this stage; there is no guessed
legacy-to-full mapping.

The production AvbParser accepts both bins without warnings: 721 full MSML IDs
and 684 legacy keys for `01_SEQ`; 517 full IDs and 189 legacy keys for
`02_SEQ_LOCK`. Those sets may overlap and are not physical-file counts. Production
filter readiness is narrower than complete format interpretation.

Archived evidence: [pyavb probe](evidence/avb-sequence-probe-2026-10-04.py),
[graph summary](evidence/avb-sequence-summary-2026-10-04.json),
[representative locators](evidence/avb-sequence-locators-2026-10-04.json),
[reader results](evidence/avb-reader-probe-results-2026-10-04.jsonl).
The probe uses local input paths and writes its report to `/tmp`; the reported
unchanged-file check compares size and modification time, not cryptographic hashes.

## Implementation and proof

Current engine-stage result: `MediaEngine::AvbReader` and `MediaEngine::AvbReferenceIndex`
implement the reader, catalogue and graph traversal below. The two new suites
exercise source evidence and selection rules; the supplied nine-bin corpus was
also read without modification. `Testsequenz` and `FINE_01` through `FINE_06`
resolve completely under the supported rules. `ROUGH` remains qualified because
six descriptors lead to external, path-only MP4 media rather than established
managed-media identities. See [the implementation report](avb-reader.md).

The checklist below also contains **later integration checks**: no physical-row
adapter, live filter operation, sequence picker or Media Composer export oracle
is claimed as delivered in this stage. `AvbResolution.complete` describes
coverage; the revised warning policy governs the later adapter's eligibility.

1. Implement the independent MediaEngine AVB reader, retaining bin membership, sequence
   properties, typed identities, original text encodings and qualified references.
2. Verify sequence listing against the supplied bins and controlled examples,
   including duplicate sequence names and non-user-placed dependencies.
3. Implement reference resolution with retained evidence paths, cancellation,
   cycle handling and explicit unresolved/unsupported outcomes. Preserve verified
   null/filler terminal reasons and exercise ASPI source-master links. Verify that
   legacy-only SourceClips and duplicate-MobID ambiguities do not appear complete.
4. Test that selecting one sequence excludes another sequence's unrelated media;
   selecting both produces their union. Shared media and physical copies must
   keep correct membership without collapsing physical rows. Add a four-angle
   group case where only one angle is used by the edit: all four referenced
   angles must match, while an unrelated group's media must not. Unresolved or
   unavailable angle references remain explicit rather than silently disappearing.
   Include tests with both render and source inputs, and muted/disabled tracks.
   Confirm loading a bin has no filter side effect and applying each operation
   uses the chosen Entire bin or Selected sequences scope. In this engine-first
   stage, test underlying resolution without adding live UI. At integration,
   verify partial results retain their warning and failed/cancelled selections
   cannot be applied.
5. Establish an independent Avid reference comparison for selected sequences.
   A freshly copied sequence bin can be one comparison fixture; it is not a
   requirement imposed on the final user workflow or the sole truth oracle.
6. Integrate selection with the existing filter operations after the resolver
   and its completeness rules pass the agreed checks. Measure responsiveness and RAM
   on the supplied larger sequence bin before making performance claims.

The workflow benefit is concrete: fewer manual bin-copying steps. No claim is
made here that it alone establishes a particular market price or that current
MDVx versions lack every comparable feature. The locally inspected MDVx manual
(build b3153, 26 December 2019, page 6) documents whole-bin reference filtering.
