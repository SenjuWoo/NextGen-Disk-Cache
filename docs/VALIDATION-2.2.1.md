# NextGen Disk Cache 2.2.1 validation

Parent: `02a3896169f3b5acd26ce020ae29eac21ddcc973` / 2.2.0. Date: 2026-09-29. Old releases and the exact installed/tested parent are preserved.

The user's default Safe game session establishes active warming: 903,412,094 completed buffered bytes across 509 archives, zero warm failures, 138 save-load pause checks, and a 180,172 ms work window. Installed DLL bytes match the 2.2.0 CI build and the installed INI matches SafeDefault exactly. [Runtime evidence](RUNTIME-2.2.0.md) records the counters and their limits.

That session exposed a diagnostic bug: the final message consulted the latest empty batch's recalculated memory budget, incorrectly reporting no RAM headroom after completing work. The existing host regression now lowers available RAM only after its observed archive reads finish. It **fails against the unchanged parent DLL** with `idle RAM changes replaced the real completion reason`; the patch reports the actual deadline exit.

MSVC 19.51 Release x64 builds. All **13 local CTest checks pass** (49.10 seconds), including the reproduced diagnostic defect. The patch preserves warming settings, eligibility, read/write protections, rate/session/per-file limits, save-load/memory guards and all three installer choices. The native behavior change is the completion diagnostic's use of the initial session budget.

CI repeats the production-policy and actual-DLL host checks before packaging. Its exact-SHA run, final release tag and downloaded/published hashes are recorded in release receipts. The archive gate checks exported version 2.2.1, exact file membership and bytes, CRC, and all three exclusive installer selections. Native/FOMOD/archive audits remain separate from measured in-game performance.

No new game deployment is part of this release task; the current install stays at the user-tested 2.2.0 build. No comparative route/loading/FPS benchmark or full supported-runtime matrix was performed. The 2.2.1 diagnostic patch is host/CI validated; the unchanged warmer has the parent session's real runtime evidence. Nexus upload is prepared separately and has not been performed.
