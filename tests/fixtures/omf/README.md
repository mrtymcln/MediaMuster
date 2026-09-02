# OMF-era fixtures

Real OMF-era (legacy, pre-MXF) Avid media and the databases that describe
it, captured on 2026-09-02 from Media Composer **26.8.0.58987** on macOS.
Two folders because both database pairs share the filenames
`msmFMID.pmr` / `msmMMOB.mdb`.

| Folder | Contents | Source |
|---|---|---|
| `avid_supporting/` | 80 × `*.omf` slates, `msmFMID.pmr` (version 2, 80 pairs, Unicode set present), `msmMMOB.mdb` | `/Applications/Avid Media Composer/SupportingFiles/Avid_MediaFiles/` (its `RAW/` subfolder omitted) |
| `mc2026_audio/` | `TONE_100A01.6A972974.039700.wav`, `TONE_100A01.6A972997.0C53E0.aif`, `msmFMID.pmr` (version 2, 2 pairs + Unicode set), `msmMMOB.mdb` | `/Users/Shared/AvidMediaComposer/OMFI MediaFiles/` — written fresh by MC 26.8.0.58987 on 2026-09-02 |
| `mc2026_audio/bins/` | `WAVE(OMF).avb`, `AIFF-C(OMF).avb` | `~/Documents/Avid Projects/zTeßt_PAL_25p/` |

**Project and bins.** The two audio files were created in project
`zTeßt_PAL_25p` (the `ß` is deliberate: a MacRoman-hostile character, as
in the MXF corpus) in bins `WAVE(OMF)` and `AIFF-C(OMF)` — one tone each,
48 kHz, 24-bit, mono, one minute at 25 fps. Their PMR carries the project
name in MacRoman in BOTH record sets (the Unicode set only re-spells the
FILE name in UTF-8; the project stays MacRoman, `ß` = 0xA7, as PmrParser's
layout comment records) — so the `ß` reaches the app through AvidText's
MacRoman decode, exactly as it does for MXF-era PMRs.

**What every file here has in common.** Each essence file is an Apple
Bento container (essence first, TOC at the tail) — the same container
`msmMMOB.mdb` uses. MobIDs inside the files and the MDBs are 12-byte
`omfi:UID`s; the version-2 PMR stores 8-byte MOBs; Avid's own PMR Unicode
set and the two bins wrap those 8 bytes in a fixed 16-byte prefix and
8-byte suffix to make the 32-byte form (`src/omfuid.h`). MC 2026
additionally writes a 32-byte UMID on the physical mob of each file.

**No SD2 specimen exists.** Media Composer's binary still names Sound
Designer II (`.sd2`) as an OMF-era audio container, but no current
release writes one, so anything built for it is by name only and marked
unverified in the code.

**mtimes are not preserved by git.** The PMR trailer is the file's
modification time in Unix seconds (`mc2026_audio`: 1788291444 and
1788291480). Tests that need the staleness guard to pass set the mtime
back from the trailer, as `tst_scanner` does for the MXF fixtures.
