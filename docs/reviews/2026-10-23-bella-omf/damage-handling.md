# Bella OMF damage handling

25 October 2026. **The user approved these OMF damage boundaries.**
The [approval record](../../../Project%20Bella/evidence/omf-damage-handling-plan-2026-10-25.json)
preserves the direct instruction and unchanged research snapshot.
The user approved [Effect](effect.md); this review settles the proposed general
OMF failure boundaries. No runtime implementation/cutover or Bin Filter change.
The agreed OMF1/Bento 1 release scope, field rules and independent physical
inventory remain authoritative.

Keep the physical file listed. If one safely isolated value is damaged, withhold
that value and its dependent answers. If the OMF container's shared structure
cannot be trusted, use none of that container's metadata. Neither case requires
repairing the file or abandoning the scan of other files.

## What the readers do

These are bounded static findings, not a malformed-input experiment or a
guarantee about every import, live GUI error or possible crash.

| Reader | Verified behaviour | Useful Bella lesson |
| --- | --- | --- |
| Media Composer | Legacy container paths reject bad labels and corrupt TOC/index structure. Bad value-pointer reads can raise a corrupt-OMFI error; required-object restoration can clean up and rethrow. Some typed getters instead return zero/null for absent or short values, and the reviewed exact-rate reader does not require all eight requested bytes. | Retain structural and typed validation, but distinguish missing/error from a genuine zero and require complete supported values. No universal skip-one-record, automatic repair or catch-every-failure policy is established. |
| MDVx | Generic OMFI opening has false/nil exits; inaccessible strings and missing lookup hits can leave fields unavailable or skip enrichment. Reviewed range checking is uneven, with gaps in TOC bounds, iteration, array/scalar shapes and a pointer-access boundary. | Selective extraction and cleanup are useful. Its validation and open-success result are insufficient as Bella's metadata-validity rule. Ordinary standalone physical OMF fallback remains unestablished. |
| Current MediaMuster | Checks ranges, label/version, continuations, typed values, dictionaries, roots and relationships; preserves independent fields/branches on many local failures. It retains partial source objects and does not expose a separate complete-TOC gate at projection. | Reuse verified checks and useful cases in small new helpers. Separate structural completeness from successful decoding of every optional value. Do not carry over the permanent raw-property/evidence machinery. |

Current OMF's normal metadata-only route processes the TOC before reading
deferred metadata. A TOC exception therefore does **not** automatically imply
that a usable decoded metadata prefix was published. The source preserves
partial data and projection can consume incomplete sources; a fresh corrupt-OMF
prefix publication or crash has not been demonstrated. Some absence/default
checks also depend on the broad Complete outcome, so an unrelated value failure
can prevent an otherwise independent absence/default conclusion.

Native catch/log/continue paths do not prove every parser call or hardware fault
is protected. Public Avid troubleshooting describes isolating corrupt media as
an operational recovery workflow; it does not specify these internal parser
boundaries. [Avid corrupt-media guidance](https://avidtech.my.salesforce-sites.com/pkb/articles/en_US/troubleshooting/en268209).
No recovery workflow was performed.

## Agreed Bella boundaries

First establish the complete framing of the supported container's TOC and the
lookup/membership foundations needed to identify requested values and owners.
For Bento 1, validate its fixed-entry shape, supported flags and continuations,
label/TOC bounds, checked ranges/count arithmetic and file-local definitions.
This uses the already-agreed one temporary TOC read. It does not mean loading
or decoding every property payload, reading essence or reading the file twice.

| Damage scope | Example | Bella response |
| --- | --- | --- |
| One safely bounded value | Invalid identified UTF-8, wrong numeric width, unusable fraction, bad independently bounded payload/continuation | Withhold that value and calculations requiring it. Preserve independently usable fields. Use only an already-agreed separately stored fallback. |
| One object or relationship | Unusable own ID, conflicting singular claim, unresolved required descriptor or master/bin link | Withhold facts depending on that identity/association. Keep independently qualified branches. Do not choose an arbitrary first/last candidate or unrelated record. |
| Shared OMF container structure | Bad label, incomplete/unsafe TOC framing, unusable required global ownership/membership foundation | Ignore metadata from that OMF container. Keep the physical row and independent eligible database/filesystem/native-audio facts. Continue other files. Do not salvage an unsafe structural prefix. |

The failure scope follows the dependency. A damaged definition needed by one
property need not erase unrelated safely named properties. Unusable global
revision/order/root information may prevent the affected numerical decoding or
ownership more widely. A complete TOC with a bounded bad payload is different
from one whose entries/context cannot be enumerated safely.

Conflicting singular identities/values are unavailable, rather than first/last
wins. Identical repeated claims, legitimate ordered arrays and declared
continuation segments are not automatically corruption. Preserve their format
semantics. Unknown optional properties/classes can be skipped when their framing
is safe; a needed unsupported form leaves dependent metadata unavailable.

## Missing information and independent sources

Missing, damaged, unsupported and conflicting data remain distinguishable in
compact internal results. Proving absence requires a complete relevant lookup;
a failed lookup cannot justify an absence-based default or No Reference.
No need for every unrelated optional payload to decode successfully.

Existing field-specific decisions stay intact:

- Use identified UTF-8/marked Unicode and legacy MacRoman as agreed. A real
  separate legacy counterpart can recover its field; never reinterpret damaged
  Unicode bytes as MacRoman or invent a counterpart.
- Known zero Duration is zero; unavailable required timing stays blank.
  Genuinely absent applicable drop-frame information uses NDF; damaged or
  ambiguous required numbering data does not silently become NDF. Preserve
  semicolon DF formatting and exact internal timing.
- Kind can remain known from a valid own descriptor class despite an unrelated
  bad rate/depth. Unknown Kind displays `unknown`; unavailable Type/Precompute
  Category are blank. Effect/Effect Category keep their agreed Precompute-only
  rules and do not establish Type themselves.
- In WAV/AIFF files with embedded OMF, reject only the failed OMF contribution
  when native audio facts are independently trustworthy and already available.
  This does not authorize a new fallback read or decide final source priorities.
- A short WAVD/AIFD descriptor summary can deliberately omit samples. Validate
  it as the supported summary, not as a damaged complete recording. A readable
  metadata header likewise does not certify playable media or intact essence.

Do not scan picture/audio payloads to certify a header, guess a replacement
owner or byte-resynchronize after bad framing. No automatic rewrite, deletion,
database rebuild or repair during scanning.

## Small implementation and reporting

Keep bounds/exact typed reads in shared small C++17/Qt 6.5.3 helpers. Validate
offset/count arithmetic and representability before indexing or allocating;
read only requested metadata and necessary linkage. Use scoped file/buffer
ownership and release temporary bytes/indexes when the job ends. A compact
container status, value availability and useful diagnostic summary suffice;
no new recovery framework or permanent graph/history of rejected bytes.

Expected format failures affect that source's contribution and scanning can
continue. I/O failure, source changes detected during acquisition, cancellation,
unsupported scope and resource exhaustion report their actual reasons. Do not
hide out-of-memory or programming errors under a blanket corrupt-file catch;
do not publish a cancelled job as successful. A length check alone cannot prove
the source was unchanged in place. Final capture/scheduling/retry policy remains
separate; this review adds no automatic retry loop or new read trigger.

## Evidence limits

The [curated record](../../../Project%20Bella/evidence/omf-damage-handling-2026-10-25.json)
contains native/current receipts, hashes, source anchors and useful inspected
test cases. Tests are evidence of intended cases, not new executions here;
some broadly named projection tests actually use MDB and must stay labelled.
The saved genuine OMF examples do not supply a malformed-file corpus.

No media was damaged, repaired or rewritten; no native app/private API,
production parser, test suite or build was executed. No Bella runtime exists
to establish a performance/reliability improvement. Plausible but wrong
metadata can pass structural checks, so no 100% semantic-accuracy claim is made.
