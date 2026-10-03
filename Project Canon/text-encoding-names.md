# Agreed text encoding and PMR set names

User decision, 3 October 2026:

- `PmrFileSet`: `Legacy`, `Unicode`.
- `TextEncoding`: `Ascii`, `MacRoman`, `Utf8`, `Utf16LE`, `Utf16BE`, `Unknown`.

`PmrFileSet` identifies a PMR record set, not the encoding of every property in it.
`TextEncoding` belongs to each text observation and describes its original source
bytes. Keep the basis for establishing/interpreting an encoding alongside it.
An unknown encoding is distinct from absent, unreadable and not-read properties.
The original encoded bytes, value locator and decoded value remain separate facts.
Binary identities and numeric properties are not text; do not assign them an encoding.

Implemented in the replacement model under namespace `Canon` on 3 October 2026.
`RecordSet::pmrFileSet` identifies `Legacy` or `Unicode`; its readable name follows
that terminology. The on-disk version and declared count remain independent fields.
Non-PMR record sets leave this optional PMR classification unset. Earlier evidence
ledgers used the label `Base` for the same legacy set; that historical label does not
change the captured bytes or versions.

Each `RawProperty` has optional `textEncoding` and `textEncodingBasis`. Numeric,
binary and absent properties leave both unset. A text property with an unestablished
encoding uses `TextEncoding::Unknown`, with no claimed encoding basis. Explicitly
UTF-8 filename layouts use `Utf8`/`Recorded` even when the payload is invalid or
incomplete; property readability remains separate. ASCII interpretation of untagged
legacy text uses `Ascii`/`Derived`. Interpretation explanations and original bytes
remain retained.

These fields reside in the shared RAM source graph reached by object references;
there is no need to repeat complete database records for each physical file.
The metadata engine and UI adapter have not yet been connected. No visible encoding
column or change to the live application's selected strings is introduced here.

| Encoding | Meaning | Established application/example |
| --- | --- | --- |
| `Ascii` | Basic English letters, digits and punctuation; one byte each | Plain text in the supplied PMR. ASCII bytes are also compatible with MacRoman and UTF-8; this does not establish a broader legacy codepage |
| `MacRoman` | Older Mac character set, including many European accents and symbols | Interpretation that fits the supplied MDB/AVB legacy bin-name bytes. Do not assign universally to untagged PMR text |
| `Utf8` | Unicode stored using one to four bytes per Unicode scalar | PMR Unicode-set filenames; supplied MDB `binNameUTF8` and AVB bin-reference UTF-8 extension |
| `Utf16LE` | Unicode using 16-bit units, with the low byte first | Supported by the current MXF AAF Indirect string-value reader when its byte-order marker specifies little endian; not the PMR Unicode filename payload |
| `Utf16BE` | The same Unicode representation with the high byte first | Current MXF package-name and TaggedValue-name reading; big-endian Indirect string values |
| `Unknown` | Original text bytes retained; encoding is not established | Untagged, non-ASCII legacy PMR filename/project text in the fresh reader |

When a format explicitly identifies UTF-8, an English-only value remains `Utf8`.
When only the ASCII subset of an untagged legacy field is established, `Ascii`
describes that bounded interpretation, not an assertion about its historical writer.
Always retain whether encoding was specified by the format or inferred from evidence.

## PMR property map

| Text property | `PmrFileSet::Legacy` | `PmrFileSet::Unicode` |
| --- | --- | --- |
| Filename | Untagged legacy text; ASCII subset can be decoded, otherwise `Unknown` pending evidence | Explicit `Utf8`, after the counted field's two reserved bytes |
| Project | Untagged legacy text | **Still untagged legacy text**, not automatically UTF-8 |

The PMR's overall integer byte order does not make its UTF-8 filename UTF-16LE or
UTF-16BE. Filename bytes and integer framing are different format facts. Version-1
PMR records omit project and master fields; absence is not an encoding.

## Fresh MDB reader

Implemented 4 October 2026. MDB legacy/UTF-8 counterparts are separate properties
on the same source object, with independent byte locations and encodings; they do
not create PMR-style file sets. The original bytes, including string terminators
and padding, remain intact. See [MDB implementation and evidence](fresh-mdb-reader-2026-10-04.md)
for the verified named UTF-8 properties and decoding limits. Untagged non-ASCII
MDB strings remain `Unknown`, even where a MacRoman interpretation fits a known
specimen. Invalid explicit UTF-8 retains `Utf8`/`Recorded` with `Unreadable` state.

The fresh OMF/legacy reader shares these object-string rules. Native AIFF-C
`COMM.compressionName` is kept as a separate counted-text observation with its
exact original bytes/range. ASCII bytes use `Ascii`/`Derived`; non-ASCII remains
`Unknown`. A Pascal/count-prefixed string describes framing, not proof of a
MacRoman or Unicode encoding. Other native private metadata chunks are retained
without guessed text decoding. See [legacy evidence](fresh-legacy-reader-2026-10-04.md).

Qt stores decoded `QString` text as UTF-16 code units internally. That does not
change the source's `TextEncoding`: an original UTF-8 observation remains UTF-8
in its provenance even after MediaMuster decodes it into a QString.

Evidence and limits: [fresh PMR reader](fresh-pmr-reader-2026-10-03.md),
[non-English specimens](non-english-encoding-specimens-2026-10-03.md), and current
`src/mxfparser.cpp` package-name/TaggedValue routines. The older PMR/MDB text helper
tries UTF-8 and falls back to MacRoman; that is an existing heuristic, not proof of
a source encoding. The fresh PMR reader does not use that heuristic.

## Verification of the naming implementation

The universal Debug build succeeded. Both fresh discovery and PMR CTest suites
passed. The updated PMR suite passed 28 cases with the opt-in real-drive audit enabled,
including all six local/EDIT PMRs and the five existing fixtures. New assertions
check Legacy/Unicode membership, per-property encoding/basis, ASCII-only explicit
UTF-8, legacy project text inside the Unicode set, invalid UTF-8 retaining its known
encoding, null strings, and binary/absent fields having no text encoding.

Proof: [encoding implementation test output](evidence/text-encoding-tests-2026-10-03.txt).
These focused tests supplement the preceding full 29-suite PMR-stage result; they
are not a claim that fresh MDB/AVB readers have been implemented.
