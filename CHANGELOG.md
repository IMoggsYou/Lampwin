# Changelog

All notable changes to MacWin are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and MacWin adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Every user-visible change gets a line under **[Unreleased]** in the same pull request;
a release turns that section into the new version (see [AGENTS.md](AGENTS.md#releasing)).

## [Unreleased]

## [1.0.1] - 2026-10-23

### Changed

- The author is shown as **IMoggsYou**, the project's GitHub account, in the About window,
  the installer, the file properties and the license notices; every link points to
  github.com/IMoggsYou/MacWin.

## [1.0.0] - 2026-10-23

The first public release.

### Added

- **Genie effect** like macOS: windows fall into their taskbar icon when minimized and
  fly back out when restored, drawn on the GPU with a smooth "lamp" curve (modern style,
  Potassiumuncher's engine) or the original funnel (classic style).
- **Opening and closing** windows with the genie effect, from and into the program's
  taskbar icon.
- **Live direction changes**: click a window that is still minimizing and the genie turns
  around from exactly where it is.
- **Window corners**: a macOS squircle (100% smoothing) or the Windows 11 rounding, any
  radius, for the genie, the settings window and programs that draw their own title bar
  (Chrome, Edge, Brave, Discord, VS Code, Spotify, …), with a matching shadow.
- **"Show desktop"** (Win+D) and minimize-all animate every window at once.
- **Automatically hidden taskbar**: it slides out so a minimizing window has somewhere to go.
- **32-bit programs** are animated too (MacWinHelper32 and MacWinHook32.dll).
- **Programs started as administrator** are animated when MacWin is installed (it starts
  with administrator rights through a scheduled task, without a UAC prompt).
- **Settings window** in the style of macOS System Settings: live preview, search,
  light and dark mode that follow Windows, exclusions per program or folder.
- **11 languages**: English, Polish, Simplified Chinese, Hindi, Spanish, French, Arabic
  (right-to-left), Bengali, Russian, Brazilian Portuguese and Japanese, picked from the
  Windows display language (English for any other language), or chosen in the settings.
- **Automatic updates** from GitHub Releases: a daily check, a notification and a one-click
  install, or fully automatic updates; the installer's SHA-256 is verified first.
- **Installer** in the same 11 languages; it installs the .NET 10 Desktop Runtime when it
  is missing and offers to remove settings on uninstall.
- **Start with Windows**, a tray icon with a quick menu, and an emergency switch:
  Ctrl+Alt+Shift+F12 turns MacWin off at once.
- **Crash reports**: if MacWin ever crashes, a minidump is saved in
  `%LOCALAPPDATA%\MacWin\crashes` and MacWin starts again.

### Security

- MacWin never loads into system processes (csrss, winlogon, dwm, the UAC prompt, the lock
  screen, the shell's own UI) or into games protected by anti-cheat (EasyAntiCheat and
  BattlEye folders, the Steam, Epic, Riot, Xbox, EA, Ubisoft and GOG game folders, and a
  list of known anti-cheat games).
- When running with administrator rights, MacWin only runs from Program Files, and it
  loads DLLs only from System32 and its own folder.
- Updates are downloaded only from github.com over HTTPS and run only when their SHA-256
  matches the release's `SHA256SUMS.txt`.

[Unreleased]: https://github.com/IMoggsYou/MacWin/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/IMoggsYou/MacWin/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/IMoggsYou/MacWin/releases/tag/v1.0.0
