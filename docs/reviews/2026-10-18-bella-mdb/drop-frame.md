# Bella: MDB timecode and drop-frame numbering

Research date: 20 October 2026. **The wire interpretation and observed source
relationships below are established research. The user subsequently approved
the discussed Duration design: verified maths, correctly linked DF, NDF when
genuinely missing and blank Duration when required information is damaged.**
Recorded qualified DF, exact arithmetic, frame-numbered timecode, sample
preservation, zero/blank and hours beyond 24 are already agreed. OldScanner
remains active; this adds no runtime implementation or Bin Filter change.

**Separator revision approved on 22 October 2026:** DF uses `HH;MM;SS;FF` and
NDF uses `HH:MM:SS:FF`, superseding the earlier colon-for-both interpretation.
Exact maths, source/clock/flag qualification, zero/unavailable handling and
hours beyond 24 remain unchanged. Zero labels use the selected mode's separators
(`00;00;00;00` for DF, `00:00:00:00` for NDF).

**Source revision, 21 October 2026:** MDB Duration remains first; coherent
own MXF timing may supply Duration when the scan falls back to MXF because
usable database information is unavailable. MXF-specific owner, audio display-
clock and DF qualification remain under review. The verified MDB flag layouts
below do not establish MXF flag layouts or permit mixing numbering from an
unrelated source. No Duration-only header read is introduced. See the
[MXF fallback-field decision](../2026-10-21-bella-mxf/fallback-fields.md).

## Edit rate and timecode duration

Edit rate is a clock, in edit units per second. A duration count says how many
units there are. Timecode duration is a length formatted with the chosen clock
and numbering convention. At 25 fps, 300 frames have timecode duration
`00:00:12:00`. The rate alone does not say how long the recording is, and the
formatted text alone does not completely identify its rate or numbering mode.

[Avid's common-object API](https://developer.avid.com/ctms/api/rs_common_object_properties.html)
exposes editRate, duration and durationTC separately. For Bella audio, the
preserved sample count/sample clock establish precise length; a separately
qualified own-file editing clock supplies the agreed FF presentation. These public
API definitions do not prove Media Composer's private MDB Duration-cell path.

## Native flag encoding

The installed arm64 engine's `ATimecodeClip::Get` and `Put` call `TCGet` and
`TCPut`. Resolved stream vtables and dictionary keys establish these fields:

| Layout | Drop-frame representation | Related timing value |
| --- | --- | --- |
| Observed legacy MDB | Complete 32-bit `OMFI:TCCP:Flags`; **bit 0** is DF | `TCCP:FPS`, 16-bit nominal frame base; component EditRate supplies clock context |
| OMF2 typed layout | Separate one-byte Boolean `OMFI:TCCP:Drop` | `TCCP:FPS`, specified as UInt16 in OMF2.1 |

`ATimecodeClip::ConvertToOMF2` explicitly masks `flags & 1` to form Drop. Other
bits must not by themselves be declared damaged or make the parser treat the
entire Flags word as a Boolean. In particular, words 2 and 3 have different
bit-0 states even though both are nonzero. Unknown other bits are outside this
DF interpretation. For the separate Boolean layout, require a complete known
typed 0/1 value; do not turn malformed bytes into false.

The actual databases store Flags as four-byte `omfi:Long`, FPS as two-byte
`omfi:Short`, in their established little-endian layout. These are numeric
fields, not text. Other supported object byte orders require scalar decoding
in that order. OMF2 coverage is native/spec research, not a new actual-file test.

Native `TCGet` can obtain a global application default for FPS zero. Bella
should not copy that implicit application-state fallback. The native nominal
DF predicate is broader than the two initial Bella cases; this does not
automatically establish broader rational-clock support or Duration UI policy.

## Finding the applicable timecode

The earlier direct-file-only suggestion is too narrow for these databases.
None of the 2,412 physical-file owners has its own TCCP. The relevant candidate
timecode is on an explicitly linked source record. The original Avid-authored
[OMF2.1 specification](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf)
describes using the file SourceClip's SourceID, track and position to find the
corresponding source timecode span (printed pages 95–96); page 206 defines the
TCCP Drop/FPS fields.

Fresh live hashes of all six MDBs match the audited raw indexes. A bounded
candidate census finds:

| Physical rows | Observation |
| ---: | --- |
| 2,412 | One own SourceClip and one uniquely linked immediate source mob |
| 2,304 | Timecode candidates on that immediate source |
| 108 | No reachable timecode component in the checked complete relationships |
| 2,303 | Reachable candidates record NDF only |
| 1 | Reachable candidate records DF |

These are candidate relationships, **not a claim that every row has undergone
complete source-track/position/range qualification**. There are 2,011 distinct
TCCP objects: 2,010 Flags zero and one Flags one. Object counts and physical-row
counts differ because several files can refer to the same source.

The single DF example was qualified further: physical CDCI file object 73992
has Length 18,340 at stored 2997/100. Its own picture SourceClip 73985 uniquely
references source mob 70288, picture track 1, position zero. The source's
timecode component 70283 has the same span, EditRate 2997/100, FPS 30 and Flags
one. The mapped interval `[0,18340)` fits its sole timecode span. The picture
and timecode tracks both use label 1, so track kind must participate in matching.
Sixty-six selected raw fields were freshly compared with the live MDB bytes.

Clock compatibility also matters in actual data: 52 files use 23976/1000 for
their display clock while the linked source timecode uses 59940/1000 and FPS
60. Their source flags are NDF. That source numbering is inapplicable to
23.976/base-24 Duration and must not override or blank it. No additional rate
conversion for those source relationships is established by this review.

## Agreed Bella rule

1. Establish the Duration display clock first. Supported NDF-only clocks such
   as 24/25 need no DF lookup. For a DF-capable clock, do not infer DF from the
   rate alone.
2. Follow only qualified recorded file/source relationships. Resolve the
   essence track by identity, track/kind and mapped position; find the applicable
   timecode segment/span. Support the demonstrated simple direct/immediate
   source layouts initially. Do not recursively gather every reachable flag or
   pick the first readable timecode track.
3. Check the complete typed flag, nominal FPS and relevant timing context.
   Initially apply DF only to the verified `30000/1001`/base-30 and
   `60000/1001`/base-60 cases. A mismatched source clock is not automatically
   damage; clearly inapplicable timecode does not participate.
4. Keep one compact numbering mode plus the shared availability/reason status.
   Temporary source/segment reading need not become a retained per-row graph.
   Agreement among all applicable spans can produce one mode; unresolved or
   mixed applicable modes are unavailable for this single Duration label.

For a clock where a DF/NDF choice is needed, the user-approved rules are:

| Relevant MDB result | Agreed display behaviour |
| --- | --- |
| Usable applicable DF flag | DF numbering with `HH;MM;SS;FF` separators |
| Usable applicable NDF flag | NDF numbering with `HH:MM:SS:FF` separators |
| Applicable flag proven absent after a complete supported lookup | NDF as an explicit fallback; preserve absent status rather than claiming recorded NDF |
| Damaged applicable value, broken/ambiguous applicability, conflicting applicable modes or unsupported purported DF mode | Blank formatted Duration; retain independently valid exact count/rate and the physical file |
| Clearly unrelated or different-clock timecode | Ignore it; it cannot override or invalidate a supported NDF-only clock |

A broken reference is not proof of absence. An unreadable relevant value must
not silently become false. Conversely, do not blank known 24/25 or other
supported NDF-only Duration because unrelated source timecode is unusable.
Established zero can retain its zero label because numbering mode cannot
change a zero length; its input qualification remains separate from absence.
Use the selected mode's separators: `00;00;00;00` for DF or `00:00:00:00` for NDF.

This is an approved Bella default/error policy, not a discovered universal
Media Composer UI rule. Avid's public
[time-based resource](https://developer.avid.com/ctms/api/aa/resources/time-based.html)
also treats omitted dropFrame as false in that API, but it does not specify
private MDB damage handling. General slot, pulldown, mixed-range, unsupported
layout and damage fixtures still need implementation coverage.

Keep MDB first and the 21 October qualified database-unavailable MXF fallback.
Do not open a header solely for Duration or repair MDB timing/numbering from
a header Frame Rate alone. An isolated damaged MDB field does not automatically
trigger fallback. Do not add StartTC to Duration, substitute a source/master length,
change source samples or guess a default rate. Use semicolons for DF and colons
for NDF, preserving hours beyond 24. The full priority-table discussion
remains at the end of the requirements plan. The discussed audio editing-clock
design and verified rounding maths are accepted with this Duration approval;
supported ownership/layout and numeric edge coverage still need implementation
verification. The [approval receipt](../../../Project%20Bella/evidence/mdb-duration-plan-2026-10-20.json)
records the user decision and supersedes pending status in historical evidence.

## Current code and evidence

Current [timecode projection](../../../src/oldscanner/mediaengine/mdbprojection.cpp)
sweeps the recursively linked source graph, accepts whole Flags/Drop values
only equal to 0/1, and does not qualify FPS or timecode ranges. The
[row adapter](../../../src/oldscanner/mediaengineadapter.cpp) converts an
unavailable flag to false. Its existing association and DF arithmetic tests
provide useful facts, but their incomplete ownership/clock checks and unavailable
to false conversion are not Bella requirements.

### Current-app accuracy assessment, 20 October

The user asked whether current MediaMuster is correct and 100% accurate. The
source audit does **not** support an unqualified guarantee. Its original
count/rate retention and integer audio-to-frame conversion are useful, while
flag applicability, missing/conflict handling and zero presentation have
specific limits. This assessment concerns the checked-out current scanner and
formatter, not a new live scan of the installed app.

Current MDB audio uses the descriptor-owning file's CPNT EditRate, or supported
same-owner slot rates, as its display clock. The
[integer conversion](../../../src/mediaduration.h) rounds only the final frame
count, with positive halves upward. Original audio samples remain unchanged.
When available, header Duration and DF outrank MDB under the current
[selection table](../../../src/oldscanner/mediaengine/metadataselectionpolicy.cpp).
The two fields are selected separately; display-clock supplementation requires
an equivalent count/unit-rate pair. These are OldScanner behaviours, not changes
to Bella's agreed source rules.

The definite limitations are:

- Recursive MDB flag gathering does not validate source track/position/span,
  nominal FPS or compatible clock before accepting a flag.
- Equal-best flag conflicts clear the selected value in
  [resolution](../../../src/mediaevidence.h). With no usable selected flag, the
  adapter's Boolean conversion makes missing/conflicting/ignored-invalid
  results NDF. A usable preferred header flag may still win if one exists.
- The whole-word 0/1 restriction misses bit-0 meaning on other valid legacy
  words, such as Flags 3. This is established from source/native semantics;
  it was not triggered by the observed actual 0/1-only corpus.
- Known zero and valid positive audio rounded to zero display blank under the
  current formatter. This differs from Bella's agreed zero presentation.

Current formatting applies DF only at nominal base 30 or 60 and ignores a true
DF flag at 24/25. Therefore the 52 actual different-clock source examples,
whose flags are all NDF and own clock is 23.976/base24, are evidence of the
selection gap, **not 52 demonstrated wrong Duration labels**. The one qualified
actual DF example has matching track/range/clock; no error was demonstrated in
that example. Current literal decimal-rate handling also differs from Bella's
interpreter, but a particular audio label difference must be demonstrated
before declaring that physical file's displayed Duration wrong.

No accuracy percentage is established. No complete runtime/corpus comparison,
new parser test execution or production fix was performed for this follow-up.

The [curated evidence](../../../Project%20Bella/evidence/mdb-drop-frame-2026-10-20.json)
preserves native disassembly receipts, live MDB hashes, index census, the
positive binding and selected raw-byte verification. Native flags/FPS meaning
is established; complete native Duration-cell selection/default behaviour is
not. No app, scanner, parser test suite or media header was run. Only research
artifacts and requirements documents were written.
