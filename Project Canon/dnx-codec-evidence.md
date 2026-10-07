# DNx codec evidence

Checked 3 October 2026. Research only; no parser fix is implemented by this note.

## Primary references

- [Avid DNx naming scheme and data rates](https://kb.avid.com/pkb/articles/en_US/Knowledge/Avid-DNx-naming-scheme-and-data-rates), updated 21 April 2026. Use its current capabilities when considering newer DNx files; older profile tables are version-specific.
- [SMPTE RDD 50:2019, submitted by Avid](https://pub.smpte.org/pub/rdd50/rdd50-2019.pdf): section 9/Table 5 and sections 10.2–10.3/Tables 7–8 give the DNxUncompressed coding labels and descriptor interpretations. This is a Registered Disclosure Document, not a SMPTE Standard.
- [BBC libMXF types](https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_types.h) define the eight code/depth pairs used by an MXF RGBA layout.
- [SMPTE ST 2019-1:2016](https://pub.smpte.org/latest/st2019-1/st2019-1-2016.pdf): compressed VC-3 essence header section 7.2.4, compression parameters Annex C, and profiles/levels Annex H. This edition establishes historical format facts, not complete coverage of later revisions.
- [Avid Media Composer 2025.12 naming changes](https://www.avid.com/resource-center/whats-new-avid-media-composer-202512): current unified product terminology.
- [Autodesk Flame DNxHD compression names](https://help.autodesk.com/cloudhelp/2024/ENU/Flame-ImportingandExportingMedia/files/GUID-D05C5C21-74DD-4A75-B38F-EF40CC1B9622.htm): historical marketing-name examples. Nominal DNxHD names are not interchangeable with rounded calculated bitrates.
- User-supplied [DNx Specs new.pdf](/Users/martymclean/Downloads/DNx%20Specs%20new.pdf), *The Avid DNx Video Codec*: pages 5–6 explain the historical HD/HR brands and resolution-independent profile. Relevant pages reviewed visually because much of this PDF is image-based.
- User-supplied [DNx Specs old.pdf](/Users/martymclean/Downloads/DNx%20Specs%20old.pdf), *Avid DNxHD Technology*, copyright 2012: pages 9–10 give the historical nominal names by project format, raster, colour sampling, depth and frame rate. Tables and footnotes reviewed visually.

## Confirmed correction to the existing assumption

The coding-label variant and component depth must be interpreted together. Standard-format variant 01 is not exclusively floating point. Fixed-point variant 02 includes S2.14, 10.6 and 12.4. Depth 254 alone cannot distinguish S2.14 from float. Inspect alpha independently through PixelLayout or Alpha Sample Depth.

### DNxUncompressed interpretation reference

Label suffixes below belong to the complete DNxUncompressed picture-coding ULs in RDD 50 Table 5. They are not a suffix-matching rule for arbitrary codec labels. Depth means CDCI Component Depth or an applicable RGBA component's Depth.

| Coding-label suffix | Depth | Interpretation |
| --- | --- | --- |
| `03070100` | 8, 10, 12, 16 | Integer |
| `03070100` | 253 | 16-bit half float |
| `03070100` | 254 | 32-bit float |
| `03070200` | 254 | 16-bit S2.14 fixed point |
| `03070200` | 10 | 10.6 fixed point |
| `03070200` | 12 | 12.4 fixed point |

Reference: RDD 50 sections 9–10.3. Interpret components separately; alpha need not share the colour sample representation. Validate complete descriptor and format-header combinations rather than treating this abbreviated table as exhaustive syntax validation.

The comment in `src/mediametadata.cpp` above `bitDepthLabel()` says the descriptors are byte-identical and assigns one label to both representations. The saved audit evidence contradicts that statement: the component-depth values agree, but the picture-coding labels differ.

Local corroboration:

- `docs/reviews/2026-10-03-format-audit/evidence/sentinel-254.json` retains both coding labels alongside raw depth 254 and the current displayed value.
- Installed Media Composer `SupportingFiles/DynamicRelinkUI/DRUI.xml`, lines 8219 and 8381, provides separate 16(2.14) and 32-bit float choices with different coding-label filters. This corroborates installed application behaviour; it is not a universal format specification.
- Audit F02 records genuine alpha-only 16-bit PixelLayout evidence, including `/Volumes/EDIT/Avid MediaFiles/MXF/86452/V02.6A0415BB_154BE154BEED3V.mxf`. The existing special case for 8-bit alpha does not cover it.

## Project Canon consequences and proof work

Preserve coding labels, raw depth values, complete component layouts and alpha-depth observations independently. Resolve a readable sample-format description from that evidence with a cited rule. Codec capability does not establish the component layout of an individual file. A declared alpha channel also does not establish that any pixel is translucent.

Acceptance checks should distinguish the two real depth-254 fixtures, cover ordinary integer and half-float representations, and distinguish RGB, RGB plus alpha, alpha-only, and unreadable layouts. Verify disagreements against the associated essence's format header when necessary, without decoding every frame. Test older and newer compressed DNx profiles separately; do not apply DNxUncompressed rules to VC-3 compressed essence. Keep unknown or contradictory combinations explicit rather than inventing a display value.

## DNxUncompressed flavour and agreed alpha column

User direction: correctly list the DNxUncompressed flavour. When implemented, put the `Alpha` table column behind a feature flag. Its only nonempty values are `Yes` and `No`; unknown remains empty. This supersedes the earlier five-label column proposal. No production UI change is implemented by this note.

Proposed display examples, once validated from the file's recorded properties:

- `Avid DNxUncompressed — 4:2:2, 32-bit float`
- `Avid DNxUncompressed — 4:2:2, 16-bit S2.14 fixed point`
- `Avid DNxUncompressed — RGB, 16-bit half float`

Keep the underlying family, sample representation, per-component depths and component layout independently addressable. A formatted codec cell is derived presentation, not the sole stored fact. Preserve which associated descriptor/track supplied each observation; do not take the first picture descriptor anywhere in a file.

Agreed `Alpha` column values:

| Display | Required meaning |
| --- | --- |
| No | Successfully interpreted evidence establishes no alpha in this file's relevant essence |
| Yes | Alpha exists in this file, either alongside colour components or as alpha-only essence |
| Empty | Unknown, not read, unsupported, incomplete or unreadable evidence; also an unresolved conflict that prevents a reliable selection |

These display labels do not replace the agreed read-state and agreement enums. Store alpha presence, layout/role, depth and conflicts with provenance; alpha-only remains distinguishable internally even though its column says `Yes`. The feature flag controls column availability/presentation, not evidence collection. No default flag setting or flag identifier is agreed yet. Do not show `No` simply because a reader did not find or support an alpha property. Do not mark a colour file `Yes` just because a related, separate alpha file exists: clip-level alpha availability and file-level alpha content are different facts. This applies to compressed DNx too, using its corresponding signalling rules rather than assuming DNxUncompressed descriptor rules are sufficient.

## Current codec table: useful, but not a complete naming model

Inspected `src/mediametadata.cpp` on 3 October 2026:

- `kCodecs` maps coding labels to strings, with DNx strings serving as routing keys into `kDnxTiers`.
- `kDnxTiers` combines a current brand and historical rate-dependent DNxHD names in one returned string. The resolver receives a coding label and a formatted frame-rate string, not a structured profile/raster/scan-type description.
- Its DNxHR rows have empty numeric-name cells, so the returned value is only `Avid DNx <level>`: the original `DNxHR <level>` routing key is lost from the display result.
- Separate 444 and thin-raster entries bypass this two-name resolver. Do not claim that all supported profiles already receive both names.
- The DNxUncompressed fixed-point label is named `Avid DNxUncompressed 2.14` without examining depth; this does not cover its other fixed-point representations correctly.
- `OmfObjects::ulFromResId()` synthesizes labels across IDs 1235–1489 using arithmetic, with one version exception. Its comment cites ten observed IDs. That observation does not validate every accepted ID, establish all label revisions, or prove that every MDB resolution ID is an essence CID. Retain the original source/property distinction and audit unsupported combinations.

Retain the verified parts of these tables as reference data. This inspection does not certify every existing row or nominal-bitrate cell.

## Proposed identity and name resolution

Keep a structured, versioned codec catalogue whose entries cite their evidence, rather than making display strings the technical keys. Retain raw observations separately from all derived names.

| Information | Evidence or resolution method |
| --- | --- |
| Recorded coding label | Relevant media descriptor, or identified database/bin property with its encoding/context |
| Recorded compression ID | Compressed VC-3 essence header when read; keep separate from Avid database resolution IDs |
| Encoded profile and quality level | Verified mapping for that identifier and format revision, checked against recorded geometry, layout and sample properties |
| `NewDnx` | Branding rule applied to the established profile/level based on April 2026 white paper |
| `OldDnx` | Historical profile/level naming rule, when it has an exact counterpart |
| `ReallyOldDnx` | Verified historical DNxHD nominal-name table plus the necessary profile/raster/scan-type and rational edit rate |
| Actual data rate | Separate measurement/calculation, never silently substituted for a historical marketing name |

ST 2019-1:2016 section 7.2.4 defines a compression-ID area in the essence header; Annexes C and H distinguish fixed HD and resolution-independent profiles and their levels. This is more concrete identity evidence than guessing the profile from dimensions or bitrate. A DNxHR encoding can have HD dimensions.

Start with correctly associated header metadata. Reading a small format header from the relevant essence is a targeted cross-check/fallback when necessary; naming does not require decoding every frame. Preserve disagreements between descriptor labels and essence fields as competing observations. Unsupported identifiers remain unresolved until their definitions are verified.

### Three distinct naming schemes

User decision: call the three schemes `NewDnx`, `OldDnx` and `ReallyOldDnx` in Project Canon and future implementation code. These are MediaMuster's agreed internal scheme names, not Avid terminology or claims about source property names. `NewDnx`, such as `Avid DNx HQX`, must be prominent. Retain the two older schemes separately rather than grouping them into a single legacy string. Different Media Composer versions or other applications may record or display different schemes. Retain any actually recorded name with its source/application context when available; keep a generated alias explicitly derived. This documentation change does not rename production code yet.

| Scheme | Example | Applicability |
| --- | --- | --- |
| `NewDnx` — current unified brand and level | `Avid DNx HQX` | Prominent display when the quality level is established |
| `OldDnx` — historical HD/HR brand and level | `DNxHD HQX` or `DNxHR HQX` | Verified historical profile mapping |
| `ReallyOldDnx` — historical HD nominal-rate name | `DNxHD 175x` | Verified DNxHD operating point; no numbered DNxHR name |

The 2012 white paper distinguishes nominal names from its Mbps column: 1080p/23.976 and 1080p/24 4:2:2 10-bit media are named `175x` while listed at 176 Mbps; 1080p/25 is `185x` while listed at 184 Mbps. Therefore actual bitrate alone does not define the historical name. Its 720p tables also require distinct mappings, and thin-raster footnotes must be respected.

The newer white paper, pages 5–6, describes HD and HR as historical marketing brands and explains that the HR/resolution-independent profile supports both HD and higher resolutions. Above-HD historical branding uses DNxHR level letters, not bitrate numbers. Do not implement the converse shortcut `HD dimensions => DNxHD`: HD-sized media can use the resolution-independent profile. Establish the encoded profile before selecting an alias.

Illustrative derived names, not a complete approved mapping catalogue:

| Established historical identity | `NewDnx` (prominent) | `OldDnx` | `ReallyOldDnx` |
| --- | --- | --- | --- |
| DNxHD HQX, 1080p/23.976 or 24, 4:2:2, 10-bit | Avid DNx HQX | DNxHD HQX | DNxHD 175x |
| DNxHD HQX, 1080p/25, 4:2:2, 10-bit | Avid DNx HQX | DNxHD HQX | DNxHD 185x |
| Historical DNxHR HQX profile | Avid DNx HQX | DNxHR HQX | Not applicable |
| New format combination lacking a verified historical counterpart | Verified current name, if established | No verified equivalent | No verified equivalent |

The Autodesk reference confirms the historical 175x/HQX naming example. Current unified names do not uniquely determine historical profiles. Retain all three schemes independently so future table columns, exports or display preferences can expose them. The original name used by the creating application is known only if recorded; a derived legacy-compatible name is not proof of that application's original wording. Do not use the creating application's version alone to infer the encoded profile or manufacture a recorded string.

For newer customizable-bitrate/profile combinations, do not manufacture a DNxHD number or a DNxHR equivalence from resolution, quality level, file size or nearest bitrate. Establish each mapping from the applicable specification/vendor evidence. Unknown mappings must be retained and presented to the user for semantic/display decisions, consistent with Project Canon's completeness requirement.

### Resolution is a naming input, not profile identification

Use the MediaFile's retained, correctly associated resolution observations as one input and consistency check. A selected `1920 x 1080` table value alone cannot distinguish HD-profile encoding from resolution-independent encoding. It also cannot establish quality level, sample depth, sampling or scan type. Keep stored/sample/display geometry distinctions and field-height conventions intact; do not blindly feed a formatted UI resolution string into the catalogue.

Resolution-dependent name resolution must follow this order:

1. Establish the encoded profile and level from a verified coding-label/compression-ID mapping, retaining source evidence and conflicts.
2. Use the associated geometry, rational frame/edit rate, scan type and sample properties to select a documented historical operating point where needed.
3. Resolve `NewDnx`, `OldDnx` and applicable `ReallyOldDnx` independently. Use an exact verified rule; never a nearest-resolution or nearest-bitrate match.

Example: a 1920 x 1080 DNxHR HQX file resolves to `NewDnx = Avid DNx HQX`, `OldDnx = DNxHR HQX`, and no applicable `ReallyOldDnx`. The fact that a DNxHD file of the same dimensions/rate might be named `175x` does not give this DNxHR file that name. When identification evidence is insufficient, retain the unresolved state; when the identified profile has no numbered historical scheme, retain that inapplicability separately from uncertainty.

## Additional acceptance checks

- Known legacy fixtures resolve all applicable naming schemes while preserving raw identifiers, actually recorded name observations and rational edit rates; the unified name is prominent.
- An HD-sized resolution-independent fixture remains identified by its encoded profile, rather than being renamed DNxHD from its dimensions.
- Equal-resolution HD and resolution-independent fixtures do not receive the same `OldDnx` or a fabricated common `ReallyOldDnx`; no nearest-match fallback is allowed.
- Invalid/unknown identifiers and incompatible descriptor/header combinations do not receive fabricated exact names.
- A rounded actual bitrate cannot replace a verified historical nominal name.
- Alpha-only 16-bit fixtures retain their complete layout/role and display `Yes` in the Alpha column.
- Alpha column outputs are exactly `Yes`, `No` or empty; uncertain/unresolved evidence does not become `No`.
- Disabling the Alpha column feature flag hides the column without stopping evidence collection or changing the stored alpha observations.
- Newly encountered properties, encodings and naming combinations remain retained even when no catalogue entry exists.

## Concrete lookup tables and the Wikipedia cross-check

Checked the user-linked [Avid DNx article](https://en.wikipedia.org/wiki/Avid_DNx_(codec)) and [Avid DNxHD article](https://en.wikipedia.org/wiki/Avid_DNxHD) on 3 October 2026. The latter links to [List of Avid DNxHD resolutions](https://en.wikipedia.org/wiki/List_of_Avid_DNxHD_resolutions), which supplies nominal-name tables. These are navigation/cross-check references; implementation definitions come from the primary Avid/SMPTE sources listed above.

There are tables we can use. A codec identification table and a historical operating-point/name table answer different questions and can form one joined catalogue. The current Avid capability/data-rate table does not itself identify which encoded profile a particular file uses.

### Historical compressed-profile catalogue seed

The following is a bounded seed derived from ST 2019-1:2016 Annexes C/H plus Avid's naming policy. CID means the compression ID in compressed essence, not any numeric database field that happens to have the same value. Validate descriptor-label aliases independently. Newer revisions/extensions and thin-raster entries require additional verified rows; this is not the complete production catalogue.

| Verified historical CID(s) | Historical profile | `NewDnx` | `OldDnx` |
| --- | --- | --- | --- |
| 1235, 1241, 1250 | HD | Avid DNx HQX | DNxHD HQX |
| 1238, 1243, 1251 | HD | Avid DNx HQ | DNxHD HQ |
| 1237, 1242, 1252 | HD | Avid DNx SQ | DNxHD SQ |
| 1253 | HD | Avid DNx LB | DNxHD LB |
| 1256 | HD | Avid DNx 444 | DNxHD 444 |
| 1271 | Resolution independent | Avid DNx HQX | DNxHR HQX |
| 1272 | Resolution independent | Avid DNx HQ | DNxHR HQ |
| 1273 | Resolution independent | Avid DNx SQ | DNxHR SQ |
| 1274 | Resolution independent | Avid DNx LB | DNxHR LB |
| 1270 | Resolution independent | Avid DNx 444 | DNxHR 444 |

Each grouped HD CID has distinct geometry/scan constraints in Annex C. Grouping above shares the naming level, not the complete codec definition. For example 1235 and 1271 can both describe HQX at HD dimensions, but establish different historical profiles. Never use dimensions to collapse them into a single naming row.

### Historical nominal-name lookup seed

For the historical HD HQX rows, Avid's supplied 2012 white paper pages 9–10 supplies these operating-point names:

| Established HD operating point | `ReallyOldDnx` |
| --- | --- |
| 1920 x 1080 progressive, 4:2:2, 10-bit, 24000/1001 or 24 fps | DNxHD 175x |
| 1920 x 1080 progressive, 4:2:2, 10-bit, 25 fps | DNxHD 185x |
| 1920 x 1080 progressive, 4:2:2, 10-bit, 30000/1001 fps | DNxHD 220x |
| 1280 x 720 progressive, 4:2:2, 10-bit, 25 fps | DNxHD 90x |

These are exact documented names, not nearest-bitrate calculations. The rest of pages 9–10 must be transcribed and checked with their scan/raster/depth constraints when building the production catalogue. A resolution-independent profile does not acquire one of these numbered aliases by matching the same dimensions/rate. Its `ReallyOldDnx` is inapplicable.

The joined catalogue should retain source/revision citations per rule and associate the result with the observations used. This supplies the requested table-driven implementation without making a dimensions-only identification assumption.

## Confirmed legacy OMF DNxHD 220 spelling — 7 October 2026

`BLACK_1920x540x2_AVHD_220.omf` is **DNxHD 220**, displayed as
`Avid DNx HQ [DNxHD 220]`. Withholding its numbered name solely because its
OMF descriptor stores `2997/100` was too strict. The confirmed legacy spelling
is accepted for naming; its recorded frame-rate fraction remains unchanged.

The repository fixture and installed Avid supporting file have the same SHA-256:
`e5636021776523f6676f61689bd41572092596e85529379fb6b33d202bdb18fb`.
Its two compressed field headers, at byte offsets 0 and 458752, both record CID
1243. The OMF descriptor independently records resolution ID 1243, 1920 × 540
separate fields, eight-bit components and 2 × 1 chroma subsampling.
[MediaInfo's parser](https://github.com/MediaArea/MediaInfoLib/blob/master/Source/MediaInfo/Video/File_Vc3.cpp)
maps that CID to HD/HQ, 1920 × 1080, interlaced, eight-bit 4:2:2.
[Avid's MediaDirector 1.0.1 release notes, PDF page 5](https://resources.avid.com/SupportFiles/attach/ReleaseNotes_MediaDirector_v1_0_1.pdf)
list DNxHD 220 at 1080i/29.97.

The user independently confirmed the `220` name in MediaInfo and VLC. A local
read through the installed MediaInfoLib 26.05 independently reproduced the
profile, geometry, scan type and component format; its JSON output did not
include the numbered name. These are recorded separately in the
[reproducible file evidence](evidence/dnxhd220-legacy-clock-2026-10-07.json).

The initial correction accepted only this exact legacy OMF profile/clock combination,
subject to the existing raster, layout, depth and sampling checks. It neither
rounds arbitrary rates nor reads `220` from the filename. Nearby clocks and
incompatible descriptors remain unnamed. Internally the generated alias keeps
`EvidenceBasis::Derived`, meaning a verified name obtained from recorded facts;
it does **not** mean an uncertain guess. The original descriptor observations
remain `Recorded`.

## Corroborated MDB decimal clocks — 7 October 2026

The database-first comparison exposed fourteen additional numeric DNx names that
were withheld even though their other descriptor facts were sufficient. All are
in `/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/`. Seven MDB descriptors
record `2997/100`; seven record `23976/1000`. Their file-mob `OMFI:CPNT:EditRate`
properties repeat those exact decimal rates. The corresponding MXF descriptors
record `30000/1001` or `24000/1001`. Each comparison matches the complete file
MobId after applying the source's established byte convention, and agrees on
profile, raster, layout, depth and chroma subsampling. See the
[fourteen file comparisons](evidence/dnx-mdb-decimal-clocks-2026-10-07.json).

These observations establish finite, source-qualified **name-table clock
spellings**, not a general equality between decimal and NTSC rational rates.
The following resolution-ID/rate pairs may use the corresponding named operating
point in Avid's 2012 DNxHD white paper, pages 9–10:

| Recorded OMF/MDB rate | Corroborated resolution IDs | Naming operating point |
| --- | --- | --- |
| `2997/100` | 1235, 1237, 1238, 1241, 1242, 1243, 1253 | `30000/1001` |
| `23976/1000` | 1235, 1237, 1238, 1250, 1251, 1252, 1253 | `24000/1001` |

The 1243 entry is established by the earlier Avid OMF slate; the other pairs are
established by the linked MDB/MXF comparisons. The original OMF toolkit's
`RationalFromFloat` in
[`omUtils.c`](https://github.com/LWKS-Software/omfkt22/blob/main/kitomfi/omUtils.c)
also explicitly writes `2997/100` for NTSC video. That corroborates the historical
representation; its approximate conversion is **not** copied into MediaMuster.
The checked local toolkit source is under
`/private/tmp/mediamuster-mdb-reference/omfkt22-main/kitomfi/omUtils.c`, lines 581–598.

The coding label must agree with the verified resolution-ID mapping, and all
existing profile/raster/layout/depth/subsampling checks still apply. Other
profile/rate pairs, nearby decimals, thin rasters and missing or contradictory
facts gain no alias. This includes an uncorroborated decimal 720p/29.97 pair even
though the rational-rate naming table contains a 720p/29.97 row.

Only `ReallyOldDnx` and its bracketed presentation are affected. `Frame Rate`,
duration arithmetic, raw property bytes and original observations retain the
recorded fraction. The alias observation names both the recorded fraction and
the naming operating point, with `EvidenceBasis::Derived`. No additional header
reads are needed for these verified database combinations.

Regression coverage checks all fourteen accepted profile/clock pairs, unchanged
frame-rate and duration fractions, and rejects unsupported clocks, IDs, raster,
layout, depth or sampling. No nearest-rate matching is used.

## Agreed catalogue storage: handwritten C++ for v1

Agreed Codec presentation: `NewDnx` first, then a verified `ReallyOldDnx` alias
in square brackets when applicable, for example `Avid DNx HQX [DNxHD 175x]`.
Omit the bracketed alias if unavailable/inapplicable; no nearest-match substitute
or invented numbered DNxHR name. Retain OldDnx separately as already agreed.

User decision: the DNx identification/naming tables must be handwritten and hardcoded
in C++ when the rewrite is implemented. Do not load the authoritative mappings from
CSV/TSV at runtime. CSV/TSV can be documentation or a review/export aid if useful,
but must not become a second independently maintained runtime authority.

Proposed implementation shape, subject to the repository's C++/Qt conventions:

- Typed, read-only entries for verified identifiers/profile/level definitions and
  historical operating points; `constexpr` arrays where the chosen types permit.
- Exact applicable geometry, scan type, rational rate and sample constraints rather
  than matching display strings or looking for a nearest bitrate/resolution.
- Separate `NewDnx`, `OldDnx` and `ReallyOldDnx` scheme results. Use the agreed scheme
  names in code; retain source-recorded strings separately from generated aliases.
- A source/revision reference for each mapping rule, directly or through a shared
  source-reference ID, and comments explaining any evidenced exceptions.
- Static catalogue storage shared across records. MediaFile results retain links to
  the applicable rule/evidence rather than copying the whole catalogue per file.
- Explicit unsupported/inapplicable/conflicting outcomes; preserve unknown source
  identifiers for later extension instead of manufacturing valid mappings.

C++ is preferred because the rules ship with the implementation, structure can be
checked by the compiler, and mapping changes can be reviewed with their verification
checks. Hardcoding is not proof that a mapping is correct: row-specific evidence and
coverage remain required. Do not certify the starter tables as exhaustive or silently
exclude source properties/identifiers missing from them.

During implementation, verify representative real historical fixtures and controlled
cases for every supported rule family, detect ambiguous/duplicate catalogue keys,
and check exact rational-rate boundaries, HD-sized resolution-independent media,
unsupported identifiers and missing historical equivalents. Changing the Alpha
feature flag must not alter stored evidence or the resolved codec names.
