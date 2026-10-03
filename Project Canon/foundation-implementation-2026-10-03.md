# Project Canon: first implementation stage

Recorded 3 October 2026. Branch: `codex/project-canon-foundation`.
The user subsequently chose fresh replacement engines. See the
[replacement plan](replacement-engine-plan.md). This aggregate-based implementation
is retained as a working comparison; it is not the final engine architecture.

This is the RAM/evidence foundation and its initial consumers. It does not close
all findings in the format audit or implement the complete planned format model.

## Implemented

| Area | Result |
| --- | --- |
| Physical rows | Scan-wide nonzero `quint64` KelpieIds; separate rows for different physical paths, including matching Avid identities |
| Lifecycle | Fresh assignment on scan/model reset; sorting/filtering keeps IDs; confirmed ordinary move updates the existing row; successful ordinary copy adds a row with a fresh ID |
| RAM evidence | Shared immutable source receipts; sparse Qt implicitly shared observation and selection maps; recorded/derived basis, raw input where available, read state, eligibility, freshness, conflicts and selection explanation |
| States | `NotRead`, `Present`, `Absent`, `Unreadable`; independent agreement state; unset fields remain addressable without allocating empty cells |
| Source coverage | Existing decoded PMR/MDB/MXF/OMF aggregates feed evidence. Source/property locators explicitly identify aggregates where readers do not expose precise binary property locations |
| Technical selection | Validated selected header metadata, then qualified matching MDB metadata; equally ranked different answers remain unresolved; rational-rate comparison retains original fractions separately |
| Selected technical fields | Codec, resolution, rates, file duration, bit depth, sample format, kind and internal channel count |
| Clip name | Material/master header name, then associated MDB names; tied incompatible names stay blank. Loaded-bin fallback still uses the existing resolver |
| Associations | MDB retains all masters referencing a file source; scan-wide joins across admitted folders retain associations without merging physical rows |
| Database discovery | PMR/MDB extension only, including arbitrary names and uppercase extensions. No special msm/ama filename priority |
| Media discovery | Existing managed folder scope retained. MXF family admits MXF; OMFI root and immediate accepted children admit OMF/AIF/WAV. Existing hidden/staging/symlink exclusions remain |
| Legacy flag | `FeatureFlags::OmfScan`, enabled by default; existing flag callers retain a compatibility alias |
| Table/export | MobId, all MasterMobIds separated by `;`, KelpieId and OmfScan. OmfScan table column follows its gate; CSV always retains the family boolean |
| Internal fields | Sample Format and Channels stay internal; Bit Depth remains visible; unknown Kind displays blank; Date Created keeps its name |
| Stamp | RAM receipt has path, native volume identifier, listing modification timestamp, selected file MobId and retained master IDs; no filesystem file identifier or size added to that receipt |
| Diagnostics | Scoped PMR expected-path issues with matching paths elsewhere; unmatched MDB file identities distinguished from missing-file claims; selected-field conflicts retained and logged |
| DNxUncompressed correction | Complete standard/fixed coding UL plus component depth distinguish integer, half float, float and fixed point. Sentinel 254 alone does not mean Float. Bit Depth and numeric representation are separate |

A move/copy updates filesystem evidence and the receipt. Earlier header/database
receipts remain historical evidence at their original source paths. A destination
with database files is unverified until read, rather than confidently marked absent.

## Read cost and memory

Admitted nonempty media now receives a header read even when a database supplies
current metadata. This deliberately enables independent evidence comparison.
Zero-byte candidates remain physical rows. No media payload is decoded by this
change. Reader byte limits and existing parsing behaviour otherwise remain.

Database source receipts are shared across rows. Qt copy-on-write containers let a
copy initially share observations while location observations and selection changes
detach as needed. There is no imposed RAM cap and no persistent metadata database.
This is not yet a completed performance optimization.

## Verification

The original code passed all 26 registered suites before changes. The foundation
adds an evidence suite and targeted scanner, model, CSV and numeric-format checks.
Final isolated build: universal macOS Debug executable (arm64 and x86_64), Qt
6.5.3, C++17. All **27 CTest suites passed**, zero failed, in 21.23 seconds on
this Mac. The executed architecture was arm64; this is not Windows or Intel
runtime validation. `git diff --check` and strict bundle signature verification
also passed. Tests use temporary fixtures for writable operation scenarios.

- [Full suite result](evidence/foundation-tests-2026-10-03.txt)
- [Read-only real-scan inventory and issues](evidence/foundation-real-scan-2026-10-03.json)
- Review build: `/Users/martymclean/Developer/MediaMuster/build-canon/MediaMuster.app`

The separate `build-canon` folder avoids interference from the other build process
observed using `build`. The foundation was subsequently committed on the foundation branch.

New checks exercise unresolved ties without majority voting, source eligibility,
copy-on-write isolation, allocator exhaustion/reset, actual copy/move row updates,
optional-column notifications, all-master export, renamed database discovery and
local missing references with two matching locations elsewhere.

A read-only real-drive test covers these selected managed roots:

- `/Volumes/EDIT/Avid MediaFiles/MXF`
- `/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF`
- `/Users/Shared/AvidMediaComposer/OMFI MediaFiles`

The first diagnostic run returned 2,413 distinct paths/nonzero unique KelpieIds,
including both legacy audio files, in 3,932 ms from scanner start through delivery
of its completed inventory/issues. No file contents on these drives were modified.
The final isolated-build read-only run returned the same 2,413 rows in **3,849 ms**.
It recorded four standard DNxUncompressed float files as 32-bit/Float and four
S2.14 files as 16-bit/S2.14 fixed point. Eight existing RGBA-related DNxUncompressed
files remain without established bit depth/sample format; their parser coverage is
still pending. There were no empty codec cells in either run. The final report retains 23
unmatched MDB file/source identities: three from EDIT folder 1, eight from 8647
and twelve from 8646. It records no PMR local-absence issue in this real scan.
The Console groups unmatched identity counts by source database; individual IDs
remain in RAM/report. Unmatched identities can reflect historical or other
metadata and are **not** automatically classified as missing physical files.
In the JSON report, issue kinds 0/1/2/3 mean local reference absent / unmatched
MDB identity / metadata conflict / changed source respectively.

The Debug test process reported 488,767,488 bytes maximum resident set size and
417,237,440 bytes peak footprint via macOS `/usr/bin/time -l`. These measure the
whole harness, including Qt, concurrent readers and diagnostic output. They are
not retained-evidence RAM alone. The final run reported 479,887,360 bytes maximum
resident set size and 425,101,824 bytes peak footprint. These are not directly comparable to the supplied
147.1 MB Activity Monitor snapshot. Similarly, the earlier 3,774 ms scan is an
uncontrolled baseline; no speedup or unchanged resource cost is established.

The optional read-only test is skipped in the ordinary suite. Run it explicitly
with `MEDIAMUSTER_CANON_REAL_SCAN_ROOTS` containing semicolon-separated managed
roots and `MEDIAMUSTER_CANON_REAL_SCAN_REPORT` naming a JSON output file. This
report complements a future user-exported CSV; it does not replace that agreement.

## Remaining acceptance work

1. Complete raw property/object/reference retention in the readers, including
   unknown/private properties and repeated descriptor/track contexts. Current
   aggregate observations do **not** yet satisfy the complete 1:1 fidelity goal.
   The property enum is not a reader whitelist.
2. Move remaining editorial/classification/source fields and loaded AVB evidence
   through individual selection policies. Compatibility scalar fields and the
   existing bin resolver remain; this is not yet a universal evidence projection.
3. Finish canonical typed identity migration and ambiguous PMR identity selection.
   Current public MobId text still uses the application's existing PMR/MDB field
   order, with raw header IDs retained in observations where supplied.
4. Carry the five-field receipt into operation requests/journal recovery and enforce
   all applicable volume/path/time/MobId/master checks against the opened source.
   Existing operation guards still operate; **new scan receipts are not yet enforced
   by move/delete**. Handle OMF identity checks and unavailable checks explicitly.
5. Preserve row lifecycle through rebalance, recovered operations, published copies
   whose source removal fails and undo paths that currently rescan. Ordinary confirmed
   copy/move callbacks are covered; these other paths are not finished.
6. Detect sources changing during reads, preserve exact source snapshot freshness,
   and ensure superseded scan callbacks cannot affect a new session. Current receipts
   use directory-listing timestamps and database freshness qualifications, not a
   complete handle-bound snapshot guarantee.
7. Implement the verified handwritten DNx naming catalogue and exact square-bracket
   ReallyOldDnx aliases. The existing compressed-DNx naming table still operates;
   it has **not** been certified by this foundation.
8. Complete general RGBA/component sample-format and alpha parsing; add the agreed
   feature-gated Yes/No/blank Alpha presentation. These are not implemented here.
9. Compare a fresh user-exported CSV with the agreed baseline by physical location,
   explain changed values from evidence, measure comparable first/repeat timing and
   peak/retained RAM, and reduce avoidable allocations/read work.

Do not call the full Project Canon rewrite complete on the strength of this stage.
