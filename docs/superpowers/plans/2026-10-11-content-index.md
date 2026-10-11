# Bounded content index implementation plan

Execute directly with the executing-plans workflow. The user explicitly requires
no subagents and authorizes implementation, fork commits and an IPA build.

**Goal:** remove repeated directory enumeration during asset probes and repeated
verification of unchanged base/DLC files, without hiding mod or DLC changes.

**Architecture:** Wine lazily indexes actual directory names, independently of
Steam manifests. A Steam verification catalog records only successfully verified,
unchanged files, with depot/manifest/path identity and native file fingerprints.
Manifest absence never implies filesystem absence. Runtime priming from the
installer is deliberately optional: it cannot certify names added by mods.

**Spec:** docs/LOADING_PERFORMANCE.md, follow-up investigation.
**Tech stack:** Wine C, Darwin kqueue, Swift/Foundation, extracted host fixtures.

## Constraints and acceptance

- [x] Runtime: 32 process-wide directory slots, at most 64 KiB name storage each,
  less than 64 KiB fixed metadata, at most 32 watch descriptors plus one kqueue.
- [x] Fresh path identity/stat plus native vnode events on every reused index;
  watcher errors, incomplete enumeration, changes while building, oversized
  directories and allocation failures fall back to the existing resolver.
- [x] Keep APFS literal probes, reparse priority, short-name behavior, case
  collision ordering and non-iOS/Unix semantics.
- [x] Installer: bounded persistent verification catalog; atomic success-only
  publication, invalidate before writes, fingerprint changes force hashing.
  Repair always hashes. No full-tree scan or second full-file read just to index.
- [x] Add mutation, failure, bound/eviction and warm-scan checks. Run existing
  lookup differential fixtures, transfer/resume/DLC/repair checks, and real Darwin
  filesystem/watcher checks in CI. Review directly, without delegation.
- [x] Commit Wine first, update source pin and maintenance docs, push parent;
  require host and fresh native CI success, package and inspect the final IPA.

## Review focus

Directory replacement with identical spelling, reused root handles, events
during enumeration, Unicode normalization and duplicate folded names; modified
files with unchanged size/mtime; cancellation and stale catalogs; cold indexing
cost and bounded RAM. Host syscall reductions are not device timing results.

## Completion evidence

- Wine `8b948ac0`; app implementation `4b46a89`, Xcode audit comment correction
  `94a31b7`, preparation metadata reuse `d25a5d9`.
- The first host run stopped on the project source-comment convention. The
  comment was corrected; final run 38115441553 passes all three jobs.
- 4,820 existing lookup differential fixtures, 10,000 warm misses without new
  scans, mutable/failing/oversized/evicted directory fixtures, and real Darwin
  vnode, normalization and concurrent-write checks pass. Darwin checks use
  ASan/UBSan; Swift catalog/download integration uses ASan.
- Verified unchanged installs and download-all-missing DLC hash no base chunks.
  Repair hashes, restored-mtime edits are repaired, malformed/oversized catalogs
  fall back, old generations are removed before mutation, and uninstall cleans up.
- Native 38115986505 and final build 29 IPA verification pass. Full hashes and
  dependency provenance are recorded in FORK.md and upstream-state.json.
- Optional installer-to-Wine priming was not implemented: runtime absence needs
  an actual mutable-directory snapshot, including mods. The two indexes share
  no claim that manifests list all runtime files.
- Device timing/RAM measurement remains pending, with rollback through
  env.MADEIRA_DIRECTORY_INDEX=0. No FEX or speculative memory settings changed.
