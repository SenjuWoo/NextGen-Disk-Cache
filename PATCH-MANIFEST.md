# NextGen Disk Cache 2.2.1 runtime-evidence and diagnostic patch

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
