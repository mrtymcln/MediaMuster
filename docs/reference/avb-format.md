# AVB object and dependency evidence

The independent [`AvbReader`](../../src/avb/avb_reader.cpp) and
[`AvbReferenceIndex`](../../src/avb/avb_references.cpp) read and resolve one
bin at a time. The active product contract is [Whole Bin filtering](avb-parser.md).
Exact sequence-use filtering is [deferred](../deferred/sequence/README.txt).

## Source-local facts

An AVB is an object/reference graph, not a list of physical media files. Native
object numbers are local to their bin. Full recorded MobIDs, object references
and historical numeric identity fields are different reference forms. Equal
names or MOB-looking text do not establish a dependency or physical-file match.

Supported grammars cover bin/component/track structures, media descriptors,
locators, attributes, parameter lists and selected effect/settings objects.
Individual occurrences, duplicate attribute names and verified modern/legacy
text counterparts are decoded independently. Unknown classes or extensions
qualify coverage; a known outer object does not justify guessing the next field's
width. Nonempty RGBA palette variants and insufficiently established `wchar`
encoding remain bounded limitations. Embedded WAVE/AIFF summaries retain their
own byte order independently of the surrounding bin.

After resolution the app retains compact file references and warnings, not the
complete graph. The reader/resolver cannot certify arbitrary timeline use simply
because it can decode starts, lengths, rates or effect fields.

## Native PCMA extension 02

The inspected Media Composer **26.8.0.58987 arm64** native implementation adds
this grammar to PCMA, beyond the consulted pyavb reader's extensions 1 and 3:

```text
01 02       extension marker and number
47          signed-32 property type marker (decimal 71)
<4 bytes>   subframe alignment, in bin scalar byte order
```

The following `03` terminates the descriptor, not the alignment value.
`PCMAudioDescriptor::Get` (`0x3e8b8c`, extension dispatch `0x3e9088`) reads and
sign-extends the word before `SetSubframeAlignment` (`0x3e90b4`). The paired
`Put` path (`0x3e86d4`, writes through `0x3e8ad8`) corroborates its width.
This establishes a field, not a new sample-rate conversion or display column.

## Terminals and dependency meanings

In the application's material-UUID-little-endian canonical representation:

| Full identity | Meaning |
| --- | --- |
| `060a2b340101010101010f00130000000000000000000000060e2b347f7f2a80` | Exact supported Avid null terminal. |
| `060a2b340101010101010f00130000000000000001000000060e2b347f7f2a80` | Exact supported Avid filler terminal. |

The reader also recognizes explicitly all-zero complete IDs. Native `IsNullMobID`
(`0x2353dc`) and `IsFillerMobID` (`0x235d94`) plus literal bytes at `0x10ec848`
and `0x10ec850` establish these meanings for the inspected build. Do not generalize
its broad zero-material predicate to arbitrary non-Avid IDs or treat every
well-known reserved identity as a terminal.

AudioSuite `ASPI` extension 1 is `SourceMasterClipOMFMobID`, extension 8 is
`SourceMasterClipAAFMobID`; extension 7 is retired. The native property name is
`OMFI:ASPI:sourceMasterClipMobID`. The extension-8 writer through `Write_AAFMobID`
at `0x7b1030` establishes a source-master dependency. It does not by itself
establish a physical file. `_MATCH`/`_SYNC` MCMR associations, position/marker
IDs and other arbitrary MobID properties do not inherit that dependency meaning.

Legacy SourceClip numeric words without a complete established identity route,
duplicate candidate MobIDs, broken links and unsupported path-only external media
remain explicit coverage limitations. No filename or path-only substitute is
invented for managed FileMobId matching. Every physical scanned copy matching an
established file identity retains its own inventory row.

## Attribution and scope

Layouts were independently checked against [pyavb](https://github.com/markreidvfx/pyavb),
particularly `essence.py`, `misc.py`, `attributes.py`, `components.py`,
`trackgroups.py` and `ioctx.py`. pyavb is an independent implementation, not an
Avid specification or endorsement. It is MIT licensed, copyright 2022 Mark Reid;
retain its required notice with copied/substantial source-derived material.
The [AAF object specification](https://aafassociation.org/html/specs/aafspec-v1.0.1.pdf),
sections 7.6–7.7, distinguishes explicit zero SourceID from omitted SourceID;
that does not define the meaning of an absent AVB extension.

Native addresses are unslid locations in the inspected `libameLibrary.dylib`
arm64 slice, SHA-256
`70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e`.
They establish bounded format evidence, not all historical variants or current
fixture-based acceptance. See the
[validation policy](../projects/Copland_readme.md#validation-and-change-policy).
