# Default Safe game-session evidence

Observed 2026-09-29 from the user's game session, after installing the complete default FOMOD. The deployed DLL SHA-256 is `f35546862dad0916732dba727d24bfbc0a4e9e1dc26c1320c23f4593f05c23f3`, matching the verified 2.2.0 CI artifact.

- Skyrim 1.6.1170, SKSE 2.2.8; SKSE reports `NextGenDiskCache.dll` version `00020200` loaded correctly.
- Executable-only CreateFileA/CreateFileW IAT hooks attached. Full hardware profiling disabled; drive-only tuning identified NVMe.
- Safe INI matched the packaged SafeDefault profile exactly. Delay: 15 seconds after DataLoaded. One worker, 1 GB/session ceiling, 64 MB/archive ceiling, 64 MB/s aggregate ceiling, 180-second work window.
- Final counters: `opens=68640 patched=0 no_buffering_stripped=0 in_scope_safety_gated=70 warm_read_bytes=903412094 prefetch_requested_bytes=0 raw_discarded_bytes=0 observed_archives=509 queue_dropped=0 warm_failures=0 memory_pause_checks=0 load_pause_checks=138`.
- Finished after 180,172 ms, having completed reads across 509 archives. Zero flag changes accompanied positive buffered reads, confirming warming does not depend on stripping NO_BUFFERING.

The old completion reason said `no RAM headroom or disabled file limit`. Its source read the latest replanned batch's mutable budget rather than the initial session budget and actual loop termination. Version 2.2.1 corrects that diagnostic and adds a host regression, preserving the tested warmer behavior.

This establishes loaded/active cache work in one real session. It does not establish retained cache pages, a faster route/load, higher FPS, every runtime, or universal plugin compatibility. The log does not show RAM-pressure pauses in this session; the memory guards are exercised separately by the DLL host checks. The count of save-load pause checks is not a count of separate save loads.
