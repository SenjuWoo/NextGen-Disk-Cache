#pragma once

// Learned load cache ("hot set").
//
// Records which files under Data, and which byte ranges of each archive,
// SkyrimSE.exe read from plugin load until shortly after the first save load.
// The next launch reads exactly that data back into the Windows file cache in
// parallel, as soon as SKSE loads the plugin, so the game's own one-request-
// at-a-time reads are served from RAM.
//
// Recording only observes requests the game already made. Prefetching only
// reads, through separate shared read-only handles. A stale or missing
// profile therefore costs a few wasted reads, never a changed game request.

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

constexpr unsigned kHotSetChunkShift = 16; // archive ranges are stored in 64 KiB chunks

struct HotSetEntry {
	std::wstring relPath;    // below Data, backslash separated
	uint64_t size = 0;       // size and last-write time when recorded: a file
	uint64_t mtime = 0;      // that changed since is skipped, never guessed at
	uint32_t firstUseMs = 0; // when the game first opened it, relative to plugin load
	uint32_t age = 0;        // sessions since the game last read it
	bool archive = false;    // archives keep chunk ranges; loose files are read whole
	std::vector<std::pair<uint32_t, uint32_t>> chunks; // half-open [first, last)
};

// --- profile ---------------------------------------------------------------
std::wstring HotSetPathKey(const std::wstring& path); // case-insensitive identity of a path
bool HotSetSafeRelativePath(const std::wstring& rel);
void HotSetCoalesce(std::vector<std::pair<uint32_t, uint32_t>>& chunks, uint32_t mergeGapChunks);
uint64_t HotSetEntryBytes(const HotSetEntry& entry);
// This session's entries first (in first-use order), then older entries that
// are still young enough, until maxBytes of planned reads is reached.
std::vector<HotSetEntry> HotSetMerge(std::vector<HotSetEntry> current,
	const std::vector<HotSetEntry>& previous, uint32_t maxAge, uint64_t maxBytes);
// False when the file is missing, unreadable or recorded for another Data folder.
// Individually invalid lines are skipped and counted in `rejected`.
bool HotSetLoad(const std::wstring& path, const std::wstring& dataPath,
	std::vector<HotSetEntry>& out, size_t& rejected);
// Writes a temporary file and atomically replaces the profile.
bool HotSetSave(const std::wstring& path, const std::wstring& dataPath,
	const std::vector<HotSetEntry>& entries);

// --- recorder (called from the executable's file hooks; never does I/O) -----
void HotSetRecordStart();
bool HotSetRecording();
// relPath = nullptr forgets the handle (it was reused for an untracked file).
void HotSetRecordOpen(HANDLE handle, const wchar_t* relPath, bool archive) noexcept;
bool HotSetIsTrackedArchive(HANDLE handle) noexcept;
void HotSetRecordRead(HANDLE handle, uint64_t offset, uint64_t bytes) noexcept;

struct HotSetRecordStats {
	uint64_t files = 0;
	uint64_t archiveReads = 0;
	bool truncated = false; // a memory cap was reached; the profile is partial
};
// Stops recording and returns what was seen, stamped with current size/mtime.
std::vector<HotSetEntry> HotSetRecordStop(const std::wstring& dataPath, HotSetRecordStats& stats);

// --- prefetch ----------------------------------------------------------------
using HotSetOpenFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

struct HotSetPrefetchConfig {
	std::wstring dataPath;
	uint64_t budgetBytes = 0;
	unsigned threads = 1;
	bool lowIoPriority = false;
	ULONGLONG deadlineTick = 0;
	const std::atomic<bool>* stop = nullptr;
	bool (*memoryHeadroom)(uint64_t upcomingBytes) = nullptr;
	HotSetOpenFn open = nullptr; // must bypass this plugin's own CreateFile hooks
};

struct HotSetPrefetchResult {
	uint64_t plannedBytes = 0;
	uint64_t readBytes = 0;
	uint32_t files = 0;
	uint32_t stale = 0;
	uint32_t failures = 0;
	const char* reason = "complete";
};

HotSetPrefetchResult HotSetPrefetch(const std::vector<HotSetEntry>& entries, const HotSetPrefetchConfig& config);
