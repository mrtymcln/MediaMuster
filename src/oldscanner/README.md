# OldScanner

This is the retained scanner/parser. Normal macOS and Windows application
targets use `BellaAppScanner` and the independent Bella implementation. The
`OldScanner` facade and scan pipeline remain for legacy-mode regression tests
and the `mediaengine_compare` probe; shared location types are also used by the
Bella app adapter.

- `oldscanner.h` / `oldscanner.cpp`: location preparation, background work,
  progress, cancellation and result publication.
- `mediaengineadapter.h` / `mediaengineadapter.cpp`: formatting selected facts
  for the legacy table/CSV presentation and capturing operation checks.
- `mediaengine/`: retained discovery, PMR/MDB/MXF/OMF/WAV/AIFF reading,
  projection, metadata selection and scan coordination. These internals retain
  namespace `MediaEngine` and their existing behavior.

Bin Filter's active AVB reader/reference resolver and shared reading types remain
in `../mediaengine/`. File operations still use the retained MXF/OMF readers and
projectors for live identity checks. Bella also calls the shared compression-name
lookup under `mediaengine/`; it does not use these source readers for scanning.
Shared application value types and utilities remain in `src/`.

Bella is an independent C++17 / Qt 6.5.3 scanner/parser under `../bella/`, with its
own discovery, source readers, matching and compact results. Keep these ownership
boundaries when changing either implementation.
See [Project Bella](../../Project%20Bella/README.md#approved-staging) and the
[ownership map](../../docs/media-engine.md).
