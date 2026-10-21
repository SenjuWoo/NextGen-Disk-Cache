<p align="center">
  <img src="https://raw.githubusercontent.com/SenjuWoo/NextGen-Disk-Cache/main/docs/images/logo.svg" width="72" height="72" alt="NextGen Disk Cache mark">
</p>

<h1 align="center">NextGen Disk Cache</h1>

<p align="center"><strong>A learned load cache for Skyrim. It remembers what your game reads and has it in RAM before the game asks.</strong></p>

<p align="center">
  Configurable SKSE64 derivative of Disk Cache Enabler (Archost / enpinion, ISC).<br>
  Version 2.3.0: learned load cache, load timing in the log, Safe by default.
</p>

<p align="center">
  <a href="https://github.com/SenjuWoo/NextGen-Disk-Cache/actions/workflows/build-release.yml"><img src="https://github.com/SenjuWoo/NextGen-Disk-Cache/actions/workflows/build-release.yml/badge.svg" alt="Build"></a>
  <a href="LICENSE.txt"><img src="https://img.shields.io/badge/license-ISC-e8b86d?labelColor=0d0f11" alt="ISC License"></a>
  <a href="https://github.com/SenjuWoo/NextGen-Disk-Cache/releases/tag/v2.3.0"><img src="https://img.shields.io/badge/release-v2.3.0-e8b86d?labelColor=0d0f11" alt="v2.3.0"></a>
  <img src="https://img.shields.io/badge/SKSE-SE%20%2F%20AE-8f9aa6?labelColor=0d0f11" alt="SKSE SE/AE">
</p>

<p align="center">
  <a href="#install">Install</a>
  ·
  <a href="#installer-profiles">Profiles</a>
  ·
  <a href="#measure-it">Measure it</a>
  ·
  <a href="#build">Build</a>
  ·
  <a href="#honest-status">Honest status</a>
  ·
  <a href="https://www.nexusmods.com/skyrimspecialedition/mods/185563">Nexus</a>
</p>

## Why it exists

Skyrim loads its plugins, archives and the assets around your save with one small read at a time. On a cold start (after a reboot) every one of those reads waits on the disk. A large load order spends a lot of that time idle: in one real 2.2.1 session, 3,022 plugin files and 509 archives were read in the 80 seconds before the main menu.

Since 2.3.0 the plugin learns exactly which files, and which parts of each archive, your game reads from start-up until shortly after your first save load. At the next launch it reads that data back into the Windows file cache **in parallel**, the moment SKSE loads it. The game's own requests then come from RAM.

## What it actually does

**Recording (every session, Safe and Experimental).** The plugin watches `SkyrimSE.exe`'s own `CreateFile`, `ReadFile` and `ReadFileEx` calls through the executable's import table. It never changes them. It notes:

- files under the game's `Data` folder opened for reading only, with read sharing;
- for `.bsa` / `.ba2` archives, the 64 KiB ranges that were actually read.

Recording stops 90 seconds after the first save load (or new game). The result is merged with earlier sessions: files you have not touched for four launches drop out. It is written atomically to `NextGenDiskCache.hotset` beside the log.

**Prefetch (next launch).** Before the game reads its plugins, the plugin reads the recorded data with separate read-only, fully shared handles, in first-use order:

- 4 parallel reads on NVMe, 2 on SATA or unknown drives; skipped on hard disks, where parallel reads would fight the game's own seeks;
- at most 1/8 of your RAM (512-4096 MB) by default; stops at the RAM reserve and after 120 seconds;
- any file whose size or date changed since it was recorded (a mod update) is skipped, never guessed at.

**Never recorded or read:** saves, cosaves, save backups, logs, temporary files, anything opened for writing, deleting or creating, files opened without read sharing, and anything outside `Data`. The profile is validated line by line when loaded; a path that could leave `Data` is rejected.

The original archive flag policy is still present but narrow: on 1.6.1170 the engine never sets `FILE_FLAG_NO_BUFFERING`, so it changes nothing there (`patched=0`).

## Installer profiles

| Profile | Load cache | Legacy warmer | Hooks |
| --- | --- | --- | --- |
| **Safe** (recommended) | Record + prefetch, automatic size | Off | `SkyrimSE.exe` imports only |
| **Minimal** | Off | Off | Same as Safe (control) |
| **Experimental** | Record + prefetch, 6144 MB cap, 180 s recorded | 2048 MB, strided | Process-wide Detours, random-access hint, EcoQoS opt-out, hardware profile |

The legacy warmer (Safe in 2.2.x) reads the start of every archive the game opened, after the data load. A real session showed it spreading its budget to about 2 MB per archive, the header the game had already read, and only after the save load. It stays available in Experimental and via `bEnableWarmCache=1`.

All settings live in `[HotSet]` and `[WarmCache]` of `NextGenDiskCache.ini`; each key is commented in the shipped files.

## Measure it

Every log has timing lines:

```text
Timing: game data loaded 81.3 s after the plugin loaded (main menu ready)
HotSet: prefetched 2410 MB of 2410 MB planned (24120 files, stale=3, failures=0) in 1890 ms (1275 MB/s, 4 threads, cap 4056 MB); reason=complete
Timing: save data loaded in 14.2 s (SKSE PreLoadGame to PostLoadGame; ...)
```

(Example shape, not a measurement.) The gain is largest on a **cold boot**: restart Windows, launch, load the same save. Do that once with Safe and once with Minimal. The first Safe launch only records, so compare from the second launch on. A warm relaunch (no reboot) may show little difference because Windows may still hold the files.

## DirectStorage

Removed in 2.3.0. DirectStorage reads are unbuffered and land in the caller's own buffers, not in the Windows file cache Skyrim reads from, and the engine never issues DirectStorage requests. It therefore cannot make Skyrim load faster. The parallel prefetch above gives the queue-depth benefit DirectStorage is known for while filling the cache the game actually uses. Old `[DirectStorage]` INI keys are ignored.

## Requirements

- 64-bit Windows
- Skyrim Special Edition or Anniversary Edition with SKSE64

DLL metadata lists **1.5.97**, **1.6.640**, **GOG 1.6.659** and **1.6.1170**, with signature-based version independence. The implementation uses Windows APIs and the SKSE messaging interface, without game-code offsets or Address Library. A full in-game runtime compatibility matrix remains open.

Do not install together with original Disk Cache Enabler or another copy of `NextGenDiskCache.dll`. It complements Faster Loadin' 'n' Savin' / S.L.A.C.K. (cosave I/O), SSE Engine Fixes and Faster Cell Lookup; it never touches the files those mods write.

## Install

Install with Vortex or Mod Organizer 2 from [Releases](https://github.com/SenjuWoo/NextGen-Disk-Cache/releases) or [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/185563).

1. Choose **Safe** for normal play.
2. Choose **Minimal** for troubleshooting or a load-time comparison.
3. Choose **Experimental** only when you intend to benchmark it.

The installer places `NextGenDiskCache.dll` and the selected `NextGenDiskCache.ini` in `Data\SKSE\Plugins`. Upgrade by reinstalling the full FOMOD; a 2.2.x INI keeps its old warmer setting and gains the load cache with default settings.

Log and profile (Steam): `Documents\My Games\Skyrim Special Edition\SKSE\NextGenDiskCache.log` / `.hotset`  
Log and profile (GOG): `Documents\My Games\Skyrim Special Edition GOG\SKSE\`

Delete `NextGenDiskCache.hotset` to make the plugin relearn from scratch (it also relearns by itself as your load order changes). `bLogToPluginDirectory=1` puts both files beside the DLL instead.

When reporting an issue, include the complete log, profile choice, Skyrim and SKSE versions, storage type, and whether it happens without this plugin. Uninstall by removing the mod; saves are untouched.

## Build

```powershell
.\build.ps1            # Release x64; no downloads
.\package-release.ps1  # FOMOD zip + SHA-256
ctest --test-dir build -C Release --output-on-failure
```

CI builds the exact release SHA and runs the policy checks plus an isolated Windows host against the actual DLL before packaging. The host test plays two sessions: it records real reads (sequential, seeked, positional and `ReadFileEx`), then checks that the next launch prefetched exactly those ranges, skipped a changed file, preserved the caller's last error and file position, and never recorded saves, writes, exclusive opens or files outside `Data`.

```text
dumpbin /exports NextGenDiskCache.dll   →  only SKSEPlugin_Load/Query/Version
dumpbin /imports NextGenDiskCache.dll   →  SETUPAPI, KERNEL32, SHELL32 only
```

## Project map

```text
src/main.cpp         SKSE entry, file hooks, flag policy, legacy warmer, timing
src/HotSet.*         learned load cache: recorder, profile, parallel prefetch
src/IatHook.*        executable-only import-table hooks
profiles/            Safe / Minimal / Experimental INI
fomod/               installer
deps/                Detours + SKSE headers (compile)
tools/               validation gate
tests/               policy and profile checks + isolated actual-DLL Windows host
```

## Honest status

Verified in this tree:

- Version **2.3.0**; Safe / Minimal / Experimental as documented
- Actual-DLL host checks for the load cache (record, prefetch, stale skip, exclusions, handle reuse, last error and file position) and for every 2.2.x warmer behaviour
- 2.2.1 in-game evidence (Skyrim 1.6.1170 / SKSE 2.2.8): 903,412,094 bytes warmed across 509 archives, zero failures; that same log is why Safe moved to the load cache

Not yet measured:

- In-game load-time gain from the load cache. The timing lines exist so you can measure it on your own setup.
- A complete Skyrim runtime compatibility matrix

AI tools assisted development, auditing and documentation. That does not replace verification; behaviour is limited to the published source, shipped INIs and testing record.

## Credits

- **Archost** — original Disk Cache Enabler and ISC-licensed source
- **enpinion** — original Nexus upload
- **Microsoft** — Detours
- **SKSE Team** — SKSE64 plugin interface and notices

## License

[ISC](LICENSE.txt) — original Disk Cache Enabler terms retained. See also `LICENSE.Archost-DiskCacheEnabler.txt`, `LICENSE.detours.txt` and `LICENSE.SKSE64.txt`.
