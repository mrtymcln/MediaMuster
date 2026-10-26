# Bella: PMR reading and memory lifetime

Research date: 21 October 2026. **PMR read-once/extract/release memory workflow
approved by the user on 21 October 2026; not yet implemented.** The PMR field/set/fallback rules
and four-job maximum remain approved. The equivalent MDB read-once memory
workflow and separate four-MDB maximum were already approved.

## Agreed practical rule

The user explicitly accepted this workflow: "read each PMR once into memory,
extract only what we need, and release the buffer." Research limitations and
future implementation checks remain unchanged by that approval.

Use the same broad lifetime for PMR as MDB:

**one complete capture into job-owned memory → validate/select a whole set →
keep compact owned results → release the raw buffer and parsing temporaries.**

Current MediaMuster and MDVx already buffer ordinary PMR scan reads. Bella's
opportunity here is to reduce interpretation work and temporary allocations,
not to eliminate per-row disk rereads that these inspected paths already avoid.
No speed or memory improvement has been measured by this review.

## Current MediaMuster / OldScanner

The active scan path uses `prepareDatabase`, which opens the PMR and captures
its complete bytes through `DatabaseImage::read`. Capture uses bounded reads
into one owned byte array. It then provides a read-only `QBuffer` over the same
Qt-shared storage to `PmrReader`; the parser's field reads are from memory.
That wrapper does not make another complete byte-for-byte image.

The PMR grammar interprets both stored sets into detailed objects, properties,
raw field bytes, decoded values, relationships and set provenance. Projection
then derives file facts and metadata observations. The full source image and
temporary parsed-object structures are released when preparation finishes.
The source receipt preserves outcome/snapshot/diagnostics rather than the full
parsed source. Some selected raw field values and evidence can survive in
projected observations; this is distinct from retaining the whole PMR image.
Those per-field observations can survive until `OldScanner::doScan` finishes
formatting rows. The final table rows do not own the full `ScanResult` or its
per-field evidence. The normal database-preparation loop is currently
sequential; Bella's approved four-PMR and four-MDB maxima concern the future
replacement, not current OldScanner execution.

Qt implicit sharing means multiple references to a string/byte array do not
automatically mean separate physical copies of its contents. The meaningful
comparison is the actual structures, allocations and work retained at each
stage, not a count of C++ assignment statements. Direct standalone reader
tests can supply other devices; this report describes the app's database scan
preparation path.

## Installed MDVx

Its ordinary PMR operation reads the complete file into a memory buffer and
walks records there. It interprets both stored sets and later removes
duplicates. Item construction copies selected fields and also keeps a mutable
copy of each PMR record's bytes, which can be byte-swapped in place.

The full PMR buffer lasts through its directory job, including optional
MDB/header work, and is released at cleanup. Items and their copied record
slices can remain until the items are destroyed. MDVx therefore demonstrates a
buffered job workflow, but its per-item retained record bytes and double-set
interpretation are unnecessary for Bella's agreed result model.

Its queue maximum is six PMR operations; Bella's already agreed maximum is
four. These are independent database/directory jobs, not multiple workers
splitting a single PMR.

## Agreed Bella flow

1. Capture an unchanged PMR once into a job-owned byte buffer. Close the source
   after complete capture; subsequent parsing and fallback use those bytes.
2. Follow known older-set framing safely to locate v16. Do not decode and keep
   every older value merely to reach the preferred set. If framing cannot
   establish the boundary, follow the already agreed unsupported/damage rules;
   do not search raw bytes for a plausible version word.
3. Validate/extract the preferred v16 set into private compact temporary
   results. Publish nothing until the entire set passes the agreed checks.
4. If v16 is absent or unusable, discard any rejected temporary results and
   try the older set from the **same in-memory capture**. Do not reopen the PMR
   or combine values from different sets.
5. Retain owned Filename/Project text, full IDs through the agreed normalization
   helper, and only necessary compact status. Skip DTM with bounds checking.
6. Release the source buffer, views and temporary parsing/index storage when
   compact results are independent of them. No raw-record copy is needed in
   each final row. Keep physical inventory independent of PMR usability.

Read-once means one ordinary source capture. It can contain several operating-
system read calls and more than one pass over bytes already in RAM. Later
explicit rescans or rejected changing/failed sources are separate events.

## C++17 and Qt 6.5.3

The user's attached [C++ guidance](</Users/martymclean/Downloads/CPP SKILL.md>)
supports scoped ownership/RAII, immutable input and small owned value records.
A Qt byte array can own the capture; bounded non-owning views can inspect it
only while that owner lives. Final strings and binary IDs must own their data.
Move completed results onward without keeping a raw database alive through
views, pointers, borrowed hash keys or raw-data wrappers.

Do not allocate based on unchecked declared record counts, expose partial
results before whole-set validation, or retain both decoded sets unnecessarily.
Worker state stays local to each job. Four PMR and four MDB maxima remain
separate; combined peak memory and storage contention still require measurement.

## Observations and verification limits

Fresh size-only observations of the six previously studied working PMR paths
total **583,336 bytes**, approximately 0.58 MB, with the largest 164,660 bytes.
That makes whole-file capture a modest input-buffer cost in this corpus. It
does not measure temporary object overhead or bound other users' PMR sizes.

The [curated evidence](../../../Project%20Bella/evidence/pmr-memory-lifetime-2026-10-21.json)
records source/native findings, file-size observations, applied C++ principles
and the approved workflow. No scan benchmark, parser suite, app/private API execution
or live database write was performed. OldScanner remains active and Bin Filter
is unchanged. The MDB-version baseline was subsequently approved in the
[separate review](../2026-10-18-bella-mdb/versions.md).
