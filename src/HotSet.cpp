// Learned load cache ("hot set"). Contract and safety model: see HotSet.h.

#include "HotSet.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr const char* kHeader = "NGDC-HOTSET 1";
constexpr size_t kMaxEntries = 200000;          // profile and recorder file cap
constexpr size_t kMaxRecordedRanges = 1u << 21; // recorder memory cap (16 MB of pairs)
constexpr size_t kMaxRangesPerEntry = 65536;
constexpr uint64_t kMaxProfileBytes = 64ull << 20;
constexpr uint64_t kMaxWholeFileBytes = 256ull << 20; // bigger loose files are never cached whole
constexpr uint64_t kPieceBytes = 8ull << 20;          // unit of work shared by prefetch threads
constexpr uint32_t kRecordMergeGapChunks = 4;          // close reads become one 256 KiB-scale range

std::wstring UpperKey(const std::wstring& s)
{
	return HotSetPathKey(s);
}

std::string ToUtf8(const std::wstring& s)
{
	if (s.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
	if (n <= 0)
		return {};
	std::string out(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
	return out;
}

bool FromUtf8(const std::string& s, std::wstring& out)
{
	out.clear();
	if (s.empty() || s.size() > 4096)
		return false;
	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0)
		return false;
	out.resize(static_cast<size_t>(n));
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &out[0], n) == n;
}

bool FileStamp(const std::wstring& path, uint64_t& size, uint64_t& mtime)
{
	WIN32_FILE_ATTRIBUTE_DATA fd{};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fd) ||
		(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return false;
	size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
	mtime = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
	return true;
}

uint32_t ChunkCount(uint64_t size)
{
	const uint64_t n = (size + (1ull << kHotSetChunkShift) - 1) >> kHotSetChunkShift;
	return n > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(n);
}

void ClampChunks(std::vector<std::pair<uint32_t, uint32_t>>& chunks, uint64_t size)
{
	const uint32_t limit = ChunkCount(size);
	for (auto& c : chunks)
		c.second = std::min(c.second, limit);
	HotSetCoalesce(chunks, 0); // also drops ranges emptied by the clamp
}

// Unsigned decimal followed by one space; no sign, no overflow.
bool NextNumber(const std::string& line, size_t& pos, uint64_t& value)
{
	value = 0;
	const size_t start = pos;
	while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
		const uint64_t digit = static_cast<uint64_t>(line[pos] - '0');
		if (value > (UINT64_MAX - digit) / 10)
			return false;
		value = value * 10 + digit;
		++pos;
	}
	if (pos == start || pos >= line.size() || line[pos] != ' ')
		return false;
	++pos;
	return true;
}

bool ParseRanges(const std::string& text, uint32_t maxChunk, std::vector<std::pair<uint32_t, uint32_t>>& chunks)
{
	size_t pos = 0;
	while (pos < text.size()) {
		if (chunks.size() >= kMaxRangesPerEntry)
			return false;
		const size_t end = std::min(text.find(',', pos), text.size());
		const std::string item = text.substr(pos, end - pos) + ' ';
		const size_t dash = item.find('-');
		if (dash == std::string::npos)
			return false;
		std::string firstText = item.substr(0, dash) + ' ';
		size_t a = 0, b = dash + 1;
		uint64_t first = 0, last = 0;
		if (!NextNumber(firstText, a, first) || a != firstText.size() ||
			!NextNumber(item, b, last) || b != item.size() || first >= last || last > maxChunk)
			return false;
		chunks.emplace_back(static_cast<uint32_t>(first), static_cast<uint32_t>(last));
		pos = end + 1;
	}
	HotSetCoalesce(chunks, 0);
	return !chunks.empty();
}

bool ParseEntry(const std::string& line, HotSetEntry& e)
{
	if (line.size() < 2 || (line[0] != 'A' && line[0] != 'F') || line[1] != ' ')
		return false;
	e.archive = line[0] == 'A';
	size_t pos = 2;
	uint64_t age = 0, firstUse = 0;
	if (!NextNumber(line, pos, age) || !NextNumber(line, pos, firstUse) ||
		!NextNumber(line, pos, e.size) || !NextNumber(line, pos, e.mtime))
		return false;
	if (age > 1000 || firstUse > UINT32_MAX || !e.size)
		return false;
	e.age = static_cast<uint32_t>(age);
	e.firstUseMs = static_cast<uint32_t>(firstUse);
	if (e.archive) {
		const size_t end = line.find(' ', pos);
		if (end == std::string::npos || !ParseRanges(line.substr(pos, end - pos), ChunkCount(e.size), e.chunks))
			return false;
		pos = end + 1;
	} else if (e.size > kMaxWholeFileBytes) {
		return false;
	}
	return FromUtf8(line.substr(pos), e.relPath) && HotSetSafeRelativePath(e.relPath);
}

// --- recorder state ----------------------------------------------------------
struct Recorded {
	std::wstring rel;
	uint32_t firstUseMs = 0;
	bool archive = false;
	std::vector<std::pair<uint32_t, uint32_t>> chunks;
};

std::atomic<bool> g_recording{false};
std::shared_mutex g_recLock; // game threads take it shared to look up a handle
std::vector<Recorded> g_rec;
std::unordered_map<std::wstring, uint32_t> g_recByPath;
std::unordered_map<HANDLE, uint32_t> g_recByHandle;
ULONGLONG g_recStart = 0;
uint64_t g_recArchiveReads = 0;
size_t g_recRanges = 0;
bool g_recTruncated = false;

void StopAfterResourceError() noexcept
{
	g_recTruncated = true;
	g_recording.store(false, std::memory_order_relaxed);
}

} // namespace

// --- profile -----------------------------------------------------------------
std::wstring HotSetPathKey(const std::wstring& path)
{
	std::wstring key = path;
	// Kernel32 invariant upper-casing (in place is allowed); keeps USER32 out of the imports.
	if (!key.empty())
		LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, key.c_str(), static_cast<int>(key.size()),
			&key[0], static_cast<int>(key.size()), nullptr, nullptr, 0);
	return key;
}

bool HotSetSafeRelativePath(const std::wstring& rel)
{
	if (rel.empty() || rel.size() > 1024)
		return false;
	size_t start = 0;
	for (size_t i = 0; i <= rel.size(); ++i) {
		const wchar_t c = i < rel.size() ? rel[i] : L'\\';
		if (c < 0x20 || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' ||
			c == L'<' || c == L'>' || c == L'|')
			return false; // drives, streams, wildcards and forward-slash forms are never ours
		if (c != L'\\')
			continue;
		const size_t len = i - start;
		if (!len || (len == 1 && rel[start] == L'.') ||
			(len == 2 && rel[start] == L'.' && rel[start + 1] == L'.'))
			return false; // empty, "." or ".." component: could leave Data
		start = i + 1;
	}
	return true;
}

void HotSetCoalesce(std::vector<std::pair<uint32_t, uint32_t>>& chunks, uint32_t mergeGapChunks)
{
	chunks.erase(std::remove_if(chunks.begin(), chunks.end(),
		[](const std::pair<uint32_t, uint32_t>& c) { return c.first >= c.second; }), chunks.end());
	std::sort(chunks.begin(), chunks.end());
	size_t out = 0;
	for (size_t i = 0; i < chunks.size(); ++i) {
		if (out && static_cast<uint64_t>(chunks[i].first) <=
				static_cast<uint64_t>(chunks[out - 1].second) + mergeGapChunks)
			chunks[out - 1].second = std::max(chunks[out - 1].second, chunks[i].second);
		else
			chunks[out++] = chunks[i];
	}
	chunks.resize(out);
}

uint64_t HotSetEntryBytes(const HotSetEntry& entry)
{
	if (!entry.archive)
		return entry.size;
	uint64_t total = 0;
	for (const auto& c : entry.chunks) {
		const uint64_t first = static_cast<uint64_t>(c.first) << kHotSetChunkShift;
		const uint64_t last = std::min(static_cast<uint64_t>(c.second) << kHotSetChunkShift, entry.size);
		if (last > first)
			total += last - first;
	}
	return total;
}

std::vector<HotSetEntry> HotSetMerge(std::vector<HotSetEntry> current,
	const std::vector<HotSetEntry>& previous, uint32_t maxAge, uint64_t maxBytes)
{
	std::stable_sort(current.begin(), current.end(),
		[](const HotSetEntry& a, const HotSetEntry& b) { return a.firstUseMs < b.firstUseMs; });
	std::unordered_set<std::wstring> seen;
	for (auto& e : current) {
		e.age = 0;
		seen.insert(UpperKey(e.relPath));
	}
	std::vector<HotSetEntry> older;
	for (const auto& e : previous) {
		if (e.age >= maxAge || seen.count(UpperKey(e.relPath)))
			continue;
		older.push_back(e);
		++older.back().age;
	}
	std::stable_sort(older.begin(), older.end(), [](const HotSetEntry& a, const HotSetEntry& b) {
		return a.age != b.age ? a.age < b.age : a.firstUseMs < b.firstUseMs;
	});

	std::vector<HotSetEntry> out;
	uint64_t total = 0;
	for (auto* group : { &current, &older }) {
		for (auto& e : *group) {
			const uint64_t bytes = HotSetEntryBytes(e);
			if (!bytes || bytes > maxBytes - total || out.size() >= kMaxEntries)
				continue; // one oversized file must not crowd out everything read after it
			total += bytes;
			out.push_back(std::move(e));
		}
	}
	return out;
}

bool HotSetLoad(const std::wstring& path, const std::wstring& dataPath,
	std::vector<HotSetEntry>& out, size_t& rejected)
{
	out.clear();
	rejected = 0;
	uint64_t fileBytes = 0, mtime = 0;
	if (!FileStamp(path, fileBytes, mtime) || fileBytes > kMaxProfileBytes)
		return false;
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	auto readLine = [&in](std::string& line) {
		if (!std::getline(in, line))
			return false;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		return true;
	};
	std::string line;
	if (!readLine(line) || line != kHeader || !readLine(line) || line.compare(0, 5, "data ") != 0)
		return false;
	std::wstring recordedData;
	if (!FromUtf8(line.substr(5), recordedData) ||
		CompareStringOrdinal(recordedData.c_str(), -1, dataPath.c_str(), -1, TRUE) != CSTR_EQUAL)
		return false; // another install or a moved game: relearn instead of guessing
	std::unordered_set<std::wstring> seen;
	while (out.size() < kMaxEntries && readLine(line)) {
		if (line.empty())
			continue;
		HotSetEntry e;
		if (!ParseEntry(line, e) || !seen.insert(UpperKey(e.relPath)).second) {
			++rejected;
			continue;
		}
		out.push_back(std::move(e));
	}
	return true;
}

bool HotSetSave(const std::wstring& path, const std::wstring& dataPath, const std::vector<HotSetEntry>& entries)
{
	const std::wstring tmp = path + L".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
			return false;
		out << kHeader << "\ndata " << ToUtf8(dataPath) << '\n';
		for (const auto& e : entries) {
			if (!HotSetSafeRelativePath(e.relPath) || !e.size || (e.archive && e.chunks.empty()))
				continue;
			out << (e.archive ? 'A' : 'F') << ' ' << e.age << ' ' << e.firstUseMs << ' '
				<< e.size << ' ' << e.mtime << ' ';
			if (e.archive) {
				for (size_t i = 0; i < e.chunks.size(); ++i)
					out << (i ? "," : "") << e.chunks[i].first << '-' << e.chunks[i].second;
				out << ' ';
			}
			out << ToUtf8(e.relPath) << '\n';
		}
		out.flush();
		if (!out) {
			out.close();
			DeleteFileW(tmp.c_str());
			return false;
		}
	}
	if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		DeleteFileW(tmp.c_str());
		return false;
	}
	return true;
}

// --- recorder ----------------------------------------------------------------
void HotSetRecordStart()
{
	std::unique_lock<std::shared_mutex> lock(g_recLock);
	g_rec.clear();
	g_recByPath.clear();
	g_recByHandle.clear();
	g_recArchiveReads = 0;
	g_recRanges = 0;
	g_recTruncated = false;
	g_recStart = GetTickCount64();
	g_recording.store(true, std::memory_order_release);
}

bool HotSetRecording()
{
	return g_recording.load(std::memory_order_relaxed);
}

void HotSetRecordOpen(HANDLE handle, const wchar_t* relPath, bool archive) noexcept
{
	if (!HotSetRecording() || !handle || handle == INVALID_HANDLE_VALUE)
		return;
	try {
		if (!relPath) {
			// The value may belong to a closed archive handle. Forget it so a later
			// read of this unrelated file is never attributed to that archive.
			{
				std::shared_lock<std::shared_mutex> lock(g_recLock);
				if (g_recByHandle.find(handle) == g_recByHandle.end())
					return;
			}
			std::unique_lock<std::shared_mutex> lock(g_recLock);
			g_recByHandle.erase(handle);
			return;
		}
		std::wstring key = UpperKey(relPath);
		std::unique_lock<std::shared_mutex> lock(g_recLock);
		if (!HotSetRecording())
			return;
		uint32_t index = 0;
		const auto found = g_recByPath.find(key);
		if (found != g_recByPath.end()) {
			index = found->second;
		} else if (g_rec.size() >= kMaxEntries) {
			g_recTruncated = true;
			g_recByHandle.erase(handle);
			return;
		} else {
			index = static_cast<uint32_t>(g_rec.size());
			Recorded r;
			r.rel = relPath;
			r.firstUseMs = static_cast<uint32_t>(std::min<ULONGLONG>(GetTickCount64() - g_recStart, UINT32_MAX));
			r.archive = archive;
			g_rec.push_back(std::move(r));
			g_recByPath.emplace(std::move(key), index);
		}
		if (archive)
			g_recByHandle[handle] = index;
		else
			g_recByHandle.erase(handle);
	} catch (...) {
		StopAfterResourceError();
	}
}

bool HotSetIsTrackedArchive(HANDLE handle) noexcept
{
	if (!HotSetRecording())
		return false;
	std::shared_lock<std::shared_mutex> lock(g_recLock);
	return g_recByHandle.find(handle) != g_recByHandle.end();
}

void HotSetRecordRead(HANDLE handle, uint64_t offset, uint64_t bytes) noexcept
{
	if (!bytes || !HotSetRecording() || offset + bytes < offset)
		return;
	const uint64_t first = offset >> kHotSetChunkShift;
	const uint64_t last = (offset + bytes + (1ull << kHotSetChunkShift) - 1) >> kHotSetChunkShift;
	if (last > UINT32_MAX)
		return;
	try {
		std::unique_lock<std::shared_mutex> lock(g_recLock);
		const auto found = g_recByHandle.find(handle);
		if (found == g_recByHandle.end())
			return;
		++g_recArchiveReads;
		auto& chunks = g_rec[found->second].chunks;
		// Sequential reads extend the previous range instead of growing the list.
		if (!chunks.empty() && first >= chunks.back().first && first <= chunks.back().second) {
			chunks.back().second = std::max(chunks.back().second, static_cast<uint32_t>(last));
			return;
		}
		if (g_recRanges >= kMaxRecordedRanges) {
			g_recTruncated = true;
			return;
		}
		chunks.emplace_back(static_cast<uint32_t>(first), static_cast<uint32_t>(last));
		++g_recRanges;
	} catch (...) {
		StopAfterResourceError();
	}
}

std::vector<HotSetEntry> HotSetRecordStop(const std::wstring& dataPath, HotSetRecordStats& stats)
{
	std::vector<Recorded> recorded;
	{
		std::unique_lock<std::shared_mutex> lock(g_recLock);
		g_recording.store(false, std::memory_order_relaxed);
		recorded.swap(g_rec);
		g_recByPath.clear();
		g_recByHandle.clear();
		stats.files = recorded.size();
		stats.archiveReads = g_recArchiveReads;
		stats.truncated = g_recTruncated;
	}
	std::vector<HotSetEntry> out;
	out.reserve(recorded.size());
	for (auto& r : recorded) {
		// An archive opened but never read through the executable has no known ranges;
		// reading all of it would be a guess, so it is left out.
		if (r.archive && r.chunks.empty())
			continue;
		HotSetEntry e;
		if (!FileStamp(dataPath + L"\\" + r.rel, e.size, e.mtime) || !e.size ||
			(!r.archive && e.size > kMaxWholeFileBytes) || !HotSetSafeRelativePath(r.rel))
			continue;
		e.relPath = std::move(r.rel);
		e.firstUseMs = r.firstUseMs;
		e.archive = r.archive;
		e.chunks = std::move(r.chunks);
		if (e.archive) {
			HotSetCoalesce(e.chunks, kRecordMergeGapChunks);
			ClampChunks(e.chunks, e.size);
			if (e.chunks.empty())
				continue;
		}
		out.push_back(std::move(e));
	}
	return out;
}

// --- prefetch ----------------------------------------------------------------
HotSetPrefetchResult HotSetPrefetch(const std::vector<HotSetEntry>& entries, const HotSetPrefetchConfig& config)
{
	struct Piece {
		uint32_t entry;
		uint64_t offset;
		uint64_t bytes;
	};
	HotSetPrefetchResult result;
	std::vector<Piece> pieces;
	std::vector<std::wstring> paths(entries.size());
	uint64_t planned = 0;
	for (uint32_t i = 0; i < entries.size() && planned < config.budgetBytes; ++i) {
		const HotSetEntry& e = entries[i];
		uint64_t size = 0, mtime = 0;
		paths[i] = config.dataPath + L"\\" + e.relPath;
		if (!FileStamp(paths[i], size, mtime) || size != e.size || mtime != e.mtime) {
			++result.stale; // a mod was updated or removed: its old ranges mean nothing now
			continue;
		}
		++result.files;
		auto add = [&](uint64_t offset, uint64_t bytes) {
			bytes = std::min(bytes, config.budgetBytes - planned);
			for (uint64_t done = 0; done < bytes; done += kPieceBytes) {
				pieces.push_back({ i, offset + done, std::min(kPieceBytes, bytes - done) });
			}
			planned += bytes;
		};
		if (!e.archive) {
			add(0, e.size);
			continue;
		}
		for (const auto& c : e.chunks) {
			const uint64_t first = static_cast<uint64_t>(c.first) << kHotSetChunkShift;
			const uint64_t last = std::min(static_cast<uint64_t>(c.second) << kHotSetChunkShift, e.size);
			if (last > first && planned < config.budgetBytes)
				add(first, last - first);
		}
	}
	result.plannedBytes = planned;
	if (!config.open) {
		result.reason = "no file-open function";
		return result;
	}
	if (pieces.empty()) {
		result.reason = "nothing valid to prefetch";
		return result;
	}

	std::atomic<size_t> next{0};
	std::atomic<uint64_t> readBytes{0};
	std::atomic<uint32_t> failures{0};
	std::atomic<const char*> stopReason{nullptr};
	auto halt = [&stopReason](const char* why) {
		const char* none = nullptr;
		stopReason.compare_exchange_strong(none, why);
	};
	auto worker = [&] {
		try {
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
			std::vector<char> buffer(1u << 20); // allocate before owning a handle
			HANDLE file = INVALID_HANDLE_VALUE;
			uint32_t openEntry = UINT32_MAX;
			for (;;) {
				if (stopReason.load())
					break;
				if (config.stop && config.stop->load(std::memory_order_relaxed)) {
					halt("shutdown");
					break;
				}
				if (GetTickCount64() >= config.deadlineTick) {
					halt("deadline");
					break;
				}
				const size_t index = next.fetch_add(1);
				if (index >= pieces.size())
					break;
				const Piece& piece = pieces[index];
				if (config.memoryHeadroom && !config.memoryHeadroom(piece.bytes)) {
					halt("RAM reserve reached");
					break;
				}
				if (piece.entry != openEntry) {
					if (file != INVALID_HANDLE_VALUE)
						CloseHandle(file);
					openEntry = piece.entry;
					file = config.open(paths[piece.entry].c_str(), GENERIC_READ,
						FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL, nullptr); // no sequential hint: it invites early eviction
					if (file != INVALID_HANDLE_VALUE && config.lowIoPriority) {
						FILE_IO_PRIORITY_HINT_INFO hint{};
						hint.PriorityHint = IoPriorityHintLow;
						SetFileInformationByHandle(file, FileIoPriorityHintInfo, &hint, sizeof(hint));
					}
				}
				if (file == INVALID_HANDLE_VALUE) {
					failures.fetch_add(1);
					continue;
				}
				for (uint64_t done = 0; done < piece.bytes;) {
					const uint64_t offset = piece.offset + done;
					OVERLAPPED at{}; // positional read on a synchronous handle
					at.Offset = static_cast<DWORD>(offset);
					at.OffsetHigh = static_cast<DWORD>(offset >> 32);
					DWORD got = 0;
					const DWORD want = static_cast<DWORD>(std::min<uint64_t>(buffer.size(), piece.bytes - done));
					if (!ReadFile(file, buffer.data(), want, &got, &at) || !got) {
						failures.fetch_add(1);
						break;
					}
					done += got;
					readBytes.fetch_add(got, std::memory_order_relaxed);
				}
			}
			if (file != INVALID_HANDLE_VALUE)
				CloseHandle(file);
		} catch (...) {
			failures.fetch_add(1);
			halt("resource error");
		}
	};

	std::vector<std::thread> extra;
	try {
		for (unsigned t = 1; t < std::max(1u, config.threads); ++t)
			extra.emplace_back(worker);
	} catch (...) {
		// Fewer threads only lowers queue depth; the caller's thread still works.
	}
	worker();
	for (auto& t : extra)
		t.join();

	result.readBytes = readBytes.load();
	result.failures = failures.load();
	if (const char* why = stopReason.load())
		result.reason = why;
	return result;
}
