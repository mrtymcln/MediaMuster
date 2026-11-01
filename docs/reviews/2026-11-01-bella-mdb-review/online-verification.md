# Fresh documentation verification

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

This is the retained pre-JPED verification. The [current verdict](VERDICT.md#current-scope-and-approved-jped-change) records the approved JPED change and superseding scope assessment. The separate later research receipt retrieved original specifications/manuals through other URLs; failures recorded here remain truthful historical attempts, not a claim those documents are unavailable now.

Date: 1 November 2026 (Australia/Sydney). MDB only. This independently rechecked the main claims in [online.md](online.md) with 16 fresh search queries and direct source reads. It adds no production changes and no application runtime claims.

The public evidence does **not** establish one universally correct MDB implementation. No complete public specification of the current private MDB schema was located. Original Apple/Avid sources define the foundation; the independent MDB reader documents its own observed vocabulary. The remaining private-field decisions still require specimen bytes, native behavior and an explicit report contract.

## Findings checked against primary sources

| Material freshly checked | What it establishes | What it does not establish |
| --- | --- | --- |
| [Avid: Media Database files](https://kb.avid.com/pkb/articles/en_US/Knowledge/en262645) and [Avid: Refreshing Media Databases](https://kb.avid.com/pkb/articles/en_US/Troubleshooting/Refreshing-Media-Databases) | MDBs supply Media Tool/relink information and are maintained in media folders. The MediaManager discussion includes entries for media files **and** master clips. | Wire layout, precedence among private Project properties, damaged-list semantics, or the correctness of a parser's count. |
| [Apple's original `Containr.h`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/bento/Containr.h#L225-L251) and [`TOCIO.h`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/bento/TOCIO.h#L61-L85) | The label ends the container; its bytes 10–11 are the buffer-size field. The legacy TOC has 24-byte entries and separate immediate-value handling. | A rule that those footer bytes are a generic `II`/`MM` byte-order marker, or that all Bento features occur in ordinary Avid MDBs. |
| [Original Avid toolkit `omFile.c`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omFile.c#L2500-L2600) | Its opener distinguishes object revision from container revision, including legacy OMF in a newer container (IMA). | That Media Composer's ordinary MDB loading path uses every generic toolkit combination. Bella's explicit Bento-1/legacy-only scope remains a deliberate compatibility boundary. |
| [pymdb object documentation](https://pythonmdb.readthedocs.io/en/latest/api/objects.html) and [Bento reader source](https://pythonmdb.readthedocs.io/en/latest/_modules/mdb/bento.html) | The object/property tables were derived from the registries of two reference samples. Many declared classes did not occur in those samples. Its reader demands footer bytes 10–11 be `II`/`MM` and demands TOC extent reach the label exactly. | Independent proof of the complete private schema, every class's semantics, or the correctness of those container assumptions. The footer assumption conflicts with the original Apple definition above. |
| [MDVx author's page](https://djfio.com/mdv/) | The scanner uses PMR/MDB and the site advertises a PDF manual. | A public MDB wire specification. The historically cited [manual URL](https://djfio.com/mdv/MDVx-theManual.pdf) returned 404 on this verification; the downloads page exposed no usable direct manual link. |

The [indexed Apple Bento specification](https://citeseerx.ist.psu.edu/document?doi=a908c3abaad8b15022594a7cf4fe8dce8458ba01&repid=rep1&type=pdf) remains discoverable, but its PDF returned 403 when opened. Its contents have not been newly read here. New searches also returned Avid's old release notes and effects guides, which are operational descriptions, not a private database schema. No recovery/rebuild instructions were executed.

## A useful qualification about local byte order

Bella's `Image::bigEndian()` (`src/bella/mdbparser.cpp:324`) accepts an object's explicit order only when it equals HEAD's order. An opposite recognized local marker makes that object's numeric facts unavailable. The existing review correctly treats native per-object override handling as a potential compatibility improvement, outside the presently agreed scope.

Do not justify that extension using toolkit comments alone. The [original `omUtils.c`](https://github.com/LWKS-Software/omfkt22/blob/4216021db63f3ef6f094b24f260fbdd67f4cb7e2/kitomfi/omUtils.c#L327-L455) comments describe reading the object's marker with default fallback, but the actual `omfsGetByteOrder()` raises `OM_ERR_NOBYTEORDER` for a present non-HEAD marker. Its foreign-order helper then falls back to file order on that error. Thus this particular implementation does not prove the promised local-override behavior. The freshly captured Media Composer `omfiDualStream::LoadBentoObject` instructions in [binaries.md](binaries.md) are the stronger relevant evidence. A real mixed-order MDB or a controlled native read should settle the extension before changing Bella.

## Recommendations grounded in this verification

1. Keep dictionary-based property identification, typed widths, independent container/metadata byte order and complete identities. Neither a permissive third-party decoder nor generic OMF2 tables justify weakening these checks.
2. Keep mixed-order verification gaps and additional MDB layouts marked as research/deferred compatibility work. Do not equate a smaller approved support range with universal superiority, or treat the existence of native/generic code as authorization to expand Bella's release-one support.
3. Use pymdb's readable object/property catalogue as an additional source of hypotheses and a debugging cross-check. Validate each adopted private field against bytes/native behavior; do not import its container assumptions or convenience fallbacks wholesale.
4. Rank fixes by demonstrated semantic errors before broadening formats. The historical null-slot and class-qualification reproductions in this review are more concrete than speculative support for unseen MDB versions.

Search terms included exact MDB filenames with `format`, `schema`, `parser`, `OMFI` and `github`; Avid MDB/Bento specifications; Apple Bento revision/PDF names; original toolkit byte-order handling; and MDVx manual references. This is a broad search of discoverable material, not a claim that every online document exists in the search index or that private SDK documentation was accessible. No user MDB content was uploaded.
