# Disposable verification of findings 2, 3 and 5

Diagnostic branch only. Production files under `src/` are unchanged from
`a52ec0cf7bdebd20fe807a462bc908df8ed06023`. No product fixes or Media Composer
warnings are included.

The PMR probe asserts the current reported defect and its scanner consequence.
A green diagnostic run means the expected current behavior was reproduced; it
does not mean the production behavior is correct.

The operation probe exercises the production engine using disposable temporary
files. It prints file locations, leftover directories, partials and cancellation
results. It forces a copy-based Move to exercise final original removal on a
single available volume.

The Windows Trash harness separately tests the production deletion path with
synthetic files. Edge conditions must be demonstrated before their outcomes can
be treated as evidence. A timeout or unestablished Recycle Bin configuration is
inconclusive. Hosted Windows Server tests do not establish interactive behavior
on every Windows desktop version, removable device or NEXIS installation.

The workflow has read-only repository permissions and no publishing/signing
steps. Evidence is uploaded as diagnostic artifacts. It runs only on the named
diagnostic branch.
