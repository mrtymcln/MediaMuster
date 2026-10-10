# MXF and OMF: metadata location and reading scope

Reviewed 10 October 2026, before choosing a media-reading or retention change.
This report records published format rules, Avid/workflow evidence, local
observations and proposed classifications separately. The specification review
changed no engine code; the subsequent reader-ownership refactor is noted below.

## Scope and meaning of “needed”

The review covers the core published MXF container/KLV/OP-Atom specifications,
relevant body metadata extensions, OMF 1.0 and 2.1, Bento, the original OMF
toolkit, and Avid/integration documentation. It does not claim to cover every
codec standard, every MXF application profile, or unpublished Avid extensions.
The historical Avid “MXF Unwrapped” PDF remained unavailable through its original
and archived URLs; no conclusion relies on having read it.

### Confirmed product scope and reader boundaries

After this review, the user confirmed the supported MXF family is **Avid-compatible
OP-Atom**, produced by Media Composer or third-party applications for Media
Composer, alongside **OMF/legacy media**. General MXF layouts in this report are
background and explain qualifications; they are not an instruction to expand the
product into a general-purpose MXF scanner. Proven Avid layout variations still
matter; a strict conformance rejection is not introduced by this scope decision.

Keep separate format readers and metadata interpretation: `MxfReader` and
`projectMxf` for MXF; `OmfReader` and `projectOmf` for admitted OMF/legacy
media, with native audio helpers for the previously agreed WAV/AIF support.
Share the source contract, `MediaFile`/evidence model and selection policy.
The user subsequently required independent MDB and OMF implementations, even
where their wire formats overlap. `MdbReader`/`projectMdb` now own their container,
object and summary decoding; `OmfReader`/`projectOmf` own the legacy-media path.
Disabling or removing legacy-media support must not remove MDB decoding. See
[reader ownership and verification](reader-boundaries-2026-10-10.md).

`FeatureFlags::kOmfScan` controls the OMFI-family path, enabled by default as
previously agreed. Off skips its media and databases; on includes the OMFI root
and admitted immediate subfolders with `.omf`, `.aif`, `.wav`, `.pmr`, `.mdb`.
The family gate already existed in MediaEngine; the subsequent independence refactor
changes implementation ownership, not byte-reading, retention or recovery policy.

Three different requirements must remain separate:

1. **Required in a conforming file:** the format says the author must write it.
2. **Needed by MediaMuster:** the scanner needs it to interpret/match/display
   a particular fact, when available.
3. **Worth retaining:** it may have no current column but belongs to the user's
   original-metadata preservation goal.

For example, a format can require a playback index without requiring our
inventory scanner to expand every entry. Conversely, a private property can
matter to MediaMuster even though the public baseline does not require it.
“Needed” never permits fabricating an absent value or rejecting every file
that lacks a table field. Keep NotRead, Absent and Unreadable distinct.

## What is physically where?

### MXF generally

MXF partitions have declared metadata/index extents. Header partitions may also
contain recording data; Body partitions may repeat header metadata. Footers
contain no recording essence but can contain indexes and header metadata.
Open metadata can be updated later; closed copies have defined authority.
The optional Random Index Pack locates partitions; it is different from a
frame-access Index Table. These rules come from
[ST 377-1, sections 6–7, 9 and 12](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf).

### Completed conforming OP-Atom

[ST 390, sections 5 and 8–9](https://pub.smpte.org/pub/st390/st0390-2011_stable2016.pdf)
requires a closed, complete initial header, header metadata only there, one
top-level file package/essence track/container, and a complete footer index.
Its recommended arrangement is:

    Header                 Body                    Footer
    metadata               recording data          playback index

A single essence track can contain multichannel audio. Creation-in-progress
is a separate case: section 8.2.5 describes using an open generalized pattern
before rewriting the final header. Avid's
[managed-dailies guidance](https://kb.avid.com/pkb/articles/en_US/Knowledge/import-dailies-into-MC)
identifies qualified OP-Atom as the intended managed MXF format.

Therefore, the general MXF example of updated footer metadata must not be
presented as the normal layout of a completed conforming OP-Atom file.

### Useful metadata outside header metadata

The [Generic Container specification, sections 5–7](https://pub.smpte.org/pub/st379-1/st0379-1-2009_stable2015.pdf)
defines System, Picture, Sound, Data and Compound items. The
[System Scheme specification](https://pub.smpte.org/doc/st394/20060707-pub/st0394-2006.pdf)
defines metadata/control associated with those contents. Body data therefore
cannot universally be described as only pictures and sound.

[Generic Stream, section 6](https://pub.smpte.org/pub/st410/st0410-2008.pdf)
also permits a special Body partition for generic KLV data.
[ANC/VBI mapping](https://pub.smpte.org/latest/st436-1/st0436-1-2013.pdf)
places ancillary content in recording data. Avid explicitly documents both
ancillary data embedded in DNxHD and separate D-track MXFs in its
[ancillary-data guidance](https://kb.avid.com/pkb/articles/en_US/Knowledge/Preserving-HD-Closed-Captioning-and-Ancillary-Data).

Decoding captions is unnecessary for today's table. Keeping a referenced
physical D-track file when gathering a sequence's media is still relevant.
The approved Kind display is Audio/Video/blank; adding Data would require a
separate user decision. No new column or discovery exclusion is introduced.

### OMF 1 and OMF 2

OMF has a different physical model. Its logical HEAD object is an index/root,
not a contiguous block at the file beginning. Objects can be stored in any
order. OMF 1 explicitly supports random access and semantic traversal through
recorded references, rather than treating every physical TOC entry as a
separate active media object.
[OMF 1, printed pages 10–12 and 48–49](https://web.archive.org/web/20030809131248if_/http://www.peakoverload.com/Downloads/omfspec10.pdf).

The standard Bento end label locates the contents list. That list describes
object/property/type identifiers and value locations/lengths. Values can be
inline or fragmented. The actual names/descriptors may therefore require
additional indexed reads; reading the contents list alone is insufficient.
[Bento 1.0d5, printed pages 55–63](https://web.archive.org/web/20120906101250if_/http://info.wgbh.org/upf/pdfs/BentoSpec1_0d5.pdf).

    End label -> contents list -> HEAD and recorded object references
                                  -> dictionaries
                                  -> descriptors and other property values
                                  -> recording-data locations

[OMF 2.1, printed pages 8–9, 49–50 and 152–155](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf)
describes HEAD membership, Mobs, MediaData and optional PrimaryMobs. Version 1
and 2 index/type conventions differ; the toolkit's
[registrations](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omFile.c)
confirm those distinctions. The end label is navigation information, not an
MXF-style metadata footer.

Some format information occurs within an OMF recording property. Audio
descriptor Summary values provide descriptive information without samples;
TIFF data can include directories/tables/indexes as well as pictures.
[OMF 2.1, printed pages 113, 217 and 228–230](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).
The original toolkit reads summaries and compares them with media headers in
its [WAVE](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omcWAVE.c)
and [AIFC](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omcAIFF.c)
code. Missing summaries can justify a small format-header read; they do not
justify copying or decoding the whole recording.

## Classification for MediaMuster

This table is an engineering recommendation for the agreed features and
preservation goal, not a format-mandated UI or an approved new retention policy.
Property names refer to current MediaMuster concepts in
[MediaProperty](../src/mediaevidence.h) and [MediaFile](../src/mediafile.h).

| Information | Priority and purpose | Appropriate representation/read scope |
| --- | --- | --- |
| Container identification, version, byte order and metadata bounds | **Essential** to interpret safely | Compact typed navigation information plus original framing where acquired; validate before following a location |
| MXF Primer; OMF property/type/class definitions | **Essential** where they determine a property's identity or encoding | Preserve original definitions; interpret the needed ones without permanently expanding every definition |
| Active root and membership lists | **Essential** to establish which objects describe the media | Follow recorded membership and references; retain unlisted objects as observations without silently selecting them |
| FileMobId, MasterMobId and source/track references | **Essential** for file/bin/sequence matching | Preserve complete typed identities and every valid relationship, with owner/source context; do not infer meaning from ID length |
| ClipName, Project, OriginalBin and related attributes | **Essential for the approved displayed fields when present** | Keep source observations and the shared selection policy; header text is not necessarily the latest Interplay/bin rename |
| CompressionLabel, WrappingLabel and relevant descriptor class | **Essential** for compression/container interpretation | Keep original identifier and applicable format facts; apply verified naming mappings |
| Resolution rectangles/offsets, FrameRate and frame layout | **Essential** for current video display | Keep recorded geometry/rationals; derive the displayed value using the approved geometry rules |
| BitDepth, SampleFormat, component layout and Alpha | **Essential to their approved metadata semantics** | Store independently with evidence; SampleFormat stays internal and Alpha visibility remains gated |
| SampleRate, Channels and channel assignments | **Essential for correct audio interpretation; names/roles are useful retained detail** | Keep source distinctions; no new channel table/CSV column |
| FileDuration, exact units/rates and applicable DropFrame | **Essential** for current duration | Use relevant recorded lengths/clocks, not sibling counts or guessed playback values |
| ClipDuration and master/track associations | **Essential when its existing feature is enabled; preserve evidence independently** | Keep each related track's length and context separately |
| SourcePath, SourceFilename, imported/source-container information | **Needed for the approved source fields when present** | Keep recorded values distinct from the file's current location |
| Type, precompute/effect identifiers and associations | **Needed for existing classification/filter features** | Interpret established tokens/classes; preserve private tokens when their meaning is unresolved |
| Other private/custom header properties, creator history, object timestamps, colour details and unused definitions | **Needed for original-header preservation; optional to interpret/display now** | Retain exact bytes, identity, encoding and relationships compactly; a missing UI column is not a reason to discard them |
| Thumbnails/previews and opaque header data | **Potentially useful retained header detail** | Preserve under the chosen metadata scope; size/type must not be guessed from an unfamiliar name |
| MXF partition packs and validated navigation; OMF contents list | **Essential navigation** | Keep needed positions, lengths, identifiers and status. A complete MXF partition map/Random Index Pack is an optional navigation aid, not a universal reading requirement; validate it where used |
| Playback index segment information | **Conditional** for targeted duration/consistency checks | Read established segment fields if a feature needs them; indexed presence alone does not require expanding every frame entry |
| Every individual playback-index entry | **Optional deeper inspection**, not needed to populate ordinary metadata cells | A full array is primarily a playback/seek structure; retaining original index bytes is a separate scope decision |
| System/ANC/caption and other time-varying contents | **Optional deeper content inspection** for the current product | Do not call them meaningless; whole-header retention does not preserve all metadata streams everywhere |
| Other Generic Stream contents | **Depends on the identified metadata/data and feature** | Not necessarily per-frame. Interpretation and original-byte retention are separate decisions; an unfamiliar stream is not automatically irrelevant |
| Every recording packet's detailed layout | **Optional structural diagnostics** | Compact ranges can suffice for navigation; thousands of rich descriptive objects are not prescribed by the file format |
| Actual picture and sound sample payloads | **Unnecessary in normal scan RAM** | Leave on disk; narrowly scoped sample-format checks, copying, hashing or playback have different read needs |
| Padding values and superseded physical bytes outside current indexed values | **No current metadata meaning** | No displayed value should be invented; keep necessary extents, and distinguish exact whole-region copying from semantic preservation |
| Path, Size, Date Created, Modified, VolumeIdentifier, KelpieId and OmfScan | **Essential application/filesystem facts, not header metadata** | Collect from the filesystem/scan session/layout; do not substitute Avid object dates or identities |

The classification above does not authorize dropping already retained evidence.
For example, “optional to interpret” can still mean “keep the original bytes.”
Known properties absent from a source retain their appropriate read state.

## Private Avid metadata is not expendable

The BBC's real Avid interoperability work required an AAF-style metadictionary
and an object directory in the MXF header. The dictionary supplied class,
property and type information beyond the Primer's subset. Their Avid and
Panasonic OP-Atom variants also needed rewrapping for interchange.
[BBC WHP 155, PDF pages 7–8](https://downloads.bbc.co.uk/rd/pubs/whp/whp-pdf-files/WHP155.pdf).

This supports preserving those original structures. It does not establish that
every definition needs a full-time C++ object, or that every modern Avid version
has the identical private layout. An unfamiliar type/property cannot be marked
irrelevant just because MediaMuster does not yet understand it.

## Interplay and live-media qualifications

Avid documents an MXF SDK case involving an open/incomplete initial header and
missing finalized footer metadata in
[Media Director 2.1, MPI-20379](https://resources.avid.com/SupportFiles/attach/MediaDirector_TechnicalOverview_v2_1.pdf).
Telestream's actual Avid integration documents growing Frame Chase assets and
a final metadata update after capture in the
[Avid Integration Guide, Media Creation Action Configuration](https://www.telestream.net/pdfs/app-notes/Avid_Integration_Guide_V2025.2.pdf).
These are published workflows, not invented damaged-file examples.

Interplay also has a separate asset metadata database and media storage.
[Interplay Administration Guide 3.4, printed page 17](https://resources.avid.com/SupportFiles/attach/InterplayAdminGuide_V3_4.pdf).
The [Avid asset-medias API](https://developer.avid.com/ctms/api/pa/resources/asset-medias.html)
distinguishes an asset from its physical media items, paths, tracks and formats.
Preserving an MXF header therefore does not promise preservation of every
current bin/Interplay user field or sequence edit.

OMFI-folder WAV/AIF remain relevant. Avid distinguishes modern SMPTE IDs,
legacy Pro Tools IDs and Media Composer OMF-chunk IDs in
[Pro Tools 2019.10, SMPTE ID in Wave Files](https://resources.avid.com/SupportFiles/PT/Whats_New_in_Pro_Tools_2019.10.pdf).
Its [field-recorder metadata guidance](https://kb.avid.com/pkb/articles/en_US/FAQ/Supported-Field-Recorder-Audio-Files-and-Metadata)
also documents file-borne production metadata. Folder family, actual container
and identity source remain separate concepts.

## What the existing evidence establishes

The saved [3,832-file framing inventory](evidence/fresh-mxf-klv-counts-2026-10-04.jsonl)
was recounted for this review. Every entry has nonzero initial-header metadata,
zero Body/Footer HeaderByteCount and one recording/system packet. All initial
headers have status value 4. This is local/EDIT evidence, not a new Windows scan.
The [complete-file reader proof](fresh-mxf-reader-2026-10-04.md) requested about
2.9 MB of logical bytes across about 2.66 GB of twenty media files, without
reading their independently identified recording ranges. MediaEngine does not load
all those recordings into RAM.

Four real [root-membership specimens](root-membership-specimens-2026-10-08.md)
lack Preface.PrimaryPackage. That field is optional in the general
[libMXF baseline model](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_baseline_data_model.h)
but mandatory under strict ST 390. Thus an Avid folder or an OP-Atom label
is not proof of full profile conformance. Do not introduce a strict rejection
rule that excludes these ordinary files.

The saved inventory demonstrates that retaining more original metadata can be
achieved within the initial header for those files. It does not establish that
all Windows/Interplay files have that layout. Most KLVs there describe genuine
initial-header sets/dictionaries, not repeated recording blocks; improving the
RAM representation is therefore a distinct concern from narrowing read scope.

## Current MediaEngine limitations relevant to this decision

- [MxfReader](../src/mediaengine/mxfreader.cpp) physically visits KLV headings to EOF
  and skips recording/index payloads. It retains metadata by partition and keeps
  framing/range observations.
- Unknown header KLV values and unsupported local-set payloads can be retained
  **as ranges only**. A range preserves where bytes were, not the bytes themselves.
  Current MediaEngine therefore does not retain every possible original header byte.
- Body/System/ANC payloads are not comprehensively interpreted. Walking the
  full physical structure is not equivalent to preserving every metadata stream.
- [MxfProjection](../src/mediaengine/mxfprojection.cpp) preserves separate partition
  candidates; it does not automatically select closed/latest-generation metadata
  under the general MXF authority rules. No affected local sample is established;
  handling actual open/growing files needs focused qualification before a change.
- [OmfReader](../src/mediaengine/omfreader.cpp) uses metadata-only Bento reading.
  [Bento metadata reading](../src/mediaengine/omfbentoreader_p.cpp) skips seven
  toolkit-identified recording properties, but reads other values, including
  unfamiliar ones. A private DataValue can be large recording data; its generic
  type alone cannot establish that it is small metadata.
- [SourceArchive](../src/mediaengine/sourcearchive.cpp) compresses completed expanded
  records and releases them. Native-byte storage can avoid some conversion and
  serialization overhead, but narrowing traversal alone cannot remove the
  representation cost of all the initial-header properties.

These are observations about code and scope. They do not prove the exact fatal
mechanism of the Windows crash, nor authorize new selection rules.

## Opportunities to review next, without choosing them here

1. Retain acquired original metadata and compact indexes in RAM, with exact
   source context; expand detailed views only when useful. Lossless compression
   remains an option that must be measured.
2. Use validated partition-location information to inspect metadata-bearing
   sections directly, rather than routinely visiting every recording heading.
3. For OMF, use the label/TOC and recorded object graph to access metadata;
   use descriptor summaries before narrowly targeted media-format headers.
4. Decide separately what happens when navigation information is absent,
   unreliable, or the source is still being written. No universal first-N-byte
   limit, silent broad recovery, or new default is selected by this report.
5. Compare original bytes, identities, object relationships, evidence states,
   selected values and CSV on genuine files. Include header-heavy Windows
   data before promising that the 300,000-file workload completes.

The PMR/MDB-first policy, duplicate physical rows, feature flags, source priorities,
UI and file operations remain unchanged. Old readers/tests remain available.

## Additional specifications and reference receipts

- [ST 336:2017](https://pub.smpte.org/latest/st336/st0336-2017.pdf): KLV identifiers,
  lengths and local-set coding. Referenced for framing; not a fixed header-size cap.
- [ST 377-4:2021, sections 6.3–6.4](https://pub.smpte.org/latest/st377-4/st377-4-2021.pdf):
  multichannel label subdescriptors, tag names and channel IDs. Channel labels
  are real metadata; no channel UI change is approved here.
- [LWKS-preserved OMF toolkit](https://github.com/LWKS-Software/omfkt22):
  registrations, TOC access and audio summary/media-header separation. Reference
  implementation, not evidence that every producer follows all baseline rules.
- OMF 1.0: third printing, 3 May 1994, 124 recovered PDF pages. The archived
  response had a 208-byte transport prefix before the PDF; only that prefix was
  removed for reading. Clean PDF SHA-256:
  7a6cbcc57157364f0cec9f27aebe042667a073761d98b0d614c7c8433341562b.
- Bento 1.0d5: 106 PDF pages. SHA-256:
  f86bf158ea45b5e10ce8b5021e5eac4f66e6fbcfa80fd36f72a9c74fd21a5f5e.
- Toolkit reference hashes: omFile.c
  ddeb8da392f4e41688db3e9824b729281fadce6a78e4152b2f33a5dd18e1201f;
  TOCIO.c d34c8571b3badbc6d0eb8b6570e803548c5448192424e63ad245a60134170188;
  omcWAVE.c 4c955b6137a3b9d455678a4e85c16081624b59127df6941dde65140212e30575;
  omcAIFF.c eedbedd3b4204f695dbea13baebad878909784c5eba7f1316274ab3aaed35fce.

Reference downloads were read in temporary storage; third-party PDFs were not
added to the repository. Sources establish layouts and capabilities, not measured
future speed/RAM savings or universal Avid-private completeness.
