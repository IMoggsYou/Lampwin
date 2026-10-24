# SPDX-License-Identifier: GPL-3.0-or-later
# MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

# docs/media/demo.gif from the settings app's live preview, rendered frame by frame:
#     powershell -ExecutionPolicy Bypass -File tools\media\make-gif.ps1
# Needs a build (build\x64\bin) and ffmpeg on PATH.
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$frames = Join-Path ([IO.Path]::GetTempPath()) "macwin-preview-frames"
if (Test-Path $frames) { Remove-Item -LiteralPath $frames -Recurse -Force }

& "$root\build\x64\bin\MacWin.Settings.exe" --record-preview $frames --lang en --theme light | Out-Null
Start-Sleep -Milliseconds 200
while (Get-Process MacWin.Settings -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$root*" }) {
    Start-Sleep -Milliseconds 200
}

$out = Join-Path $root "docs\media\demo.gif"
$filter = "scale=860:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=200:stats_mode=full[p];" +
          "[b][p]paletteuse=dither=sierra2_4a:diff_mode=rectangle"
ffmpeg -hide_banner -loglevel error -y -framerate 30 -i "$frames\frame%04d.png" -vf $filter -loop 0 $out
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed" }
Remove-Item -LiteralPath $frames -Recurse -Force
"{0} ({1:N0} KB)" -f $out, ((Get-Item $out).Length / 1KB)
