# Resume and update this fork

Start here, then read [FORK.md](FORK.md), [BUILDING.md](BUILDING.md) and
[upstream-state.json](upstream-state.json). The JSON is also the packaging
contract: exact dependency pins and the minimum set of freshly rebuilt files.
Update it deliberately when dependency revisions or the shipped module set change.

## Working checkouts and integrated history

| Checkout | Development branch | Write remote | Read-only upstream |
|---|---|---|---|
| Madeira | `ui-ux` | `LordierClaw/Madeira` | `willfaust/Madeira` (`upstream`), `c-gow/Madeira` (`c-gow`) |
| `wine` submodule | `madeira-fixes` | `LordierClaw/Madeira-wine` | `willfaust/wine`, `madeira-lgpl` |

The older `work/` checkouts are build/reference history. The parent
`madeira-fixes` branch is the original fork base, not the active UI branch.
Push Wine changes first, then the parent's updated gitlink. Never force-push
published fix history to accommodate an upstream update.

Integration on 2026-10-10:

| Component | Integrated source | Integration commit in our fork |
|---|---|---|
| Madeira main | `48f976429c189f8396e23d251d8a82f43c705922` | `b4c6d80` |
| Wine madeira-lgpl | `257f271cfffed9f22f7987cac53bc00095d092fe` | `2233aa8208ffdd6d5da46c192727f7380e5555ae` |
| c-gow OpenGL | `e7113515c455a48df5fa5bcce4ebfdeaaf04f152` | `0998dcd` |

Local recovery refs are `backup/pre-upstream-2026-10-10` in both checkouts,
and `backup/pre-cgow-2026-10-10` in the parent. Full FEX, DXMT, Dock, LuaJIT,
Mesa and MoltenVK pins are in the JSON. Wine already contains the c-gow
OpenGL implementation; **do not reapply `patches/wine-opengl-winios.patch`**.

Latest verified output is **build 22**, source `283ecb9`,
[native run 38034074592](https://github.com/LordierClaw/Madeira/actions/runs/38034074592).
The local `outputs/Madeira-0.1.3-Upstream-OpenGL.ipa` and adjacent manifest/checksum
are outside Git. Full size/hash and remaining device checks are in FORK.md and
the JSON's `latest_build` field. Documentation commits after `283ecb9` do not
change the source identity embedded in this app.

## Fork differences to preserve

This table identifies the behavior and its implementation, rather than relying
on a cherry-pick list that becomes stale after merges. Use `git log -- <path>`
and `git diff <integrated-upstream>..HEAD -- <path>` for exact changes.

| Area | Implementation / origin | Regression evidence |
|---|---|---|
| ARM64EC writable data exports (DataFix) | Wine `dlls/ntdll/loader.c`; our Wine commit `c1115cac` | `tests/host/check-data-exports.py`: 17 cases; original device probes passed |
| Trackpad click and soft-keyboard minimum hold | `app/Madeira/Winios/MadeiraClickQueue.h`, `Winios.m`; our bounded FIFO pacing | `check-input-queue.py`: 51 checks; ClickFix confirmed by user, keyboard game result pending |
| Drag owner release/cancellation | `app/Madeira/ContentView.swift`; our DragFix plus integration with upstream `TouchMouseGate` | `check-trackpad.py`; image-freeze report still requires device retest |
| Owned DLC discovery/download and aggregate transfer estimates | `SteamGames.swift`, `SteamOwnedLibrary.swift`, `SwiftSteam` content/install/library code | `check-steam-library.py`, `check-steam-ui.py`; keep ownership and shared-depot handling |
| Steam Cloud default file filtering / persistent keep-local | Steam Cloud model/UI and preferences | `check-steam-cloud.py`, `check-steam-ui.py`; keep choice across run/sync |
| Landscape only while playing | `MadeiraApp.swift`, library and controls window/session state | `check-orientation.py`, UI typecheck |
| OpenGL ES / Mesa Zink and WoW64 thunks | c-gow sources in `WiniosGL.m`, Wine, Mesa patches 0001-0007 | all seven patches apply to pinned Mesa; native graphics compilation and receipts |
| GL FPS/launch observation | c-gow GL submitted-frame count added to DXMT count | counts submissions, not GPU completion or pixel changes |
| Audio 5 ms request, XAudio2 2.8/2.9 | c-gow audio session change and Wine/FAudio builds | compile/host checks; voice quality remains device-unverified |
| Experimental thin reservations | c-gow allocator in `build/ntdll-unix/virtual_ios.c`; our opt-in, initialization locking and decommit bounds | `check-thin-reserve.py`: 14 production-function cases; **off by default**, overlapping tails remain unsafe for general use |
| Portable config catalog generator | `build/tools/gen-config-catalog.py` and generated source | `check-config-catalog.py` |
| Source-only CI and packaging | `.github/workflows`, `build/ci`, `tools/package-local-ipa.py`, `tools/build-receipts.py` | `check-local-package.py`: 10 synthetic archive cases; native CI and final manifest |

Main upstream also supplies controller continuity, XInput selection/rumble,
Wine Mono, library/display and loader/memory changes. These are integrated
upstream features, not new fork fixes. Do not restore old versions when
resolving UI or input conflicts.

## Next upstream update

1. Check `git status --short`, branch and remotes in both repositories. Preserve
   any local work and create dated backup refs before integration. Fetch origin
   and both parent upstreams; fetch Wine upstream. Read changes since the JSON
   pins before choosing merge order or enabling new behavior.
2. Merge Wine upstream into `madeira-fixes`, retaining DataFix and OpenGL.
   Run the loader/runtime tests, audit source-only staged changes, commit and
   push to our Wine origin. Merge Madeira main, then relevant c-gow updates
   into `ui-ux`, preserving the feature table and setting the fork Wine gitlink.
3. Source-only merge policy: retain existing binary payloads unchanged in Git;
   exclude newly introduced DLL/EXE/library/runtime/game payloads. Rebuild the
   required open-source components outside Git. Keep upstream license texts.
   Review every conflict and the resulting diff; never blindly choose one side.
4. Update the JSON pins, required rebuilt modules, this table and FORK history.
   Changes in Wine/FEX/DXMT source need matching PE outputs as well as native
   archives. Do not assume an app rebuild refreshes tracked DLLs.
5. Run source/host checks and the Steam/UI workflow. Before **each** commit:
   `python tools/check-source-commit.py --staged` and `git diff --cached --check`.
   Keep hooks enabled with `git config core.hooksPath .githooks`.
6. Run `iOS native build` on `ui-ux`. Leave `native_run` empty whenever runtime
   source or recipes changed. Reuse LLVM only if its recipe matches and the
   artifact exists. Graphics cache is keyed by its full recipes and patches;
   it is validated against the unsigned digest before Xcode copies/signs it.
7. Download `Madeira-native-app`, read its `source-commit.txt`, and use the
   local packager with an explicitly supplied, checksum-verified compatibility
   IPA. New graphics and rebuilt PE files must win over old compatibility files.
   Keep the resulting IPA, manifest and checksum outside Git.
8. Record source/build commit, CI run, artifact filename/size/SHA-256, reuse
   inputs, test results and device limitations in FORK/BUILDING. Documentation
   commits can follow the build commit; keep those identities distinct.

## Build provenance and remaining limits

`gl/build-info.json` records Mesa tarball checksum, MoltenVK pin, all patch
hashes and unsigned plugin hashes. `build-info/runtime.json` records source
pins and hashes of every refreshed runtime file. `build-info/app.json` binds
the app commit, both receipts and the signed graphics hashes. The local IPA
manifest repeats these and lists every file sourced from the compatibility IPA.

The refreshed set includes FEX ARM64EC/WoW64, native WoW64 DLLs, changed Wine
i386 modules, ARM64EC Wine builtins/XAudio2, DXMT PE, D3D12 and LuaJIT. It is
**not** a rebuild of every inherited DLL: unchanged i386 modules (including
their existing DXMT files) and other unchanged tracked modules remain. Local
Microsoft runtime inputs are not uploaded to CI or committed.

Native build and package success do not establish device behavior. Retest
Darkest Dungeon tutorial loading with all DLC, trackpad click/hold/drag/drop,
virtual keyboard/controller, audio and both graphics backends. Keep thin
reservations disabled unless separately investigating that experimental mode.

See [the build ledger](superpowers/plans/2026-10-10-updated-ipa.md) for observed
failures, corrections and final build/package evidence from this update.
