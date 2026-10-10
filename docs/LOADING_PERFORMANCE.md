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
