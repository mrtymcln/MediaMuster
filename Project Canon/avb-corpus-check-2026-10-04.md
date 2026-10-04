# Canon AVB corpus check — 4 October 2026

The fresh reader successfully read all nine supplied bins. It retained **132,165 objects and 2,988,319 object properties**, with no unreadable properties, unknown object layouts or reader diagnostics in these specimens. Object and class counts match the separate pyavb framing inventory. These are observations about this corpus, not proof of support for every AVB ever written.

The probe read exactly **14,464,589 bytes**, equal to the combined bin sizes. It opened each source read-only and checked that its size and modification time stayed unchanged. No media payloads were opened. This count measures logical reads requested by the reader, not physical disk traffic.

## Reader and reference results

| Bin | Objects | Object properties | Reader | Entire-bin reference coverage |
| --- | ---: | ---: | --- | --- |
| `sample.avb` | 192 | 5,414 | Complete | Complete |
| `Nön English bin náme™ 你好 漢.avb` | 22 | 1,614 | Complete | Complete |
| `01_SEQ.avb` | 99,714 | 2,148,541 | Complete | Incomplete: six descriptors belong to external path-only media |
| `02_SEQ_LOCK.avb` | 25,096 | 630,625 | Complete | Complete |
| `D001_A001_TRNSCDS.avb` | 1,073 | 30,561 | Complete | Complete |
| `D001_A002_TRNSCDS.avb` | 1,253 | 35,513 | Complete | Complete |
| `D002_A001_TRNSCDS.avb` | 1,445 | 40,826 | Complete | Complete |
| `D003_A001_TRNSCDS.avb` | 1,733 | 49,113 | Complete | Complete |
| `D003_A002_TRNSCDS.avb` | 1,637 | 46,112 | Complete | Complete |

Each bin was indexed separately. The following results therefore establish references within that bin, without borrowing evidence from the other eight bins.

| Selected sequence | Bin | Reachable MSML locator objects | Distinct full media identities | Reference coverage |
| --- | --- | ---: | ---: | --- |
| `Testsequenz` | `sample.avb` | 3 | 3 | Complete |
| `ROUGH` | `01_SEQ.avb` | 234 | 232 | Incomplete: external media described below |
| `FINE_01` | `01_SEQ.avb` | 293 | 293 | Complete |
| `FINE_02` | `01_SEQ.avb` | 280 | 280 | Complete |
| `FINE_03` | `01_SEQ.avb` | 268 | 268 | Complete |
| `FINE_04` | `01_SEQ.avb` | 357 | 357 | Complete |
| `FINE_05` | `01_SEQ.avb` | 411 | 411 | Complete |
| `FINE_06` | `02_SEQ_LOCK.avb` | 511 | 511 | Complete |

The seven complete sequence results are `Testsequenz` and `FINE_01` through `FINE_06`. For comparison, the entire `02_SEQ_LOCK.avb` graph contains 517 media identities; selecting `FINE_06` reaches 511. Locator counts and identity counts describe bin evidence. They are not physical file counts, and repeated identities do not merge MediaMuster's per-location rows. Complete reference coverage does not establish that the referenced media files currently exist.

`ROUGH` reaches three `MULD` objects: 92874, 92880 and 92886. Each contains one `CDCI` and one `PCMA` child with a null locator. The parents also have null locators. Their physical-media references lead through `MDES` to `FILE` locators naming `Scene04Rough.mp4` under an old `/Volumes/AVID DRIVE/.../008 EXPORTS/081 ROUGH/` path. This is evidence of external path-only media, not six proven missing MXF files. The reader preserves those properties and paths. The resolver reports incomplete coverage rather than pretending the established MSML identities are the whole selection; the result has `complete = false`. The future live filter adapter must block applying it under the agreed policy.

## Timing and memory observations

These figures come from the **Debug universal library, Qt 6.5.3, with an x86_64 probe under Rosetta on Apple silicon**. Each bin was tested in a fresh process. They are neither release-performance measurements nor a comparison with the existing application's 3,774 ms scan baseline.

| Bin | Read and decode | Build reference index | Resolve entire bin | Process peak after reader | Process peak after reporting/index/sequence/whole-bin work |
| --- | ---: | ---: | ---: | ---: | ---: |
| `01_SEQ.avb` | 3,778.8 ms | 173.2 ms | 558.5 ms | 1,081.0 MB | 1,108.4 MB |
| `02_SEQ_LOCK.avb` | 1,085.0 ms | 47.9 ms | 131.3 ms | 332.9 MB | 341.1 MB |

For `01_SEQ.avb`, individual sequence resolution took approximately 90–148 ms after the index was built. `FINE_06` took 115.2 ms. The other seven bins took 3.1–85.4 ms each to read. The raw results retain exact figures for every run.

Memory is the process high-water mark reported by macOS `getrusage`, in decimal MB here. It includes Qt, the runtime, evidence objects and temporary allocations; the later figure also includes the diagnostic report. It is not retained metadata size alone or the application's eventual memory consumption. The large bin's roughly 1.1 GB peak is a material observation to carry into memory work: preserving millions of individual observations currently costs substantially more RAM than the bin's 9.1 MB on disk. No memory limit has been imposed, and no evidence was dropped to obtain these figures.

## Reproducible evidence

- [Final reader/reference probe source](evidence/avb-reader-probe-2026-10-04.cpp), [results](evidence/avb-reader-probe-results-2026-10-04.jsonl), [build and measurement context](evidence/avb-reader-probe-context-2026-10-04.json), and [exact supplied paths](evidence/avb-corpus-paths-2026-10-04.json).
- [Independent framing inventory script](evidence/avb-framing-inventory-2026-10-04.py) and [results](evidence/avb-framing-inventory-2026-10-04.json).
- [Six ROUGH descriptor observations](evidence/avb-rough-descriptors-2026-10-04.json), [narrow parent-locator probe](evidence/avb-rough-parent-probe-2026-10-04.py), and [parent-locator results](evidence/avb-rough-parent-locators-2026-10-04.json).

From the repository root, link the standalone probe against the built library:

```sh
clang++ -std=c++17 -arch x86_64 -Isrc \
  -F/Users/martymclean/Qt/6.5.3/macos/lib \
  -I/Users/martymclean/Qt/6.5.3/macos/lib/QtCore.framework/Headers \
  'Project Canon/evidence/avb-reader-probe-2026-10-04.cpp' \
  build-canon/libmediamuster_canon.a -framework QtCore \
  -Wl,-rpath,/Users/martymclean/Qt/6.5.3/macos/lib \
  -o /tmp/canon-avb-reader-probe
```

Run `/tmp/canon-avb-reader-probe` once per supplied path, in a fresh process, and collect its JSON lines. The source paths are local evidence references; the bins themselves are not copied into the repository. Unit-test results are recorded separately in the main AVB reader note.
