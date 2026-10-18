# Bella

Reserved for MediaMuster's new, independent scanner/parser. The user chose to
finish the requirements review before writing Bella's readers or parsers. This
folder currently contains documentation only; there is no Bella runtime code or
build target.

Bella will use C++17 and the project's pinned Qt 6.5.3 building blocks, applying
the attached C++ guidance through scoped ownership, type and bounds safety,
compact value records and explicit lifetimes. It must not be produced by
patching or adapting OldScanner.

The app continues using [OldScanner](../oldscanner/README.md). Bin Filter stays
outside Bella's scope. A switch to Bella requires the user's explicit approval.

[Project Bella](../../Project%20Bella/README.md) records the approved scope,
staging and PMR requirements. MDB and the remaining source requirements come
before replacement implementation.
