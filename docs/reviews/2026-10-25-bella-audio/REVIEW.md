# Bella native WAVE and AIFF: preserve existing behaviour

25 October 2026. **The user chose to preserve current WAVE/AIFF rules and behaviour.**
The [preservation decision](../../../Project%20Bella/evidence/audio-preservation-plan-2026-10-25.json)
supersedes the pending behaviour changes below. This is not blanket approval of
the preceding proposal or its extra version checks. The original research is
retained unchanged as evidence.

## Agreed Bella direction

Keep the current native WAVE/AIFF format support, interpretation rules, useful
results/labels and missing/unreadable/unsupported handling. Express those rules
in small fresh C++17/Qt 6.5.3 helpers with compact results and scoped temporary
storage. Preserve useful behaviour while improving how the work is organised;
do not carry the whole OldScanner pipeline into Bella. No extra formats,
metadata features, columns or per-field redesign are required.

**Do not add the proposed FVER admission/version gate.** Keep the existing
handling, including Avid AIFF-C without FVER. The earlier proposal to reject
dependent native interpretation for an unknown/damaged present FVER is withdrawn.
Skip sound samples as the current reader already does; avoid unused metadata
copying only while preserving useful outputs and required availability/error
behaviour. Compare with OldScanner and measure benefits during implementation.

Previously approved OMF rules still govern the separate embedded OMF parser.
Final source priorities, extra-read triggers and scheduling remain for the end;
native behaviour preservation does not adopt OldScanner's overall scheduling
heuristics. The native WAVE/AIFF requirements review is now closed by preservation,
with no further per-field planning required. No runtime/cutover approval.

## Historical research and superseded proposal

The material below records the preceding investigation and proposal. Its
suggested behaviour changes and follow-on field-review programme are superseded
by the preservation decision above; verified findings remain useful references.

The [OMF first-release baseline](../2026-10-23-bella-omf/REVIEW.md), including
damage handling, is agreed. This review covers native audio containers and
metadata navigation. It does not implement Bella, change discovery/feature
gates or Bin Filter, settle every audio field, or decide final cross-source
priorities/header-opening/scheduling.

## Verified reader boundaries

| Reader | Variants and metadata reading established here |
| --- | --- |
| Media Composer | Reviewed WAVE descriptor reading accepts RIFF/WAVE and RF64/WAVE. Its selected entry rejects BW64/RIFX. It dispatches fmt, data, ds64, bext, iXML and omfi, plus native/private identity/format/overview chunks. The selected AIFF reader accepts FORM AIFF/AIFC and handles COMM, SSND, bext and private metadata. Unknown chunks are skipped with alignment. FVER has no special dispatch in that AIFF reader. This is concrete descriptor reading and a qualified embedded-OMF scan connection, not proof of every standalone import/AMA/plugin workflow. |
| MDVx | PMR/MDB can describe audio rows. Extension tagging can mark Audio without opening the native header. Generic `.wav`/`.aif` dispatch uses its whole-file Bento/OMFI reader, not an established native RIFF/FORM chunk walker. Reviewed corresponding/unreferenced physical fallback is MXF-only. Native WAV/RF64/AIFF/AIFC variant/chunk support is unestablished in this inventory path; generic/library capability is not proof. |
| Current MediaMuster | OmfReader dispatches RIFF/WAVE, RF64/WAVE and FORM AIFF/AIFC to its native audio reader. It follows chunk sizes, RF64 ds64 and alignment, skipping sound samples and later reading embedded OMF separately. No native BW64, RIFX or Wave64 gate. It copies all whitelisted metadata bodies into temporary raw records, even opaque XML/BWF/private values which its native projection does not interpret. |

Container recognition is separate from audio-encoding interpretation. Current
MediaMuster recognizes PCM/IEEE-float WAVE, including exact known extensible
subtypes; AIFF-C has explicit known integer/float codes. It does not decompress
sample data. Unrecognized coding can coexist with readable basic metadata;
it must not become assumed PCM or a guessed precision/duration.

The native eight-method audit explicitly separates two incidental traces:
InitWaveStream is a writer, and DetermineFileType classifies an already restored
descriptor. Neither is standalone file-admission proof. No complete supported
codec catalogue, live GUI import matrix or broader BW64 plugin absence is claimed.

## Proposed first-release variants

| Form | Bella recommendation |
| --- | --- |
| Ordinary RIFF/WAVE | Support the current bounded layout and known format interpretation. |
| Broadcast WAVE (BWF) | Accept as WAVE with additional metadata chunks, not a separate parser. BWF v0/v1/v2 extensions must not alter interpretation of independent base format facts; any selected extension field needs its own qualified version/layout rule. |
| RF64/WAVE | Keep current large-file support with required first, usable ds64 and validated 64-bit size resolution. No sample loading. |
| FORM/AIFF | Support ordinary AIFF metadata. |
| FORM/AIFC (AIFF-C) | Support the evidenced Avid layout and known coding meanings; the “C” form can contain uncompressed integer or float audio. Preserve the missing-FVER compatibility below. |
| BW64, RIFX, Sony Wave64, SDII/resource-fork and other containers | Defer new parser support. Keep physical inventory/independent eligible facts under the established discovery scope. Do not guess a supported signature or change filename admission. |

BWF adds a broadcast extension to WAVE; RF64 supplies larger size fields.
These are container capabilities, not a promise to interpret every XML, ADM,
marker or loudness value. [EBU BWF specification](https://tech.ebu.ch/docs/tech/tech3285.pdf),
[EBU RF64 specification](https://tech.ebu.ch/files/live/sites/tech/files/shared/tech/tech3306v1_1.pdf).

Keep the existing known PCM/IEEE-float and AIFF-C coding mappings as the starting
interpretation boundary, with complete required format/subtype context. Ordinary
AIFF's absent compressionType legitimately means its ordinary PCM layout;
AIFF-C's required missing/damaged coding does not. Existing Compression naming
stays authoritative. Exact native field/precision/count policies follow in the
focused audio reviews rather than silently approving every current coercion.

Recognize actual signatures/form types, not filename suffix alone. Current
managed discovery admits `.wav`/`.aif` and `.omf`; this review does not add
arbitrary-folder audio importing, `.aiff` aliases or new discovery suffixes.

## Read only what the retained results require

| Section | Purpose and proposed reading |
| --- | --- |
| Outer RIFF/RF64/FORM header and each chunk header | Establish form, boundaries, byte order and padding. Walk the declared chunk chain; validate ranges/count arithmetic. Metadata can occur after the sound data, so do not stop at data/SSND or assume a fixed front-header size. |
| WAVE fmt | Read required base fields and the bounded supported extension for format, sample rate, precision/storage distinction, channels and block alignment. A known extensible subtype needs its full GUID, not a short prefix guess. Skip unused extension bytes. |
| RF64 ds64 | Read fixed size/count fields and validated size-table entries needed for navigation. Respect ordered repeated chunk-size entries. Do not allocate blindly from the declared chunk/table size. |
| AIFF/AIFC COMM | Read the supported numerical/coding prefix. Reuse the same bounded format helpers as OMF/MDB descriptor summaries. A descriptive Pascal compression name is not coding authority and need not be retained for existing labels. |
| AIFF-C FVER, when present | Read the small version value for supported-version handling. It is a format revision marker, not file Date Created. Missing FVER uses the evidenced Avid compatibility rule below. |
| WAVE data | Record its safe extent/count context; seek past sample bytes. Do not load them. |
| AIFF SSND | Read its eight-byte offset/blockSize prefix, validate against the declared extent and skip alignment/sample bytes. Do not interpret all remaining SSND bytes as unpadded sample frames. |
| Embedded omfi / established physical Bento trailer | Invoke the agreed OMF reader only in established chunk/trailer contexts. Keep original physical offset basis and independent contribution status; one physical file remains one row. No raw search for plausible Bento magic. |
| bext, iXML, umid, minf and other optional metadata | Locate safely. Read only a specific value if a retained Bella field is established to require it in the upcoming field review. Do not copy all bodies merely because their chunk IDs are recognized. No new XML/ADM/tag framework. |
| fact, axml, chna, CHAN, markers, comments, LIST/ID3, overview and unknown/private chunks | Skip safely bounded unneeded payloads. A later supported field/count rule may select a specific value explicitly; do not imply full semantic support or discard needed information before that review. |

WAVEFMT extensions distinguish sample storage from valid precision; the
subtype supplies encoding context. [Microsoft WAVEFORMATEXTENSIBLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-waveformatextensible).
RIFF defines chunk sizes and even-byte alignment, rather than a fixed header
length. [Microsoft RIFF](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-).

## Actual Avid files and the FVER compatibility point

A new bounded independent probe checked the two known current-Mac Avid tones.
It read only outer/chunk headers, small format prefixes and fixed trailer
signatures: 124 bytes for WAVE and 114 for AIFF-C. This is a layout observation,
not Bella's total metadata-read cost or a performance benchmark.

| Specimen | Form | Native chunk order |
| --- | --- | --- |
| TONE_100A01.6A972974.039700.wav | RIFF/WAVE | bext, fill, fmt, data, fill, umid, minf, iXML, omfi |
| TONE_100A01.6A972997.0C53E0.aif | FORM/AIFC | bext, COMM, SSND, umid, minf, omfi |

Both declared containers end at the physical end; each has an omfi chunk with
a Bento label at the physical tail. Both place identity/private/OMF metadata
after sound data. The AIFC COMM declares `in24`, with a nonzero SSND offset and
block alignment. No samples, XML/private bodies or Bento object graph were read
by this probe, and no full-file hash was taken by reading sound samples.

The Avid AIFC has **no FVER**. Apple's published AIFF-C description requires
one and defines the known marker as A2805140; it also distinguishes COMM from
SSND. [Apple AIFF/AIFF-C sound files](https://developer.apple.com/library/archive/documentation/mac/Sound/Sound-61.html).
Native/current selected readers do not require FVER in the reviewed path.

For Bella, propose accepting this evidenced missing-FVER Avid-compatible layout
without inventing a recorded version. If FVER is present, require its supported
bounded, consistent interpretation; an explicit unsupported revision must not
be treated as the known version. Malformed/conflicting required version facts
withhold dependent native interpretation, preserving independently eligible
OMF/database/filesystem facts. This is a proposed Bella boundary, not a claim
that native Media Composer enforces that gate. Do not mistake native missing
FVER compatibility for support of arbitrary future AIFF-C revisions.

## Small integration, remaining work and evidence

Use one small audio-reading component with a shared checked chunk walker,
explicit WAVE/RF64 and AIFF/AIFC cases, and shared ordinary format helpers.
C++17/Qt 6.5.3 fixed-width endian reads, scoped QIODevice ownership and compact
owned results suffice; no parser/class per chunk, new dependency, decompressor
or permanent full chunk/raw-evidence catalogue is proposed. Keep only selected
facts and temporary necessary sizes/OMF locators, then release the job storage.

The concrete planned simplification is reading fewer unused metadata bytes and
constructing fewer raw records. Current OldScanner already skips audio samples;
do not claim it loads whole recordings. Time/peak-memory benefits remain
unmeasured. Embedded OMF and native audio are already separate contributions in
one physical row. Their final priorities and reasons to open a header remain
at the end of the requirements plan.

After scope/navigation, review only real native-audio field gaps: useful identity/
editorial extensions, own sample counts/rates/precision and audio display-clock/
numbering availability, then local failure/duplicate handling under the agreed
general principles. No inference that a BWF UMID automatically is FileMobId,
time reference is Duration, or recorded origin date is filesystem Date Created.

The [curated evidence](../../../Project%20Bella/evidence/audio-layouts-2026-10-25.json)
retains three bounded reader receipts, hashes/anchors and the two native layout
observations. Other variants are source/native evidence and authored test cases,
not observed physical RF64/BW64/plain-AIFF/float examples. No application/private
API, production parser, tests/builds, media write or Bella runtime/cutover was
performed. Safely recognized unknown chunks do not establish their content or
100% metadata/playback correctness.
