# NextGen Disk Cache 2.3.0 learned load cache

Parent: d91f528 (main; 2.2.1 release fe2969e plus README link update). Audit evidence: the user's 2026-10-21 Safe session on Skyrim 1.6.1170 / SKSE 2.2.8 with the 2.2.1 CI DLL installed: patched=0 over 54,784 opens, the warmer split 1024 MB into ~2 MB per archive across 509 archives and started 15 s after DataLoaded, which itself arrived ~80 s after plugin load.

- New src/HotSet.*: record Data files and archive 64 KiB ranges read by SkyrimSE.exe (CreateFile/ReadFile/ReadFileEx through the executable IAT, observe-only, position query via NtQueryInformationFile) until iHotSetRecordSecs after the first save load; merge with aging; atomic save; validated load; parallel buffered prefetch at plugin load (QD by drive, RAM reserve, deadline, stale skip).
- Session timing lines for DataLoaded and each PreLoadGame->PostLoadGame.
- Safe: load cache on, legacy warmer off. Experimental: load cache + legacy warmer + wide hooks. Minimal: all off.
- Removed the disabled DirectStorage backend, its SDK fetch, the CI NuGet step and the SDK license notice.
- ObserveArchive dedupe is a hash set instead of a linear scan; removed the uncalled DetachHooks.
- Tests: profile parse/save/merge/path-safety unit checks; two-session actual-DLL host test.

## Historical 2.2.1 runtime-evidence and diagnostic patch

Parent: 02a3896169f3b5acd26ce020ae29eac21ddcc973 / 2.2.0. The user installed the default Safe FOMOD and supplied a real game session: 903,412,094 completed buffered bytes across 509 archives, zero warm failures, 138 save-load pause checks and a 180-second work window. Performance gain was not measured.

- Keep the initial session budget for completion diagnostics; do not use a later empty batch's recalculated RAM budget as the exit reason.
- Extend the existing actual-DLL observed-mode regression: after its reads finish, reduce available RAM and require the deadline completion reason.
- Keep all warming settings, eligibility and safety behavior unchanged. Refresh release/version metadata, runtime evidence and user changelog.

## Historical 2.2.0 active cache rework

Native parent: 4755359 / 2.1.0. Current remote documentation/CI/community baseline: 10b7590. Historical 1.4.0 notes follow below and describe that release, not the 2.2.0 default.

- Safe enables paced, bounded buffered reads of successfully opened game archives, with a narrow IAT hook and separate read-only handles.
- Minimal is the warming-disabled comparison; all three installer choices and every existing INI key remain present.
- Memory checks run between chunks; the session budget and deadline bound work. DataLoaded defers startup and save-load messages pause it.
- Completed reads, accepted mapped requests and raw DirectStorage discard diagnostics have distinct counters. Failed opens do not count as successful flag changes.
- IAT publication preserves existing call-through pointers before atomic replacement. Experimental Detours enlists live threads before code changes and refuses attachment on errors.
- Tests exercise the production policy and actual DLL in isolated fixtures. No runtime deployment or public release is performed.

## Historical 1.4.0 safety rework

Base: `af0ab6172ae6f19c5f8511d46cd55163948c272a` (1.3.1).

## Core policy changes

- Default hook scope is limited to existing, synchronous, read-only BSA/BA2 archive opens.
- Unknown extensions, loose assets, plugins, saves, cosaves, state databases, logs, temporary files, writes, creates, truncates, overlapped handles, write-through handles, and delete-on-close handles retain the caller's flags.
- `FILE_FLAG_RANDOM_ACCESS` is archive-only.
- An internal policy self-test must pass before hooks attach.
- INI numeric values are clamped.

## Background I/O changes

- Warm cache is disabled by default.
- DirectStorage probing and raw reads are disabled by default.
- The optional warmer is limited to BSA/BA2, 512 MB, 8 MB per archive, one low-priority thread, and a 60-second delay in the supplied experimental profile.
- Auto-tune may reduce load but cannot inflate the configured budget.

## Release layout

- `SafeDefault`: recommended archive-only profile.
- `Minimal`: archive-only with optional process/hardware tuning disabled.
- `ExperimentalWarmCache`: bounded opt-in prefetch profile.

The source bundle intentionally contains no 1.4.0 DLL. Build and Windows CI validation are still required before shipping a public binary.
