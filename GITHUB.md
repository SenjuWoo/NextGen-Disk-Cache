# GitHub

| | |
|--|--|
| **Repo** | https://github.com/ShugokiFable/NextGen-Disk-Cache |
| **This folder** | git root (``origin`` should already point here) |
| **Parent package** | ``..\`` version folders for ship zips |

Current release snapshot: 2.2.1, linked worktree on `agent/release-cache-2.2.1`.
Canonical object store: sibling `source/`; do not create another repository.
The existing origin URL redirects to `SenjuWoo/NextGen-Disk-Cache` (verified
2026-09-29). Keep normal changes on a scoped branch and use a PR with the required
`build` check; main is protected. Stage explicit reviewed source paths, never
`git add -A`. GitHub publication and tagging were explicitly requested after CI passes. Nexus upload is not requested; prepare its verified package and user changelog. Game files remain read-only.

Exclude build artifacts. Keep LICENSE/upstream credit if any.
