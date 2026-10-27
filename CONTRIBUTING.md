# Contributing to Lampwin

Thank you for helping. Bug reports, translation fixes, ideas and code are all welcome.
Please read the [Code of Conduct](CODE_OF_CONDUCT.md) first.

- **Found a bug?** Open an issue with the *Bug report* form.
- **Have an idea?** Use the *Feature request* form, or start a
  [discussion](https://github.com/IMoggsYou/Lampwin/discussions) if you are not sure yet.
- **A translation is wrong or missing?** Use the *Translation* form, or send a pull request
  that edits the file for your language (see [Translations](#translations)).
- **Found a security problem?** Do not open an issue. Follow [SECURITY.md](SECURITY.md).

For larger changes, please open an issue or discussion before you start, so we can agree
on the direction and you do not spend time on something that cannot be merged.

## Building

Lampwin is built on Windows 11 x64. You need:

| Tool                | Notes                                                                 |
| ------------------- | --------------------------------------------------------------------- |
| llvm-mingw          | Scoop package `mingw-mstorsjo-llvm-ucrt` (`scoop install mingw-mstorsjo-llvm-ucrt`); the presets look for it in `%USERPROFILE%\scoop\apps` |
| Ninja               | Build generator (`scoop install ninja`)                               |
| CMake 3.25 or newer | `scoop install cmake`                                                 |
| .NET 10 SDK         | For the settings app (C# / WPF)                                       |
| Inno Setup 6        | Only to build the installer                                           |

Then, from the repository root:

```powershell
cmake --preset x64
cmake --build --preset x64
```

Build the installer (needs Inno Setup 6):

```powershell
cmake --build --preset x64 --target MacWinSetup
```

Run the tests:

```powershell
ctest --test-dir build/x64
```

The detailed engineering rules (architecture, code style, how the pieces fit together,
what to check before a change is done) are in [AGENTS.md](AGENTS.md). Please read it
before a bigger change.

## Rules for every pull request

- **Changelog.** Every user-visible change adds a line under `## [Unreleased]` in
  [CHANGELOG.md](CHANGELOG.md).
- **Strings.** Every user-visible string goes through a key in the `i18n/*.json` files,
  and the key must exist in **all 11 languages**: `en`, `pl`, `zh-Hans`, `hi`, `es`, `fr`,
  `ar`, `bn`, `ru`, `pt-BR`, `ja`. Never put user-visible text directly in code. If you are
  not sure about a translation, add your best attempt and flag it in the pull request so
  a native speaker can review it.
- **Tests.** `ctest --test-dir build/x64` must pass. Add or update tests with the change
  when you can.
- **Real-world check.** Say in the pull request which Windows 11 build and which GPU
  (AMD, Intel or NVIDIA) you tested on. Lampwin draws with the GPU, so this matters.
- **Small and focused.** One logical change per pull request. Follow the style of the code
  around you and the [`.editorconfig`](.editorconfig).
- **Commit messages.** A short imperative summary, with the area first when it helps
  (for example `Corners: keep the program's border color for the uncut`), then, if needed,
  a blank line and an explanation of why.
- **Sign off your commits** (see below).

## Translations

All text lives in `i18n/<language>.json`, one file per language (`en`, `pl`, `zh-Hans`,
`hi`, `es`, `fr`, `ar`, `bn`, `ru`, `pt-BR`, `ja`). English (`en`) is the source. To fix or
improve a translation, edit the file of your language and send a pull request; you do not
need to build anything. Corrections from native speakers are very welcome. Keep the keys,
any placeholders and any formatting markers exactly as they are in `en`.

## Licensing of contributions

Lampwin is licensed under the GNU General Public License, version 3 or (at your option) any
later version (GPL-3.0-or-later), with the additional terms in [NOTICE](NOTICE). See
[LICENSE](LICENSE).

- **Inbound = outbound.** By contributing, you agree that your contribution is licensed
  under GPL-3.0-or-later with the additional terms in [NOTICE](NOTICE), the same as the
  rest of Lampwin. You keep the copyright on your contribution.
- **Developer Certificate of Origin.** Every commit must carry a `Signed-off-by` line, which
  states that you agree to the [DCO 1.1](https://developercertificate.org/) below. Add it
  with `git commit -s`, which uses your `user.name` and `user.email` from git:

  ```
  Signed-off-by: Your Name <you@example.com>
  ```

  If you forgot it, fix the last commit with `git commit --amend -s --no-edit`, or add it
  to several commits with `git rebase --signoff main`.
- **Relicensing grant.** By contributing to Lampwin, you also grant IMoggsYou a perpetual,
  irrevocable, worldwide, non-exclusive, royalty-free license to use, copy, modify,
  distribute, sublicense and **relicense** your contribution, under any license terms,
  as part of Lampwin. This lets the project change its license in the future without
  asking every contributor again. It does not take away the rights that the GPL gives
  everyone else for the version you contributed to.

### Developer Certificate of Origin, version 1.1

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```
