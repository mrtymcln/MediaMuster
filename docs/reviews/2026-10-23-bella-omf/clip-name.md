# Bella Clip Name from OMF

24 October 2026. **The user approved these Clip Name rules.** The
[approval record](../../../Project%20Bella/evidence/omf-clip-name-plan-2026-10-24.json)
preserves the direct instruction and original research snapshot. The user has
approved the OMF reading, version/text and FileMobId/MasterMobId rules. This is
a focused field review for the supported Bento1/legacy-OMF1 media layout, not
an implementation or app cutover. OldScanner and Bin Filter remain unchanged.

Use the already selected associated master's stored name. The physical filename,
file mob name and original source name have different roles. Establish the
master through the agreed identity/link rules before inspecting its name.

## Exact field and encoding

The supported legacy Avid layout stores its component name in
`OMFI:CPNT:Name`, with type `omfi:String`. Read that field on the qualified
master, including a qualified precompute master. Use the agreed MacRoman
legacy-text interpretation within Bella's Mac-focused scope. No usable separate
`CPNT:NameUTF8` counterpart was established in the inspected native/sample
routes. Do not invent one or apply UTF-8-validity guessing to these legacy bytes.

The later OMF2 model has `OMFI:MOBJ:Name` as an optional mob display name.
That is a different schema mapping, not the UTF-8 backup for legacy `CPNT:Name`.
OMF2 support remains deferred under the agreed release-one scope.
[Avid OMF specification, MMOB/MOBJ definitions](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).

The agreed marked Unicode attribute decoder remains useful for its identified
text attributes; its existence does not establish a Unicode version of the
component-name property. Decode the bounded selected value, preserve spelling,
case, punctuation and whitespace. Stop at the first recorded NUL if present;
otherwise use the complete bounded string. The native string-handle helper
appends its own terminator after the recorded bytes, so a missing stored
terminator alone is not proof of damage. General OMF damage rules remain a
separate review.

## What the three applications do

| Reader | Verified route and limits |
| --- | --- |
| Media Composer | The ordinary legacy OMF path restores shared component/mob objects. The component name reader loads `CPNT:Name` through `ReadStringHandle`, which reads the recorded length and appends its own NUL. The cached clip-name getter clears its output, resolves the associated master and copies that component name. Legacy/Unicode conversion happens through the native current-Mac machinery. This does not prove every UI placeholder, all producer encodings, duplicate policy or a paired UTF-8 Clip Name property. |
| MDVx | The traced PMR/MDB enrichment finds the supplied master, asks its generic same-object string reader for a value, and assigns it to `DB_NameInBin`, used by Clip Name. That reader can choose the first accepted property interpreted as text rather than requiring `CPNT:Name` or checking its stored value type. Its ASCII/MacRoman choice is label-derived. Ordinary standalone physical OMF Clip Name fallback remains unestablished. |
| Current MediaMuster | OMF projection associates names with the qualified master. Its broad reader collects both `CPNT:Name` and `MOBJ:Name` candidates, and unlabelled non-ASCII text goes through UTF-8-validity inference before MacRoman. Bella can retain the verified owner/name principle while using the exact release-one field and already approved text rule. |

MDVx's internal `DB_ClipName` refers to the separate physical Filename value;
it must not be mistaken for its logical Clip Name. Its ordinary table displays
nil Clip Name as `...`, while a nonnil empty name can remain empty. A separate
string-value path returns the actual nil/empty value. This is not a proposal for
Bella placeholders or proof of a filename replacement in that path.

Current source observations, master selection and final table display are
separate stages. The current selection code treats its two name aliases at the
same rank: differing usable nonempty values remain unresolved, empty values
are skipped, and an unreadable candidate does not veto a separate present
candidate. Its final adapter/table/CSV path uses the selected Clip Name directly,
leaving unavailable names blank with no Filename substitution. These are source
findings; no production table/export was executed for this review.

## Rechecked actual names

A bounded read-only probe rechecked the five previously owner-qualified Avid
files. Their labels, TOCs, dictionary definitions and selected small identity,
relationship and name properties match the earlier captures. Each qualified
master has one typed `CPNT:Name` string.

| Physical Filename | Selected master's recorded name |
| --- | --- |
| `TONE_100A01.6A972974.039700.wav` | `TONE: 1000 Hz @ -14.0 dB.1` |
| `TONE_100A01.6A972997.0C53E0.aif` | `TONE: 1000 Hz @ -20.0 dB.2` |
| `FORMAT_288x243x1_JFIF15m.omf` | `WRONG_FORMAT_720x486.PCT` |
| `FORMAT_1920X540X2_AVHD.omf` | `Wrong Format 1920 x 1080.psd` |
| `FORMAT_720x576x1_MPEG50.omf` | `palformat.bmp` |

The three video file mobs have corresponding names ending in `.PHYS`; the two
audio file mobs have no captured component name. All five filenames differ
from the associated master name. The onward original-source names happen to
equal the master names in these five examples; equality of text does not
establish the correct owner or justify choosing that source as a fallback.

All five name strings are ASCII and NUL-terminated. They prove field/owner
mapping, not whether non-ASCII Clip Names use UTF-8 or MacRoman. The agreed
MacRoman legacy policy follows the native text research and scoped product
decision, not an encoding distinction measurable in these ASCII-only names.
These are raw name observations, not executions of Avid/MDVx or the current
production parser/table. No recording payload was read.

## Agreed Bella rules

1. Reuse the master selected by the agreed OMF identity/link rules. Read that
   master's exact legacy `CPNT:Name` string; an empty name does not authorize
   choosing a different master. Use the same selected master for other
   master-owned metadata.
2. Apply the already agreed legacy MacRoman rule, with bounded reading and
   owned QString output. Preserve characters, spelling, punctuation, case and
   whitespace. Stop at a recorded NUL or the declared value boundary; do not
   require an otherwise complete bounded value to contain a stored terminator.
   Do not try UTF-8 first or invent a separate Unicode Clip Name.
3. No uniquely selected master, missing/empty/unusable name or contradictory
   required Name values supplies no OMF Clip Name. Equal repeated values can
   describe one name. Keep the physical row and independently usable file ID,
   master ID and technical facts. A missing name does not erase a valid ID.
4. Keep Filename as the actual physical filename. Do not substitute it, a file
   mob name, a track name or an original-source name for the absent Clip Name.
   On an OMF-only fallback with no usable name, the table value is blank.
   Cross-source priorities and reasons for extra header reads remain later
   decisions; this review introduces neither.
5. Keep only the selected owned name and necessary availability state, releasing
   raw bytes and temporary owner/index information with the OMF job. A small
   helper using the approved C++17/Qt 6.5.3 building blocks is sufficient; no
   new per-field class, persistent name graph or alternate-name history is needed.

## Evidence and limits

The [curated evidence](../../../Project%20Bella/evidence/omf-clip-name-2026-10-24.json)
retains native/source receipts, exact source anchors, five fresh bounded name
rechecks and explicit distinctions between tests inspected and tests run.
The [ID approval](../../../Project%20Bella/evidence/omf-identities-plan-2026-10-24.json)
records the preceding user decision. The original identity research snapshot
remains unchanged.

No runtime code, native app/private API, production parser, build or test was
executed or changed in this review. No media file was written, no new scan
benchmark was run and no time/memory improvement is claimed. Non-ASCII,
missing/damaged names and ambiguous masters were not fresh real-media examples
in this five-file set. Project is the next OMF field review.
