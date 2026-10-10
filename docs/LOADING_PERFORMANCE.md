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
clock on Madeira's starting screen. Physical-device results are pending.
