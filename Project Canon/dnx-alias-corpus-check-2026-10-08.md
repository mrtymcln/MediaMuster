# Nine inherited DNx aliases: actual corpus check

Checked 8 October 2026. **No matching files were found in the checked corpus.** None of the nine identifiers appeared in a parsed compression property, another retained metadata value, or the independent byte searches described below. That investigation did not change production mappings.

The user subsequently **approved removing these nine unsupported mapping rows**
from the supported compression lookup. Removal verification passes 40/40 native suites and
both universal Debug app builds. The repeat scan keeps all 2,413 rows and every
exported metadata value unchanged, including all 806 DNx names. See the
[removal verification receipt](evidence/dnx-alias-removal-verification-2026-10-08.json).
The original corpus and byte-search receipts remain unchanged.

These are nine extra identifier-to-name rules in the old code. They are not nine missing codecs or nine affected files. Their assigned names are familiar DNx profiles; the unsupported part is the claim that these exact identifiers mean those profiles.

| Exact identifier | Assigned name before the approved removal | Exact corpus matches |
| --- | --- | --- |
| `060e2b34040101010d01030102060301` | DNxHD LB | 0 |
| `060e2b34040101010d01030102060101` | DNxHD SQ | 0 |
| `060e2b34040101010d01030102060201` | DNxHD HQ | 0 |
| `060e2b34040101010d01030102060202` | DNxHD HQX | 0 |
| `060e2b34040101010d01030102110101` | DNxHR LB | 0 |
| `060e2b34040101010d01030102110201` | DNxHR SQ | 0 |
| `060e2b34040101010d01030102110301` | DNxHR HQ | 0 |
| `060e2b34040101010d01030102110401` | DNxHR HQX | 0 |
| `060e2b34040101010d01030102110501` | DNxHR 444 | 0 |

## What was checked

| Sources | Count | Method |
| --- | ---: | --- |
| Media from the Macintosh HD/EDIT scan | 2,413 | Opened every media header directly, including headers normally skipped because a database match exists. |
| Their PMR/MDB databases | 12 | Direct reader inspection, plus full-file independent byte search. |
| Saved repository corpus files | 919 | Direct reader inspection, plus independent search of all 488,393,888 stored bytes. Includes genuine archived MXF headers, OMF slates, databases, audio and bins. |
| Supplied bins | 9 | Direct reader inspection, plus independent full-file byte search. |

All 2,425 live scan sources and all nine supplied bins returned a Complete reader outcome. The 823 incomplete saved MXF sources are header extracts, ending before their original media files do. Their entire archived bytes were also searched independently, so omitted parser content within those stored slices cannot hide one of these exact byte patterns.

The independent search covered the three relevant stored forms: UL octets, structured AUID big endian and structured AUID little endian. There were zero matches and zero read errors. The live non-MXF/bin search read another 96,327,581 bytes. The two searches recorded file hashes and checked that sizes and modification times stayed stable.

## Real DNx files still match the verified rules

For example, the live file below records a verified DNxHD HQX coding identifier and was already displayed as **Avid DNx HQX [DNxHD 220x]** in the scan export:

`/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/V01.E6967030_CBBA90CBBA935V.mxf`

Recorded coding identifier: `060e2b340401010a0401020271070000`. This is a separate, verified table entry; it is not one of the nine aliases. The saved corpus also contains verified DNxHR LB, SQ, HQ and HQX coding identifiers. Their presence does not establish the different identifiers above.

The available corpus provides no evidence that these nine extra aliases are needed.
Their approved removal leaves the independently verified DNx rules and historical
operating-point names in place. This is a narrow mapping correction. The original corpus
check establishes zero matches, while the subsequent verification receipt records
the builds, regression tests and exported-value comparison after removal.

## Limits and receipts

Live MXF picture/audio payloads, padding and range-only opaque data were not searched byte-for-byte. The check targets compression properties and retained metadata; arbitrary bytes in a recording would not establish a codec identity. Header archives cannot speak for omitted original footers. This result applies to the explicit manifest, not every possible historical Avid file.

- [Summary, per-alias results and verified examples](evidence/dnx-alias-corpus-check-2026-10-08.json)
- [Exact source-path manifest](evidence/dnx-alias-corpus-manifest-2026-10-08.json)
- [Per-source property results](evidence/dnx-alias-corpus-properties-2026-10-08.jsonl)
- [Independent full-byte fixture search](evidence/dnx-alias-fixture-byte-search-2026-10-08.json)
- [Independent full-byte live non-MXF/bin search](evidence/dnx-alias-live-nonmxf-byte-search-2026-10-08.json)
- [Read-only diagnostic probe source](evidence/dnx-alias-corpus-probe-2026-10-08.cpp)
- [Earlier bounded Avid-definition review](evidence/dnx-unproven-aliases-2026-10-08.json)
- [Subsequent approved removal verification](evidence/dnx-alias-removal-verification-2026-10-08.json)
