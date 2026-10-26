# Bella: MDB text encoding

Research date: 18 October 2026. This follows the [MDB overview](REVIEW.md).
The user approved the Bella text rule below on 18 October 2026, explicitly
including deliberate recovery through the separate legacy value when Unicode is
damaged. It is a requirement, not implemented behaviour. OldScanner and Bin
Filter are unchanged.

## Answer

Bella can prefer an explicitly identified, usable UTF-8 representation of a
field and then use its separately recorded legacy representation as MacRoman
within the current Mac-focused scope. This preference is per field and owner.
It does not mean trying UTF-8 and then MacRoman on the same bytes.

There are two qualifications. Some fields have two stored representations;
others have only one value whose recorded type tells us how to read it. Also,
Media Composer's traced readers of paired named UTF-8 properties raise an error
for invalid UTF-8 rather than recovering the legacy value. Recovery from damaged
Unicode would be Bella's chosen behaviour.

## What the installed Media Composer does

The read and write paths were reconstructed from arm64 disassembly and native
symbols in the installed Media Composer **26.8.0.58987**. Both
`libameLibrary.dylib` and `AvidCore` hashes match the earlier MDB review.
This was static analysis, with no Media Composer launch or database writes.
It establishes the traced functions' behaviour, not every historical producer,
every MDB field or a live application error dialog.

| Field | How Avid writes it | How the traced native reader selects it |
| --- | --- | --- |
| Original Bin name | Separate `binName` legacy and `binNameUTF8` properties from the original Unicode name. | Uses nonempty Unicode; if absent or empty, converts the legacy name through the current Mac codec. Invalid Unicode raises an error before that fallback. |
| File/directory locator path | Separate legacy `PathName` and Unicode `PathNameUTF8`. Legacy and Unicode can also use different path syntax. | Prefers nonempty Unicode; otherwise reconstructs the older path. Invalid Unicode raises an error. File locators also have separate POSIX/UNC representations. |
| Last-known volume | Separate legacy and UTF-8 properties; the writer omits the Unicode extension when empty. | Loads legacy first, then replaces it when the Unicode extension exists. This reader does not check for empty Unicode before replacing it. |
| Project | The traced `CMSetProjectName` writes `_PJ` through the legacy string route. The attribute system can store a named text attribute as legacy Kind 2 or marked Unicode Kind 4. | Reads the one attribute according to its kind. `CMGetProjectName` checks `_PJ`, then `PROJNAME` only if `_PJ` was not found. An existing empty `_PJ` blocks that fallback. |
| Clip Name | The traced component-name setter converts Unicode to current-Mac bytes; component serialization stores the legacy name. | `GetNameCopyUTF16` converts that legacy name through the current Mac codec. No paired `CPNT:NameUTF8` was established. |

The native attribute collection keeps one entry per attribute name. Its normal
string writers remove an existing text entry before inserting the new one.
Consequently, Kind 2 and Kind 4 are normally **alternative forms of one
attribute**, rather than two backup copies. No same-name Kind-2/Kind-4 pair was
found in an attribute list in the examined databases or the committed fixture.

A Unicode attribute called `Name` inside `PreservedBinData` is on a different
owner path. It is not an established UTF-8 backup for the master's Clip Name.

### Writing can lose information in the older copy

For the paired fields, Avid converts the original Unicode to the current Mac
codec for the legacy copy and separately writes the original as UTF-8. The
legacy converter calls Core Foundation with `?` as its substitution character
for text the older codec cannot represent. Bin/volume writers do not request
the loss flag; the path writers request it and log loss but still write the
converted copy. A healthy MDB can therefore have a less complete legacy value.

The native codec selector is `CFStringGetSystemEncoding()`. The earlier
[standalone Core Foundation probe](../../../Project%20Bella/evidence/pmr-project-display-2026-10-18.json)
returned encoding 0, Western (Mac OS Roman), on this Mac. This corroborates the
MacRoman scope and the actual `ß` Project bytes. It is not proof that every
historical or Windows writer uses MacRoman, or a measurement of any application
process-specific override.

### The stored type identifies Unicode

Named properties such as `binNameUTF8` contain ordinary UTF-8 followed by a NUL
terminator. The native UTF-8 writer adds no two-byte marker. These are not PMR's
counted strings.

Known Unicode text attributes use Kind 4 binary storage containing **two zero
marker bytes, UTF-8 text, then NUL**. The native Unicode-only attribute reader
checks both marker bytes. Kind 4 also stores arbitrary binary information, so
neither Kind 4 alone nor a familiar-looking attribute name is enough to declare
an arbitrary value UTF-8 text.

Kind 2 and ordinary legacy `omfi:String` data do not acquire a UTF-8 encoding
merely because their bytes happen to be valid UTF-8. Numeric byte order is also
separate from text encoding.

The attribute helpers do not uniformly perform strict UTF-8 validation. The
marker-copy helper checks framing and size; a Unicode attribute conversion route
uses ICU `UnicodeString::fromUTF8` without the explicit conversion-status check
established for the paired named properties. Project Kind 4 must not inherit a
claim about the named-property reader's exact error behaviour.

### Damaged Unicode is a distinct case

`ReadUTF8StringAndConvertToUTF16` calls the ICU-backed `UTF8ToUTF16` converter.
Conversion failure constructs `STRM_UNICODE_CONVERSION_ERROR` and invokes
`operator+(XError const&)`, which allocates and throws a C++ exception. The
traced paired-field readers do not catch that error and retry the legacy field
locally. They have missing/empty-value fallbacks, not a universal corruption
recovery rule. A buffer-capacity error can also reach this path; Bella should
not copy a native fixed buffer size as a universal on-disk text limit.

## What the actual files confirm

The six live MDBs from the earlier review were examined by their independent
raw indexes. The counts below are property-owning objects, not physical files.

| Stored pair | Objects | Observation |
| --- | ---: | --- |
| Bin name | 1,029 | Both properties exist at different ranges; all text is ASCII in this live sample. |
| File locator path | 3,838 | All have nonempty valid UTF-8. The legacy value is empty in 3,603 and has different path syntax in the other 235. |
| Last-known volume | 4,077 | Both properties exist at different ranges, with matching ASCII text. Another 20 objects have no Unicode value and blank legacy text. |

No live named UTF-8 value was empty, invalid, unterminated or followed by nonzero
bytes after its first NUL. These files cannot demonstrate the application's
response to such damage; that conclusion comes from the native trace.

All 4,405 `_PJ` attributes in the live files are Kind 2. The 480 non-ASCII legacy
values contain the known MacRoman Project spelling. Other attributes, including
`Name` and `Comments`, use both Kind 2 and Kind 4 on different owners.

The existing committed fixture
[`msmMMOB_macroman.mdb`](../../../tests/fixtures/msmMMOB_macroman.mdb)
provides a non-ASCII pair on bin-reference object 71271:

| Representation | File offset | Bytes including NUL | Decoded text |
| --- | ---: | --- | --- |
| Legacy | 73,146 | `43 61 66 8e 20 74 91 73 74 00` | Café tëst |
| UTF-8 | 73,156 | `43 61 66 c3 a9 20 74 c3 ab 73 74 00` | Café tëst |

These are separate stored values with different byte lengths. The fixture is
distinct from the six live MDBs. Its tracked bytes were unchanged.

Path handling needs one further distinction: for 213 nonempty legacy paths the
Unicode value starts with `/Volumes/` and the legacy one does not; another 22
include a boot-volume prefix in the legacy representation. Bella must interpret
the recorded path form as well as its codec. The two decoded strings need not
be identical for both values to be healthy.

## Agreed Bella rule

1. Identify the field and its correct owner, then the representations that its
   schema actually provides. Do not borrow a similarly named field from another
   object or try codecs until a string looks plausible.
2. Prefer its explicitly identified UTF-8 representation when the complete value
   is readable, has its expected text layout, and passes strict UTF-8 validation.
   A blank value supplies no useful text for a nonempty result field.
3. If that representation is absent or unusable, use the **separate, structurally
   readable legacy counterpart**, if one exists, decoded as MacRoman within the
   agreed Mac-focused scope. Legacy-only text is decoded directly as MacRoman.
4. If neither supplies usable text, leave that result unavailable. Keep other
   readable metadata and the physical media row. Never retry a damaged Unicode
   payload as MacRoman or invent a missing legacy counterpart.

For typed attributes, read the form actually stored. Project therefore does
not normally offer two copies to choose between. The `_PJ`/`PROJNAME` preference
is a separate choice between attribute keys, not a choice of text encoding.
The native absent-only fallback is established; extending it to empty/damaged
`_PJ` was not approved by this encoding review. The user separately approved
that extension and the owner/key rules in the [Project follow-up](project.md)
on 19 October 2026.

“Usable” detects invalid structure and invalid declared Unicode. It cannot
prove that a validly encoded name is the intended name, nor recover characters
already replaced by `?` in a legacy copy. Exact historical terminator acceptance,
structural MDB damage handling, source priority and extra media-header reads
remain separate requirements. PMR's approved whole-set fallback is unchanged.

## Native evidence anchors

Addresses refer to the arm64 slice of the hashed installed binaries. Full hashes,
selected call sites, actual-file observations and temporary-artifact receipts are
in the [curated text evidence](../../../Project%20Bella/evidence/mdb-text-encoding-2026-10-18.json).
Raw disassembly and probes remain outside the repository.

| Binary | Function | Address |
| --- | --- | --- |
| libame | `AMCBinRef::Put` / `Get` | `0x2d5990` / `0x2d5b48` |
| libame | `AMacDirLocator::Put` / `Get` | `0xa0618` / `0xa09b0` |
| libame | `AMacFileLocator::Put` / `Get` | `0xa1814` / `0xa1d40` |
| libame | `MSMLocator::Put` / `Get` | `0x3cae94` / `0x3cb1d0` |
| libame | `WriteString` / `WriteUTF8String` / `WriteUTF16AsUTF8String` | `0xb5d34` / `0xb62f4` / `0xb6300` |
| libame | `ReadUTF8StringAndConvertToUTF16` | `0xb8168` |
| libame | `CMGetProjectName` / `CMSetProjectName` | `0x282b9c` / `0x282c44` |
| libame | `AComponent::GetNameCopyUTF16` / `SetNameCopyUTF16` | `0x14943c` / `0x149524` |
| AvidCore | `CoreAttrList::AddUTF8StringCompatible` / `FindString` / `FindUTF8StringOnly` | `0x6d718` / `0x6dd10` / `0x6e0fc` |
| AvidCore | `CFStringRefToEncoded` / `UTF8ToUTF16` | `0x12b7e8` / `0x12ca0c` |
| AvidCore | `operator+(XError const&)` | `0xc840`, `__cxa_throw` call at `0xc9bc` |
