Safe now performs active, bounded archive warming instead of depending on the game using NO_BUFFERING archive opens. Install the full FOMOD and select **Safe**; replacing only the DLL preserves an older INI's warming-disabled setting.

- Read-only warming of archives successfully opened by Skyrim, with session/per-file/rate/deadline limits and memory/save-load guards.
- Minimal provides a warming-disabled comparison. Experimental remains an explicit wider-hook/sampling option.
- Default Safe activity observed in Skyrim 1.6.1170 / SKSE 2.2.8: the 2.2.0 build read 903,412,094 bytes across 509 archives with zero warm failures. This patch preserves that warming behavior and corrects the misleading completion reason.
- CI runs 13 actual-DLL/production-policy checks, then validates the final archive and all three installer choices. The installer ZIP contains the DLL, profile choices and required notices; symbols are separate.

Cache activity is verified; comparative loading/FPS gains and a complete runtime matrix are not. This release does not include DirectStorage runtime DLLs.
