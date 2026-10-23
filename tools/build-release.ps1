# SPDX-License-Identifier: GPL-3.0-or-later
# MacWin - Copyright (C) 2026 KosaWLeb. Created by KosaWLeb: https://github.com/KosaWLeb/MacWin

# Builds everything the way CI does (x64 + x86, tests, installer) and writes
# SHA256SUMS.txt next to the installer:
#     powershell -ExecutionPolicy Bypass -File tools\build-release.ps1
# Needs: llvm-mingw (scoop: mingw-mstorsjo-llvm-ucrt), CMake, Ninja, .NET 10 SDK, Inno Setup 6.
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

function Step([string]$name, [scriptblock]$run) {
    Write-Host "==> $name" -ForegroundColor Cyan
    & $run
    if ($LASTEXITCODE -ne 0) { throw "$name failed (exit code $LASTEXITCODE)" }
}

Step 'Configure (x64)' { cmake --preset x64 }
Step 'Build (x64)' { cmake --build --preset x64 }
Step 'Tests' { ctest --test-dir build/x64 --output-on-failure }
Step 'Installer (x86 + Inno Setup)' { cmake --build --preset x64 --target MacWinSetup }

$version = (Get-Content VERSION -Raw).Trim()
$setup = "build\installer\MacWinSetup-$version.exe"
if (-not (Test-Path $setup)) { throw "$setup was not built" }
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  MacWinSetup-$version.exe" | Set-Content -Encoding ascii build\installer\SHA256SUMS.txt
Write-Host "Done: $setup" -ForegroundColor Green
Write-Host "SHA-256: $hash"
