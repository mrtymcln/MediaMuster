# Which findings already existed in earlier MDB readers?

> Raw audit evidence and shared test fixtures were retired on 1 November 2026.
> Dated results and file references below describe the original investigations.

The table records pre-fix results. All 28 original controls were [rebuilt and rechecked after JPED](history-verification.md#post-jped-source-and-control-revalidation) and gave identical outputs before the user authorized the three qualification fixes. Those fixes are now [implemented and verified](VERDICT.md#qualification-fixes-implemented); the table explains their inheritance, not current open Bella defects. The withdrawn scope-expansion proposals remain excluded.

The later proposed [SEQU conflict finding is withdrawn](VERDICT.md#sequ-conflict-finding-withdrawn). Mixing OMF 1's `Sequence` with OMF 2's `Components` in an artificial record does not establish a supported-MDB failure or a native agreement rule. No extra conflict machinery or tests are required on that basis. The table's wrong-owner sequence routes are distinct from that withdrawn proposal; their approved fixes and the JPED addition remain valid.

Follow-up, 1 November 2026. The same retained category and graph controls were exported as 28 standalone MDBs (14 cases in each byte order) and run through the fresh current-source previous scanner probe from the baseline review. Every process exited normally. The generator and complete previous scanner reports are retained.

| Case | Pre-fix Bella | previous scanner baseline | Assessment |
| --- | --- | --- | --- |
| Null slot inside a counted attribute list | Skips slot and returns a category | Withholds category | Bella regression relative to previous scanner and the original TOC reader's explicit test |
| Track object changed from TRAK to ATTB, retaining TrackComponent | Counts it and returns Titles and Matte Keys | Withholds category | Bella lost the former category helper's track-class guard |
| Component object changed from SCLP to ATTB, retaining picture TrackKind | Counts it and returns Titles and Matte Keys | Also returns Titles and Matte Keys | Shared/inherited validation gap; not new to Bella |
| Known wrong-class track (FILL/TCCP) used for master relationships | Accepts complete direct master link | Also accepts link | Shared/inherited graph gap |
| SEQU:Sequence or NEST:Slots attached directly to a MOBJ master | Accepts the invalid route as a direct master link | Also accepts link | Shared/inherited graph gap in the active reader generation |
| SEQU:Sequence attached directly to a file MOBJ for Project inheritance | Returns the source Project | Also returns the source Project | Shared/inherited graph gap in the active reader generation |

The category rows have a narrow distinction: previous scanner checks that each direct track is TRAK (`removed source mdbprojection.cpp:987`), but does not qualify the component's class before counting its TrackKind. Its generic `collectComponents()` also traverses a union of property names regardless of their owner class (lines 741–752), just as Bella does. Saying simply “the older reader checked classes” would overstate its protection.

The pre-Canon original TOC reader has the same partial distinction in `9c286ee^:src/omfobjects.cpp`: `directVideoTrackCount()` requires a TRAK, then merely a nonempty component class. Thus it avoids the wrong-track category case but can still count a known unrelated component class. Its explicit null-attribute-slot regression test avoids Bella's first finding.

That original reader's general component walker does dispatch outgoing wrapper properties by class, unlike the active previous scanner's generic union. However, its initial TRKG:Tracks → TrackComponent step does not check the track's class. Therefore it is not accurate to say every original graph case was safe: wrong-class track traversal was already possible, while the later unconditional MOBJ → SEQU/NEST shortcut is broader. These original-reader statements are source inspection, not a newly executed historical binary test.

The three review findings therefore mixed lost safeguards and inherited flaws; the evidence does not show that all were newly introduced. They are now corrected in Bella. MDVX comparison is a separate native investigation, not established by this previous scanner probe.

The unterminated-key observation remains outside the three confirmed findings. Current previous scanner also accepts it in these controls; the original dedicated category test was stricter, but current approved requirements do not settle exact on-disk terminator acceptance.
