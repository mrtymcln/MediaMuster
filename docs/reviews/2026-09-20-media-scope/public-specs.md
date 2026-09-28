# Primary-source format research — 20 September 2026

Scope: Relevant original specifications, Avid documentation and engineering white papers were inspected. This is not a claim to have read every historical document or to certify all Media Composer versions. No public normative PMR/MDB byte-layout specification was located in this search. Avid's OMF specification is primary authored material even though the accessible copy is hosted by a third party.

## Main conclusions

- MXF versus OMF is a file/storage-format distinction, not a dividing line between files produced before and after a year or a Media Composer version. Later versions continued supporting both, and video and audio format settings can differ.
- Standard Avid managed MXF files use `.mxf` in `Avid MediaFiles/MXF/<number or client.number>`; legacy managed video uses `.omf` and audio can use `.wav`/`.aif` in `OMFI MediaFiles`. These conventions are strong routing hints, not structural proof about renamed or arbitrary files.
- Actual MXF identification is the MXF Header Partition Pack key and supported Operational Pattern/Essence Container metadata. OMF uses a different object/container model (Bento); an ordinary WAV is not automatically an OMF container simply because it is in a legacy workflow. OMF descriptors can refer to externally stored raw media.
- MobIDs/UMIDs identify material and connect metadata; their shape is not an unambiguous container-format label. Legacy-style Avid identifiers can occur in MXF files. A legacy ID in a database cannot by itself prove a media file is OMF.
- Standard OMF `omfi:UID` is 96 bits / 12 bytes. MXF package UMIDs are 32 bytes. An 8-byte PMR identifier is therefore an Avid cache serialization detail, not the general OMF identifier specified by the OMF standard.
- Using PMR/MDB as fast metadata indexes is sensible, provided code keeps heuristic classification separate from actual format validation and handles ambiguity conservatively.

## Sources and verified findings

### 1. Avid Media Composer Adrenaline v1.6.5 ReadMe (revision history through July 13, 2005)

https://resources.avid.com/SupportFiles/attach/README_MCAdrenaline_1_6_5.pdf

Printed pp. 7–9, PDF pages 7–9: the retained **New for Avid Media Composer Adrenaline v1.5.1** feature table identifies MXF support and conversion between OMF and MXF. Printed p. 29 also documents creating OMF instead of MXF as a workaround, proving coexistence. This directly verifies introduction in the 1.5 generation; this file does not establish the exact first release date.

Historical limit: September 2004 / v1.5 is supported by a contemporary trade report and secondary timelines, but no first-party release announcement fixing that exact date was retrieved. Prefer saying **the Adrenaline 1.5 era**, or explain that the exact date is less certain and irrelevant to classification. Do not invent a 2003/2004 cutoff.

### 2. Avid KB: Why are my Audio media files stored differently from my Video media files? (March 28, 2023)

https://kb.avid.com/pkb/articles/en_US/troubleshooting/en273303

Media Creation's Format tab selects OMF or MXF video; HD projects offer only MXF. Audio Project separately chooses the audio file format, labelled with OMF or MXF. Different settings explain simultaneous `Avid MediaFiles` and `OMFI MediaFiles` directories, with WAV/AIFF in the latter. Avid recommends moving away from OMFI due to its limitations. This disproves any simple **new Media Composer version implies every file is MXF** rule.

### 3. Avid Video Satellite Guide (Pro Tools 10, 2011)

https://resources.avid.com/SupportFiles/attach/Pro_Tools/10.0/ENGLISH/Video_Satellite_Guide.pdf

Printed p. 34, PDF page 38: directly documents `.omf` video, `.wav` or `.aif` OMF-wrapped audio, and `.mxf` MXF files, with their usual `OMFI MediaFiles` versus `Avid MediaFiles` locations. Printed p. 39 / PDF page 43 repeats copy rules. The guide describes these as usual storage conventions, not a binary-identification algorithm.

### 4. Avid Pro Tools ISIS Guide (Pro Tools 10, 2011)

https://resources.avid.com/SupportFiles/attach/Pro_Tools/10.0/ENGLISH/Pro_Tools_ISIS_Guide.pdf

Printed p. 4: MXF local media uses `Avid MediaFiles/MXF/1`, also reads 2, 3, etc.; shared storage uses `Avid MediaFiles/MXF/client.1`. OMF uses `OMFI MediaFiles`. This is storage layout, not an encoding specification for either database.

### 5. OMF Interchange Specification v2.1 (Avid, September 18, 1997)

https://www.cubase.it/wp/wp-content/uploads/2014/12/omfspec21.pdf

Primary Avid-authored specification; accessible mirrored copy. Printed pp. 3–4 (PDF 15–16) describes the Bento container. Appendix B printed p. 222 (PDF 234) defines `omfi:UID` as 96 bits interpreted as three 32-bit values, with an organization/application code first. Printed p. 217 (PDF 229) defines WAVD and the `OMFI:MDFL:IsOMFI` property: true means media is in an OMFI file; false means raw data in a separate file. Printed p. 200 (PDF 212) describes source mobs; printed p. 164 (PDF 176) ties media data to its source mob by shared MobID. These are semantic IDs and descriptors, not PMR/MDB file specifications.

### 6. SMPTE ST 377-1:2019, Material Exchange Format — File Format Specification

https://pub.smpte.org/latest/st377-1/st377-1-2019.pdf

Approved November 28, 2019. §6.6, pp. 36–37 bounds possible run-in to less than 65536 bytes and requires decoders to locate the partition prefix. §6.7, p. 37 explains the minimum decoder: locate Header Partition Pack, inspect Operational Pattern and Essence Container labels, and reject unsupported forms. §7.2.1, p. 41 explicitly makes the 16-byte Header Partition Pack key the MXF identifier. Tables 4 and 6, pp. 38 and 42 define the key and allowed status values. Annex B.1, p. 118 defines Package UID as a required 32-byte UMID. §6.4.2, p. 36 requires big-endian multibyte metadata values. These rules support structural identification rather than extension-only certainty.

### 7. SMPTE ST 390:2011, MXF Specialized Operational Pattern OP-Atom

https://pub.smpte.org/pub/st390/st0390-2011_stable2016.pdf

Approved March 31, 2011; revision of 390M-2004; stable cover adds one PDF page to printed page numbers. §1 printed p. 3 / PDF 4 and §5 printed p. 4 / PDF 5 specify a single essence track and essence container and separately stored tracks. §5 sets run-in to zero bytes. §7.3 printed pp. 6–7 / PDF 7–8 identifies OP-Atom by its operational-pattern UL. §8.2.2 printed p. 8 / PDF 9 cautions that a single track can still carry multichannel audio. Thus don't simplify OP-Atom to universally one audio channel per file; that is a common Avid workflow, not the entire standard.

### 8. SMPTE ST 330:2011, Unique Material Identifier

https://pub.smpte.org/doc/st330/20110823-pub/st0330-2011.pdf

Annex D, printed/PDF p. 25 documents an OMF-era commercial UMID-generation form, deprecated for new designs. Its first 16 bytes start `06.0C.2B.34.02.05.11.01...`; the material number includes fixed `06.0E.2B.34.7F.7F`, a company prefix, major and minor values. This concerns generation of identifiers. It does not define a reliable test of whether a containing file uses the OMF or MXF container.

### 9. BBC R&D WHP 155, File-based Production: Making It Work In Practice (September 2007)

https://downloads.bbc.co.uk/rd/pubs/whp/whp-pdf-files/WHP155.pdf

Authors Stuart Cunningham and Philip de Nier report their own implementation/interoperability work. PDF p. 7, **Supporting legacy post-production equipment**: older Avid MXF import needed AAF-style metadictionary and object-directory extensions. They also changed **MXF Package UIDs** to resemble Avid's old AAF SDK generation so an OMF export would work in Pro Tools 5.3.1; Avid preserved the IDs across export. This is direct engineering evidence that old identifier style can live in MXF. Therefore the proposition **legacy-looking MobID means OMF file** is unsound. PDF p. 8 describes both Panasonic P2 and Avid using MXF OP-Atom with different metadata needs.

### 10. Avid What's New in Pro Tools 2019.10

https://resources.avid.com/SupportFiles/PT/Whats_New_in_Pro_Tools_2019.10.pdf

Printed p. 5, PDF p. 8: both Media Composer and Pro Tools produce SMPTE IDs in WAV files. It distinguishes the legacy Pro Tools UMID chunk from the OMF chunk ID seen in WAV files supplied with an AAF from Media Composer. Useful evidence that WAV extensions alone do not tell us their Avid identity metadata or provenance, and that these audio mechanisms persisted long after 2004.

### 11. Avid developer program — Avid Media Toolkit and Transfer DET API

https://developer.avid.com/

Current page read September 20, 2026. AMT creates/reads compliant Avid OP-Atom plus matching AAF metadata. Transfer DET API description mentions media wrapped with MXF or OMF metadata. This corroborates the managed-media architecture but provides no normative PMR/MDB layout. Do not claim Avid documents our specific private-database decoding based on this page.

### 12. Avid KB: Refreshing Media Databases (August 1, 2026)

https://kb.avid.com/pkb/articles/en_US/Troubleshooting/Refreshing-Media-Databases

Explains PMR/MDB identify and locate media and are regenerated by Media Composer. Names `msmMMOB.mdb` and `msmFMID.pmr` inside MXF numbered folders. The names alone therefore cannot be taken as signs of legacy OMF. Older Avid Effects Guide printed p. 209 shows the same names in an `OMFI MediaFiles` directory: https://resources.avid.com/SupportFiles/attach/fxguide.pdf .

### 13. AMWA/Omneon white papers inspected for the physical explanation

- **Encoding Data into MXF files: BER and KLV encoding**, pp. 1–5: https://www.amwa.tv/_files/ugd/f66d69_c72d6a0c30c94f7eaed294dcc9326c2e.pdf . Explains identifying keys, encoded lengths, values, and primer mapping of local tags. Helpful explanation; use normative SMPTE for exact requirements.
- **The Structure of an MXF File: The Physical View**, especially pp. 1–2: https://www.amwa.tv/_files/ugd/f66d69_9427fa940f6c4e14be6b987f72d4d286.pdf . Explains partition layout and metadata versus essence. Some broad generalizations are less exact than the normative spec, so not a basis for declaring a parser fully conformant.
- **A Quick Tour of Wrappers and MXF** (2012), pp. 1–2: https://www.amwa.tv/_files/ugd/f66d69_e86d445c6c4a4cb4bede5f95e800f944.pdf . Separates a wrapper/container from a codec. MXF tells how data and metadata are organized; codec is separate.
- **Avid Viewpoint: The Promise of AS-02** (September 9, 2011), pp. 2–4: https://www.amwa.tv/_files/ugd/f66d69_dfe2776c75874acda9143e3db4367b15.pdf . Avid-authored explanation of multi-essence OP-1A and component-based workflows. Relevant context, not evidence about PMR/MDB internals.

## Limits and implications for comparing the application

Code can correctly use extensions and managed-folder context to decide which parser to try, then validate container content. If a fast scan intentionally trusts a matching fresh database instead of opening every media file, describe that honestly as **database-backed metadata classification**, not as proving each file's binary format. Actual MC version cannot be reliably inferred merely from ID widths or extensions.

An all-legacy-ID test for renamed folders may be useful as a narrow fallback, but neither the MXF standard nor OMF standard gives it authority as a container discriminator. In particular, test a renamed folder holding Avid-managed WAV/AIFF with modern 32-byte IDs and mixed ID families, in addition to MXF files with legacy-shaped IDs. Full format and compatibility claims require representative media and differential results against actual Media Composer; documentation plus static binary inspection alone cannot establish universal equivalence.

Unavailable: Avid's old **MXF Unwrapped** PDF and its linked Internet Archive copy were not retrievable by the web tool. Equivalent core claims were checked against official SMPTE, Avid and AMWA material instead. No claim was made to read that unavailable paper.
