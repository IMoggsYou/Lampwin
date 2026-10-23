# CLAUDE.md

Everything about working in this repository is in AGENTS.md (architecture, build and
test commands, the hook DLL rules, translations in 11 languages, the changelog and the
release process). Read it first:

@AGENTS.md

Claude Code specifics:

- `/release` and `/add-string` (in `.claude/commands/`) walk through the two most common
  multi-file tasks.
- Before you say a change is done: build (`cmake --build --preset x64`), run
  `ctest --test-dir build/x64`, and for UI changes look at
  `MacWin.Settings.exe --snapshot <dir> --lang <code>` output.
- Every user-visible change: a line in `CHANGELOG.md` under `[Unreleased]`, and strings in
  all 11 `i18n/*.json` files.
