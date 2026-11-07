# PMR filename association: approved exact spelling

4 November 2026. **Exact PMR filename spelling approved.** MatchContext owning
the extracted reports/lookups is approved. Bella's readers remain unchanged.
This concerns association of decoded PMR names with physical files in the
same media directory, not UI Search or a new path-canonicalization policy.

## Recorded rules and current code

PMR's filename is a managed-file matching claim. Displayed Filename/File Path
come from the filesystem. Only full qualified file/master IDs join metadata.
The agreed workflow preserves existing path semantics and adds no new symlink
resolution or normalization policy. The user has now explicitly selected exact
decoded PMR-to-physical-basename spelling for Bella, including case and Unicode
code points, instead of previous scanner's guarded normalization fallback.

Current previous scanner uses exact names first. If no exact spelling exists, it can
use `PmrKey::primary` (NFC normalization followed by lowercase). It accepts that
fallback only when the normalized physical filename is unique in the folder;
exact-name and identity qualifications still apply. See `src/pmrkey.h` and
`removed source scancoordinator.cpp`, `matchFile`.
UI Search's case/accent rules are not PMR association requirements.

## Fresh actual-name observation

Read-only enumeration found seven PMRs in 142 folders from the earlier
accessible physical-media discovery scope. They are installed Avid supporting
media, Shared Avid MXF/OMFI and the four EDIT managed MXF folders. No generated
test PMR or synthetic name was used. Bella's existing unchanged parser returned
Completed/Verified and usable selected metadata for all seven reports.

- 2,492 selected PMR filenames; every one exactly matched a physical basename
  in its own directory using Qt's case-sensitive QString comparison.
- Zero entries matched only after the current NFC/lowercase conversion.
- Zero normalized physical-name collisions for these entries.
- All recorded PMR filenames were ASCII; this is not non-ASCII producer coverage.
- PMR file stamps and folder-name snapshots stayed stable before/after.

Receipts, per-entry observations, source/library hashes and the temporary
C++17/Qt 6.5.3 observation tool are under
`/private/tmp/bella-pmr-name-comparison-20261104/`. Folder scope comes from
`/private/tmp/bella-header-identity-need-20261104/` and retains its exclusions.
This is evidence of exact matching in the checked files, not proof that case/
Unicode differences never occur. No Media Composer binary behavior is newly
claimed and no new format restriction is inferred from this corpus.

## Approved exact filename association

Match the decoded PMR filename to the physical basename exactly, in the same
directory, retaining the original spelling. For example, `Clip001.mxf` matches
`Clip001.mxf`; `CLIP001.MXF` is not the same PMR-name match under this option.
Do not trim, rewrite underscores, case-convert or normalize the strings for
this association. This changes the proposed Bella association algorithm; it
does not alter filesystem path resolution or current previous scanner behavior.

Benefit: the rule is direct and sufficient for every checked PMR entry. It
does not require additional sibling-name uniqueness information solely for a
normalized fallback. Cost: an entry differing only in case or equivalent
Unicode composition cannot establish this PMR mapping. The already-agreed
header-identification path can then be required; PMR-only metadata may remain
unavailable, with eligible MDB/header fallback under existing priorities.
Both comparison strategies give the same filename associations in this sample.
No scanner runtime comparison of header counts or final results was performed;
no speed or memory improvement is claimed.

## Declined alternative: retain previous scanner's guarded normalized-name fallback

Keep exact-name preference, then the existing NFC/lowercase fallback when it
unambiguously identifies one captured physical basename. Benefit: retains
matching for case/Unicode-composition differences outside this observed sample.
Cost: additional comparison/ambiguity rules and complete relevant sibling-name
facts are needed. On a case-sensitive folder, a PMR name such as `TAKE.mxf`
must not be assigned to both physical `take.mxf` and `Take.mxf` merely because
their normalized keys agree. This is an illustrative boundary, not a claimed
real specimen or reason by itself to add the fallback.

## Scope and next input facts

Both arrangements retain full-ID, same-folder, source eligibility/reference
coverage and header-opening rules. A PMR miss is not automatically NoReference;
a qualified local MDB reference still counts. Useful metadata/physical rows
survive an unavailable PMR mapping under the recorded rules.

The user selected exact spelling on 4 November. Matching needs the current physical path and
captured folder-discovery completeness to distinguish NoDatabase from unresolved
discovery. Exact physical-input member names/layout remain a subsequent contract
check-in. No sibling-name roster, NFC/lowercase key or normalized-name uniqueness
flag is needed for the selected exact association. Do not add another complete MediaFile inventory
or change reader reports for these facts. Matching performs no filesystem reads.

No runtime matching, reader, MediaFile/table, progress, journal, Unfinished
Business, Undo or Bin Filter code is changed. previous scanner remains active.
