# Updated upstream IPA implementation and build ledger

> Execute inline using superpowers:executing-plans. User authorized completion
> through a built IPA and a durable record of differences from upstream.

**Goal:** deliver a local IPA from the integrated sources, with fresh OpenGL
plugins, preserved fork fixes, and enough provenance to resume future updates.

**Architecture:** extend the existing native CI with a pinned graphics build and
source rebuilds for changed PE components. Package native outputs preferentially;
the checksum-verified compatibility IPA supplies unchanged i386 modules and local
Microsoft runtimes only. Keep generated payloads outside Git.

**Tech stack:** macOS/Xcode CI, Wine/FEX/DXMT, Mesa/MoltenVK, Python packaging.
**Spec:** the user's 2026-10-10 request and constraints in AGENTS.md/docs/FORK.md.

## Tasks and completion contract

- [ ] Pin and build Mesa 25.0.7 plus all seven patches and MoltenVK 1.4.2;
  preserve source/hash metadata and licenses. Native CI must succeed.
- [ ] Rebuild the updated Wine modules, FEX PE modules, DXMT PE and native D3D12;
  list exact rebuilt outputs. Preserve unchanged compatibility files explicitly.
- [x] Test and change the packager so fresh graphics and PE outputs cannot be
  overwritten by the old compatibility IPA; fail if required fresh graphics are
  missing. Validate package member hashes and provenance.
- [ ] Run fresh native app/JIT helper build and host regressions. Resolve actual
  failures with recorded causes; review build/packaging changes independently.
- [ ] Download artifacts, create and verify local IPA, record SHA-256, source
  pins, build run and outstanding device checks in docs/FORK.md and docs/UPDATING.md.

## Decisions and evidence

- Starting parent: 8488b3d; main upstream 48f9764; c-gow e711351;
  Wine fork 2233aa82 (upstream 257f271c). No upstream histories are rewritten.
- Reuse only the verified, unchanged LLVM recipe/artifact from run 38030334310.
  Runtime and graphics are rebuilt because their inputs changed.
- Thin reservations remain experimental and off by default. No device issue is
  called fixed solely because compilation or host tests pass.
- FEX source is not edited. Build adapters belong to the parent repository.
- IPA, vendor runtimes, toolchains, logs, generated DLLs and libraries are local
  artifacts only. Source audit must pass before every commit.

## Build and review log

- Run 38032306400 (`0a89852`): Mesa/MoltenVK and native DXMT succeeded.
  Runtime failed during FEX ARM64EC configure: its native CPU probe imports
  Python packaging and reads Linux `/proc/cpuinfo` on macOS. Set generic/none
  CPU tuning, matching the existing WoW64 cross-build.
- Independent review also found missing FEX iOS host compile/link flags and
  DXMT's required toolchain symlink. Both corrected in `a25754c`, without
  editing FEX source. These were real build/runtime settings, not cosmetic.
- Initial five packaging tests reproduced stale graphics and i386 overwrite
  behavior. After correction they pass. Review then reproduced accepting
  wrong dependency pins and incomplete receipts. Five more tests were red
  before `bfe4319` and all ten are now green. Unsigned graphics are verified
  before Xcode; signed hashes and app source bind the final app receipt.
- Follow-up independent review found no remaining actionable issues. Both
  receipt modes passed valid synthetic inputs and rejected tampered graphics.
  Native CI is still required; synthetic tests are not build/device evidence.
- Run 38032866055 builds `bfe4319`, reusing the unchanged LLVM artifact from
  38030334310. Host workflow 38032867939 checks the same commit.
- Host workflow 38032867939 completed successfully: downloads, cloud-and-ui,
  and upstream-runtime. The local compatibility donor checksum was rechecked:
  `0f0afd36d6713155b25fda999a097ea543786bc0c3087b376a95b0861f8c87ca`.
  Its graphics are ignored. No Microsoft payload is present in commits/CI.
