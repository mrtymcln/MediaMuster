# Source Filename URI correction — 7 October 2026

The full-volume comparison exposed a real presentation regression: a source name
could become blank because the same name appeared as both `Apple%20ProRes...mov`
in an MXF import URL and `Apple ProRes...mov` in its recorded native import path.
Those are equivalent filename observations, not conflicting filenames.

Read-only probes of the actual shared ProRes sample and EDIT audio confirmed
this combination. An EDIT title's name similarly became `ADR%20TEMPLATE...PICT`
instead of `ADR TEMPLATE...PICT` when only the URL supplied it. The
[original projected observations](evidence/source-filename-uri-2026-10-07.json)
retain the exact paths, owning object handles and property names.

The MXF projector now derives the filename of `NetworkLocator.URLString` with
[Qt's QUrl::fileName](https://doc.qt.io/qt-6/qurl.html#fileName), decoding URI
escapes once. Native/UNC paths keep their literal characters, so a real filename
containing `%20` remains `%20`; a URL containing `%2520` decodes to the same
literal filename. Raw URL bytes and recorded SourcePath observations are unchanged.
This correction adds no new selection priority and does not equate different
full paths or resolve volume-name aliases.

The comparison's newly populated R3D name was also checked: it comes from an
explicitly linked import descriptor's locator. That sample is additional
recorded information which the former parser missed.

Regression cases cover equivalent spaces, UTF-8 URL characters, literal percent
characters, and truly different filenames which must still remain conflicting.
