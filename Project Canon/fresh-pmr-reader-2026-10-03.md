# Fresh PMR reader — implementation and proof

> Historical implementation report. On 4 October 2026 the user selected the
> independently written alternative. It now occupies `src/canon/pmrreader.h/.cpp`
> as `Canon::PmrReader`; the first implementation described here was removed.
> The format evidence below remains relevant. See [selection and current checks](pmr-reader-selection-2026-10-04.md).

Implemented 3 October 2026 in `src/canon/pmrreader.h/.cpp`. This is a replacement
reader returning `Canon::ParsedSource`, without calling the old PMR parser or
passing facts through `PmrEntry` or `MediaMetadata`. It is built in the independent
`mediamuster_canon` library. The app has not switched to this reader yet.

## What this stage stores

Each encountered record becomes a source-local object, with its raw file identity,
qualified identity encoding, recorded properties and exact byte ranges.
The subsequent [typed encoding implementation](text-encoding-names.md) adds
`PmrFileSet::Legacy/Unicode` membership and per-property `TextEncoding` plus basis. The parsed
source retains header words, independently counted record sets, all record handles,
master references, diagnostics and an explicit read outcome. A database record is
**not** a physical MediaFile row or a KelpieId. Ownership/matching will be established
by the replacement metadata engine.

| Recorded fact | New representation |
| --- | --- |
| File identity | Complete encoded 8/32 bytes when available; PMR identity encoding includes byte order. No prefix filtering, nil-ID rejection or canonical-identity guess |
| Filename | Full counted encoding, including length, Unicode reserved bytes and bytes after an embedded terminator; decoded text only when supported |
| Project | Counted MBCS encoding; non-ASCII bytes stay recorded and undecoded unless a later policy can establish the codepage |
| Master association | Recorded reference with source-local origin, byte range and identity encoding. Target handle stays zero until ownership is established; no fabricated master object |
| Modification word | Original four bytes and decoded uint32; epoch/timezone and freshness matching are later decisions |
| Version-1 project/master | Explicitly absent from this record layout, with the reason retained. No fallback identity invented by the parser |
| Unknown trailing structure | Source range and diagnostic; bytes are not copied wholesale into RAM or guessed into another record layout |
| Incomplete record | Encountered object plus partial bytes/unreadable field; already established observations remain available |

`RawProperty::state == Present` describes recorded presence. It does not guarantee
that a semantic `decoded` value exists: untagged non-ASCII MBCS is an explicit case.
`interpretation` explains the limitation; a later selection engine must require a
usable, justified value. This is distinct from invalid explicitly tagged UTF-8,
which is unreadable as text and makes the source outcome malformed.

`ParsedSource::Outcome` distinguishes complete, incomplete, malformed, unsupported,
I/O failure and cancellation. `RecordSet::framingComplete` independently records
whether all declared record boundaries were consumed. Bad text with known boundaries
can therefore coexist with completely framed records. The supplied source receipt
is immutable; the reader returns its own shared receipt with its read state.
Completion does not establish that the source remained unchanged during reading.

## Primary evidence behind the framing

The inspected binary is installed Media Composer **26.8.0.58987**, arm64 slice of
`libameLibrary.dylib`, SHA-256
`70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`.
These are static instruction observations, not execution of private Avid APIs and
not a claim about all historical/future versions.

- [`LoadPMR` evidence](../docs/reviews/2026-10-03-format-audit/evidence/libame-excerpts.txt):
  `0x444bac–0x444c10`, magic `0x7a9`, signed base-version check below 9;
  `0x445154–0x4452ec`, separately counted version-16 Unicode set.
- [`ReadPmrRec` evidence](../docs/reviews/2026-10-03-format-audit/evidence/libame-excerpts.txt):
  `0x447544–0x44758c`, versions through 7 use OMF IDs, subsequent accepted versions
  use AAF IDs; `0x4476c8–0x4477f4`, version 1 omits stored project/master fields.
  Filename MBCS capacity is `0x800`, Unicode input capacity `0x400`; the project
  MBCS capacity is `0x40`. These are compatibility checks, not application RAM caps.
- [`AStream` Unicode evidence](../docs/reviews/2026-10-03-format-audit/evidence/avidcore-utf8-excerpt.txt):
  counted UTF-8 after two reserved bytes; first reserved byte checked for zero,
  second retained without a new constraint. `0xffff` is a null-string marker;
  signed payload length and input capacity are checked.
- [`ReadPmrRec` MBCS conversion](../docs/reviews/2026-10-03-format-audit/evidence/libame-excerpts.txt):
  `0x4475f0` calls the current Mac MBCS conversion. The file does not thereby declare
  a universal UTF-8 or MacRoman codepage. The replacement reader decodes only the
  ASCII subset of these untagged fields, retaining everything else for an explicit
  later interpretation policy.

The local Python reference reader and old MediaMuster output are comparison material,
not format specifications. In particular, `version * 4` is not used for identity
width. Zero/negative version words follow the observed signed branch with a diagnostic;
there are no genuine historical specimens establishing that such versions shipped.

Base and Unicode records are **both retained** even when their counts, names or IDs
differ. The parser does not align them by position or select one set as displayed
truth. Avid's observed preferred-vector behaviour belongs in a later explicit
selection rule, qualified by read outcome and usability.

## Verification

The new suite `tst_canonpmr` checks:

- Versions 1, 2, 7, 8 and observed zero/negative branches in both byte orders;
  big-endian Unicode framing and UTF-8 decoding.
- Independent set counts, an empty Unicode set, unrelated identities across sets
  and repeated same-identity entries without filtering or merging.
- Null string versus absent property; raw non-ASCII MBCS, embedded terminator tails,
  nil and non-SMPTE-prefixed identities without guessing.
- Invalid/incomplete UTF-8, reserved-byte rules and bounded over-capacity text
  retention with explicit malformed outcomes.
- Every byte truncation of a two-set specimen; maximum uint32 record count without
  allocating from that count; unsupported structures retained as ranges.
- Short reads, injected I/O failure, cancellation before/during parsing and
  immutable supplied receipts; rejection of text-mode and sequential inputs.
- Five checked-in genuine PMRs: 878 records per set, **1,756 source records retained**.
  Every property range reproduces its exact fixture encoding.

The opt-in read-only local/EDIT check used the same three managed roots as the fresh
real-discovery check. All **six PMRs** parsed completely, preserving **4,824 source
records**, with both sets independently counted. File size/modification time were
checked before/after as test safeguards. These are source records, not a physical
media count. Source-level completeness is not yet proof of reconciliation correctness.

All **29 registered CTest suites passed** after this stage.

Proof: [PMR test/real-drive output](evidence/fresh-pmr-tests-2026-10-03.txt) and
[full CTest result](evidence/fresh-pmr-all-tests-2026-10-03.txt).
The universal Debug build includes arm64 and x86_64; these test runs executed on the
host architecture. The whole app still uses its comparison engines, so no full-scan
performance or memory improvement is claimed at this stage.

## Remaining work

Implement fresh MDB/Bento and media-header readers, qualified identity normalization,
ownership reconciliation, per-field selection and the consumer adapter. In particular,
these raw observations do not yet populate a physical file's selected metadata or
stamp IDs. No new persistent database or UI feature was introduced by this stage.
