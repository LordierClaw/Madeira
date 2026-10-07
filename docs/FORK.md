# Maintained fork and source-only changes

The working repositories are [LordierClaw/Madeira](https://github.com/LordierClaw/Madeira)
and its Wine submodule [LordierClaw/Madeira-wine](https://github.com/LordierClaw/Madeira-wine).
Both use `madeira-fixes` as the maintained branch. The inherited `main` and other
upstream branches have not been overwritten. Future development belongs here.

## Provenance

- Madeira baseline: `willfaust/Madeira` at `4e9d45a74294cd820120791c4b3f2b79adf4fc70`.
- Wine baseline: `willfaust/wine`, LGPL branch, at
  `4f5b19718f4de88ecc5cb0dc08b119497a67ba8f`.
- OpenGL source: the difference from that Madeira baseline to
  [c-gow/Madeira](https://github.com/c-gow/Madeira/commit/165c05dfcd9d963aae71a01ca1b4145d6223cfa0)
  (`opengl-test-6`). Connor Gow's notices and Wine patch attribution are retained.
  The prebuilt `opengl32.dll` was deliberately excluded. The Wine source patch
  is committed in our Wine submodule, so it must not be reapplied during builds.

These known baselines match the source used for the device troubleshooting.
New upstream commits are not automatically part of this branch; merging them
is a separate change with its own validation.

## Fixes and evidence as of 2026-10-07

1. **ARM64EC data exports:** Wine's `dlls/ntdll/loader.c` redirects eligible writable
   MSVCP140 data exports into the existing JIT mapping, preserving code thunks and
   unrelated DLLs. The mapping must round-trip. Dynamic/ordinal and x64 static
   imports are covered. `MADEIRA_EC_DATA_EXPORTS=0` disables the correction.
   The change is in Wine source; it does not modify or redistribute Microsoft's
   runtime. Both dynamic and static device probes passed.
2. **Trackpad clicks:** `Winios.m` uses `MadeiraClickQueue.h` to retain a minimum
   80 ms down interval and a 30 ms released interval for state-polling games.
   The user confirmed that trackpad clicks work on iPhone with ClickFix.
3. **Soft keyboard:** the same queue protects keyboard down/up edges. The device
   probe received 17 ordinary key pairs separated by only 0–2 ms and missed all
   17 in `GetAsyncKeyState`; two roughly 80 ms Alt+F4 chords were observed.
   ClickKeyFix passed 102 ARM64 emulation checks against the native release
   binary with mocked OS/Wine calls, but its iPhone/game result is still pending.

`MADEIRA_CLICK_HOLD_MS` and `MADEIRA_KEY_HOLD_MS` default to 80; 0 disables the
respective pacing, and 16–250 chooses a hold duration in milliseconds. The
queue preserves FIFO/modifier ordering and never sleeps. A delayed release can
also defer events behind it; burst text is consequently paced. This tradeoff is
documented rather than hidden as a general-purpose text-input improvement.

## Trackpad drag investigation, 2026-10-08

The build 15 device log records a long-press drag followed by repeated
"non-drag finger up" events without a drop. `MetalBackedView` allowed a second
touch to switch an active drag into its two-finger branch; that branch consumed
the owner's ending before posting left-up. It retained the ended `UITouch`, so
later fingers could neither move that drag nor release it. This is separate from
the bounded 80 ms click queue below the Swift gesture layer.

The trackpad now keeps an active drag with its original finger, releases that
finger before other input branches, and recovers an owner that has ended or is
absent from the event. Cancellation, view detachment and app deactivation also
release and invalidate pending holds. Touches on other views do not count toward
trackpad gestures. `tests/host/check-trackpad.py` replays the production handlers
with simulated touch lifetimes. Device verification of this correction is pending;
it does not establish the cause of every game stop or the separate slow loading.

The user reports that the picture itself freezes. The same log still advances
the GL present counter after the unreleased drag. That counter records submitted
Metal command buffers, not changing image contents or completed GPU work, so it
does not rule out the reported visual freeze. No speculative renderer change is
included. The drag regression, existing input queue checks and iOS native build
passed for `03baa7c`; confirming the complete symptom still requires a device run.

**Not fixed:** controller behavior in Darkest Dungeon and minor voice-audio
crackle. The device probe did receive controller buttons, axes and a trigger
through XInput 1.4, 1.3 and 9.1.0. The player slot was absent at startup and first
appeared at 39.940 seconds; that alone does not prove why the game ignores it.
The user confirmed Enable Controller is on. No speculative controller default
change is included. Device logs/configuration are kept outside Git.

## Rebuild instead of committing binaries

Follow [BUILDING.md](BUILDING.md) with the pinned submodules. Rebuild native app
code, `ntdll.dll` and `opengl32.dll` before packaging. Inherited tracked binaries
do **not** contain these new source changes; build outputs are local only.
The Windows troubleshooting IPAs were made by replacing the rebuilt Wine DLL
and applying a tested native queue overlay to the c-gow release. The fork now
has a successful
[native Debug app build](https://github.com/LordierClaw/Madeira/actions/runs/37663379693)
from commit `03baa7c`, including trackpad drag release, corrected DLC transfer
estimates, landscape only during play, native input queues and the rebuilt
Wine loader. Its local IPA packaging reuses the explicitly supplied OpenGL
plugins, i386 farm and VC runtimes; other inherited PE modules are not all
rebuilt. This is distinct from a complete rebuild of every shipped component.
The new native build still needs iPhone/game confirmation.

Microsoft VC runtime files must be supplied separately as described in
[fetch-vcruntime.md](../tools/fetch-vcruntime.md). They are excluded from source
commits and CI artifacts. The local packager can copy them from an explicitly supplied,
checksum-verified compatibility IPA into the final local IPA. Wine runtime
implementations bearing similar names already exist in upstream history; their names alone do not make them Microsoft
redistributable payloads. Upstream license files and notices remain intact.

Local IPA/build artifacts, toolchains, game data, device logs, user configuration,
and signing/pairing material must not be committed. `.gitignore` and the source
audit prevent accidentally adding these in new commits; they do not rewrite
upstream history. Enable hooks in each new Madeira checkout:

```sh
git config core.hooksPath .githooks
python3 tools/check-source-commit.py --staged
```

## Source tests and diagnostic programs

The queue test compiles the production header and needs a C11 host compiler:

```sh
python3 tests/host/check-input-queue.py --cc clang
```

On Windows, the loader harness extracts the actual helper from `wine`, mocks
the PE/JIT mapping and runs 17 cases. Point `--cc` at an llvm-mingw compiler:

```text
python tests/host/check-data-exports.py --cc <llvm-mingw-bin>/x86_64-w64-mingw32-clang.exe
python tests/input/build-probes.py --toolchain <llvm-mingw-bin>
```

The generated EXEs and the two-symbol import library remain in ignored
`tests/input/out/`. No Microsoft DLL is needed to build the probe sources.
The data probes require the user's runtime when run inside Madeira. The input
probe writes `C:\madeira-input-probe.txt`; its `--self-test` checks only local
message/report handling, not the iPhone input path. Host tests and native
emulation do not replace device validation.

Commit/push changes inside Wine first, then update and commit the parent gitlink.
Push only to our `origin` remotes. `upstream` and `c-gow` are references.
