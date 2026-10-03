# Audit coverage by Project Canon

Assessment: 3 October 2026. Compared the HTML audit and its 40 finding/scope entries
with the agreed requirements and proposed design in Project Canon. These categories
are an engineering assessment of design coverage, not measured completed fixes.

## Counts

| Coverage category | Count | Meaning |
| --- | ---: | --- |
| Directly addressed by the central model | 7 | Candidate/evidence/state/selection design directly targets the reported failure mechanism; parser integration, rule decisions and tests are still required |
| Supported, but needs a specific correction or decision | 24 | The design provides a useful representation or boundary, but does not itself specify/implement the complete fix |
| Separate correction or scope decision | 9 | Format typing/evidence, operation safeguards, wording or supported scope needs explicit work beyond the central metadata refactor |
| Total audit entries | 40 | Includes genuine defects, authored/static findings, documentation errors and intentional scope limits |
| Closed by verified implementation in this planning work | 0 | Production code has not yet changed |

The 1,103 occurrence locations are related code/tests/comments/docs references, not
1,103 independent bugs. A single finding can require several code and documentation
changes. The 31 entries in the first two groups are not a promise of 31 automatic fixes.

The rewrite must preserve the current information while treating deliberate format
corrections as separately evidenced changes. A faithful evidence container cannot
correct a wrongly decoded value unless the relevant decoder/mapping is also fixed.
Retaining every property creates a requirement to address omissions, not proof of
complete supported-format coverage. Newly discovered semantic/selection decisions
must be brought to the user as agreed.

## Finding-by-finding map

### Directly addressed by the central model

| Finding | Audit title | Remaining work / connection to Canon |
| --- | --- | --- |
| F07 | Repeated MXF PackageUIDs overwrite the lookup | Candidate-preserving identity indexes and explicit ambiguity replace silent overwrites. Apply this inside MXF package lookup, not just the physical-file inventory. |
| F13 | Repeated OMF/MDB mob records choose the first descriptor and values | Retained observations, multiple object candidates and per-field selection replace first-descriptor/first-value wins. Parser candidates must survive through resolution. |
| F22 | PMR timestamps establish a cache heuristic, not current content | Source snapshots and distinct timestamp-consistency/freshness facts replace the implication that a cache match establishes current content. Revise names and claims too. |
| F25 | Boolean/timecode merges cannot replace a positive value with false | Read presence and typed values distinguish an explicit false from absence/unknown. The resolver must permit an eligible false to replace true. |
| F26 | Stale editorial values can override a usable same-identity header | Per-field source, freshness, conflict and selection policy directly address stale editorial precedence. Individual authority rules still need validation. |
| F38 | Codec/effect tables and absence-based defaults are empirical | Retained raw values, derivation rules and source/version provenance make empirical mappings and defaults distinguishable from recorded facts. Mapping provenance must be carried into actual results. |
| F39 | A unique orphan project is treated as the selected file’s project | Recorded/derived basis and owning-object context make orphan-project recovery an explicit inference. The resolver must not label the heuristic established ownership. |

### Supported, but needs a specific correction or decision

| Finding | Audit title | Remaining work / connection to Canon |
| --- | --- | --- |
| F01 | 2.14 fixed point is displayed as Float | Bit depth and sample encoding have separate places in the model, but fixed-versus-float descriptor decoding and tests must actually be corrected. |
| F02 | RGBA component depths are mostly ignored | Complete property/component observations can retain PixelLayout, but readers must parse every component/depth pair correctly. |
| F03 | Stored, sampled and display geometry are collapsed | Multiple geometry properties can coexist; readers must extract them and the user must choose the Resolution column semantics. No detail UI is required for retention. |
| F04 | Padding and field-height rules are promoted to universal facts | Original values and inference reasons can be kept, but normalization must be restricted to evidenced conventions and explicit geometry must take precedence. |
| F05 | MXF graph traversal follows ordinary 16-byte values | Recorded relationships have a home, but MXF traversal must use declared reference properties/classes rather than every 16-byte value. |
| F06 | Missing MXF references silently disappear | Missing/unreadable evidence and relationship context can be represented; graph completeness and decisive-reference checks must be implemented explicitly. |
| F08 | Primer-less tag recovery has the same output confidence | Recorded/derived and source locators support recovery distinctions, but Primer/property identification and private-property gating need specific decoder rules. |
| F09 | Several MXF property types accept the wrong lengths | Typed/raw observations can retain malformed data; exact property widths and safe numeric range checks remain decoder fixes. |
| F11 | Numeric resolution IDs fabricate codec ULs | Unknown numeric values can be preserved instead of invented labels; replace arithmetic UL fabrication with evidenced mappings. |
| F12 | Legacy text decoding can silently change the text | Raw encoding and uncertainty can be retained; text decoding still needs explicit encoding/code-page and ambiguity rules. |
| F14 | OMF HEAD indexes and media-object classes are bypassed | Source object context can retain active/recovery distinctions; OMF HEAD membership and media-data class validation must be implemented. |
| F15 | OMF timecode is the first TCCP found anywhere in ancestry | Relationships can carry track/position context; OMF timecode traversal must evaluate applicable branches/offsets rather than the first TCCP. |
| F16 | A single OMF mob rate is an insufficient multi-slot model | Per-track properties/rates fit the model; relevant-slot selection still needs a correct OMF implementation and genuine coverage. |
| F17 | Identifier width is used as a media-era discriminator | Object role/schema/family are separate concepts; remove identifier-width-driven behaviour with supported descriptor/family evidence. |
| F19 | Standard OMF locator classes are omitted | Previously omitted locators can be retained; implement the locator classes and distinguish actual paths from URLs/text hints. |
| F20 | WAVE valid bits and subtype are ignored | Storage bits, valid bits, subtype and mask can coexist; WAVE extensible-tail parsing and validation remain specific work. |
| F23 | Normalized PMR filename collisions choose the first record | Location identity and ambiguity principles help, but PMR matching needs exact-name-first and unique-compatible-candidate rules, including case/index collisions. |
| F24 | Database files are not read as a coherent snapshot | Snapshots and changed-source states help; implement pre/post identity/stat checks, retries and coherent-pair qualifications. Stable stats alone do not prove a common rebuild generation. |
| F27 | Every nonempty MXF header is read with the current flag | Read coverage and preservation of existing information are explicit; removing header I/O requires equivalent validated extraction or a separately agreed capability policy. Existing misleading docs still need correction. |
| F29 | Whole-database and metadata-copy I/O has avoidable cost | Shared RAM observations and measured allocation reduction help; file-backed MDB access and redundant MXF reads/copies need targeted implementation and benchmarks. Faithful retention can also increase memory. |
| F33 | Format acceptance is broader than native Avid qualification | Read outcome is separate from meaning/confidence; native container/profile qualification remains a separate specification and implementation decision. |
| F35 | Hard-coded placeholder identity is assumed never real | Do-not-discard and object-role evidence expose this exclusion; placeholder recognition must be revised rather than retaining an unconditional ID blacklist. |
| F37 | Rate display buckets obscure exact nearby rates | Exact typed rates are preserved; display buckets and codec-tier calculations still need exact-versus-approximate rules and corrected claims. |
| F40 | MXF package discovery is not scoped through the active header root | Object context/reference preservation supports root scoping; MXF Preface/ContentStorage membership must actually constrain package discovery. |

### Separate correction or scope decision

| Finding | Audit title | Remaining work / connection to Canon |
| --- | --- | --- |
| F10 | 4–8 byte duration “flavours” are not established MXF types | Strict MXF duration width/recovery admission and unsupported format claims require a specific decoder/test/documentation correction. |
| F18 | Private AUID conversion assumes little-endian numeric fields | Private AUID endianness requires serialization evidence, a genuine big-endian specimen or explicitly narrower support; the RAM model does not answer it. |
| F21 | AIFF/RIFF summary validity is weaker than chunk validity | RIFF/AIFF/AIFC chunk/summary framing and subtype validity need parser-specific checks. |
| F28 | The documented64 MiB MXF metadata cap does not exist | Remove the stale 64 MiB-cap documentation. The agreed no-application-memory-cap policy does not itself edit that existing claim. |
| F30 | MXF identity operations accept two byte-order spellings | Operation identity guards must compare one canonical identity; journal migration/provenance is separate from display metadata selection. |
| F31 | OMF operations have no format identity recheck | OMF operation-time identity rechecking over the protected file/handle needs explicit operation-engine work. |
| F32 | Automatic database-rebuild wording is too broad | Database-rebuild UI wording must be qualified by local/unmanaged versus managed storage; this is separate from the deferred evidence UI. |
| F34 | Whole-bin MSML membership is not timeline usage | Whole-bin reference filtering versus selected-timeline usage requires explicit scope wording; full semantic timeline/effect evaluation is a separate feature. |
| F36 | Supported file suffixes and container subclasses are a subset | Supported suffixes, legacy container versions and subclasses need explicit scope decisions and genuine specimens. Completeness within admitted inputs does not silently expand discovery to every exchange/legacy format. |

## Closure requirements

Close a finding only after its actual correction is implemented, affected claims and
tests are updated, and its failure mode is checked with appropriate evidence. Preserve
the audit distinction between genuine media, authored specimens and static risks.
Where no genuine specimen or authoritative serialization evidence exists, narrower
explicit support or an honest unresolved state can be the correct outcome. Do not
claim new support, byte equality, semantic timeline usage or native Avid certification
merely because observations are retained.

The two output defects established on genuine media, F01 and F02, should receive
explicit parser fixes and regression checks early. Typed graph traversal/completeness
and operation identity checks deserve their own work items; they cannot be inferred
from KelpieId uniqueness.

[Original HTML audit](../docs/reviews/2026-10-03-format-audit/format-audit.html)
and [audit findings data](../docs/reviews/2026-10-03-format-audit/evidence/findings.json).
