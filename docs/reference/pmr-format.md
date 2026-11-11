# PMR layout and selected-set rules

[`pmr_parser.cpp`](../../src/scanner/pmr_parser.cpp) reads one captured PMR and returns
one qualified selected filename set. PMR is an Avid cache/index, not a physical
media inventory or a generic OMF identifier format.

| Wire fact | Interpretation |
| --- | --- |
| Magic `0x000007a9` | Establishes supported framing and integer byte order. |
| Signed base version below 9 | Versions 1–7 use 8-byte legacy IDs; version 8 uses full 32-byte IDs. Nonpositive words follow the inspected signed branch with a warning, not a claim that those versions shipped. |
| Version 1 | No stored Project or MasterMobId. |
| Counted legacy strings | MacRoman under Bella's explicit compatibility policy, interpreted through the first NUL. |
| Appended version 16 | Independently counted preferred set; filenames are counted UTF-8 after two reserved bytes. The first reserved byte must be zero. Project remains legacy text. |
| `0xffff` string length | Null-string marker. An empty/null filename cannot establish a usable entry. |
| FileMobId / MasterMobId | Complete binary identifiers with the source's established scalar byte convention, not text or filename-derived IDs. |
| Modification word (DTM) | Four bytes consumed with bounds checks and deliberately ignored by Bella. |

The usable v16 set is preferred. If it cannot be used, the legacy set can be
decoded from the same capture. A damaged entry rejects that set; Bella does not
publish a mixture of good entries from a rejected set. Unexplained outer content
and incomplete reference coverage stay qualified separately from whether selected
metadata is usable. Counts and lengths are bounded by captured bytes.

PMR filenames associate only with the exact decoded physical basename in the
same directory, including case and Unicode code points. No trimming, case folding,
NFC normalization or underscore repair is applied. A miss can require header
identification; it does not erase the physical row or automatically prove No Reference.
See [matching](matching.md).

## Native-format evidence

The inspected reference is Media Composer **26.8.0.58987**, arm64
`libameLibrary.dylib`, SHA-256
`70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`.
Addresses identify that build, not every historical/future release.

- `LoadPMR`, `0x444bac–0x444c10`: magic and signed version gate;
  `0x445154–0x4452ec`: independently counted v16 set.
- `ReadPmrRec`, `0x447544–0x44758c`: identity widths;
  `0x4476c8–0x4477f4`: version-1 omissions.
- The Unicode stream method reads counted UTF-8 with two reserved bytes. The
  native legacy-string conversion does not establish one universal writer codepage;
  Bella's fixed MacRoman choice is an application compatibility policy.
- `ReadPmrRec`, `0x447854–0x44786c`, reads DTM as UInt32. The current Mac MXF
  writer obtains low-32-bit filesystem modification seconds, but older genuine
  records exhibit a different epoch/clock convention. Native cache comparison
  admits equality or an **exact** 3,600-second difference. Bella does not copy that
  freshness heuristic or convert DTM into Date Created/Modified.

Physical filesystem dates and operation stamps remain independent facts. Stable
PMR size/date does not prove a common rebuild generation with MDB or the media.
