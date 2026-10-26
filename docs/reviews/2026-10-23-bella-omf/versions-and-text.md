# Bella OMF versions and text encoding

23 October 2026. **The user approved these release-one version and text rules.**
The [approval record](../../../Project%20Bella/evidence/omf-versions-and-text-plan-2026-10-23.json)
preserves the direct instruction and research snapshot. The TOC-driven reading
and compact-retention approach is already agreed. This review records the
release-one version range and text rules; it does not implement a parser or
change Bin Filter, source priorities or header-opening reasons.

For release one, support the evidenced ordinary Avid OMF media family: Bento 1.0
with known legacy OMF1 records. Decode explicitly identified UTF-8 as UTF-8 and
ordinary legacy strings as MacRoman within Bella's agreed Mac-focused scope.
An OMF version number does not select a character encoding.

## Two different version numbers

| Version | Meaning |
| --- | --- |
| Bento 1.0 or 2.0 | The outer container and table-of-contents representation. |
| OMF record revision 1 or 2 | Names, classes and reference layouts inside the container, established from typed HEAD information. |

Numeric byte order is a third independent fact. The OMF specification defines
HEAD version and `II`/`MM` numeric order separately; its basic String type does
not assign every character string a universal UTF-8 or MacRoman codec.
[Avid OMF specification, HEAD and Appendix B](https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf).

Do not infer the record revision from the Bento version, the file extension,
the Media Composer release, a 32-byte ID or the presence of Unicode. These
facts are not interchangeable. In particular, VersionType needs its established
typed interpretation rather than a generic two-byte integer swap.

## What the readers do

| Reader | Versions | Text |
| --- | --- | --- |
| Media Composer | Ordinary legacy-media scanning uses its Bento-1.0 domain. A separate implemented OMF2 import path uses Bento 2.0. The native audit did not establish one universal legacy HEAD-version rejection table. | Legacy component names use the shared current-Mac conversion machinery. Explicit Unicode attributes and named UTF-8/legacy bin-name siblings have their own formats. OMF2 name reading is not proof all its strings are UTF-8. |
| MDVx | Generic OMFI reading rejects label major 2, then uses a flat legacy TOC for other majors without a strict major/minor whitelist. No separate semantic HEAD-version admission rule was established there. Ordinary physical OMF-header inventory reading remains unestablished. | Its generic row helper chooses ASCII or MacRoman from a label-derived flag. A separate raw-browser branch recognizes one UTF-8 path property. It does not implement Bella's proposed per-field Unicode/separate-legacy recovery. |
| Current MediaMuster | Accepts Bento 1.0/2.0 and separately recognizes OMF1/OMF2. OMF1 includes first-byte-1 forms and a context-qualified legacy `00 01`; OMF2 requires `02 00`. The broad first-byte-1 check does not validate every minor version. | Explicit UTF-8 properties are validated; unlabelled non-ASCII text is later inferred as UTF-8 when valid, otherwise MacRoman. Its current OMF attribute projection handles ordinary string/object forms but has no marked Kind-4 Unicode text decoder. |

Current recognition preserves raw strings and stops decoding at the first NUL;
it does not require a terminator or reject trailing bytes there. It does not
trim or normalize spelling. These are source findings, not blanket approval of
every malformed string or an instruction to retain raw values permanently.

Media Composer's current-Mac conversion selects `CFStringGetSystemEncoding()`.
The earlier public Core Foundation probe on this Mac reported MacRoman, and
the known Mac-written project bytes corroborate it. This does not establish
the codec used by every historical or Windows producer. Numeric byte order
must not choose text encoding, as in MDVx's heuristic.

## Actual examples and their limits

The same-day independent observations cover two live Avid WAV/AIFF-C files with
embedded OMF and three Avid-supplied OMF slates. All five use Bento 1.0, legacy
OMF version bytes `00 01`, and `II` numeric metadata order. The two project
attributes contain `zTeßt_PAL_25p` in MacRoman, with `A7` for the deliberate `ß`.
Those bytes are not valid UTF-8. This is reanalysis of hashed captures, not a
new full scan or evidence of a Unicode sibling in those files.

A public toolkit `Complx2x.omf` was freshly fetched and independently indexed.
Its label records Bento 2.0; its typed HEAD records version `02 00` and `II`.
It contains composition, master, source and editing-component objects, so it
demonstrates a real OMF2 interchange example. It does not establish ordinary
Avid managed-media-folder requirements. Selected names were ASCII; it supplied
no additional Unicode-encoding proof. The entire small public fixture was
downloaded for inspection, not used as a metadata-only acquisition benchmark.
[Pinned toolkit blob](https://api.github.com/repos/LWKS-Software/omfkt22/git/blobs/10879df78a8d428572540e7e5c9fa5bd6f83e07c).

## Agreed release-one version baseline

1. Support Bento 1.0 with the known OMF1/Avid record layout. Recognize canonical
   `01 00` and the evidenced legacy `00 01` only in its supported legacy HEAD
   context. Do not swap arbitrary version bytes or accept every `01 xx` merely
   because OldScanner's broad test did.
2. Keep both recognized numeric metadata orders, `II` and `MM`, through checked
   typed helpers. The current Mac examples use `II`; actual opposite-order OMF
   media coverage remains an engineering verification requirement. Conflicting
   or unproved overrides do not authorize guessing.
3. Defer Bento 2/OMF2 and mixed-container/import variants for release one.
   Also retain the explicit unsupported boundaries for extended labels, the
   legacy 40-bit extension, update overlays and unknown layouts. Do not build
   a sequence/project importer merely to preserve generic-reader capability.
4. Report unsupported distinctly from damaged. Keep the physical row and
   independently eligible database/native-audio/filesystem facts when the OMF
   graph is unsupported. Retain research and useful cases for future support.

This deliberately narrows current MediaMuster's broader reader. The real OMF2
example and authored direct-OMF2 cases would remain outside Bella's first-release
OMF graph support; they are not redundant or invalid formats. The choice follows
the evidenced managed-media workload and simple first-release scope. No measured
speed or memory gain is attributed to dropping a decoder branch.

## Agreed text rules

Use the already approved MDB principle after identifying the OMF representation
and owner. Choose an available representation of the same field, not an encoding
that happens to accept its bytes.

| Recorded form | Bella interpretation |
| --- | --- |
| Explicit supported UTF-8 property, such as `binNameUTF8` | Strict, bounded UTF-8 decoding. A damaged value is unavailable. |
| Separately stored legacy counterpart | Use MacRoman if the usable Unicode value is missing, empty or damaged. Only use a real counterpart for that same field/owner. |
| Legacy-only component/mob name, Kind-2 string attribute or ordinary legacy text | MacRoman within the agreed Mac-focused scope. ASCII keeps the same characters. Do not try UTF-8 first on these unlabelled bytes. |
| Known Avid marked Unicode text attribute | Check the requested text field/name, established representation, Kind/type, two zero marker bytes, bounded UTF-8 and terminator. Kind 4 also carries arbitrary binary data, so Kind 4 alone does not identify Unicode text. |
| Unrecognized binary/private value | Do not turn it into text merely because it resembles UTF-8 or has a suggestive name. Leave the dependent value unavailable. |

The native Kind-2 and Kind-4 forms normally replace each other for one attribute
name; they are not an assumed backup pair. Some fields have no Unicode sibling.
No paired `CPNT:NameUTF8` was established. Never reinterpret damaged explicitly
UTF-8 bytes as MacRoman or invent an absent legacy value.

Deliberate recovery through a separate legacy value matches Bella's existing
MDB requirement. It is not an exact copy of Avid's paired reader, which can
raise a conversion error before that local fallback. Preserve decoded spelling,
whitespace and punctuation. Keep owned QString results and compact availability;
release temporary byte values with the rest of the OMF job.

This adds support for a known native Unicode representation missing from the
current OMF attribute projection. It does not require a general encoding detector,
runtime locale probing or a permanent raw-value history. It makes no universal
Windows or arbitrary third-party producer claim.

## Verification and next step

The [evidence record](../../../Project%20Bella/evidence/omf-versions-and-text-2026-10-23.json)
contains fresh installed-native/source receipts, the captured Mac project bytes,
the pinned external sample and explicit coverage limits. No application/private
API or production parser/build/test was executed. Tests were inspected only;
several broadly named projection tests default to MDB and are not OMF coverage.
New helper implementation and meaningful representation/order cases follow
the requirements review, with no app cutover here.

With the version/text range agreed, review complete FileMobId/MasterMobId
normalization and ownership, then focus remaining OMF work on actual differences
from already settled field rules. Source precedence and extra reads remain later.
