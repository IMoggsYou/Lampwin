# AGENTS.md — how to work on MacWin

Instructions for AI coding agents (Claude Code, Codex, Cursor, Copilot, …) and for
humans who want the short version. `CLAUDE.md` and `.github/copilot-instructions.md`
point here; keep this file the single source.

MacWin brings macOS-style window animations (the genie effect) and squircle window
corners to Windows 11. Author: **KosaWLeb**. License: **GPL-3.0-or-later** with the
additional terms in `NOTICE` (keep "Created by KosaWLeb" in file headers and in the About
page; modified versions must be renamed).

## Architecture in one minute

| Piece | Where | What it does |
| --- | --- | --- |
| Host `MacWin.exe` | `src/host/` (C++20, 64-bit) | Tray icon, settings, update checks, installs the global `WH_CBT` hook, renders the genie with Direct3D 11 + DirectComposition, IPC server |
| Hook DLL `MacWinHook64.dll` / `MacWinHook32.dll` | `src/hook/` | Loaded into **other programs**; MinHook detours of `ShowWindow`, `DefWindowProc`, `SetWindowPos`, … capture a snapshot and ask the host to animate; cuts window corners |
| `MacWinHelper32.exe` | `src/helper32/` | Puts the 32-bit DLL into 32-bit programs (thread-specific hooks) |
| Shared contracts | `src/common/` | `SharedSettings` (named section), `AnimRequest` (WM_COPYDATA), object names (`names.h`) |
| Settings app | `settings-app/` (C# / WPF, .NET 10) | The window users see; edits `%APPDATA%\MacWin\settings.json`, tells the host to reload |
| Installer | `installer/` (Inno Setup 6) | Program Files install, scheduled task (elevated autostart), .NET runtime, 11 languages |
| Translations | `i18n/*.json` | Every user-visible string of the settings app and the host |

More detail: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build and test

Windows 11 x64; tools in [docs/BUILDING.md](docs/BUILDING.md).

```powershell
cmake --preset x64; cmake --build --preset x64            # host, DLL, settings app
ctest --test-dir build/x64 --output-on-failure            # unit tests + i18n check
cmake --build --preset x64 --target MacWinSetup           # installer (also builds x86)
powershell -ExecutionPolicy Bypass -File tools\build-release.ps1   # all of it, like CI
```

- **Test build that coexists with an installed MacWin:** `cmake --preset x64-dev` →
  `build/x64-dev/bin` (own object names, `host-dev.log`).
- **Functional checks** (MacWin must be running): `MacWinTestWindow.exe --cycle 3`,
  `--rapid 3`, `--open-close 3`, `--corners --quiet` (exit code 0 = pass);
  `MacWinTestWindow32.exe` for 32-bit. Run them with **one** MacWin: an installed copy
  and a dev build running together both animate the same window and `--corners` fails.
- **Settings window without clicking:** `MacWin.Settings.exe --snapshot <dir> --lang <code>
  [--theme light|dark] [--page general]` renders every page to PNG (and `page<n>-end.png`
  scrolled down) and exits. Use it to check layouts and translations.
- Logs: `%LOCALAPPDATA%\MacWin\host.log` (previous run: `host.old.log`),
  `settings.log`, crash dumps in `crashes\`.

## Hard rules

### The hook DLL runs inside other people's programs

A crash or hang there takes down Explorer, a browser or a game. In `src/hook/`:

- No C++ exceptions (`-fno-exceptions`), no RTTI. Allocate with `new (std::nothrow)` and
  fall back to the default Windows behaviour when something fails.
- `DllMain` / `ShouldLoadIntoThisProcess` run under the loader lock: kernel32 and CRT
  string functions only. No user32, no threads, no waiting.
- Every detour must have a cheap fast path and must call the original API in the end.
- Never add an import to the DLL (each one is loaded into every process). Load rarely
  used APIs on first use (see `TaskbarState` in `genie_hooks.cpp`).
- The DLL must never load into the processes in `kNeverLoad` (`dllmain.cpp`) or next to
  anti-cheat. Keep the default `noInject` list in `src/host/settings_store.cpp` and
  `settings-app/Services/SettingsStore.cs` identical.
- Changing `SharedSettings` or `AnimRequest`: update the `static_assert`s **and** bump the
  `_vN` suffixes in `src/common/names.h` (and `MW_IPC_VERSION`).

### Every user-visible string is translated

- Add the key to **all 11** files in `i18n/` (en, pl, zh-Hans, hi, es, fr, ar, bn, ru,
  pt-BR, ja). English (`en.json`) is the source. Keys: `settings.*` (settings app),
  `host.*` (tray, notifications, message boxes). Placeholders are `{0}`, the same in every
  language.
- XAML: `Text="{l:Tr settings.some.key}"`; C#: `Loc.T("…")` / `Loc.F("…", arg)`; C++:
  `i18n::T(L"host.…")` / `i18n::F(...)`, message boxes through `i18n::Message`.
- Installer texts live in `installer/languages/<Language>.isl` (`[CustomMessages]`, same
  keys in every file, UTF-8 with BOM).
- Use the terms in [i18n/GLOSSARY.md](i18n/GLOSSARY.md). Arabic is right-to-left: a Latin
  name followed by a number needs `‎` before the number (`Windows ‎11`).
- `ctest` runs `tools/check_i18n.cmake` (same keys, no empty text, same placeholders).
  Check the result visually with `--snapshot` for at least one CJK language, Hindi or
  Bengali, and Arabic. Details: [docs/TRANSLATING.md](docs/TRANSLATING.md).

### Changelog

Every user-visible change adds a line under `## [Unreleased]` in `CHANGELOG.md`
([Keep a Changelog](https://keepachangelog.com/en/1.1.0/) sections: Added, Changed, Fixed,
Removed, Security). Write it for users, not for developers.

### Style

- Follow the code around you: comments explain *why*, in full sentences; names over
  comments; no dead code. C++ formatting matches the existing files (4 spaces, 120 columns).
- New source files start with the license header (`python tools/add_license_headers.py`
  adds it).
- Commit messages: short summary with the area first (`Corners: …`, `Updater: …`),
  blank line, then why. Sign off (`git commit -s`, see CONTRIBUTING.md).

## Releasing

1. Make sure `main` is green in CI.
2. Pick the version ([SemVer](https://semver.org): fix = patch, feature = minor).
3. Put it in `VERSION` (the only place; CMake, the csproj and the installer read it).
4. In `CHANGELOG.md` rename `## [Unreleased]` to `## [X.Y.Z] - YYYY-MM-DD`, add a new empty
   `## [Unreleased]` above it, update the compare links at the bottom.
5. Commit (`Release X.Y.Z`), then tag and push:
   `git tag vX.Y.Z && git push origin main vX.Y.Z`.
6. `.github/workflows/release.yml` builds, tests, attaches `MacWinSetup-X.Y.Z.exe` and
   `SHA256SUMS.txt` and publishes the release with that changelog section. Installed copies
   find it within a day (the updater needs exactly those two file names).

## Things not to do

- Don't change the scheduled task name, the registered message names or the window class
  without handling older installs.
- Don't put user-visible text in code, logs excepted (logs are English).
- Don't run MacWin elevated from outside Program Files (the host refuses, on purpose).
- Don't add network access other than the updater (privacy promise in the README).
- Don't commit build output (`build/`) or personal settings.
