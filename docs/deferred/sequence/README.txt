Deferred sequence work — saved before approved V1 removal

Base commit: f8f8c2bec39870928680a07ac4b229e6f4502bc5

The patch restores the complete pre-removal working changes and research evidence onto a clean checkout of that base. It contains earlier bin/source-track work too. Do not apply it wholesale onto the cleaned V1 checkout. For later sequence development, inspect and extract the desired changes.

Verified with git apply --cached --check against a temporary index at the base commit. manifest.json records the original file hashes and patch hash.
