// Loads the actual packaged DLL in a private game-shaped directory. No game,
// manager, Documents, save or profile path is used by this test.
#include <Windows.h>
#include <winioctl.h>
#include <cstdint>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <stdexcept>
#include <skse/common/IPrefix.h>
#include <skse/skse64/PluginAPI.h>
#include "IatHook.h"

namespace fs = std::filesystem;
static std::atomic<uint64_t> bytesRead{0};
static std::atomic<bool> pressure{false}, failMemory{false}, triggerPressure{false};
static std::atomic<unsigned> stridedReads{0};
static std::mutex pathsMutex;
static std::vector<std::wstring> readPaths;
static decltype(&ReadFile) originalRead = ReadFile;
static decltype(&GlobalMemoryStatusEx) originalMemory = GlobalMemoryStatusEx;
static SKSEMessagingInterface::EventCallback listener = nullptr;

static void require(bool value, const char* what)
{
	if (!value) throw std::runtime_error(what);
}

static BOOL WINAPI ReadSpy(HANDLE file, LPVOID buffer, DWORD count, LPDWORD got, LPOVERLAPPED overlap)
{
	const BOOL result = originalRead(file, buffer, count, got, overlap);
	const DWORD error = GetLastError();
	wchar_t path[1024] = {};
	if (result && got && *got && GetFinalPathNameByHandleW(file, path, 1024, FILE_NAME_NORMALIZED)) {
		const std::wstring value(path);
		if (value.size() >= 4 && (value.substr(value.size() - 4) == L".bsa" || value.substr(value.size() - 4) == L".ba2")) {
			bytesRead.fetch_add(*got);
			LARGE_INTEGER zero{}, pos{};
			if (SetFilePointerEx(file, zero, &pos, FILE_CURRENT) && pos.QuadPart > *got + (4ll << 20))
				stridedReads.fetch_add(1);
			std::lock_guard<std::mutex> lock(pathsMutex);
			readPaths.push_back(value);
			if (triggerPressure.exchange(false)) pressure.store(true);
		}
	}
	SetLastError(error);
	return result;
}

static BOOL WINAPI MemorySpy(LPMEMORYSTATUSEX status)
{
	if (failMemory.load()) return FALSE;
	const BOOL ok = originalMemory(status);
	if (ok && pressure.load()) { status->ullAvailPhys = 0; status->dwMemoryLoad = 99; }
	return ok;
}

static bool Register(PluginHandle, const char* sender, SKSEMessagingInterface::EventCallback callback)
{
	require(std::string(sender) == "SKSE", "wrong messaging sender");
	listener = callback;
	return true;
}
static SKSEMessagingInterface messaging{SKSEMessagingInterface::kInterfaceVersion, Register, nullptr, nullptr};
static void* Query(UInt32 id) { return id == kInterface_Messaging ? &messaging : nullptr; }
static PluginHandle Handle() { return 1; }
static void Message(UInt32 type)
{
	require(listener != nullptr, "SKSE listener not registered");
	SKSEMessagingInterface::Message message{"SKSE", type, 0, nullptr};
	listener(&message);
}

static void MakeFile(const fs::path& path, uint64_t size = 4ull << 20)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	std::string block(1 << 20, 'x');
	out.write(block.data(), block.size());
	out.close();
	if (size >= (64ull << 20)) {
		HANDLE sparse = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		DWORD returned = 0;
		require(sparse != INVALID_HANDLE_VALUE, "sparse fixture open failed");
		const BOOL ok = DeviceIoControl(sparse, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
		CloseHandle(sparse);
		require(ok != FALSE, "sparse fixture creation failed");
	}
	fs::resize_file(path, size);
}
static void OpenW(const fs::path& path, DWORD access = GENERIC_READ, DWORD flags = FILE_ATTRIBUTE_NORMAL)
{
	HANDLE file = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, flags, nullptr);
	require(file != INVALID_HANDLE_VALUE, "fixture open failed");
	CloseHandle(file);
}
static std::string Text(const fs::path& path)
{
	std::ifstream input(path);
	return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// --- learned load cache: two sessions in one fixture -----------------------
struct SpyRead { std::wstring path; uint64_t offset; DWORD bytes; };
static std::vector<SpyRead> spyReads; // guarded by pathsMutex
static std::atomic<bool> exCompleted{false};

static BOOL WINAPI AllReadSpy(HANDLE file, LPVOID buffer, DWORD count, LPDWORD got, LPOVERLAPPED overlap)
{
	uint64_t offset = 0;
	if (overlap) offset = (static_cast<uint64_t>(overlap->OffsetHigh) << 32) | overlap->Offset;
	else { LARGE_INTEGER zero{}, pos{}; SetFilePointerEx(file, zero, &pos, FILE_CURRENT); offset = pos.QuadPart; }
	const BOOL result = originalRead(file, buffer, count, got, overlap);
	const DWORD error = GetLastError();
	wchar_t path[1024] = {};
	if (result && got && *got && GetFinalPathNameByHandleW(file, path, 1024, FILE_NAME_NORMALIZED)) {
		std::lock_guard<std::mutex> lock(pathsMutex);
		spyReads.push_back({path, offset, *got});
	}
	SetLastError(error);
	return result;
}

static VOID CALLBACK ExDone(DWORD, DWORD, LPOVERLAPPED) { exCompleted.store(true); }

static bool EndsWith(const std::wstring& value, const std::wstring& suffix)
{
	return value.size() >= suffix.size() && _wcsicmp(value.c_str() + value.size() - suffix.size(), suffix.c_str()) == 0;
}

static std::string WaitForLog(const fs::path& logPath, const char* needle)
{
	std::string log;
	for (const ULONGLONG start = GetTickCount64(); GetTickCount64() - start < 8000; Sleep(50)) {
		log = Text(logPath);
		if (log.find(needle) != std::string::npos) break;
	}
	return log;
}

static int HotSetPhase(const fs::path& root, const std::wstring& phase, const fs::path& sourceDll)
{
	const fs::path data = root / "Data", plugins = data / "SKSE" / "Plugins";
	const fs::path bsa = data / "hot.bsa", nif = data / "meshes" / "hot.nif", esp = data / "hot.esp";
	const fs::path stale = data / "hot-stale.esp", save = data / "hot.ess", outside = root / "outside.bsa";
	const fs::path writable = data / "write.bsa", exclusive = data / "exclusive.esp";
	const fs::path profile = plugins / "NextGenDiskCache.hotset";
	const fs::path logPath = plugins / "NextGenDiskCache.log";
	const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
	fs::create_directories(plugins);
	if (phase == L"record") {
		fs::remove(profile);
		MakeFile(bsa, 8ull << 20); MakeFile(nif, 1ull << 20); MakeFile(esp, 2ull << 20); MakeFile(stale, 1ull << 20);
		MakeFile(save, 1ull << 20); MakeFile(outside, 1ull << 20); MakeFile(writable, 1ull << 20);
		MakeFile(exclusive, 1ull << 20);
	} else {
		require(fs::exists(profile), "record session left no profile");
		std::ofstream(stale, std::ios::binary | std::ios::app) << "changed"; // a mod update since recording
	}
	fs::copy_file(sourceDll, plugins / "NextGenDiskCache.dll", fs::copy_options::overwrite_existing);
	std::ofstream(plugins / "NextGenDiskCache.ini", std::ios::trunc) <<
		"[FileCache]\nbEnableFileCacheHooks=1\niHookScope=0\nbPreferRandomAccessOnArchives=0\n"
		"[Hardware]\nbHardwareProfile=0\nbAutoTune=0\n[WarmCache]\nbEnableWarmCache=0\n"
		"[HotSet]\nbHotSetRecord=1\nbHotSetPrefetch=1\niHotSetBudgetMB=64\niHotSetThreads=2\n"
		"iHotSetRecordSecs=1\niHotSetMaxSecs=10\n[Log]\nbLogToFile=1\nbLogToPluginDirectory=1\n";
	const HMODULE dll = LoadLibraryW((plugins / "NextGenDiskCache.dll").c_str());
	require(dll != nullptr, "actual DLL load failed");
	if (phase == L"prefetch")
		require(IatHookInstall(dll, "ReadFile", AllReadSpy, nullptr, 0, reinterpret_cast<void**>(&originalRead)) != nullptr,
			"ReadFile spy not installed");
	const auto load = reinterpret_cast<bool(*)(const SKSEInterface*)>(GetProcAddress(dll, "SKSEPlugin_Load"));
	SKSEInterface skse{}; skse.skseVersion = 0x02020800; skse.runtimeVersion = 0x01064920;
	skse.QueryInterface = Query; skse.GetPluginHandle = Handle;
	require(load && load(&skse), "SKSE load failed");

	if (phase == L"prefetch") {
		const std::string log = WaitForLog(logPath, "HotSet: prefetched");
		require(log.find("HotSet: prefetched") != std::string::npos, "prefetch did not finish");
		require(log.find("stale=1") != std::string::npos, "changed file was not reported stale");
		const std::vector<std::pair<uint32_t, uint32_t>> allowed = {{0, 1}, {48, 50}, {96, 97}, {112, 113}};
		uint64_t bsaBytes = 0, nifBytes = 0, espBytes = 0;
		std::lock_guard<std::mutex> lock(pathsMutex);
		for (const auto& r : spyReads) {
			// The static CRT reads the DLL's own INI and profile through the same import.
			if (r.path.find(L"\\SKSE\\Plugins\\NextGenDiskCache.") != std::wstring::npos)
				continue;
			if (EndsWith(r.path, L"\\hot.bsa")) {
				const uint64_t first = r.offset >> 16, last = (r.offset + r.bytes + 0xFFFF) >> 16;
				bool inside = false;
				for (const auto& range : allowed) inside = inside || (first >= range.first && last <= range.second);
				require(inside, "prefetch read archive bytes the game never read");
				bsaBytes += r.bytes;
			} else if (EndsWith(r.path, L"\\meshes\\hot.nif")) nifBytes += r.bytes;
			else if (EndsWith(r.path, L"\\hot.esp")) espBytes += r.bytes;
			else require(false, "prefetch read a file outside the recorded valid set");
		}
		require(bsaBytes == 5ull * 65536, "archive ranges were not prefetched exactly");
		require(nifBytes == (1ull << 20) && espBytes == (2ull << 20), "loose files were not prefetched whole");
		std::ofstream(root / "result.txt") << "Prefetch read exactly the recorded set.\n" << log;
		return 0;
	}

	// Record session: act as the game through this executable's own imports.
	std::vector<char> buffer(256u << 10);
	DWORD got = 0;
	HANDLE h = CreateFileW(bsa.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	require(h != INVALID_HANDLE_VALUE, "archive open failed");
	require(ReadFile(h, buffer.data(), 64u << 10, &got, nullptr) && got == (64u << 10), "sequential read failed");
	LARGE_INTEGER to{}; to.QuadPart = 3ll << 20;
	require(SetFilePointerEx(h, to, nullptr, FILE_BEGIN) != FALSE, "seek failed");
	SetLastError(777);
	require(ReadFile(h, buffer.data(), 128u << 10, &got, nullptr) != FALSE, "seeked read failed");
	const DWORD hookedError = GetLastError();
	LARGE_INTEGER here{}, zero{};
	require(SetFilePointerEx(h, zero, &here, FILE_CURRENT) && here.QuadPart == (3ll << 20) + (128ll << 10),
		"hooked read moved the file position");
	const auto directRead = reinterpret_cast<decltype(&ReadFile)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ReadFile"));
	SetLastError(777);
	require(directRead(h, buffer.data(), 1, &got, nullptr) != FALSE, "control read failed");
	require(GetLastError() == hookedError, "ReadFile hook changed the caller's last error");
	OVERLAPPED at{}; at.Offset = 6u << 20;
	require(ReadFile(h, buffer.data(), 64u << 10, &got, &at) != FALSE, "positional read failed");
	CloseHandle(h);
	HANDLE async = CreateFileW(bsa.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	require(async != INVALID_HANDLE_VALUE, "async open failed");
	OVERLAPPED ex{}; ex.Offset = 7u << 20;
	require(ReadFileEx(async, buffer.data(), 64u << 10, &ex, ExDone) != FALSE, "ReadFileEx failed");
	for (int i = 0; i < 50 && !exCompleted.load(); ++i) SleepEx(100, TRUE);
	require(exCompleted.load(), "ReadFileEx did not complete");
	CloseHandle(async);
	const std::string ansi = nif.string();
	HANDLE a = CreateFileA(ansi.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	require(a != INVALID_HANDLE_VALUE && ReadFile(a, buffer.data(), 4096, &got, nullptr), "ANSI loose read failed");
	CloseHandle(a);
	for (const auto& path : {esp, stale, save, outside}) {
		HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		require(f != INVALID_HANDLE_VALUE && ReadFile(f, buffer.data(), 4096, &got, nullptr), "loose read failed");
		CloseHandle(f);
	}
	HANDLE w = CreateFileW(writable.c_str(), GENERIC_READ | GENERIC_WRITE, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	require(w != INVALID_HANDLE_VALUE && ReadFile(w, buffer.data(), 4096, &got, nullptr), "write-intent read failed");
	CloseHandle(w);
	HANDLE x = CreateFileW(exclusive.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	require(x != INVALID_HANDLE_VALUE && ReadFile(x, buffer.data(), 4096, &got, nullptr), "exclusive read failed");
	CloseHandle(x);
	// A closed archive's handle value reused for a loose file must not carry archive ranges over.
	CloseHandle(CreateFileW(bsa.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	HANDLE reused = CreateFileW(esp.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	to.QuadPart = 1ll << 20;
	require(reused != INVALID_HANDLE_VALUE && SetFilePointerEx(reused, to, nullptr, FILE_BEGIN) &&
		ReadFile(reused, buffer.data(), 4096, &got, nullptr), "reused-handle read failed");
	CloseHandle(reused);

	Message(SKSEMessagingInterface::kMessage_DataLoaded);
	Message(SKSEMessagingInterface::kMessage_PreLoadGame);
	Message(SKSEMessagingInterface::kMessage_PostLoadGame);
	const std::string log = WaitForLog(logPath, "HotSet: saved profile");
	require(log.find("HotSet: saved profile") != std::string::npos, "profile was not saved after the load window");
	require(log.find("Timing: game data loaded") != std::string::npos &&
		log.find("Timing: save data loaded") != std::string::npos, "session timing missing");
	const std::string text = Text(profile);
	require(text.rfind("NGDC-HOTSET 1\ndata ", 0) == 0, "profile header missing");
	require(text.find(" 0-1,48-50,96-97,112-113 hot.bsa\n") != std::string::npos, "archive ranges wrong (or reuse leaked)");
	for (const char* loose : {"meshes\\hot.nif\n", " hot.esp\n", " hot-stale.esp\n"}) {
		const size_t found = text.find(loose);
		require(found != std::string::npos && text[text.rfind('\n', found) + 1] == 'F', "loose file not recorded whole");
	}
	for (const char* excluded : {"hot.ess", "outside.bsa", "write.bsa", "exclusive.esp"})
		require(text.find(excluded) == std::string::npos, "excluded file recorded");
	std::ofstream(root / "result.txt") << text << log;
	return 0;
}

int wmain(int argc, wchar_t** argv) try
{
	require(argc >= 4, "usage: CacheHost dll other-module mode [phase]");
	const fs::path sourceDll = fs::absolute(argv[1]), sourceOther = fs::absolute(argv[2]);
	const std::wstring mode = argv[3];
	wchar_t exe[1024] = {};
	require(GetModuleFileNameW(nullptr, exe, 1024) != 0, "host path failed");
	if (argc == 4) {
		const fs::path root = fs::path(exe).parent_path() / "fixtures" / mode;
		fs::create_directories(root);
		const fs::path child = root / "CacheHost.exe";
		fs::copy_file(exe, child, fs::copy_options::overwrite_existing);
		DWORD code = 0;
		for (const wchar_t* phase : mode == L"hotset" ? std::vector<const wchar_t*>{L"record", L"prefetch"}
				: std::vector<const wchar_t*>{L"child"}) {
			std::wstring command = L"\"" + child.wstring() + L"\" \"" + sourceDll.wstring() +
				L"\" \"" + sourceOther.wstring() + L"\" " + mode + L" " + phase;
			STARTUPINFOW startup{}; startup.cb = sizeof(startup);
			PROCESS_INFORMATION process{};
			require(CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
				nullptr, root.c_str(), &startup, &process) != FALSE, "host launch failed");
			const DWORD wait = WaitForSingleObject(process.hProcess, 20000);
			if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 2);
			code = 2; GetExitCodeProcess(process.hProcess, &code);
			CloseHandle(process.hThread); CloseHandle(process.hProcess);
			if (code) break;
		}
		std::wcout << L"HOST " << mode << (code == 0 ? L" PASS\n" : L" FAIL\n");
		if (code) std::cerr << Text(root / "result.txt");
		return static_cast<int>(code);
	}
	if (mode == L"hotset") return HotSetPhase(fs::path(exe).parent_path(), argv[4], sourceDll);
	const fs::path root = fs::path(exe).parent_path(), data = root / "Data";
	const fs::path plugins = data / "SKSE" / "Plugins";
	fs::create_directories(plugins);
	fs::copy_file(sourceDll, plugins / "NextGenDiskCache.dll", fs::copy_options::overwrite_existing);
	const fs::path first = data / "active.bsa", second = data / L"active-\u65E5\u672C.ba2";
	const fs::path other = data / "other-plugin.bsa", inactive = data / "inactive.bsa";
	const fs::path late = data / "late.bsa", outside = root / "Data-other" / "outside.bsa";
	for (const auto& path : {first, second, other, inactive, late, outside, data / "save.ess", data / "write-only.bsa"})
		MakeFile(path, mode == L"strided" ? 512ull << 20 : 4ull << 20);
	const auto firstWrite = fs::last_write_time(first), secondWrite = fs::last_write_time(second);
	const bool baseline = mode == L"baseline", minimal = mode == L"minimal";
	std::ofstream ini(plugins / "NextGenDiskCache.ini", std::ios::trunc);
	ini << "[FileCache]\nbEnableFileCacheHooks=1\niHookScope=" << (mode == L"wide") << "\nbPreferRandomAccessOnArchives=0\n"
		"[Hardware]\nbHardwareProfile=0\nbAutoTune=0\n"
		"[WarmCache]\nbEnableWarmCache=" << (!baseline && !minimal) <<
		"\nbWarmCacheOnlyObservedArchives=" << (mode != L"directory") <<
		"\niWarmCacheDelaySecs=0\niWarmCacheDurationSecs=5\niWarmCacheRateMBps=" << (mode == L"pacing" || mode == L"threads" || mode == L"inflight" ? 2 : 64) <<
		"\niWarmCacheMaxFiles=32\niWarmCacheBytesPerFileMB=" << (mode == L"strided" ? 8 : 2) <<
		"\niWarmCacheBudgetMB=" << (mode == L"strided" ? 16 : 8) <<
		"\niWarmCacheThreads=" << (mode == L"threads" ? 2 : 1) << "\nbWarmCacheMappedPrefetch=" << (mode == L"mapped") <<
		"\nbWarmCacheStridedPrefetch=" << (mode == L"strided") <<
		"\niWarmCacheStrideMB=32\nbStopWarmCacheOnMemoryPressure=1\n"
		"[HotSet]\nbHotSetRecord=0\nbHotSetPrefetch=0\n"
		"[Log]\nbLogToFile=" << !baseline << "\nbLogToPluginDirectory=1\nbLogStatsAfterWarm=1\n";
	ini.close();
	const HMODULE dll = LoadLibraryW((plugins / "NextGenDiskCache.dll").c_str());
	require(dll != nullptr, "actual DLL load failed");
	require(IatHookInstall(dll, "ReadFile", ReadSpy, nullptr, 0, reinterpret_cast<void**>(&originalRead)) != nullptr, "ReadFile spy not installed");
	require(IatHookInstall(dll, "GlobalMemoryStatusEx", MemorySpy, nullptr, 0, reinterpret_cast<void**>(&originalMemory)) != nullptr, "memory spy not installed");
	const auto load = reinterpret_cast<bool(*)(const SKSEInterface*)>(GetProcAddress(dll, "SKSEPlugin_Load"));
	SKSEInterface skse{}; skse.skseVersion = 0x02020800; skse.runtimeVersion = 0x01064920;
	skse.QueryInterface = Query; skse.GetPluginHandle = Handle;
	std::atomic<bool> stopBusy{false};
	std::thread busy;
	if (mode == L"wide") busy = std::thread([&] {
		while (!stopBusy.load()) { OpenW(first); Sleep(1); }
	});
	const bool loaded = load && load(&skse);
	stopBusy.store(true);
	if (busy.joinable()) busy.join();
	require(loaded, "SKSE load failed");
	OpenW(first); OpenW(first); OpenW(second); // duplicate must not consume another reservation
	const auto directOpen = reinterpret_cast<decltype(&CreateFileW)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateFileW"));
	SetLastError(777);
	HANDLE control = directOpen(first.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	const DWORD controlError = GetLastError(); CloseHandle(control);
	SetLastError(777);
	HANDLE hooked = CreateFileW(first.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	const DWORD hookedError = GetLastError(); CloseHandle(hooked);
	require(hookedError == controlError, "successful-open error changed");
	const std::string ansi = first.string();
	HANDLE a = CreateFileA(ansi.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	require(a != INVALID_HANDLE_VALUE, "ANSI open failed"); CloseHandle(a);
	OpenW(outside); OpenW(data / "save.ess");
	OpenW(data / "write-only.bsa", GENERIC_READ | GENERIC_WRITE);
	OpenW(first, GENERIC_READ, FILE_FLAG_OVERLAPPED);
	const HMODULE otherDll = LoadLibraryW(sourceOther.c_str());
	const auto otherOpen = reinterpret_cast<bool(*)(const wchar_t*)>(GetProcAddress(otherDll, "OpenArchive"));
	require(otherOpen && otherOpen(other.c_str()), "other-module open failed");
	SetLastError(777);
	const HANDLE missing = CreateFileW((data / "missing.bsa").c_str(), GENERIC_READ,
		FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
	const DWORD missingError = GetLastError();
	require(missing == INVALID_HANDLE_VALUE && missingError == ERROR_FILE_NOT_FOUND, "failed-open error changed");
	Sleep(300);
	require(bytesRead.load() == 0, "warming began before DataLoaded");
	if (baseline || minimal) {
		Sleep(300);
		require(bytesRead.load() == 0, "control profile performed warm reads");
		std::ofstream(root / "result.txt") << "Control DLL completed zero background archive reads.\n";
		return 0;
	}
	if (mode == L"memfail") failMemory.store(true);
	if (mode == L"pressure") triggerPressure.store(true);
	Message(SKSEMessagingInterface::kMessage_PreLoadGame);
	Message(SKSEMessagingInterface::kMessage_DataLoaded);
	Sleep(300);
	require(bytesRead.load() == 0, "warming continued during save loading");
	const ULONGLONG start = GetTickCount64();
	Message(SKSEMessagingInterface::kMessage_PostLoadGame);
	if (mode == L"inflight") {
		while (!bytesRead.load() && GetTickCount64() - start < 2000) Sleep(10);
		require(bytesRead.load() != 0, "first paced read did not complete");
		Message(SKSEMessagingInterface::kMessage_PreLoadGame);
		const uint64_t paused = bytesRead.load(); Sleep(700);
		require(bytesRead.load() == paused, "paced request ignored mid-file save load");
		Message(SKSEMessagingInterface::kMessage_PostLoadGame);
	}
	if (mode == L"pressure") {
		while (!pressure.load() && GetTickCount64() - start < 2000) Sleep(10);
		require(pressure.load(), "pressure injection did not fire");
		const uint64_t paused = bytesRead.load(); Sleep(300);
		require(bytesRead.load() == paused, "read continued across the RAM reserve");
		pressure.store(false);
	}
	if (mode == L"late") { Sleep(700); OpenW(late); }
	const fs::path logPath = plugins / "NextGenDiskCache.log";
	std::string log;
	while (GetTickCount64() - start < 10000) {
		// Match the real session: RAM can change after the archive batch is done.
		// An empty replanned batch must not rewrite the session's exit reason.
		if (mode == L"observed" && bytesRead.load() >= (4ull << 20)) pressure.store(true);
		log = Text(logPath);
		if (log.find("WarmCache: finished;") != std::string::npos) break;
		Sleep(50);
	}
	require(log.find("WarmCache: finished;") != std::string::npos, "warmer missed deadline/completion");
	if (mode == L"observed") require(log.find("reason=session deadline") != std::string::npos,
		"idle RAM changes replaced the real completion reason");
	require(log.find("patched=0 no_buffering_stripped=0") != std::string::npos,
		"failed archive opens were counted as successful caching changes");
	const uint64_t expected = mode == L"strided" ? 16ull << 20 : mode == L"late" || mode == L"wide" ? 6ull << 20 : 4ull << 20;
	if (mode == L"memfail") require(bytesRead.load() == 0, "failed memory query permitted reads");
	else if (mode == L"mapped") {
		require(log.find("prefetch_requested_bytes=4194304") != std::string::npos && bytesRead.load() == 0,
			"mapped requests were reported as completed reads");
	} else if (mode == L"directory") require(bytesRead.load() == (8ull << 20), "directory mode did not obey session budget");
	else require(bytesRead.load() == expected, "wrong completed byte count");
	if (mode == L"strided") require(stridedReads.load() > 0, "ReadFile fallback did not sample later windows");
	if (mode == L"pacing" || mode == L"threads") require(GetTickCount64() - start >= 1900, "aggregate rate limit was bypassed");
	if (mode != L"directory") {
		require(log.find(mode == L"late" || mode == L"wide" ? "observed_archives=3" : "observed_archives=2") != std::string::npos,
			"archive observation admitted unrelated/duplicate/failed opens");
		for (const auto& path : readPaths)
			require(path.find(L"active") != std::wstring::npos || (mode == L"late" && path.find(L"late.bsa") != std::wstring::npos) ||
				(mode == L"wide" && path.find(L"other-plugin.bsa") != std::wstring::npos),
				"warm read escaped the eligible archive set");
	}
	require(fs::last_write_time(first) == firstWrite && fs::last_write_time(second) == secondWrite,
		"warming modified archive write timestamps");
	std::ofstream(root / "result.txt") << "Actual DLL: " << bytesRead.load() << " completed buffered bytes.\n" << log;
	// SKSE never hot-unloads plugins; the process owns worker/hook lifetime.
	return 0;
} catch (const std::exception& error) {
	std::ofstream("result.txt") << error.what() << '\n';
	std::cerr << error.what() << '\n';
	return 1;
}
