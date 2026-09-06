# Media Composer AVB binary evidence

Reviewed 2026-09-05. Scope: static inspection of the supplied Media Composer 26.8.0.58987 arm64 code. Media Composer itself was not executed or modified. Binary extraction, symbols, LLVM disassembly, and Hopper 6.5.0-demo focused decompilation were used. 20 of 21 selected procedures yielded Hopper pseudocode; ASourceClip::Get returned None, so its evidence is the direct disassembly. Raw Hopper pseudocode contains inaccurate inferred types/register expressions; conclusions below were checked against assembly and AStream vtable targets, not copied blindly from the decompiler.

Full source paths and SHA-256 hashes are in provenance.json. Key identities:

- libameLibrary.dylib universal: 659e3b6d07c9ae0d54f5741a153244e4a9c554d030c28e0cc7f4c73dba65ba22
- libameLibrary arm64 slice: 70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e
- AvidCore framework universal: b3e046bfc19eea285a330121727fa800ca3a22a0427047512fcf7429e66be1bb
- AvidCore arm64 slice: 59a51c7b3583b13bb816237b3058c58a10d155a8a6a2bcc519fff8865c556902

## Findings supported directly by the binary

### 1. Native MOB serialization is binary and structured

libameLibrary functions:

| Function | arm64 image-relative address |
| --- | --- |
| Write_AAFMobID(AStream*, const char*, _aafMCMobID_t) | 0x235a8c |
| Read_AAFMobID(AStream*, const char*) | 0x235bf0 |
| Write_OMFMobID(AStream*, const char*, _aafMCMobID_t) | 0x235918 |
| Read_OMFMobID(AStream*, const char*) | 0x2359b4 |
| AComposition::Put(AStream*, AIODesc*) | 0x180a70 |
| AComposition::Get(AStream*, AIODesc*) | 0x181420 |
| ASourceClip::Put(AStream*, AIODesc*) | 0x1dd5e0 |
| ASourceClip::Get(AStream*, AIODesc*) | 0x1dd7b8 |

Read_AAFMobID first checks IsSpecialType(1); a special stream dispatches ReadSpecialType(1). The ordinary stream path is equivalent to this analyst-normalized pseudocode:

```cpp
readBytes("MobID.SMPTELabel", id + 0, 12);
id[12] = readUChar("MobID.length");
id[13] = readUChar("MobID.instanceHigh");
id[14] = readUChar("MobID.instanceMid");
id[15] = readUChar("MobID.instanceLow");
material.Data1 = readUInt32();
material.Data2 = readUInt16();
material.Data3 = readUInt16();
readBytes("MobID.material.Data4", id + 24, 8);
```

The writer mirrors this exactly. AComposition::Get dispatches Read_AAFMobID for extension tag 2; ASourceClip::Put calls WriteBeginTagMark(1), then Write_AAFMobID. Thus this is directly connected to CMPO/SCLP object serialization, not merely an unused helper.

The field names are present in the binary. Supporting direct instructions in Write_AAFMobID:

```asm
235af8: mov w3, #0xc       ; 12-byte SMPTELabel
235afc: blr x8             ; stream vtable +0x128 = WriteBytes
235b08: ldr x8, [x8,#0x140] ; WriteUChar for length
235b70: ldr w2, [x19,#0x10] ; Data1
235b78: ldr x8, [x8,#0x178] ; WriteUInt32
235b8c: ldrh w2, [x19,#0x14] ; Data2
235b94: ldr x8, [x8,#0x150] ; WriteUInt16
235bdc: mov w3, #0x8       ; 8-byte material Data4
```

AvidCore stream functions independently explain the framing:

- AStream::WriteBytes, 0x24e38: when tags are enabled, emits tag 0x41 and a 32-bit length, then writes the bytes.
- AStream::WriteUChar, 0x25330: emits tag 0x44 when tags are enabled, then writes one byte.
- AStream::WriteUInt32, 0x258d0: emits tag 0x48 when tags are enabled, applies configured byte swapping, then writes four bytes.

The AStream vtable lives at 0x394250 in AvidCore. astream-vtable.txt records the decoded target addresses for all used virtual call slots; raw encoded pointer values are retained for reproducibility.

**MediaMuster implication:** Keep the warning against treating a tagged MOB record as an arbitrary contiguous 32-byte slice. Correct the much broader statement that Avid writes a MOB as ASCII text. A bounded native reader can reconstruct the 32 logical bytes reliably; existing ASCII scavenging finds another representation of some identities and does not define complete bin membership. OMF native read normally consumes two int32 fields and reconstructs the wrapper; fixture observations of a contiguous wrapper are not a universal native serialization rule.

### 2. Object IDs are explicit references into a table

AvidCore functions:

| Function | arm64 image-relative address |
| --- | --- |
| AObjDoc::InitDomainOld() | 0xc7f90 |
| AObjDoc::ReadCB(void*, AStream*, const char*, unsigned char*) | 0xcaca8 |
| AObjDoc::CreateObject(AStream*, int) | 0xcaf98 |
| AObjDoc::LoadObject(AStream*, int) | 0xcb188 |
| AObjDoc::WriteCB(void*, AStream*, const char*, APortable*) | 0xc943c |
| AObjDoc::WriteObject(AStream*, APortable*, int) | 0xc949c |

InitDomainOld checks the serialized AObjDoc string. CreateObject reads ClassID and Length, calls NewByClassID, stores the object at objectList[id-1], and skips Length bytes. LoadObject reads the same framing, gets objectList[id-1], deserializes the object, and checks whether stream position exceeded payload start + Length. ReadCB reads an object ID using the document's 1-, 2-, or 4-byte ID width, treats zero as null, bounds-checks the ID, and returns objectList[id-1]. The binary includes error strings for invalid ID width and out-of-range object IDs.

Minimal supporting instructions in ReadCB:

```asm
cad34: ldr x8,[x22]
cad38: ldr x8,[x8,#0x250] ; ReadUInt32 for ID width 4
cad84: cbz w21,0xcae98   ; ID zero is null
cad88: ldr w8,[x19,#0x40] ; object count
cad8c: cmp w21,w8        ; upper bound
caee4: sub x23,x9,#1
caee8: ldr x22,[x8,x23,lsl #3] ; objectList[id-1]
```

**MediaMuster implication:** Flat physical chunks do not imply a graph cannot be indexed or navigated. Build an ordinal-to-(class,payload offset,payload size) table in one pass, then read only the classes needed for identities, names, and references. Distinguish absence of a separate on-disk offset index from ability to build an in-memory index; the current blanket “no index and no random access” rationale is misleading. These inspected routines do not by themselves establish every possible on-disk document variant; the implementation should cross-check header fields and variants against fixtures and the reference reader.

### 3. Original-bin metadata has a native object type, with UTF-8 extension

AMCBinRef::Get(AStream*, AIODesc*) at 0x2d5b48 checks class mark 0x4d434252 (MCBR), version 1, reads two int32 identity words and a legacy string handle, then recognizes extension tag 1 and calls ReadUTF8StringAndConvertToUTF16. AMCBinRef::Put is at 0x2d5990. This agrees with the reference-reader MCBR discovery.

Relevant instructions:

```asm
2d5bd0: mov w1,#0x4252
2d5bd4: movk w1,#0x4d43,lsl #16 ; MCBR
2d5bd8: mov w2,#1
2d5bdc: mov w3,#1
2d5be0: blr x8             ; CheckClassMark
2d5c0c: ldr x8,[x8,#0x238] ; ReadInt32 identity high
2d5c34: ldr x8,[x8,#0x238] ; ReadInt32 identity low
2d5c5c: ldr x8,[x8,#0x290] ; ReadStringHandle
2d5c80: cmp w0,#1          ; extension tag 1
2d5cf0: ldr x8,[x8,#0x2a8] ; ReadUTF8StringAndConvertToUTF16
```

**MediaMuster implication:** Resolve a clip's original bin reference through the object graph. Prefer native UTF-8 bin names, retaining legacy name fallback. Keep “current AVB filename” distinct from “original bin name” in data and UI semantics. The precise _ORG_BIN attribute linkage is supported by the reference-reader and fixture work, not asserted solely from this small native function.

## Scope and reproducibility

- Every address above is an image-relative address in the identified arm64 slice, not an ASLR process address.
- *.asm.txt files contain focused LLVM disassembly; *.pseudo.txt files are local Hopper output. The two decompile-*.py scripts state their exact target addresses.
- Metadata and pseudocode are review evidence, not product implementation. No Media Composer binary, decompiled proprietary source, or modified product file was added to the repository.
- This does not certify an exhaustive AVB grammar or historical version support. It supports a targeted, bounded parser and explicit confidence/error states, with reference-reader and real-bin comparison as the validation oracle.
