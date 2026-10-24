# Security policy

## Supported versions

Only the latest release of MacWin receives security fixes. Please update to the latest
release (see [Releases](https://github.com/IMoggsYou/Lampwin/releases)) before reporting,
and say in the report which version you tested.

| Version        | Supported |
| -------------- | --------- |
| Latest release | Yes       |
| Older releases | No        |

## Reporting a vulnerability

Please report it **privately**. Do not open a public issue, pull request or discussion.

1. Go to <https://github.com/IMoggsYou/Lampwin/security/advisories/new>
   ("Report a vulnerability" on the repository's Security tab).
2. Describe the problem, the affected version, your Windows version and build, the
   steps to reproduce it, and what an attacker could achieve. A proof of concept helps.

**What to expect:** an acknowledgement within 7 days and a first assessment within
14 days. Fixes are developed in private and released as soon as they are ready; the
target is within 90 days of the report. You are credited in the release notes and the
advisory unless you prefer to stay anonymous. This is a volunteer project, so these
times are goals, not guarantees. Please keep the details private until a fixed
release is out.

## Scope

MacWin runs code inside other programs and downloads updates, so these parts matter
most:

- **The hook DLL** (`MacWinHook64.dll` / `MacWinHook32.dll`) is injected into other
  processes to animate their windows. Reports about memory safety, privilege
  boundaries, or ways to make it load or run something it should not are in scope.
- **The host and its IPC** (`MacWin.exe`, `MacWinHelper32.exe`): the channel between
  the host, the helper and the injected DLLs, the elevation helper, and handling of
  the settings file.
- **The updater**: it downloads releases from GitHub Releases and verifies their
  SHA-256 checksum before using them. Ways to make it install an unverified or
  tampered file, or to downgrade, are in scope.
- **The installer** and the **settings app**.

Out of scope:

- Vulnerabilities in other programs, in Windows, or in third-party libraries, unless
  MacWin makes them worse. Report those to the project concerned.
- Attacks that already need administrator rights or full control of the user's
  account or machine.
- Antivirus or SmartScreen warnings (injecting into other processes looks like
  malware to some scanners). Open a normal issue for false positives.
- Crashes of MacWin or of another program that cannot be used to attack anything.
  Report those as normal bugs.
