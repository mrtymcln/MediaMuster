# Release feature gates

The Debug menu enables these features for the current session only. Every launch
starts with all three off; there are no saved preferences to carry into a public
build.

| Debug command | Enabled behavior | Disabled behavior |
| --- | --- | --- |
| Enable OMF/OMFI | Subsequent scans discover and parse legacy OMF essence. Rescan after enabling. | Scans admit MXF essence only and skip OMFI MediaFiles trees, including manually added folders. Turning it off also removes legacy rows already in the table. |
| Enable Undo | Makes file-operation Undo available in Edit, with its shortcut. | Hides file-operation Undo, removes its shortcut, and rejects new Undo requests. Normal text-editing Undo still works. |
| Enable Precomputes | Shows Type and precompute detail columns, the Precomputes tab, the filter picker, and those fields in CSV exports. | Hides these controls and fields, clears precompute filters, and resets sorting if its column disappears. Rendered media remains in ordinary scan results. |

OMF/OMFI and Precomputes toggles cannot change while scanning or performing a media
operation. Metadata classification and parser implementations remain intact and
tested. Interrupted operations, including an Undo already started in a developer
build, remain recoverable through Unfinished Business; the gate prevents starting
a new Undo.

## Public builds

Configure a public build with the Debug menu omitted:

```sh
cmake -S . -B build -DMEDIAMUSTER_DEBUG_MENU=OFF
cmake --build build --config Release --parallel 4
```

This option is independent of the compiler's Debug/Release configuration. It
defaults to `ON` for local development. CI explicitly uses `OFF` for release tags
starting with `v1.` or a higher major version, and `ON` for `v0.` tags and branch
builds. Public builds have no menu command that enables these features. Omitting
the `buildDebugMenu()` call likewise leaves the new gates off.

To restore the developer menu locally, configure with
`-DMEDIAMUSTER_DEBUG_MENU=ON` and rebuild. The toggles still start off.
