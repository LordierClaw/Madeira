# Working on this fork

- This is the maintained checkout of `LordierClaw/Madeira`, branch `madeira-fixes`.
  `origin` is our fork; `upstream` is `willfaust/Madeira`; `c-gow` is an OpenGL reference.
- Wine changes belong in the `wine` submodule, `LordierClaw/Madeira-wine`, branch
  `madeira-fixes`. Commit and push Wine first, then commit the parent gitlink.
- Preserve upstream copyright and license notices. Credit imported c-gow source.
- Commit source, tests and build instructions only. Never add Microsoft VC runtime
  DLLs/installers, IPA files, generated DLLs/EXEs/libraries, toolchains, game files,
  signing/pairing material, user configuration or device logs. Existing upstream
  tracked binaries are inherited; do not update them in this source-only workflow.
- Enable the repository hooks with `git config core.hooksPath .githooks`.
  Run `python tools/check-source-commit.py --staged` before every commit.
- Read `docs/FORK.md` for provenance, rebuilding and validation status.
  Mouse ClickFix was confirmed on iPhone; keyboard ClickKeyFix still needs device
  confirmation. Controller and minor voice-audio issues remain under investigation.
- Diagnose input at the relevant layer before changing behavior. Do not enable
  speculative controller defaults or claim a fix based only on source inspection.
- Do not modify FEX or propose upstream FEX contributions without first reading
  its contribution policy.
