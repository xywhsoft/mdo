# Historical builtin upgrade fixture

`legacy-builtin-home-0.7.0.zip` contains the default Agent, eleven commands,
two Skills and installation receipts from the published 0.7.0 directory release
(`b934d96fde273164e7f13bab530a210a5e2d959f`). Every builtin file was compared
with that commit and the corresponding unmodified installation receipt.

It contains no account, credentials, projects, sessions, user prompts or memory.
All enabled flags are normalized to true; the regression sets opt-outs itself.
The receipt hashes are intentional: old manifests have no logical `id`, so a
newly generated fixture cannot exercise the actual upgrade failure.

On POSIX, the test recalculates the native mode/size portion of installation
receipts after extraction. Archive hashes and every historical file byte remain
unchanged; this represents an unmodified installation on that platform.
