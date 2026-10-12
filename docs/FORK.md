# Maintained fork and source-only changes

The working repositories are [LordierClaw/Madeira](https://github.com/LordierClaw/Madeira)
and its Wine submodule [LordierClaw/Madeira-wine](https://github.com/LordierClaw/Madeira-wine).
Madeira and Wine development both use `madeira-fixes`. The inherited
`main` and other upstream branches have not been overwritten. Future development belongs here.

For the current dependency pins, preserved-difference map and next-update
procedure, read [UPDATING.md](UPDATING.md) and
[upstream-state.json](upstream-state.json). The integration sections below
record the state at each merge; later build records supersede their build
limitations without changing their device-validation status.

## Provenance

- Madeira baseline: `willfaust/Madeira` at `4e9d45a74294cd820120791c4b3f2b79adf4fc70`.
- Wine baseline: `willfaust/wine`, LGPL branch, at
  `4f5b19718f4de88ecc5cb0dc08b119497a67ba8f`.
- OpenGL source: the difference from that Madeira baseline to
  [c-gow/Madeira](https://github.com/c-gow/Madeira/commit/165c05dfcd9d963aae71a01ca1b4145d6223cfa0)
  (`opengl-test-6`). Connor Gow's notices and Wine patch attribution are retained.
  The prebuilt `opengl32.dll` was deliberately excluded. The Wine source patch
  is committed in our Wine submodule, so it must not be reapplied during builds.

These original baselines match the source used for the device troubleshooting.
Development continued on `ui-ux` until 2026-10-12, when `madeira-fixes` was
fast-forwarded to it and `ui-ux` was deleted. Both repositories now develop on
`madeira-fixes`.

## Upstream integration, 2026-10-10

`ui-ux` merges `willfaust/Madeira` main at `48f976429c189f8396e23d251d8a82f43c705922`.
The Wine fork merges `willfaust/wine` madeira-lgpl at
`257f271cfffed9f22f7987cac53bc00095d092fe`, producing `2233aa82`.
Local `backup/pre-upstream-2026-10-10` branches retain both pre-merge tips.
These are merges, with no rewriting of the published fix commits.

DataFix, OpenGL, click/key pacing, DragFix, DLC ownership/downloads and progress,
Cloud's keep-local choice, and landscape only while playing are retained.
Drag endings also retire touches suppressed by upstream's controller mouse gate;
suppressed touches cannot turn a later trackpad tap into two-finger scrolling.
Upstream's controller continuity, builtin XInput selection, rumble, Wine Mono,
library, display, loader and memory fixes are included, together with its FEX,
DXMT and Dock pins. c-gow was fetched for reference, not merged wholesale.

The merge preserves the fork's existing binary files and excludes 25 newly
added upstream DLLs. No Microsoft runtime is added. The native CI recipe now
rebuilds the changed kernelbase, mscoree and XInput PE modules along with ntdll
and OpenGL, keys its FEX cache by the actual pin, and checks the HID/PE recipes
before reusing runtime artifacts. This is still not a full rebuild of every
inherited PE module: the newly added Wine builtins, updated FEX/DXMT PE files,
and WoW64 components require their documented build steps before shipping a
fully refreshed runtime. Existing build 17 predates this merge.

Local DataFix (17 cases) and input queue (51 checks) pass.
[Integration CI](https://github.com/LordierClaw/Madeira/actions/runs/38030442329)
passes all three jobs: Steam/DLC transfers, Cloud and Swift/iOS SDK checks,
and runtime/input regressions against the new Wine and FEX pins. The added
trackpad cases cover controller suppression during a live drag and a later tap.
Wine Mono's synthetic archive checks pass; its optional real-tarball check
is skipped because the archive is not present on CI. This source integration
does not claim a full native build, a new IPA or physical-device validation.
The next native build must rebuild its runtime dependencies (leave `native_run`
empty); the build 17 dependency artifacts do not match these new pins.

## OpenGL fork integration, 2026-10-10

After the main upstream integration, `ui-ux` also merges
[c-gow/Madeira at e711351](https://github.com/c-gow/Madeira/commit/e7113515c455a48df5fa5bcce4ebfdeaaf04f152).
The merge's first parent, `c571c473`, is our preceding tip. Existing input, DragFix,
DLC/progress, Cloud keep-local and playing-only landscape fixes are preserved.
The Wine pin remains `2233aa82`: its existing OpenGL implementation matches
the imported patch (reverse-apply check excluding the license context passes).
The updated patch only moves its license hunk to the new upstream context;
do not apply it again to this fork's Wine.

- Mesa patches 0005-0007 handle MoltenVK triangle fans, GLSL variable names
  colliding with Metal types, depth clamp, and unsupported overflow queries.
- OpenGL contributes to the common frame counter, so FPS and first-frame
  launch checks can see both GL backends. These are submitted command buffers,
  not evidence of GPU completion or changing image contents.
- Audio requests a 5 ms I/O period before activating the session, with
  `MADEIRA_AUDIO_IO_MS` override. This may help short-buffer audio clients;
  the Darkest Dungeon voice issue still needs device confirmation.
- XAudio2 2.8/2.9 source builds and FAudio notices are included. The native CI
  recipe builds and carries both DLLs using Wine's bundled FAudio.

The imported thin-reservation workaround is **off by default in our fork**.
It reports 1-2 GB reservations backed by small heads whose logical tails
overlap, so sparse access can reach another allocation. `MADEIRA_THIN_RESERVE=1`
is an experimental opt-in only for a separately tested game. We serialize
configuration initialization with `virtual_mutex` (avoiding partially initialized
geometry on concurrent first allocations), and reject zero-length or out-of-range
tail decommits through the normal VM error path. This does not make overlapping
reservations equivalent to Windows allocation semantics.

Local checks: DataFix 17 cases, input queue 51 checks, thin-reservation admission,
initialization and tail bounds 14 cases, and configuration catalog pass. The
thin harness extracts production functions and mocks VM calls; it does not
validate the experimental allocator on iOS. Both imported x64 probe sources
compile. All seven Mesa patches apply in order to checksum-verified Mesa 25.0.7.

[Integration CI for 0998dcd](https://github.com/LordierClaw/Madeira/actions/runs/38031831395)
passes all three jobs: Steam/DLC transfers, Cloud and Swift/iOS SDK checks,
and runtime/input regressions, including the new thin-reservation host checks.

At merge commit `0998dcd`, no inherited binary was changed and none of c-gow's
new DLL/EXE payloads was added. This was source integration only. The subsequent
pipeline commits `0a89852`, `a25754c` and `bfe4319` add native graphics and
updated PE builds, and require those fresh outputs when packaging. The
compatibility IPA can no longer overwrite fresh OpenGL or rebuilt i386 modules.
Build and package evidence is recorded in the update ledger and later sections.

## Updated native IPA, 2026-10-10

[Native CI run 38034074592](https://github.com/LordierClaw/Madeira/actions/runs/38034074592)
successfully built source `283ecb913804f223a8d5214f7ac4587125a7e523`: graphics,
runtime, DXMT, Debug app and JIT helper. The unchanged LLVM artifact was reused
from run 38030334310 after recipe comparison. Graphics were freshly compiled
in run 38032306400 from `0a89852` and then reused by their recipe/patch cache key;
their unsigned digest was checked before Xcode and signed hashes afterward.

Local output: **`Madeira-0.1.3-Upstream-OpenGL.ipa`**, version 0.1.3,
**build 22**, **159,096,870 bytes**. SHA-256:
`a0b09734d864a5bff28f2c8ac7600c24da43647abe3dca8dece5d712b86b11c8`.
The IPA, `.manifest.json`, `.ipa.sha256`, native archives and diagnostic logs
are in the workspace's sibling `outputs/` directory, outside Git.

The package contains new Mesa 25.0.7 plus all seven patches, MoltenVK 1.4.2,
native libraries and 63 refreshed PE binaries (66 runtime receipt entries
including LuaJIT metadata/license). All 63 mandatory paths are present.
Rebuilt i386 files win over the compatibility overlay. Its 42 reported
module/import pairs missing from the partial i386 build are supplied by the
unchanged compatibility farm. This is module-presence validation, not a claim
that every imported function has been exercised on a device.

Packaging verifies all ZIP members and hashes, graphics source/patch pins,
rebuilt PE hashes, app source, loader DataFix markers/alignment/padding and
new UI/input strings. Native signing verification passed on macOS; the app
Mach-O carries `get-task-allow`, `allow-jit` and increased-memory entitlements,
and that binary is preserved exactly in the IPA. The final local overlay needs
normal sideload signing. No Apple credentials or Microsoft runtimes are in
source commits or CI uploads; local runtime inputs come from the checksum-
verified compatibility IPA recorded in the manifest.

[Host CI for bfe4319](https://github.com/LordierClaw/Madeira/actions/runs/38032867939)
passes all three jobs, including the ten new synthetic packaging cases.
Later changes address build configuration only: pinned cross dependencies and
the reproduced ARM64EC ThinLTO linker issue. The build ledger records each
failure and correction. No iPhone execution is claimed for build 22; retest
gameplay, DLC loading, drag/drop, keyboard/controller and audio. Thin
reservations remain off by default. Other unchanged inherited PE modules and
the unchanged i386/DXMT compatibility files are explicitly not rebuilt.

## Game-loading path lookup, 2026-10-10

The subsequent build 22 loading investigation and Wine change `17c52803` are
recorded in [LOADING_PERFORMANCE.md](LOADING_PERFORMANCE.md). The iOS-only
existing-parent shortcut reduces repeated path-component stats. Its 960 host
differential fixtures plus root-handle, UTF/reparse-offset, buffer-growth and
between-call mutation checks pass; the targeted syscall-count reproducer drops
from six stats to three. Neither this count nor source review measures game
loading speed. Native build and physical-device timing are recorded separately.
No memory, swap, shader-cache or game-file settings are changed with this fix.

Native run [38067549022](https://github.com/LordierClaw/Madeira/actions/runs/38067549022)
builds `70756cc` successfully (graphics, runtime, DXMT, app/JIT helper), with the
unchanged LLVM artifact from `38030334310`. Host run
[38067549685](https://github.com/LordierClaw/Madeira/actions/runs/38067549685)
passes all three jobs, including the new file-lookup regression on Linux.

Local **`Madeira-0.1.3-LoadLookup.ipa`**, version 0.1.3 **build 23**, is
**159,095,537 bytes**, SHA-256
`c072d8c4113e315b75e219c008e1cf5d02989ffe670a46af0de2c5fded85bd09`.
Packaging verifies fresh source/graphics/runtime receipts, ZIP members and
hashes, and loader DataFix. The embedded runtime receipt pins Wine `17c52803`.
The preserved native executable carries get-task-allow, allow-jit and
increased-memory entitlements. IPA, manifest, checksum and native archive live
outside Git under `outputs/`; sideload signing is still required. Build 22 is
retained for comparison. No physical-device speedup is claimed for build 23.

## Remaining loading and audio investigation, 2026-10-11

The user tested build 23 and still reports long save/dungeon loading. Its new
device log confirms a minute-long process-footprint plateau near 1,974 MiB,
while the same game worker remains busy in native file lookup/metadata calls.
The persistent audio stream delivers zeros with healthy output pacing; the old
probe cannot distinguish client-requested silence from an unflagged zero signal.
See [LOADING_PERFORMANCE.md](LOADING_PERFORMANCE.md) for the measured intervals,
JIT-pool difference, evidence limits, and bounded diagnostic additions.

The next diagnostic candidate preserves build 23 behavior, adding per-thread
file-open/metadata timings and bounded path/status samples plus per-stream
silent-flag/source-sample summaries. These are measurements, not another
performance or audio-output fix. File lookup results and audio ring bytes stay
unchanged in host regression checks. A report of misplaced iOS system time in
landscape still needs a screenshot; no clock/window change is included.

Native [run 38071462636](https://github.com/LordierClaw/Madeira/actions/runs/38071462636)
and host [run 38071462054](https://github.com/LordierClaw/Madeira/actions/runs/38071462054)
pass for source `1a30c74`, Wine `aa758165`. Local diagnostic **build 25** is
`Madeira-0.1.3-LoadingAudioProbe.ipa`, **159,102,594 bytes**, SHA-256
`f8590bba81e5f78bfc47a4de7c70c47c240f56a2cc89c95693c38cca8319bac7`.
The final IPA's source/runtime receipts, diagnostic markers and native
JIT/debug/increased-memory entitlements are verified. Artifacts, manifest and
checksum are under sibling `outputs/`, outside Git; normal sideload signing is
required. Build 23 remains the behavior baseline. The build 25 device result
and its targeted follow-up are recorded below.

## Missing-parent loading optimization, 2026-10-11

Build 25 still loads slowly. Its game worker spends 75.894 of 85.009 measured
seconds resolving paths, with 38,080 failures in 40,968 opens. Sampled failures
are missing intermediate DLC override directories; only 39 full-parent shortcut
hits occur. Wine commit `c5c322e3` extends that shortcut with a fresh binary
search for an existing ancestor, then resumes ordinary case/short-name/reparse
resolution. It caches no filesystem results and changes no DLC files.

Host checks pass 4,752 differential fixtures; representative missing-parent
probes fall from 15 to 8 stat calls, including trailing separators. Independent
source review found no blockers. Native build and device timing are separate
checks; no measured game speedup is claimed yet.

The user reports normal audio and system-clock placement in this session. RAM
is higher (sampled peak 2,919 versus 2,455 MiB), but actual JIT allocation is also
larger (624 versus 432 MiB) and the FEX memory band differs. The log contains a
stable footprint interval followed by resource loading; it does not establish
a memory leak. Detailed metrics, comparison limits and retesting instructions
are in [LOADING_PERFORMANCE.md](LOADING_PERFORMANCE.md).

Native [run 38075335460](https://github.com/LordierClaw/Madeira/actions/runs/38075335460)
and all three host jobs in
[run 38075335110](https://github.com/LordierClaw/Madeira/actions/runs/38075335110)
pass for source `a62c24ee8dc6c94928926596004ff63fc5a2fa72`, Wine
`c5c322e300c8990c5a04861ccedbc816207bb2e2`. Local **build 26** is
`Madeira-0.1.3-LoadAncestor.ipa`, **159,101,136 bytes**, SHA-256
`696eeb91c462f549443abf4459b7be9f3ad61e09f374377b275a5515664d42ba`.
Runtime is freshly rebuilt; LLVM/graphics/DXMT reuse verified recipes. The
local packager validates ZIP integrity, payload hashes, source/dependency
receipts and fresh runtime/graphics precedence. Reading the final IPA confirms
version 26, source/Wine pins, diagnostic markers and JIT/debug/increased-memory
entitlements. Device load-time and memory results for build 26 remain pending.

## Repeated-path loading optimization, 2026-10-11

The build 26 follow-up identifies repeated ancestor searches and duplicate
normal/reparse directory scans as the remaining measured loading bottleneck.
Wine `7a940b7f` uses bounded, freshly verified prefix hints and reuses the
same-call directory observation while retaining APFS's literal reparse stat.
The 4,820 differential host fixtures and all three
[host CI jobs](https://github.com/LordierClaw/Madeira/actions/runs/38112011394)
pass. The representative repeated miss falls from 8 stats/two scans to
4 stats/one scan. No JIT/FEX memory default is changed; this is not a measured
RAM reduction or whole-game speedup.

[Native run 38112018449](https://github.com/LordierClaw/Madeira/actions/runs/38112018449)
passes for source `0e2f884fbda132e78acbedf57e142bbd82494042`, Wine
`7a940b7fa3bf67b66f7a30bf225fd3fe3b38941b`. Local **build 27** is
`Madeira-0.1.3-LoadReuse.ipa`, **159,105,657 bytes**, SHA-256
`fded79acafbf1225dd67ca2e635fa86acf108e95113c443c83b630a4cec5f131`.
Runtime is fresh; LLVM/graphics/DXMT reuse verified recipes. ZIP/hash/source/
runtime/graphics checks pass, and the final IPA verifies new diagnostic
markers, version/pins and native JIT/debug/increased-memory entitlements.
The checksum-pinned local compatibility input is unchanged and remains
outside Git. Device load-time/RAM results are pending; full evidence and
retest instructions are in [LOADING_PERFORMANCE.md](LOADING_PERFORMANCE.md).

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

The 2026-10-11 build 26 device follow-up still spends about 86% of selected
high-failure worker intervals in path lookup. Wine `7a940b7f` adds 32 bounded,
freshly revalidated ancestor hints per thread and reuses an error-free
directory observation to avoid scanning twice for normal/reparse misses.
The literal reparse stat is retained for APFS Unicode normalization. The host
reproducer reduces repeated missing-parent probes from 8 stats/two scans to
4 stats/one scan; 4,820 differential cases pass. JIT/FEX memory defaults are
unchanged: the log does not show paging as the plateau bottleneck or establish
a leak. No RAM reduction or whole-game speedup is claimed before retesting.
Full diagnosis, limitations and the next build receipt are in
[LOADING_PERFORMANCE.md](LOADING_PERFORMANCE.md).

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

## Content index build 29, 2026-10-11

Source `d25a5d9852ad85f7b7ea2ead38da0fe551e1edc3`, Wine
`8b948ac0a8c6867e77efe749edddb061f2d410a2`. Developed and reviewed directly,
without subagents. The Wine commit adds process-wide, bounded directory names
validated by fresh path metadata and native vnode events. The app commits add
bounded, versioned verified-file catalogs, force-verify Repair and metadata
reuse in preparation. See [LOADING_PERFORMANCE.md](LOADING_PERFORMANCE.md) and
[implementation ledger](superpowers/plans/2026-10-11-content-index.md).

All three [host jobs](https://github.com/LordierClaw/Madeira/actions/runs/38115441553) pass, including real Darwin filesystem/vnode
checks under ASan/UBSan and Steam catalog mutation/repair/all-missing-DLC tests.
[Native build 29](https://github.com/LordierClaw/Madeira/actions/runs/38115986505) passes, using LLVM from 38030334310 and the freshly
built Wine/PE runtime from successful run 38115187333. Matching recipes and
submodule pins were checked before reuse; graphics/DXMT recipe caches match.
The preceding build 28 is an intermediate artifact, not the delivered IPA.

Native archive: 97,087,299 bytes, SHA-256
`c133d31a2f019b860fe7c6342c89e17d21e527922eef82dcb69230a955c25b0b`.
Local IPA: `Madeira-0.1.3-ContentIndex.ipa`, **159,119,672 bytes**, SHA-256
`bc8cc16e42b4eb7c27dbbe69eeb05029e0c360a5019d47f70fe705e81753e33c`.
Packaging uses the unchanged checksum-pinned ClickKeyFix compatibility input.
ZIP integrity, all payload hashes, rebuilt-file precedence, source/runtime/
graphics receipts and native JIT/debug/increased-memory entitlements pass.
Final binary inspection confirms the directory-index and Steam-index markers.
Artifacts, compatibility payloads and raw logs remain outside Git. Re-sign
through the sideload tool as usual. Device load time and total RAM comparison
against build 27 remain pending; host scan reductions are not a device speedup.
