# Translating Lampwin

Lampwin speaks English, Polish, Simplified Chinese, Hindi, Spanish, French, Arabic,
Bengali, Russian, Brazilian Portuguese and Japanese. It picks the Windows display
language (or English for any other one); users can also choose a language in
**Settings → General → Language**.

Native speakers make the best translations — corrections are very welcome. You don't
need to build anything to fix a translation.

## Where the texts are

| What | File |
| --- | --- |
| Settings window, tray menu, notifications, messages | `i18n/<code>.json` (`en`, `pl`, `zh-Hans`, `hi`, `es`, `fr`, `ar`, `bn`, `ru`, `pt-BR`, `ja`) |
| Installer | `installer/languages/<Language>.isl` |
| Word list (terms used everywhere) | `i18n/GLOSSARY.md` |

`en.json` is the source. Keys start with `settings.` (settings window) or `host.` (tray and
messages). `language.name` is the language's own name, shown in the language menu.

## Rules

- Keep every key and every placeholder (`{0}`) exactly as in English.
- Use the terms in the glossary: Windows words as Windows says them in your language
  (taskbar, notification area, minimize), macOS words as macOS says them (genie effect).
- Match the tone of the file: short, friendly, no exclamation marks.
- Arabic: a Latin name followed by a number gets `\u200e` (left-to-right mark) before the
  number, e.g. `Windows \u200e11`, or it would show as "11 Windows".
- Installer files are UTF-8 with BOM; keep the `[CustomMessages]` names.

## Check your change

```powershell
cmake -P tools/check_i18n.cmake        # same keys, nothing empty, same placeholders
MacWin.Settings.exe --snapshot C:\temp\snap --lang <code>   # every page as PNG
MacWin.exe --widget-snapshot C:\temp\snap --lang <code>     # every desktop widget (widgets.png)
```

Look at the PNGs for text that is cut off or wraps badly. `host.widgets.date.*` are not
sentences but Windows date patterns (`dddd` weekday, `d` day, `MMMM` month, `yyyy` year,
text in single quotes): put them in your language's order. Open a pull request (or use the
*Translation* issue form) and say which language you checked.

## Adding a language

1. Copy `i18n/en.json` to `i18n/<code>.json` and translate it.
2. Add the code to `Loc.Languages` (`settings-app/Services/Loc.cs`), `i18n::kLanguages`
   and an RCDATA line (`src/host/i18n.cpp`, `src/host/host.rc.in`, same order), and
   `LANGUAGES` in `tools/check_i18n.cmake`. Map regional variants in both `Resolve`
   functions if needed, and give the script a font in `Loc.ApplyFonts` if Segoe UI lacks it.
   The widgets write dates with a Windows locale: add one to `i18n::LocaleName`
   (`src/host/i18n.cpp`) and, if it isn't the code itself, to `WidgetPreview.DateCulture`.
3. Add the installer language: a `[Languages]` line in `installer/MacWin.iss` and
   `installer/languages/<Language>.isl`.
4. Add it to the language tables in the READMEs and to `CHANGELOG.md`.
