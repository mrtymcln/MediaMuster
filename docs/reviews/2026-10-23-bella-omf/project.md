# Bella Project from OMF

24 October 2026. **The user approved these OMF Project rules.** The
[approval record](../../../Project%20Bella/evidence/omf-project-plan-2026-10-24.json)
preserves the direct instruction and original research snapshot. The user has
approved selective OMF reading, version/text, identity/ownership and Clip Name
rules. This review proposes Project's owner/key order within an OMF already
being read. It does not settle PMR/MDB/header priorities or extra opening reasons,
implement Bella, switch the app or change Bin Filter.

Project is a recorded association on a particular record. It is not necessarily
the original creation project or the project currently open in Avid. Unlike
Clip Name, it should not automatically come from the selected master.

## Agreed simple order

Use the same owner principle already approved for MDB:

| Order inside OMF | Agreed source |
| --- | --- |
| 1 | Usable Project on the positively identified physical file mob. |
| 2 | Only if the file supplies no usable Project, one distinct usable Project from its qualified immediately linked source records. |
| 3 | No OMF Project value. Keep the physical row and other valid facts. |

Do not recover Project from a master merely because it supplied Clip Name,
search unrelated objects, flatten import settings or recursively search source
ancestry. The source fallback is useful in three actual support slates below;
it is not required in the two observed live audio files. This is Bella's proposed
choice, approved by the user, not a universal Media Composer priority table.

On each permitted owner, use this small fixed key order:

1. Exact direct `_PJ`.
2. Exact direct `PROJNAME`.
3. Exact `_PJ` in that owner's exact local `_USER` object-attribute list.

The notation `_USER._PJ` means one named nested list; the dot is not part of
the stored key. `PROJNAME` is an alternate recognized key, not a Unicode version
of `_PJ`. An ordinary missing, empty, whitespace-only or individually unusable
value can advance to the next candidate when its boundaries and owner remain
safe. Preserve the exact spelling/spacing of a nonblank selected value.

## Text and supported representations

Read named, typed attributes through the supported OMF relationships:
`CPNT:Attributes` -> `ATTR:AttrRefs` -> `ATTB` name/kind/value. The standard
describes named attributes with integer, string and object-reference forms;
the Project keys and Avid marked Unicode extension are separately established
by native/sample evidence. The standard does not assign these private keys a
universal text encoding.
[Avid OMF specification, ATTB/ATTR definitions](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).

Apply the already agreed text policy to the actual Project representation:

- Kind-2 legacy `StringAttribute`: MacRoman in the agreed Mac-focused scope.
- Established marked Unicode Project text: validate its known requested-field,
  type/kind, two-zero marker, bounded UTF-8 and terminator requirements.
- Kind-3 `_USER`: a checked reference to the one permitted local list, not text.
- Unrecognized binary/integer forms do not become Project text. Kind 4 alone
  does not identify Unicode text.

Prefer a usable Unicode representation of the same candidate when that
representation is actually established. Deliberate separate-legacy recovery
remains allowed under the approved text rule. Do not invent a backup copy or
reinterpret damaged explicitly Unicode bytes as MacRoman. No separate Unicode
Project partner occurred in these five examples; Kind-2/Kind-4 forms normally
represent alternatives for the named attribute, not an assumed stored pair.
Unicode on another key/owner does not outrank a usable preferred Project.

## What the three readers do

| Reader | Verified route and limits |
| --- | --- |
| Media Composer | Direct `CMGetProjectName` asks for exact `_PJ`, then exact `PROJNAME` if the first key is absent. A found blank can stop this fallback. `FindProjectName` adds separate `_USER` and source-inheritance machinery. The ordinary OMF scanner asks the direct getter on restored catalogue objects; other callers start from a master or file. There is no established single native file/master/source priority for every caller. |
| MDVx | Generic PMR/MDB/OMFI enrichment starts from the matched file SourceMob. Its direct helper matches a raw three-byte `_PJ` prefix, not the complete key, and has no `PROJNAME`, `_USER` or typed Unicode handling. A nonnil direct result, even blank, stops fallback. Nil triggers a specific one-level source search, where later resolved values replace earlier ones, even with nil/empty text. Standalone physical OMF header fallback remains unestablished. |
| Current MediaMuster | Reads exact `_PJ` Kind 2 and recursively searches `_USER` and `_IMPORTSETTING`; it has no `PROJNAME` or marked Kind-4 text projection. File, immediate-source and selected-master Project observations have the same OMF rank. Different nonempty values therefore conflict rather than preferring the file's own value. Unlabelled text uses UTF-8 inference before MacRoman. |

Media Composer's typed attribute machinery supports the known legacy/Unicode
forms, while its char-buffer path can convert Unicode back to current-Mac bytes.
That is not a reason for Bella to discard usable Unicode QString text or probe
the live locale. Direct key lookup uses whole case-sensitive names; do not copy
MDVx's prefix matching. Native found/blank/unsupported-kind outcomes are not
Bella's proposed usable-value fallback policy.

Current MediaMuster's raw missing Project is empty, but its table/sidebar/CSV
use `projectDisplay()` and show `No project`. This is a display label, not
stored OMF Project text. This field review proposes no fabricated parser value
and does not change that UI placeholder; final display/source integration is
separate. No application table or export was executed for this review.

## Fresh actual-file observations

An independent bounded read-only probe freshly read the five known files'
label, TOC, definitions, small identities/links and owner-local attributes. It
checked exact names, kinds, types and local references, keeping direct and local
`_USER` values distinct. No recording payload or production parser was read/run.

| Physical Filename | File-owned Project | Immediate-source Project | Proposed result |
| --- | --- | --- | --- |
| `TONE_100A01.6A972974.039700.wav` | `zTeßt_PAL_25p` | Missing | `zTeßt_PAL_25p` |
| `TONE_100A01.6A972997.0C53E0.aif` | `zTeßt_PAL_25p` | Missing | `zTeßt_PAL_25p` |
| `FORMAT_288x243x1_JFIF15m.omf` | Missing | `Symphony NTSC` | `Symphony NTSC` |
| `FORMAT_1920X540X2_AVHD.omf` | Missing | `hd` | `hd` |
| `FORMAT_720x576x1_MPEG50.omf` | Missing | `Sample PAL MPEG media` | `Sample PAL MPEG media` |

All masters lack Project. All five recorded Project values are direct exact
Kind-2 `_PJ` strings. The two audio values have raw `A7` for the deliberate `ß`,
which decodes correctly as MacRoman and is invalid UTF-8 in that string. The
three video specimens are Avid-supplied support slates, not user recordings.

No `PROJNAME`, local `_USER._PJ`, marked Unicode Project, owner disagreement,
blank Project, damaged text or multiple competing source Project occurred in
this five-file set. Local `_USER` objects exist in the audio graphs but supply
no Project. These are selected observations, not every producer or an exhaustive
attached-drive survey. Do not relabel earlier MDB disagreement counts as OMF
findings or claim this probe reproduced an application UI.

## Agreed Bella availability and retention

Use the first usable owner-local key in the stated order. A repeated usable
same-key value can agree; conflicting usable values at the same priority leave
OMF Project unavailable rather than hiding the disagreement with a lower key
or source. Structural ambiguity in required ownership/references also leaves
the dependent contribution unavailable. General OMF damage boundaries remain
for their later review.

Only when the file supplies no usable Project, resolve supported file-local
track/component SourceID links to qualified immediate sources using complete
normalized identities. Apply the same owner-local key order there. Accept one
distinct usable source value, including exact agreement among several sources.
Ordinary missing/blank values are normal. Conflicting usable values, or required
relationships too incomplete/ambiguous to establish the result, supply no OMF
Project. Do not choose by first/last record, hash order, date or master name.

Keep only the selected owned QString and necessary compact availability,
owner/key context. Release raw attribute bytes and temporary indexes/links with
the OMF job. A small fixed key table and shared text helpers are sufficient;
do not build a generic inheritance engine, retained raw graph or permanent
alternate-Project history. Preserve the physical file and independent metadata
when no Project is available. Do not substitute the open Avid project, volume,
folder, clip name or an invented Project. Cross-source reconciliation remains later.

## Evidence and verification

The [curated Project evidence](../../../Project%20Bella/evidence/omf-project-2026-10-24.json)
contains bounded native receipts, exact current source anchors, fresh five-file
attribute/owner observations and explicit limitations. The
[Clip Name approval](../../../Project%20Bella/evidence/omf-clip-name-plan-2026-10-24.json)
records the preceding user decision without rewriting its original research.

Tests were inspected only. A genuine direct OmfReader test expects the WAVE
Project with sharp-s, but does not establish key/owner priority. Similarly named
nested/import damage tests default to MDB; they are not direct OMF execution
proof. No native app/private API, production parser, build or test was executed;
no runtime code/media file was changed and no scan-time/peak-memory gain measured.
Original Bin is the next field review.
