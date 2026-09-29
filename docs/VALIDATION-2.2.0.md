# NextGen Disk Cache 2.2.0 candidate validation

Date: 2026-09-29. This is an unreleased candidate, not an in-game performance verdict.

Safe now performs bounded buffered archive reads even when no open needs its flags changed. The unchanged 2.1.0 DLL completed zero background reads in the same isolated Windows host; the new observed-archive mode completed 4,194,304 bytes with `patched=0 no_buffering_stripped=0`. These are actual DLL reads measured through its Windows imports, not simulated warmer output. Windows decides cache retention; completed reads do not establish an FPS or loading-time benefit.

## Source authority

- Native parent: released 2.1.0 source at `4755359`.
- Current main incorporated before the change: `10b7590b758e2a76bc77c9002429b2a9dfe14330` (documentation and workflow updates).
- Candidate: `agent/demand-cache-2.2.0`; use the CI run's full head SHA to identify its exact build.
- Original checkout and released archive remain preserved. The candidate is a linked worktree in the same project lineage.

The historic game log showed `no_buffering_stripped=0 warm_read=0 MB`. That establishes no flag stripping or warming in that session, not that every Skyrim installation opens every archive identically. The current read-only installation audit found a 2.1.0 staging DLL, but no deployed NextGen DLL/INI or entry in the available SKSE log. A houseCARL Vortex-shim provider result is staging evidence, not deployment proof.

## Checks performed

Release x64 configured and built with MSVC 19.51.36252.0. All **13 CTest checks passed** locally (48.93 seconds). CI runs the same checks before packaging:

- Actual production policy: every write intent, unsafe flag and non-read disposition remains unchanged; malformed settings are bounded; byte reservations cannot exceed the session budget.
- Actual DLL: observed archives, duplicate/failed/out-of-Data/save/write/overlapped exclusions, Unicode names, and preservation of caller errors.
- Executable-only IAT scope excludes another DLL; the explicit Experimental Detours scope includes it and attaches while another thread is opening an archive.
- Positive buffered reads with zero flag changes; disabled Minimal control; archives opened later in the work window.
- Mapped request accounting separated from completed reads; buffered strided windows reach later archive regions.
- Failed memory queries refuse work; pressure and save loading pause and resume reads, including a load beginning while a read waits for its rate slot.
- Session byte cap, finite work window and shared aggregate pacing across one or two workers.

The host writes only private generated fixtures beside its test executable, loads a copied actual DLL through `SKSEPlugin_Load`, and supplies the SDK's messaging interface. It neither launches Skyrim nor changes game files, saves or mod-manager staging. The fixed-size test archives are deliberately small; host throughput is not a gameplay benchmark.

## Package and binary checks

- Source/version/profile gate passes. Safe uses observed archives, 1 GB/session, 64 MB/archive, 64 MB/s ceiling, 15-second DataLoaded delay and 180-second work window. Drive tuning can only reduce the configured limits. Minimal disables warming. Experimental preserves its opt-in wider interception and strided sampling.
- Final ZIP CRC, exact 14-member allowlist and every packaged byte checked against the candidate's source/staged DLL.
- All three exclusive FOMOD selections checked: each installs exactly the DLL and its own selected INI.
- Exported DLL version inspected as 2.2.0 using the SDK metadata layout. Loading this metadata does not invoke `SKSEPlugin_Load`.
- Forge native audit passes: AMD64 PE32+, imports `KERNEL32.dll`, `SETUPAPI.dll`, `SHELL32.dll`. Only `SKSEPlugin_Load`, `SKSEPlugin_Query`, `SKSEPlugin_Version` are exported.
- Forge FOMOD validation passes with one exact document exemption: the preserved root `PACKAGE-NOTICE.txt` is intentionally not installed into Data. Strict mode reports this document as unreferenced. No missing mapping sources; profile INI destination collisions are mutually exclusive. The independent archive gate still requires this notice and rejects every unexpected file.
- Skyrim ship gate passes. No PDB, test executable, source, DirectStorage runtime, manager bookkeeping or agent/control documents inside the FOMOD.

CI archive hashes belong to the exact successful run and are verified again after download. Compiler versions can change DLL bytes; a local DLL hash is not substituted for a CI asset hash.

## Remaining evidence

No in-game launch, save/load route comparison, full runtime matrix or actual Vortex/MO2 installer execution was performed. No public release, tag, deployment or upload was authorized. Static/native host/installer checks establish a tool-validated candidate. They do not establish gameplay gains or universal coexistence with other SKSE plugins.

Windows behavior and hook transaction references: [file caching](https://learn.microsoft.com/en-us/windows/win32/fileio/file-caching), [CreateFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea), [best-effort PrefetchVirtualMemory](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-prefetchvirtualmemory), [Detours thread enlistment](https://github.com/microsoft/Detours/wiki/DetourUpdateThread).
