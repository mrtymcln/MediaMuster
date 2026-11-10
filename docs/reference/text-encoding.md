# Text encodings and binary identities

A source encoding describes the original bytes, not Qt's internal `QString`
representation. Binary MobIDs and numeric fields are not text. Decode the
established source layout; do not normalize characters or guess an identity from
text, filename spelling or a successfully rendered string.

| Source | Current supported interpretation |
| --- | --- |
| PMR legacy filename and Project | Explicit MacRoman compatibility policy. |
| PMR v16 filename | Strict UTF-8 after its two reserved bytes; Project remains legacy text. |
| MDB/OMF legacy strings | MacRoman; separately named supported UTF-8 properties remain explicit UTF-8. |
| MXF standard UTF16String | Big-endian UTF-16 under its mapped type. |
| Avid/AAF Indirect string | Its verified type and explicit byte-order marker can select UTF-16LE or UTF-16BE. |
| AVB legacy/modern counterparts | Independent fields under their established class grammar; do not overwrite one while decoding the other. |

A PMR `Legacy`/`Unicode` set name is not an encoding declaration for every field
inside the set. In particular, the Unicode set's Project bytes are not UTF-8 by
virtue of membership. An explicitly malformed declared UTF-8 value is unreadable,
not an invitation to reinterpret it as MacRoman. A counted/Pascal string specifies
framing, not a universal codepage.

No NFC/case transformation is applied to PMR association. Composed and decomposed
Unicode can be distinct managed-name claims even when they render identically.
UI Search's case/accent rules are a separate user-facing operation.

## Native encoding evidence

Media Composer **26.8.0.58987 arm64** uses
`CurrentMacMBCSEncodingToUTF16` for the PMR-derived Project chooser. Its AvidCore
helper at `0x12c7f4` selects `CFStringGetSystemEncoding()` through
`EncodedToUTF16`, `0x12bb3c`; this is receiving-system decoding, not UTF-8-first
validation or an originating-codepage lookup. The fixed MacRoman rule in Bella
is an explicit application policy, not proof that every historical PMR writer
used MacRoman. A successful interpretation cannot establish an undeclared writer
encoding.

Verified named MDB counterparts include `OMFI:MCBR:MC:binNameUTF8` and
`OMFI:MSML:LastKnownVolumeUTF8`. Arbitrary private names ending in `UTF8` do not
inherit that meaning. Actual legacy/UTF-8 bin-name counterparts can have separate
byte locations and different encodings on the same object.

The shared [MacRoman table](../../src/media/avidtext.h) is static data. Active
parsers own their format-specific decoding and bounds/error policy. See
[PMR](pmr-format.md), [MDB](mdb-format.md), [MXF](mxf-format.md) and
[AVB](avb-format.md) for their independent scope limits.
