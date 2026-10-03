from pathlib import Path
import json,html,re,hashlib,collections,shutil,subprocess
R=Path('/Users/martymclean/Developer/MediaMuster');D=R/'docs/reviews/2026-10-03-format-audit';E=D/'evidence';L=Path((E/'large-evidence-location.txt').read_text().splitlines()[0]);F=[]
def add(id,title,status,priority,issue,truth,fix,proof,targets,sources=()):
 F.append(dict(id=id,title=title,status=status,priority=priority,issue=issue,truth=truth,correction=fix,verification=proof,targets=targets,sources=list(sources)))
BMX='https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_baseline_data_model.h'
OMF='https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf'
GUIDE='https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_Editing_Guide.pdf'
KB='https://kb.avid.com/pkb/articles/en_US/Troubleshooting/Refreshing-Media-Databases'
add('F01','2.14 fixed point is displayed as Float','Confirmed in genuine media','P2',
 'bitDepthLabel(254) always returns Float. Its comment asserts that fixed-point and floating-point descriptors are byte-identical, and tests require the same wrong display.',
 'The installed DRUI.xml distinguishes compression labels ending 0307-0200 (16-bit 2.14) and 0307-0100 (32-bit float). Both can use component-width sentinel 254. The complete descriptors therefore differ.',
 'Keep raw component depth and coding identifier. Derive a numeric representation and display depth from both: 16-bit fixed (2.14), 32-bit float, or an explicitly unknown representation. Update both readers and the tests; remove the byte-identical assertion.',
 'Raw sentinel observations: 19 fixed-point paths, 11 distinct file identities; 18 float paths, 10 distinct identities. Every fixed-point path displays Float. Example: tests/fixtures/avid_headers/V01.E6842A40_45B1045B10D32V.mxf. See sentinel-254.json and installed DRUI.xml lines 8219–8248 / 8381 onward.',
 [('src/mediametadata.cpp','254|byte-identical|2.14|03070100|03070200'),('src/mxfparser.cpp','bitDepthLabel'),('src/omfobjects.cpp','bitDepthLabel'),('tests/tst_mediametadata.cpp','Float|254'),('tests/tst_mxfparser.cpp','Float|2.14|float'),('docs','byte-identical|2.14|Float')])
add('F02','RGBA component depths are mostly ignored','Confirmed omission in genuine media','P2',
 'MXF PixelLayout is read only for the special case alpha A:8. Other valid component layouts never populate bitDepth. The OMF RGBA path likewise recognizes the special alpha case rather than a general component-depth list.',
 'Twenty-four genuine MXF paths, representing eight distinct file identities, have PixelLayout 41100000300030003000300030003000: an A component with depth 16, followed by padding/terminators. Their parsed Bits value is empty. A label lookup cannot replace reading this field.',
 'Parse all Code:Depth pairs, retain each component and padding separately, and expose uniform depth when unambiguous. Distinguish component storage depth from numeric representation; do not infer float merely from a compression-family name.',
 'Example /Volumes/EDIT/Avid MediaFiles/MXF/86452/V02.6A0415BB_154BE154BEED3V.mxf; raw descriptor type 0x29. See missing-component-depth.json.',
 [('src/mxfparser.cpp','0x3401|rgbaAlpha8|alphaOnly'),('src/omfobjects.cpp','rgbaLayout|rgbaStructure|oneComponent'),('src/mxfproperties.h','PixelLayout'),('tests','alpha.*8|rgbaAlpha|PixelLayout')],[BMX])
add('F03','Stored, sampled and display geometry are collapsed','Confirmed omission; product semantics need definition','P2',
 'The readers retain only StoredWidth/Height. Sampled and Display geometry and their offsets are absent from the property maps. The table cannot distinguish encoded raster from intended display raster.',
 'Genuine AVC media records stored 320x180 and sampled/display 1280x720; another records stored 480x270 and display 1920x1080. These are real, separate recorded values. Showing stored raster is legitimate if labelled that way; claiming it is the complete displayed resolution is not established.',
 'Preserve all three rectangles plus layout/aspect information in metadata. Define whether the existing Resolution column means stored or display raster; provide the other value in a tooltip/column. Apply field geometry consistently to the chosen rectangle.',
 'Examples tests/fixtures/avid_headers/V01.E68429BA_454B5454B572DV.mxf and corpus_headers/V01.E6969069_252C9252C952BV.mxf. Probe explicit_display_crop requests 1800x1000, but current output is 1920x1080. See geometry-observations.json.',
 [('src/mxfproperties.h','StoredWidth|StoredHeight|FrameLayout'),('src/mxfparser.cpp','0x3202|0x3203|stored height'),('src/omfobjects.cpp','StoredWidth|StoredHeight|e.width|heightIsFrameHeight'),('src/mediametadata.cpp','1088|544|SampledHeight|resolution ='),('tests/tst_mediametadata.cpp','height|resolution')],[BMX,OMF])
add('F04','Padding and field-height rules are promoted to universal facts','Unsupported generalization with observed compatibility behavior','P2',
 'finalise changes every height 1088 to1080 and544 to540. Legacy OMF1 also doubles mixed-fields height. Comments derive these rules from Avid relink ranking and captured MDBs.',
 'DRUI normalization is a relink comparison rule. It does not establish a universal storage/display equivalence for every writer or schema. The inspected files support these conversions in specific cases; the code has no producer discriminator or explicit sampled/display precedence.',
 'Read explicit geometry first. Restrict fallback normalization to positively identified Avid conventions and retain the original value and inference reason. Expand coverage for single-field and segmented-frame layouts instead of guessing a rule from a UI grouping.',
 'The stored_1088_uncropped authored probe outputs1080 despite having no crop property. Real geometry counts also show stored180/270 versus larger sampled/display heights. OMF2 mixed-fields is already treated separately; preserve that distinction.',
 [('src/mediametadata.cpp','Interlaced sources|1088|544|Layout 4|DRUI.xml|half heights'),('src/mxfparser.cpp','stored height'),('src/mdbparser.cpp','HALF height|half height'),('src/omfobjects.cpp','half heights|layout == 1'),('tests/tst_mediametadata.cpp','Layout 3|half height|1088|544'),('docs','half.height|1088|544')])
add('F05','MXF graph traversal follows ordinary 16-byte values','Reproduced with authored metadata; no affected genuine file established','P2',
 'reachableObjectIndexes treats every field as an object reference when its value is16 bytes or has a reference-batch shape. Only EssenceGroup has a typed-property exception.',
 'An ordinary UL can equal another object’s InstanceUID without declaring an ownership edge. The false_UL_edge probe makes DataDefinition equal the file-package UID. The parser then changes REAL_FILE to UNRELATED_MASTER and positively classifies the row, although there is no SourceClip relationship.',
 'Use a class/property reference schema. Traverse only declared reference properties and check target classes. Follow SourceClip cross-package links through SourcePackageID together with SourceTrackID where relevant. Retain unknown properties without adding edges.',
 'Compare probes/no_false_edge.mxf and false_UL_edge.mxf in probes/results.jsonl. They differ only in the file InstanceUID. These are deliberately constructed recovery inputs, not manufacturer-generated samples.',
 [('src/mxfparser.cpp','reachableObjectIndexes|for \(const auto &value : object.fields\)|resolveObjectReferences'),('docs/parser-compatibility.md','Clip identity'),('tests/tst_mxfparser.cpp','coincidental|connect|ownership')],[BMX])
add('F06','Missing MXF references silently disappear','Static finding; crafted graph needed for each affected output','P2',
 'resolveObjectReferences ignores unresolved UIDs. Ownership, import and timing walks can continue using a subset of a declared graph. The strict precompute-category reader checks completeness, but the general walker does not.',
 'Current HeaderStatus::Complete is primarily a framing status. It is not proof that all declared references resolve or that the selected graph is semantically complete. A partially resolved batch is not equivalent to a complete shorter batch.',
 'Return reference values with complete/missing/invalid status. Require complete decisive lists before assigning ownership, duration or classification, and expose recovery confidence separately from framing success.',
 'Direct source inspection. No genuine missing-reference specimen was established in this corpus.',
 [('src/mxfparser.cpp','resolveObjectReferences|found !=|decisive lists|HeaderStatus|headerStatus'),('src/mediametadata.h','HeaderStatus'),('docs/parser-compatibility.md','Clip identity|completed header')])
add('F07','Repeated MXF PackageUIDs overwrite the lookup','Static ambiguity defect; no genuine conflicting specimen found','P2',
 'Duplicate InstanceUIDs are rejected, but packageIndexByMobId.insert replaces the previous package for an equal PackageUID. SourceClip and EssenceContainerData ownership can then depend on file order.',
 'A dictionary with one value per identity cannot represent distinct contradictory packages. Repetition must be distinguished from compatible repeated information or declared malformed input.',
 'Index PackageUID to candidates, reject incompatible duplicates or mark the relationship ambiguous, and never resolve a conflicting identity by last occurrence.',
 'Source inspection at the packageIndexByMobId insertion and consumers. This is an ambiguity-handling finding, not a claimed real-file failure.',
 [('src/mxfparser.cpp','packageIndexByMobId|objectIndexByInstanceUid.contains'),('tests/tst_mxfparser.cpp','duplicate.*[Pp]ackage|duplicate.*[Ii]nstance')])
add('F08','Primer-less tag recovery has the same output confidence','Compatibility/recovery distinction is missing','P2',
 'primer.value(localTag,localTag) decodes canonical-looking tags even without an identifying Primer mapping. Private f003 is specially gated, while f001/f002 can enter the general attribute graph without equivalent identification.',
 'The authored probes without a Primer are accepted with Complete status. This is useful recovery behavior, but it does not prove that a private file-local number denotes the property the app assigns to it.',
 'Separate strict identified parsing from canonical-tag recovery. Require identified private properties for every decisive use. Report confidence/recovery mode rather than describing all accepted fields as resolved through the Primer.',
 'Source inspection plus all seven Primer-less probes. No recommendation to discard compatibility inputs silently.',
 [('src/mxfparser.cpp','primer.value\(localTag|identifiedProperties|0xf001|0xf002'),('src/mxfproperties.h','canonical property|Avid MobAttribute|Avid Tagged'),('docs','Primer|primer')])
add('F09','Several MXF property types accept the wrong lengths','Reproduced permissive typing; genuine corpus uses expected shapes','P2',
 'Descriptor decoding uses >=4 for dimensions/bits/channels, >=8 for rates and picture coding, and >=16 for sound coding. These checks conflate a readable prefix with a correctly typed value.',
 'The 20-byte coding-UL probe is accepted as Complete and described as an unknown VC-3 variant. It is not a16-byte identifier. Narrowing unsigned dimensions/channels to int can also lose representability.',
 'Validate registered property widths before decoding; check integer ranges before narrowing. Preserve malformed/unsupported fields with diagnostics rather than inventing a typed value. If prefix recovery is retained, identify it as recovery.',
 'probes/oversized_20_byte_coding_UL.mxf; parseDescriptorSet and readDescriptor casts. Identity/rate fields already have stricter checks in parts of the graph decoder.',
 [('src/mxfparser.cpp','len >= 4|len >= 8|len >= 16|static_cast<int>\(readUint32BE'),('src/omfobjects.cpp','int\(b.uintValue|int\(s.sampleRate'),('tests/tst_mxfparser.cpp','odd_width|malformed_local')])
add('F10','4–8 byte duration “flavours” are not established MXF types','Unsupported format claim; accepted recovery demonstrated','P2',
 'readDuration accepts4 through8 bytes. Source and test comments assert that different MXF flavours record varying widths, including5–7 bytes.',
 'The baseline model uses Length for ContainerDuration and StructuralComponent Duration; libMXF’s Length representation is signed 64-bit. No genuine5–7-byte duration was established here. Successfully interpreting authored5-byte values does not prove an Avid serialization convention.',
 'Require8 bytes in strict MXF parsing. Keep any historical short-width compatibility path explicit and backed by a named genuine specimen; otherwise remove the unsupported flavour claim and rewrite synthetic tests as recovery tests.',
 'probes/odd_5_byte_duration.mxf returns250 and Complete. Existing odd_width_duration_fields_read_exactly tests repeat this assumption.',
 [('src/mxfparser.cpp','readDuration|4–8 bytes'),('tests/tst_mxfparser.cpp','odd_width|5-7|5–7|recorded length varies'),('docs','duration.*width|width.*duration')],[BMX])
add('F11','Numeric resolution IDs fabricate codec ULs','Reproduced helper error; affected real fallback not established','P2',
 'ulFromResId invents a UL for every integer 1235–1489 using resId−1234 and version 0A, except 1244. Several actual mappings in the app’s own table use version 0D.',
 'For 1256 the helper returns ...010A...71160000, which the codec table cannot name. The installed DRUI.xml associates1256 with ...010D...71160000. Reserved IDs in the accepted interval also receive invented labels.',
 'Replace arithmetic fabrication with explicit evidenced resolution-ID mappings. Unknown IDs should remain unknown numeric IDs, not synthetic registered labels. Keep native label precedence when present.',
 'probes/helper-results.json shows1256,1258,1259,1260 and1270 as unknown variants. DRUI.xml lines6838–6862 confirms1256; src/mediametadata.cpp353 already has the correct0D label.',
 [('src/omfobjects.cpp','ulFromResId|1234|1490|other DNxHD'),('src/mediametadata.cpp','vcid 1256|vcid 1258|vcid 1259|vcid 1260|vcid 1270'),('src/omfresolutions.cpp','1235–1489'),('tests','ulFromResId|1235.*1489'),('docs','1235.*1489|resolution.*[Uu][Ll]')])
add('F12','Legacy text decoding can silently change the text','Confirmed ambiguity; wrong universal comments','P2',
 'AvidText treats every valid UTF-8 byte string as UTF-8, otherwise MacRoman. Its comments and PMR tests say the legacy set is universally MacRoman.',
 'The installed ReadPmrRec calls CurrentMacMBCSEncodingToUnicodeString. The encoding is not stated in the bytes. C3 A9 can mean é in UTF-8 or √© in MacRoman; the helper always chooses é. Windows/code-page histories are not covered by a MacRoman-only fallback.',
 'Prefer explicit UTF8 properties/Unicode PMR section. For untagged text retain bytes and encoding confidence; allow a producer/code-page policy rather than claiming universal correct decoding. Revise universal comments and preserve ambiguous-name diagnostics.',
 'probes/helper-results.json and libame-excerpts.txt0x4475f0. Existing MacRoman specimens demonstrate one encoding, not all encodings.',
 [('src/avidtext.h','MacRoman|UTF-8|never blanked'),('tests/tst_pmrparser.cpp','Avid writes PMR strings|MacRoman|future'),('src/pmrparser.cpp','CurrentMacMBCS|readMbcs'),('docs','MacRoman|code page')])
add('F13','Repeated OMF/MDB mob records choose the first descriptor and values','Static ambiguity defect; compatible duplicates are real','P2',
 'Mob records are grouped, but the first supported descriptor, first object for SourceID hops, and first nonempty editorial attributes win. Usage-code conflicts have stronger handling than these fields.',
 'Observed compatible duplicates justify grouping. They do not prove that conflicting descriptors, projects, locators or names can be unioned into one truthful record. Selection is order-dependent.',
 'Retain candidates and distinguish agreement, complementary information and conflict. Use a declared authority policy where documented; otherwise leave the affected field/relationship ambiguous. Preserve the existing usage conflict handling.',
 'MdbParser objectByMob and fileMobObjectId selection; OmfParser MobGroup; walkAttributes first-nonempty assignments. No conflicting genuine sample was found.',
 [('src/mdbparser.cpp','First object|first non-empty|fileMobObjectId == 0|union of its objects'),('src/omfparser.cpp','fileMobObjectId == 0|objectByMob.insert|MobGroup'),('src/omfobjects.cpp','isEmpty\(\)|first-non-empty'),('docs','first non.empty|multiple objects.*MobID')])
add('F14','OMF HEAD indexes and media-object classes are bypassed','Unproven semantic equivalence; recovery limitation','P2',
 'OmfParser’s comment says HEAD is unnecessary. It scans every object carrying mob/media-ID properties; mediaDataMobId does not require the corresponding media-data class or HEAD membership.',
 'The OMF header declares mob/media-data collections. A dictionary/TOC scan locates stored objects, including any unreferenced objects; it does not by itself establish active membership.',
 'Use revision-specific HEAD collections and validate classes when present. Keep all-object discovery as an explicit recovery mode. Distinguish missing HEAD, incomplete lists and incompatible active media IDs.',
 'OMF2.1 Header class, PDF page164; source mediaDataMobId/property scans. Actual shipped single-essence files succeed; no genuine orphan contamination was established.',
 [('src/omfparser.cpp','HEAD object|does not need it|objectsWithProperty|mediaDataMobId'),('src/mdbparser.cpp','objectsWithProperty'),('src/bentofile.cpp','SourceMobs|CompositionMobs|HEAD')],[OMF])
add('F15','OMF timecode is the first TCCP found anywhere in ancestry','Static provenance gap; no real disagreement established','P2',
 'findTimecodeComponent walks all supported components and SourceIDs, then returns the first TCCP. SourceTrackID, source start position, slot origin and competing timecodes are not represented in this walk.',
 'OMF source references include a track and position, not only a MobID. A timecode discovered on a different track/branch is not automatically the file’s applicable timecode.',
 'Carry typed source-track and position context, collect candidate timecodes, and require an applicable unambiguous result. Mark start timecode as unavailable when offsets are not evaluated; avoid copying an arbitrary ancestor’s value.',
 'findTimecodeComponent/readTimecode and OmfMetadata.startTimecode assignments. Core inventory needs duration/drop state, not a complete OMF timeline evaluator; implement the relevant subset explicitly.',
 [('src/omfobjects.cpp','findTimecodeComponent|referencedMobObjectIds|tc.start|tcFlags'),('src/omfparser.cpp','startTimecode|Timecode:|findTimecodeComponent'),('src/omfobjects.h','sourceId|tcStart|Timecode'),('src/mdbparser.cpp','drop frame|source mobs a SCLP')],[OMF])
add('F16','A single OMF mob rate is an insufficient multi-slot model','Documented limitation; schema model needs extension','P3',
 'readMobEditRate returns no rate if readable OMF2 slot rates disagree. Several downstream derivations want one mob-level rate even though slots own their clocks.',
 'Refusing to invent one rate is correct. It can nevertheless discard a usable rate for the selected essence track when audio and timecode slots differ.',
 'Select the relevant slot through source-track/descriptor relationships and retain per-track rates. Keep differing clocks separate; do not restore a first-rate-wins rule.',
 'readMobEditRate and readDescriptor; current genuine OMF corpus does not include a standard OMF2 multi-rate example.',
 [('src/omfobjects.cpp','readMobEditRate|Slots own rates|slotRate'),('src/omfobjects.h','slotRate'),('tests/tst_mdbparser.cpp','slot.*rate|rate.*slot')])
add('F17','Identifier width is used as a media-era discriminator','Unsupported generalization; current specimens happen to fit','P2',
 'MDB attribute/codec rules treat 12-byte IDs as OMF-era and 32-byte IDs as MXF-era. This changes legacy codec precedence and admission of WINL/UNXL source locators.',
 'The same OMF files contain32-byte physical mob identities in MC2026. ID representation, declared schema and media family are distinct facts. Width alone does not prove which legacy descriptor/attribute conventions apply.',
 'Pass media family/schema/descriptor evidence explicitly. Use width only for decoding the identity. Preserve compatibility rules supported by actual descriptors rather than tying them to12 versus32 bytes.',
 'The code already supports hybrid OMF identities and normalizes them. This finding concerns behavioral branching on width, not the12/32-byte ID decoding itself.',
 [('src/mdbparser.cpp','omfEra =|12-byte mob|MXF-era file mob'),('src/omfobjects.cpp','32-byte \(MXF-era\)|kUidSize|12-byte\)|MXF-era \(32-byte\)'),('src/omfresolutions.cpp','Entirely OMF-specific|no MXF-era row')])
add('F18','Private AUID conversion assumes little-endian numeric fields','Verification gap','P3',
 'auidToUl swaps GUID numeric fields unconditionally and has no container byte-order argument. The reader otherwise advertises both metadata byte orders.',
 'This matches the observed little-endian Avid MDB payloads. The audit does not establish whether this private Avid property is always serialized that way in big-endian containers.',
 'Identify the property’s declared type/serialization before choosing endianness. Add a genuine big-endian specimen with EssenceCompression, or document the narrower supported convention; do not change the swap merely because the container is big-endian.',
 'Source and test review; no matching genuine big-endian private-property sample was found.',
 [('src/omfobjects.cpp','auidToUl'),('src/omfobjects.h','auidToUl'),('src/mdbparser.cpp','AAF AUID|GUID order'),('tests','auidToUl')])
add('F19','Standard OMF locator classes are omitted','Verified schema omission; absent from inspected Avid specimens','P3',
 'isLocatorClass admits MACL/WINL/UNXL. DOSL, NETL and TXTL properties are not resolved. A standard OMF2 file can retain a source-location hint the app never reads.',
 'OMF2.1 defines DOSL:PathName, NETL:URLString and TXTL:Name. These represent different kinds of locator information; a text hint is not necessarily a usable filesystem path.',
 'Read supported standard locators by class and preserve their kind. Populate sourceFilePath only for a real path; retain URL/text hints separately. Otherwise explicitly document the locator subset.',
 'OMF2.1 PDF pages150,193/related locator sections; PropertyIds and locatorPath have no corresponding properties.',
 [('src/omfobjects.cpp','isLocatorClass|locatorPath|PathName|MACL|WINL|UNXL'),('src/omfobjects.h','Path|unxl|winl|macl'),('docs/parser-compatibility.md','OMF1 and OMF2')],[OMF])
add('F20','WAVE valid bits and subtype are ignored','Verified format omission; no affected genuine managed specimen found','P3',
 'readWaveSummary reads only the basic16-byte fmt prefix. It uses container bits for Bits and does not interpret WAVE_FORMAT_EXTENSIBLE valid bits, channel mask or subtype.',
 'WAVEFORMATEXTENSIBLE separates valid sample precision from container width. A20-bit signal in24-bit storage cannot be described accurately by one undifferentiated bit count.',
 'Validate and decode the extensible tail when present, retain storage bits and valid bits separately, and record subtype/channel mask. Keep WAVE (OMF) clearly a wrapper/display-format label rather than proof of PCM or float.',
 'Static source review and Microsoft’s documented structure. Inspected Avid legacy summaries use the ordinary forms.',
 [('src/omfobjects.cpp','readWaveSummary|bitsPerSample|formatTag'),('src/omfobjects.h','WaveSummary'),('src/omfresolutions.cpp','WAVE \(OMF\)'),('tests/tst_omfparser.cpp','fmt |WaveSummary')],['https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible'])
add('F21','AIFF/RIFF summary validity is weaker than chunk validity','Static recovery/validation distinction','P3',
 'Summary readers accept the required prefix even if the declared fmt/COMM chunk extends past the blob. AIFF COMM bytes beyond18 are treated as a compression fourcc without checking that the form is AIFC.',
 'An OMF summary can intentionally omit audio data, so requiring the entire original RIFF/FORM file would be wrong. But prefix-derived facts do not certify a complete fmt/COMM payload, and AIFF has no AIFC compression field.',
 'Define summary-specific framing rules; check availability of the declared metadata chunk, and inspect compType only for AIFC. Return recovered facts with a partial status when truncation is deliberately tolerated.',
 'readWaveSummary/readAifcSummary source. The returned AifcSummary.compressionType is currently not used for codec naming, so the AIFF mistake is presently latent.',
 [('src/omfobjects.cpp','size < 16|size < 18|size >= 22|compressionType|s.valid ='),('src/omfobjects.h','compressionType|valid')])
add('F22','PMR timestamps establish a cache heuristic, not current content','Acknowledged limitation with an additional timezone assumption','P2',
 'databaseMetadataCurrent uses a nonzero PMR timestamp matched to filesystem mtime. The alternate Mac-epoch candidate subtracts the scanning machine’s local timezone offset. Size/identity/content and writing-machine timezone are not part of this proof.',
 'The installed CompareDirectory independently supports exact±3600 comparisons. That does not prove every PMR timestamp uses Unix UTC or1904 local time for the current machine. Identical mtimes also do not prove unchanged media.',
 'Rename/describe this state as a timestamp-based cache match. Retain the raw timestamp and chosen interpretation. Require media identity checks for ambiguous cases and before operations; add producer/timezone evidence before making broader epoch claims.',
 'libame-excerpts.txt0x4537a8–0x4537c0; trailerMatchesModified and buildMediaFile. Docs already correctly acknowledge modification dates are practical cache checks.',
 [('src/pmrparser.cpp','trailerMatchesModified|kMacToUnix|utcOffsetSeconds|EXACTLY'),('src/mediascanner.cpp','databaseMetadataCurrent|databaseTimestampMatches'),('src/pmrparser.h','trailer'),('docs','modification dates|timestamp|Mac epoch')])
add('F23','Normalized PMR filename collisions choose the first record','Static selection defect; not reproduced on this case-insensitive volume','P2',
 'PmrKey lowercases and NFC-normalizes filenames. The index keeps multiple entries, but buildMediaFile chooses pmrIt->first without resolving conflicting exact names/identities. Primary and AMA indexes can also contribute colliding records.',
 'Two case-distinct filenames can exist on a case-sensitive filesystem. Their identical normalized key does not prove identical media. Exact/canonical path behavior elsewhere in the app does not fix this PMR selection.',
 'Prefer an exact filename match, then a unique compatible normalization match. Treat incompatible candidates as ambiguous and verify the header; do not supply arbitrary editorial metadata. Handle primary/secondary index conflicts explicitly.',
 'PmrKey::primary, buildFileMap, readFolderPmrs, buildMediaFile. The scanner’s filesystem-specific case test skipped here and tests folder casing rather than proving every PMR collision policy.',
 [('src/pmrkey.h','lower-cased|toLower|Normalization'),('src/pmrparser.cpp','map\[PmrKey'),('src/mediascanner.cpp','pmrIt|pmrHit =|dbs.pmr\['),('docs','PMR.*normal|normal.*PMR')])
add('F24','Database files are not read as a coherent snapshot','Verification/concurrency gap','P2',
 'PMR and MDB are opened separately, fully read and then joined. There is no shared generation/snapshot check around the pair or stat-before/stat-after validation comparable to bin loading.',
 'Reading safely bounded bytes does not guarantee that separate files came from the same Avid rebuild generation. Local test directories do not establish behavior with an active writer on NEXIS/NAS.',
 'Capture file identity/size/mtime before and after each database read, retry boundedly on change, and expose an unstable-database status. Use read-handle identity/protection where available; retain header fallback when a coherent pair cannot be established.',
 'PMR/MDB load and scanner folder joins. No live concurrently rebuilding shared-storage test was performed; this is not a claim that every scan currently mixes generations.',
 [('src/pmrparser.cpp','QFile file|file.readAll'),('src/mdbparser.cpp','QFile file|file.readAll'),('src/mediascanner.cpp','readFolderPmrs|readFolderMdbs'),('docs/parser-compatibility.md','simultaneous-writer|modification dates')])
add('F25','Boolean/timecode merges cannot replace a positive value with false','Static merge defect; conflicting real cache/header not established','P2',
 'applyMetadata updates dropFrame only when it is true. A database true survives a successfully read non-drop header for the same identity. Empty technical fields similarly retain earlier values.',
 'False, absent, malformed and unknown are distinct states. A plain bool cannot tell the merge whether a header proved non-drop or simply supplied no timecode evidence.',
 'Add presence/confidence for timecode and other optional fields. Replace a field on stronger explicit evidence, including false; retain it only when the newer source is genuinely unknown. Exercise same-identity DB/header disagreement.',
 [('src/mediascanner.cpp','if \(metadata.dropFrame\)|mf.dropFrame = true|timecodeBase'),('src/mediametadata.h','dropFrame|timecodeBase'),('src/mxfparser.cpp','DropFrame|dropFrame ='),('src/omfobjects.cpp','dropFrame =')],
 [('src/mediascanner.cpp','if \(metadata.dropFrame\)|mf.dropFrame = true|timecodeBase'),('src/mediametadata.h','dropFrame|timecodeBase'),('src/mxfparser.cpp','DropFrame|dropFrame ='),('src/omfobjects.cpp','dropFrame =')])
# fix a deliberately separate verification string after construction
F[-1]['verification']='Direct inspection of applyMetadata. clearReplacedMetadata correctly resets dropFrame when identities differ; this finding concerns the same-identity path.'
add('F26','Stale editorial values can override a usable same-identity header','Explicit precedence policy; truth/confidence not represented','P2',
 'Project, original bin and import/source information generally fill only missing values. PMR/MDB editorial values are applied even when the timestamp is stale; a same-identity header does not replace them. A changed identity correctly clears the old row.',
 'A stale timestamp triggers a read, but that alone does not make the final editorial fields fresh. The selected source order is a product policy; it is not proof that its retained project/path is the current recorded value.',
 'Record source, freshness and conflicts per field. Decide which source is authoritative for each editorial fact; distinguish database-current metadata from cached descriptive recovery. Do not advertise stale-data verification as a refresh of all fields.',
 'buildMediaFile, applyMdbRecord, applyMetadata, readMediaHeader project assignment and same-ID rejoin. No genuine contradictory project was established in this run.',
 [('src/mediascanner.cpp','assignIfMissing|preserve an existing database value|applyMdbRecord|project.isEmpty'),('docs/parser-compatibility.md','Stale databases'),('docs/current-behaviour.md','project|stale')])
add('F27','Every nonempty MXF header is read with the current flag','Confirmed behavior; contradictory documentation','P2',
 'kClipDurationEnabled is true and needsHeaderRead unconditionally includes every nonempty non-OMF row. Claims that current complete MXF databases retain the no-header-read fast path omit this condition.',
 'The extra reads are intentional to produce per-track Clip Duration unavailable from the current MDB model. They are not required for every other metadata column. The feature-gates table and later current-behaviour section already state this correctly.',
 'Qualify fast-path claims throughout comments/docs. If scan I/O matters, make per-track clip duration an explicit/lazy capability, or add equivalent validated database extraction; do not silently remove requested data.',
 'src/featureflags.h and mediascanner.cpp1071. Existing tests explicitly expect this behavior. parser-compatibility.md14 is misleading in the current build.',
 [('src/featureflags.h','kClipDurationEnabled'),('src/mediascanner.cpp','headers for the rows|Pass 2|needsHeaderRead =|marks rows|database fast path'),('docs/parser-compatibility.md','fast path|Header checks'),('docs/current-behaviour.md','avoid|skip|headers are read'),('docs/release-feature-gates.md','avoid|reads MXF headers|also reads MXF')])
add('F28','The documented64 MiB MXF metadata cap does not exist','Confirmed documentation error','P2',
 'parser-compatibility.md64 says selected metadata is bounded to64 MiB and the limit is reported separately. Source explicitly has no policy ceiling; HeaderStatus has no matching resource-limit status.',
 'The current test metadata_beyond_former_resource_limit_is_read explicitly accepts more than64 MiB. Bento’s64 MiB dictionary-prefetch optimization is a different mechanism.',
 'Remove or correct the stale cap statement. If introducing a resource limit, choose it as an application policy, add an explicit limit result and streaming strategy, and keep valid large metadata distinguishable from malformed input.',
 'mxfparser.cpp selected-buffer check; mxfparser.h no-ceiling comment; tst_mxfparser.cpp1020–1042; bentofile.cpp17.',
 [('docs/parser-compatibility.md','64 MiB'),('src/mxfparser.cpp','no policy size ceiling|representable'),('src/mxfparser.h','size or item-count ceiling'),('tests/tst_mxfparser.cpp','former_resource|64 MiB|Beyond 64'),('src/bentofile.cpp','kDictionaryPrefetchBytes')])
add('F29','Whole-database and metadata-copy I/O has avoidable cost','Verified implementation cost; not an Avid format defect','P3',
 'PMR and MDB readAll load the entire database. MXF first probes up to64 KiB and then rereads selected KLVs; selected values are copied into aggregate metadata, raw sets, per-field storage and canonical fieldData. Many graph-unneeded useful-set fields are retained.',
 'BentoFile already supports on-demand file-backed reads for OMF. The app does not bulk-read MXF/OMF essence in the normal metadata path. Avoiding essence decoding is correct; avoiding redundant metadata storage is still possible.',
 'Move MDB to file-backed Bento access where advantageous; avoid reopening/re-reading the initial region; decode typed selected fields once and preserve required unknown data only if needed. Measure on genuine large MDBs before changing PMR’s simpler small-file path.',
 'Source inspection plus bytesRead outputs; authored520-byte graph probes count1040 actual disk bytes because the probe is read twice. No benchmark claims about network speed or memory peak are made.',
 [('src/pmrparser.cpp','readAll'),('src/mdbparser.cpp','readAll|b.load'),('src/mxfparser.cpp','rawSets|metadata.append|fieldData|65536|kProbeBytes'),('src/bentofile.cpp','kDictionaryPrefetchBytes|fetch')])
add('F30','MXF identity operations accept two byte-order spellings','Static guard ambiguity; no real collision established','P2',
 'mediaIdentityMatches accepts expected==actual OR expected==swapMaterialByteOrder(actual). The scanner establishes one canonical PMR/MDB spelling, but the operation guard permits the alternate identity too.',
 'Byte-order conversion belongs at the format boundary. Reversing material fields of a different32-byte identity is not a general equivalence relation for canonical IDs. Both alternatives cannot prove exactly the scanned identity.',
 'Convert the header result once to the scanner’s canonical representation, then compare exact IDs. If a persisted older journal used another representation, migrate it with explicit provenance instead of making every comparison permissive.',
 'oprunner.cpp109–110 compared with canonicalHeaderId in mediascanner.cpp. Current bin filtering correctly refuses modern byte-order aliases.',
 [('src/oprunner.cpp','expected == actual|swapMaterialByteOrder'),('src/mediascanner.cpp','canonicalHeaderId'),('src/binfilereferences.h','fullIds.contains'),('docs/avb-parser.md','byte-order aliases')])
add('F31','OMF operations have no format identity recheck','Verified asymmetry; operation stat guards still apply','P2',
 'mediaIdentityMatches returns true for every source without the MXF extension, including OMF-era media whose identity the scanner knows.',
 'File-operation stat/handle protections are useful but do not supply an OMF identity comparison. A same-stat replacement risk is not eliminated by an MXF-only format guard.',
 'Use OmfParser over the protected operation handle, or a minimal file-backed identity reader, and compare canonical OMF IDs when known. Distinguish unreadable identity from unsupported formats according to the operation contract.',
 'oprunner.cpp99–101. This audit did not execute copy, move, delete or rebalance operations.',
 [('src/oprunner.cpp','mediaIdentityMatches|hasMxfExtension'),('src/omfparser.h','parseHeader'),('src/opfile.h','io\('),('docs/file-operations-native-api-validation.md','identity|OMF|MXF')])
add('F32','Automatic database-rebuild wording is too broad','Confirmed scope error in UI promise','P2',
 'Rebalance confirmation says Avid will rebuild its database on next project open without specifying storage management. The application also scans shared and MediaCentral-accessible trees.',
 'The2026 Editing Guide limits ordinary directory-refresh guidance to local/unmanaged storage. Avid’s database-refresh KB expressly separates standalone handling from managed shared environments. A future rebuild is not established simply by moving media.',
 'Qualify the reminder by storage management and give the appropriate rescan/ownership workflow. Do not imply the app coordinates Media Indexer or other editing seats. Preserve the existing requirement to close files before operations.',
 'rebalancedialog.cpp607–608;2026 guide PDF page359. No live Avid rebuild was triggered.',
 [('src/rebalancedialog.cpp','will rebuild|next project open'),('docs','rebuild.*open|rebuild.*project|next project open')],[KB,GUIDE])
add('F33','Format acceptance is broader than native Avid qualification','Intentional scope limitation; names/status need care','P3',
 'The scanner discovers .mxf in recognized managed folders, but the reader does not validate OperationalPattern, essence/index integrity or footer updates before describing it.',
 'A readable header is not a certified Avid OP-Atom media file. HeaderStatus and valid currently express metadata usability. The docs correctly state many of these exclusions.',
 'Keep metadata readability separate from container/operational-pattern qualification. If the product needs native-managed certification, validate the required profile and report unsupported/malformed status without confusing it with unknown codec metadata.',
 'MxfParser header-only reader; current parser-compatibility limitations. Genuine corpus succeeded; no footer-only or revised-footer specimen was covered.',
 [('src/mxfparser.cpp','isPartition|partitionEssenceContainer|next partition|essence'),('src/mxfparser.h','header partition|recovery path'),('src/mediametadata.cpp','meta.valid = true'),('docs/parser-compatibility.md','certify essence|footer|multi-track'),('docs/release-feature-gates.md','OP-Atom|OP1a|UME')],['https://kb.avid.com/pkb/articles/en_US/Knowledge/import-dailies-into-MC','https://developer.avid.com/'])
add('F34','Whole-bin MSML membership is not timeline usage','Intentional documented semantic limitation','P3',
 'AvbParser visits all indexed objects and collects MSML file identities. It does not evaluate a selected sequence, effect inputs, trims or root reachability for a semantic usage calculation.',
 'All121 bins agree with pyavb for the compared CMPO/MSML data. That proves extraction on these bins, not which media is needed by a selected edit. Known skipped descriptor/effect payloads also prevent complete object-graph certification.',
 'Keep whole-bin file-reference filtering described precisely. If adding “used by sequence” or unused-media deletion semantics, implement typed reachability/timeline selection and effects coverage, with an explicit unsupported result.',
 'pyavb-comparison.json: zero compared-field mismatches. The reference reader itself failed on635 descriptor objects in11 bins; those were skipped for selective CMPO/MSML comparison, so there is no claim of full-reference decoding.',
 [('src/avbparser.cpp','MSML|readMediaLocator|complete =|skip'),('src/avbparser.h','whole-bin|timeline|complete'),('docs/avb-parser.md','whole-bin|all indexed|skipped'),('src/binmetadataresolver.cpp','masterMobType')],['https://github.com/markreidvfx/pyavb'])
add('F35','Hard-coded placeholder identity is assumed never real','Unproven exclusion rule','P3',
 'MdbParser skips a fixed32-byte identity on sight. Its comment says it is never a real clip and is in no MXF.',
 'The inspected corpus can demonstrate that an identity is repeated as a placeholder and absent from these MXFs. It cannot establish a universal never-real identity across all producers, builds and external imports.',
 'Identify the placeholder through object role/content as well as identity, retain a diagnostic, and state the empirical scope. Avoid making a global reserved-identity rule without manufacturer specification.',
 'placeholderMobId and raw==placeholder exclusion. No legitimate media with that identity was found.',
 [('src/mdbparser.cpp','placeholderMobId|Never a real|in no'),('tests/tst_mdbparser.cpp','placeholder'),('docs','placeholder MOB|placeholder identity')])
add('F36','Supported file suffixes and container subclasses are a subset','Intentional supported-scope limitation','P3',
 'Managed OMF discovery admits .omf/.wav/.aif, omitting .aiff/.omfi and historical SDII. Bento1.1 extended labels/update overlays, some RF64 lengths and custom OMF subclasses are not fully implemented.',
 'Avid’s historical documentation includes AIFF/SDII and OMF exchange files. That does not require a managed-media inventory to accept every exchange/timeline file. Current docs explicitly define the narrower suffix and container scope.',
 'Keep the scope table explicit. If expanding legacy media coverage, require actual file-backed Avid specimens and distinguish exchange documents from single-essence managed files; use unknown/unsupported results for unimplemented subclasses.',
 'Conventions/AvidMediaLayout and BentoFile admission checks. All14 real MDB paths in this run use the legacy OMF1 schema; standard OMF2 coverage here is authored tests, not a new broad genuine corpus.',
 [('src/conventions.h','hasOmfEraExtension|aif|omf'),('src/bentofile.cpp','unsupported|RF64|ds64'),('src/omfobjects.cpp','isMediaClass'),('docs/release-feature-gates.md','aif|omfi|SDII|suffix'),('docs/parser-compatibility.md','Bento|RF64|custom OMF|testing gap')],['https://kb.avid.com/pkb/articles/en_US/troubleshooting/en273303'])
add('F37','Rate display buckets obscure exact nearby rates','Reproduced presentation behavior; raw rate correctly preserved','P3',
 'applyEditRate labels every rate within 0.01 of a recognized fraction/integer as that bucket. Comments say the five fractions are the complete set Media Composer can produce.',
 'The24995/1000 probe retains exact24.995 for duration but displays25 and gets DNxHD120 branding via the label. This is a presentation convention, not exact rate equality. A static set is not proof about every future or external producer.',
 'Keep exact rational data for decisions. Label equivalent reduced fractions exactly; mark approximate legacy decimal recognition as a convention. Qualify the complete-set claim by supported build/project formats, and avoid using a rounded display label as the only codec-tier input.',
 'probes/nearby_25_rate.mxf and applyEditRate/kDnxTiers. Exact duration arithmetic is already preserved correctly.',
 [('src/mediametadata.cpp','complete fractional set|closest fractional|kFractional|qAbs\(rate|kDnxTiers'),('tests/tst_mxfparser.cpp','Every fractional|equivalent_rate|fractional_frame'),('docs','fractional.*complete|complete.*fractional|supported.*rates')])
add('F38','Codec/effect tables and absence-based defaults are empirical','Evidence scope; not all entries independently certified in this run','P3',
 'Several mapping tables and classification defaults are drawn from installed configuration, old specimens and earlier reverse-engineering reports. Recognized labels/names do not prove bitstream integrity, plugin availability or all future codec settings.',
 'OMF DV100 rows are already marked unverified; effect names are already documented as editable-name recognition. Missing MXF usage defaults to ordinary media under an explicitly documented corpus convention. These are not universal format axioms.',
 'Retain per-row provenance and verification level; preserve unknown raw values. Keep inferred classifications distinguishable from explicit manufacturer fields. Revalidate mappings when the installed build changes; do not promote filenames or synthetic fixtures to specifications.',
 'Installed26.8 configuration and binary agree with several core mappings checked here, including PMR framing, usage1/7/9, and distinct fixed/float labels. The audit does not newly certify all887 effect pairs or every codec UL. Avid’s current SDK also describes wider flexible VC-3 settings.',
 [('src/omfresolutions.cpp','UNVERIFIED|Every row|1235–1489'),('src/avideffects.cpp','catalogue|kEffects'),('src/avidusage.h','No usage properties|materialClassification'),('src/omfuid.h','never this|every specimen'),('docs/avid-effects-catalogue.md','not a definitive|future version|availability'),('docs/usage-code-identification.md','compatibility rule|not a claim')],['https://developer.avid.com/'])
add('F39','A unique orphan project is treated as the selected file’s project','Explicit recovery heuristic; ownership not established','P3',
 'When scoped project searches are empty, the MXF reader collects project tags from the entire header and accepts one distinct value. The comment says this avoids an arbitrary unrelated package, but uniqueness alone does not establish ownership.',
 'The existing test intentionally recovers an orphan project. This verifies the heuristic, not a recorded association between that attribute and the selected essence.',
 'Retain this compatibility recovery if useful, but mark the project as inferred/orphan-derived. Prefer a typed owning-package relationship and preserve conflicts. Do not present a unique global value as verified ownership.',
 'parseHeaderMetadata projectsIn(all) fallback and project_recovers_unique_orphan_attribute test. No genuine wrong project was established.',
 [('src/mxfparser.cpp','Recover a project|every readable|projectsIn\(all\)|never choose an arbitrary'),('tests/tst_mxfparser.cpp','project_recovers_unique_orphan|project.*fallback'),('docs','orphan.*project|project.*orphan|project.*ownership')])
add('F40','MXF package discovery is not scoped through the active header root','Static semantic scope gap; no genuine orphan conflict found','P3',
 'selectPackages considers every recognized EssenceContainerData and SourcePackage set. Preface/ContentStorage active package membership is not used to restrict this candidate set, although some related property identifiers are already in the table.',
 'Finding an object in header bytes and finding an object through the declared root are different observations. Unreferenced ECD/package objects can alter uniqueness even if one active native file package is unambiguous.',
 'Use the Preface/ContentStorage package and essence-data collections when available, validate their references, and expose whole-header candidate discovery as recovery. Combine this with typed traversal rather than adding another uniqueness guess.',
 'selectPackages ECD loop; kMxfProperties ContentStorage::EssenceContainerData identifier is present but not used to scope that loop. No genuine unreferenced package was established.',
 [('src/mxfparser.cpp','selectPackages|for \(const auto &set : header.objects\)|linkedFiles'),('src/mxfproperties.h','Preface|ContentStorage|EssenceContainerData'),('docs/parser-compatibility.md','owning file package|Clip identity')],[BMX])

# Every targeted textual occurrence, with live/historical status, retained verbatim.
files=[p for root in ('src','tests','docs') for p in (R/root).rglob('*') if p.is_file() and p.suffix in ('.cpp','.h','.mm','.md','.txt') and D not in p.parents and 'fixtures' not in p.parts]
cache={p:p.read_text(errors='replace').splitlines() for p in files}
for f in F:
 hits={}
 for target,pat in f.pop('targets'):
  for p,lines in cache.items():
   rel=p.relative_to(R).as_posix()
   if rel!=target and not rel.startswith(target.rstrip('/')+'/'):continue
   for n,line in enumerate(lines,1):
    if re.search(pat,line,re.I):hits[rel,n]={'file':rel,'line':n,'text':line.strip(),'context':'Historical review; assess against current source' if rel.startswith('docs/reviews/') or 'review' in p.name else 'Current source/test/documentation'}
 f['occurrences']=list(hits.values())
(E/'findings.json').write_text(json.dumps(F,indent=2,ensure_ascii=False))
(E/'occurrence-inventory.tsv').write_text('Finding\tContext\tFile\tLine\tText\n'+'\n'.join(f['id']+'\t'+x['context']+'\t'+x['file']+'\t'+str(x['line'])+'\t'+x['text'].replace('\t',' ') for f in F for x in f['occurrences'])+'\n')
manifest=[{'path':p.relative_to(R).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'lines':len(cache[p])} for p in files]
(E/'code-documentation-manifest.json').write_text(json.dumps(manifest,indent=2))
counts=collections.Counter(f['status'] for f in F);stats=json.loads((E/'corpus-summary.json').read_text())
# Direct raw evidence for component omissions and geometry.
rows={r['path']:r for r in map(json.loads,(L/'parser-results.jsonl').read_text().splitlines())};geometry=[]
for r in map(json.loads,(L/'descriptor-observations.jsonl').read_text().splitlines()):
 if any(d.get('sampleH',d.get('sh'))!=d.get('sh') or d.get('dh',d.get('sh'))!=d.get('sh') for d in r['descriptors']):geometry.append({'raw':r,'decoded':rows[r['path']].get('metadata')})
(E/'geometry-observations.json').write_text(json.dumps(geometry,indent=2))
missing=[r for r in rows.values() if 'metadata' in r and r['metadata']['width']>0 and not r['metadata']['bits']]
(E/'missing-component-depth.json').write_text(json.dumps(missing,indent=2))
shutil.copy('/tmp/mediamuster-audit-build.log',E/'build-log.txt') if Path('/tmp/mediamuster-audit-build.log').exists() else None
intro='''MediaMuster reads a substantial and useful subset of Avid metadata. It is not possible to certify the whole implementation as matching all Avid formats. This audit found genuine output defects, permissive parsing/ambiguity defects demonstrated by constructed inputs, contradictory documentation, and explicit supported-scope limits. They are separated below; an accepted synthetic input is never presented as proof of a manufacturer-generated failure.

The app does not decode or transcode picture/audio essence. PMR supplies filename/identity/project/cache information; MDB supplies mob metadata; MXF and OMF readers recover selected embedded metadata; AVB supplies whole-bin file locators and clip/original-bin metadata. The scanner joins these sources into one row per physical file. OMF support is currently ON: src/featureflags.h has kOmfEnabled=true, a compile-time gate, not a runtime preference.

No production code, tests, existing documentation, databases, bins or media were edited. Only this audit and its read-only evidence were added. The attached CPP SKILL.md was treated as reference material, not as user instructions. The two supplied Python libraries were comparison sources, not authoritative specifications.

Scope: repository HEAD 86c500ca6bf9cc93529017979d0f1b3ddac856be; installed Media Composer 26.8.0.58987. Core format readers and their shared models, scanner joins, bin consumers, operation identity guards and format claims in comments/docs/tests were examined. The occurrence inventory searches the entire textual src/tests/docs tree for each identified issue; related occurrences include tests and historical evidence, not just assertions that are wrong today. General UI/business logic unrelated to format interpretation was not audited for all possible bugs.

Internet scope: primary Avid developer material, knowledge-base guidance, the 2026 documentation index/Editing Guide, selected historical Avid media/shared-storage guides, the original OMF2.1 specification hosted by a mirror, the AAF object-model reference, SMPTE’s registry and BBC/libMXF’s actual data model, plus Microsoft’s WAVE structure. The web cannot be exhaustively enumerated, and public Avid pages do not supply a complete versioned PMR/MDB/AVB specification. This report does not claim to have read every Avid page or proved undocumented behavior for every release.

Local discovery: repository fixtures, the supplied Downloads files/directories, /Volumes/EDIT, Desktop/Documents and /Users/Shared; installed Avid SupportingFiles and application resources. Other inaccessible/unmounted/excluded locations and unrelated ordinary audio files are outside the corpus. Counts are paths, not unique physical specimens. Many paths are copied media or captured headers, not complete essence files.

Production reader probe: 5013 MXF paths (2875 distinct file identities and metadata signatures),17 PMR,14 MDB,160 OMF,2 WAVE,2 AIFF-C and121 AVB paths: 5329 total. All were accepted by the relevant production reader; this alone proves neither field correctness nor media integrity. Independent raw KLV walks covered all 5013 MXF paths; independent Bento1 tail/dictionary inspection found 178 containers. The121 bins agreed with pure-Python pyavb for CMPO identities/names/type/usage/original-bin and full MSML IDs. pyavb failed to decode 635 other descriptor objects across 11 bins, so that comparison is selective, not a complete decode certificate.

The supplied sample.avb was included: six CMPO mobs and three full MSML file identities matched the independent reader. Its master clip and sequence are different objects; the current bin filter uses file locators, not the sequence identity. The supplied PMR.py computes identity length as version multiplied by four and labels its first record identity materialPackageUID, whereas this installed Avid reader branches between legacy8-byte and AAF32-byte fields and the corpus joins the first identity to the physical file. It is useful historical evidence, not a replacement or oracle.

Build/tests: the current application/test build succeeded. All 10 media-labeled CTest suites passed, containing 927 QtTest passed checks including setup/cleanup, with 3 existing skips: external Bento toolkit corpus, external OMF semantic corpus, and case-sensitive folder test. The external toolkit was not available in this run. No sanitizer rerun or live Avid/shared-storage writer/operation/rebuild experiment was performed.

Binary evidence: static inspection only; no proprietary functions were executed. arm64 libameLibrary SHA-256 70b6f2810f53dc044a9b6b2d3f9d3e3c50df40c91fcc263e21a2678752566f9e matches the build cited by existing docs. Independently checked LoadPMR/ReadPmrRec, OMF/AAF MobID readers, Unicode-section handling and exact 3600-second cache comparisons; also checked usage setter/import branches. ReadPmrRec’s UTF8 byte-capacity claim was checked against AvidCore’s function and is supported: the function compares UTF8 byte length against the passed buffer limit before converting. Installed DRUI.xml supplied independent fixed/float and resolution-ID corroboration. These observations establish behavior of this build, not a public historical specification.
'''
positives='''KEEP THESE SUPPORTED DESIGN CHOICES
• PMR magic/byte order; short OMF IDs for legacy base versions;32-byte AAF IDs for version 8; version 1 omission of embedded master/project; separately counted version 16 Unicode replacement. Actual LoadPMR accepts signed versions < 9; that observation does not prove that every negative version is a historical format.
• Dictionary-resolved Bento/OMF property IDs and separation of container revision from object schema. Genuine legacy OMF, audio and MDB fixtures parse successfully.
• Full modern identities, explicit legacy prefix-42 bridging, and separate file/master IDs. Modern bin matching does not fall back to8-byte aliases.
• Selected descriptors and exact rational duration clocks; master/reference duration is not automatically physical file duration. ClipReference and LegacyHeuristic provenance already exist in the model.
• Integer Avid usage versus16-byte standard AAF/MXF usage are separate. LowerLevel alone is not sufficient to identify a precompute. Unknown classifications remain explicit.
• Direct-child managed folders, named ingest folders and workstation layouts are supported.5000 is a practical Avid folder-management recommendation, not a parser magic-number limit. UME/exchange/timeline evaluation are explicit scope decisions.
• Name-derived effects are descriptions after metadata-based precompute classification. Editable names do not establish an effect graph or installed plugin.
• PMR completeness checks exclude incomplete indexes from No Reference conclusions. No Reference describes local PMR membership, not unused media or safe deletion.
'''
plan='''CORRECTION ORDER
1. Fix genuine field errors: F01 fixed-point display; F02 component layouts; F03 preserve geometry. Retain raw bytes and exact values so future interpretation does not require rereading media.
2. Repair typed relationships and ambiguity: F05–F09, F13–F15 and F23. Separate framing, schema validity, graph completeness, field confidence and native-profile qualification.
3. Make metadata merges evidence-aware: F22–F26, then canonicalize operation comparisons and add equivalent OMF identity checks (F30–F31).
4. Replace fabricated mappings and universal text/era assumptions: F10–F12, F17–F20. Unknown means unknown; no new value should be invented to make a column look complete.
5. Correct the current read-policy/cap/rebuild claims (F27–F28,F32), then measure and reduce duplicate metadata I/O (F29).
6. Add genuine specimens for uncovered producer/version/byte-order cases before broadening support. Keep recovery mode available, but visibly weaker than verified fields.

VALIDATION THAT WOULD ESTABLISH THE FIXES
Use before/after tests with raw expected bytes and independently recorded Avid exports/UI observations. Pin both codec ULs with sentinel254; alpha16/RGB/RGBA layouts; stored/sample/display rectangles; unresolved and conflicting typed graphs; repeated PackageUIDs; genuine legacy text/code pages; primary/AMA filename collisions; same-identity true→false timecode and changed editorial metadata. Do not use “the parser previously returned X” as the only expected-value source. Test concurrent database replacements and operation identity over protected handles; these were not exercised here. Maintain a versioned specimen ledger recording producer, schema, byte order, original file hash, expected raw properties, and provenance.
'''
sources=[
 ('Current Avid documentation index','https://kb.avid.com/pkb/articles/en_US/Knowledge/Media-Composer-2026-Documentation','Located the26.8 ReadMe and2026.x Editing Guide; guide downloaded directly and relevant pages extracted locally.'),
 ('Media Composer2026.x Editing Guide',GUIDE,'1320 PDF pages; relevant media-management/OMF/audio/shared-storage sections retained. Ordinary refresh guidance excludes Production Management-managed storage.'),
 ('Avid developer SDK overview','https://developer.avid.com/','AMT/native media qualification and current VC-3/DNx SDK scope; not a public PMR specification.'),
 ('Avid database refresh KB',KB,'Distinguishes standalone database-refresh workflow from managed/shared environments.'),
 ('Import dailies into Media Composer','https://kb.avid.com/pkb/articles/en_US/Knowledge/import-dailies-into-MC','Managed-tree/qualified OP-Atom workflow and practical folder-count guidance.'),
 ('Why audio is stored differently','https://kb.avid.com/pkb/articles/en_US/troubleshooting/en273303','Legacy audio settings and OMFI folders.'),
 ('Unity ISIS media folder structure','https://resources.avid.com/SupportFiles/attach/Unity_ISIS_Media_File_Folder_Structure.pdf','Historical workstation/ingest folder layout; date is2008, not a current-format specification.'),
 ('MediaCentral Production2023.7 best practices','https://resources.avid.com/SupportFiles/attach/MediaCentral_Production/MCPM_2023_7_0_Best_Practices_Guide.pdf','Managed ingest/storage context.'),
 ('Avid supported video formats','https://resources.avid.com/SupportFiles/attach/Avid_Supported_Video_File_Formats.pdf','Linked from2026 index but document is dated2021; linking/import compatibility is not managed-native equivalence.'),
 ('OMF Interchange Specification2.1',OMF,'Original1997 specification,282 PDF pages, retrieved from a mirror. Standard object/locator/geometry source; does not define all private Avid MDB extensions.'),
 ('BBC/libMXF baseline model',BMX,'Actual property/class/type definitions used as an independent schema reference; snapshot copied with hash.'),
 ('BBC/libMXF extensions','https://github.com/bbc/bmx/blob/main/deps/libMXF/mxf/mxf_extensions_data_model.h','Avid/private extension context; retained snapshot.'),
 ('AAF object-model draft','https://aaf.sourceforge.net/docs/aafObjectModel.pdf','Historical model context; a draft is not final evidence of private writer behavior.'),
 ('SMPTE published registry','https://registry.smpte-ra.org/view/published/','Register discovery; not every codec/type entry was individually certified here.'),
 ('Microsoft WAVEFORMATEXTENSIBLE','https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible','Valid bits, storage bits, subtype and channel mask.'),
 ('pyavb reference reader','https://github.com/markreidvfx/pyavb','Reverse-engineered comparison implementation; supplied local source used with compiled extensions disabled.'),
 ('Installed DRUI.xml','/Applications/Avid Media Composer/SupportingFiles/DynamicRelinkUI/DRUI.xml','Local Avid configuration; relink filters are evidence of its matching policy, not automatically container axioms.'),
 ('Installed libameLibrary','/Applications/Avid Media Composer/AvidMediaComposer.app/Contents/MacOS/libameLibrary.dylib','Static arm64 instruction checks with retained addresses/excerpts.'),
 ('Supplied legacy PMRReader','/Users/martymclean/Downloads/python-PMRReader-master/','Not authoritative: Python2-era parser, ignores modern Unicode details and uses assumptions inconsistent with this binary. No proposal to replace production code with it.')]
(E/'source-ledger.json').write_text(json.dumps(sources,indent=2))
text=[intro,positives,plan,'FINDING REGISTER: '+str(len(F))+' entries; priorities concern inventory/format confidence, not emergency severity.\n']
for f in F:
 text+=['\n'+f['id']+' '+f['title']+'\n'+f['status']+' / '+f['priority']+'\n','Current behavior: '+f['issue']+'\n','Verifiable truth / boundary: '+f['truth']+'\n','Correction: '+f['correction']+'\n','Evidence: '+str(f['verification'])+'\n','Sources: '+', '.join(f['sources'])+'\n','Occurrences (current and historical related references):\n']
 for x in f['occurrences']:text.append('  '+x['file']+':'+str(x['line'])+' ['+x['context']+'] '+x['text']+'\n')
text+=['\nSOURCE LEDGER\n']+['  '+name+' — '+url+'\n    '+scope+'\n' for name,url,scope in sources]
(D/'format-audit.txt').write_text(''.join(text))
def h(x):return html.escape(str(x))
body=['<!doctype html><html lang="en"><meta charset="utf-8"><title>MediaMuster format audit ·3 October2026</title><style>body{font:16px/1.55 system-ui;margin:auto;max-width:1100px;padding:34px;color:#182331;background:#f4f6f9}h1{font-size:34px}h2{margin-top:38px}article{background:white;border:1px solid #d9dfe8;border-radius:10px;padding:22px;margin:22px 0}p{white-space:pre-wrap}pre{white-space:pre-wrap;font:13px/1.5 ui-monospace;max-height:480px;overflow:auto;padding:14px;background:#f5f7fa}a{color:#1748a1}table{width:100%;border-collapse:collapse;background:white}td,th{text-align:left;vertical-align:top;border-bottom:1px solid #ddd;padding:9px}.tag{font-size:13px;background:#e9eef5;padding:4px 9px;border-radius:4px}input{padding:12px;width:calc(100% - 26px);font-size:16px;position:sticky;top:0;background:white;border:2px solid #869bb8;border-radius:6px}summary{cursor:pointer}.muted{color:#536275}.banner{background:#e4ecf6;padding:18px;border-radius:8px}</style><h1>MediaMuster: format evidence audit</h1><p class="muted">3 October2026 · read-only investigation ·40 findings and scope entries</p><div class="banner"><b>The readers work on the available corpus, but that is not a format-correctness certificate.</b><p>Two field defects are visible in genuine media: fixed-point labelled Float, and component depths left blank. Other findings concern geometry semantics, ambiguous ownership, recovery confidence, cache/merge policies, unnecessary reads and claims that exceed the evidence.</p></div>']
body+=['<h2>What was investigated</h2><p>'+h(intro)+'</p><h2>Supported behavior worth keeping</h2><p>'+h(positives)+'</p><h2>Correction order and verification</h2><p>'+h(plan)+'</p>']
body+=['<h2>Finding index</h2><input id="search" placeholder="Filter findings by format, title, file, status or text"><table><tr><th>ID</th><th>Finding</th><th>Evidence status</th></tr>']
for f in F:body.append('<tr class="indexrow" data-id="'+f['id']+'"><td>'+f['id']+'</td><td><a href="#'+f['id']+'">'+h(f['title'])+'</a></td><td>'+h(f['status'])+'</td></tr>')
body.append('</table>')
for f in F:
 body+=['<article id="'+f['id']+'"><h2>'+f['id']+' · '+h(f['title'])+'</h2><span class="tag">'+h(f['priority']+' · '+f['status'])+'</span>']
 for label,k in [('Current behavior','issue'),('Verifiable truth / evidence boundary','truth'),('Correction','correction'),('Verification','verification')]:body.append('<p><b>'+label+'</b>\n'+h(f[k])+'</p>')
 if f['sources']:body.append('<p>Primary references: '+', '.join('<a href="'+h(s)+'">'+h(s)+'</a>' for s in f['sources'])+'</p>')
 body.append('<details><summary>'+str(len(f['occurrences']))+' related source, test and documentation occurrences</summary><p class="muted">Historical reviews describe earlier code and may already be superseded. Related lines are an impact inventory, not a claim that each line is a new defect.</p><pre>')
 for x in f['occurrences']:body.append(h(x['file']+':'+str(x['line'])+' ['+x['context']+']\n'+x['text'])+'\n\n')
 body.append('</pre></details></article>')
body.append('<h2>Source ledger</h2><table><tr><th>Source</th><th>Use and limitation</th></tr>')
for name,url,scope in sources:body.append('<tr><td><a href="'+h(url)+'">'+h(name)+'</a></td><td>'+h(scope)+'</td></tr>')
body.append('</table><h2>Evidence files</h2><p>See <a href="evidence/findings.json">structured findings</a>, <a href="evidence/occurrence-inventory.tsv">complete occurrence inventory</a>, <a href="evidence/corpus-summary.json">corpus summary</a>, <a href="evidence/probes/results.jsonl">authored probe outputs</a>, and <a href="evidence/large-evidence-location.txt">raw evidence location</a>. The plain-text report includes every occurrence expanded.</p><script>const search=document.querySelector("#search");search.addEventListener("input",()=>{let q=search.value.toLowerCase();for(let a of document.querySelectorAll("article")){let yes=a.textContent.toLowerCase().includes(q);a.hidden=!yes;document.querySelector(`.indexrow[data-id="${a.id}"]`).hidden=!yes;}});</script></html>')
(D/'format-audit.html').write_text(''.join(body))
print('Findings',len(F),'occurrences',sum(len(f['occurrences']) for f in F),'manifest files',len(manifest));print('report bytes',(D/'format-audit.html').stat().st_size)
