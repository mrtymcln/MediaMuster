# Bella timing: fresh pre-build reconfirmation

4 November 2026. The user selected timing helpers next, and required fresh
actual-file, specification and Media Composer/MDVx decompilation checks before
building. Those checks completed before timing implementation began. They
support the already-agreed source interpretation and count/rate rules; no new
parser defect or requirement was established. This is bounded evidence, not a
claim of universal native UI equivalence or a measured scanner improvement.

The user subsequently approved one shared `src/bella/timing.h/.cpp`, separate
pure rate/duration/formatting operations, lean values and
`tests/tst_bellatiming.cpp`. The alternative was three separate components.
Only new Bella source/tests and root/test CMake lists are approved touch points.
Existing readers, app integration, previous scanner and Bin Filter remain unchanged.

## Fresh actual-file evidence

Independent observers read original metadata and compared it with the current
Bella public reports using C++17/Qt 6.5.3 probes. **21,735 comparisons, zero
mismatches**: 21,180 MDB field/record checks and 555 header/identity/selected
numbering-context checks. Both optional-detail modes were exercised.

| Sources | Scope |
| --- | --- |
| Seven genuine Avid MDBs | 2,515 active physical-file records: complete identities, physical Length, original SampleRate words/type, returned own audio EditRate and PCMA frequency. Application catalogue, Shared MXF/OMFI locations and four EDIT managed folders. |
| Nine MXFs | Physical count/rate, relevant identities and selected source/timecode context. Explicit rates include 24/1, 25/1, 24000/1001, 30000/1001 and 48000/1. |
| Fourteen OMF/native paths | Five OMF containers and eleven native audio files, overlapping in the two embedded Avid tones. Native examples include 48 kHz, 96 kHz, 44.1 kHz and 11127 Hz. |
| Seven ancillary PMRs | Used to locate exact managed filenames by complete identities. These are not claimed as independent timing checks. |

This is 60 timing-source reports across 30 paths, plus seven ancillary PMR
reports: 67 reports across 37 input paths. All input size/date/device/inode
stamps and the pre-build executable source/library hashes remained stable.

The actual MDB descriptor census is 1,513 rates of 48000/1, 729 of 25/1, 125 of
24/1, 93 of 2997/100, 49 of 23976/1000 and six of 59940/1000. Every examined
descriptor SampleRate is typed `omfi:ExactEditRate`. Actual alias coverage is
23.976×1 and 29.97×1/×2; it does not establish all native multipliers in files.
No selected MXF stores an explicit 2997/100 pair.

Public MDB numbering is 2,283 NotRequired, 221 RecordedNdf and 11 RecordedDf.
Fresh original relationships independently qualify **all eleven DF records and
two NDF controls**, including source identity, source track/kind, position,
clock, nominal FPS and covering timecode interval. The other 219 NDF file
contexts were not all independently re-walked. Raw flag counts are not used as
a substitute for applicability.

Concrete examples establish why the chosen values remain separate:

| Actual example | Physical facts | Separate editing context |
| --- | --- | --- |
| Installed WAVE and AIFF-C Avid tones | 2,880,002 sample frames at 48000/1: 60 seconds plus two samples | Own GroupLength 1,500 at 25/1. The agreed display rounds to `00:01:00:00` while retaining those samples. |
| JFIF and AVHD OMF slates | Length one at typed 2997/100 | Editing span 300. Physical Duration remains one frame. |
| MPEG50 PAL OMF slate | Length one at 25/1 | Editing span 250. |
| Shared `Untitled Sequence.175B1728V.mxf` and its MDB record | Length 18,340; explicit MXF 30000/1001 versus MDB ExactEditRate 2997/100 | Correctly linked Flags 1/FPS 30, position zero and covering span 18,340. Agreed DF arithmetic gives `00;10;11;28`. |
| Shared `A01.E6966CE6_A3C580A3C589AA.mxf` | 1,468,800 audio sample frames at 48000/1 | Project display clock 30000/1001; the longer sequence span 1,470,269 must not replace the physical count. |
| EDIT `NOV_78D9V02.22180610.586550.mxf` | ContainerDuration one at 24/1 | Sequence span 2,880. |

The example labels above are exact arithmetic illustrations under the agreed
rules, not captured Media Composer GUI cells. No examined physical media count
was zero. Zero-length timeline Fill components are a different fact.

The genuine native `Boing.wav` preserves 7,787 samples at 11127 Hz despite an
odd data chunk whose pad lies outside its declared RIFF boundary. The independent
walker recorded that framing failure; a separate bounded header check confirmed
the readable values. Bella's existing partial-fact/malformed answer agrees.
This was not silently dropped or presented as a new production defect.

Inventory scope: only `/Volumes/EDIT` was mounted among EDIT* volumes. Explicit
EDIT/local Avid/home-media roots plus indexed managed-name searches found 4,190
MXF, 80 OMF, 842 WAV, 997 AIF, one AIFF, ten MDB and fourteen PMR paths. Three
MDB Notes/fixture copies were excluded from genuine evidence. Four protected
EDIT housekeeping directories were inaccessible; other computer/Library roots
were not exhaustively walked, indexes can omit paths and symlink directories
were not followed. These counts describe discovery, **not an all-media parse**.
No media samples were decoded or huge media files fully hashed. No genuine
zero-length physical media, fractional native AIFF frequency, opposite-order
MDB/OMF or RF64 specimen was established by this selection.

## Format specifications

- [Avid OMF Interchange 2.1, printed pp. 26 and 167–168](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf):
  physical sample units/rate and referencing edit units/rate can differ. The
  descriptor owns its physical Length and SampleRate. Printed p. 206 defines
  modern timecode FPS and Boolean Drop. This modern schema does not redefine
  observed legacy ExactEditRate, Long Length or full-word Flags encodings.
- [SMPTE ST 377-1:2019, definitions, B.17, F.1–F.2 and G.2.2](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf):
  rational clocks, context-specific units, descriptor ownership, ContainerDuration,
  RoundedTimecodeBase and DropFrame are distinct. Omitted duration is not known
  zero; negative lengths are distinguished markers. Picture sample units depend
  on layout, so the specification does not justify blanket field/frame doubling.
  It does not authorize aliasing a generic MXF fraction to an Avid decimal.
- [SMPTE ST 12-1:2014, 5.2.1–5.2.2 and 12](https://pub.smpte.org/latest/st12-1/st0012-1-2014.pdf):
  consecutive NDF numbering and DF omission of the first two 30-base labels at
  non-tenth minutes support the numbering maths. Traditional 60-fps transport
  uses frame pairs; Bella's per-frame base-60 Duration label is not that bit
  encoding. The standard's 24-hour time-address wrap does not override Bella's
  approved non-wrapping elapsed-length display.
- [Microsoft WAVEFORMATEX](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-waveformatex):
  sample frequency and block alignment identify sampling speed and bytes per
  atomic block. Supported fixed-width audio uses sample frames across channels;
  channel count does not multiply elapsed duration.
- [Apple AIFF-C Common Chunk](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-83.html)
  and [original Apple AIFF 1.3, PDF pp. 8–12](https://www.mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/AIFF-1.3.pdf):
  frame count is distinct from bytes and channel sample points. Playback rate
  is sample frames per second. The original specification explicitly permits
  zero sample frames without SSND, supporting known zero independently of the
  selected positive-length files.
- [ITU-R BT.709-6, Table 2](https://www.itu.int/dms_pubrec/itu-r/rec/bt/r-rec-bt.709-6-201506-i%21%21pdf-e.pdf)
  corroborates standardized picture rates with the 1.001 divisor. Avid's private
  serialized decimal interpretation requires the separate native evidence below.

Downloaded original PDFs, hashes, extracted text and bounded interpretation
notes are retained in the temporary specification receipt. Display separators,
half-up audio rounding, absent/damaged handling and non-wrapping hours remain
approved Bella policies; the specifications are not proof of universal native UI.

## Fresh Media Composer decompilation

Installed **26.8.0.58987**, arm64 semantic audit of the universal binaries.
Main SHA-256 `a4a1d1b41f2f8b509781c3b8a50286c0cd7c9746cf85e609ec5a7bcbf45a727a`;
engine `659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22`.
Fresh Hopper analysis includes 26 correctly bound pseudocode targets and 27
assemblies, plus 16 independently extracted LLVM engine ranges. Symbols and
addresses were checked; stale initial Python exports were quarantined and excluded.

- Engine `RationalFromExactEditRate`, `0x258148`: exact 23.976×k and 29.97×k
  recognition, k=1…10, yielding 24000×k/1001 and 30000×k/1001. This corroborates
  the approved finite rule, not approximate snapping or generic-rational aliasing.
- `AFileMediaDesc::Get`, `0x17c018`, `GetLength`, `0x17c278`, and
  `FillMetadataSet`, `0x17d958`: own physical Length and interpreted SampleRate
  are published together. Clip Length and GroupLength have separate getters.
- PCM getters `0x3e9428`/`0x3e9444` use the concrete sampling clock. Native
  `GetEditRate`, `0x3e9484`, uses the base MDFL clock, **not file-mob CPNT EditRate**.
  Bella's agreed qualified CPNT audio display clock is a deliberate requirement.
- `ATimecodeClip::ConvertToOMF2`, `0x21e6f0`, masks full-word Flags bit zero.
  AvidCore `getFramesTable`, `0x524a0`, confirms two/four omitted labels at
  nominal bases 30/60 and the ten-minute pattern; `TCDeconstruct`, `0x5253c`,
  applies DF. `TCToStr`, `0x529e4`, has colon/semicolon and high-FPS modes.
- Main `GetDurationText`, `0x101cb87cc`, and
  `ABinModel::RunningSecTimeToDurationString`, `0x100d11fe4`, use editorial,
  project or global context. Their existence does not establish one universal
  physical-file Duration-cell path or override Bella's source rules.

No Media Composer process, private reader API or user project was executed.
Original binaries remained unchanged. Native defaults, high-FPS transport modes
and broader utilities do not expand Bella's agreed scope.

## Fresh MDVx decompilation

Installed **0.2 build 4073**, executable SHA-256
`2c0032276f7f50fe398e061db8a5ed2c4264d017bf9682642197e3c55d5e424d`, unchanged.
Six procedures were freshly decompiled in Hopper with assembly and information
captures, checked against independent LLVM disassembly and original constants.

The ordinary inventory's 18 enumerated columns, complete 29-ivar row model,
table/export dispatch and MXF row projection show no physical Duration
contribution. Bundled MXF timing and diagnostic timecode routines **do exist**;
this is not a claim that the entire executable lacks timing support.

Ordinary OMFI enrichment at `0x100064794` reads a selected master editing pair;
the label dictionary at `0x1000523ec` and export at `0x100050ce4` use finite
decimal keys and a numerator fallback. Fresh local dictionary bytes bind the
binary's numeric property IDs to the corresponding Avid property names. These
paths do not implement Bella's physical count/rate Duration or source-aware exact
conversion. They are not an accuracy oracle for that feature. No MDVx process,
patched adapter or private runtime reader was executed.

## Receipts and implementation boundary

Fresh research artifacts are outside the checkout under
`/private/tmp/bella-timing-reconfirmation-20261104/`:

- `files/REPORT.md`, `RECEIPT.json`, comparison lists, original byte/type/range
  observations, complete public reports and final stamps/hashes.
- `media-composer/FINDINGS.md`, `receipt.json`, authoritative capture QC and
  bound Hopper/LLVM evidence.
- `mdvx/README.md`, `findings.json`, `receipt.json` and checked captures.
- `specs/receipt.json`, original PDFs and `interpretation-notes.json`.

No timing helper existed during the reconfirmation. The component implementation
and its verification are a subsequent step; the corpus comparisons above must
not be presented as tests of code that had not yet been written. Preserve the
approved exact unsigned-64 audio-fraction range when building the shared helper;
native signed32 frequency limits do not narrow MDB/OMF summary compatibility.
The approved component was subsequently built and verified; see the separate
[implementation receipt](IMPLEMENTATION.md).
