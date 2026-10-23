# Building MacWin

## Tools (Windows 11 x64)

| Tool | Version | Install |
| --- | --- | --- |
| llvm-mingw (UCRT) | 20261006 or newer | `scoop install mingw-mstorsjo-llvm-ucrt` |
| CMake | 3.25+ | `scoop install cmake` or cmake.org |
| Ninja | any | `scoop install ninja` |
| .NET SDK | 10.0 | `winget install Microsoft.DotNet.SDK.10` |
| Inno Setup | 6.5+ (installer only) | `scoop install inno-setup` |
| Python | 3.x (optional, for `tools/*.py`) | `winget install Python.Python.3.12` |

The presets add `%USERPROFILE%\scoop\apps\mingw-mstorsjo-llvm-ucrt\current\bin` to `PATH`;
with llvm-mingw elsewhere, put its `bin` on `PATH` yourself.

## Commands

```powershell
cmake --preset x64                  # configure (build\x64)
cmake --build --preset x64          # MacWin.exe, MacWinHook64.dll, settings app, test window
ctest --test-dir build/x64          # unit tests and the translation check
cmake --build --preset x64 --target MacWinSetup   # x86 parts + installer
```

Everything lands in `build\x64\bin` (the x86 preset builds `MacWinHook32.dll`,
`MacWinHelper32.exe` and `MacWinTestWindow32.exe` into the same folder). The installer is
`build\installer\MacWinSetup-<version>.exe`. `tools\build-release.ps1` runs all of it and
writes `SHA256SUMS.txt`, exactly like CI.

## Presets

| Preset | Output | Use |
| --- | --- | --- |
| `x64` | `build\x64` | The real build |
| `x86` | `build\x86` → `build\x64\bin` | 32-bit DLL and helper (the installer target runs it) |
| `x64-dev` | `build\x64-dev` | Test build with its own object names and `host-dev.log`, can run next to an installed MacWin |

## The version

`VERSION` at the repository root is the only place it is written. CMake (`project()`,
`gen/version.h`, the version resources), the settings app's csproj and the installer all
read it.

## Debugging tips

- `host.log` shows every animation with its frame count and CPU time per frame.
- Build with `-DCMAKE_CXX_FLAGS=-DMACWIN_DEBUG_LOG` to get the DLL's `OutputDebugString`
  messages (DebugView).
- Ctrl+Alt+Shift+F12 quits MacWin at once if something goes wrong while testing.
