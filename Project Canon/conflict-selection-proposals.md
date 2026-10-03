# Conflict selection: agreed principles and detailed proposals

3 October 2026. The user approved the recommendations summarized in the readiness
review: per-property rules; validated header preference for technical facts; retain
all observations; blank unresolved values with Console diagnostics; show all
established MasterMobIds in table and CSV. These are agreed behaviour, not implemented
changes. Preserve today's behaviour as the comparison baseline, with deliberate
corrections separately documented. Detailed editorial-name/project/original-bin
priority orders below remain proposals where not covered by that approval; the
summary did not specify those exact orders.

## Common eligibility rules

Before ranking values, establish that they describe the same subject, track,
property meaning and units. File length versus clip-reference length, current bin
membership versus original bin, and sample storage bits versus valid bits are
different facts, not automatically conflicts.

Require correctly associated file/object identities and readable typed properties.
A database from another file is not an eligible fallback. Timestamp consistency is
supporting evidence, not proof of correctness. Changed/unreadable snapshots cannot
silently outrank coherent observations. Compare normalized semantic values while
preserving raw values and encoding; equivalent fractions can agree even when their
original spellings differ. Do not manufacture approximate equality thresholds.

Keep all competing observations and the selected rule/version/reason. `Absent`,
`NotRead`, `Unreadable`, an explicit false/zero, and a real value are different states.
An explicit false may be selected; it must not lose to a sticky earlier true.

## Field-specific recommendations

| Field | Proposed selected-value rule | If relevant sources disagree |
| --- | --- | --- |
| Location, filename, size, file birth/modification time | Filesystem evidence for this physical file at the inspected snapshot. | Preserve earlier snapshots; detect changes. Database dates/filenames describe their own records and must not replace physical-file facts. |
| File identity | Relevant file/source object in the media header, with coherent container association, rather than a filename alone. | Retain the PMR/MDB contradiction, report it, and exclude metadata joined under the wrong identity. If header identity cannot be established, retain the database identity as qualified evidence; never invent proof. |
| Master associations | Preserve all supported graph relationships. Show all established MasterMobIds in the table cell and CSV using consistent formatting. | Multiple masters need not be a conflict. Do not choose a first master and discard others or merge physical rows. |
| Codec, sample representation, component layout, alpha, video/sample rates, channels | Coherent relevant media descriptor/header evidence, with format-specific essence-header cross-check where necessary; eligible matching MDB is fallback when media evidence is unavailable. | If a validated format rule establishes authority, select its result and retain disagreement. If descriptor and essence signalling remain contradictory, leave that field unresolved and log an issue instead of guessing. |
| Resolution | Apply the user-approved geometry meaning to correctly associated recorded display/sample/stored properties. | Preserve all geometry sets; different geometry meanings do not conflict merely because dimensions differ. Genuine unresolved contradictions remain unresolved; no nearest raster or unconditional padding trim. |
| File Duration | Relevant stored-essence length/count and its exact clock; supported file-track fallback remains qualified. | Keep master/clip-reference lengths separately. For true contradictions, use an evidenced format rule or leave unresolved; do not pick the shortest, longest or sibling's length. |
| Clip Duration | Relevant material/master track/reference length and its own clock/context. | Preserve distinct tracks and associations; do not sum or flatten them. Multiple candidate values for the same track require a supported selection rule or an unresolved result. |
| Clip Name | Preserve the current principal preference: supported material/master header name, then matching MDB master name, then unambiguous loaded AVB fallback. | Same-rank conflicting candidates leave the field unresolved. Lower-ranked alternative names remain retained. A bin rename is not silently preferred unless the user chooses current-bin editorial naming. |
| Project | Preserve the current principal preference provisionally: exact eligible PMR association, then matching MDB master/file, then associated header value. | Keep owner context and all names. Wrong-identity or changed evidence is excluded. Equally ranked valid contradictions leave the field unresolved. Confirm this ordering with the user; no universal format authority is asserted. |
| Original Bin | Prefer an explicitly recorded original-bin attribute for the relevant object; preserve current MDB/supporting OMF-header priority provisionally. AVB fallback must supply that original-bin fact. | Do not replace with the current bin containing the clip. Conflicting equally eligible original-bin values remain unresolved. |
| Imported source path/name/container | Select a coherent set from the same associated import record where possible, rather than constructing a name/path from unrelated observations. | Retain alternative records and provenance; equally authoritative contradictory import origins remain unresolved. Source basename is derived only from the selected path when appropriate. |
| Media/precompute role and categories | Supported selected-master usage/classification plus applicable effect evidence. | Explicit ordinary-media evidence can supersede a provisional precompute inference. Unsupported/conflicting classifications stay unknown; do not classify from filename or clip title. |
| Effect name/category/sequence | Preserve recorded tokens/annotations; derive friendly labels from verified, cited catalogue rules. | Unknown tokens remain retained. Ambiguous friendly mappings remain unresolved; sequence annotation is not proof of timeline usage. |
| Local PMR membership | Record actual membership result independently of metadata selection. | Listed plus contradictory header identity is possible: retain both and report. It does not become verified identity agreement. |
| Database freshness | Retain specific snapshot/timestamp/change observations rather than a universal "fresh" verdict. | Do not treat file/database timestamp agreement as proof of matching contents or common rebuild generation. |

## Ties and unresolved results

If eligible observations agree, select the value and link all supporting evidence.
If a documented field rule establishes a preferred source, select it while retaining
`Conflicting` agreement where appropriate. When the rule cannot distinguish equally
eligible contradictory candidates, retain an unresolved selected value and a Console
issue; never use source iteration order or majority vote. The user approved blank
displayed values when there is no defensible winner. Ordinary unavailable/inapplicable
display follows the column review; Alpha retains its explicit blank rule.

Avoid warning once for every lower-ranked alternate editorial name without context.
Retain all observations; report unresolved or material identity/technical problems
with grouped source/field counts and affected file locations. The exact diagnostic
severity/filtering remains an implementation proposal for review.

The developer-controlled rule collection remains handwritten typed C++ for v1 as
proposed in [metadata selection policy](metadata-selection-policy.md). No new
persistent database or selection-editor UI is required.
