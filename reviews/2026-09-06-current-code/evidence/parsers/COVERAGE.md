# Parser coverage

Fully read production source logic and headers present on disk:

- src/avbparser.cpp, src/avbparser.h
- src/avidtext.h
- src/avidusage.h
- src/avidprecompute.h
- src/avideffects.cpp, src/avideffects.h
- src/bentofile.cpp, src/bentofile.h
- src/mdbparser.cpp, src/mdbparser.h
- src/pmrparser.cpp, src/pmrparser.h
- src/pmrkey.h
- src/mxfparser.cpp, src/mxfparser.h
- src/mxfproperties.h
- src/omfobjects.cpp, src/omfobjects.h
- src/omfparser.cpp, src/omfparser.h
- src/omfresolutions.cpp, src/omfresolutions.h
- src/omfuid.h
- src/mobid.h

Generated data: src/avideffectscatalogue.inc processed in full as 887 rows; unique name/category pairs, normalized aliases, shared aliases and comma-containing names inspected. Relevant rows were then read with exact line numbers. Not an independent validation of every translation or Avid registration.

Fully inspected executable test logic and fixture builders listed below. Some original reads elided comments/blank lines. The follow-up explicitly extracted and read all C++ line/block comments in every one of these files, excluding comment-looking text inside literals, closing the comment-reading gap. No executable code remains uninspected in this list; blank lines carry no code:

- tests/testavb.h
- tests/testbento.h
- tests/testbento2.h
- tests/testomf.h
- tests/testutil.h
- tests/tst_avbparser.cpp
- tests/tst_avbmetadata.cpp
- tests/tst_avideffects.cpp
- tests/tst_bentofile.cpp
- tests/tst_mobid.cpp
- tests/tst_omfuid.cpp
- tests/tst_pmrkey.cpp

Selectively inspected large suites, with complete test-function inventory and targeted relevant tests/fixture construction:

- tests/tst_mxfparser.cpp: primitive builders, descriptors, audio rates, graph selection, split-master duration, malformed fields/partitions, test slots.
- tests/tst_mdbparser.cpp: duplicate MobID merge, audio summaries, direct import/track evidence, malformed classification evidence, test slots.
- tests/tst_pmrparser.cpp: record/version test inventory, Unicode/count/null/length boundaries and framing.
- tests/tst_omfparser.cpp: test inventory and referenced duplicate/descriptor/master cases; not every body line independently inspected.

Additional narrow cross-component reads for verified call chains: src/binfilter.h; src/mediatablemodel.cpp:1–110; src/mediascanner.cpp:1310–1350; src/logcategories.cpp.

The named .cpp files corresponding to avidtext, avidusage, avidprecompute, pmrkey, mxfproperties, omfuid and mobid do not exist: those components are header-only.

Review artifacts and build only under /Users/martymclean/Developer/MediaMuster/reviews/2026-09-06-current-code/evidence/parsers. No repository modifications.

## Additional dependency check requested by parent

- `src/third_party/xxhash.c`: fully read; every byte compared with primary upstream v0.8.3.
- `src/third_party/xxhash.h`: every byte compared with primary upstream v0.8.3, and every line read, including API contracts/examples and every architecture-specific implementation/preprocessor branch. Initial selected reading was completed with all remaining ranges; precise inventory in `XXHASH_REVIEW.md`. Full source reading is not a formal correctness proof or execution of every architecture.
- `src/opcopier.cpp` and `.h`: fully read to trace the xxHash ownership and call paths; no claim of replacing the operations agent's broader safety review.
- Sanitized standalone C++17 library-caller boundary probe: 294 cases passed on macOS ARM64; does not establish Windows execution.


## Explicit executable test-coverage boundary

This parser reviewer left executable bodies partly uninspected in exactly these four inventoried suites: `tests/tst_mxfparser.cpp`, `tests/tst_mdbparser.cpp`, `tests/tst_pmrparser.cpp`, and `tests/tst_omfparser.cpp`. Targeted areas are listed above; a test-function inventory alone was not treated as a body read. No exact per-line reading ledger was retained for those initial selective reads, so do not invent a fully inspected line range for them. The parent reports separate reviewers have now fully covered these large suites; the combined review should attribute completion to their coverage rather than this parser reviewer's original selective reads.

For the twelve smaller tests/fixture headers listed above, executable code was fully read initially, and all comments were explicitly read during the completion pass. Therefore they are no longer limited to comment-elided coverage.
