// One check against the production implementation, not a duplicate policy.
#include "../src/main.cpp"
#include <stdexcept>

static void require(bool value) { if (!value) throw std::runtime_error("cache policy regression"); }

int main()
{
	const DWORD base = FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL;
	const DWORD accesses[] = {GENERIC_WRITE, GENERIC_ALL, MAXIMUM_ALLOWED, FILE_WRITE_DATA,
		FILE_APPEND_DATA, FILE_WRITE_EA, FILE_WRITE_ATTRIBUTES, WRITE_DAC, WRITE_OWNER, DELETE, 0};
	const DWORD flags[] = {FILE_FLAG_OVERLAPPED, FILE_FLAG_WRITE_THROUGH, FILE_FLAG_DELETE_ON_CLOSE,
		FILE_FLAG_OPEN_REPARSE_POINT, FILE_FLAG_BACKUP_SEMANTICS};
	for (bool broad : {false, true}) {
		for (bool random : {false, true}) {
			g_settings.conservativeHookScope = !broad;
			g_settings.preferRandomAccessOnArchives = random;
			require(ValidateFilePolicy());
			for (DWORD access : accesses)
				require(ComputePatchedFlags(base, access | (access ? GENERIC_READ : 0), OPEN_EXISTING, PathKind::Archive) == base);
			for (DWORD flag : flags)
				require(ComputePatchedFlags(base | flag, GENERIC_READ, OPEN_EXISTING, PathKind::Archive) == (base | flag));
			for (DWORD disposition : {CREATE_NEW, CREATE_ALWAYS, OPEN_ALWAYS, TRUNCATE_EXISTING})
				require(ComputePatchedFlags(base, GENERIC_READ, disposition, PathKind::Archive) == base);
		}
	}
	require(ClassifyPathW(L"Data\\folder.bsa\\save.ess") == PathKind::Save);
	require(ClassifyPathW(L"Data\\archive.BSA") == PathKind::Archive);
	require(ParseUnsigned("-1", 17) == 17 && ParseUnsigned("99999999999999999999", 17) == 17);
	require(ParseDouble("NaN", 0.5) == 0.5);
	g_settings.warmCacheRateMBps = 0;
	g_settings.warmCacheDurationSecs = UINT_MAX;
	g_settings.warmCacheBudgetMB = UINT_MAX;
	SanitizeSettings();
	require(g_settings.warmCacheRateMBps == 1 && g_settings.warmCacheDurationSecs == 3600 && g_settings.warmCacheBudgetMB == 16384);
	g_warmBudgetLeft.store(7);
	require(ReserveWarmBudget(5) == 5 && ReserveWarmBudget(5) == 2 && ReserveWarmBudget(1) == 0);

	// Learned load cache: a profile path can never leave Data.
	require(HotSetSafeRelativePath(L"meshes\\a.nif") && HotSetSafeRelativePath(L"Skyrim - Textures0.bsa") &&
		HotSetSafeRelativePath(L"active-\u65E5\u672C.ba2"));
	for (const wchar_t* bad : {L"", L"..\\x.bsa", L"a\\..\\b.bsa", L"C:\\x.bsa", L"\\x.bsa", L"\\\\srv\\x.bsa",
			L"a.bsa:stream", L"a/b.bsa", L"a\\\\b.bsa", L"a\\", L".\\a.bsa"})
		require(!HotSetSafeRelativePath(bad));
	std::vector<std::pair<uint32_t, uint32_t>> chunks = {{5, 6}, {0, 2}, {1, 3}, {8, 9}, {4, 4}};
	HotSetCoalesce(chunks, 1);
	require(chunks == decltype(chunks){{0, 3}, {5, 6}, {8, 9}});
	HotSetCoalesce(chunks, 2);
	require(chunks == decltype(chunks){{0, 9}});

	// Round trip, another install's profile, and hostile lines skipped one by one.
	wchar_t tempDir[MAX_PATH] = {};
	require(GetTempPathW(MAX_PATH, tempDir) != 0);
	const std::wstring profile = std::wstring(tempDir) + L"ngdc-hotset-check-" + std::to_wstring(GetCurrentProcessId()) + L".hotset";
	const std::wstring dataPath = L"C:\\Games\\Skyrim\\Data";
	HotSetEntry a; a.relPath = L"Skyrim - Meshes0.bsa"; a.size = 10ull << 20; a.mtime = 123; a.firstUseMs = 50;
	a.archive = true; a.chunks = {{0, 2}, {10, 12}};
	HotSetEntry b; b.relPath = L"meshes\\\u65E5.nif"; b.size = 4096; b.mtime = 7; b.firstUseMs = 10;
	require(HotSetSave(profile, dataPath, {a, b}));
	std::vector<HotSetEntry> loaded;
	size_t rejected = 1;
	require(HotSetLoad(profile, dataPath, loaded, rejected) && rejected == 0 && loaded.size() == 2);
	require(loaded[0].relPath == a.relPath && loaded[0].chunks == a.chunks && loaded[0].archive &&
		loaded[0].size == a.size && loaded[0].mtime == 123 && loaded[0].firstUseMs == 50);
	require(loaded[1].relPath == b.relPath && !loaded[1].archive && loaded[1].size == 4096);
	require(HotSetEntryBytes(loaded[0]) == 4ull * 65536 && HotSetEntryBytes(loaded[1]) == 4096);
	require(!HotSetLoad(profile, L"D:\\Other\\Data", loaded, rejected));
	{
		std::ofstream out(profile, std::ios::binary | std::ios::trunc);
		out << "NGDC-HOTSET 1\ndata C:\\Games\\Skyrim\\Data\n"
			"F 0 1 100 1 ..\\..\\Windows\\win.ini\n"
			"F 0 1 100 1 C:\\x.esp\n"
			"A 0 1 65536 1 0-2 x.bsa\n"
			"A 0 1 655360 1 3-1 y.bsa\n"
			"F -1 1 100 1 neg.esp\n"
			"F 0 1 0 1 empty.esp\n"
			"F 0 1 999999999999 1 huge.esp\n"
			"Q 0 1 100 1 what.esp\n"
			"A 0 1 655360 1 1-2 z.bsa\n"
			"A 0 1 655360 1 1-2 Z.BSA\n";
	}
	require(HotSetLoad(profile, dataPath, loaded, rejected) && loaded.size() == 1 && rejected == 9 &&
		loaded[0].relPath == L"z.bsa");
	{
		std::ofstream out(profile, std::ios::binary | std::ios::trunc);
		out << "NGDC-HOTSET 9\n";
	}
	require(!HotSetLoad(profile, dataPath, loaded, rejected));
	DeleteFileW(profile.c_str());

	// Merge: this session in first-use order, then still-young older entries, within the byte cap.
	HotSetEntry c = b; c.relPath = L"c.esp"; c.firstUseMs = 1; c.age = 3;
	HotSetEntry d = b; d.relPath = L"d.esp"; d.age = 4;
	HotSetEntry oldA = a; oldA.age = 2;
	auto merged = HotSetMerge({a, b}, {oldA, c, d}, 4, UINT64_MAX);
	require(merged.size() == 3 && merged[0].relPath == b.relPath && merged[1].relPath == a.relPath &&
		merged[2].relPath == L"c.esp" && merged[2].age == 4 && merged[0].age == 0);
	merged = HotSetMerge({a, b}, {}, 4, 4096 + 1);
	require(merged.size() == 1 && merged[0].relPath == b.relPath);
	printf("CACHE POLICY PASS\n");
}
