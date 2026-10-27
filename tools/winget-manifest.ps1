# SPDX-License-Identifier: GPL-3.0-or-later
# MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

<#
.SYNOPSIS
  Writes the winget manifests for a published release (docs/WINGET.md).
.DESCRIPTION
  Fills the templates in packaging\winget with the version, the release's installer URL and
  its SHA-256 (from the release's SHA256SUMS.txt, or a local copy), into
  <OutDir>\manifests\i\IMoggsYou\Lampwin\<version>: the folder layout of
  github.com/microsoft/winget-pkgs. Submits nothing.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\winget-manifest.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\winget-manifest.ps1 -Version 1.5.0 -SumsFile dist\SHA256SUMS.txt
#>
param(
    [string]$Version,
    [string]$SumsFile,
    [string]$OutDir
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Version) { $Version = (Get-Content (Join-Path $repo 'VERSION') -Raw).Trim() }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Not a version: $Version" }
if (-not $OutDir) { $OutDir = Join-Path $repo 'build\winget' }

# The asset name stays MacWinSetup-*.exe: installed copies of 1.4.0 update only from that.
$installer = "MacWinSetup-$Version.exe"
$download = "https://github.com/IMoggsYou/Lampwin/releases/download/v$Version"
if ($SumsFile) {
    $sums = Get-Content $SumsFile -Raw
} else {
    $sums = (Invoke-WebRequest -UseBasicParsing "$download/SHA256SUMS.txt").Content
    if ($sums -is [byte[]]) { $sums = [Text.Encoding]::ASCII.GetString($sums) }
}
$line = ($sums -split "`n") | Where-Object { $_ -match "^([0-9a-fA-F]{64})\s+\*?$([regex]::Escape($installer))\s*$" } |
    Select-Object -First 1
if (-not $line) { throw "SHA256SUMS.txt has no line for $installer" }
$sha = $Matches[1].ToUpperInvariant()

$values = @{
    '{{Version}}'         = $Version
    '{{InstallerUrl}}'    = "$download/$installer"
    '{{InstallerSha256}}' = $sha
    '{{ReleaseDate}}'     = (Get-Date -Format 'yyyy-MM-dd')
}
$target = Join-Path $OutDir "manifests\i\IMoggsYou\Lampwin\$Version"
New-Item -ItemType Directory -Force $target | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false
Get-ChildItem (Join-Path $repo 'packaging\winget') -Filter '*.yaml' | ForEach-Object {
    # The comments at the top are for this repository (all but the schema line).
    $lines = New-Object Collections.Generic.List[string]
    $top = $true
    foreach ($l in Get-Content $_.FullName -Encoding UTF8) {
        if ($top -and $l.StartsWith('#') -and -not $l.StartsWith('# yaml-language-server')) { continue }
        if (-not $l.StartsWith('#')) { $top = $false }
        $lines.Add($l)
    }
    $text = ($lines -join "`n") + "`n"
    foreach ($key in $values.Keys) { $text = $text.Replace($key, $values[$key]) }
    if ($text -match '\{\{\w+\}\}') { throw "$($_.Name): $($Matches[0]) not filled in" }
    [IO.File]::WriteAllText((Join-Path $target $_.Name), $text, $utf8)
}
Write-Host "Manifests for $Version in $target"
Write-Host "Check them with: winget validate --manifest `"$target`""
