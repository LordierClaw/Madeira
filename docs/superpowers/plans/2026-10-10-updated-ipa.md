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

- [x] Pin and build Mesa 25.0.7 plus all seven patches and MoltenVK 1.4.2;
  preserve source/hash metadata and licenses. Native CI must succeed.
- [x] Rebuild the updated Wine modules, FEX PE modules, DXMT PE and native D3D12;
  list exact rebuilt outputs. Preserve unchanged compatibility files explicitly.
- [x] Test and change the packager so fresh graphics and PE outputs cannot be
  overwritten by the old compatibility IPA; fail if required fresh graphics are
  missing. Validate package member hashes and provenance.
- [x] Run fresh native app/JIT helper build and host regressions. Resolve actual
  failures with recorded causes; review build/packaging changes independently.
- [x] Download artifacts, create and verify local IPA, record SHA-256, source
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
- Run 38032866055 passed host checks, graphics and DXMT, but FEX PE configure
  selected Homebrew's macOS `fmt::fmt` and failed with `IMPORTED_IMPLIB not set`.
  Both PE adapters now disable system discovery for fmt/range-v3/unordered_dense
  and use the pinned submodules. Wine and PE refresh are separate workflow steps
  so subsequent failures identify their stage clearly.
- Run 38033438999: FEX ARM64EC compilation now reaches link, which fails on
  C++ runtime EC symbols with default ThinLTO. A local mutex/shared-mutex/thread
  DLL probe using the same llvm-mingw 20260421 reproduces the failure with
  `-flto=thin` (exit 1) and links without LTO (exit 0). This matches
  [LLVM issue 168469](https://github.com/llvm/llvm-project/issues/168469).
  Disable LTO for ARM64EC, as already done for WoW64; retain normal Release
  optimization. Add CMake link commands and remaining Wine configure logs to
  failure artifacts. No workaround changes FEX emulation source.

Minimal linker reproduction (write outside the repository; no device execution):

```cpp
#include <mutex>
#include <shared_mutex>
#include <thread>
extern "C" __declspec(dllexport) void test() {
    std::recursive_mutex m; m.lock(); m.unlock();
    std::shared_mutex s; s.lock_shared(); s.unlock_shared();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
```

Compile with the pinned `arm64ec-w64-mingw32-clang++`:
`-shared -O3 -std=c++20 -static -flto=thin probe.cpp -o probe.dll` fails on
EC C++ runtime symbols. Removing only `-flto=thin` succeeds. Do not switch
the FEX adapter back to LTO merely because a new toolchain configures.

## Completion evidence

- Run **38034074592**, source **283ecb913804f223a8d5214f7ac4587125a7e523**:
  graphics, runtime, DXMT and app/JIT helper succeeded. LLVM was validly reused.
  Both FEX PE targets, Wine aarch64/i386 modules, DXMT PE, D3D12 and LuaJIT built.
- Downloaded graphics verified locally: iOS ARM64, Mesa minimum iOS 17 and
  MoltenVK minimum iOS 15, correct install names, 7 patch hashes and both
  unsigned hashes. App CI separately verifies signed-plugin receipts.
- Runtime receipt has 66 verified file hashes / 63 mandatory PE paths.
  Architectures: 4 aarch64, 47 ARM64EC/Wine x64-header, 11 i386, 1 x64 LuaJIT.
  All 42 partial-i386 missing module/import pairs have donor modules available.
- Final IPA **0.1.3 build 22**, **159096870 bytes**, SHA-256
  **a0b09734d864a5bff28f2c8ac7600c24da43647abe3dca8dece5d712b86b11c8**.
  Local packager completed successfully, including ZIP integrity/member hashes,
  source/pin/patch/required-output receipts, native precedence and DataFix/UI checks.
- Native archive SHA-256:
  `f7adfc80c24556d6d67e4f0fc6575dd6fb2e5a9a74a60dfedd14fcd7870dd47c`.
  App entitlements parsed from its Mach-O signature include all three requested
  JIT/memory flags. Packager preserves that app binary byte for byte.
- IPA, archives, manifest, checksum and logs remain outside Git. No device is
  connected; build 22 requires normal sideload signing and iPhone/game retesting.
