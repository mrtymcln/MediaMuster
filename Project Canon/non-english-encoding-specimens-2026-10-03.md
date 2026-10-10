# Non-English PMR, MDB and AVB specimens

Inspected read-only on 3 October 2026 after the user supplied these three files
from Apple Notes attachments. No source files were edited or copied into fixtures.
The [focused evidence ledger](evidence/non-english-encoding-specimens-2026-10-03.json)
retains source paths, sizes, SHA-256 hashes, selected original bytes and locations.

## Observed values

| Source | Legacy/base observation | Unicode observation |
| --- | --- | --- |
| PMR | Base version 8, one record, filename `TONE_100A01.EA7D504A.611740.mxf`; project `block 1729` | Appended version 16, independently counted one record; same filename and project |
| MDB, object 68111 | `OMFI:MCBR:MC:binName`: `Nön English bin náme™ ?? ?` when its bytes are interpreted as MacRoman | `OMFI:MCBR:MC:binNameUTF8`: `Nön English bin náme™ 你好 漢`, valid UTF-8 |
| AVB, MCBR object index 4 | Base name: `Nön English bin náme™ ?? ?` under MacRoman interpretation | Extension name: `Nön English bin náme™ 你好 漢`, UTF-8 |

This PMR contains ASCII names/project text, so it demonstrates the two independent
sets but cannot distinguish MacRoman from other ASCII-compatible legacy encodings.
The fresh PMR reader reports Complete and retains both records separately.

The MDB provides two separately named properties on **the same object**, not two
whole-database record sets. Its legacy bin-name bytes include `9a` (MacRoman ö),
`87` (á) and `aa` (™); Chinese text is recorded as question marks in that property.
The Unicode property retains the Chinese characters. Legacy payload location:
byte 2065, 27 bytes including its terminating NUL. Unicode payload location:
byte 2092, 39 bytes including its terminating NUL. Each byte span was checked directly
against the original MDB, independently of its decoded display text.

The AVB carries a base bin-reference name plus its UTF-8 extension. The counted
legacy field begins at byte 277; the counted Unicode field begins at byte 308.
The latter includes two leading zero bytes before the UTF-8 text. The local pyavb
reference reader was used with native extensions disabled to expose both fields;
its string reads were checked against exact original byte spans. Interpretation
by this reference is corroboration for this specimen, not a universal Avid encoding
specification. The existing MediaMuster AVB reader follows the corresponding
base-name/UTF-8-extension branches.

The Unicode name contains decomposed accents: `o` + U+0308 and `a` + U+0301.
The legacy interpretation uses precomposed accented characters. They can look the
same while having different Unicode sequences. Preserve the original bytes and
recorded Unicode sequence. Any later normalization for comparison must be separate
from the stored original observation.

## Consequences for the replacement engines

- Do not label every legacy field MacRoman solely because these examples fit it.
  The PMR binary evidence still identifies untagged/current-Mac MBCS decoding.
- Store provenance at the actual format boundary: PMR record set/version/record,
  MDB object/property identifier/name, AVB object/base field/extension field.
- Keep both observations and their raw encodings. The UTF-8 observation can retain
  information already lost from the legacy counterpart. Do not reconstruct missing
  characters from the legacy bytes or silently discard them as duplicates.
- A whole record set's Unicode label must not automatically establish the encoding
  of every property. In PMR version 16, the project field is still legacy text.
- Legacy and Unicode text observations remain separate; preferred display
  selection must not discard either encoding or its source context.

Inspection used direct byte-span checks and the local Python AVB reference,
alongside the recorded PMR source observations.
No scanner/parser implementation changed during this specimen inspection.
