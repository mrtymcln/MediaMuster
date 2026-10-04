# Select sequences directly from an Avid bin

Recorded 4 October 2026. Requested direction: let a user load an original AVB,
see its sequences and select one or more to filter the scanned media table.
This avoids first copying those sequences into temporary bins. The reader and
sequence filtering are not implemented by this planning/evidence update.

## Proposed user flow

Extend the existing bin-filter window, retaining Intersect, Subtract and Add.
Keep an explicit whole-bin option separate from selecting individual sequences.
The illustrative selections below are not saved user choices.

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

The exact controls, defaults and treatment of mixed clip/sequence bins remain
design details to settle. In particular, do not inherit automatic whole-bin
Intersect while the user is still choosing individual sequences.

## Agreed scope and pending choices

The existing parser collects all supported MSML locators from the bin. It does
not prove that they are reachable from a chosen sequence. The fresh reader must
retain source properties and relationships; a separate resolver will follow
references from the selected sequence roots to qualified file identities.

Media referenced by a sequence is not automatically identical to only the media
that contributes to final playback. Group-angle inclusion is agreed below;
inactive tracks, nested edits, effects, source media and precomputes still need
explicit policies where those decisions are not already established.

- **Agreed, 4 October 2026:** include all referenced angles of a group used by
  a selected sequence, preserving the ability to switch angles later. A sequence
  using one angle from a four-camera group therefore includes the referenced
  media for all four angles, where it can be resolved and found in the scan.
  Retain the sequence-to-group-to-angle evidence for every match. This does not
  include unrelated groups merely because they are stored in the same bin, and
  it does not settle the separate render or inactive-track policies below.
- Render versus original-input inclusion, inactive-track handling and any exact
  used-range analysis remain unsettled. Do not silently adopt playback semantics.
- File rows represent whole physical files. This proposal does not trim media
  down to the frames or samples used by an edit.
- Report incomplete reference resolution explicitly. A partial read or unsupported
  relationship cannot establish that other scanned files are unused. A filter
  result is not, by itself, authorization or proof that excluded files are safe
  to delete. Completion rules must be checked before integration with Subtract.

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
complete playback analysis. The pyavb walk encounters an unsupported PCMA
extension `0x02`. Two unresolved, sentinel-shaped SourceClip identities also
need verified terminal semantics; do not label them missing media. A visited
set prevents loops but does not establish that the source has no cycles.

The production AvbParser accepts both bins without warnings: 721 full MSML IDs
and 684 legacy keys for `01_SEQ`; 517 full IDs and 189 legacy keys for
`02_SEQ_LOCK`. Those sets may overlap and are not physical-file counts. Production
filter readiness is narrower than complete format interpretation.

Archived evidence: [pyavb probe](evidence/avb-sequence-probe-2026-10-04.py),
[graph summary](evidence/avb-sequence-summary-2026-10-04.json),
[representative locators](evidence/avb-sequence-locators-2026-10-04.json),
[current-parser results](evidence/avb-current-parser-2026-10-04.jsonl).
The probe uses local input paths and writes its report to `/tmp`; the reported
unchanged-file check compares size and modification time, not cryptographic hashes.

## Implementation and proof

1. Implement the independent Canon AVB reader, retaining bin membership, sequence
   properties, typed identities, original text encodings and qualified references.
2. Verify sequence listing against the supplied bins and controlled examples,
   including duplicate sequence names and non-user-placed dependencies.
3. Implement reference resolution with retained evidence paths, cancellation,
   cycle handling and explicit unresolved/unsupported outcomes.
4. Test that selecting one sequence excludes another sequence's unrelated media;
   selecting both produces their union. Shared media and physical copies must
   keep correct membership without collapsing physical rows. Add a four-angle
   group case where only one angle is used by the edit: all four referenced
   angles must match, while an unrelated group's media must not. Unresolved or
   unavailable angle references remain explicit rather than silently disappearing.
5. Establish an independent Avid reference comparison for selected sequences.
   A freshly copied sequence bin can be one comparison fixture; it is not a
   requirement imposed on the final user workflow or the sole truth oracle.
6. Integrate selection with the existing filter operations after the pending
   policies and completeness rules are settled. Measure responsiveness and RAM
   on the supplied larger sequence bin before making performance claims.

The workflow benefit is concrete: fewer manual bin-copying steps. No claim is
made here that it alone establishes a particular market price or that current
MDVx versions lack every comparable feature. The locally inspected MDVx manual
(build b3153, 26 December 2019, page 6) documents whole-bin reference filtering.
