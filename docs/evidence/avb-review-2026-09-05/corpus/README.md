# AVB corpus differential, 2026-09-05

Review-only work: product and attached inputs were not changed. `summary.json` contains compact results, role counts, fingerprints and limitations. `comparison.json` and `media_joins.json` remain scratch evidence and are intentionally omitted from the reproduction archive.

The 42 real files comprise the 14 attached fixtures, two MediaMuster OMF fixtures and 26 bins under the explicitly scoped `Documents/Avid Projects` corpus. The two OMF repository fixtures duplicate two local bins; totals count file provenance rather than deduplicating their content. An ID/bin pair is not a media file.

`compare.py` mirrors the current text/wrapped-OMF scanner and compares it with the actual C++ parser executable. It also checks an experimental exact 49-byte typed MobID decoder, constrained to indexed AVB chunks, against the reference reader. The typed envelope is 17 bytes label section + 8 bytes length/instance tags + 24 bytes tagged UUID. Its purpose is feasibility evidence; production parsing should read actual class fields and graph references.

`generate_fixtures.py` writes valid LE/BE bins containing a master ID only in its tagged binary field, plus a valid bin with an unrelated ID in Comments, and an intentionally truncated header. The first three reopen using the reference reader but were not opened/saved with Media Composer. Current C++ returns valid with zero IDs for the binary-only bins, returns only the unrelated comment ID (both forms) for the comment bin, and returns valid for the 12-byte header.

`join_media.py` joins graph file/master IDs to the existing 2,493-row audit export: all 363 matching media/bin pairs were already matched by the current scanner. This historical snapshot does not establish a live media loss; the missing IDs include physical sources, while known media may still join a scanned master.

Rebuild the current parser probe (local paths reflect this review machine):

```sh
/usr/bin/c++ -std=c++17 -fPIC -I /Users/martymclean/Developer/MediaMuster/src -I /Users/martymclean/Qt/6.5.3/macos/lib/QtCore.framework/Headers -F /Users/martymclean/Qt/6.5.3/macos/lib /tmp/mediamuster-avb-review/corpus/avb_probe.cpp /Users/martymclean/Developer/MediaMuster/src/avbparser.cpp /Users/martymclean/Developer/MediaMuster/src/logcategories.cpp -framework QtCore -Wl,-rpath,/Users/martymclean/Qt/6.5.3/macos/lib -o /tmp/mediamuster-avb-review/corpus/avb_probe
python3 -B /tmp/mediamuster-avb-review/corpus/generate_fixtures.py
python3 -B /tmp/mediamuster-avb-review/corpus/compare.py /tmp/mediamuster-avb-review/corpus/fixtures/binary_only_le.avb /tmp/mediamuster-avb-review/corpus/fixtures/binary_only_be.avb /tmp/mediamuster-avb-review/corpus/fixtures/unrelated_comment_le.avb /tmp/mediamuster-avb-review/corpus/fixtures/header_only.avb
python3 -B /tmp/mediamuster-avb-review/corpus/join_media.py
python3 -B /tmp/mediamuster-avb-review/corpus/summarize.py
```

Framework CPU detection aborts with a false missing-NEON error under the restricted sandbox; the comparison driver was approved to run outside the sandbox. Inputs are opened read-only. Python bytecode writes are disabled.
