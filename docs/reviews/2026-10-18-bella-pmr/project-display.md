# Media Composer's Project-name display path

18 October 2026. Read-only analysis of the installed **Media Composer
26.8.0.58987, arm64**. The PMR-derived Media Tool project chooser explicitly
decodes Project bytes using **the receiving Mac's current system encoding**.
There is no UTF-8-first test or originating-code-page lookup in this path.

The earlier [encoding report](text-encoding.md) stopped before the UI consumer.
This follow-up connects the PMR record, cache, concrete dialog, list entry and Qt
text drawing. It also traces the separate bin/table Project column. The user's
subsequently approved Bella policy is recorded in
[Project Bella](../../../Project%20Bella/README.md#agreed-pmr-reading-rules);
production code has not changed.

## What the decoder does

Both displays call `CurrentMacMBCSEncodingToUTF16`. Its implementation in AvidCore
at `0x12c7f4` supplies internal encoding selector **5** at `0x12c820` to
`EncodedToUTF16`, `0x12bb3c`. That branch calls
`CFStringGetSystemEncoding()` at `0x12bb88`, passes the returned codec to
`CFStringCreateWithBytes` at `0x12bba0`, then obtains Unicode with
`CFStringGetCharacters` at `0x12bbe0`.

This is an explicit decoding instruction. The helper does not inspect whether
the bytes form valid UTF-8, try several character sets or receive the PMR's
originating platform. The library has fixed MacRoman and UTF-8 selectors, but
these Project display callers choose the **system** selector.

The previously retained standalone public CoreFoundation probe on this computer
returned encoding **0**, `Western (Mac OS Roman)`, and decoded byte `a7` as
**U+00DF, ß**. This agrees with the user's confirmed Mac-created project name.
The probe was outside Media Composer; it is distinct from observing the API's
return value inside a running Media Composer process. The binary trace establishes
the selection rule regardless of that distinction.

## The PMR-derived Media Tool chooser

Addresses below are unslid virtual addresses in the named arm64 image. Main
executable addresses retain its `0x100000000` image base; library addresses use
their respective image bases. Vtable offsets are relative to each object's
primary address point.

| Stage | Verified operation | Location |
| --- | --- | --- |
| Read the record | `ReadPmrRec` reads the counted Project bytes unchanged into `record + 0x240`, for stored Project versions | libame `0x4477f8–0x447814` |
| Load the chosen set | `LoadPMR` passes that Project buffer as argument `x5` to manager virtual `+0x328`, `AddCacheEntry` | libame `0x445334`, call `0x445348` |
| Cache the name | `AddCacheEntry` copies nonempty Project bytes, deduplicates them with exact `strcmp`, and stores an index into `_gProjectList` | libame `0x452f60`; global `0x1511910` |
| Enumerate names | `GetKnownProjects` copies the pointer array and sorts with bytewise case-insensitive comparison; it does not decode the strings | libame `0x452f0c`, manager virtual `+0x190` |
| Obtain the list | `MToolSetDisplayable` calls that manager method and stores its result in `ProjList` | main `0x100e36654–0x100e3666c`; global `0x10478dc60` |
| Decode each label | `MediaSelectionDlg::IMediaSelectionDlg` fetches each byte string and calls `CurrentMacMBCSEncodingToUTF16`, using a 256-unit output buffer | main `0x100e376fc–0x100e3771c` |
| Set the named control | It passes the UTF-16 buffer and literal `Projects_ListBox` through dialog virtual `+0x508` | main `0x100e37738–0x100e3774c` |

The last virtual target was resolved from the **actual MediaSelectionDlg
constructor and vtable**, rather than inferred from a similar method name:

- Constructor `0x100e3a56c` installs primary address point `0x1045a5fe8`.
- Address point `+0x508` is `0x1045a64f0`.
- Its Mach-O bind names `DialogWindow::SetTextValUTF16`, libUIshell `0x74728`.
- Address point `+0x4f8`, `0x1045a64e0`, binds `DialogWindow::SetTextVal`,
  libUIshell `0x747bc`.

### The final control and drawing path

The control introduces an extra legacy round-trip. `SetTextValUTF16` calls
`UTF16ToCurrentMacMBCSEncodingString` at `0x74754`, then dispatches to
`SetTextVal` at `0x74778`. Thus the function's UTF-16 name does not imply that
all downstream storage immediately remains Unicode.

This dialog's design initializer, main `0x100e3a634`, passes the literal
`Projects_ListBox` to **`DialogWindow::AddTextListPane` at `0x100e3a70c`**.
The dialog constructor calls that initializer at `0x100e3a5b4`. This identifies
the concrete list-control branch used by the setter. The factory in libUIshell,
`0x77c48`, directly calls `ATextListPane::New` at `0x77cd0`, assigns the requested
control name through pane virtual `+0x430`, and returns that pane.

In the `ATextListPane` branch, `SetTextVal` creates a narrow-string
`ATextListEntry::New` at call `0x74888`. That constructor, `0xd56b0`, converts
the bytes back to Unicode at `0xd56dc`, then calls `NewUTF16`, `0xd574c`.
The entry retains a copied UTF-16 handle at offset `+0x18`.
Pane virtual `+0x7f0` resolves to `ATextListPane::AddEntry`, `0xc6564`,
which delegates to `AListPane::AddEntry`.

`AListPane::DrawDetail`, `0xbcda0`, invokes entry virtual `+0x70` at `0xbcffc`;
the actual entry vtable resolves that slot to `ATextListEntry::DrawDetail`,
`0xd593c`. The entry passes its Unicode text to
`CDrawStringUTF16` at `0xd5a00`. In Mac2Qt, `DrawTextBufUTF16`, `0xe9a8`,
calls **`QString::fromUtf16` at `0xe9d4`**, then
`StdTextInternalUTF16`, `0x12f08`, calls **`QPainter::drawText` at `0x12fd4`**.
Font shaping and rasterization after this Unicode handoff do not determine how
the original PMR bytes were decoded.

The resulting text path is:

```text
PMR Project bytes
  → raw interned project cache
  → Media Tool's known-project list
  → current Mac system encoding → UTF-16
  → current Mac system encoding bytes, through the legacy dialog setter
  → UTF-16 list entry
  → Qt QString → QPainter text drawing
```

### Names which are not directly stored PMR values

Two substitutions matter when interpreting a screenshot or comparing a scanner:

- `AddCacheEntry` uses the current project when its incoming Project is null or
  empty. Manager virtual `+0x1e8` resolves to `GetCurProjectName`, libame
  `0x4443d0`; it copies the manager's existing byte string without recoding.
- `MToolSetDisplayable` also obtains that current project and can append it to
  `ProjList` when absent. The chooser therefore includes known media projects
  and potentially the current project.

Version 1 has no stored Project; its reader's separate fallback attempts
`FindProjectName` on existing objects. `_gProjectList` can also be populated during
media scanning. The trace proves how an accepted PMR Project reaches this chooser;
it does not claim every displayed label was read from a PMR during that session.

## The separate bin/table Project column

The column does not directly display `record.project` or call `GetKnownProjects`.
It resolves the loaded composition and its linked sources:

```text
ABinModel::data                  main 0x100d165dc
  → binGetTextUTF16              main 0x100e65150
  → MobGetTextUTF16              main 0x100e61014
  → AME_MobGetText               libame 0x243794
  → Project case, selector 204   libame 0x245ed8
  → FindProjectName              libame 0x29e974
  → current Mac bytes → UTF-16   call 0x245f2c
  → QString::fromUtf16           main 0x100d17388
  → QVariant for the Qt model    main 0x100d17394
```

Selector **204 / 0xCC** is an internal field identifier, not a visible column
position. The dispatch table entry at `0x10ecdee` targets `0x245ed8`. That case
leaves sequence Project values blank; otherwise it zeroes a 256-byte temporary
buffer, calls `FindProjectName(comp, 0, buffer, 256)` at `0x245f1c`, then calls
the current-Mac-to-UTF-16 decoder at `0x245f2c`.

`FindProjectName` first asks `CMGetProjectName` for `_PJ`, with `PROJNAME`
fallback. Next it asks `GetPropValue` for `_USER._PJ`, including traversal of
media/source tracks. If still absent, it follows source compositions and retries
`CMGetProjectName`. These are composition attributes; their provenance is not
exclusively MDB or PMR merely because the column appears in Media Tool.

The char-output attribute reader, `CoreAttrList::FindString`, copies legacy
kind-2 strings as bytes. For kind-4 UTF-8 attributes it first converts to current
Mac bytes. Consequently even a Unicode attribute can undergo a legacy encoding
round-trip on this column route. This establishes the implementation, not that
every possible international character survives it.

## What this settles for Bella

The missing Project display connection is established. Reproducing the inspected
Mac application's **decoding selection** does not require guessing the writer's
character set: select the current system codec, just as this code does. On the
probed Roman configuration, that decodes `a7` as `ß`.

That is a receiving-system convention, not an automatic recovery mechanism for
unknown foreign bytes. Nothing in this traced path discovers the originating
Windows code page or switches Project decoding because the PMR contains a
version-16 UTF-8 **filename** set. A copied PMR retains its original Project bytes.
The [earlier report](text-encoding.md) retains the relevant Avid locale history.

The user subsequently chose a fixed convention for Bella: use a usable version-16
set, decode its Filename as UTF-8 and Project as MacRoman, and fall back to the
older set only when version 16 is missing or unusable. Older-set text uses
MacRoman. Windows producer behavior is deferred. That approved rule is separate
from the native system-codec selection established here. No reader or scanner
implementation has changed. Bella does not need Media Composer's intermediate
legacy UI conversions to follow the agreed rule.

## Verification and retained evidence

Hopper decompilation covered the concrete main UI entry points, cache helpers,
actual dialog setters and list-entry constructor. Those expressions were checked
against arm64 instructions; register types and recovered pseudo-C are approximate.
The Project switch, bind records, vtables and CoreFoundation selector were checked
directly in disassembly. Existing encoding-helper decompilations were reused.

This is a static trace of the installed Mac build, with a separate public-API
codec probe. Media Composer's private functions were not executed, its UI was not
used to rescan media, language preferences were not changed, and no timing claim
or Windows-build decoding claim is made.

[Curated evidence](../../../Project%20Bella/evidence/pmr-project-display-2026-10-18.json)
retains the binary hashes/UUIDs, exact call and dispatch receipts, local probe and
decompilation hashes. Raw disassembly and recovered pseudo-C remain in temporary
folders under `/private/tmp/bella-pmr-project-display-20261018-*`; they are not
checked into the repository. The evidence identifies the installed binaries so
the relevant functions can be inspected again if those temporary files expire.
