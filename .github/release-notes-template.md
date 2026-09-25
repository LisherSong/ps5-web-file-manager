<!--
Release notes template — keep them SHORT.

One bullet per user-visible change, one line each. A release page is a
"What do I get?" list, not a design document or a test report.

  * `## What's Changed`, then a flat bullet list. No tables, no sub-sections,
    no design rationale, no test counts, no per-file detail.
  * Start each bullet with an area label: **extract** / **ui** / **fix** /
    **build** / **docs**.
  * Describe what the user can now DO, in their words. "encrypted archives
    work" beats "implemented a virtual ISeekInStream that splices a
    pseudo-header, the archive and the decrypted header".
  * Close with one line for the artifact (name · size · sha256 · machine) and
    one line for the fork marker / rollback build.
  * Implementation notes, measurements, verification runs and known gaps go to
    CHANGELOG.md and docs/ — link them if needed, never inline them.
-->

## What's Changed

- **area**: one line, the user-visible outcome
- **area**: another one
- **fix**: what used to be broken, and is not any more

`your-artifact.elf` · NNN,NNN bytes · sha256 `…` · x86-64 (`e_machine 0x003e`)

One line for the fork marker and/or the rollback build, with a link.
