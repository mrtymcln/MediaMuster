# Real Avid media findings: 3 October 2026

These are observations from the mounted local and EDIT storage during this discussion.
The reads were non-destructive. Counts are a dated snapshot, not permanent properties
of the folders. The production parsers were used for the targeted rereads; independent
raw inspection and reference comparisons from the wider audit provide supplementary
evidence, not certification of every decoded property.

## Inspected locations

Primary real-media root: `/Volumes/EDIT/Avid MediaFiles/MXF/`.

| Folder | Physical MXFs | Parsed PMR entries | Parsed MDB file objects | Parsed MDB master objects |
| --- | ---: | ---: | ---: | ---: |
| `1` | 389 | 389 | 392 | 154 |
| `86452` | 687 | 686 | 686 | 415 |
| `8646` | 513 | 513 | 525 | 353 |
| `8647` | 463 | 463 | 471 | 293 |
| Total across these folders | 2,052 | 2,051 | 2,074 | 1,215 |

Totals are per-folder counts, not counts of globally unique identities. All four
PMR/MDB pairs parsed successfully. The 2,052 physical MXF headers were inspected.

- All PMR filenames were found in their corresponding folders.
- PMR file/master IDs matched the corresponding header identities after the scanner's
  MXF-to-database byte-order conversion.
- Folder `86452` contains `V01.6A0416EF_15EDA15EDA552V copy.mxf`, which has no PMR
  filename entry. Its parsed metadata and identities match
  `V01.6A0416EF_15EDA15EDA552V.mxf`. These remain two physical records/rows.
- MDB file identities unmatched by current headers within their respective folders:
  3 in `1`, 0 in `86452`, 12 in `8646`, and 8 in `8647`: 23 total. These are
  reconciliation candidates, not proof of 23 missing media files. Some inspected
  unmatched objects have one-unit descriptors and no unique linked master; their
  roles and expectations require interpretation.
- A direct comparison of the selected decoded technical fields (codec, bit depth,
  dimensions, displayed rate, sample rate and channels) found agreement for matched
  MDB/header records. This does not test every property or establish payload equality.

Local root: `/Users/martymclean/Desktop/Avid MediaFiles/`.

- 1,421 MXFs were enumerated and no PMR/MDB databases were found in that tree.
- Twelve headers were sampled initially, all accepted by the parser.
- The three local copies of the clip below were subsequently read explicitly and
  had matching respective header file/master identities.

`/Volumes/EDIT/2026/Desktop/Avid MediaFiles/` contained no media or PMR/MDB files
in the enumeration, only two extensionless filesystem metadata entries.

## A real clip with video and two audio files

Folder: `/Volumes/EDIT/Avid MediaFiles/MXF/1/`.

Clip name from the headers: `A001_C011_01019F.new.01`.

Project from PMR, selected MDB records and headers: `FIXING_JAMES_KINGSLEY`.

Shared master ID in the database representation:

```text
060a2b3401010101.01010f0013000000.69ffc55bd3a4b305.060e2b347f7f2a80
```

| Physical filename | File ID's distinguishing third group | Decoded technical metadata |
| --- | --- | --- |
| `A001_C011_V01.D7EC5BC5FF69V.mxf` | `69ffc55bd4a4b305` | DNxHD 36 / DNx LB, 1920 x 1080, 24 fps, 8-bit |
| `A001_C011_A01.D7EC5BC5FF69A.mxf` | `69ffc55bd5a4b305` | PCM, 48,000 Hz, 24-bit, one channel |
| `A001_C011_A02.D7EC5BC5FF69A.mxf` | `69ffc55bd6a4b305` | PCM, 48,000 Hz, 24-bit, one channel |

These three full file IDs use the same first, second and fourth groups as the
master above; the table shows only their differing third group for readability.
The app must compare complete normalized IDs, not these snippets or filenames.

Video duration was 4,014 units at 24 frames/second. Each audio file reported
8,028,000 units at 48,000 samples/second. Both represent 167.25 seconds, but this
agreement must not become a rule for establishing clip membership: sibling lengths
can differ in other real clips.

```text
Master clip: A001_C011_01019F.new.01
Master ID fragment: ...69ffc55bd3a4b305...
       |
       +-- video --> File-source ID ...69ffc55bd4a4b305...
       |                |
       |                +--> EDIT/MXF/1/A001_C011_V01.D7EC5BC5FF69V.mxf
       |                |       physical MediaFile -> individual table row
       |                |
       |                `--> local Desktop/MXF/1/same video filename
       |                        physical MediaFile -> another table row
       |
       +-- audio --> File-source ID ...69ffc55bd5a4b305...
       |                +--> EDIT/MXF/1/A001_C011_A01.D7EC5BC5FF69A.mxf
       |                `--> local Desktop/MXF/1/same A01 filename
       |                     TWO physical records and TWO rows
       |
       `-- audio --> File-source ID ...69ffc55bd6a4b305...
                        +--> EDIT/MXF/1/A001_C011_A02.D7EC5BC5FF69A.mxf
                        `--> local Desktop/MXF/1/same A02 filename
                             TWO physical records and TWO rows

One clip association; three file-source identities; six physical files/rows.
Both locations must be included to display all six in a scan.
```

The full local folder for these copies is
`/Users/martymclean/Desktop/Avid MediaFiles/MXF/1/`. There are no local PMR/MDB files
there. The headers themselves supplied the identities and clip metadata.

The user confirmed that the Desktop media was copied from EDIT and visually checked
the files in playback. This is user-supplied provenance. The inspection independently
confirmed matching decoded metadata and identities; no full-file hash or byte-by-byte
comparison was performed in this follow-up. Metadata agreement alone is not a byte
equality test.

## Evidence for the real A01 record

```text
EDIT/MXF/1/msmFMID.pmr
   `--> PMR entry:
           filename = A001_C011_A01.D7EC5BC5FF69A.mxf
           file ID  = ...69ffc55bd5a4b305...
           master   = ...69ffc55bd3a4b305...
           project  = FIXING_JAMES_KINGSLEY

EDIT/MXF/1/msmMMOB.mdb
   `--> matching file-object descriptor: bit depth = 24
                   |
                   +--> ResolvedField: bit depth = 24-bit
                   |        retained evidence: MDB + MXF agree
A01 MXF header     |        physical file still has its own MediaFile row
   `--> selected audio descriptor: bit depth = 24
```

The PMR establishes filename/identity/project observations here; it does not supply
the 24-bit observation. The existing readers expose decoded values. Retaining exact
property locators, raw values, source receipts and selection explanations is proposed
work, not a claim that today's `MediaFile` already contains them all.

## Cross-folder placement

The diagram below is a hypothetical relocation of the verified three-file group,
not a claim that these particular files currently occupy those locations:

```text
Same master clip identity
    +--> V01 file identity --> EDIT/MXF/1/video.mxf
    +--> A01 file identity --> EDIT/MXF/2/audio1.mxf
    `--> A02 file identity --> another scanned volume/MXF/7/audio2.mxf

Location changes do not replace the recorded Avid identities/references.
Unknown or ambiguous associations must remain unknown or ambiguous.
```

## Bin observation

The real bin
`/Users/martymclean/Documents/MC First Avid Projects/mrtymcln/test/new imports.avb`
parsed as valid and complete in the targeted read. It contained 40 mob objects and
16 referenced file identities. Its objects included physical-source and master
objects associated with imported media. This demonstrates that a bin is an object
and reference collection, not one physical-file row per object. This bin was not
used to establish the three-file clip's membership above.

## Cost observation

Across the 2,052 EDIT headers, parser-reported bytes read totalled 83,926,332 bytes,
about 84 MB, versus 297,426,709,774 bytes of physical media, about 297 GB. Per-file
header reads ranged from 32,722 to 60,367 bytes in these folders. The targeted run
took about 5.19 seconds including 12 local header samples.

This is an observation from an already-used machine/cache, not a controlled cold
drive benchmark, RAM measurement, or guarantee for other formats. Parser counters
do not measure every filesystem/device read. It does show that metadata inspection
need not read the entire picture/audio payload.

## Avid references and interpretation limits

- [Avid proxy-workflow white paper, page 6](https://connect.avid.com/rs/avid/images/Proxy%20WP.pdf):
  distinguishes master, file-source and physical-source objects and describes
  identity-based association of different media representations.
- [Avid MediaCentral glossary](https://resources.avid.com/SupportFiles/attach/Interplay_Central/IPC_Help/InterplayCentral_Help/IPC_Glossary.html):
  defines master clips as holding pointers to the media containing video/audio.
- [Media Composer 2025 Editing Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf):
  documents separate video/audio destination drives during import.
- [Media Composer 2020.6 Editing Guide](https://resources.avid.com/SupportFiles/attach/Media_Composer_Editing_Guide_2020.6.pdf):
  distinguishes ordinary local/unmanaged media-database workflows from
  Interplay-managed storage.

These sources support the object/association model. They do not establish that every
Interplay metadata field can be recovered from a media header, that every MDB object
requires a local file, or that MediaMuster's current decoding is universally correct.
