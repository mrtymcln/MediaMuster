# Corpus header archive

512 KB header slices of every MXF in the ground-truth test corpus, plus the
folder's real Avid databases. Captured from
`/Users/Shared/AvidMediaComposer/Avid MediaFiles/MXF/1/` (Media Composer
2025) in rounds: rounds 1–2 on 2026-07-31 (435 slices, `msmFMID.pmr` /
`msmMMOB.mdb`), round 3 on 2026-08-05 (359 slices, `msmFMID_round3.pmr` /
`msmMMOB_round3.mdb` — the live folder's media was replaced between
rounds, so each round keeps its own databases for the PMR/MDB ground-truth
joins). 794 slices total. The original full-size media may no longer exist
— these slices ARE the corpus now.

**Why 512 KB is enough:** `MxfParser::parseHeader` never reads more than
512 KB (Avid allocates 256 KB or 512 KB for the header partition), so a
slice is parser-equivalent to its original. Verified at capture time: a
field-by-field sweep diff of all 435 slices against the live files showed
zero differences.

**Ground truth convention:** every clip was named in Avid after the codec
menu entry that created it — so each file's embedded clip name states what
the file *is*. Any clip-name/resolved-codec mismatch in a sweep join is a
codec-table bug by construction. Projects are named `<name>_<raster>_<rate>`
(with deliberate MacRoman-hostile characters: ë, ß) and cover UHD
23.976/24/25/29.97/30/50p and 1080i50 across DNxHR, DNxHD, DNxUncompressed
(all depths incl. float and 2.14), ProRes, XAVC HD/4K, AVC-Intra HD/4K,
AVC Long GOP, DVCPro HD, J2K, XDCAM, H.264 proxy, PCM and MP2 audio, plus
timeline effect renders. Known gap: no drop-frame project.

`corpus_manifest.txt` is the machine-readable answer key: one `ROW|` line
per file with the parsed codec/rate/duration/clip fields as of capture
(format produced by `review/probes/mxfsweep`; regenerate after parser
changes rather than trusting it blindly).

`tst_mxfparser`'s `archived_corpus_all_parses_with_no_unknowns` walks this
folder and fails if any slice stops parsing or resolves to an
"unknown variant" label — the tripwire that keeps the dictionary complete.
