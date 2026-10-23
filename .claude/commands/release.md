---
description: Prepare a MacWin release (version, changelog, tag) and publish it
argument-hint: "[version, e.g. 1.1.0]"
---

Prepare MacWin release $ARGUMENTS following the "Releasing" section of AGENTS.md:

1. Check the working tree is clean and on `main`, and that CI on `main` is green
   (`gh run list --branch main --limit 3`).
2. If no version was given, propose one from `## [Unreleased]` in CHANGELOG.md (SemVer:
   fixes only = patch, new features = minor) and ask before continuing.
3. Write the version to `VERSION`.
4. In CHANGELOG.md: rename `## [Unreleased]` to `## [X.Y.Z] - <today>`, add a fresh empty
   `## [Unreleased]` above it, and update the compare links at the bottom. If
   `[Unreleased]` was empty, stop and say so.
5. Build and test: `cmake --preset x64`, `cmake --build --preset x64`,
   `ctest --test-dir build/x64 --output-on-failure`.
6. Commit `Release X.Y.Z` (signed off), then show the user the commit and ask for a clear
   yes before `git tag vX.Y.Z` and `git push origin main vX.Y.Z` (pushing publishes).
7. After pushing, watch the Release workflow (`gh run watch`) and check that the release
   has `MacWinSetup-X.Y.Z.exe` and `SHA256SUMS.txt`.
