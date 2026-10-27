# Code signing (SignPath)

An unsigned installer makes Windows SmartScreen say "Windows protected your PC" and makes
some antivirus programs suspicious of the hook DLL. Open-source projects can get a free
certificate through the [SignPath Foundation](https://signpath.org). The release workflow
is ready for it: once the steps below are done, every `v*` tag publishes a signed
`MacWinSetup-X.Y.Z.exe`. Until then releases are published unsigned, exactly as before.

Only the owner (IMoggsYou) can do these steps: they need the owner's accounts.

## 1. Apply

1. Read the [conditions](https://signpath.org/terms) (an OSI license such as GPL-3.0, the
   project released and maintained, no hidden behaviour, multi-factor sign-in for everyone
   who can push).
2. Apply at <https://signpath.org/apply> with the repository
   `https://github.com/IMoggsYou/Lampwin`.
3. When approved, SignPath sets up an organization for the project and invites you.

## 2. Set up the project in SignPath

- **Project** with the slug `Lampwin`, connected to the GitHub repository as a *trusted
  build system* (GitHub.com).
- **Artifact configuration** with the slug `installer`. The artifact SignPath receives is the
  workflow's `MacWinSetup` artifact, which GitHub zips:

  ```xml
  <?xml version="1.0" encoding="utf-8"?>
  <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
    <zip-file>
      <pe-file path="MacWinSetup-*.exe">
        <authenticode-sign/>
      </pe-file>
    </zip-file>
  </artifact-configuration>
  ```

- **Signing policy** with the slug `release-signing` (the Foundation's certificate; approval
  by you for each release is the usual setting).
- A **CI user** for GitHub Actions; copy its API token.

Other slugs work too: set them in the repository variables `SIGNPATH_PROJECT_SLUG`,
`SIGNPATH_SIGNING_POLICY_SLUG` and `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG`.

## 3. Turn it on in GitHub

In the repository's **Settings → Secrets and variables → Actions**:

| Kind | Name | Value |
| --- | --- | --- |
| Variable | `SIGNPATH_ORGANIZATION_ID` | your SignPath organization's ID |
| Secret | `SIGNPATH_API_TOKEN` | the CI user's API token |

That's all: the `sign` job in `.github/workflows/release.yml` runs whenever
`SIGNPATH_ORGANIZATION_ID` is set. It sends the installer to SignPath, waits for the signed
one (up to an hour, for your approval), and the release publishes that; `SHA256SUMS.txt`
is computed from the signed file, so the updater accepts it. If signing fails, nothing is
published. The Foundation also asks for a short "Code signing policy" section in the README;
add it when they approve the project.

## What is signed

The installer. The programs inside it (MacWin.exe, the hook DLLs, the settings app) are
not signed yet: Inno Setup compresses them, so SignPath can't reach them in the finished
installer. Signing them means a second request before the installer is built (a
`build/x64/bin` artifact with its own artifact configuration); worth doing once the
installer signing works.
