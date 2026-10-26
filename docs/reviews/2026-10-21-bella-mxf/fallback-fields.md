# Bella: Duration and Original Bin during MXF fallback

Agreed by the user on 21 October 2026. **Requirements revision; not yet
implemented.** This supersedes the earlier blanket MDB-only Duration restriction
for the specific fallback below.

| Field | Database-backed path | MXF fallback because usable PMR/MDB information is unavailable |
| --- | --- | --- |
| Duration | Prefer the file's usable own MDB Length/SampleRate and qualified display facts. | Use the same physical MXF's own duration and qualified timing/display facts. |
| Original Bin | Keep the agreed selected-master MDB bin-name rule. | Leave blank; no MXF bin-name search or guessed replacement. |

This is the scan's **database-unavailable MXF fallback**. It does not instruct
Bella to open a header solely for Duration, verify a usable MDB value, or repair
every isolated missing/damaged MDB field merely because a header was read. PMR
has no duration value; PMR absence alone does not displace usable MDB timing.
Exact header-opening conditions remain part of the deferred priority review.

Use one coherent, qualified physical-file timing source. Do not mix an MDB count
with a header rate/clock/drop-frame flag or choose by worker completion order.
The independent Frame Rate fallback rule remains unchanged: filling Frame Rate
alone does not qualify header Duration. A usable MDB Original Bin is not erased
simply because an MXF is independently opened; the database-unavailable fallback
itself supplies no bin name.

Keep exact arithmetic and audio sample precision, HH:MM:SS:FF, qualified recorded
drop-frame numbering, NDF for genuine applicable absence, blank for unusable
required timing, established-zero display and durations beyond 24 hours. Retain
the necessary MXF count/rate and supported clock/numbering facts on the eligible
fallback rather than discard them under the old MDB-only restriction.

The [focused MXF Duration review](duration.md) now investigates the physical
descriptor owner, count/rate pairing, audio display clock and applicable
drop-frame association. Its detailed rules were approved on 22 October 2026,
including DF semicolons/NDF colons. The source/display boundary in this
decision remains agreed.
Explicit MXF rationals keep their own meaning; the legacy MDB ExactEditRate
conversion is not applied indiscriminately to them. This approval does not
assert that every MXF timing layout is already verified.

The full source-priority table, MXF concurrency and unrelated header recovery
remain separate decisions. OldScanner stays active until explicit cutover
approval and Bin Filter is unchanged. This revision changes documentation only.

The [approval receipt](../../../Project%20Bella/evidence/mxf-fallback-fields-2026-10-21.json)
preserves the user's two instructions. Earlier research/approval receipts remain
historical snapshots; this later revision governs current Bella requirements.
