# Scan scope and OmfScan

Source inspected 3 October 2026. No production changes in this documentation task.

## Agreed naming

Future feature and column name: **OmfScan**, enabled by default for discovery.
Per-file column: true for accepted OMFI-family media, false for MXF-family media.
Includes legacy .aif/.wav by earlier agreement. This supersedes Media Format.
The boolean is not a copy of the global flag. Actual container stays separate.

Names at the initial pre-rewrite inspection were `FeatureFlags::kOmfEnabled`, `Options::includeOmf`,
`MediaScanner::scanOmfRoot()` and `MediaFile::omfEra`; that historical implementation
did not have an OmfScan class/function/column. Adopt the agreed
terminology during the rewrite; see the current boundary clarification below.

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

Sources: [scanner](../src/mediascanner.cpp), [layout](../src/app_avidmedialayout.h),
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

For accepted MXF media folders, use a combined listing for `*.mxf`, `*.pmr`,
`*.mdb`, then dispatch by extension to the appropriate reader. No separate
database-basename checks. Preserve case-insensitive suffix matching,
dot-hidden/symlink exclusions and current managed folder scope.

Implementation correction, 4 October 2026: `QDir::entryInfoList()` returns an
empty list for both failed reads and empty/nonmatching directories
([Qt 6.5 documentation](https://doc.qt.io/qt-6.5/qdir.html#entryInfoList)). MediaEngine now
uses a checked C++17 directory iterator plus explicit entry-status checks, with
Qt's native path conversion, wildcard matching and hidden/symlink checks. Every
enumeration level reports access errors, retains readable results and marks the
scan incomplete. A folder that could not be inspected cannot establish absence.
See [verification](replacement-engine-plan.md#discovery-review-corrections-4-october-2026).

Any PMR/MDB basename is admitted within the agreed folder scope.
Do not apply the MXF filter to discovering folder names, which would hide eligible
subfolders. The OS/Qt still has to enumerate directory entries to apply filters;
unmatched media contents are not read, and reduced metadata/allocation work should
be measured rather than promising no directory I/O or a particular speedup.

With OmfScan enabled, use the separate agreed legacy candidate filters .omf/.aif/.wav
plus `*.pmr` and `*.mdb` within the OMFI tree. A global .mxf/.pmr/.mdb-only
filter would accidentally disable agreed legacy support.

## Fresh legacy reader, 4 October 2026

`MediaEngine::OmfReader` is now implemented independently of the production scanner;
see [implementation, evidence and limits](fresh-legacy-reader-2026-10-04.md).
It inspects bytes for supported OMF, WAVE/RF64 or AIFF/AIFF-C containers and retains
embedded OMF graphs separately from native audio headers. Database-free media
can supply its own header evidence. Extensions still control discovery admission;
the reader's broader byte recognition does not admit additional extensions or
folders. The OmfScan row flag remains a folder-family fact, independently of the
actual parsed container. Live-scanner integration and selection remain pending.

## Confirmed reader boundaries, 10 October 2026

The supported MXF family is Avid-compatible OP-Atom created by Media Composer or
third-party applications for Media Composer. OMF/legacy support stays separate.
The broad [format review](mxf-omf-read-scope-review-2026-10-10.md) supplies evidence
and qualifications, not a requirement to support every MXF application profile.

MediaEngine now routes MXF to `MxfReader`/`projectMxf` and legacy media to
`OmfReader`/`projectOmf`. Native WAV/AIF helpers preserve the previously agreed
legacy audio support. Both paths produce the shared source evidence and MediaFile
model; PMR/MDB matching and selection policies remain shared. The user subsequently
required MDB and OMF to own separate decoding implementations even where their
formats overlap. MDB now uses `MdbReader`/`projectMdb` and its own container,
object and audio-summary decoding. See [reader ownership](reader-boundaries-2026-10-10.md).

The implemented gate is `FeatureFlags::kOmfScan`, enabled by default. It feeds the
scan request's `omfScan` setting; disabling it skips OMFI roots before their media
and database enumeration. MXF-family scanning and its PMR/MDB readers remain
available. The independent-reader refactor preserves these gate semantics and
metadata retention; it changes implementation ownership.
