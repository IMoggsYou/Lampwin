# Changelog

All notable changes to MacWin are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and MacWin adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Every user-visible change gets a line under **[Unreleased]** in the same pull request;
a release turns that section into the new version (see [AGENTS.md](AGENTS.md#releasing)).

## [Unreleased]

## [2.1.0] - 2026-11-07

### Added

- **YouTube** (Settings → Island): videos on YouTube count as music too, in the island and
  the Now playing widget. Off by default.

### Changed

- The island and the Now playing widget only show music – from Spotify, Apple Music,
  Amazon Music, TIDAL and YouTube Music. Films (Netflix and others), calls and other sounds
  no longer show up there, and the island's buttons play, pause and skip in that music app
  rather than in whatever Windows' media keys would reach.
- The island's level bars move with the music itself: the sound of the music app, split into
  bass to treble, and the microphone's level during a call. The sound is only read while the
  bars show; nothing is recorded or sent anywhere.
- Every pill of the island is as wide as what is on it – music, a call, mirroring, charging,
  low battery, sound on or off, airplane mode, new headphones – with the same margins at both
  ends, instead of fixed widths with empty black between. A long song title glides along
  smoothly instead of being cut, and the island's edges fall on whole pixels.
- The headphones symbol is drawn anew: one smooth band joined to round cups.
- Window corners: the switches for smooth corners and for other programs' windows are gone.
  Every program's windows always get the smooth round corners Windows draws, also the ones
  Windows leaves square. The tray menu's "Window corners on" went with them.
- Privacy: the Copilot app of Windows 11 24H2 and later has its own switch, which uninstalls
  it for your account (turned back on, its Microsoft Store page opens). Recall is now removed
  from Windows too, not only paused. Recall, Click to Do and the lock screen tips show as not
  available where this PC can't have them (no Copilot+ PC, Windows Spotlight on the lock
  screen) instead of a switch that does nothing, and a setting Windows doesn't let Lampwin
  read says so.
- Privacy and Tweaks: after a change that only shows once Explorer restarts (Widgets, the
  search box, Start and taskbar settings) a "Restart Explorer" button appears, and Explorer is
  told about every change right away.

### Fixed

- "Something went wrong in the settings window" could appear while switching pages or
  saving settings: the icon bounce of the sidebar, saving at the same moment as Lampwin,
  adding the bundled app icons at start and a broken output pipe on the Privacy page no
  longer end in that message. Settings that couldn't be saved at once are saved again shortly
  instead of being lost.
- Settings → Privacy: switches that need administrator rights and your own ones applied
  together no longer change your own ones in the administrator's account.
- Opening a page with `--page` while the settings window is open goes to the right page again
  (Island, Desktop and Tweaks were missing).

## [2.0.0] - 2026-11-06

### Added

- **Island** (Settings → Island): a black pill at the top of the screen, like the Dynamic
  Island on the iPhone. It shows the music that plays – with its cover, moving level bars
  and, when you point at it, the song's progress and the playback buttons – a call while a
  program such as Discord, Signal, Teams, Zoom or WhatsApp uses the microphone (with its
  time and a button to mute the microphone), and screen mirroring to a TV or projector
  (with a button to stop). Short notes come and go for charging, low battery, sound muted
  or back, new headphones, airplane mode and mobile data turned off. Options for hardware
  a PC doesn't have – a battery, Wi-Fi, a mobile broadband modem – are left out. It stays
  out of the way of full-screen games and videos, and everything it shows is read on your PC.
- **Smooth corners** (Settings → Window corners): Windows itself draws the corners of
  every window, perfectly smooth, and Lampwin now also rounds the windows Windows leaves
  square – programs with a frame of their own, such as many launchers and apps made with
  WPF, Qt or Java. Full-screen games and videos stay square.
- **Window borders** (Settings → Window corners): turn off the thin light line around
  every window; windows then only have their shadow.
- Motion everywhere: the island's views flow into one another (the old one shrinks away
  as the new one comes in), its symbols come in with their view – the bell shakes when the
  sound is muted, the phone rings in, the battery fills up as charging starts, the plane
  flies in – play and pause trade places with a little bounce, a new song's cover and title
  slide in over the last ones, the level bars rise and settle smoothly, and buttons light
  up under the pointer and give when pressed. Widgets fade in when they appear, their rings,
  bars and icons fill up and pop in, and every change on a widget (a new song, the weather,
  a new reading) fades smoothly instead of switching at once. The page you pick in the
  settings window gets a little bounce of its icon. With Windows' animation effects off,
  everything changes at once.

### Changed

- The settings window's sidebar has Lampwin's own icons – a genie lamp for the animations
  among them – drawn in white on coloured tiles like the settings of iOS, sharp at any
  display scaling and centred on their tiles by their weight.
- The Now playing widget has a new music tile: two beamed notes drawn cleanly, as on a
  Mac. The weather's temperatures are written in bolder digits.
- Widgets are laid out anew on one grid: the same margin on every side, words lined up
  on shared lines and medium widgets split into equal halves. The clock and the battery
  show the day or a label, a big number and a line under it beside the face or ring; the
  calendar's month fills its half; the weather's hours are spread evenly across the card;
  a small System widget shows its gauges as bars, easier to read than four tiny rings.
- Smooth corners are on by default, so other programs' windows get Windows 11's own
  round corners instead of the stepped squircle. Turn Smooth corners off to keep the
  macOS shape or another radius.

### Fixed

- Windows of Chrome, Edge, Discord, VS Code, Claude and other Chromium / Electron apps no
  longer have black edges along their sides while they minimize, restore, open or close,
  and the animation starts and ends exactly where the window is.
- Thin lines in a window (a scrollbar, a border) no longer look dashed while the window
  bends into the taskbar.
- A window that has its own transparency (a colour key or per-pixel alpha) could keep a
  solid background or stop updating after its opening animation; such a window now opens
  without the animation when it can't be hidden safely.
- Riot Vanguard no longer warns that Lampwin interrupts League of Legends or VALORANT:
  Lampwin stays out of Riot's games, the Riot Client and Vanguard wherever they are
  installed, whatever the list of programs to leave alone says.

## [1.6.1] - 2026-10-30

### Changed

- The settings window's sidebar has new icons: Windows 11's own symbols on macOS-style
  tiles, sharp at any display scaling.
- The Network widget counts only the connection your internet goes through (Wi-Fi,
  Ethernet or a VPN), so nothing is counted twice, and shows that connection's speed
  (such as 1 Gb/s) beside its title. The speeds below are what goes through it right now:
  a few KB/s is normal while nothing downloads.

### Fixed

- The window overview closed again at once, so **Show** (Settings → Desktop), the shortcut
  and the hot corner seemed to do nothing.

## [1.6.0] - 2026-10-30

### Added

- **Window overview** (Settings → Desktop): every window on the screen glides into a grid
  over a dimmed, blurred desktop, like Exposé on a Mac. Click one to bring it to the front,
  or use the arrow keys and Enter; Esc goes back. Open it with **Ctrl+Alt+↑** (turn the
  shortcut on first), a hot corner, the tray menu or `MacWin.exe --overview`.
- **Hot corners**: push the pointer into a corner of the screen and Lampwin shows the
  window overview, the desktop, Task view, Start or the notifications, locks the PC, turns
  the display off or starts the screen saver. Corners stay quiet while a game or a video
  runs full screen, and a click in a corner (a maximized window's close button) never
  triggers them.
- **Light and dark by the time of day**: Windows switches to dark mode at sunset and back
  at sunrise, and the desktop can show a day and a night wallpaper. Sunrise and sunset are
  worked out on your PC from Windows' location (or the Weather widget's place); set times
  work too.
- A **Tweaks** page: show file extensions and hidden files, the full path in File
  Explorer, open File Explorer to This PC, a compact view, taskbar icons on the left, no
  search box or Task view button, seconds in the clock, End task on the taskbar and more
  pins in Start. They are the settings Windows itself has, need no administrator rights,
  are backed up first and **Restore** puts them back.
- Two new widgets: **Battery** (its charge, whether it charges and the time left) and
  **Network** (download and upload speed, with the last minute as a chart). Both read
  everything on your PC.
- A **welcome** the first time the settings open after installing: pick a look to start
  with (like a Mac, calm, or the defaults) and, if you like, a clock and a calendar on the
  desktop.
- The tray menu turns the window corners on and off and opens the window overview.

### Changed

- While Windows' energy saver is on, windows open, close and minimize without animations,
  to save battery. A switch on the Animations page turns this off.

## [1.5.1] - 2026-10-27

### Changed

- Widgets move left and right in half steps too, and go all the way to the left edge of
  the screen, as they already go down to the taskbar. Widgets already on the desktop stay
  where they are.

### Fixed

- On a small screen, or with a big widget size and high display scaling, widgets
  disappeared when there was no room for them; they now shrink to fit, on every monitor.

## [1.5.0] - 2026-10-27

MacWin is now called **Lampwin**. Nothing changes for your settings, widgets or icons, and
installed copies keep updating themselves. **If you have MacWin 1.3.1 or older**, install
this version once by hand: those versions can't find updates since the rename.

### Added

- Two new ways to open and close windows (Settings → Animations):
  - **Shatter**: closing breaks the window into shards that fly off to the left, right,
    up and down, spinning as they fade away; opening flies them together into the window.
  - **Fragments**: the same with a few big pieces, each flying its own way.

  The glass breaks where you clicked (the close button), its cracks flash, and the pieces
  tumble in 3D, catch the light and fall in an arc. The live preview shows each effect as
  soon as you pick it.
- **Zoom** for opening and closing: the window grows out of the place you clicked, a touch
  past its size, and settles; closing shrinks it into the pointer (its close button).
- Lampwin follows Windows' **Animation effects** setting (Settings → Accessibility → Visual
  effects): with it off, windows open, close and minimize without animations, the widgets
  and the settings window stay still too. A switch on the Animations page turns this off.
- Two new desktop widgets:
  - **Countdown**: the days to go until a day you choose (or since it), with its title; the
    medium size also shows the day itself.
  - **Now playing**: the song or video playing on your PC, its artist and the app, the same
    as Windows' media controls show. It is read on your PC only, while the widget is shown.
- **Your settings** (Settings → General): export them to a file and import them on another
  PC, or restore the defaults. Widgets, app icons, privacy switches and the language stay
  when you restore; the settings you had are kept in `settings.json.bak`.
- Drop icon files, folders or an icon set (`.macwinicons`) onto the **App icons** page to
  add them.

### Changed

- The program's visible name is **Lampwin**: the window titles, the tray, the installer, the
  Start menu shortcut and every text. Its files, its folder in AppData and its startup task
  keep the old name, so nothing has to move.
- Shatter and Fragments take a little longer than the duration slider says, so the pieces
  have time to fly apart.
- The installer is a third of its size (about 4.5 MB): the included icons come as compact
  pictures. Icons you removed stay removed.
- `--export-icon` writes a PNG when the file name ends in `.png`.
- The **classic genie** is the default animation style for new installs (and after
  Restore defaults). It now pours into the taskbar and comes out of it, instead of
  running on to the bottom edge of the screen, which showed on an empty desktop.
- Widgets can go lower: the grid has half-height rows, and the lowest widgets sit right
  above the taskbar instead of leaving a gap under them. Widgets already on the desktop
  stay where they are.

### Fixed

- **Check now** said "up to date" when the update couldn't be read or installed; it now
  says so, and the settings window says when Lampwin can't be reached. A failed silent
  install leaves a log (`%LOCALAPPDATA%\MacWin\updates\install.log`).

## [1.4.0] - 2026-10-24

### Added

- **App icons** (a new page in the settings): give programs icons of your own, for example
  macOS ones. Add .icns, PNG, JPEG, WebP, HEIC, ICO and other images, or take a program's
  icon from its .exe; whole folders of large files are added in the background without
  slowing the window down. MacWin cuts away the empty margin and shadow so the icons are
  as big as other icons, and gives each one to the program its file name names; an icon
  that fits no program waits until you pick one. The icon shows on the program's windows,
  in Alt+Tab, on the taskbar and on its Start menu, desktop and taskbar shortcuts.
  "No program", removing the icon or turning the option off gives everything its own icon
  back. A set of macOS-style icons comes with MacWin and is given to your programs the
  first time the settings open.

### Changed

- MacWin keeps finding updates when its GitHub repository moves to its new name, Lampwin.

## [1.3.1] - 2026-10-24

### Fixed

- Animations stopped altogether after a maximized window was minimized at an unlucky
  moment (every window then got the plain Windows animation until MacWin restarted).
  MacWin now uses the window's real place, and an animation that can't be drawn no longer
  breaks the next ones.
- **Check now** works while MacWin is stopped: it starts MacWin and checks right away.
- The mouse wheel no longer scrolls the page away from an open pop-up menu: it scrolls the
  menu's own list, and anywhere else it closes the menu.

## [1.3.0] - 2026-10-24

### Added

- The installer has an **Automatic updates** page: leave it on and new versions download
  and install by themselves in the background.
- With automatic installs off, the settings window shows a new version when it opens: what
  changed in a few lines, the full changelog one click away, and **Update now** or
  **Not now**. **Don't show this again** stops it (Settings → General still shows the
  update, and **What's new** opens the same window).
- **Settings → Exclusions** has a **Restore default games** button that puts the games
  with anti-cheat (and the game folders) back on the "never load" list; your own entries
  stay.
- A new **Privacy** page turns off Windows telemetry and similar features: optional
  diagnostic data, the advertising ID, tips and ads in Start, Settings and on the lock
  screen, web results in search, activity history, Copilot, Recall and more. Its switches
  show what your PC does right now; **Apply** asks for administrator rights once, MacWin
  backs up every value first, and **Restore** puts back everything it changed. Windows
  Update, Microsoft Defender, SmartScreen and the firewall are never touched.

### Changed

- **Install updates automatically** is now on by default.
- Pop-up menus in the settings unfold item by item, and stay with their button while the
  page scrolls (or close once it scrolls out of sight) instead of floating where they
  opened.

### Fixed

- Windows keep their own corners while they open, close, minimize or restore: a window with
  Windows 11's corners no longer gets the bigger macOS corners for the length of the
  animation.
- The widget gallery: the "+" over a widget is no longer cut in half, and no tooltip pops
  up over it.
- The classic genie always pours the window away under the taskbar; it no longer sometimes
  ends in the middle of the program's icon instead.

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
  github.com/IMoggsYou/Lampwin.

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

[Unreleased]: https://github.com/IMoggsYou/Lampwin/compare/v2.1.0...HEAD
[2.1.0]: https://github.com/IMoggsYou/Lampwin/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/IMoggsYou/Lampwin/compare/v1.6.1...v2.0.0
[1.6.1]: https://github.com/IMoggsYou/Lampwin/compare/v1.6.0...v1.6.1
[1.6.0]: https://github.com/IMoggsYou/Lampwin/compare/v1.5.1...v1.6.0
[1.5.1]: https://github.com/IMoggsYou/Lampwin/compare/v1.5.0...v1.5.1
[1.5.0]: https://github.com/IMoggsYou/Lampwin/compare/v1.4.0...v1.5.0
[1.4.0]: https://github.com/IMoggsYou/Lampwin/compare/v1.3.1...v1.4.0
[1.3.1]: https://github.com/IMoggsYou/Lampwin/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/IMoggsYou/Lampwin/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/IMoggsYou/Lampwin/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/IMoggsYou/Lampwin/compare/v1.0.2...v1.1.0
[1.0.2]: https://github.com/IMoggsYou/Lampwin/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/IMoggsYou/Lampwin/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/IMoggsYou/Lampwin/releases/tag/v1.0.0
