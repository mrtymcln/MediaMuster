# Bella naming connections: concrete function/result proposal

4 November 2026. **Bella-owned small naming answers approved, implemented and verified.** Naming connections and
separate compilation of the unchanged lookup implementations are approved.
The user approved the concrete adapter API below, choosing Bella-owned results
over exposing the existing helper's five-member Compression result.

## Approved component and answers

Add `src/bella/naming.h/.cpp` and `tests/tst_bellanaming.cpp`, registered in the
existing root/test CMake lists. Use C++17 and exact Qt 6.5.3. One component holds
the two related naming jobs; no class or source file per column is proposed.

Bella-owned ordinary values:

```cpp
struct CompressionNames
{
    QString compression;
    QString newDnx;
    QString oldDnx;
    QString reallyOldDnx;
};

struct EffectNames
{
    QString effect;
    QString category;
};
```

These four Compression strings preserve the existing output and DNx naming
variants. Empty unavailable strings stay empty. They are not four competing
metadata sources. Effect and Effect Category come from one existing lookup.
No status enum, reason list, source history, Effect Sequence, instance or
matched flag is added. The existing helper's `legacyIdentifiersUsed` flag is
used by OldScanner evidence explanations; no current Bella consumer needs it.

Approved functions in namespace `Bella`:

```cpp
CompressionNames compressionFor(const MdbFileRecord &);
CompressionNames compressionFor(const MxfFileInformation &);
CompressionNames compressionFor(const OmfFileInformation &);
CompressionNames compressionFor(const OmfNativeAudioInformation &);
EffectNames effectNamesFor(bool confirmedPrecompute, const QString &selectedClipName);
```

The records/source/name are already selected by the caller. Functions do not
read files or choose between PMR/MDB/header candidates. A temporary
`MediaEngine::Detail::CompressionFacts` supplies the existing unchanged resolver;
only its four name strings leave Bella's adapter. Native/summary encoding and
the reader's already-qualified fallback facts preserve their established labels.

`effectNamesFor(false, name)` returns two blanks for ordinary Media or unknown
Type. With confirmed Precompute true, call `AvidEffects::lookup` once and keep
its Effect/Category behavior. A missing/empty Effect becomes lowercase `unknown`
as already agreed; Category retains its current unknown/ambiguity rules. Keep
custom token spelling and whitespace; add no replacement suffix parser. The
caller invokes this derivation only when optional details are requested; no
second enable flag is needed. Reuse a derived answer for unchanged selected
master/name inputs under the existing processing-lifetime rule, not a permanent
cache inside these functions.

**Benefit:** Bella's public types expose only required names and remain
independent of OldScanner's private helper type. **Cost:** two small result
types and mapping the existing strings.

**Alternative:** alias/reuse the existing five-member Compression result,
including `legacyIdentifiersUsed`. Less result mapping, but the public header
depends on the existing private helper type and exposes an unused flag.
Effect/Category still use the two-string answer because Sequence/instance/status
are not Bella outputs. This is a representation choice, not a naming-rule change.

## Existing interpretation boundaries

- Use validated coding presence/absence, exact labels/legacy identifiers and
  required owned descriptor context from each reader. Missing coding is not
  inferred from an unreadable property. Preserve existing source priorities.
- Keep namingGeometry separate from visible/proxy Resolution. Use the exact
  source naming clock/layout/depth/subsampling and qualified alpha inputs.
  Video Frame Rate availability is not a Compression validity flag.
- Preserve the finite ResolutionID/profile-qualified legacy DNx naming rate
  rules and the separately approved MDB generic-J2K ExactEditRate adjustment.
  Do not substitute the broad Duration interpreter or approximate snapping.
  The existing DNx helper checks exact recorded fractions and qualified profile/
  ResolutionID pairs without a rate-encoding gate; the separate J2K exception is
  restricted to the approved MDB ExactEditRate context.
- Transient DNxUncompressed depth/representation strings preserve established
  naming meaning even when generic Bit Depth is unavailable. This does not
  settle the still-open generic fixed10.6/12.4 table-label choices. Preserve
  MXF mixed-component order/compaction from the retained PixelLayout even when
  one common numeric depth/representation is unavailable.
- Use the existing exact alpha/encoding fallback facts. In particular, MXF's
  Pcm/UncompressedAlpha8 proofs do not fabricate recorded coding, legacy NONE or
  a resolution identifier merely to make a resolver branch match.
- Preserve all current names, catalogue entries, DNx aliases and Effect suffix/
  localized/compatibility/ambiguous-category rules. No helper rewrite, new
  catalogue, additional retained reader field or graph walk is proposed.

The current reader contracts contain the required inputs or qualified facts
from which those temporary inputs can be derived. No blocking retained-field
gap was found by the read-only mapping review. Exact mapping and tests must
remain within these agreed rules; any genuine new choice returns to the user.

Approved runtime touch points: new Bella naming files/tests and root/test
CMake registration. The selected separate-compilation boundary is described in
[SHARING.md](SHARING.md). Existing lookup code/headers/tables, reader reports and
warning messages remain unchanged. OldScanner remains active; Bin Filter,
MediaFile/table, progress, journal, Unfinished Business and Undo are outside
this step. No performance improvement is claimed. See [verification](VERIFICATION.md)
for the focused suites, genuine-file comparison and preserved-source checks.
