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

- [ ] Runtime: 32 process-wide directory slots, at most 64 KiB name storage each,
  less than 64 KiB fixed metadata, at most 32 watch descriptors plus one kqueue.
- [ ] Fresh path identity/stat plus native vnode events on every reused index;
  watcher errors, incomplete enumeration, changes while building, oversized
  directories and memory pressure fall back to the existing resolver.
- [ ] Keep APFS literal probes, reparse priority, short-name behavior, case
  collision ordering and non-iOS/Unix semantics.
- [ ] Installer: bounded persistent verification catalog; atomic success-only
  publication, invalidate before writes, fingerprint changes force hashing.
  Repair always hashes. No full-tree scan or second full-file read just to index.
- [ ] Add mutation, failure, bound/eviction and warm-scan checks. Run existing
  lookup differential fixtures, transfer/resume/DLC/repair checks, and real Darwin
  filesystem/watcher checks in CI. Review directly, without delegation.
- [ ] Commit Wine first, update source pin and maintenance docs, push parent;
  require host and fresh native CI success, package and inspect the final IPA.

## Review focus

Directory replacement with identical spelling, reused root handles, events
during enumeration, Unicode normalization and duplicate folded names; modified
files with unchanged size/mtime; cancellation and stale catalogs; cold indexing
cost and bounded RAM. Host syscall reductions are not device timing results.
