# Bella: Bit Depth from MXF

Research date: 22 October 2026. **The user approved the focused MXF source,
layout and missing-value rules on 22 October 2026; they remain unimplemented.**
Audio Sample Rate is agreed. The existing Bit Depth core remains agreed: carry verified
interpretation rules and useful cases into small new helpers, retain numeric
depth with necessary format information, and generate the label at display.
No extra column, header-opening policy or Compression behavior is changed.

## Physical sources and encoding

Follow the already qualified physical file's own descriptor, rather than a
master or another recording. Bit Depth describes an audio sample or an image
component, not compressed bitrate or the sum of bits in a whole pixel.

| Supported descriptor | Own MXF source | Wire representation |
| --- | --- | --- |
| Sound / WaveAudio / AES3Audio | QuantizationBits | Unsigned 32-bit big-endian integer. Zero is unavailable, not a useful zero-bit depth. |
| CDCI and its supported MPEG-video subclass | ComponentDepth | Unsigned 32-bit big-endian coded value; representation/coding can affect interpretation. |
| RGBA | PixelLayout | Fixed 16-byte layout: eight interleaved component-code/depth-byte pairs. |

Keep these property names in source context. Legacy MDB RGBA uses two separate
arrays; it must not be decoded as the MXF interleaved form. Toolkit in-memory
struct alignment likewise does not establish the on-disk pair size. No text
encoding conversion or audio/picture payload decoding is needed.

[SMPTE ST 377-1 F.5, G.2.26 and G.2.40](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf)
defines the scalar/pair meanings and picture floating-point codes. Preserve
representation context rather than displaying every raw code as a bit count.

## What the three readers do

| Reader | Verified behavior | Limit |
| --- | --- | --- |
| Media Composer | Imports the own audio quantization width, picture component width and typed RGBA roles/widths. Publishes depth/layout facts separately from total pixel size and alpha depth. | Bounded native import/publication paths, not a fully traced universal Bit Depth GUI/default policy. |
| MDVx | Its bundled MXF reader reads optional own sound QuantizationBits as a raw UInt32. | No CDCI ComponentDepth or RGBA PixelLayout read in this route; ordinary MXF wrapper retains no Bit Depth. |
| OldScanner | Reads the class-specific scalar/layout, interprets ordinary and special picture values with coding context, and produces depth/representation labels. | Stores labels early; mixed/unsupported RGBA handling and a native-padding compatibility gap need explicit Bella rules. |

### Media Composer native evidence

The previously connected ordinary source-mob/own-descriptor import reaches
AudioMediaDesc's typed GetQuantizationBits and SetBitsPerSample, then its
separate QuantizationBits publication. CDCIDescriptor reads the typed component
width into its own raw state and publishes ComponentDepth. AlphaSampleDepth
has separate state and must not replace the colour-component value.

RGBADescriptor obtains typed PixelLayout elements and separates their component
roles and widths internally. Its BuildLayout publication rebuilds interleaved
bytes and pads the result to 16 bytes. Separate native calculations can sum
component widths into bits per pixel; that is a different quantity from this
single per-component depth column.

Fresh toolkit traces establish an important compatibility form:

- SetPixelLayout pads unused records with **ASCII `0` (0x30), width zero**.
- CountPixelLayoutElements ignores both numeric code zero and ASCII `0`.
- GetPixelLayout copies the requested prefix. For the actual A8/terminator/
  ASCII-padding example below, the count is one and the imported prefix is A8.

Native counting examines all eight code fields, rather than exactly using
Bella's agreed prefix/padding validation. This proof does not authorise
arbitrary malformed tails or make native count/copy quirks Bella requirements.
No app/private API was executed and no GUI result was observed.

### MDVx and current MediaMuster

`ami_read_info` follows the selected file descriptor and sound-subclass guard,
then reads QuantizationBits using an exact four-byte unsigned getter. Missing
and recorded zero both leave the internal value zero; malformed width fails
the broader library read. It performs no special picture-depth interpretation.
The ordinary wrapper still transfers names, Project and IDs only. Its table
formats separately populated `DB_bits` as an unsigned bit count, blank for zero.

OldScanner's own scalar handling already supports ordinary widths, standard
picture 253/254/255 interpretation and full-coding-qualified DNxUncompressed
refinements. It keeps alpha distinct. Its BitDepth observations and final row
are strings, with numeric sorting extracted from the label. Bella's already
agreed numeric/late-label design is preferable for the new helpers.

The current RGBA loop rejects nonzero codes after a null terminator, including
the native ASCII-zero padding below. It can also emit labelled mixed-component
strings. If one structurally accepted component has an unsupported depth,
current code can omit it while still publishing other components' depths.
That is a source-level partial-result risk, not an observed wrong UI count.
Bella's agreed common-width result must consider every meaningful component.

## Special depth values and existing coverage boundary

Ordinary audio uses its supported positive quantization count. Picture codes
253, 254 and 255 have known half/32-bit/64-bit floating representations. They
are not literal 253-/254-/255-bit samples and are not audio enumerations.

The complete coding identifier is essential where a format changes this
meaning. The cached primary [RDD 50:2019](https://pub.smpte.org/pub/rdd50/rdd50-2019.pdf)
defines the DNxUncompressed standard/fixed-point variants: qualified code 254
can mean 32-bit float or 16-bit S2.14 storage, respectively. The existing
verified rules preserve that distinction. Do not apply an override from a
codec name, short suffix, advertised maximum or unrelated descriptor.

The earlier [MDB Bit Depth review](../2026-10-18-bella-mdb/bit-depth.md)
already identifies unresolved 10.6/12.4 generic-label and unusual component
coverage. Those choices are not silently approved here. Keep the compact raw
code/coding or layout needed to interpret them; an unsupported combination
does not become a guessed scalar label. The current Compression system keeps
its separately agreed behavior and receives the facts it needs.

## Actual metadata

The original thirteen qualified files supply:

| Own depth facts | Files |
| --- | ---: |
| Audio QuantizationBits 24 | 4 |
| Audio QuantizationBits 16 | 1 |
| CDCI ComponentDepth 8 | 2 |
| CDCI ComponentDepth 10 | 5 |
| RGBA prefix A8, with native-style trailing padding | 1 |

The RGBA file is `NOV_78D9V02.22180610.586550.mxf`. Its own 16-byte layout is
`41080000300030003000300030003000`: A8, a null pair, then six ASCII-zero/zero
pairs. OldScanner withholds an MXF depth from this layout; this is not a claim
that a live table must be blank, since another source may supply it. The native
count/get trace establishes how that meaningful A8 prefix is imported.

Ten facts were available in cached evidence; one original specimen was checked
through its existing repository prefix, and two known attached-file initial
metadata ranges were re-read. Two existing interlaced repository prefixes were
also read for additional depth controls. Fresh bounded regions matched prior
hashes with immediate stability checks. No essence or full media was read.
No original-thirteen specimen has a special 253/254/255 code or fixed10/12 case;
standard/native/source evidence must not be called actual header coverage.

The additional DV100 1080/50 control records ComponentDepth 10. This is a
recorded descriptor value, not independent proof of its encoded/decoded
precision. [ST 370:2013 section 4.1.1.3](https://pub.smpte.org/pub/st370/st0370-2013.pdf)
does not establish a universal eight-bit ceiling suitable for silently
clamping this metadata. Preserve the value/context; no new codec-wide override
or essence claim is introduced.

## Agreed Bella rule

1. Read the class-specific own scalar or layout above, with known type/width,
   byte order, ownership and bounds. Preserve numeric source names/values.
   Apply the verified small format-aware interpretation helpers; retain coded
   width and necessary full coding/representation information when needed.
2. For RGBA, decode the fixed eight pairs and require an interpretable meaningful
   prefix. Recognise standard zero/zero and native ASCII-`0`/zero as end/padding;
   require the remaining pairs to be these known padding forms. Do not read
   beyond 16 bytes, treat ASCII padding as a component, or accept an arbitrary
   nonpadding tail. This deliberately supports the demonstrated Avid form
   without copying the native all-slot count/copy behavior.
3. Require **every** meaningful non-fill component to have a supported
   interpretation. If all interpreted component widths agree, use that common
   depth, including alpha-only files. Do not sum widths, choose the first colour
   component, replace colour depth with separate alpha depth, or publish a
   partial result after omitting an unsupported component.
4. Mixed widths, palette-only/unsupported component meanings or combinations
   requiring an unverified LSB-extension merge supply no single MXF Bit Depth.
   Retain only the small required layout/context, with unavailable status.
   Missing/damaged/unsupported chosen scalars likewise supply no depth. Keep the
   physical file and independent metadata; no guessed default or master lookup.
5. Store the numeric interpreted depth and necessary representation/availability,
   then format familiar `8-bit`, `10-bit`, `16-bit`, `24-bit` or `32-bit` labels
   at display and sort numerically. Keep the source compact layout/coded value
   only where necessary. No separate visible alpha/sample-format column is added.
6. Use small new C++17/Qt fixed-width/endian helpers and at most eight component
   entries. Share existing verified interpretation/tests rather than copy the
   old evidence/projector framework. Extract from the already required header,
   release temporary structures, and preserve the Compression contract. Source
   priorities and reasons for extra header reads remain for the final table.

## Evidence and limits

The [approval receipt](../../../Project%20Bella/evidence/mxf-bit-depth-plan-2026-10-22.json)
records the user's acceptance of these recommendations. Source priorities,
extra header reads and the outstanding uncommon fixed-point labels remain
separate decisions. This is requirements approval, with no runtime cutover.

The [curated receipt](../../../Project%20Bella/evidence/mxf-bit-depth-2026-10-22.json)
records installed binary/source hashes, fresh native padding proof, actual
scalar/layout bytes, primary-source hashes and the recommendation now approved. Raw disassembly
and probes remain in `/private/tmp`.

No Bella runtime code, production test suite, app/private API or scan benchmark
was run. These are bounded reader/format findings, not universal native UI
parity or a measured speed/memory gain. OldScanner remains active and Bin Filter
is unchanged. All prior Sample Rate/Frame Rate/Duration decisions remain agreed.
