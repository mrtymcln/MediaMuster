# Legacy compression and database audio summaries

Implementation and verification: 8 October 2026. This is the user-authorized
follow-up for F17, F20 and F21, following a
separate read-only investigation of their evidence.

## Before and after, in plain English

| Topic | Before | After |
| --- | --- | --- |
| Older Avid compression names | A verified Compression/resolution pair was consulted only when the owning MobId occupied 12 bytes. A successful lookup could overwrite a name obtained from a recorded compression label. | The actual descriptor class, property types and verified pair establish applicability. A recorded coding label permits refinement only when its exact compatibility is established. MobId length does not participate. |
| Copied audio headers in MDB/OMF | Original Summary bytes survived, but a separate small decoder reduced them to selected audio facts. The native reader decoded more detail. | Native chunks and Summary chunks share the format-field decoder. Individual fields retain their bytes, source ranges, decoded meaning where supported, and read state. Original Summary bytes remain intact. |
| WAVE storage width versus precision | A partly readable extensible Summary could supply storage width as an unqualified Bit Depth even when the subtype/precision was unavailable. | Storage width remains a recorded fact. It does not stand in for unknown valid precision; independent sample rate and channel facts remain usable. |
| AIFF-C descriptive compression name | Native headers decoded the counted name; Summary paths retained it only inside the original bytes. | Both paths decode it consistently. A missing descriptive name does not erase independently readable compression type, bit depth or sampling rate. |
| Omitted audio samples in a Summary | The special Summary decoder correctly avoided demanding the entire recording. | That distinction remains. Shared format-field decoding does not turn the Summary into an ordinary full-file validation request. |

These changes do not add a new display column, persistent database, recovery engine
or compression family. Source selection preferences remain in the shared
policy table.

## Compression evidence and boundaries

Avid's OMF specification defines the image descriptor's Compression property
separately from Mob identity: printed pages 133 and 175 in the
[OMF 2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=145).
The standard UID is 96 bits in both OMF revisions; its width does not distinguish
OMF1 from OMF2.

The original Avid toolkit selects codecs through descriptor classes and codec-specific
property examination. Its JPEG selector accepts JPEG/JFIF, and its Avid resolution
table independently identifies resolution ID 82 as JFIF20:1:

- [Toolkit codec dispatch](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omSearch.c).
- [Toolkit JPEG descriptor examination](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omcJPEG.c).
- [Avid JFIF20:1 resolution definition](https://github.com/LWKS-Software/omfkt22/blob/main/avidjpg/AVRJFIF35.h).

Avid's supplied OMF slates, corresponding `.vr` resolution resources and regenerated
MDB corroborate the admitted mappings. The existing short-name table is consulted
only through a typed descriptor/pair whitelist; its explicitly unverified DV100 rows
are excluded. Compatible explicit coding labels are a finite set, corroborated by
the specimens and BBC's independently published
[Avid label definitions](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_avid_labels_and_keys.h)
and [standard coding labels](https://github.com/BBC-archive/libMXF/blob/main/mxf/mxf_labels_and_keys.h).
An unknown, unreadable or different coding label is not authorization to overwrite it
with a private-table name. The raw inputs remain available, with an explanation when
the refinement cannot be established.

**Evidence limit:** the initial slate-MDB pair corpus has 12-byte owning identities.
The two live MXF-folder DV cases below separately establish genuine 32-byte owners
for their DV pairs. The 32-byte-owner/JFIF example remains an authored independence
check: it proves that compression interpretation no longer depends on that unrelated
width, without certifying that Avid writes that JFIF combination or establishing a
formerly wrong genuine JFIF row.

## Audio evidence and boundaries

The OMF specification describes AIFD/WAVD Summary properties as copies of audio
descriptive information, with sample data omitted: printed pages
[113](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=125) and
[217](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf#page=229).
This establishes the OMF property semantics; genuine MDB observations establish
that Media Composer also records those properties in its database.

The real `/Users/Shared/AvidMediaComposer/OMFI MediaFiles/msmMMOB.mdb` contains:

| Descriptor object | Summary | Bytes | Independently observed information |
| --- | --- | ---: | --- |
| 68013 | WAVD | 4,152 | PCM, mono, 48 kHz, 24-bit |
| 68070 | WAVD | 4,152 | PCM, mono, 48 kHz, 24-bit |
| 68127 | AIFD | 726 | AIFF-C `in24`, recorded name `24-bit Integer`, mono, 48 kHz, 24-bit |

The original bytes, property locations, metadata chunk payloads and hashes are
preserved in [the genuine-source receipt](evidence/audio-summary-source-evidence-2026-10-08.json).
The corresponding WAV/AIF tone files carry identical copies of two of these summaries.
The WAV Summary's outer RIFF length describes the short copy. Its internal `data`
header advertises 8,640,006 sound bytes which are deliberately absent. The decoder
does not reject that legitimate layout as a truncated recording.

Microsoft defines storage width, valid precision, channel mask and SubFormat as
different WAVE facts. Its documented 20-bit-in-24-bit example motivates retaining
both numbers; it is not represented as an observed Avid specimen.
[WAVEFORMATEXTENSIBLE specification](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible).
Apple separately defines AIFF-C's compression identifier and human-readable name.
[Extended Common Chunk specification](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-83.html).

**Evidence limit:** no genuine extensible-WAVE Summary was found in this inspected
corpus. Missing GUID/name and conflicting-summary controls are authored. They check
the established field boundaries and evidence handling, not undocumented Avid
layouts or damaged real media. Readable fixed fields need not be discarded because
an independent extension or description is unreadable. The original Bento framing
outcome is not a certificate that every audio subfield is understood or valid.

## Verification

Both `build/MediaMuster.app` and `build-canon/MediaMuster.app` rebuilt successfully
for arm64 and x86_64. Runtime verification used arm64, Qt 6.5.3 and macOS 15.8.1;
Windows/NEXIS was not exercised in this pass. The normal app's deep/strict code
signature verification succeeds. These are Debug builds.

The first coordinated run exposed an outdated MDB test count:
it counted newly decoded child fields as extra physical TOC entries. The corrected
test still requires the exact original count, verifies each child against its
retained parent, and reconstructs every retained value from the original file's
byte ranges. The genuine audio fixture retains all **1,368 original entries** and
adds **24 decoded chunk/field receipts**, rather than inventing database entries.

The new controls exercise descriptor class/type admission, identity-width
independence, compatible and conflicting coding labels, shared native/Summary
decoding, fragmented source ranges, individually unreadable extensions, and
repeated summaries whose rates agree or conflict. They do not establish new
historical writer layouts. Existing guarded reader checks still prevent sound
sample reads. No new corruption-recovery path was added.

The [genuine MDB decoding receipt](evidence/audio-summary-decoded-evidence-2026-10-08.json)
was produced by a [small read-only probe](evidence/audio-summary-decoded-probe-2026-10-08.cpp)
linked against the rebuilt MediaEngine library. The three original summaries retain
their exact hashes. It separately records AIFF-C's `24-bit Integer` at absolute
MDB offset **10,523**, length **14**. The existing diagnostic about two ambiguous
dictionary definitions remains; it is not silently suppressed or treated as a
new audio-summary failure.

### Real media comparison

The read-only repeat scan uses the same managed roots as the prior MediaEngine receipt:
`/Users/Shared/AvidMediaComposer` and `/Volumes/EDIT`. This is the established
baseline scope, not a new claim that every directory on both volumes was searched.

| Check | Result |
| --- | --- |
| Physical media rows | **2,413**, unchanged: 361 local and 2,052 on EDIT |
| Media types | 2,411 MXF, one WAV, one AIF |
| Added, missing or duplicate paths | None |
| KelpieId | Nonzero and unique for each physical row; excluded from cross-session value comparison |
| Databases read | Six PMRs and six MDBs, unchanged |
| Media headers | 116 read; 2,297 deliberately unopened, unchanged |
| Discovery/read scheduling | No source-path, read-outcome or read-reason changes |
| Existing scan notices | Same 298 notices and details as the earlier receipt |
| Exported metadata | Exactly two Compression cells change; all other cells match |
| Recorded scan time | 21,141 ms |
| Retained process footprint | 2,531,203,584 bytes (about 2.53 GB / 2.36 GiB) |

The CSV comparison normalizes the already approved `Codec` → `Compression`
heading rename, and compares rows by exact Location. The two name refinements are:

| File in local `Avid MediaFiles/MXF/1` | Before | After |
| --- | --- | --- |
| `V01.E69CEDE0_F926CF926C983V.mxf` | DV NTSC 25Mbps 4:1:1 | DV 25 411 |
| `V01.E69CEDFB_F93B5F93B54A0V.mxf` | DV NTSC 50Mbps 4:2:2 | DV 50 |

These are Avid short names for the same supported encoding families, rather than
evidence that the earlier rows described a different codec. The matched MDB's
typed private properties can now refine those compatible coding labels despite
the owning identity's 32-byte storage. Exact descriptor inputs and field locations
are recorded in [the DV name receipt](evidence/legacy-audio-dv-name-evidence-2026-10-08.json).
Those two files' own MXF headers independently agree with the MDB identities and
coding labels; the header-only names remain the longer DV descriptions.
The two legacy tone rows remain PCM / 24-bit / 48 kHz.

Evidence: [comparison receipt](evidence/legacy-audio-real-scan-comparison-2026-10-08.json),
[current CSV](evidence/full-scan-legacy-audio-2026-10-08.csv),
[scan log](evidence/legacy-audio-real-scan-2026-10-08.txt),
[source/row/notice detail](evidence/legacy-audio-real-scan-2026-10-08.json),
and [build/source verification receipt](evidence/legacy-audio-verification-2026-10-08.json).
The filesystem cache was uncontrolled; these measurements are not a controlled
speed or memory comparison. The inspected MDB hash is unchanged after the scan.

## Audit assessment

F17, F20 and F21 are **Resolved within the admitted scope**: the remaining
identity-width criterion is removed, supported audio fields survive separately,
and unreadable extensions no longer certify unknown precision or erase readable
independent facts. This does not certify every private compression mapping,
audio subtype or writer. The genuine/authored boundaries above remain explicit.
The [updated ledger](evidence/audit-closeout-ledger-2026-10-08.json) records
28 Resolved, 6 Partly resolved, 1 Open, 1 Needs evidence and 4 Accepted scope limits.
