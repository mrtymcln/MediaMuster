# RAM metadata design

Status: proposed implementation, with the requirements in [README](README.md)
accepted for v1. The names below are MediaMuster design terminology, not a claim
that Avid defines these exact classes.

## Physical files remain individual

A `MediaFile` describes a physical file at a particular volume and location. It
stores filesystem information, scan/read status, local database membership,
selected metadata, and links to supporting evidence and Avid identities.

Do not use a Mob ID as the unique key of the physical-file inventory. Copies can
carry the same identity. A scan-local record ID plus volume/location information
can distinguish their rows. Path changes and later rescans need explicit handling;
a pathname is not an eternal file identity.

Recommended proposed fields:

- `kelpieId`: a `KelpieId` backed by Qt's `quint64`, uniquely identifying this
  physical-file row within its scan. `0` means "not assigned".
- `volumeIdentity`: the volume identity captured by the app, with its confidence.
- `relativePath`: this file's path within that volume.
- `fullPath`: its current absolute path, for display and filesystem access.
- `fileMobId` and the object links: the Avid identities associated with this location.
- `contentMatchStatus`: optional, separately measured content equality evidence;
  initially `NotChecked`, independently of metadata agreement.

Full absolute paths distinguish different locations in one mounted scan, including
copies with identical filenames on different drives. For implementation, use volume
identity plus a relative path as the location key, and `kelpieId` as the row handle.
Volume names and mount paths can change or be reused; keep current path and volume
identity separately. When a persistent volume identity is unavailable or weak, retain
that qualification and a scan-local volume handle; do not invent certainty.

Do not globally lowercase paths or treat case variants as equivalent without the
filesystem's rules. Overlapping scan roots discovering the same directory entry
should not manufacture a second row, while different locations containing copies
must remain distinct rows. Platform file-object identifiers can help recognize
aliases/hard links, but must not become a blanket rule for merging location rows.
Hard links and filesystem clones can also affect actual recoverable storage; logical
file size alone does not prove how much space deleting one location will release.

```text
KelpieId 101: volume EDIT + MXF/1/A001_C011_V01.D7EC5BC5FF69V.mxf
KelpieId 102: volume EDIT + MXF/2/A001_C011_V01.D7EC5BC5FF69V.mxf
KelpieId 103: local volume + Desktop/Avid MediaFiles/MXF/1/same filename

Same filename, same File Mob ID, even identical bytes: still THREE rows.
Two files cannot occupy the exact same directory entry at the same time.
```

Assign KelpieIds from one scan-wide coordinated counter starting at `1`, not
independent per-folder counters. If assignment occurs on worker threads, use
synchronized allocation; alternatively assign on the single aggregation thread.
Never wrap the counter to `0` or reuse an assigned ID within that scan. Keep IDs
stable through sorting, filtering and metadata changes; they are not row positions,
path hashes, content hashes or Avid Mob IDs. An ID alone does not identify a record
from another scan. Any future cross-scan references need explicit scan context.

Qt provides the storage type, not an automatic unique-number assignment policy.
No UUID generator or new persistent store is needed for this RAM-only v1 design.

Keep these concepts distinct in UI labels: `Same Avid identity`, `Matching metadata`,
and `Content equality checked`. Same master/clip metadata alone can describe different
video/audio tracks or representations, not copies of the same file. Equality checks
should retain the method and source snapshots and detect files changing during the
check. Full-file hashing or comparison is separate, potentially expensive work; do
not require it merely to assign physical rows or group identity candidates.

```text
ONE Avid file identity
        |
        +--- found at EDIT/MXF/1/video.mxf
        |       `MediaFile` record 1 -> table row 1
        |
        +--- found at EDIT/MXF/2/video copy.mxf
        |       `MediaFile` record 2 -> table row 2
        |
        `--- found at local Desktop/MXF/1/video.mxf
                `MediaFile` record 3 -> table row 3

Shared identity/evidence does not combine the three physical records.
```

Matching identities and metadata identify a duplicate candidate or another
representation; they do not alone establish identical payload bytes. Byte equality
requires a separate verification method. Do not automatically delete or hide copies.

## Supporting records

| Proposed name | Stores | Links to |
| --- | --- | --- |
| `AvidObject` | An Avid identity, its role, and observations about that object | Physical locations, observations, recorded relationships |
| `Relationship` | A reference between objects; track, start, length and units where supplied; recorded/derived basis | Origin object, target object, supporting source/observations |
| `SourceSnapshot` | Which PMR, MDB, bin or media header was inspected; source location/version markers, read time, parser version and read outcome | Observations from that inspection |
| `MetadataObservation` | Field, typed decoded value, original value where practical, exact property/object locator, and recorded/derived basis | Subject file/object, source snapshot, derivation inputs/rule |
| `ResolvedField` | Selected value, selection rule/version, explanation, supporting and competing observations | Observations and the file/object whose field it resolves |
| `ScanIssue` | An unresolved condition, supporting references, affected location/identity, scan scope and completion status | Source snapshots, objects, physical records where present |
| `ScanResult` | Shared RAM collections for one scan, including physical files | All of the above |

An `AvidObject` can represent a master clip, file source, or physical source. Objects
from several snapshots can disagree about its properties; retain those observations.
Some objects have no physical file by design. Preserve identity roles and namespaces;
do not conflate a File Source Mob with its lower-level physical source.

`MobID` is the general identifier term. A master identity identifies a master clip;
a file-source identity identifies a media representation; a physical-source identity
describes underlying source content/origin. `SourceMobId` alone is ambiguous because
a File Source Mob is also a Source Mob. Prefer explicit roles in new names.

Relationships establish connections. Equal names, equal durations, folder proximity,
or a shared physical-source identity are not substitutes for recorded references.
More than one master can reference a file source; do not force all relationships
into a single `masterMobId` field. The existing field can remain a selected primary
association during migration.

```text
Master clip object
   |
   +-- video reference --> File-source object V
   |                          `--> physical video `MediaFile`
   |
   +-- audio reference --> File-source object A1
   |                          `--> physical audio `MediaFile`
   |
   `-- audio reference --> File-source object A2
                              `--> physical audio `MediaFile`

File-source objects can also reference lower-level source ancestry.
Their physical locations can be in different scanned folders or volumes.
```

## Evidence for each selected value

| What the record remembers | Example |
| --- | --- |
| Selected value | 16-bit fixed point, displayed from separate bit-depth and sample-encoding fields |
| Evidence source | This MXF header's identified audio descriptor and property |
| Basis | `Recorded`, or `Derived` with rule and inputs |
| Other observations | MDB reported floating point; preserve that actual observation |
| Selection reason | A field-specific rule selected the explicit encoding observation for this file |
| Freshness | Source version markers and a timestamp-consistency or stronger validation result |

This disagreement is an illustrative example, not an observed conflict in the real
A01/A02 group documented in the findings.

Store technical values in their original useful shapes: bit depth and sample encoding
separately, rates as exact fractions, duration with units and rate, dimensions as
dimensions, coding/wrapping labels separately. Produce readable labels from those
values. Retain raw encodings where normalization could lose meaningful information.

```text
SourceSnapshot: header ------> Observation: encoding = fixed point
                                       |
                                       +---> ResolvedField
                                       |       selected = fixed point
SourceSnapshot: MDB ---------> Observation: encoding = floating point
                                               |
                                               `--> `MediaFile` display value

ResolvedField retains both observations and explains its selection.
```

Recorded means the source explicitly encoded the value. It does not guarantee that
the source is current or that our decoder is correct. Decoding, derivation, selection,
freshness and source readability are separate concerns.

Rules must be specific to the field and object role. A technical descriptor and a
bin's editorial name serve different purposes. Preserve current clip name versus
original source name as different fields. Never impose a universal rule that headers
always outrank bins or databases.

## Agreed property state names

The user has accepted the following names and distinctions for the v1 design.
Use scoped C++ enums; Qt has no predefined enum for this application-specific model.
Names need not repeat "Property" once scoped by their type.

`PropertyReadState` describes one source's property-read outcome:

| Value | Meaning |
| --- | --- |
| `NotRead` | That property/source has not yet been checked |
| `Present` | A usable value was read |
| `Absent` | A sufficiently complete read established that the property was not supplied |
| `Unreadable` | The attempted read could not establish a usable value |

Retain a structured reason for `Unreadable`, such as I/O error, malformed encoding,
unsupported interpretation or incomplete source. Unknown-format properties may have
raw bytes retained even when their interpreted value is unreadable. An absent
descriptor/object does not automatically prove every possible property absent.
Coverage records must say which object and property were actually checked.

The user requires a complete logical table of all defined MediaMuster metadata
fields for each MediaFile. It includes meaningful negative and inapplicable entries,
not only values discovered during reads. For a property established as not stored
by a source format, use `Absent` with a structured reason such as `NotStoredByFormat`.
PMR codec and audio sample rate are examples. This result can be supported by the
known format layout without inspecting nonexistent properties in every PMR file.
Keep source availability/read status separately: no PMR file present is not evidence
from an actual PMR entry. The field/source capability can still say PMR does not
store codec while the source snapshot says no PMR was available.

Proposed additional distinction: field applicability (`Applicable`, `NotApplicable`,
or `Unknown`) is separate from read state. Video frame dimensions can be not applicable
to an established audio-only file, while audio sample rate is applicable but its PMR
observation is absent. Unsupported extraction is not proof that the format omits the
property: preserve `NotRead` if not attempted or `Unreadable` with an unsupported
reason if an attempted interpretation cannot establish a value.

The scope is not limited to fields defined by today's MediaMuster. The user requires
1:1 fidelity to every source property/value/context/reference, including newly
discovered vendor/private properties. Extend the semantic field model as discoveries
are agreed; preserve unmapped observations/raw encodings meanwhile and ask the user
how new properties/value meanings should be represented and selected. Current tables
are reference only. Actual completeness must be supported by coverage evidence;
unrecognized structures must be retained/reported, not silently skipped or labelled
absent. See the selection-policy document for the discovery/decision workflow.
Complete logical coverage does not require duplicating empty strings and identical
format capability records for every file: shared field definitions/source capabilities
and compact per-record states can expose a complete table while storing efficiently.

`PropertyAgreement` separately describes comparable observations:

| Value | Meaning |
| --- | --- |
| `NotCompared` | No comparison result has been established |
| `SingleSource` | One usable source supports the field |
| `Agreeing` | Multiple comparable observations agree |
| `Conflicting` | Comparable observations disagree |

A header observation can be `Present` while the selected field is `Conflicting`
because an MDB observation supplies a different value. Do not squeeze these into
one enum. Agreement applies to retained comparable evidence, not unread sources.
Only compare the same semantic field/object/context; absent optional metadata in one
source is not automatically a conflict with metadata supplied elsewhere.

Selection is separate again: keep an optional selected observation/value, its rule
and version, inputs and reason. Conflicting evidence can still support a selected
value under an explicit rule. An ambiguous association can remain unresolved without
being a property read error. Recorded/derived basis and freshness are also independent.

## Future presentation without changing evidence collection

The user explicitly deferred a "Why this value?" detail UI. Preserve observations,
source/property locators, raw values where needed, read outcomes, derivation inputs,
selected-value rule/version/reason, competing observations and freshness in RAM now.
Any later UI feature flag should control presentation only, not whether that evidence
is collected. A detail panel should consume the same read-only evidence interface as
the table/Console, rather than introducing another parser or selection implementation.

Stabilize the concepts and invariants early; keep parser, selection and UI boundaries
separate. A field identifier and typed-value representation should permit adding
new properties without redesigning every MediaFile consumer. This is extensibility,
not a promise that an immutable schema can cover all future discoveries. Changes to
Avid interpretation may still require model additions. RAM-only storage avoids a
persistent database migration, but still needs clear, tested record contracts.

## Record lifecycle and reconciliation rules

Record lifecycle means when a record is created, updated and retired. Agreed rules:

- A KelpieId belongs to one scan session; allocate it once for its physical record.
- Sorting, filtering and metadata enrichment preserve the ID.
- Quitting or rescanning flushes that scan's RAM records/evidence. A rescan creates
  a new context and assigns IDs afresh; numeric IDs may repeat across sessions.
- A confirmed move retains the original record and its KelpieId, updates its location,
  and invalidates/rechecks location-dependent evidence.
- A copy creates a separate record with a new KelpieId; the original retains its ID.

Implementation safeguards still to specify: late worker callbacks from an old scan
must not update a new scan whose numeric KelpieIds happen to repeat. Use an internal
scan-context token or equivalent lifetime control, without making KelpieId persistent.
Rescan flushing must not discard the independent operation journal or recovery facts.
Failed operations must retain the actual source/destination outcome instead of assuming
success. Re-observe changed files; never silently attach old evidence to a replacement
at the same path. Confirmed deletion should retire its available row and invalidate
dependent evidence; deletion/replacement handling remains implementation detail to settle.

Evidence states mean exactly what we know about each property and its sources, as
defined above. Reconciliation rules mean how PMR/MDB references are matched to files,
how local and scan-wide results differ, how all candidate locations are retained,
and when completion permits an unmatched conclusion. The following sections describe
the proposed rules; source completeness and match basis must remain explicit.

### Proposed matching and reporting rules

These restate existing proposals for discussion, not newly accepted per-field
selection priorities:

1. Enumerate supported physical locations and create one record per location.
   Normalize only according to the source/format and filesystem rules. Repeated
   discovery of the same location is not a second copy.
2. Use the PMR's recorded filename for local membership. Treat association as a
   database observation; when a header is checked, compare its complete file identity
   to the referenced identity. A filename hit with a different header identity is
   a mismatch, not a verified successful association.
3. Join MDB file objects using complete normalized file-source identities. Preserve
   the recorded graph references to master/source objects, including multiple or
   ambiguous references. Do not use a shared clip name or Master Mob ID to substitute
   one file for a different audio/video track.
4. Use readable header observations when databases are absent or insufficient;
   keep fields that cannot be established unknown. Retain observations already read
   from other sources rather than destructively overwriting them.
5. Index each file identity to every found physical location across all selected
   folders/drives. Finding several locations preserves every row; it does not prove
   payload equality or permit a silent choice of one copy.
6. Record local database-reference mismatches separately from scan-wide matching.
   A matching identity elsewhere does not repair the original folder's database.
7. Finalize scan-wide results only after relevant selected scope has completed.
   Unreadable locations, source-read failures and cancellation qualify the conclusion.
8. Compare only equivalent properties for the same subject/context. Retain agreeing
   and conflicting observations. Select a display value with a documented field-specific
   rule; leave selection unresolved when no rule is supported by evidence. The exact
   source priorities for individual fields still require an explicit specification.
9. Emit qualified local Console diagnostics and a reconciled end-of-scan summary.
   No phantom physical-file rows, automatic database rewriting, or automatic duplicate
   deletion follows from reconciliation.

## Source snapshots and freshness

A snapshot is a small receipt for an inspection, not an entire media file copied
into RAM. Record source type, volume/location, size, modification time, read time,
parser version, completeness/read outcome, and stronger version/fingerprint evidence
when available. Observations locate their precise PMR entry, MDB/bin object or header
descriptor/property within that source.

Distinguish `NotChecked`, `TimestampConsistent`, `ChangedSinceRead` and any stronger
validation states eventually implemented. Timestamp agreement is not proof of byte
equality. PMR timestamps require the parser's supported epoch/clock interpretation;
do not compare their raw integer directly to Unix seconds.

If a source changes while being read, mark or retry that inspection rather than
presenting a mixture of versions as one established snapshot. Parser changes can
require reinterpretation even when source files have not changed.

## Associations and absence across folders

Build shared identity-to-object and identity-to-physical-locations indexes for the
whole selected scan. Match after source-specific identity normalization, retaining
the original representation as evidence. Never deduplicate physical-file rows.

```text
File exists + database match
   -> `MediaFile` + database/header observations + associations

File exists + no local database
   -> `MediaFile` + header observations + recovered associations
   -> unknown fields stay unknown; local membership is unchecked

Database file reference + no file found in its recorded location
   -> retained object/reference + `ScanIssue`
   -> no invented physical-file row
   -> separately check whether matching media exists elsewhere in scan
```

Report both local absence and broader reconciliation: a file can be absent from the
database's folder but found elsewhere in the scan. Wait until relevant scan work is
complete before declaring a final unmatched result. Cancellation, excluded locations,
unreadable sources and unavailable drives must remain visible qualifications.

Store local and scan-wide results separately on each relevant diagnostic:

- Local result: `ExpectedLocationFound`, `ExpectedLocationNotFound`, or
  `LocalCheckIncomplete`, with the database folder and expected filename if known.
- Scan-wide result: `Pending`, `MatchingIdentityFoundElsewhere`,
  `NoMatchingIdentityFoundInCompletedScope`, or `ScanIncomplete`.
- Candidate locations: all matching physical-file KelpieIds, with the matching
  basis. Same clip/master alone is not proof of the referenced file identity.

Flag a completed folder's local mismatch in the Console, qualifying that wider
reconciliation may still be pending. At scan completion, emit one aggregate summary
and enrich those issues with matches across all selected drives. Do not erase the
local mismatch because a copy exists elsewhere, and do not duplicate the same issue
as unrelated local and global warnings. An optional dialog should use the completed
scan summary rather than interrupting each folder's read.

```text
Database in EDIT/MXF/1 expects file identity X at its recorded location
    |
    +-- not found there --> LOCAL mismatch retained
    |
    `-- whole selected scan:
           +-- X found on another drive --> list candidate locations
           +-- X not found in completed scope --> scoped unmatched result
           `-- drive unreadable / scan cancelled --> incomplete result

Finding X elsewhere neither repairs this database nor proves identical bytes.
```

Files without local databases are supported by header evidence when readable. This
is compatible with inspecting accessible media in managed/shared environments; it
does not provide an Interplay connection or recover all server-held editorial data.
No database reference does not establish that a file is unused.

## Console and optional dialog

The design must carry enough information to report:

- Database/source path and read completeness.
- Recorded filename where available; otherwise the file-source identity.
- Expected location and relevant clip/master/project observations where available.
- Scan roots and outcome, including a match elsewhere or multiple candidate copies.
- Why the reference is unresolved, and whether it actually represents expected
  physical media rather than a metadata-only or external-source object.

Recommended v1 presentation: an aggregate Console summary with inspectable individual
details. An optional end-of-scan summary dialog can point to those details. Avoid a
modal dialog for each entry. The precise dialog behaviour is not yet decided.

Suggested wording: "Database references media not found in this scanned folder."
For identity-only MDB cases: "Database file-source identity unmatched in scan."
Do not call every MDB object missing media: masters, source-history objects and some
file-source descriptions need additional interpretation before expecting a local file.
Unreadable/partial databases cannot establish that an entry was absent.

## RAM and scanning costs

Agreed policy: no application-defined memory cap and no fixed memory budget. Use as
little RAM as practical, but as much as required to retain the complete scan and its
evidence. Measure usage to eliminate waste, not to drop records/conflicts at a quota.
Physical system allocation failures must still be reported explicitly; the policy
does not mean available system memory is unlimited.

- Store shared source receipts and clip objects once; link to them from physical rows.
- Use typed values, compact IDs and shared strings/rule identifiers instead of
  duplicating long explanations per observation.
- Parse a database/bin once per unchanged snapshot within the session.
- Read metadata regions and skip media payloads. Bound parallel reads and temporary
  parser buffers; tune concurrency for the actual storage.
- Keep selected table values readily available; render detailed explanations on demand
  from retained observations. Avoid reparsing on every table refresh.
- Retain needed interpreted evidence; capture unknown/raw metadata selectively for
  investigation. Do not promise that current readers preserve every Avid property.
- If scan modes are introduced, label which sources were checked. A quick database
  pass cannot claim agreement with headers that were never read.

RAM-only evidence lasts for the current scan/session. Restarting requires a new read.
The operation journal remains separate and persistent for its existing recovery/Undo
responsibilities; it may retain the operation-specific snapshot it relies on.
