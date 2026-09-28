# Release feature gates

## v1 media scope

The supported workflow families are Avid-managed MXF OP-Atom and OMF media.
MXF OP-Atom is available by default; OMF remains behind `FeatureFlags::kOmfEnabled` as
requested for the public release. Enabling OMF does not depend on the version of
Media Composer that created the files. The OMF gate controls product availability;
the enabled feature is held to the same v1 correctness and testing requirements
as the public MXF workflow.

Media Composer's OP1a workflow under `Avid MediaFiles/UME` is deferred. These
trees are excluded from volume scans and explicitly added paths, including
canonical aliases. This is a managed-folder scope rule, not a new guarantee that
every `.mxf` encountered outside UME has been structurally certified as OP-Atom.
Adding UME/OP1a later will require its own discovery, metadata and destination
handling rather than treating UME as another MXF OP-Atom folder.

For managed OMF audio, the observed AIFF-C filename suffix is `.aif`, and WAVE
uses `.wav`. A missing `.aiff` alias is not a v1 requirement for that workflow.
`.omf` is not restricted to video: the reader also handles supported OMF audio
descriptors under that suffix. Other suffixes, including `.sd2`, are ignored by
the filename allowlist just like unrelated documents. There is no SDII-specific
parser or exclusion path. Historical filename research is archived in the
[20 September review](reviews/2026-09-20-media-scope/REVIEW.md); retained Media
Composer code does not define MediaMuster's supported scope.

## Managed media locations

Automatic volume scans look for immediate `Avid MediaFiles/MXF` and enabled
`OMFI MediaFiles` roots. The documented system-drive bases remain supported.
Manually added copies may live anywhere, provided their internal Avid structure
is intact. Add the media root, an eligible media subfolder, or the directory
directly containing the roots; the scanner does not search arbitrary descendants.
A bare `MXF` tree or an arbitrary folder containing PMR/MDB files is not enough.

`AvidMediaLayout` supplies folder and filename rules to scanning and Rebalance.
MXF scans read `.mxf` files directly inside ordinary child folders of
`Avid MediaFiles/MXF`. This includes numbered, workstation-numbered and named
ingest folders. Hidden folders and `Creating` are excluded; nested folders are
not scanned. OMF media
lives in `OMFI MediaFiles`, either directly or one level down in a legacy shared
workstation folder; only `.omf`, `.wav` and `.aif` files enter that family.
Names such as `Temp` and `Quarantine` do not exclude an ordinary MXF folder.
`Avid MediaFiles/MXF/Quarantined Files` is a known location:
its direct MXF contents are scanned, flagged **Quarantined**, and shown by the
Quarantined filter tab. Subfolders are not scanned. Quarantined media remains
excluded from Rebalance.
OMF workstation folders use plain names, including `Temp` and `Quarantine`;
hidden folders, `Creating` and `Quarantined Files` remain excluded there.
Media file symlinks are excluded. Directory aliases must resolve to a supported
managed location; an alias cannot change the resolved folder's quarantine status.
Rebalance accepts only positive numbered or workstation-numbered MXF folders.
It checks that source and destination directories resolve to the intended MXF
root and folder number before dispatch. Other named MXF folders remain available
for scanning and ordinary media operations.

Avid's [MediaCentral Production Management best practices](https://resources.avid.com/SupportFiles/attach/MediaCentral_Production/MCPM_2023_7_0_Best_Practices_Guide.pdf#page=130)
describe named ingest folders in a Media Indexer workflow. MediaMuster supports
reading their accessible files and folder databases; it does not connect to the
MediaCentral server or its catalogue. Its direct-child discovery rule does not
claim to reproduce Media Indexer's storage configuration or recursive indexing.

Rebalance uses a **5,000-file target**, matching Avid's documented
[folder performance threshold](https://kb.avid.com/pkb/articles/en_US/Knowledge/Avid-MediaFiles-MXF-folder-size-limit).
This is not a scan limit: folders containing more files are still read.

The managed layout selects the family. There is no mandatory per-file
Operational Pattern probe and no attempt to authenticate the authoring app.
Media Composer and compatible third-party media are treated alike. PMR/MDB
records are read first; missing, unreadable, stale or incomplete records trigger
the existing header fallback. A current complete database record can avoid
opening the media entirely. This is a managed-workflow scope rule, not a
guarantee that a deliberately misplaced or renamed file is format-certified.

OMF bin matching preserves legacy ID bytes. Preserve-structure transfers use
`OMFI MediaFiles` for OMF alongside `Avid MediaFiles/MXF/<folder>` for MXF.
Rebalance accepts MXF only, retains its stricter destination-name and mutation
checks, and carries both file and master identities to the operation engine.

## Build flags

`src/featureflags.h` contains independent compile-time switches:

```cpp
inline constexpr bool kDebugMenuEnabled = true;
inline constexpr bool kOmfEnabled = false;
inline constexpr bool kPrecomputesEnabled = false;
inline constexpr bool kUndoEnabled = false;
```

| Flag | Enabled behavior | Disabled behavior |
| --- | --- | --- |
| kOmfEnabled | Scans discover and parse managed OMF essence. | Scans admit MXF essence only and skip OMFI MediaFiles trees, including manually added folders. |
| kPrecomputesEnabled | Adds precompute detail columns and CSV fields, the Precomputes tab, toolbar filter and Special > Filter Precomputes. | Hides precompute details and filtering; rendered media remains in ordinary scan results. |
| kUndoEnabled | Makes file-operation Undo available in Edit, with its shortcut, for eligible recorded jobs. | Hides file-operation Undo, removes its shortcut and rejects new Undo requests. Normal text-editing Undo still works. |
| kDebugMenuEnabled | Shows Fusion style and Rebalance demos in the Debug menu. | Omits the Debug menu. |

OMF, Precomputes and Undo have no Debug menu controls. Their availability is
independent of whether the Debug menu is included. Fusion style starts off on
each launch. Rebalance demos use synthetic plans and simulated progress.

Interrupted operations, including an Undo already started in another build,
remain recoverable through Unfinished Business even when Undo is disabled.
Metadata classification and parser implementations remain intact and tested.

## Public builds

Set the desired flags, commit the change with the release, and rebuild:

```sh
cmake --build build --config Release --parallel 4
```

These values are compiled into the app. No saved preference, CMake option,
version number, Git tag, or CI setting changes them. They are also independent
of the compiler's Debug/Release configuration. Set `kDebugMenuEnabled` to `false`
to omit the developer menu; select each feature's availability separately.

The operation UI tests check startup configuration against these constants and
verify that the removed feature toggles are absent. Rebuild after changing flags.
