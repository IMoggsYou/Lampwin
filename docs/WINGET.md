# winget

With a package in the [Windows Package Manager](https://learn.microsoft.com/windows/package-manager/)
community repository, people can install Lampwin with

```powershell
winget install IMoggsYou.Lampwin
```

The manifests are templates in [`packaging/winget`](../packaging/winget); a script fills them
in for a published release. Nothing is sent to Microsoft by the build or the release
workflow: the owner (IMoggsYou) submits each version.

## Make the manifests for a release

After the release is published (it has `MacWinSetup-X.Y.Z.exe` and `SHA256SUMS.txt`):

```powershell
powershell -ExecutionPolicy Bypass -File tools\winget-manifest.ps1
winget validate --manifest build\winget\manifests\i\IMoggsYou\Lampwin\X.Y.Z
```

The script takes the version from `VERSION` (or `-Version X.Y.Z`) and the SHA-256 from the
release's `SHA256SUMS.txt` (or a local copy, `-SumsFile`). To try the package before
submitting: `winget install --manifest build\winget\manifests\i\IMoggsYou\Lampwin\X.Y.Z`
(needs `winget settings --enable LocalManifestFiles` once, as administrator).

## Submit

1. Fork <https://github.com/microsoft/winget-pkgs> (once).
2. Copy the folder `build\winget\manifests\i\IMoggsYou\Lampwin\X.Y.Z` into the fork at the
   same path, commit, and open a pull request to `microsoft/winget-pkgs`. Its bots install
   the package in a sandbox and check it; a moderator merges it, usually within a few days.
3. For later versions, [wingetcreate](https://github.com/microsoft/winget-create) can do
   steps 1–2 in one command:
   `wingetcreate update IMoggsYou.Lampwin --version X.Y.Z --urls <installer URL> --submit`.

Installed copies keep updating themselves either way; winget only adds another way to
install (and `winget upgrade` sees new versions once they are submitted).

## Notes

- The installer is Inno Setup (`InstallerType: inno`): winget runs it silently with
  `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`. It asks for administrator rights itself
  (`ElevationRequirement: elevatesSelf`).
- `ProductCode` is the installer's `AppId` plus `_is1`, the same for every version, so
  winget recognises upgrades and an existing MacWin install.
- The download keeps the name `MacWinSetup-X.Y.Z.exe`: MacWin 1.4.0 updates only from a file
  with that name.
- A signed installer ([SIGNING.md](SIGNING.md)) passes the winget checks with fewer
  questions; it isn't required.
