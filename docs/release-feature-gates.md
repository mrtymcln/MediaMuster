# Release feature gates

## v1 media scope

The supported workflow families are Avid-managed MXF OP-Atom and OMF media.
MXF OP-Atom is available by default; OMF remains behind **Enable OMF** as
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

`AvidMediaLayout` supplies common folder and filename rules to scanning and
Rebalance. MXF media lives under positive numbered or workstation-numbered
folders in `Avid MediaFiles/MXF`; only `.mxf` files enter that family. OMF media
lives in `OMFI MediaFiles`, either directly or one level down in a legacy shared
workstation folder; only `.omf`, `.wav` and `.aif` files enter that family.
MXF admission uses that numbered-folder pattern without a `Temp`/`Quarantine`
exclusion list. `Avid MediaFiles/MXF/Quarantined Files` is also a known location:
its MXF contents are scanned, flagged **Quarantined**, and shown by the
Quarantined filter tab. They remain excluded from Rebalance.
OMF workstation folders use plain names, including `Temp` and `Quarantine`;
hidden folders, `Creating` and `Quarantined Files` remain excluded there.
Media file symlinks are excluded. Directory aliases must resolve to a supported
managed location; a quarantine alias cannot grant recursive access to an
ordinary media folder. Rebalance additionally checks that source and destination
directories resolve to the intended MXF root and folder number before dispatch.

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

## Session toggles

The Debug menu enables these features for the current session only. Every launch
starts with all six off; there are no saved preferences to carry into a public
build.

| Debug command | Enabled behavior | Disabled behavior |
| --- | --- | --- |
| Enable OMF | Subsequent scans discover and parse managed OMF essence. Rescan after enabling. | Scans admit MXF essence only and skip OMFI MediaFiles trees, including manually added folders. Turning it off also removes OMF rows already in the table. |
| Enable Precomputes | Shows Type and precompute detail columns, the Precomputes tab, the filter button, and those fields in CSV exports. Shows Special > Filter Precomputes, enabled when scanned media is available and the app is idle. | Hides the Special menu command, toolbar control and fields, clears precompute filters, and resets sorting if its column disappears. Rendered media remains in ordinary scan results. |
| Verify copies | New Copy/Move requests capture the option; transfers compare source and destination checksums after the native copy. | Transfers still check identity, size and storage outcomes, but do not compare full contents. Resume preserves the job's saved option. |
| Enable undo | Makes file-operation Undo available in Edit, with its shortcut. | Hides file-operation Undo, removes its shortcut, and rejects new Undo requests. Normal text-editing Undo still works. |
| Show codec hex | Displays raw codec identifiers where available in place of readable names. | Displays readable codec names. |
| Fusion style | Uses Qt's Fusion widget style. | Uses the style installed at startup. |

The first four commands form one group, followed by the two display options.
The final group is **Rebalance demos**, whose **Small**, **Big** and **Really big**
scenarios use synthetic plans and simulated progress without changing files.

OMF and Precomputes toggles cannot change while scanning or performing a media
operation. Metadata classification and parser implementations remain intact and
tested. Interrupted operations, including an Undo already started in a developer
build, remain recoverable through Unfinished Business; the gate prevents starting
a new Undo.

## Public builds

The Debug menu is controlled by one line in `src/featureflags.h`:

```cpp
inline constexpr bool kDebugMenuEnabled = true;
```

Change `true` to `false`, commit that change with the release, and rebuild:

```sh
cmake --build build --config Release --parallel 4
```

With `false`, the app does not create the Debug menu, so users cannot enable its
gated features. The value is compiled into the app; no saved preference, CMake
option, version number, Git tag, or CI setting changes it. It is also independent
of the compiler's Debug/Release configuration.

Change the same line back to `true` and rebuild to restore the developer menu.
Its feature toggles still start off. Hiding a menu in an already running app does
not disable features that were enabled earlier in that session; this switch
determines whether the menu is created when the app starts.

The operation UI tests read this same constant. Before shipping, run them with
`false` to check that the menu and feature entry points are unavailable; run them
with `true` to check the developer toggles. Rebuild after each change.
