# Independent binary dispatch check

20 September 2026. Target: the parent's extracted arm64 `libameLibrary.dylib` from installed Media Composer 26.8.0.58987. Checked fresh Hopper pseudocode against ARM64 instructions and actual virtual-table entries. This verifies the relevant dispatch paths, not the complete application.

## Verified scanner dispatch

`MSMDBMediaStreamMgr::ScanDirectoryToCache` starts at `0x44c33c`. It calls `AMediaStreamMgr::GetMediaDirectoryDomainType(ADirLocator*, bool)` at `0x44c578`. Its second argument comes from the directory record's `isAMA()` virtual method, verified by its vtable slot (`+0xd0 -> 0x459788`). The first argument comes from `GetMetadataDir(bool)` (`+0xe0 -> 0x434c18`).

The domain result is saved once at `0x44c640` to `[sp+0x30]`. When a directory entry needs scanning, the code reloads that value as argument `w3` at `0x44c7f8` and invokes `ScanFileToCache` through virtual slot `+0x2c8`. It does not reclassify the whole directory from all database identities at this point.

`MSMDBMediaStreamMgr::ScanFileToCache` starts at `0x44cea0`. After skipping known non-media names, it branches on that supplied domain value:

| Domain value | Branch behavior | Verified vtable destination |
|---|---|---|
| 1 | OMF scanner | `+0x2d0 -> 0x44d3b8`, `ScanOMFFileToCache` |
| 3 | AAF scanner | `+0x2f0 -> 0x44f764`, `ScanAAFFileToCache` |
| 4 | If `IsMXFSupported()` succeeds, MXF scanner; otherwise fall through to supported AMA scanner | `+0x2d8 -> 0x44e184`, `ScanMXFFileToCache` |
| 5 | AMA scanner if `IsAMAFIDomainSupported()` succeeds | `+0x2e0 -> 0x44ef10`, `ScanAMAFIFileToCache` |
| 6 | Proxy scanner if `IsProxyDomainSupported()` succeeds | `+0x2e8 -> 0x44f3c4`, `ScanProxyFileToCache` |

Unmatched values retain the initialized false result in this dispatch function. In particular, although the separate domain-to-string table labels both values 1 and 2 “OMF,” this dispatcher has an OMF branch for value 1 only; do not infer another case from the labels.

The branch instructions are at `0x44d008–0x44d090`, followed by the selected virtual call at `0x44d0b0`. `dispatch-vtable-evidence.txt` records the stored chained-fixup pointer words and the matching image offsets/symbols; these resolve the decompiler's otherwise anonymous virtual calls.

## Directory context, not a date or ID-family test

`GetMediaDirectoryDomainType` (`0x4386ac`) examines the directory locator and supported media-directory information. Its fallback path iterates the 18-entry supported-directory table using `GetSupportedMediaDirInfo` (`0x433188`), compares against path text, and returns the matched record's domain field (`0x4388e4`). If the initial special-location check yields a domain it returns it; if not, `isAMA=true` selects AAF (3) before the supported-directory path loop. Not every imported locator helper was independently named in this bounded pass, so this report does not invent the exact special-location predicate.

Separately, `GetMediaDirectoryNameForDomain` (`0x447ff8`) confirms domain 1 selects `getOmfiDirectoryNameUTF16()`, while domains 4, 5 and 6 select `getAvidMediaDirectoryNameUTF16()`. This agrees with the OMF/MXF managed-folder distinction.

There is no Media Composer release-year comparison or PMR-ID-family scan in these dispatch decisions. MediaMuster's “all selected IDs have legacy form” handling for renamed folders is its own recovery heuristic, not a rule established by these Avid routines.

## Per-file byte evidence is a separate stage

Directory-domain dispatch chooses which scanner to attempt; it is not proof of a file's actual container. Actual acceptance is performed by the format readers/checks downstream. The parent independently decompiled OMF recognition (Bento tail and supported RIFF/RF64 `omfi` wrapper handling) and the MXF file-kind checks.

One tooling clarification: `nm -m` labels `_MvAFileIsMXFFile` as **external**, but it is defined in this same image's `__TEXT,__text` at `0xe5a7dc`, not an undefined import. Its instructions call `_AAFFileIsAAFFileKind` first with the GUID at `0x11b99e4`, then, only after successful negative detection, with the GUID at `0x11b99f4`. The parent resolved these respectively to `kAAFFileKind_MxfKlvBinary` and `kAAFFileKind_AvidAafKlvBinary` using Hopper. This independent pass verifies the two-stage call structure and does not claim to have reimplemented or exhaustively audited the SDK's internal file-kind detector.

## Evidence files

- `binary/ScanFileToCache.pseudo.txt` and `binary/ScanFileToCache.asm.txt`: parent-produced decompilation and instructions, independently read.
- `binary/ScanDirectoryToCache.pseudo.txt`: parent-produced decompilation.
- `binary/ScanDirectoryToCache.asm.txt` and `binary/GetMediaDirectoryDomainType.asm.txt`: added instruction dumps.
- `binary/dispatch-vtable-evidence.txt`: independently decoded dispatch slots, directory-record methods, domain string labels and raw file-kind GUID bytes.

No installed binary, media, database, or production source was modified.
