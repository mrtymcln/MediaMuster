# Scan scope and OmfScan

Source inspected 3 October 2026. No production changes in this documentation task.

## Agreed naming

Future feature and column name: **OmfScan**, enabled by default for discovery.
Per-file column: true for accepted OMFI-family media, false for MXF-family media.
Includes legacy .aif/.wav by earlier agreement. This supersedes Media Format.
The boolean is not a copy of the global flag. Actual container stays separate.

Current names are `FeatureFlags::kOmfEnabled`, `Options::includeOmf`,
`MediaScanner::scanOmfRoot()` and `MediaFile::omfEra`; there is no current OmfScan
class/function/column. Adopt the agreed terminology during the rewrite.

## Agreed rewrite scope

| Folder | Accepted extensions |
| --- | --- |
| `Avid MediaFiles/MXF/[number or name]` | `*.mxf`, `*.pmr`, `*.mdb` |
| `Avid MediaFiles/MXF/Quarantined Files` | `*.mxf`, `*.pmr`, `*.mdb` |
| `OMFI MediaFiles` root and its immediate numbered/named subfolders | `*.omf`, `*.aif`, `*.wav`, `*.pmr`, `*.mdb` |

OMFI discovery requires OmfScan, enabled by default. The user explicitly confirmed
root-level OMFI media as well as immediate subfolders. Quarantined Files is an
accepted named MXF folder, explicitly listed for clarity. Preserve existing
case-insensitive matching and hidden/staging/symlink exclusions.

Database discovery is **extension-only** within these accepted folders. Do not
probe or require particular PMR/MDB basenames or msm/ama prefixes. This supersedes
the earlier known-filename recommendation. Extensions admit candidates; readers
must still validate their contents and report unsupported/unreadable databases.
Databases supply observations, not physical-media table rows. Retain observations
from all admitted databases; filename alone must not silently select a winner.

## Current rules (before the rewrite)

Sources: [scanner](../src/mediascanner.cpp), [layout](../src/avidmedialayout.h),
[conventions](../src/conventions.h), [feature flags](../src/featureflags.h).

- Root spelling is **Avid MediaFiles**, with a space; `AvidMediaFiles` is not that
  root name. Comparisons of the recognized folder names are case-insensitive.
- Scan accepted immediate child folders of Avid MediaFiles/MXF, including numbered,
  workstation-prefixed, other named folders and Quarantined Files. Numeric `[n]`
  is not an exclusive scanner requirement.
- Exclude dot-hidden/Creating staging folders. Media folders are flat, not recursively
  crawled. Loose .mxf files directly in the MXF root are not enumerated by this path.
- Admit non-dot-hidden .mxf files case-insensitively within accepted MXF folders;
  file symlinks are excluded. Other suffixes are ignored as media candidates.
- Configured msm/ama PMR/MDB filenames are database inputs, not media rows. This is
  not a reader for every arbitrary .pmr/.mdb file anywhere. Databases are optional.
- OMF off excludes OMFI MediaFiles scans. On includes that separate root and accepted
  immediate workstation folders, with .omf/.aif/.wav candidates and recognized
  databases. Accepted OMFI roots are outside the Avid MediaFiles tree.
- Volume scans probe configured roots/bases, not the whole drive recursively;
  manually added eligible managed trees can be elsewhere. UME/unrelated trees are
  excluded by the admission rules.

The rewrite preserves accepted named folders and root-level OMFI media. Extension-only
database discovery above deliberately changes the current fixed-basename rule.

"Ignored" and "not scanned" both mean no media row is produced, but distinguish
where exclusion occurs. Ignored files may be encountered in a directory listing
and rejected by the suffix/admission rule; their media contents are not parsed by
that path. An OMFI tree excluded by OmfScan off is not traversed/read for media or
databases by the OMF scanner. Parent/root discovery may still inspect folder names.

## Agreed filtering approach for the rewrite

Qt provides wildcard name filters through `QDir::entryInfoList()` / `setNameFilters()`:
[official QDir documentation](https://doc.qt.io/qt-6/qdir.html). For accepted MXF
media folders, use a combined listing for `*.mxf`, `*.pmr`, `*.mdb`, then dispatch
by extension to the appropriate reader. No separate database-basename checks.
Preserve case-insensitive suffix matching, dot-hidden/symlink
exclusions and current managed folder scope.

Any PMR/MDB basename is admitted within the agreed folder scope.
Do not apply the MXF filter to discovering folder names, which would hide eligible
subfolders. The OS/Qt still has to enumerate directory entries to apply filters;
unmatched media contents are not read, and reduced metadata/allocation work should
be measured rather than promising no directory I/O or a particular speedup.

With OmfScan enabled, use the separate agreed legacy candidate filters .omf/.aif/.wav
plus `*.pmr` and `*.mdb` within the OMFI tree. A global .mxf/.pmr/.mdb-only
filter would accidentally disable agreed legacy support.
