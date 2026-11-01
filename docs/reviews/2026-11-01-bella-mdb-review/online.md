# Bella MDB: online documentation review

This retains the earlier online/source review of pre-JPED `20220f2`. The [current verdict](VERDICT.md#current-scope-and-approved-jped-change) records JPED's separately approved Video mapping and the scope reassessment. Publicly documented capabilities do not require Bella to restore excluded fields, add generic provenance, change cancellation granularity or support deferred MDB versions.

The failed PDF/manual fetches below describe this investigation's URLs and attempts. A separate later [47-query research run](../../../artifacts/mdb-independent-audit-2026-11-01/evidence/online/research-findings.txt) retrieved original OMF 1, OMF 2.1, Bento and MDVx manuals using other archived/current URLs; those later results supersede any inference that the publications remain unavailable. Neither search claims to exhaust the web or supply a complete private MDB schema.

Reviewed 1 November 2026 (Australia/Sydney), against `src/bella/mdbparser.cpp` at repository HEAD `20220f2`. This is a documentation and source comparison, not a new application execution receipt. No production source was changed.

The documentation search found no new confirmed Bella defect. The strongest available public references support Bella's separation of Bento framing, legacy OMF records, typed property values, source/master identity and embedded audio summaries. They do **not** establish a complete public specification for current Avid MDB private properties. Applying generic OMF 2 rules directly to these legacy MDBs would introduce errors.

## Primary references and what they establish

| Reference | Reviewed material | Relevance and limits |
| --- | --- | --- |
| [Avid: Media Database files](https://kb.avid.com/pkb/articles/en_US/Knowledge/en262645) | Full indexed article, last updated 2 December 2022 | MDB supplies Media Tool information and relinking; the MediaManager example explicitly includes media-file and master-clip entries. Operational description, not wire schema. |
| [Avid: Refreshing Media Databases](https://kb.avid.com/pkb/articles/en_US/Troubleshooting/Refreshing-Media-Databases) | Full indexed article, last updated 1 August 2026 | Current database locations and refresh/rebuild workflow. Does not define parser acceptance or damaged-input semantics. No refresh or deletion was performed in this review. |
| [Avid: corrupt database troubleshooting](https://kb.avid.com/pkb/articles/en_US/Knowledge/en284411) | Full indexed article, last updated 20 May 2024 | Confirms corruption can cause native failures and databases also occur in supporting media folders. Does not justify treating native acceptance as a corruption validator. |
| [Avid OMF Interchange Specification 2.1](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf) | Avid-authored 1997 specification, hosted by a mirror; downloaded 1,081,300 bytes, 282 PDF pages. Relevant class model, header, Mob chains, descriptor, RGBA, source clip, audio Summary and version-change sections were inspected. | Authoritative for its declared OMF revision. Printed pp. 37–39, 43–53, 113, 127–133, 157–165, 186–188, 197, 216–217, 233–237 are especially relevant. Not a full modern MDB specification. |
| [Original OMF toolkit source](https://github.com/LWKS-Software/omfkt22/tree/4216021db63f3ef6f094b24f260fbdd67f4cb7e2) | Fresh clone of LWKS distribution, commit `4216021db63f3ef6f094b24f260fbdd67f4cb7e2`; original Avid/Apple implementations and revision-qualified registrations | Essential legacy OMF 1 evidence that the OMF 2.1 tables alone cannot supply. Reference implementation, not proof of every installed Media Composer caller. |
| [Apple Bento specification 1.0d5 listing](https://citeseerx.ist.psu.edu/document?doi=a908c3abaad8b15022594a7cf4fe8dce8458ba01&repid=rep1&type=pdf) | Indexed title, authors and contents | Direct PDF fetch failed (web tool 403; independent request 404). No claim to a new full reading of this PDF. Actual framing checks below use original Bento source. |
| [MDVx author's product page](https://djfio.com/mdv/) | Product description and current downloads navigation | Confirms MDB-based scanning. The current reachable page advertises a PDF manual but its downloads navigation did not expose a direct manual asset. No public MDB schema was found there. |
| [Samba's Media Harmony documentation](https://devel.samba.org/samba/docs/4.1/man-html/vfs_media_harmony.8.html) | MDB file-name virtualization description | Useful inventory caveat: client-specific suffixed MDB files can exist. It does not define MDB payloads. |

Source downloads and inspected file hashes are recorded in [online-sources.json](online-sources.json). A mirror of an original specification is distinguished from commentary by a third party.

## Schema comparison

The following table is the result of checking the relevant source definitions against Bella, not a claim that the generic toolkit defines Avid's entire current MDB format.

| Area | Primary evidence | Bella assessment |
| --- | --- | --- |
| Bento entry framing | [`bento/TOCIO.h:77–85`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/bento/TOCIO.h#L77-L85) defines the original 24-byte entry and its seven field offsets. | Matches `Image::establish()`'s fixed-layout baseline and separate immediate payload treatment. |
| Final label and buffer-size field | [`bento/Containr.h:225–251`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/bento/Containr.h#L225-L251); [`kitomfi/omfansic.c:1519–1625`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omfansic.c#L1519-L1625). | Bento label bytes 10–11 are a TOC block/buffer-size field. They must not be assumed to be the OMF metadata byte-order marker. Original revision-1 handler chooses little-endian container metadata from the version family. Bella's independent metadata `II`/`MM` handling remains distinct. |
| OMF 1 active catalogue | [`omFile.c:748–765`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omFile.c#L748-L765). | Required ObjectSpine versus optional SourceMobs/CompositionMobs/MediaData agrees with Bella's supported foundation. |
| Legacy types, rate and Length | [`omFile.c:440–485`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omFile.c#L440-L485), [`omFile.c:845–858`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omFile.c#L845-L858). | OMF 1 uses Long for 32-bit integers and ExactEditRate for MDFL SampleRate; OMF 2 registers different Length32/Length64 and Rational forms. Bella's recent four-byte Long Length correction is supported. These references do not authorize broad OMF 2 support. |
| Version bytes | [`omAcces.c:2585–2601`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omAcces.c#L2585-L2601). | Generic VersionType is major then minor, never generically byte-swapped. Bella's additional observed legacy `00 01` spelling is an MDB compatibility exception established in repository sample/native evidence, not a claim that OMF version numbers generally reverse. |
| Reference-array length | [`omAcces.c:1922–1943`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omAcces.c#L1922-L1943), [`omAcces.c:2320–2346`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omAcces.c#L2320-L2346) delegate to extent-based array length. | Consistent with deriving complete legacy slots from payload extent rather than trusting a potentially saturated 16-bit prefix. |
| Identity and Mob links | OMF 2.1 printed pp. 45–53 describes SourceID/MobID links and file/media identities. | Supports identity-based relationships. Modern 32-byte UID details still require MDB/native evidence; generic OMF's illustrated 12-byte IDs do not establish that extension. |
| WAVD/AIFD Summary | [`omFile.c:1150–1186`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omFile.c#L1150-L1186); OMF 2.1 printed pp. 113 and 217. | Legacy Summary is VarLenBytes and contains audio-file metadata without all sample data. Bella correctly permits the captured Summary to end before a declared sound payload finishes. |
| RGBA arrays | OMF 2.1 printed p. 188 describes separate component codes and widths, maximum eight entries. | Bella uses parallel arrays. The generic specification says zero-terminated; existing native review `Project Bella/evidence/mdb-bit-depth-2026-10-20.json` records `RGBADescriptor::Get` adding its own terminators and accepting counted raw MDB arrays. Missing raw termination is therefore not a newly established Bella defect. |

## Additional independent MDB documentation found

[pymdb / python-mdb 0.1.0](https://pypi.org/project/python-mdb/) was published on 22 August 2026. Its [documentation](https://pythonmdb.readthedocs.io/en/latest/index.html) and [source](https://github.com/lawson-tanner/pymdb/tree/04380153a98580b7097de30520ed5a05005aaf2d) explicitly target `msmMMOB.mdb`. A fresh source checkout at `04380153a98580b7097de30520ed5a05005aaf2d` was inspected; this review did not install it into the project or use its output as an authoritative verdict.

The author's [object reference](https://pythonmdb.readthedocs.io/en/latest/api/objects.html) says the property tables come from the registries in **two reference samples**; classes declared but never instantiated in those samples remain partly unverified. Its [attribute source](https://pythonmdb.readthedocs.io/en/latest/_modules/mdb/objects/attributes.html) and [bin-link source](https://pythonmdb.readthedocs.io/en/latest/_modules/mdb/objects/misc.html) corroborate the ATTB/ATTR vocabulary, BobData/BobSize, `_ORG_BIN` and binNameUTF8/legacy pairing. They do not prove Bella's conflict policy, private text markers or precompute classification.

Several implementation differences prevent treating pymdb as a universal oracle:

- Its [Bento reader](https://pythonmdb.readthedocs.io/en/latest/_modules/mdb/bento.html) interprets label bytes 10–11 as byte order, unlike the original toolkit's buffer-size definition. It also requires TOC extent to reach the final label exactly and resolves conflicting registry names by last value. These are implementation choices/assumptions, not independent proof that Bella is wrong.
- Its [datatype decoder](https://github.com/lawson-tanner/pymdb/blob/04380153a98580b7097de30520ed5a05005aaf2d/src/mdb/datatypes.py#L153-L180) pads short integers and replaces invalid UTF-8 in strings. Such permissive output cannot establish the correctness of damaged-input recovery.
- Its [audio convenience property](https://pythonmdb.readthedocs.io/en/latest/_modules/mdb/objects/descriptors.html) falls back through PCMA frequency, MDAU frequency and MDFL SampleRate. Bella's approved distinction between concrete audio frequency and descriptor/display clocks must be preserved; a convenience fallback is insufficient evidence to merge those meanings.

## Search coverage and remaining gaps

Searches covered exact MDB filenames and binary-format terminology, Avid knowledge-base/developer material, legacy OMF/Bento specifications and original toolkit source, MDVx author documentation, and independently authored MDB parser documentation. Queries included:

```text
Avid msmMMOB.mdb file format OMF Bento documentation
Avid OMF interchange specification OMFI MDFL Length Long ObjectSpine
Apple Bento container format specification 1.0 24 byte TOC
site:github.com LWKS-Software omfkt22 OMFI ObjectSpine MDFL
"msmMMOB.mdb" documentation parser format -site:reddit.com -site:creativecow.net
"OMFI:binNameUTF8" OR "OMFI:MOBJ:_IMPORTSETTING" OR "OMFI:_PJ"
"Bento Specification" Apple 1.0d5 pdf
"msmMMOB" "format" "github"
"OMFI:MCBR:MC:binNameUTF8"
"OMFI:PCMA:AudioSamplingRate"
"Avid" "MDB" "Bento" specification
site:avid.com "MDB" "database" "format"
"MDVx" "mdb"
"OMFI:ATTB:BobData" MDB
"OMFI:Version" "msmMMOB.mdb"
"BentoSpec1_0d5.pdf"
"OMF Interchange Specification" "1.0" PDF
"Bento" "TOC buffer size" label
site:avid.com "OMF" "specification" developer
"MDVx" "manual.pdf"
"msmMMOB.mdb" "schema"
"Bento Specification" "github"
"OMFI" "_IMPORTSETTING"
```

No complete public current MDB private-schema document was located. `_PJ`/`PROJNAME`/`_USER` source precedence, `_IMPORTSETTING` categorization, modern UID layout, MacRoman legacy text, special depth codes and finite proxy mappings remain grounded in actual/native evidence and the approved product contract. A generic schema listing cannot resolve those policies. No new independent source was located proving every Windows legacy text encoding or every current descriptor/version family.

This is a broad, reproducible online review of the material located by these searches, **not a claim that every page on the internet was discovered or read**. Search indexing, unavailable historical URLs, private SDK access and the failed Bento PDF fetch limit that claim. No MDB media or database content was uploaded to an online service.
