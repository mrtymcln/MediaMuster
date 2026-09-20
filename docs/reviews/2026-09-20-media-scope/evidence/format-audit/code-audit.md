# Independent MediaMuster format-routing audit

20 September 2026. Read-only audit of current source and existing evidence at repository HEAD `8d735b3a840ead3d089fa9422c843c15e240f256`, with unrelated feature-gate work active in the shared working tree. No production parser, scanner, or test file was edited. This report is about MXF/OMF recognition and database joins, not a whole-application audit.

## Plain-English result

The application's basic split is correct: MXF media and OMF media have different on-disk container structures, and both have databases bearing the same PMR/MDB filenames. It does not identify the producing Media Composer release, and there is no valid “before 2004 versus after 2004” branch. Real repository OMF WAVE/AIFF files were written freshly by Media Composer 26.8 on 2 September 2026.

The database readers decode both identity encodings. The scanner normally chooses the media reader by the filename suffix and known folder arrangement. For legacy audio in renamed folders, it makes an additional whole-folder inference from the database identifiers. That inference is useful but is not equivalent to examining each file's actual container. A reproducible mixed-database case defeats it. A separate `.aiff` suffix support gap is also confirmed.

## What the code actually does

1. `src/mediascanner.cpp:930–942` enumerates actual files and accepts the suffixes declared in `src/conventions.h:184–208`. With OMF disabled, only `.mxf` rows can pass; known OMFI trees are also excluded during discovery (`src/mediascanner.cpp:323`, `609`, `719`, `843`).
2. Shared readers load PMR/MDB data (`src/mediascanner.cpp:1008–1085`). There is no `includeOmf` switch inside these parsers: the flag gates legacy media discovery, not interpretation of the database used for modern MXF.
3. PMR filename records identify the file and master. The scanner joins filename to PMR, then file/master IDs to MDB (`src/mediascanner.cpp:1183–1213`). The filenames `msmFMID.pmr` and `msmMMOB.mdb` do not themselves reveal the media format.
4. The per-file routing rule (`src/mediascanner.cpp:118–123`, assigned at `1164`) is: a supported legacy suffix, AND one of `.omf`, folder named `OMFI MediaFiles`, or the whole-folder legacy-ID inference. A `.mxf` suffix fails the legacy-suffix test first, so an inherited legacy ID cannot turn a correctly named MXF row into an OMF row.
5. The whole-folder inference (`src/mediascanner.cpp:1108–1139`) requires at least one key, and all examined PMR file IDs, MDB file IDs and MDB master IDs must have Avid's wrapped legacy form. It deliberately does not inspect every MDB source object: physical source objects can carry 32-byte IDs in real OMF files and are excluded by MDB triage.
6. Current, sufficiently complete database facts avoid media-header I/O. Missing, incomplete or stale records trigger a header read (`src/mediascanner.cpp:1217–1235`). PMR timestamps are freshness heuristics, not byte-level proof of file identity. A file changed while preserving the recorded timestamp can therefore retain cached metadata until another reason triggers header verification.
7. Header dispatch is explicit: `OmfParser::parseHeader` or `MxfParser::parseHeader` (`src/mediascanner.cpp:1336–1347`). The OMF reader validates Bento structure and its object graph; MXF reads header partition/KLV structure. Successful container/metadata parsing gives stronger per-file evidence than filename/folder conventions.

## Database and identity interpretation

- PMR version words describe PMR record layouts, not Media Composer releases. `src/pmrparser.cpp:122–158` reads 8-byte identity fields for versions <= 7 and 32-byte fields for version 8 or Unicode extension version 16. Version 1 omits project/master fields. `src/pmrparser.cpp:250` reproduces the inspected Avid signed `<9` acceptance check; accepting zero/negative values is not evidence those historical PMR versions shipped.
- The optional version-16 Unicode set has its own count and can replace the earlier record set (`src/pmrparser.cpp:264–291`). A version-2 base PMR can therefore provide returned 32-byte wrapped identities. “32 bytes means MXF” would be wrong even within PMR.
- The OMF standard's UID is 12 bytes. The PMR 8-byte representation is Avid's cache-specific encoding, not the OMF container's standard identifier width. `src/omfuid.h:88–108` preserves general non-prefix-42 12-byte IDs in an `omf:` namespace and bridges observed Avid prefix-42 IDs into the 32-byte wrapper.
- `src/mdbparser.cpp:13–24` correctly describes MDB as an OMF/Bento object store even for MXF media. Modern MDB parsing must therefore retain shared OMF object/property handling. The MDB's container format and the indexed media's container format are different facts.
- `src/omfuid.h:46–59` correctly warns that the prefix alone is insufficient; current MXF fixtures can share the same prefix. However, the subsequent “never this [suffix]” wording overstates general certainty. Legacy ID form describes identity provenance/encoding, not universally the current media container. The source already acknowledges carried-over legacy IDs in MXF-era databases (`src/mediascanner.cpp:1112–1114`).
- OMF schema version and Bento container version are independent. `src/omfobjects.cpp:42–94` determines OMF1/2 from the HEAD version/properties, including Avid's observed legacy `00 01` marker. It does not infer the schema from Bento's version.

## Confirmed limitation 1: mixed databases can suppress valid OMF audio recognition

The rule at `src/mediascanner.cpp:1119–1139` turns the entire folder's legacy inference off when any examined identity is modern. The per-row decision is made before the matching PMR/MDB record is consulted (`1164` versus `1196`). Consequently a valid matching legacy audio record cannot rescue that row's routing if an unrelated record makes the folder mixed.

Independent reproduction used unmodified production scanner/parsers and disposable copies of real fixtures:

1. A renamed `RenamedArchive` folder contained the real OMF WAVE fixture plus its real legacy PMR/MDB.
2. The scanner reported `omfEra=true`, `WAVE (OMF)`, and 48,000 Hz.
3. The probe added only the unrelated real modern `msmFMID.pmr` fixture under the supported secondary name `amaFMID.pmr`. Original media and original PMR/MDB were unchanged.
4. The same scan reported `omfEra=false`, no requested header read, blank codec, and zero/unknown sample rate. The legacy clip name still came from its existing matching MDB master.

This is a constructed mixed-cache scenario using real bytes, not proof that an untouched Media Composer install produces that exact pairing. It demonstrates the supported folder/secondary-database inputs can lose valid per-file classification.

The stored flag also affects operations: `src/opmanager.cpp:125` copies it to `OpItem`; `src/operationplan.cpp:13–16` uses it to choose `OMFI MediaFiles` versus `Avid MediaFiles/MXF/<folder>` for preserve-mode destinations. A misclassified WAV would therefore receive an MXF-style destination if an operation were requested. No actual copy, move, or delete was executed by this audit.

Existing test `tests/tst_scanner.cpp:2219` intentionally lists real OMF WAV dropped into an MXF folder without parsing it. That test pins the current optimization/policy; it cannot establish general container recognition. Pure renamed OMF folders and MDB-only OMF folders are covered at `1966` and `2040`; they do not exercise mixed IDs.

Proportionate future improvement: keep the fast database path, but use matched per-file evidence and bounded container identification when legacy audio remains ambiguous. An explicit unknown/container state would be clearer than treating every failed legacy inference as MXF for destination planning. The exact design should follow requirements for arbitrary renamed/shared archives; it need not require opening every media file on every scan.

## Confirmed limitation 2: `.aiff` is omitted from scanner admission

`src/conventions.h:194–199` admits `.omf`, `.aif`, `.wav`, and `.sd2`; not `.aiff`. In the independent probe, the actual valid AIFF-C OMF fixture was copied unchanged to `OMFI MediaFiles/renamed.aiff`:

- Direct `OmfParser::parseHeader` returned valid metadata and `AIFF-C (OMF)`.
- Production scanner returned zero rows, with OMF explicitly enabled.

This proves a suffix support gap for renamed `.aiff` specimens. It does not establish that Media Composer normally writes `.aiff` rather than `.aif`; broadening admission should retain container validation for ordinary non-OMF AIFF files.

## Actual-container checks and boundaries

- OMF: `src/omfparser.cpp:86–114` calls `BentoFile::open`, then requires MOB information and rejects multiple conflicting embedded media IDs. `src/bentofile.cpp:284–386` checks a valid tail label or RIFF/RF64 `omfi` chunk. A plain WAV is not automatically valid OMF because of its suffix.
- MXF: `src/mxfparser.cpp:217–257` searches the bounded run-in for a header partition key, with an existing recovery path for standalone metadata KLVs. Later code validates framing and graph metadata. This is a metadata reader, not a full essence-integrity validator.
- `.omf` routing is a reasonable candidate choice, not proof that arbitrary bytes with that extension are valid OMF. `.omf` can also be an interchange composition containing multiple media references rather than one managed Avid essence file. The parser's single-media identity checks preserve a useful boundary.
- Real PMR fixture coverage is versions 2 and 8, little-endian. Other PMR layouts/byte orders have constructed tests based on the existing instruction-derived findings, not historical original specimens. Native SDII resource forks and some extended Bento/RF64 structures remain documented gaps.

## Documentation corrections worth making

- `src/mdbparser.cpp:79–80` says a version-2 PMR carries no project. The fresh 2026 fixture actually carries a project in both record sets, and `tests/tst_pmrparser.cpp:820–841` explicitly checks it. The bundled slate PMR has empty projects; that is specimen-specific.
- `tests/fixtures/omf/README.md:31–34` says SDII handling is “by name only”; that is stale relative to `docs/parser-compatibility.md`, which records implemented descriptor reading with constructed OMF1/2 SDII tests and accurately distinguishes the absent native resource-fork specimen.
- “Legacy,” “OMF-era,” and “pre-MXF” labels should describe format lineage rather than date or producing app release. The fixture README itself proves contemporary OMF creation.

## Validation and coverage

The probe is in `probe/` and its output is `probe-result.txt` alongside this report. It builds against current production source using Qt 6.5.3, AppleClang 17, C++17. It reads original fixtures and performs scans only on disposable temporary copies. Sandbox execution initially aborted because Qt's CPU feature detection reported unavailable NEON; authorized execution outside the sandbox succeeded with exit 0. No parser behavior was changed to obtain the results.

Read relevant portions of scanner/discovery, PMR, MDB, OmfUid, OMF/MXF entry points, Bento recognition, OMF schema handling and current tests. Read the parser-compatibility, PMR-completeness, architecture, release-gates, implementation-validation and OMF fixture documents, plus historical parser review/coverage and focused historical conclusions. This is a targeted audit; it is not a claim to have re-audited every line of the large codec readers or every historical test body. The parent task owns full current-suite execution and fresh binary comparison; the independent public-specifications agent owns external primary-source research.
