# OldScanner

This is the existing scanner/parser, isolated with its behavior preserved. The
app uses the `OldScanner` QObject facade until the user explicitly approves a
switch to Bella.

- `oldscanner.h` / `oldscanner.cpp`: location preparation, background work,
  progress, cancellation and result publication.
- `mediaengineadapter.h` / `mediaengineadapter.cpp`: formatting selected facts
  for the existing table/CSV model and capturing operation checks.
- `mediaengine/`: existing discovery, PMR/MDB/MXF/OMF/WAV/AIFF reading,
  projection, metadata selection and scan coordination. These internals retain
  namespace `MediaEngine` and their existing behavior.

Bin Filter's AVB reader/reference resolver and shared reading types remain in
`../mediaengine/`. File operations still use the retained MXF/OMF reader interfaces
for live identity checks. Shared application value types and utilities remain in
`src/`.

Do not adapt this implementation into Bella. Bella is a new, independent
C++17 / Qt 6.5.3 scanner/parser. Complete its requirements review before writing
replacement parsers; switching the app requires the user's explicit approval.
See [Project Bella](../../Project%20Bella/README.md#approved-staging) and the
[ownership map](../../docs/media-engine.md).
