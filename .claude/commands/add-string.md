---
description: Add or change a user-visible string in all 11 languages
argument-hint: "<key> <English text>"
---

Add or change the UI string $ARGUMENTS in MacWin:

1. Pick the key: `settings.<page>.<thing>` for the settings app, `host.<area>.<thing>` for
   the tray/notifications/message boxes. Reuse an existing key if the meaning is the same.
2. Write the English text in `i18n/en.json` (placeholders as `{0}`, `{1}`).
3. Translate it into every other file in `i18n/` (pl, zh-Hans, hi, es, fr, ar, bn, ru,
   pt-BR, ja) using the terms in `i18n/GLOSSARY.md` and the tone of the neighbouring
   strings in that file. Arabic: put `‎` before a number that follows a Latin name.
4. Use it: XAML `{l:Tr key}`, C# `Loc.T("key")`/`Loc.F(...)`, C++ `i18n::T(L"key")`.
5. Run `cmake -P tools/check_i18n.cmake`, build, and check the layout once with
   `MacWin.Settings.exe --snapshot <dir>` (English is enough when the text can wrap; see
   AGENTS.md, "Every user-visible string is translated").
6. Add a line to `CHANGELOG.md` under `[Unreleased]` if users will notice the change.
7. Mention in the PR which translations should get a native speaker's review.
