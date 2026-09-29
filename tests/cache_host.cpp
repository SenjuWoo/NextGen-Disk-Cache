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

int wmain(int argc, wchar_t** argv) try
{
	require(argc >= 4, "usage: CacheHost dll other-module mode [child]");
	const fs::path sourceDll = fs::absolute(argv[1]), sourceOther = fs::absolute(argv[2]);
	const std::wstring mode = argv[3];
	wchar_t exe[1024] = {};
	require(GetModuleFileNameW(nullptr, exe, 1024) != 0, "host path failed");
	if (argc == 4) {
		const fs::path root = fs::path(exe).parent_path() / "fixtures" / mode;
		fs::create_directories(root);
		const fs::path child = root / "CacheHost.exe";
		fs::copy_file(exe, child, fs::copy_options::overwrite_existing);
		std::wstring command = L"\"" + child.wstring() + L"\" \"" + sourceDll.wstring() +
			L"\" \"" + sourceOther.wstring() + L"\" " + mode + L" child";
		STARTUPINFOW startup{}; startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		require(CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, root.c_str(), &startup, &process) != FALSE, "host launch failed");
		const DWORD wait = WaitForSingleObject(process.hProcess, 20000);
		if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 2);
		DWORD code = 2; GetExitCodeProcess(process.hProcess, &code);
		CloseHandle(process.hThread); CloseHandle(process.hProcess);
		std::wcout << L"HOST " << mode << (code == 0 ? L" PASS\n" : L" FAIL\n");
		if (code) std::cerr << Text(root / "result.txt");
		return static_cast<int>(code);
	}
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
		log = Text(logPath);
		if (log.find("WarmCache: finished;") != std::string::npos) break;
		Sleep(50);
	}
	require(log.find("WarmCache: finished;") != std::string::npos, "warmer missed deadline/completion");
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
