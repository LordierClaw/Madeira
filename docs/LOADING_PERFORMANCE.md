# Darkest Dungeon loading investigation

## Build 22 evidence, 2026-10-10

The user reports a 2–3 minute wait when entering a save/dungeon, including
repeated loads in the same session. Both supplied logs identify version 0.1.3
build 22 on iPhone14,3 (A15), Low Power Mode enabled, a 1 GB requested JIT pool,
and 1 GB classic file-backed swap. Device logs remain outside Git.

The longer session starts at 23:08:11 and ends at 23:12:05. During the long
loading interval the same Darkest.exe thread is repeatedly sampled at
77–94% CPU in `fstatat`, called by `find_file_in_dir` / `lookup_unix_name`.
A full native stack identifies the caller as:

```text
NtCreateFile
get_nt_and_unix_names
nt_to_unix_file_name_no_root
fstatat
```

From approximately 23:09:40 through 23:10:59, the process footprint stays
around 2.2 GB while only about 7 MB of disk reads are reported. From 23:11:00
through 23:11:59, footprint rises from about 2.2 to 2.7 GB. These are whole-app
counters, including Wine, Steam, graphics and JIT; they are not game-only RAM
or asset-read throughput. Cached reads do not necessarily appear as disk I/O.
The log contains repeated output; do not count its timestamped replay as a
second set of measurements. The `[xp] cpu` field is CPU **milliseconds per
sample**, not a percentage; divide by `dt` before interpreting utilization.

This establishes path resolution as a sustained CPU hot spot. It does not
prove every delayed second is spent there, identify the requested filenames,
or establish that file-backed swap, power saving, shaders or other loading work
have no cost. Initial Low Power Mode is still enabled in these new logs, and
thermal state changes from nominal to fair at 23:09:56. The shorter previous
session does not capture the same sustained lookup interval.

## Targeted change

Wine's `dlls/ntdll/unix/file.c:lookup_unix_name` first stats the whole path.
If that fails, it walks every Windows path component, re-statting each prefix
and performing case-insensitive/reparse resolution as necessary. A missing or
case-mismatched leaf therefore repeats work for exact, existing ancestors.

The iOS-only shortcut attempts a **fresh stat of the full parent** after the
whole-path miss. If that exact parent exists and is a directory, the existing
resolver handles only the last component. It preserves the original NT offset
for reparse handling and keeps the ordinary walk when the parent is missing,
needs case/short-name resolution, or is a Wine reparse point. Unix namespace
paths and trailing separators take the original path. There is no persistent
positive or negative cache, no stale “missing DLC file” result, and no changes
to game files, DLC ownership, RAM limits or shader settings.

## Validation and limits

`tests/host/check-file-lookup.py` extracts the real `lookup_unix_name` and
`find_file_in_dir` functions, running the former both with and without the iOS
shortcut against a mocked filesystem. Differential fixtures cover case modes,
existing/missing parents and leaves, files used as parents, UTF paths, short
names, reparse offsets, create/open/overwrite, Unix namespace, invalid paths,
conversion failure, buffer growth, and file creation/deletion between calls.

The reproducer failed before the production edit: the deep-parent,
case-mismatched-leaf lookup performed six `stat` calls. With the shortcut it
performs three and returns the same path/status. This is a host syscall-count
result, **not a 2× game-loading speed claim**. Native compilation and device
timing are separate checks. Compare the same save/dungeon and DLC selection,
keeping power mode, swap, renderer and resolution constant; then repeat the
load in the same session. Do not combine this change with shader-cache or
memory experiments while measuring its effect.

## Built candidate

Version 0.1.3 **build 23**, `Madeira-0.1.3-LoadLookup.ipa`, contains source
`70756cc29e884ad6e7426dc44206aa2e7e70b5da` and Wine
`17c5280304a8a113b4ceee85245362058450847c`. Native CI
[38067549022](https://github.com/LordierClaw/Madeira/actions/runs/38067549022)
and host CI [38067549685](https://github.com/LordierClaw/Madeira/actions/runs/38067549685)
pass. Final packaging checks source pins, runtime/graphics hashes, loader
DataFix and ZIP integrity; JIT/debug/increased-memory entitlements are present
in the preserved native executable. See FORK.md for size/checksum. Device
loading measurements were pending at packaging; the subsequent build 23 result
is recorded below. Build 22 is retained as the baseline.

## Build 23 device result, 2026-10-11

The user reports the same long load, with a long flat RAM interval followed by
growth, and intermittent loss of **all** game audio after the voiced intro.
The supplied `madeira-log (3).txt` identifies build 23. This is not a successful
device speedup result. It does not include exact user-marked load boundaries.

Deduplicated `[xp]` samples show:

| Device-local interval | Process footprint | Process CPU / elapsed | Physical disk reads |
|---|---|---|---|
| 00:06:30–00:07:30 | 1,974 → 1,975 MiB | 1.630 core equivalents | 5.176 MiB |
| 00:07:30–00:08:30 | 1,975 → 2,338 MiB | 2.191 core equivalents | 70.430 MiB |

The same guest worker, Windows TID `00e8`, repeatedly consumes 81–91% of a
CPU while sampled in `find_file_in_dir/fstatat`, `get_dir_case_sensitivity`,
`openat`, or file metadata handling. The plateau is therefore consistent
with ongoing path/file work before more resources become resident, not proof
of an idle game or a RAM allocation cap. Cached reads still make the physical
disk counter an incomplete view of file traffic. No path/status samples from
this stage exist in the old startup-limited failure trace, so it is not yet
known whether the remaining work is parent-case resolution, missing resources,
repeated probes, or predominantly successful opens. Do not add a result cache
or change game files without that evidence.

This launch also shrinks the requested 1,024 MiB JIT pool to 432 MiB because
of address-space placement. Low Power Mode remains enabled. No cache flush
message was found; that absence does not prove a flush could not occur. The
repeated native file stacks are stronger evidence for the observed plateau
than the startup small-pool warning alone.

### Audio evidence and limits

The persistent 48 kHz float-stereo stream starts successfully and continues
through the log. Its 21 ten-second delivery reports show 99.7–100.3% pacing,
zero underruns, and roughly 2,001 actual render callbacks per interval. All
22 source-level reports are rounded to zero. A separate, short-lived SDL
44.1 kHz stream appears with `house_of_ruin.ogv`; this is the likely intro
audio, not directly proven by the source samples.

Existing level measurement happens **after** honoring the client's
`AUDCLNT_BUFFERFLAGS_SILENT`. It cannot distinguish the client requesting
silence from an unflagged zero signal, nor prove missing FMOD banks. It also
does not establish when the game should have started normal audio. No output
buffer/session change is justified by these data.

### Bounded diagnostic candidate

The next candidate adds observation, not another loading/audio behavior fix:

- `MADEIRA_FILE_PROFILE` defaults to 1 in this diagnostic candidate; 0 disables
  clocks/reporting. `NtCreateFile`/`NtOpenFile`, `NtQueryAttributesFile`, and
  `NtQueryFullAttributesFile` collect thread-local elapsed
  lookup and total request time, complete-path hits/misses, parent-shortcut
  attempts/hits, component visits, case queries, directory-scan attempts and
  entries visited. Totals exclude the new report formatting/writing and the
  older end-of-call open logging. Operation counts and path samples distinguish
  actual opens from the two metadata queries. Nested calls are folded into the
  outer request; other file APIs are not counted.
- At most four lines per active thread per five seconds: totals, the costliest
  request, the costliest failed read-open/query, and an audio-extension sample.
  `.bank/.fsb/.fev/.wem/.wav/.ogg/.mp3` actual open successes/failures are counted separately;
  an audio success cannot erase a failed audio sample in that interval. These
  extensions are a diagnostic filter, not evidence of this game's bank format.
  Paths keep at most 255 ASCII-sanitized characters (tail for long paths).
  Reporting happens on a completed request; an idle thread's final partial
  interval is not flushed. An indefinitely blocked request cannot report its
  completion. Existing thread sampling remains needed for those cases.
- Audio source reports distinguish SILENT-flagged frames, unflagged frames,
  and sampled exact-zero/nonzero/nonfinite values. Per-stream release totals
  cover intro streams shorter than ten seconds. The existing sample scan is
  reused; no logging or additional locks enter the real-time mixer callback.

Host checks cover disabled/nested profiling, timing and interval reset, 10,000
calls without a log flood, audio-extension statuses, path bounds and errno.
The 960 existing file-lookup fixtures remain equivalent. Extracted production
audio tests verify submitted ring bytes remain unchanged, flag/zero/tiny-signal
classification, PCM16/32, nonfinite samples, interval and short-stream reports.
Host checks do not establish device overhead or resolve the underlying issue.

The user narrows the iOS system-clock offset to Madeira's game-starting screen
in landscape. `LibraryModel.begin` sets both `launching` and `current`; the main
session therefore already requests status-bar hiding during loading. The loading
HUD is hosted in the separate controls window, whose root has no explicit hide
preference. This is an ownership hypothesis, not proof of a clock-coordinate
defect. Madeira does not draw the clock and uses native scene rotation. A
full-screen screenshot is still needed before changing this system-UI behavior.

### Built diagnostic: version 0.1.3 build 25

Native [run 38071462636](https://github.com/LordierClaw/Madeira/actions/runs/38071462636)
and all three host jobs in
[run 38071462054](https://github.com/LordierClaw/Madeira/actions/runs/38071462054)
pass for source `1a30c74d538d3945ada0fe83787cc97391f61a72`, Wine
`aa758165ef1e3d00cfeeecf33627b676b42f4da5`. LLVM is reused from `38030334310`;
runtime is rebuilt, graphics and DXMT reuse matching recipes. Run 24 was
cancelled before completion to include the generated diagnostics setting.

Local output: `Madeira-0.1.3-LoadingAudioProbe.ipa`, **159,102,594 bytes**,
SHA-256 `f8590bba81e5f78bfc47a4de7c70c47c240f56a2cc89c95693c38cca8319bac7`.
The native archive SHA-256 is
`ce43431d9f5c7e1f6286eaa9a2f3f4f11c7cc015a6e638e8741b7f3922b53fb5`.
The local packager verifies ZIP integrity, payload hashes and source/runtime/
graphics receipts. Reading the final IPA confirms both diagnostic string sets,
the exact source/Wine pins and JIT/debug/increased-memory entitlements. The
unchanged compatibility input is the checksum-pinned ClickKeyFix IPA recorded
in the adjacent manifest. These artifacts and device logs stay outside Git.

This candidate is for evidence collection. No loading-speed or audio fix is
claimed. Keep the save/DLC/settings constant, note load start/finish, and allow
about 30 seconds after reaching gameplay before collecting the log. If audio
fails, note that interval too. Include a screenshot of the misplaced system
clock on Madeira's starting screen if it recurs. The subsequent result is below.

## Build 25 device result and remaining bottleneck, 2026-10-11

`madeira-log (4).txt` identifies build 25. The user reports normal system-clock
placement and audio this session, but continued slow loading and higher RAM.
This does not establish a permanent fix for the intermittent audio/UI symptoms.
The persistent game stream now contains nonzero samples after intro and near
the end of loading, consistent with the audible result. No audio/UI patch is
included in the following optimization.

The new profiler identifies the dominant work, rather than just its stack:
17 consecutive game-worker (`00e4`) windows, ending approximately
01:07:42–01:09:03, report **85.009 seconds** elapsed, **40,968** opens,
**38,080 failures (92.95%)**, **75.894 seconds in path lookup (89.28% of elapsed)**,
and 81.144 seconds total inside the measured file APIs. They visit 403,635 path
components and perform 76,160 directory scan attempts, inspecting 742,678
entries. The existing full-parent shortcut succeeds only 39 times in 31,222
attempts. All sampled costliest failures in these windows return
`STATUS_OBJECT_PATH_NOT_FOUND` at DLC/feature/mode override paths, for example
`dlc/580100_crimson_court/features/districts/modes/radiant/monsters/...`.
These samples are not an exhaustive list of all failed opens. They are
consistent with the game's search for optional overrides; they do not prove
that DLC is missing or corrupt.

The build 23 shortcut only helped when the complete parent existed. Here an
intermediate directory is absent, so Wine still rechecks the many exact
ancestors from the root before doing case-insensitive and reparse searches at
the missing component. Directory scan counts are twice the failures because
the normal resolver also tries Wine's `?` reparse backing name.

RAM also differs from the previous run:

| Build 25 device-local interval | Process footprint | Process CPU / elapsed | Physical disk reads |
|---|---|---|---|
| 01:08:00–01:08:30 | 2,355 → 2,356 MiB | 1.706 core equivalents | 1.96 MiB |
| 01:08:30–01:09:00 | 2,356 → 2,357 MiB | 1.687 core equivalents | 2.24 MiB |
| 01:09:00–01:09:30 | 2,357 → 2,606 MiB | 2.201 core equivalents | 41.30 MiB |
| 01:09:30–01:10:00 | 2,606 → 2,848 MiB | 2.595 core equivalents | 39.37 MiB |

Peak sampled footprint is 2,919 MiB (build 23: 2,455 MiB). The actual JIT pool
is **624 MiB**, versus **432 MiB** in build 23, even though both requested
1,024 MiB: address-space placement selected different sizes. The region walk
also records a larger FEX allocation band; near the plateau its dirty count is
about 574 MiB versus 400 MiB in build 23. RX/RW aliases overlap in physical
pages: do not add both pool bands to explain the footprint. The first footprint
sample is already 712 MiB versus 526 MiB before game loading. These sessions
therefore are not a controlled memory comparison. A flat interval followed by
resource growth is not evidence of a leak, and this log cannot exclude one.
No growing cache was added by the fixed-size thread-local diagnostics. The
measured bottleneck during the flat interval is path work, not a demonstrated
RAM cap or disk-bandwidth limit. Low Power Mode is enabled in both sessions.

Analysis uses the original unprefixed profiler/audio records, preserving equal
successive reports; `[xp]` records are deduplicated by their embedded timestamp.
Profiler wall-clock placement uses the nearest preceding `[xp]` and is approximate;
the 85.009-second total comes from the profiler's own monotonic spans. Exact
user-marked save-load boundaries are still unavailable. Raw logs and the local
analysis JSON remain outside Git.

### Targeted ancestor search

After a complete-path miss, the iOS resolver still tries the full parent first.
If that fails, it binary-searches ancestor boundaries for a freshly verified
directory, then resumes the original resolver at the next component. It handles
trailing separators and preserves UTF-16 offsets separately from UTF-8 buffer
positions. Case matching, short names, reparse handling, final NT status and
create/overwrite rules remain in the original resolver. No negative result is
cached or returned merely because an ancestor probe failed; no game paths are
hardcoded. Unix-namespace paths retain their original behavior.

The real-function host reproducer failed before this edit: a representative
missing DLC parent took 15 `stat` calls (14 without either fork shortcut). It
now takes **8**, with or without a trailing separator, and visits only the
missing component and its reparse candidate. The existing leaf-case fixture
remains at 3 calls. **4,752** differential fixtures pass, including varying
depth/missing-ancestor boundaries, case/UTF/short names/reparse, directory
handles, create/overwrite, buffer growth and filesystem changes. The bounded
profiler tests also pass. `parent_try` now counts candidate ancestor stats;
`parent_hit` counts requests that resume after a verified prefix, so interpret
the next device log using these updated meanings.

This removes redundant ancestor work but still performs the normal case/reparse
directory scans. The syscall reduction is not a measured game speedup or RAM
reduction. Another same-save device run remains necessary.

### Built candidate: version 0.1.3 build 26

Native [run 38075335460](https://github.com/LordierClaw/Madeira/actions/runs/38075335460)
and host [run 38075335110](https://github.com/LordierClaw/Madeira/actions/runs/38075335110)
pass for source `a62c24ee8dc6c94928926596004ff63fc5a2fa72`, Wine
`c5c322e300c8990c5a04861ccedbc816207bb2e2`. LLVM is reused from `38030334310`;
runtime is fresh, graphics/DXMT use matching recipe caches. Native archive
SHA-256: `40f457dbdd9a3139a163e0d1f455e7c298451b193c17e391d52f794ee35799f5`.

Local IPA: `Madeira-0.1.3-LoadAncestor.ipa`, **159,101,136 bytes**, SHA-256
`696eeb91c462f549443abf4459b7be9f3ad61e09f374377b275a5515664d42ba`.
Packaging passes ZIP/hash/source/runtime/graphics checks using the unchanged,
checksum-pinned ClickKeyFix compatibility input. The final IPA verifies build
26, exact source/Wine receipts, diagnostic markers and native JIT/debug/
increased-memory entitlements. IPA, manifest, checksum and logs remain outside
Git. These are build/packaging results, not an iPhone loading-speed result.

Retest the same save/dungeon and DLC selection, with the same configuration.
Keep Low Power Mode and renderer settings constant for this comparison. Note
the actual JIT pool reported at launch because the requested pool can shrink
differently. Compare loading time and worker lookup time/call counts; assess
RAM separately after reaching the same scene. No memory/cache configuration
change is included in build 26. Physical-device results are pending.

## Build 26 device result and repeated-search optimization, 2026-10-11

`madeira-log (5).txt` identifies build 26. Loading is still slow. Comparing
worker `00e4` windows with more than 2,000 calls, over 92% failures and spans
under 5,100 ms gives the following comparable workload pattern. These are
profiler intervals, **not user-marked whole save/dungeon loading times**:

| Measurement | Build 25 | Build 26 |
|---|---:|---:|
| Selected windows / elapsed | 15 / 75.008 s | 11 / 55.003 s |
| Calls / failures | 36,229 / 33,773 | 37,060 / 34,542 |
| Lookup time | 67.250 s | 47.241 s |
| Lookup time per call | 1,856 us | 1,275 us |
| Verified ancestor hits | 27 | 34,542 |
| Ancestor stat attempts | 27,729 | 156,016 |
| Component visits / scan attempts | 357,780 / 67,546 | 69,084 / 69,084 |

Build 26 reduces lookup time per call by about 31% in this selection. However,
lookup still occupies about 86% of the selected elapsed time. It now skips
existing ancestors successfully, but finds them anew using about 4.5 candidate
stats per failed path and still scans the same directory twice: the normal
name, then Wine's `?` reparse backing name. Sampled paths remain optional DLC,
mode and asset overrides; these failures do not establish a broken DLC install.
Native stacks still sample `fstatat`, `lookup_unix_name`, `find_file_in_dir` and
`get_dir_case_sensitivity` on that worker. This supports optimizing this layer
without changing the renderer, DLC selection or JIT allocator.

### Memory evidence and limits

During 01:50:00–01:51:00, footprint stays about 2,231–2,238 MiB. CPU averages
about 1.7 cores, physical reads total about 8.1 MiB and page-ins total 10.
Cached filesystem traffic is not included in physical reads. There is no
demonstrated paging bottleneck during this plateau. Footprint subsequently
grows while resources load. The `[xp]` sampled peak is 2,710 MiB; the faster
`[footprint]` sampler catches 2,765 MiB. Build 25's sampled peak was 2,919 MiB.
These sessions are not controlled RAM comparisons and cannot prove or rule
out a leak.

The actual JIT pool is 592 MiB (build 25: 624 MiB), despite the same 1,024 MiB
request. RX/RW aliases share physical pages and must not be added together.
The FEX allocation band's plateau dirty count is about 422–425 MiB versus
about 574 MiB in build 25; native malloc usage is around 86 MiB during the
plateau and reaches 100 MiB near the end. Final footprint settles to 2,690 MiB
after the 2,765 MiB fast sample, while compressed memory rises to 642 MiB.
This later compression is distinct from the earlier flat loading interval.
Later
game-cache summaries add only 110 real compilations over 98,304 lookups, which
does not indicate a compilation storm. The existing 2 MiB-per-thread FEX L1
cache mode is already active.

A smaller per-game JIT pool could be a separate controlled RAM experiment,
but cannot be inferred to accelerate this load. A 512 MiB pool would have
only about 52 MiB left at the observed final head/tail usage, and different
startup placement can shrink it further or degrade code-buffer allocation.
Do not lower the universal default or discard executable pages based on this
log: `virtual_ios.c` documents the iOS execution-permission problem following
page replacement and deliberately warms those mappings. This candidate leaves
JIT/FEX memory configuration unchanged; **no RAM reduction is claimed**.

### Implementation and verification

- Keep at most 32 per-thread ancestor hints in fixed storage (17,024 bytes
  plus an index), with no retained handles or unbounded cache. Match root fd,
  base offset and path through the next unresolved component, then freshly
  stat the parent. Always resolve the child normally. A stale parent discards
  the hint; a now-resolvable child retires a shallow hint so the next miss
  relearns the deeper parent. New files and directory changes are not hidden.
- During the normal case-insensitive directory scan, observe whether the
  reparse spelling is present. A complete, error-free scan without a candidate
  lets the following reparse lookup skip the duplicate scan and case query.
  It **retains the literal native stat**, necessary for APFS normalization
  equivalence that Wine's case comparison does not implement. Incomplete
  reads and positive candidates retain the old fallback. Nothing caches a
  missing result across requests; Unix-namespace paths retain their behavior.
- Add `prefix_hint` and `reparse_saved` to bounded profile reports. The latter
  counts duplicate scans avoided after a failed native reparse stat. Normal
  and reparse component counters still count both lookup attempts.

The missing-parent fixture takes 8 stats cold and **4 on repeated probes**,
versus 8 each time in build 26 and 14 without the iOS shortcuts. Repeated
probes use one directory scan instead of two. The case-mismatched positive
leaf remains at 3 stats. A regression first demonstrated a shallow-hint cost
of 12 stats before scoping hints to the unresolved component. Independent
review also reproduced the Unicode reparse failure in an earlier draft;
retaining the literal stat fixes both normal and open-reparse cases.

**4,820 differential fixtures** pass with actual extracted lookup functions:
case/UTF/reparse/short-name paths, statuses and NT offsets, root handles and
base offsets, dispositions, buffer growth, directory/file replacement,
new reparse entries, interrupted enumeration, deeper-tree creation, eviction
and overlong hints. The profiler checks pass. The filesystem is mocked;
these checks do not establish iOS latency or real game speedup. Device logs,
analysis scripts and their derived JSON remain outside Git.

Retest the same save/dungeon, DLC, renderer and Low Power Mode setting after
building this candidate. Record elapsed load time and collect a log after
reaching the same scene. Compare lookup time/call, `prefix_hint`,
`reparse_saved`, scans and actual JIT pool separately from final footprint.

### Built candidate: version 0.1.3 build 27

Native [run 38112018449](https://github.com/LordierClaw/Madeira/actions/runs/38112018449)
and all three host jobs in
[run 38112011394](https://github.com/LordierClaw/Madeira/actions/runs/38112011394)
pass for source `0e2f884fbda132e78acbedf57e142bbd82494042`, Wine
`7a940b7fa3bf67b66f7a30bf225fd3fe3b38941b`. Runtime is freshly rebuilt;
LLVM is reused from `38030334310` and graphics/DXMT use matching recipe caches.
Native archive: **97,056,659 bytes**, SHA-256
`d135f06b98d775d2885f85314be6896e343c6adedabd189ccd4dfc7920f92264`.

Local IPA: `Madeira-0.1.3-LoadReuse.ipa`, **159,105,657 bytes**, SHA-256
`fded79acafbf1225dd67ca2e635fa86acf108e95113c443c83b630a4cec5f131`.
The local packager passes ZIP integrity, payload hashes, source/runtime/
graphics receipts and rebuilt-file precedence using the unchanged checksum-
pinned ClickKeyFix compatibility input. Reading the final IPA verifies version
27, the exact source/Wine pins, new `prefix_hint`/`reparse_saved` diagnostics,
and native JIT/debug/increased-memory entitlements. Artifact, manifest,
checksum and raw logs remain outside Git. Re-sign through the sideload tool
as usual. Device loading-speed and memory results remain pending.

## Follow-up: DLC content management and persistent lookup reuse

The user asks why repeated parent searches/scans are still necessary and
suggests optimizing DLC file management. The following is a source-backed
investigation and proposed direction, **not implemented in build 27**.
Continue development and review directly without subagents.

### Findings

- Build 27 stores verified ancestor hints, not complete directory contents or
  persistent missing-file results. It reduces repeated work per request but
  deliberately does not yet eliminate scans between separate requests.
- Wine already has `get_cached_dir_data` in `dlls/ntdll/unix/file.c`, used by
  `NtQueryDirectoryFile`. That is a directory-enumeration snapshot tied to a
  Windows handle/mask. The `lookup_unix_name` / `find_file_in_dir` path used
  to resolve these opens does not consume it. Its lifetime and stale-snapshot
  behavior must not be copied blindly into an open-path cache.
- `DepotDownloader.install` already parses all selected base/DLC manifests,
  and `prepare` derives the actual sanitized paths and folds directory
  spelling. The install record is written after transfer completion, but
  records depot/manifest IDs and sizes, not a runtime path index. Raw depot
  payloads are persisted only for depots with custom executables; even these
  are not a Wine lookup index. `DepotManifest.FileEntry` contains chunk data
  that would be unnecessary overhead for runtime name lookup.
- Madeira pauses its downloads during a game session
  (`SteamOwnedLibrary.sessionChanged`). That reduces one source of changes,
  but does not make the tree immutable: Valve's client, the game, mods and
  native file operations can still change files.
- Of the 27 original `00e4` worst-failure path samples in the build 26 log,
  21 are under DLC directories, across five add-ons; six are outside DLC.
  These are selected interval samples, not the distribution of all requests.
  A general asset-directory lookup cache can also serve the base-game paths.

### Recommended direction

Use a bounded, versioned directory-name index in the runtime, with optional
priming from the installer. On a verified cache hit, resolve names from that
index instead of searching ancestors and enumerating the same directory
again. Existing installations should acquire entries lazily as directories
are actually accessed; a first launch must not simply move the same long
full-tree scan into Madeira's loading screen.

For new installs/updates, retain a compact index derived from **successfully
installed paths after sanitization**, bound to app/depot/manifest identities
and a completed-install generation. Publish it atomically after success and
invalidate the old generation before modification starts. Cancellation or
partial writes must never leave an authoritative complete index. Manifest
absence alone cannot mean filesystem absence: mods, generated files and
files retained from earlier versions may be present.

Runtime freshness is the correctness requirement. Evaluate directory identity
and change metadata checks, invalidation on known create/delete/rename paths,
and native change notifications. Apple documents vnode write/attribute/
rename/delete notifications in
[kqueue(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html).
These are a candidate mechanism, not an existing connected Wine cache or a
proof that notifications alone cover every race. Parent replacement, ancestor
renames, reused root handles, modifications during enumeration and notification
failures all need handling. A small freshness check may remain; zero system
calls for mutable paths is not a requirement. An uncertain or stale entry
must fall back to the normal resolver.

Only names, parent relationships and minimal provenance belong in this index.
Bound RAM and retained directory handles, evict cold entries, and avoid
retaining manifest chunks or game asset bytes. Preserve each requested DLC
path and the game's own fallback order; never combine distinct DLC directories
or infer which add-on is enabled from its installed state. Keep the existing
cross-depot collision rejection unless ordering semantics are established.

Before implementation, settle the freshness contract and measured index
budget. Acceptance checks should cover repeated absent ancestors/leaf names,
no repeated enumeration on verified unchanged entries, base/DLC/mod paths,
Unicode/case/short names/reparse points, files created after a cached miss,
ancestor replacement, install interruption and bounded memory/handle usage.
Device validation must compare the same save/configuration and report both
lookup time and cache overhead, without attributing all RAM growth to lookup.

### Implemented candidate: bounded live names and verified-file reuse

Wine now builds name indexes lazily from actual directory contents. A process
shares 32 LRU slots, each with at most 64 KiB of packed UTF/native names and
hash buckets (2 MiB payload total, less than 64 KiB fixed metadata). Each slot
retains one vnode watch descriptor; one kqueue and at most one temporary
enumeration descriptor are additional. No manifest chunks or asset bytes enter
this cache. A busy cache mutex falls back instead of queuing callers behind an
index build. `env.MADEIRA_DIRECTORY_INDEX=0` selects the prior resolver.

Every reused entry requires a fresh native path stat (identity, permissions,
nanosecond mtime/ctime) and a nonblocking vnode-event poll. Watches are installed
before enumeration; complete EOF, stable path identity and no changes are
required before publication. Mod/DLC create/delete/rename, parent replacement
and reused root handles invalidate or select a different entry. Read/watch/
allocation failures fall back. Oversized directories remember admission failure
until a change, rather than repeatedly constructing a doomed index. The exact
native probe, literal reparse probe, short-name fallback and original first
case-collision ordering remain. Unix-namespace paths bypass the index.

The installer maintains a separate, versioned verification hint at
`steamapps/depotcache/madeira/<appID>.files-v1`. Its fixed-width records identify
depot, manifest and sanitized absolute path, plus file identity, size, owner,
mode, mtime and ctime. At most 16,384 records (1,507,328 bytes plus 28 bytes of
framing/checksum) are retained per catalog; fixed-size Data buffers and sorting
indices bound additional memory, independent of manifest or path-string size.
Excess files simply use normal verification. Existing manifest/chunk planning
memory is unchanged. The catalog does not become authoritative for runtime
absence: mods and retained assets may be outside Steam's manifests.

Preparation loads the prior catalog, removes it before changing files, and
reuses only exact fingerprint/manifest matches. Unsealed chunks are checked
even if an older resume journal marks them complete (a mod may have changed
them while paused). Repair bypasses the catalog. Successful completion publishes
atomically only unchanged, fully verified files that needed no chunk writes;
newly downloaded files become eligible at a later verification. This avoids
an extra content pass solely for indexing. Cancellation/failure before completion
leaves no authoritative generation. Uninstall removes the catalog.

`[file-profile]` adds `index_hit`, `index_build`, `index_stale`, `index_fallback`.
`[steam-index]` records bounded file/record counts, reuse and hashed chunks.
Local fault-injection checks show 10,000 repeated absent-name queries with no
new scan after warming; the pre-existing 4,820 differential cases still pass.
Darwin vnode/normalization checks and Swift transfer/catalog integration run in
CI. Native build, final IPA inspection and device timing remain separate gates.

No device speedup or total game RAM reduction is claimed from these tests.
Compare the same save/DLC/renderer settings, lookup time and index hit/fallback
counts against build 27. The new RAM budget limits index overhead, not game/FEX
allocations. Optional installer priming of Wine names is intentionally omitted:
an installed-file manifest cannot certify directory completeness after mod edits.

### Verified package: build 29

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
