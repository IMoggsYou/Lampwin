# Changelog

All notable changes to MacWin are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and MacWin adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Every user-visible change gets a line under **[Unreleased]** in the same pull request;
a release turns that section into the new version (see [AGENTS.md](AGENTS.md#releasing)).

## [Unreleased]

## [1.2.0] - 2026-10-24

### Added

- A third animation style, **Scale**: the window shrinks into its taskbar icon in one
  smooth glide and melts away, like the scale effect of macOS (Settings → Animations).

### Changed

- The animations are smoother and more polished: the window starts and stops moving
  more gently, melts into its taskbar icon instead of vanishing at the last moment, and
  keeps its shadow all the way, so the shadow no longer disappears when the animation
  starts or pops in when it ends.
- The settings feel smoother: a click on a slider's track glides the knob there, the knob
  grows evenly with a soft spring under the pointer and while you hold it, and buttons,
  lists and pop-up buttons fade between their states.
- The live preview shows every style the way the desktop does: the window slips under the
  taskbar into its icon (the classic one used to fall off the bottom of the picture).

### Fixed

- The live preview in **Settings → Animations** has rounded corners again, instead of
  square ones sticking out of its card.
- After **Show desktop** (Win+D), a window that opened, closed or minimized was animated
  under the desktop widgets; the animation now covers them from the first frame, like the
  window itself.

## [1.1.0] - 2026-10-24

### Added

- **Desktop widgets** in a new **Settings → Widgets** page: a clock, a calendar, the
  weather, system gauges (processor, memory, disk, battery), a note and a photo, each in
  small, medium or large (the clock and the gauges in small or medium), light, dark or
  following Windows. They sit on the desktop under every window and snap to a grid like on
  macOS: drag one to move it, right-click it to change its size or look, click the clock
  for the Clock app or the gauges for Task Manager.
- **Show widgets** in the notification area menu turns them all off and on.
- The Weather widget follows **your location** from Windows location services (on by
  default) and names the town you are in. When Windows doesn't share the location, or only
  knows it roughly, the place you chose in the settings is used.

### Changed

- Privacy: with a Weather widget on the desktop, MacWin asks Open-Meteo (open-meteo.com) for
  the forecast every 30 minutes, and OpenStreetMap (nominatim.openstreetmap.org) for the
  town's name when the PC has moved, sending only coordinates rounded to about a kilometre;
  searching for a place sends Open-Meteo the name you typed. Without a Weather widget
  nothing changes.

## [1.0.2] - 2026-10-23

### Fixed

- After an update the settings window no longer keeps showing "Installing…" with the
  "Check now" button disabled.

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

[Unreleased]: https://github.com/IMoggsYou/MacWin/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/IMoggsYou/MacWin/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/IMoggsYou/MacWin/compare/v1.0.2...v1.1.0
[1.0.2]: https://github.com/IMoggsYou/MacWin/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/IMoggsYou/MacWin/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/IMoggsYou/MacWin/releases/tag/v1.0.0
