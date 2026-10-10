# Fork build workflow (updated 2026-10-10)

Use [UPDATING.md](UPDATING.md) and [upstream-state.json](upstream-state.json)
for the maintained source pins, fork differences and update checklist. The
build records below preserve the earlier IPA history; the current workflow
also builds graphics and the refreshed PE set described below.

[Native app build passed](https://github.com/LordierClaw/Madeira/actions/runs/37581572994) for source commit
`dbcd1cef656db048de9dfc27ec6b37b99a71ea42`. LLVM and native dependencies came
from successful jobs in runs `37578363960` and `37578838131`; their recipes
and submodule revisions were checked before reuse. The final run built and
linked the Debug app and JIT helper and verified their ad-hoc signatures.

Local packaging also passed: `Madeira-0.1.3-UIUX-DLC-Cloud-Landscape.ipa`,
version 0.1.3, build 14, 154,151,199 bytes. SHA-256:
`9222ab37da049d4c74c8e16faba16cf22ad6ebc4487c24648e8f4511aa26a8c4`.
The final package retained `get-task-allow`, `allow-jit` and the increased-memory
entitlement. ZIP/member hashes, the new UI/input markers and rebuilt DataFix
loader checks passed. iPhone execution has not been verified for this build.

The follow-up [native build for `6dba36c`](https://github.com/LordierClaw/Madeira/actions/runs/37647743698)
adds corrected DLC transfer estimates and landscape only during play.
`Madeira-0.1.3-ProgressFix-LandscapePlaying.ipa` is version 0.1.3, build 15,
154,177,219 bytes, SHA-256
`ea085bfdee6180e3c4f721803f08909dd6e3b44536cb017c6022d43475922fa5`.
Local package integrity, new native code/UI markers and JIT entitlements were
verified. The DataFix loader is byte-identical to build 14. Native runtime and
LLVM inputs are unchanged; [regression checks](https://github.com/LordierClaw/Madeira/actions/runs/37647743607)
passed, including combined DLC transfers and orientation transitions. Physical
iPhone validation of this follow-up is pending.

The maintained `ui-ux` branch uses `.github/workflows/ios-build.yml` and
`build/ci/*.sh`. The original September build notes below are a historical
record, not the current fork's submodule availability status. All pinned
submodules needed by this workflow can now be fetched from their public forks.

The workflow builds LLVM 15's static shader-compiler components, FEX's native
iOS libraries, crypto/media/fonts/pairing, Wine's native side, ARM64EC
`ntdll.dll` and `opengl32.dll`, Dock, DXMT, and the Debug app/JIT helper. Native
libraries and DXMT use Xcode 26.3; the app uses Xcode 27.0 because the inherited
StikJIT framework was built with Swift 6.4. DXMT's pinned Metal intrinsics do
not compile with the newer Metal compiler, so its shader job stays separate.
The app's own Metal effects use its Xcode 27 toolchain.

The pinned FEX fork has PE-only diagnostic references in its Apple build.
`build/ci/fex-native-diagnostics.h` supplies inactive PE counters and marks the
Windows memory-attribute query unavailable. `FEXNativeDiagnostics.c` reports
no rpmalloc snapshot because FEX's Apple configuration disables that allocator.
These Madeira adapters affect optional reports only; they do not enable PE
hooks, replace allocation or alter atomic emulation. The FEX source is unchanged.
The Wine native build separately probes an SDK-specific resource counter and
marks it unavailable when the SDK lacks it.

Run the **iOS native build** workflow on `ui-ux`. Leave `llvm_run` and
`native_run` empty for a complete dependency build. To reuse successful
artifacts from earlier runs, supply their run IDs; the workflow compares the
relevant source recipes and submodule commits before accepting them. Cached
DXMT output is keyed by its source and compiler recipes. Artifacts expire
after seven days; rerun dependency jobs if they are no longer available.

The native artifact excludes separately supplied Microsoft runtimes. Download
`Madeira-native-app` and package locally with an explicitly supplied compatible
IPA using `tools/package-local-ipa.py`:

```sh
python3 tools/package-local-ipa.py \
  --native-app Madeira-native-app.tar.gz \
  --source-commit "$(cat source-commit.txt)" \
  --compat-ipa /path/to/local-compatibility.ipa \
  --compat-sha256 <verified-sha256-of-that-file> \
  --output /path/to/Madeira-UIUX.ipa
```

That local overlay contributes unchanged i386 Wine modules and previously
verified VC runtime files. The native archive supplies the app, JIT helper,
fresh graphics and rebuilt PE modules; rebuilt i386 files take precedence.
The two Wine ARM64EC runtime implementations (`msvcp140.dll`,
`vcruntime140.dll`) are retained. Other unchanged inherited modules remain,
so this is not a rebuild of every DLL. Keep local compatibility inputs and
the final IPA outside commits and CI uploads.

The 2026-10-10 pipeline adds `build/ci/graphics-ios.sh`: checksum-pinned Mesa
25.0.7 with all seven patches and MoltenVK 1.4.2. Its cache includes all
graphics recipes/patches in its key. `build/ci/pe-refresh.sh` rebuilds the
updated ARM64EC Wine builtins/XAudio2, FEX ARM64EC/WoW64, WoW64 native DLLs,
changed Wine i386 modules, DXMT PE, D3D12 and LuaJIT GC64. Exact mandatory
outputs are in `docs/upstream-state.json`; the runtime receipt lists actual
outputs and hashes. OpenGL is never taken from the compatibility IPA.

The FEX ARM64EC adapter must select `FEX_IOS_HOST_BUILD` and the
`FEX_IOS_HOST` C/C++/ASM defines. CPU tuning uses generic/none rather than a
Linux `/proc/cpuinfo` probe. DXMT's cross file requires the documented
`dxmt/toolchains` symlink. These are parent build adapters; FEX source is
unchanged. See [FORK.md](FORK.md) for thin reservations, which remain off.

The packager checks ZIP integrity and all member hashes, new UI/input markers,
loader alignment/padding and DataFix markers, required rebuilt outputs and
their pins, graphics patches, app source commit and signed plugin hashes.
`tools/build-receipts.py` verifies unsigned graphics before Xcode uses them
and records signed hashes after signing. The packager writes an IPA checksum
and provenance manifest; its ten synthetic archive tests run in host CI.
CI ad-hoc-signs the native app with its JIT
entitlements; the local overlay invalidates the resource seal. Re-sign the
final IPA through the usual sideload tool. No Apple signing identity or
provisioning profile is included. Successful compilation does not establish
that JIT, graphics, input or Steam work on an iPhone; device checks are separate.

---

# Building Madeira from a clean checkout (reproducibility record, 2026-09-16)

This is the "scripts to control compilation and installation" record the
LGPL relink obligation depends on (docs/LICENSING.md). Each step says
whether it has been re-executed from a clean checkout. A fresh recursive
clone of the repository at commit 8a8cabe was tested on 2026-09-16 (with
the submodule URLs redirected to the local forks, since nothing is pushed):
the app target does NOT build from the clone alone, because the inputs
marked "not in the repository" below are absent. Two further findings:
`app/Madeira/x86_64-vcruntime` is required by the project but ignored, and
the submodule commits (FEX, its nested rpmalloc fork, wine branch
`madeira-lgpl`, dxmt) exist only locally: the forks named in `.gitmodules`
(all under github.com/willfaust) do not yet carry them, so a recipient's
recursive clone fails at the first submodule until every fork is pushed. This document is the
remediation; steps marked UNVERIFIED have not yet been re-run from scratch.

## Inputs that are not in the repository

| Input | Why absent | How to obtain | Verified from clean |
|---|---|---|---|
| `toolchains/llvm-mingw-20260421-ucrt-macos-universal/` | 122 MB third-party toolchain | `llvm-mingw-20260421-ucrt-macos-universal.tar.xz` from https://github.com/mstorsjo/llvm-mingw/releases/tag/20260421, SHA-256 `bd85a3975723815cef28dbbd2ca2cb0c926f6b348a12a0453f39f7af273cb3f7`, extracted under `toolchains/` | tarball hash recorded; download UNVERIFIED |
| `toolchains/llvm-project/` + `toolchains/llvm-ios-build/` + `toolchains/llvm-host-build/` | LLVM built for iOS (hours) | upstream llvm-project at commit `8dfdcc7b7` ("[libc++] Fix memory leaks when throwing inside std::vector constructor"); configure `llvm-ios-build` with `-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_BUILD_TYPE=Release -DLLVM_HOST_TRIPLE=arm64-apple-ios17.0 -DLLVM_DEFAULT_TARGET_TRIPLE=arm64-apple-ios17.0 -DLLVM_TARGET_ARCH=host -DLLVM_TARGETS_TO_BUILD= -DLLVM_ENABLE_PROJECTS= -DLLVM_BUILD_TOOLS=Off -DLLVM_INCLUDE_TESTS=Off -DLLVM_ENABLE_ZLIB=Off` (values read back from the existing CMakeCache); a host build for tablegen lives in `llvm-host-build` | recipe reconstructed; UNVERIFIED |
| `research/GPTK/Metal Shader Converter 4.0 beta 2.pkg` | Apple installer, 30 MB, licence-bound | Apple developer downloads; SHA-256 `1acc33c87ea663933df89721a998d066106685473020bcbe007cee7a16155734` (pinned in `build/madeira-d3d12/deps.sh`). Only needed to REBUILD the converter fetch; the library itself is tracked | n/a |
| `app/Madeira/x86_64-vcruntime/` | Microsoft Visual C++ 2015-2022 x64 runtime DLLs (concrt140, msvcp140*, vcamp140, vccorlib140, vcruntime140*), redistributable under Microsoft's terms, not under this repository's licence | extract from Microsoft's `vc_redist.x64.exe` (or copy from `C:\Windows\System32` of a licensed Windows install) into that folder | UNVERIFIED |
| `build/wine-mono/wine-mono-11.0.0/` (optional) | Wine Mono, the .NET Framework runtime Wine's mscoree loads (not Microsoft code; licences in `build/wine-mono/COPYING` and `THIRD-PARTY-NOTICES.md`), 41.6 MB download, 228 MB unpacked | `bash build/wine-mono/fetch.sh` downloads `wine-mono-11.0.0-x86.tar.xz` from https://dl.winehq.org/wine/wine-mono/11.0.0/ and checks its SHA-256; `--source` also fetches the matching source tarball. Version and hashes are pinned in `build/wine-mono/pin.sh`, which does not follow `WINE_MONO_VERSION` in `wine/dlls/mscoree/mscoree_private.h`. The Xcode phase "Bundle Wine Mono" runs `build/wine-mono/bundle.sh`, which copies it into `Madeira.app/wine-mono` without the `lib/mono/*-api` reference assemblies and patches the bundled `mscorlib.dll`: **a dirty hack** (4 IL bytes: `GC.Collect` with `GCCollectionMode.Optimized` returns, for Terraria), pinned to the exact file by SHA-256 so the build stops on any other mscorlib; TODO: replace it with a Wine Mono built from source or an upstream fix (see the TODO in `bundle.sh`). Without the folder the app builds without Mono, and .NET Framework programs fail with "Wine Mono is not installed". **Release builds leave Wine Mono out** (2026-10-06): the packaging step deletes `Madeira.app/wine-mono`, and the app downloads it from WineHQ on first use (setup or Settings › .NET Framework, `app/Madeira/WineMono.swift`) | fetch + bundle run on the development machine 2026-10-05; not from a clean checkout |
| A free Apple ID; StikDebug or a pairing file plus LocalDevVPN | signing and JIT runtime requirements | see `docs/JIT.md` | n/a |

## Native build chains (all in the repository)

Run in this order after the inputs above are in place. Outputs are
git-ignored and consumed by the app project.

1. `build/gnutls-ios/build.sh`: GMP 6.3.0, Nettle 3.10.1, GnuTLS 3.8.9 from
   the tracked tarballs in `build/gnutls-ios/src` (SHA256SUMS there) ->
   `app/Madeira/lib{gmp,nettle,hogweed,gnutls}.a` (these four outputs are
   also tracked). Verified: built on the development machine; not re-run
   from a clean checkout.
   `build/ffmpeg/build.sh`: FFmpeg 7.1.1 in an LGPL-only configuration (WMA,
   MPEG audio and PCM decoders; mp3/wav/mov demuxers; no H.264/HEVC/AAC),
   built from the tracked, unmodified release tarball in `build/ffmpeg/src`
   after verifying it against `build/ffmpeg/src/SHA256SUMS` -> headers in `toolchains/ffmpeg-ios/include`
   (read by `build/ntdll-unix/build.sh` for winegstreamer's unix side) and
   `app/Madeira/lib{avformat,avcodec,swresample,avutil}.a` (ignored; the app
   target links them together with VideoToolbox, CoreMedia, CoreVideo,
   AudioToolbox and CoreFoundation). The configure arguments are the ones the
   port was built and device-tested with on the WSL toolchain; the macOS form
   of the script is UNVERIFIED.
2. FEX (submodule, branch ios-port-2607):
   - `FEX/build-ios`: `build/fex-ios/build.sh` (same options as the development CMakeCache) -> `FEX/build-ios/FEXCore/Source/lib{FEXCore,FEXCore_Base,JemallocLibs}.a` and the `External/{cephes,fmt,SoftFloat-3e,xxhash}` archives. UNVERIFIED from clean.
   - `FEX/build-arm64ec`: `build/fex-arm64ec/build.sh` (configures with `FEX/Data/CMake/toolchain_mingw.cmake` and the recorded options on first run, builds target `arm64ecfex`, copies `Bin/libarm64ecfex.dll` to `app/Madeira/arm64ec-windows/xtajit64.dll`). The build step was verified this session; the first-run configure in the script is reconstructed from CMakeCache and UNVERIFIED.
3. Wine (submodule, branch madeira-lgpl):
   - unix side: `build/ntdll-unix/build.sh`, `build/wineserver/build.sh`,
     `build/win32u-unix/build.sh` -> `app/Madeira/lib{ntdll_unix,wineserver,win32u_unix}.a`. Verified on the development machine.
   - PE side: `build/wine-pe/build-ntdll.sh` (configures `wine/build-arm64ec` with `--enable-archs=arm64ec --without-x --disable-tests --enable-winegstreamer` on first run, builds `dlls/ntdll`, strips, pads to SizeOfImage + 0x50000, copies to the app). Other PE modules: `build/wine-pe/build-modules.sh <name>...` (same tree; it builds each module's DLL target `dlls/<name>/arm64ec-windows/<name>.dll`, strips it with `--strip-debug` like every shipped builtin and installs it into `app/Madeira/arm64ec-windows/`, or into `$DEST`). Building the DLL target rather than `make -C dlls/<name>` is also what winegstreamer needs (enabled by `--enable-winegstreamer` although GStreamer is absent, since its unix side is `build/ntdll-unix/winegstreamer_unixlib_ios.c`). Without arguments the script rebuilds the stock builtins added for games: `cryptsp`, `d3dx11_43`, `msvcp110`, `msvcr110` and `xaudio2_7` (committed in `app/Madeira/arm64ec-windows/` like every other builtin). It needs bison 3 for `tools/wrc` (macOS ships 2.3; Homebrew's is used when installed). The strip/pad step was verified this session; the configure step is UNVERIFIED from clean; build-modules.sh reproduced the five default DLLs at their shipped sizes on the development machine (2026-10-03).
   - `app/Madeira/arm64ec-windows/` is the DLL farm: every file in it is linked into the prefix (`system32` for x64 sessions, and `sysx64`), so a Wine module is only available if it was built and copied there. The native D3D12 path needs two stock modules in addition to the existing ones: `dcomp.dll` (`make -C dlls/dcomp`; a 64-bit Godot 4 engine loads it before it creates its D3D12 device, and gives up on D3D12 without it) and `ktmw32.dll` (`make -C dlls/ktmw32`; an optional import the same engine probes).
4. DXMT (submodule, branch ios-port):
   - unix side: `build/dxmt-ios/build.sh` (needs `toolchains/llvm-ios-build`) -> `app/Madeira/libdxmt_combined.a` (ignored; the app links it). Verified this session.
   - PE side: `meson setup dxmt/build-arm64ec dxmt -Dbuildtype=release -Dwine_build_path=../../wine/build-arm64ec --cross-file=dxmt/build-arm64ec-win.txt` then `ninja -C dxmt/build-arm64ec src/winemetal/winemetal.dll` (and d3d11.dll) -> copied to `app/Madeira/arm64ec-windows/`. Verified this session (winemetal.dll).
4b. In-app pairing (Built-in StikJIT on iOS 27): `build/rppairing-ios/build.sh`
   (Rust with the `aarch64-apple-ios` target; crates from crates.io at the
   versions in `build/rppairing-ios/Cargo.lock`) -> `app/Madeira/libmadeira_rppairing.a`
   (ignored; the app links it) and the bundled crate notices
   `app/Madeira/legal/LICENSES-rppairing-crates.txt` (tracked). `cargo test`
   in that folder runs its host tests. Verified on the development machine.
5. Native D3D12 runtime: `build/madeira-d3d12/build-pe.sh` -> `d3d12.dll`, `madeira_d3d12.dll` and the test executables in `app/Madeira/arm64ec-windows/` (tracked). Verified this session. `build/madeira-d3d12/fetch-converter.sh` re-verifies the converter library; `build/stage-licenses.sh` refreshes the bundled licence copies (the Xcode build fails if they are stale).
6. OpenGL and LOVE games:
   - This fork's pinned Wine submodule already includes `patches/wine-opengl-winios.patch`; do not apply it a second time. Rebuild `opengl32.dll` from the pinned Wine source before packaging. The inherited tracked DLL predates this source import; no updated binaries are committed. See `docs/FORK.md`.
   - `build/luajit-x64/build.sh` -> `app/Madeira/compat/love/lua51.dll`, needed for LOVE games.
   - `build/moltenvk-ios/build.sh`, then `build/mesa-ios/build.sh` -> `app/Madeira/gl/`, the desktop OpenGL backend. Optional: without it the app uses OpenGL ES. The script applies every file in `build/mesa-ios/patches` once (a `.madeira-applied-*` marker in the Mesa source records it), so an existing Mesa tree picks up a new patch on the next run.
   - 32-bit OpenGL games (id Tech 3: Jedi Academy, Jedi Outcast, Quake III) need all of: the regenerated wow64 thunks in `wine-opengl-winios.patch` (from `dlls/opengl32/make_opengl`; to regenerate, run `perl make_opengl` in `wine/dlls/opengl32`, which downloads the pinned Khronos registry files into `~/.cache/wine`), `build/ntdll-unix` rebuilt, and Mesa patch `0004` (Zink on MoltenVK: fragment inputs Metal can link, and no crash when a pipeline fails). They need the desktop OpenGL (Zink) backend; fixed-function OpenGL does not exist in OpenGL ES. A 32-bit process gets `GL_EXTENSIONS` cut to 3072 bytes, because these engines copy it into a fixed buffer; `env.MADEIRA_GL_EXTENSIONS_MAX` in madeira.cfg sets another limit (0 = none).
   - Each script says what source to download if it is missing. Verified on the development machine.
7. App: `xcodebuild -project app/Madeira.xcodeproj -scheme Madeira -destination 'generic/platform=iOS' -allowProvisioningUpdates build` (Debug is the configuration that runs the games; Release builds have crashed the guest), then zip `Payload/Madeira.app` into an IPA and sideload. Verified this session on the development machine.
8. WoW64 (32-bit programs, optional): `build/wine-i386/build.sh` (i386 Wine farm
   -> `app/Madeira/i386-windows/`), `build/fex-wow64/build.sh` (FEX WOW64 module
   -> `app/Madeira/aarch64-windows/xtajit.dll`) and the aarch64 `wow64.dll` /
   `wow64win.dll`; see docs/WOW64.md, "Building". UNVERIFIED on macOS.

## Status of the LGPL relink question

A recipient of a built package can obtain the complete corresponding
source of every LGPL library (Wine fork, GnuTLS, Nettle, GMP, FFmpeg) from the
repository, and the application source and build scripts above. Whether
they can actually relink depends on assembling the "not in the repository"
inputs and re-executing the UNVERIFIED steps; that end-to-end clean-machine
rebuild, signing and installation has NOT been performed. Until it is,
docs/LICENSING.md keeps the relink capability marked unverified. The
alternative the LGPL offers, shipping the application's object files, is
not currently done.
