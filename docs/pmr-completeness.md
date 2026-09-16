# PMR completeness repair

Repair 2 prevents a structurally incomplete `msmFMID.pmr` from being treated as
a trustworthy list of media files. Previously, a lowered record count, unknown
extension or extra data could still return success, causing misleading Listed
or No Reference results.

The parser now accepts only a completely consumed base record set, optionally
followed by one complete version-16 Unicode set. Unknown extensions and bytes
left after the Unicode set produce a diagnostic and `ok=false`. Recovery entries
remain available through the existing parser API, but the scanner already
excludes entries from failed parses. Files without another trusted index show
No Database, and descriptive metadata can still be recovered from media headers
and the MDB. The existing primary/AMA index merge policy is unchanged.

Both byte orders, supported legacy versions, base-only files, genuinely empty
indexes and independent base/Unicode record counts remain supported. This is a
structural completeness check; it cannot establish that an otherwise valid
database is current or that unreferenced media is unused.

## Validation

- Before the fix, the new parser regression reproduced 72 false successes and
  the scanner regression reproduced four incorrect status results.
- All 26 Mac test suites pass after the fix. The app builds and its signature
  verifies. The parser suite passes 191 cases; the scanner passes 57 cases with
  its existing case-sensitive-filesystem test skipped on this volume. The MDB
  suite's optional external-toolkit test is also skipped without its samples.
- Tests cover lowered counts in both sections, unknown extensions, trailing
  bytes, repeated extensions and truncation, alongside valid controls.
- Every field of all 878 records in the five real PMR fixtures matches its
  pre-repair fingerprint, including modern and OMF-era databases.
- [Windows verification run 35073792665](https://github.com/mrtymcln/MediaMuster/actions/runs/35073792665)
  passes all four affected suites on Windows Server 2022: PMR 191, scanner 57,
  MDB 62 and OMF 37 passed cases, with the same two existing conditional skips.
  The diagnostic commit is `84b087c8a0ac1acf71e4555f35ed128c1c6e27ae`;
  its parser source and regression tests are byte-identical to this repair.

Repairs 3 (retired-original recovery) and 4 (temporary-file cleanup) are outside
this change.
