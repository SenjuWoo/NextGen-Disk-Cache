<p align="center">
  <img src="https://raw.githubusercontent.com/SenjuWoo/NextGen-Disk-Cache/main/docs/images/logo.svg" width="72" height="72" alt="NextGen Disk Cache mark">
</p>

<h1 align="center">NextGen Disk Cache</h1>

<p align="center"><strong>Active archive warming with bounded background I/O. Safe is the default.</strong></p>

<p align="center">
  Configurable SKSE64 derivative of Disk Cache Enabler (Archost / enpinion, ISC).<br>
  Version 2.2.1: read-only archive warming, paced I/O and memory safeguards.
</p>

<p align="center">
  <a href="https://github.com/ShugokiFable/NextGen-Disk-Cache/actions/workflows/build-release.yml"><img src="https://github.com/ShugokiFable/NextGen-Disk-Cache/actions/workflows/build-release.yml/badge.svg" alt="Build"></a>
  <a href="LICENSE.txt"><img src="https://img.shields.io/badge/license-ISC-e8b86d?labelColor=0d0f11" alt="ISC License"></a>
  <a href="https://github.com/ShugokiFable/NextGen-Disk-Cache/releases/tag/v2.2.1"><img src="https://img.shields.io/badge/release-v2.2.1-e8b86d?labelColor=0d0f11" alt="v2.2.1"></a>
  <img src="https://img.shields.io/badge/SKSE-SE%20%2F%20AE-8f9aa6?labelColor=0d0f11" alt="SKSE SE/AE">
</p>

<p align="center">
  <a href="#install">Install</a>
  ·
  <a href="#installer-profiles">Profiles</a>
  ·
  <a href="#build">Build</a>
  ·
  <a href="#honest-status">Honest status</a>
  ·
  <a href="https://www.nexusmods.com/skyrimspecialedition/mods/185563">Nexus</a>
</p>

## Why it exists

When Skyrim opens an eligible **`.bsa` or `.ba2`** with `FILE_FLAG_NO_BUFFERING`, Windows cannot use its normal file cache. This plugin can strip that flag on a narrow set of read-only archive opens.

The 2.1.0 Safe profile could be inert when the engine already used buffered opens. Version 2.2.0 adds active archive warming that does not depend on that flag. It uses separate read-only handles to populate the Windows file cache for archives the game actually opens.

## What it actually does

An archive open is eligible only when it is all of:

- A **`.bsa` or `.ba2`** archive
- Opened for reading only
- Opened synchronously
- Using `OPEN_EXISTING`
- Not opened with write-through, overlapped, delete-on-close, create, or truncate behaviour

The recommended **Safe** profile hooks only the relevant imports on **SkyrimSE.exe**. File operations from unrelated SKSE DLLs do not pass through that hook.

The flag policy and warmer are independent: `patched=0` can accompany positive `warm_read_bytes`. Safe observes successful eligible opens under the game Data directory, deduplicates them, then warms bounded portions of those archives after SKSE DataLoaded. It keeps accepting later opens until the session budget or deadline is reached. Opening an archive proves archive use, not which internal asset ranges the save will need.

## What it does not modify

Outside the modification policy in every supplied profile:

- Skyrim save files, SKSE cosaves, save backups
- Journals and database state files
- ESP, ESM, and ESL plugins
- Loose meshes, textures, and audio
- INI and JSON configuration files
- Logs, crash dumps, and temporary files
- Unknown file extensions
- Any handle opened for writing or deletion
- Any create, truncate, overlapped, write-through, or delete-on-close operation

The plugin does not parse, edit, or store information inside your save files.

## Installer profiles

The FOMOD contains three mutually exclusive profiles.

| Profile | Hook | Warmer | Session / per archive | Rate ceiling / delay |
| --- | --- | --- | --- | --- |
| **Safe** (recommended) | SkyrimSE.exe imports | Observed archives, buffered reads | 1024 / 64 MB | 64 MB/s / 15 s |
| **Minimal** | Same as Safe | Disabled; comparison control | 0 | No warm I/O |
| **Experimental** | Process-wide Detours | Observed archives, strided reads | 2048 / 128 MB | 128 MB/s / 30 s |

Both warmers use one low-priority worker, at most 512 distinct archives and a 180-second work window after the delay. The budget is **per process session**, not renewed per load. Small files return unused reservations; archives in a batch share the remaining budget so the first few filenames do not monopolize it.

Drive-only tuning in Safe performs no CPU/SMBIOS/GPU enumeration. It only reduces the configured caps: SATA SSD rate <=32 MB/s; HDD rate <=8 MB/s and budget <=256 MB; unknown drive rate <=16 MB/s. A configured RAM reserve, 90% memory-load ceiling and successful memory query are checked before each 1 MB buffered read (4 MB mapped request). Save-load messages pause work. A request already executing cannot be cancelled synchronously; slow-device I/O may finish after the deadline. Windows controls eviction and residency.

Safe leaves random-access hints, process-wide interception, working-set and power-throttling changes off. Experimental retains its wider hook, random-access hint, hardware profiler and EcoQoS opt-out. These are configurable options without a measured gameplay advantage.

`bWarmCacheOnlyObservedArchives=0` explicitly selects the legacy Data-directory scan; the default never scans inactive archives. `bWarmCacheMappedPrefetch=1` uses best-effort mapped requests, reported separately from completed buffered bytes. `bWarmCacheStridedPrefetch=1` samples windows across large archives with either backend. Existing INI keys remain supported.

The warmer does not know exact internal asset ranges. Compare Safe and Minimal on the same save, route and cache conditions to measure any loading or traversal benefit.

## DirectStorage status

Microsoft DirectStorage runtime DLLs are **not** in the public Nexus or GitHub FOMOD zip.

The plugin still contains an optional, dynamically resolved DirectStorage backend compiled against SDK headers. Probing and DirectStorage warm reads stay **disabled in every supplied profile**. No DirectStorage performance claim. The SDK code-license notice is included because those headers are used at compile time.

## Requirements

- 64-bit Windows
- Skyrim Special Edition or Anniversary Edition with SKSE64

DLL metadata lists **1.5.97**, **1.6.640**, **GOG 1.6.659** and **1.6.1170**. The implementation uses Windows APIs and the SKSE messaging interface without game-code offsets or Address Library. The isolated host uses a 1.6.1170-shaped SKSE interface; a full in-game runtime compatibility matrix remains open.

Do not install together with original Disk Cache Enabler, another NextGen Disk Cache, or a duplicate `NextGenDiskCache.dll`.

## Install

Install with Vortex or Mod Organizer 2 from [Releases](https://github.com/ShugokiFable/NextGen-Disk-Cache/releases) or [Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/185563).

1. Choose **Safe** for normal play.
2. Choose **Minimal** for troubleshooting or a warming-disabled comparison.
3. Choose **Experimental** only when you intend to benchmark it.

The installer places `NextGenDiskCache.dll` and the selected `NextGenDiskCache.ini` in `Data\SKSE\Plugins`. Launch through SKSE64.

Upgrade with the full 2.2.1 package and select Safe. Replacing only the DLL while retaining a 2.1.0 Safe INI preserves its `bEnableWarmCache=0` opt-out. Custom opt-outs are respected.

Log (Steam): `Documents\My Games\Skyrim Special Edition\SKSE\NextGenDiskCache.log`  
Log (GOG): `Documents\My Games\Skyrim Special Edition GOG\SKSE\NextGenDiskCache.log`

`bLogToPluginDirectory=1` optionally writes the log beside the DLL (requires a writable directory); the default uses Documents. Log lines distinguish `warm_read_bytes`, `prefetch_requested_bytes` and `raw_discarded_bytes`. Prefetch acceptance is not proof of cache residency.

For temporary diagnostics, `bLogEveryOpen=1` — then set it back to `0`. When reporting an issue, include the complete log, profile, Skyrim executable version, SKSE version, storage type, and whether it happens without this plugin.

Only one DLL/INI copy. Changing profiles: reinstall the FOMOD; do not merge INIs by hand. Uninstall by removing the mod and confirming `Data\SKSE\Plugins\NextGenDiskCache.dll` is gone. Saves are untouched.

## Build

```powershell
.\build.ps1            # Release x64 (may fetch DirectStorage SDK headers for compile only)
.\package-release.ps1  # FOMOD zip + SHA-256 (no DirectStorage runtime)
ctest --test-dir build -C Release --output-on-failure
```

CI builds the exact release SHA and runs the policy check plus an isolated Windows host against the actual DLL before packaging. The PDB and test executables stay outside the FOMOD ZIP.

```text
dumpbin /exports NextGenDiskCache.dll   →  only SKSEPlugin_Load/Query/Version
dumpbin /imports NextGenDiskCache.dll   →  SETUPAPI, KERNEL32, SHELL32 only
```

MSVC output is not bit-identical across compiler versions. Local builds may differ in hash while matching imports, exports, and behaviour.

## Project map

```text
src/                 SKSE plugin (IAT hook, archive warmer, optional DirectStorage backend)
profiles/            Safe / Minimal / Experimental INI
package/             staged SKSE/Plugins
fomod/               installer
deps/                Detours + DirectStorage SDK headers (compile)
tools/               package / validation helpers
tests/               production policy checks + isolated actual-DLL Windows host
LICENSE.txt          ISC (Archost Disk Cache Enabler derivative)
```

## Honest status

This mod changes eligible archive-open flags and performs bounded background archive reads. It does not guarantee higher FPS, faster loading screens, less traversal stutter, or lower memory use.

Verified in this tree:

- Version **2.2.1**
- Safe / Minimal / Experimental profiles as documented
- Real Skyrim 1.6.1170 / SKSE 2.2.8 log: the 2.2.0 Safe build completed 903,412,094 buffered bytes across 509 archives with zero warm failures and save-load pauses. Version 2.2.1 preserves that warming behavior and corrects its completion-reason message.
- Runtime file-policy self-test and source/package validation
- Actual-DLL host checks: positive buffered reads with zero flag changes, later opens, scoped observation, Unicode paths, aggregate pacing, failed memory queries, RAM pressure, save-load pauses and separate mapped-request counters
- DirectStorage backend compiled but disabled in every shipped profile

Not claimed:

- A public comparative in-game performance benchmark
- A complete Skyrim runtime compatibility matrix
- Universal compatibility with specific mod lists or SKSE plugins
- A guaranteed improvement from warming bytes that Windows may already have cached

AI tools assisted portions of development, auditing, and documentation. That does not replace verification. Behaviour here is limited to the published source, shipped INIs, and current testing record.

## Credits

- **Archost** — original Disk Cache Enabler and ISC-licensed source
- **enpinion** — original Nexus upload
- **Microsoft** — Detours and DirectStorage SDK headers
- **SKSE Team** — SKSE64 plugin interface and notices

This is a modified derivative of Disk Cache Enabler. The archive keeps Archost's copyright and ISC notice, this project's license, Microsoft Detours, the DirectStorage SDK code-license notice, and the SKSE64 notice.

## License

[ISC](LICENSE.txt) — original Disk Cache Enabler terms retained. See also `LICENSE.Archost-DiskCacheEnabler.txt`, `LICENSE.detours.txt`, `LICENSE.DirectStorage-Code.txt`, and `LICENSE.SKSE64.txt`.
