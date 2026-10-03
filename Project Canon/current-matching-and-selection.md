# Current matching and metadata selection

Source inspection: 3 October 2026. This describes the inspected implementation,
not the proposed complete evidence model. Existing priorities are recorded here
for review; documenting them does not prove that each is the best Avid interpretation.

## Existing reconciliation-like behaviour

The scanner starts with supported physical files in accepted media folders and
creates individual `MediaFile` records. Database records populate those rows.

1. PMR records are indexed by NFC-normalised, lowercased filename including extension
   and punctuation. The first record for a matching key is selected. Multiple index
   spellings can contribute records, with the primary msm database read first. This
   is the existing database lookup policy, not a recommendation to lowercase physical
   location keys or suppress case-distinct physical files.
2. The selected PMR entry supplies file/master identities and project. MDB file records
   are joined by the known file identity. A missing master can be filled from the MDB
   only when its source-reference graph establishes a unique master relationship.
   Master metadata is then joined using the master identity.
3. Technical MDB metadata is initially used for a nonempty file when the file descriptor
   is complete, a corresponding master record exists, and the PMR indexed timestamp
   matches under the supported clock rules. `databaseMetadataCurrent` is the current
   implementation's name, not independent proof that every property is current.
4. Missing/incomplete/stale database coverage or certain missing metadata schedules a
   header read. The currently enabled clip-duration feature additionally schedules
   reads for all nonempty admitted MXFs. Empty files are not header-read by this gate.
5. Header IDs are converted to the database representation for MXF matching. A usable
   contradictory file/master identity clears the old joined metadata before applying
   the new information. Physical filesystem facts and PMR filename membership remain.
6. The header's master identity can recover additional MDB master metadata through the
   same folder's retained master cache. Loaded bins separately provide missing editor
   metadata by master ID, with disagreement checks among those bin observations.

The scanner currently marks PMR-matched physical rows `Listed`; unmatched rows are
`NoReference`, `NoDatabase` or `DbUnreadable` according to local database availability
and read outcomes. The UI groups the last two under No Database with differing
explanations. Header recovery does not change PMR membership.

Consequently, `Listed` means a PMR filename match, not universal identity agreement.
The header-replacement path prevents old metadata attaching to a reused filename,
but does not create the proposed retained conflict observations/diagnostics.

## What is not currently reconciled

- No reverse pass produces a dedicated list of PMR/MDB physical-file references whose
  files were not found. Those database records do not become phantom inventory rows.
- No complete scan-wide reconciliation retains each unmatched reference and reports
  all matching copies found elsewhere across the selected drives.
- No uniform `MetadataObservation`/`ResolvedField` store preserves every competing
  value, its exact source/property and the reason for selecting a displayed result.
- The agreed KelpieId and property-state enums are not implemented by this documentation.

There are logs for missing/unreadable databases. That is different from reporting
an individual database reference whose media could not be found.

## Existing selection policies by field

A selection policy decides which value is displayed when more than one source can
supply that same field. It does not decide whether physical files get separate rows.

### Source priority matrix

This matrix expresses today's principal rules as a proposed-table shape. It does
not mean the current code already uses this central table. `1` is preferred over
`2`, but only when a usable observation is actually available under that field's
conditions. `Absent` denotes properties PMR does not store. `Not used` means this
metadata path currently does not supply that field; it is not a claim about every
property the underlying AVB format could contain.

| Field | PMR | MDB | Media header | Loaded AVB | Current displayed-result rule |
| --- | --- | --- | --- | --- | --- |
| Codec | Absent | 2 | 1 | Not used | Usable header codec replaces eligible MDB codec |
| Resolution | Absent | 2 | 1 | Not used | Usable nonempty header resolution replaces eligible MDB resolution |
| Video frame rate | Absent | 2 | 1 | Not used | Header display rate/exact fraction replace corresponding eligible MDB values when usable |
| Bit depth | Absent | 2 | 1 | Not used | Nonempty usable header bit depth replaces eligible MDB value |
| Audio sample rate | Absent | 2 | 1 | Not used | Positive header rate/valid fraction/original encoding replace corresponding eligible MDB values |
| Audio channel count | Absent | 2 | 1 | Not used | Positive header count replaces eligible MDB value |
| File duration | Absent | 2 | 1 | Not used | Valid header duration with positive units replaces eligible MDB duration |
| Clip-reference durations | Absent | 2 | 1 | Not used | Nonempty header list replaces any earlier eligible list |
| Clip name | Absent | 2 | 1: selected material/master name | 3 | Higher-ranked nonempty name wins; AVB supplies only an unambiguous missing name |
| Project | 1 | 2: master, then file | 3 | Not used | First nonempty joined value retained; header fills blank |
| Original bin | Absent | 1 | 2: supported OMF header | 3 | Fill blank; an existing value is retained; AVB fallback must be unambiguous |
| Imported source path | Absent | 1 | 2 | Not used | First nonempty value retained |
| Source basename | Absent | 1 | Derived from retained path when blank | Not used | Existing MDB basename retained, otherwise derive from selected path |
| Source container | Absent | 1 | 2 | Not used | First nonempty value retained |
| Imported flag | Absent | Can set true | Can set true | Not used | Any accepted import-setting observation sets true; a later false does not reset it |
| Timecode base | Absent | 2 | 1 | Not used | Positive usable header value replaces earlier eligible value |
| Drop-frame flag | Absent | Can set true | Can set true | Not used | Accepted true sets the flag; false does not clear a prior true |
| File Mob ID | Initial identity | Join key, not usual replacement | Recovery/replacement on usable evidence | Not used for assigning this field | Header fills gaps; identity contradiction clears old joined metadata and rebuilds |
| Master Mob ID | Initial identity | Unique relationship fills gap | Recovery/replacement on usable evidence | Used to look up fallback metadata, not assign this field | Fill missing identity; header contradiction triggers replacement handling |
| Audio/video kind | Absent | Initial eligible descriptor evidence | Later valid descriptor evidence | Not used | Audio verdict or positive picture dimensions set kind |
| Media/precompute type | Absent | Initial eligible master classification | Later supported classification or Unknown | Not used | Evidence-based verdict; fully read unsupported/conflicting material usage can clear an earlier verdict |
| Precompute category | Absent | Initial eligible master category | Later category, with disagreement handling | Not used | Certain current MDB/header disagreements become Unknown |
| Local database membership | Filename match determines Listed | Read outcome can qualify unmatched status | Does not change membership | Does not change membership | Listed / No Reference / No Database / DbUnreadable from folder checks |

Filename, absolute path, size, creation/modification timestamps and current location
come from filesystem enumeration, not a priority choice among these four metadata
sources. The PMR indexed timestamp is used for checking database consistency, not
as a replacement for the physical file's modification time.

Eligibility caveats:

- Initial MDB technical application requires a nonempty physical file, complete file
  descriptor, corresponding master record and matching PMR timestamp. Editorial MDB
  fills do not all share that technical eligibility gate.
- The header application has read-validity/classification gates and individual
  nonempty/positive/valid-fraction conditions. It does not indiscriminately replace
  every field when any header bytes were read.
- A contradictory usable header identity clears previously joined editorial and
  technical metadata before recovery. This overrides a simple numeric priority view.
- If initial database joins could not supply original bin/import metadata, header
  recovery can fill them before a later same-folder MDB master rejoin. Fill-if-missing
  fields then keep that header value. The ranking describes the usual initial-join
  path; exact order matters on recovery paths.
- Equivalent-ranked clip names keep the first; conflicting loaded-bin fallbacks are
  withheld. Technical disagreements generally do not retain all competing values in
  today's physical-file record.
- Sample format is not yet a separate uniform resolved field in MediaFile; current
  bit-depth/codec decoding and its known format issues are documented in the wider
  audit. This matrix must not be mistaken for verification of those interpretations.

| Field | Inspected current policy |
| --- | --- |
| Clip name | Nonempty selected material-package name outranks MDB name; loaded AVB master name fills a remaining blank only when usable bins do not disagree. Equal-ranked names keep the first. |
| Project | PMR first; then missing-value fill from MDB master, then MDB file; usable header fills a remaining blank. Identity replacement clears old joined values before rebuilding. |
| Original bin | MDB supplies a value first; OMF header can fill a blank; loaded AVB fallback fills a remaining blank when bin observations agree. |
| Codec, dimensions, video rate, bit depth, sample rate, channel count | Eligible MDB technical metadata can populate first; later valid usable header values replace the corresponding fields. Missing/invalid header values generally leave earlier values in place. |
| File duration | Eligible MDB duration can populate first; later valid header duration replaces it when units are positive. Duration retains its own units, rate and source. |
| Imported source path/container | Fill missing fields from MDB, then usable header metadata. Basename can be derived from the retained source path. |
| Media/precompute classification | Uses supported object usage/descriptor evidence rather than filenames. Unknown/conflicting usage can clear a prior positive classification; certain current database/header category disagreements become Unknown. |

Some flags and numeric fields have further individual update conditions. This table
is a readable map of the principal policies, not an exhaustive substitute for the
source. It does not imply a single universal header/database/bin priority.

Illustrative bit-depth disagreement: if eligible MDB metadata says 24-bit and a
later valid header supplies 16-bit, today's technical-field update displays 16-bit.
The proposed model would additionally retain the 24-bit observation, both sources,
agreement state, and the explicit selection rule. This is not a newly observed
conflict in the real A01 sample.

## User decision and remaining specification

The user confirms that fields should be handled individually. This accepts the
per-field selection principle, not every current priority above. The next specification
can start from these actual policies and check them against each property's meaning,
source scope and freshness. Preserve evidence now; no detail UI is required in v1.

## Source locations

- `src/mediascanner.cpp`: `readFolderPmrs`, `readFolderMdbs`, `buildMediaFile`,
  `setClipName`, `applyMdbRecord`, `applyMetadata`, `readMediaHeader`, and the header pass.
- `src/pmrkey.h`: current PMR filename key normalization.
- `src/mdbparser.cpp` and `src/mdbparser.h`: parsed file/master records and graph joins.
- `src/binmetadataresolver.cpp`: loaded-bin matching and name/bin disagreement handling.
- `src/mediafile.h`: current row fields and local database statuses.
- `src/featureflags.h`: current clip-duration read behaviour.
