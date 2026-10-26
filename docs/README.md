# Documentation index

## Start here

- [Project Bella](../Project%20Bella/README.md): fresh scanner and parser effort;
  finish requirements before implementing new C++17 / Qt 6.5.3 code. The current
  app uses [OldScanner](../src/oldscanner/README.md) until an explicitly approved
  switch to Bella. Bin Filter stays unchanged.
- [Project Copland](../Project%20Copland/README.md): active V1 scope, confirmed
  decisions and remaining work. Proposals and open questions are not approval.
- [How MediaMuster works](current-behaviour.md): current behaviour in plain English,
  including scan scope, labels, selection, file operations and recovery.
- [Architecture map](architecture.md): who does what in the code and where to look.
- [Release feature gates](release-feature-gates.md): compile-time feature switches and public builds.
- [Contributor guide](CONTRIBUTING.md): naming, formatting, build lists and validation.

These are the starting points for current behaviour and responsibilities. Update
them when those behaviours change. Comments beside code should explain the current
rule and its reason; dated investigations belong in the records below.
Copland's confirmed decisions take precedence where they revise earlier Canon
requirements. Use the behaviour guide and implementation to check what is built.

## Technical references and validation

- [18 October startup/support check](../Project%20Copland/startup-support-acceptance-2026-10-18.md):
  native startup, diagnostic retention, Reveal Diagnostics and disposable crash/recovery ordering.
- [18 October local workflow comparison](../Project%20Copland/local-workflow-comparison-2026-10-18.md):
  scan, bin filtering, export and rescan, with measured timing/memory limits.
- [18 October disposable file-operation check](../Project%20Copland/file-operation-acceptance-2026-10-18.md):
  Copy/Move/Delete/Rebalance, fresh-process Undo and interrupted-job recovery.
- [Native file-operation validation](file-operations-native-api-validation.md): tested behaviour and outstanding platform checks.
- [Parser compatibility](parser-compatibility.md) and [engine terminology](media-engine.md).
- [MediaEngine cleanup verification](media-engine-cleanup-2026-10-11.md): sole-engine integration and the genuine MPEG OMF ownership correction.
- [AVB parser](avb-parser.md) and [AVB error examples](avb-error-examples.md).
- [Usage-code identification](usage-code-identification.md): provenance for Avid classification rules.
- [PMR reader evidence](../Project%20Canon/fresh-pmr-reader-2026-10-03.md): framing, text and index coverage.
- [Bella PMR research](reviews/2026-10-18-bella-pmr/REVIEW.md): native reader/writer,
  six live databases and the hypothetical MDVx scanner with four concurrent jobs.
- [PMR reading and memory](reviews/2026-10-18-bella-pmr/memory-lifetime.md):
  current/MDVx buffering and retention, with agreed single-capture lifetime
  and whole-set fallback for Bella.
- [Bella scanner design](reviews/2026-10-26-bella-scanner-design/REVIEW.md):
  agreed four-reading-job total, one organiser/shared reader team and result/Cancel
  rules, plus compact matching facts retained through matching then released.
  Measurement without memory controls and changed-read/manual-rescan handling
  and responsive cleanup are agreed. Current progress/warning delivery is retained;
  current numeric timecode Duration sorting is retained. All eight registered
  choices are resolved; new concrete implementation choices still need check-ins.
- [Bella MDB research](reviews/2026-10-18-bella-mdb/REVIEW.md): native typed values
  and text encodings, MDVx enrichment, six actual databases and the agreed four-MDB-job,
  read-once/extract/release workflow.
- [MDB text read/write](reviews/2026-10-18-bella-mdb/text-encoding.md): explicit
  UTF-8 pairs, typed attributes, legacy conversion and agreed per-field fallback.
- [MDB identities and PMR matching](reviews/2026-10-18-bella-mdb/identities-and-pmr.md):
  complete object IDs, verified legacy conversion and catalogue-qualified lookups.
- [MDB Clip Name](reviews/2026-10-18-bella-mdb/clip-name.md): native name lookup,
  missing-master recovery and the agreed single-candidate Bella rule.
- [MDB Project](reviews/2026-10-18-bella-mdb/project.md): native owner/key paths,
  file-versus-source evidence and agreed missing/blank fallback.
- [MDB Original Bin](reviews/2026-10-18-bella-mdb/original-bin.md): selected-master
  references, paired bin names, MDVx omission and agreed compact retention.
- [MDB Source Path](reviews/2026-10-18-bella-mdb/source-path.md): recorded import
  path findings preserved; Bella keeps physical Filename/File Path and excludes
  recorded Source Path.
- [MDB Kind](reviews/2026-10-18-bella-mdb/kind.md): physical-file sound/picture
  descriptors, MDVx's differently named usage column and agreed six-type lookup.
- [MDB Duration](reviews/2026-10-18-bella-mdb/duration.md): physical count/rate,
  native audio clocks, MDVx omission and agreed exact retained values/display maths.
- [MDB Frame Rate](reviews/2026-10-18-bella-mdb/frame-rate.md): stored integer
  pairs, Avid's typed rate interpretation, current MediaMuster comparison and
  agreed reuse of Bella's Duration rate, numeric sorting and MDB-first priority
  with fallback only from an independently read header.
- [MDB Duration clock](reviews/2026-10-18-bella-mdb/duration-clock.md): native
  physical descriptor conversion, 121 matching headers, agreed interpretation
  before elapsed-duration arithmetic and the accepted audio display design.
- [MDB drop-frame numbering](reviews/2026-10-18-bella-mdb/drop-frame.md): native
  flag layouts, qualified linked-source example and agreed missing/damaged
  handling without changing the precise Duration data.
- [MDB damage handling](reviews/2026-10-18-bella-mdb/damage-handling.md): native
  MC/MDVx and current MediaMuster comparison; agreed value/association isolation
  within a trusted database and rejection of untrustworthy shared structure.
- [MDB versions](reviews/2026-10-18-bella-mdb/versions.md): ordinary native MDB
  paths, current generic-format coverage, fresh 14-path inventory and agreed
  release-one legacy MDB baseline.
- [Bella MXF headers](reviews/2026-10-21-bella-mxf/REVIEW.md): native/current
  reading paths, MDVx early definition filters, ten actual headers and proposed
  selective metadata extraction; the subsequent
  [MXF fallback revision](reviews/2026-10-21-bella-mxf/fallback-fields.md) permits
  Duration when usable databases are unavailable and keeps Original Bin blank.
- [MXF identities](reviews/2026-10-21-bella-mxf/identities.md): full package IDs,
  native reader selection and agreed qualified file/master ownership,
  normalization, compact retention and unavailable-value rules, approved
  22 October 2026.
- [MXF Clip Name](reviews/2026-10-21-bella-mxf/clip-name.md): native master-name
  reading, UTF-16BE text, ten matching MDB/header names and agreed compact
  Unicode retention with blank on unavailable MXF fallback.
- [MXF Project](reviews/2026-10-21-bella-mxf/project.md): native header/attribute
  readers, thirteen actual comparisons, sharp-s Unicode evidence and agreed
  header/own-file-only lookup with one retained value; master/source recovery
  omitted from the rule approved 22 October 2026.
- [MXF Kind](reviews/2026-10-21-bella-mxf/kind.md): native physical descriptor
  import, MDVx's distinct usage column, current property-based inference and
  thirteen header checks; agreed small explicit Audio/Video/unknown lookup,
  approved 22 October 2026.
- [MXF Duration](reviews/2026-10-21-bella-mxf/duration.md): own physical
  count/rate, separate audio/display clocks, MDVx clip-versus-file distinction,
  current DF/zero issues and thirteen headers; compact exact retention and
  qualified display rules approved 22 October 2026, with DF semicolons/NDF colons.
- [MXF Frame Rate](reviews/2026-10-21-bella-mxf/frame-rate.md): native physical
  rate versus codec-wrapper conversion, current numeric/label behavior, eight
  video observations and two interlaced controls; exact-rate reuse approved
  22 October 2026.
- [MXF audio Sample Rate](reviews/2026-10-21-bella-mxf/audio-sample-rate.md):
  dedicated AudioSamplingRate, native import and MDVx ordinary omission,
  current exact fraction/late kHz display and five reused audio observations;
  compact preservation and unavailable-value rules approved 22 October 2026.
- [MXF Bit Depth](reviews/2026-10-21-bella-mxf/bit-depth.md): class-specific
  scalars/layout, coded representations, actual A8/native-padding compatibility
  and current mixed/partial strings; compact numeric interpretation and bounded
  padding compatibility approved 22 October 2026.
- [MDB audio Sample Rate](reviews/2026-10-18-bella-mdb/audio-sample-rate.md):
  concrete native sources, embedded summaries, 1,513 actual 48 kHz files and
  agreed exact retention with one source per audio class.
- [MDB Bit Depth](reviews/2026-10-18-bella-mdb/bit-depth.md): own descriptor
  scalars/summaries, parallel RGBA arrays, qualified special codes and agreed
  verified-rule reuse with numeric retention; uncommon coverage remains open.
- [Bella Compression preservation](reviews/2026-10-18-bella-mdb/compression-preservation.md):
  keep the current system, catalogue, naming rules, aliases and tests unchanged.
- [MXF Compression](reviews/2026-10-21-bella-mxf/compression.md): fresh native
  comparisons, reused thirteen-header facts and the equivalent input contract
  for preserving the existing names, refinements, fallbacks and source preference.
- [Actual MDB/MXF Compression comparison](reviews/2026-10-21-bella-mxf/compression-comparison.md):
  current OldScanner execution on 2,411 same-file pairs; 2,397 exact matches and
  14 differences, with all differing files and a full CSV.
- [Compression consistency](reviews/2026-10-21-bella-mxf/compression-consistency.md):
  preserve the naming module; the verified J2K rate-form correction is approved,
  and the narrow MXF-only alpha rule was approved separately on 22 October 2026.
- [Alpha MXF identification](reviews/2026-10-21-bella-mxf/alpha-identification.md):
  positive uncompressed RGBA markers in all ten partition/Preface headers,
  native fallback/key traces and the agreed narrow MDB-independent naming rule.
- [MXF Resolution](reviews/2026-10-21-bella-mxf/resolution.md): native geometry,
  nested crop/field/proxy rules, all 2,411 paired selected values matching, and
  agreed fresh helper/numeric storage, approved 22 October 2026.
- [MDB Resolution](reviews/2026-10-18-bella-mdb/resolution.md): native geometry,
  all 899 video descriptors, current crop/field/proxy behavior and agreed
  numeric storage with selective MDB decoding.
- [MXF Precompute Category](reviews/2026-10-21-bella-mxf/precompute-category.md):
  native object-wrapper/picture-track predicate, 11 raw MDB/MXF agreements and
  compact retention with blank unavailable values, approved 22 October 2026.
- [MXF Effect Category](reviews/2026-10-21-bella-mxf/effect-category.md):
  native effect registration, ordinary MDVx omission and current selected-name
  lookup; shared Bella integration approved 22 October and eleven captured-name checks.
- [MXF Effect](reviews/2026-10-21-bella-mxf/effect.md):
  registered native name versus selected-name recognition, canonical/custom/empty
  output rules and reuse of the same combined catalogue lookup, accepted 23 October.
- [MXF Type](reviews/2026-10-21-bella-mxf/type.md): private/standard usage,
  35 owner-qualified raw examples, compact classification and strict present-UID
  handling approved 22 October 2026, plus an honest assessment of unmeasured benefits.
- [MXF damage handling](reviews/2026-10-21-bella-mxf/damage-handling.md): native
  failure paths, current partial projection and agreed value/relationship/header
  boundaries, approved 23 October, with physical files retained.
- [MXF layouts and navigation](reviews/2026-10-21-bella-mxf/layouts-and-navigation.md):
  ordinary/native route boundaries, ten fresh partition observations and agreed
  closed-complete initial-header scope without automatic recovery, approved 23 October.
- [Bella OMF media reading](reviews/2026-10-23-bella-omf/REVIEW.md): native/current
  routes, five fresh TOC observations and agreed needed-value extraction;
  first-release version/text, identity, fields and damage baseline agreed through
  25 October; native WAVE/AIFF behaviour subsequently preserved separately.
- [OMF versions and text](reviews/2026-10-23-bella-omf/versions-and-text.md):
  container/schema distinction, native text paths and real Mac/OMF2 examples;
  agreed ordinary Avid version baseline and explicit Unicode/legacy decoding,
  approved 23 October 2026.
- [OMF identities](reviews/2026-10-23-bella-omf/identities.md): complete file/master
  IDs, owner qualification, native comparison limits and five direct observed
  relationships; OMF selection rules approved 24 October 2026.
- [OMF Clip Name](reviews/2026-10-23-bella-omf/clip-name.md): associated-master
  component name, native/current text handling and five rechecked actual names;
  exact-field/MacRoman/blank/compact-retention rules approved 24 October 2026.
- [OMF Project](reviews/2026-10-23-bella-omf/project.md): native/current Project
  owners and five fresh attribute/link observations; file-first/immediate-source
  fallback, exact keys and typed text rules approved 24 October 2026.
- [OMF Original Bin](reviews/2026-10-23-bella-omf/original-bin.md): native/current
  bin-reference names, 82 surveyed files and 35 legacy-only master associations;
  selected-master UTF-8/separate-MacRoman/blank/compact-retention rules accepted
  24 October 2026.
- [OMF Kind](reviews/2026-10-23-bella-omf/kind.md): native descriptor families,
  MDVx's different usage column and current fixed lookup; agreed preservation
  of qualified OMF Audio/Video classes, with explicit compatibility limits,
  approved 24 October 2026.
- [OMF Duration](reviews/2026-10-23-bella-omf/duration.md): own descriptor
  count/rate, native audio-summary nuance, current exact arithmetic and five
  fresh timing observations; agreed shared interpreter/formatter, qualified
  audio display clock/DF, compact retention and no extra header-opening trigger,
  approved 24 October 2026.
- [OMF Frame Rate](reviews/2026-10-23-bella-omf/frame-rate.md): own descriptor
  SampleRate, MDVx master-rate difference, current raw fraction/presentation and
  reused OMF metadata; agreed shared interpreter/late labels/numeric sorting,
  blank audio and no automatic interlace halving, approved 24 October 2026.
- [OMF audio Sample Rate](reviews/2026-10-23-bella-omf/audio-sample-rate.md):
  native PCMA/MPGA dedicated frequencies, AIFD/WAVD summaries, current mixed
  observations and two fresh own-summary fields; agreed exact per-class
  authority, late kHz labels, shared small helpers and independent Duration timing,
  accepted 25 October 2026.
- [OMF Bit Depth](reviews/2026-10-23-bella-omf/bit-depth.md): own audio/picture
  fields, precision versus storage, parallel RGBA arrays and TIFF scope;
  agreed shared format helpers, numeric depth/late labels and blank unsupported
  values, approved 25 October with existing timing and Compression naming preserved.
- [OMF Compression](reviews/2026-10-23-bella-omf/compression.md): preserve the
  current resolver/names and supply equivalent own coding, legacy identifiers,
  naming geometry/clock and shared audio-summary facts through a small adapter;
  acquisition details approved 25 October, with no extra header-opening reason.
- [OMF Resolution](reviews/2026-10-23-bella-omf/resolution.md): own legacy
  rectangles, crop offsets/field heights, MDVx's absent raster column and three
  reused picture examples; small shared geometry/typed adapter, selective Sampled
  reading and numeric width/height retention approved 25 October.
- [OMF Type](reviews/2026-10-23-bella-omf/type.md): own selected-master numeric
  usage, native/MDVx classification differences and five saved Media masters;
  1/7 mapping, compact shared enum/helper and blank Unknown approved 25 October,
  with optional precompute details kept separate.
- [OMF Precompute Category](reviews/2026-10-23-bella-omf/precompute-category.md):
  native exact direct import-object/picture-track predicate, MDVx omission and
  current pure helper; selective optional decoding, compact enum, blank cells and
  explicit filter Unknown approved 25 October, without settings-body reads.
- [OMF Effect Category](reviews/2026-10-23-bella-omf/effect-category.md): preserve
  the shared selected-name catalogue behaviour and current unknown/ambiguity
  labels; common post-selection integration with Effect approved 25 October,
  no additional OMF category property/graph/open and no retained Sequence.
- [OMF Effect](reviews/2026-10-23-bella-omf/effect.md): agreed reuse of the same
  combined lookup, canonical English names for known tokens and verbatim custom
  tokens, current unknown/blank rules and no extra OMF reads; approved 25 October.
- [OMF damage handling](reviews/2026-10-23-bella-omf/damage-handling.md): native
  container/value/object failures, MDVx validation gaps and current selective
  preservation; agreed complete-framing gate and local dependency isolation,
  retaining physical rows and independent sources, approved 25 October.
- [Native WAVE/AIFF preservation](reviews/2026-10-25-bella-audio/REVIEW.md):
  current format rules, interpretation, useful outputs and availability handling
  preserved on 25 October in fresh Bella code. Extra FVER gates and further
  per-field redesign withdrawn; prior research retained as reference.
- [Final Bella source priorities](reviews/2026-10-25-bella-source-priorities/REVIEW.md):
  agreed fixed field table and one header read for identification/database
  fallback, preserving timing restrictions, Compression preference and physical
  inventory; approved 26 October. Brief scanner design follows.
- [MDB Type](reviews/2026-10-18-bella-mdb/type.md): selected-master numeric usage,
  native role qualification, MDVx's import override and agreed compact
  Media/Precompute classification, with blank unavailable values.
- [MDB Precompute Category](reviews/2026-10-18-bella-mdb/precompute-category.md):
  native direct-master predicate, 107 rendered and 64 titles/matte physical rows,
  MDVx omission and agreed selective reading/compact category retention, with
  blank unavailable values.
- [MDB Effect Category](reviews/2026-10-18-bella-mdb/effect-category.md): native
  registration knowledge, MDVx omission and agreed preservation of the current
  name/catalogue labels with simpler Bella integration.
- [MDB Effect](reviews/2026-10-18-bella-mdb/effect.md): current canonical-name and
  custom-token display rules, with agreed preservation through the already
  agreed shared Bella lookup integration.
- [Bella Effect Sequence exclusion](reviews/2026-10-18-bella-mdb/effect-sequence-exclusion.md):
  omit its column, separate CSV field and stored value; retain the Effect/Category
  token-boundary rules and original Clip Name.
- [PMR Project display](reviews/2026-10-18-bella-pmr/project-display.md): the actual
  Project cache, chooser, encoding conversion and Qt drawing path.
- [Precompute details](effect-details-preview.md): classification, display and filter design.

Validation records describe the source revision, platform and scenarios actually
checked. They do not establish that a later build or another storage system passes.
Current native-copy and journal behavior is described in
[the behaviour guide](current-behaviour.md#copy-move-and-delete).

## Design history and evidence

These documents preserve decisions, proposals, implementation follow-ups and past
findings. A statement of intent or an old finding is not necessarily current
behaviour. Start with the guide above and follow the source links when checking it.

- [Project Canon](../Project%20Canon/README.md): earlier RAM metadata requirements,
  evidence/association design, real-media findings and dated acceptance checks.
  Its retention proposals are historical where Copland has revised them.
- [Native bin fallback](native-bin-fallback.md).
- [Operation recovery and cleanup](operation-recovery-cleanup.md).
- [Historical reviews](reviews/README.md): dated findings, research and validation summaries.

Keep dated reviews as written reports under `docs/reviews/`. Record useful results
and their limits in the report; do not check in temporary probes, generated output,
logs, screenshots or copies of source code.

Real media fixtures under `tests/fixtures/` remain active regression inputs. The
app's effect catalogue is maintained in `src/effectcatalogue.cpp`; its
[provenance and recognition rules](avid-effects-catalogue.md) are documented separately.
Future catalogue updates require renewed binary inspection and comparison with
the application catalogue.
