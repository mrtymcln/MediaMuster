# Bella MXF damage handling

23 October 2026. **The user approved these boundaries on 23 October 2026;
no Bella runtime change or cutover.**
The [approval receipt](../../../Project%20Bella/evidence/mxf-damage-handling-plan-2026-10-23.json) records the accepted scope.

Keep the physical file listed. Use metadata only when its bytes, meaning and
ownership can be established. Within a safely read header, one damaged value
should not erase independent usable information. When the header's structure
cannot be trusted, use none of that metadata region rather than publishing a
partially read prefix.

This follows the principle already agreed for MDB. It does not change the
approved field rules, source priorities or reasons for opening a media header.

## What the three readers do

These findings come from installed native routines and current source code.
They establish particular failure paths, not universal recovery guarantees.
The [evidence record](../../../Project%20Bella/evidence/mxf-damage-handling-2026-10-23.json)
retains binary hashes, native addresses, source anchors and inspected tests.

| Reader | Verified behavior | Consequence for Bella |
| --- | --- | --- |
| Media Composer | Storage/header restoration can throw; required descriptor Length/SampleRate failures can invalidate import. Typed getters distinguish successful values, valid empty text and failure. Some unknown properties are skipped. A per-file rejection can let the directory scan continue; memory and directory-level errors can propagate. | Keep explicit failure states and ownership checks. Do not infer a universal field-salvage or continue-after-any-error rule. |
| MDVx | Its embedded reader rejects many structural, required-reference and accessed-value failures, and cleans up temporary objects. Unknown sets/items can be skipped. Its app wrapper ultimately returns whether the file opened, even after metadata parsing failed; the caller proceeds without gating on that return. | Reuse the selective reading/cleanup idea. Report actual parse availability; never treat file-open success or initialized zero IDs as valid metadata. |
| OldScanner | It checks many bounds and typed values, retains objects parsed before framing failure, then interprets/projects them. Projection is not globally gated on a Complete outcome. It also checks many duplicate/ambiguous owner relationships and uses complete-set checks for absence/defaults. | Preserve useful validation rules in new helpers, but establish the selected region's framing before publishing. Avoid both accepting an unsafe prefix and suppressing unrelated fields merely because another value failed decoding. |

MDVx also has bounded static validation gaps: a BER indefinite-form byte can
become zero length; its Primer reader lacks an explicit stride check in the
reviewed function; one InstanceUID path copies 16 bytes without an explicit
width gate; a successful seek does not prove skipped bytes exist. These are
reasons not to copy the parser blindly, rather than demonstrated crashes.

## Agreed Bella boundaries

First check the complete framing of the selected metadata header region and the
mapping/ownership foundations needed for the requested facts. This means safe
file and enclosing ranges, checked length/count arithmetic, supported local
coding and usable Primer mappings. It does not require decoding every optional
value or reading picture/sound payloads.

| Damage scope | Example | Bella response |
| --- | --- | --- |
| One safely bounded value | Invalid Unicode, wrong numeric width, invalid rate fraction | Withhold that value and calculations depending on it; retain independently usable fields. Apply an already agreed alternate-value rule only where one exists. |
| One object or relationship | Unusable package ID, ambiguous master, unresolved required descriptor link | Withhold facts depending on that identity/association. Keep independently qualified branches. Never select an arbitrary first/last candidate to manufacture certainty. |
| Shared header structure | Impossible KLV/local-item boundary, unusable Primer framing, inability to enumerate relevant ownership claims safely | Use none of that region's metadata. Keep the physical row and independently trustworthy PMR/MDB/filesystem facts. Do not publish a structural prefix that could conceal conflicting later claims. |

A damaged mapping need only invalidate facts using that mapping when the rest of
the framing and independence can still be established. A broken global mapping
or enclosing boundary is a wider failure. Duplicate or conflicting singular
claims must not silently become first/last-wins values.

Structural completeness and successful decoding are separate: a fully bounded
bad optional value need not prevent proving that a different property is absent.
Conversely, a readable reference prefix does not prove its containing set was
fully examined. This distinction can live in small parser helpers; it does not
require retaining OldScanner's raw evidence graph.

## Unknown and missing information

MXF extensions and references outside the file are legitimate format features.
Unfamiliar metadata or an unresolved reference therefore does not by itself mean
the whole file is damaged. The specification discusses both dark metadata and
external references. [SMPTE ST 377-1, sections 6.3 and 6.8](https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf).

Skip an unneeded unknown set/value only after establishing its outer boundary.
If a needed type/layout is unsupported, leave the dependent fact unavailable.
An unresolved required owner prevents using its metadata; an irrelevant or
legitimately external reference need not invalidate unrelated results. Repeated
headers or valid array repetitions are not automatically conflicting identities;
their context matters.

Keep missing, damaged, unsupported and conflicting states distinguishable in
compact internal results. Existing display rules remain authoritative:

- Known zero Duration displays `00:00:00:00`, or `00;00;00;00` for DF.
  Damaged required timing stays blank.
  Genuinely missing applicable drop-frame information uses NDF; damaged required
  numbering information leaves Duration blank. DF uses semicolons.
- Unknown Kind uses `unknown`; unavailable Type and Precompute Category stay blank.
- Effect/Effect Category retain their agreed Precompute-only `unknown` behavior,
  and usable custom Effect text survives an unknown category.
- Do not invent zero IDs, zero rates, guessed text or new fallback sources.

## Scan and resource handling

An expected metadata parsing failure affects that file's metadata, then scanning
can continue with other files. Cancellation, I/O failure, a changing source,
unsupported layout and resource exhaustion remain different outcomes. Do not
hide out-of-memory or programming failures under a blanket corrupt-file catch.
Cancellation must not publish a half-finished job as successful.

Do not require a whole-file Complete outcome to use an independently validated
selected header. Later essence/index/footer trouble does not automatically erase
that header; neither does a readable header certify playable media. The exact
supported layouts, open/in-progress forms and header navigation/recovery remain
the next review. These rules add no footer hunt, byte resynchronization,
repair, rewrite or extra source read.

Retain compact selected values, availability/reason and a useful scan summary.
Release temporary header buffers/lookups after extraction. This is a design
boundary, not a measured speed or memory improvement.

## Verification boundaries

No damaged media, application, private API, build or test was executed for this
review. Existing reader/projection tests were inspected as source. They cover
truncation, invalid lengths/typed values, partial results, ambiguous relationships
and cancellation; they are useful starting cases, not proof Bella passes them.

Current static review also found a decoder/projection mismatch worth carrying
into engineering verification: schema Boolean delegates to Uint8 decoding,
while DropFrame projection accepts only QVariant Bool. A graph test inserting
literal `true` does not exercise that wire-decoding path. No runtime reproduction
or change to the agreed Bella drop-frame policy is claimed.

The BBC reader provides a separate public example of exact header/local-set
accounting and unknown-item skipping. Its current source is not the installed
MDVx version and does not prove every parser path validates every type.
[BBC libMXF header metadata reader](https://raw.githubusercontent.com/bbc/libMXF/main/mxf/mxf_header_metadata.c).
