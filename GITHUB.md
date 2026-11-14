# GitHub

| | |
|--|--|
| **Repo** | https://github.com/SenjuWoo/NextGen-Disk-Cache |
| **Default branch** | `main`, protected: pull request, required `build` check, conversations resolved |
| **Version** | `VERSION.txt` (the only place it is written) |

## Change flow

1. Branch `agent/<topic>` or `release/<version>` from `main`; CI runs on both.
2. `.\build.ps1`, `ctest --test-dir build -C Release --output-on-failure`, `.\package-release.ps1 -SkipBuild`.
3. Open a pull request; the `build` job must pass on its head commit.
4. Merge, then tag the merge commit `vX.Y.Z`.

## Release

- Assets come from the CI run on the tagged commit, unmodified: `NextGenDiskCache-X.Y.Z-FOMOD.zip`,
  `NextGenDiskCache-X.Y.Z-SHA256.txt` and `NextGenDiskCache.pdb` (symbols, not needed to play).
- The release notes state the exact commit, the test count and the ZIP and DLL SHA-256.
- Nexus (mod 185563) is uploaded by hand; `docs/NEXUS-CHANGELOG-X.Y.Z.txt` holds its changelog text.

Stage explicit, reviewed source paths (never `git add -A`). Build output (`build/`, `dist/`,
`package/SKSE/Plugins/*.dll`) is ignored. Keep the Archost ISC licence and credits.
