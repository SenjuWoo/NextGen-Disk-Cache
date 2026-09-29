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
	printf("CACHE POLICY PASS\n");
}
