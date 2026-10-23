<div align="center">

<img src="docs/media/icon.png" width="128" height="128" alt="MacWin icon">

# MacWin

**The macOS genie effect and squircle window corners — for Windows 11.**

Windows fall into the taskbar like a genie and fly back out, open and close the same way,
and get soft macOS-style corners. Light, free and open source.

[![Latest release](https://img.shields.io/github/v/release/KosaWLeb/MacWin?style=for-the-badge&logo=github&label=release&color=0A84FF)](https://github.com/KosaWLeb/MacWin/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/KosaWLeb/MacWin/total?style=for-the-badge&color=34C759)](https://github.com/KosaWLeb/MacWin/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/KosaWLeb/MacWin/build.yml?branch=main&style=for-the-badge&label=build)](https://github.com/KosaWLeb/MacWin/actions/workflows/build.yml)
[![Windows 11](https://img.shields.io/badge/Windows-11-0078D4?style=for-the-badge&logo=windows11&logoColor=white)](#requirements)
[![Languages](https://img.shields.io/badge/languages-11-AF52DE?style=for-the-badge)](#languages)
[![License](https://img.shields.io/badge/license-GPL--3.0-orange?style=for-the-badge)](LICENSE)

### [⬇&nbsp; Download MacWin for Windows 11](https://github.com/KosaWLeb/MacWin/releases/latest)

<img src="docs/media/demo.gif" width="860" alt="A window falls into its taskbar icon with the genie effect and flies back out">

**English** · [Polski](README.pl.md)

</div>

---

## Features

- 🧞 **Genie effect** — minimized windows pour into their own taskbar icon and flow back out
  on restore, drawn on the GPU with a smooth "lamp" curve (or the classic funnel).
- 🪟 **Opening and closing** with the genie, from and into the program's taskbar icon.
- ↩️ **Reversible mid-flight** — click again while it is still moving and the genie turns
  around from exactly where it is. Win+D animates every window at once.
- ⬛ **Squircle corners** — the macOS shape (100% smoothing) or the Windows 11 rounding,
  for the animation, the settings window and apps that draw their own title bar
  (Chrome, Edge, Brave, Discord, VS Code, Spotify, …), with a matching shadow.
- ⚙️ **A settings window like macOS System Settings** — live preview, search, light and
  dark mode, per-app exclusions.
- 🌍 **11 languages**, picked automatically from Windows (see [Languages](#languages)).
- 🔄 **Automatic updates** from GitHub, verified with SHA-256 — one click, or fully automatic.
- 🚀 **Starts with Windows**, sits in the notification area, uses no CPU while idle.
- 🛡️ **Safe by design** — stays out of system processes and anti-cheat games, falls back
  to the normal Windows animation whenever anything goes wrong, and
  **Ctrl+Alt+Shift+F12** turns it off instantly.
- 🖥️ **Any PC with Windows 11** — AMD, Intel and NVIDIA graphics, or none at all (it then
  draws on the CPU). 32-bit apps and apps running as administrator are animated too.

## Screenshots

<p align="center">
  <img src="docs/media/settings-light.png" width="49%" alt="MacWin settings, light mode">
  <img src="docs/media/settings-dark.png" width="49%" alt="MacWin settings, dark mode">
</p>

## Install

1. Download **`MacWinSetup-x.y.z.exe`** from the
   [latest release](https://github.com/KosaWLeb/MacWin/releases/latest).
2. Run it. If the .NET 10 Desktop Runtime (needed by the settings window) is missing, the
   installer downloads it from Microsoft for you.
3. Done — minimize any window. MacWin starts with Windows; its icon is in the
   notification area (click it for the settings).

> [!NOTE]
> MacWin is not code-signed yet, so Windows SmartScreen may say *"Windows protected your
> PC"*. Click **More info → Run anyway**. Every release is built by GitHub Actions from
> this repository, with a published SHA-256 (`SHA256SUMS.txt`) and a
> [build provenance attestation](https://github.com/KosaWLeb/MacWin/attestations).

### Requirements

| | |
| --- | --- |
| Windows | Windows 11 (22H2 or newer recommended), 64-bit **x64** |
| Graphics | Any: AMD, Intel, NVIDIA, or none (Direct3D 11 / WARP) |
| Disk | ~5 MB (+ the .NET 10 Desktop Runtime for the settings window) |
| Not supported yet | Windows on ARM (Snapdragon PCs) |

### Uninstall

**Settings → Apps → Installed apps → MacWin → Uninstall.** It asks whether to remove your
settings too.

## Languages

MacWin speaks your language: it follows the Windows display language and falls back to
English. You can also pick one in **Settings → General → Language**.

| | | | |
| --- | --- | --- | --- |
| English | Polski | 简体中文 (Chinese) | हिन्दी (Hindi) |
| Español | Français | العربية (Arabic) | বাংলা (Bengali) |
| Русский (Russian) | Português (Brasil) | 日本語 (Japanese) | |

<p align="center">
  <img src="docs/media/languages.png" width="760" alt="The settings window in Japanese, Arabic, Hindi and Polish">
</p>

Spotted a clumsy translation? Native speakers are very welcome to fix it — see
[docs/TRANSLATING.md](docs/TRANSLATING.md); no build needed.

## Light on your PC

Measured on Windows 11 with an NVIDIA GeForce RTX 2070, MacWin 1.0.0, idle for 30 seconds:

| Process | CPU while idle | Memory |
| --- | --- | --- |
| `MacWin.exe` (tray, renderer) | 0 ms | ~52 MB (mostly the GPU driver) |
| `MacWinHelper32.exe` (32-bit apps) | 0 ms | ~9 MB |
| `MacWinHook64.dll` in each app | — | ~340 KB image |

Nothing runs between animations: no polling, no timers. A genie frame takes about
0.3 ms of CPU.

## Privacy

MacWin collects nothing. The only network request is the update check: once a day it
asks the GitHub API whether a new release exists, and downloads it from GitHub if you
choose to update. You can turn it off in **Settings → General → Updates**.

## FAQ

<details>
<summary><b>Is it safe for games with anti-cheat?</b></summary>

MacWin never loads into games installed in the Steam, Epic, Riot, Xbox, EA, Ubisoft or
GOG game folders, into games shipping EasyAntiCheat or BattlEye, or into a list of known
anti-cheat games. You can add any program or folder under **Settings → Exclusions →
Never load MacWin**.
</details>

<details>
<summary><b>Something looks wrong. How do I turn it off right now?</b></summary>

Press **Ctrl+Alt+Shift+F12**. MacWin quits and every window behaves normally again. Then
please [report the problem](https://github.com/KosaWLeb/MacWin/issues/new/choose) with the
log (`%LOCALAPPDATA%\MacWin\host.log`).
</details>

<details>
<summary><b>Why does MacWin ask for administrator rights during installation?</b></summary>

It installs into Program Files and registers a scheduled task, so it can start with
Windows and animate programs that run as administrator — without a UAC prompt at every
sign-in.
</details>

<details>
<summary><b>Why do File Explorer and Notepad keep the Windows 11 corners?</b></summary>

Windows with the system title bar break when their shape is changed. MacWin gives the
squircle to the genie, its own window and apps that draw their own title bar.
</details>

<details>
<summary><b>Does it work with multiple monitors and an auto-hidden taskbar?</b></summary>

Yes. The auto-hidden taskbar slides out while a window minimizes; on multiple monitors
the genie can target the taskbar on the window's monitor (Settings → Window Animations).
</details>

## Build from source

```powershell
cmake --preset x64
cmake --build --preset x64
ctest --test-dir build/x64
cmake --build --preset x64 --target MacWinSetup   # installer
```

You need llvm-mingw, CMake, Ninja, the .NET 10 SDK and Inno Setup 6 — see
[docs/BUILDING.md](docs/BUILDING.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Contributing

Bug reports, ideas, translations and code are welcome. Please read
[CONTRIBUTING.md](CONTRIBUTING.md) (and [AGENTS.md](AGENTS.md) if you work with an AI
coding assistant). Security issues: [SECURITY.md](SECURITY.md).

If you like MacWin, a ⭐ helps others find it.

## Credits

- The genie animation logic is ported from the Windhawk mod
  [MacOS Minimize Animation](https://github.com/Abdullah-Masood-05) by Abdullah Masood,
  with the genie engine by [Potassiumuncher](https://github.com/Potassiumuncher) (MIT).
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu (BSD 2-Clause).

Full notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

Created by **[KosaWLeb](https://github.com/KosaWLeb)**. Copyright © 2026 KosaWLeb.

MacWin is free software under the [GNU General Public License v3.0 or later](LICENSE),
with the additional terms in [NOTICE](NOTICE): keep the author attribution, mark modified
versions and give them another name and icon ([TRADEMARKS.md](TRADEMARKS.md)).

macOS is a trademark of Apple Inc.; Windows is a trademark of Microsoft Corporation.
MacWin is not affiliated with either.
