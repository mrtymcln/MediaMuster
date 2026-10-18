# PMR legacy text encoding

18 October 2026. **MacRoman is established for the observed Mac-created project
name.** The user confirmed that the character stored as byte `a7` was typed as
`ß`. Apple defines exactly that MacRoman mapping. Media Composer's native codec
selection and Avid's historical naming documentation explain why this is expected.

The broader statement that every legacy PMR uses MacRoman is not supported by the
current binary or the locale history. The inspected Mac conversion paths query
the system encoding, which is MacRoman on this computer, and can preserve older
legacy project bytes unchanged. No production scanner behavior changed during
this research.

## The observed project is MacRoman

Apple's original [Mac OS Roman mapping](https://www.unicode.org/Public/MAPPINGS/VENDORS/APPLE/ROMAN.TXT)
defines `0xa7 → U+00DF`, Latin small letter sharp s, `ß`. Combined with the user's
confirmation of the typed character and Mac origin, this establishes the intended
decoding for the observed project pattern. One example is
`zTeßt_1080i_59.94`; related project variants account for 361 logical records.

A standalone, read-only probe of Apple's public CoreFoundation API on this
computer returned:

| Check | Result |
| --- | --- |
| `CFStringGetSystemEncoding()` | `0` |
| Canonical name reported by CoreFoundation | `Western (Mac OS Roman)` |
| `kCFStringEncodingMacRoman` | `0` |
| Decode byte `a7` with that returned encoding | `U+00DF`, `ß` |

The probe ran on arm64 macOS 15.8.2. It did not execute Media Composer private APIs
or change language settings. It establishes this machine's public API result,
while the native trace establishes where Media Composer invokes that API.

## What Media Composer actually calls

The inspected application is Media Composer 26.8.0.58987. Ten AvidCore functions
were decompiled with Hopper and compared with arm64 instructions, including the
Project attribute converter and underlying encoding-selection functions.
Decompiled expressions are approximate; addresses and selector comparisons were
verified in disassembly.

For legacy **filenames**, `ReadPmrRec` calls
`CurrentMacMBCSEncodingToUnicodeString`. The helper supplies selector **5** to
`EncodedToUTF16`, which calls `CFStringGetSystemEncoding()` and passes its result
to `CFStringCreateWithBytes`. The corresponding filename writer uses the same
system-encoding selection in the reverse direction.

The library can explicitly select fixed codecs, but the current-Mac wrappers
choose a different branch:

| Internal selector | Encoding selection |
| --- | --- |
| 1 | Fixed MacRoman, CoreFoundation value `0` |
| 2 | Fixed Windows Latin-1/1252, `0x500` |
| 3 | Fixed UTF-8, `0x08000100` |
| 5, used by the traced current-Mac wrappers | Query the current system encoding |

The fixed table is at AvidCore `0x2f16e8`. The decisive read-side call is at
`0x12bb88`; reverse conversion calls are at `0x12bae8` and `0x12ba00`.
This establishes the actual codec-selection mechanism, beyond interpreting a
function's name.

Apple's [API documentation](https://developer.apple.com/documentation/corefoundation/cfstringgetsystemencoding())
says that the macOS default follows the user's preferred language. It distinguishes
this from application encoding and gives MacRoman and MacJapanese as examples of
Mac encodings. The full official documentation data was retrieved and inspected;
the public API is not a promise of fixed MacRoman on every Mac.

## Project has its own verified path

The Project conclusion does not rely only on filename conversion:

1. `ScanMXFFileToCache` obtains the master project's `_PJ` attribute, with
   `PROJNAME` fallback, through `CMGetProjectName` and `CoreAttrList::FindString`.
2. An already-legacy attribute, kind 2, is copied as bytes. A UTF-8 attribute,
   kind 4, is converted with `UTF8ToCurrentMacMBCSEncoding`.
3. That converter supplies selector 5 and therefore uses the system encoding.
4. The resulting bytes enter the file cache, then the PMR record. Both legacy
   and version-16 branches call ordinary `WriteString` for Project.
5. `WriteString` writes a 16-bit byte count and the bytes. It does not add a
   code-page identifier or convert the Project string to UTF-8.

Relevant addresses are `ScanMXFFileToCache` call `0x44e9dc`,
`CMGetProjectName` `0x282bd4`, `CoreAttrList::FindString` `0x6dd10`, its conversion
call `0x6dddc`, `UTF8ToEncoded` system query `0x12ba00`, and the two PMR Project
writes `0x451378` / `0x4513e4`. Virtual dispatch targets were checked against
their vtables. The reader also uses an ordinary counted byte-string read for
Project in both sets.

Consequently, version 16 provides a defined UTF-8 **filename** but does not make
Project UTF-8. Existing legacy attributes may retain an earlier producer's bytes
even when a newer Mac rebuilds a PMR.

## Reading a PMR from another system

The inspected `LoadPMR` path reads no original-system, language or code-page tag.
`ReadPmrRec` receives a stream, format version, directory string used in diagnostics
and output record; it receives no originating-encoding parameter. The byte-swap
flag describes numeric serialization, not the string's character set.

The legacy filename converter therefore uses the **receiving Mac's** system
encoding. When writer and reader share a convention, the result is compatible;
the converter does not detect the writer's encoding. The preferred usable v16
filename set supplies explicit UTF-8 and removes that particular dependency.
Different computers or platforms need not imply different encodings: Avid's
historical Roman convention also existed on Windows, as documented below.

For Project, the PMR reader at `0x4477f8–0x447814` reads counted bytes and does no
character conversion. The subsequent [Project display trace](project-display.md)
now connects these bytes through the cache and actual Media Tool chooser to Qt
text drawing. That chooser explicitly calls `CurrentMacMBCSEncodingToUTF16`,
using selector 5 and the receiving Mac's system codec. Its legacy dialog setter
then performs an additional current-Mac encoding round-trip before storing the
UTF-16 list entry. The separate bin Project column resolves composition/source
attributes and uses the same final decoder. These paths settle the earlier UI
gap without treating every displayed Project as a direct PMR value.

Bella can reproduce the verified Mac filename and Project decoding selections
without heuristic codec trials.
That reproduces local Media Composer behavior, not a guarantee of the original
meaning of all foreign legacy bytes. Known producer information, such as the
user-confirmed Roman project, can establish a codec. With no tag, agreed convention
or independent source information, the original encoding is not recoverable with
certainty from ambiguous bytes alone. No automatic foreign-encoding correction
was identified in this PMR loading path.

## What Avid publicly documents

[Media Composer and Film Composer 11.7 release notes](https://resources.avid.com/SupportFiles/attach/MC11_7RN.pdf),
printed page 35, explicitly identify MacRoman for Macintosh Avid bin, project,
tape and other element names. This is strong historical support for the Roman
naming convention; it is not a complete PMR wire-format specification.

The [MediaNetwork File Manager Setup Guide](https://resources.avid.com/SupportFiles/attach/File_Manager_SUG.pdf),
June 2005, printed pages 83–84, supplies a documented exception. Older European
Avid filename conventions used MacRoman on both Mac and Windows. Asian-language
clients with language kits used language-specific encodings, such as ANSI
Japanese. The same guide describes later Unicode storage and platform conversion.
Its scope is filesystem names and migration, rather than the PMR Project field.

[MediaManager 3.7 release notes](https://resources.avid.com/SupportFiles/attach/MM_RNv3_7web.pdf),
printed pages 15–17, describe MacRoman/ANSI filename conversion and rebuilding
PMR/MDB afterwards. Statements there about OS X UTF-8 filenames do not override
the independently traced Project byte-string behavior.

The search covered public Avid manuals, knowledge-base material, developer/source
leads, international-character and language-kit documentation, and public PMR
readers. No public PMR field specification declaring one universal legacy codec
was found. Private support knowledge bases and every internet resource cannot be
certified by that search. The supplied Python reader uses an ISO-8859-5 choice
for XML export; it does not establish the PMR format's codec. Public C++ snippets
that copy raw bytes supply no codec definition either.

## Consequences for Bella

The observed Project should be decoded as **MacRoman**. Its value is established,
rather than an arbitrary fallback guess. A known Roman source also should not be
decoded as UTF-8 merely because a byte sequence happens to pass UTF-8 validation;
different codecs can assign different characters to the same valid bytes.

The current MediaMuster PMR path recognizes ASCII and infers valid UTF-8, but does
not enable MacRoman decoding. It therefore cannot obtain this Project from its
`a7` bytes, although another source may supply a displayed name. That is a
compatibility gap for this confirmed source, not absent Project information.

The user subsequently approved Bella's explicit rule: prefer a usable version-16
set, decode its Filename as UTF-8 and Project as MacRoman, and use the older set
only when version 16 is missing or unusable. Older-set Filename and stored Project
use MacRoman. Windows producer behavior is deferred. The current requirements
are recorded in [Project Bella](../../../Project%20Bella/README.md#agreed-pmr-reading-rules).
This is a product decision; it does not make MacRoman a universal property of
every historical PMR. No reader, setting or fallback implementation has changed.

## Retained evidence

[Curated evidence](../../../Project%20Bella/evidence/pmr-text-encoding-2026-10-18.json)
contains user-confirmed provenance, binary identities, exact instruction chains,
the public API probe result, decompilation hashes and primary-source receipts.
Raw decompiled code, probes and downloaded source remain under
`/private/tmp/bella-pmr-encoding-20261018-{binary,hopper,docs,public}/`; those
temporary files may be cleaned by the operating system.
