<div align="center">

<img src="docs/media/icon.png" width="128" height="128" alt="Ikona MacWin">

# MacWin

**Efekt dżina z macOS i zaokrąglone rogi okien (squircle) — dla Windows 11.**

Okna wpadają do paska zadań jak dżin i wylatują z niego z powrotem, tak samo się otwierają
i zamykają, a do tego dostają miękkie rogi jak w macOS. Lekki, darmowy i open source.

[![Najnowsza wersja](https://img.shields.io/github/v/release/IMoggsYou/MacWin?style=for-the-badge&logo=github&label=wersja&color=0A84FF)](https://github.com/IMoggsYou/MacWin/releases/latest)
[![Pobrania](https://img.shields.io/github/downloads/IMoggsYou/MacWin/total?style=for-the-badge&label=pobrania&color=34C759)](https://github.com/IMoggsYou/MacWin/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/IMoggsYou/MacWin/build.yml?branch=main&style=for-the-badge&label=build)](https://github.com/IMoggsYou/MacWin/actions/workflows/build.yml)
[![Windows 11](https://img.shields.io/badge/Windows-11-0078D4?style=for-the-badge&logo=windows11&logoColor=white)](#wymagania)
[![Języki](https://img.shields.io/badge/j%C4%99zyki-11-AF52DE?style=for-the-badge)](#języki)
[![Licencja](https://img.shields.io/badge/licencja-GPL--3.0-orange?style=for-the-badge)](LICENSE)

### [⬇&nbsp; Pobierz MacWin dla Windows 11](https://github.com/IMoggsYou/MacWin/releases/latest)

<img src="docs/media/demo.gif" width="860" alt="Okno wpada do swojej ikony na pasku zadań efektem dżina i wylatuje z powrotem">

[English](README.md) · **Polski**

</div>

---

## Co potrafi

- 🧞 **Efekt dżina** — minimalizowane okno wlewa się do swojej ikony na pasku zadań i wypływa
  z niej przy przywracaniu; rysowane na karcie graficznej płynną krzywą „lampy” (albo
  klasycznym lejkiem).
- 🪟 **Otwieranie i zamykanie** okien efektem dżina, z ikony programu i do niej.
- ↩️ **Odwracanie w locie** — kliknij jeszcze raz w trakcie, a dżin zawróci dokładnie z tego
  miejsca. Win+D animuje wszystkie okna naraz.
- ⬛ **Rogi squircle** — kształt z macOS (wygładzenie 100%) albo zaokrąglenie Windows 11,
  dla animacji, okna ustawień i programów, które same rysują pasek tytułu (Chrome, Edge,
  Brave, Discord, VS Code, Spotify…), z dopasowanym cieniem.
- ⚙️ **Okno ustawień jak Ustawienia systemowe macOS** — podgląd na żywo, wyszukiwarka, jasny
  i ciemny motyw, wykluczenia programów.
- 🌍 **11 języków**, wybieranych automatycznie z Windows (zobacz [Języki](#języki)).
- 🔄 **Automatyczne aktualizacje** z GitHuba, sprawdzane sumą SHA-256 — jednym kliknięciem
  albo w pełni automatycznie.
- 🚀 **Startuje razem z Windows**, siedzi w obszarze powiadomień, w spoczynku nie używa
  procesora.
- 🛡️ **Bezpieczny z założenia** — nie wchodzi do procesów systemowych ani gier z anty-cheatem,
  przy każdym problemie wraca do zwykłej animacji Windows, a **Ctrl+Alt+Shift+F12**
  natychmiast go wyłącza.
- 🖥️ **Każdy komputer z Windows 11** — karty AMD, Intel i NVIDIA albo żadna (wtedy rysuje na
  procesorze). Animuje też programy 32-bitowe i uruchomione jako administrator.

## Zrzuty ekranu

<p align="center">
  <img src="docs/media/settings-light.png" width="49%" alt="Ustawienia MacWin, jasny motyw">
  <img src="docs/media/settings-dark.png" width="49%" alt="Ustawienia MacWin, ciemny motyw">
</p>

## Instalacja

1. Pobierz **`MacWinSetup-x.y.z.exe`** z
   [najnowszego wydania](https://github.com/IMoggsYou/MacWin/releases/latest).
2. Uruchom. Jeśli brakuje .NET 10 Desktop Runtime (potrzebnego oknu ustawień), instalator
   sam pobierze go od Microsoftu.
3. Gotowe — zminimalizuj dowolne okno. MacWin startuje razem z Windows, jego ikona jest
   w obszarze powiadomień (kliknij ją, żeby otworzyć ustawienia).

> [!NOTE]
> MacWin nie ma jeszcze podpisu cyfrowego, więc Windows SmartScreen może pokazać
> *„System Windows ochronił ten komputer”*. Kliknij **Więcej informacji → Uruchom mimo to**.
> Każde wydanie buduje GitHub Actions z tego repozytorium, z opublikowaną sumą SHA-256
> (`SHA256SUMS.txt`) i [poświadczeniem pochodzenia](https://github.com/IMoggsYou/MacWin/attestations).

### Wymagania

| | |
| --- | --- |
| Windows | Windows 11 (zalecany 22H2 lub nowszy), 64-bitowy **x64** |
| Grafika | Dowolna: AMD, Intel, NVIDIA albo żadna (Direct3D 11 / WARP) |
| Dysk | ok. 5 MB (+ .NET 10 Desktop Runtime dla okna ustawień) |
| Jeszcze nieobsługiwane | Windows na ARM (komputery ze Snapdragonem) |

### Odinstalowanie

**Ustawienia → Aplikacje → Zainstalowane aplikacje → MacWin → Odinstaluj.** Instalator
zapyta, czy usunąć też ustawienia.

## Języki

MacWin mówi w Twoim języku: bierze język wyświetlania Windows, a gdy go nie zna — angielski.
Język można też wybrać w **Ustawienia → Ogólne → Język**.

| | | | |
| --- | --- | --- | --- |
| English | Polski | 简体中文 (chiński) | हिन्दी (hindi) |
| Español | Français | العربية (arabski) | বাংলা (bengalski) |
| Русский (rosyjski) | Português (Brasil) | 日本語 (japoński) | |

<p align="center">
  <img src="docs/media/languages.png" width="760" alt="Okno ustawień po japońsku, arabsku, w hindi i po polsku">
</p>

Widzisz niezgrabne tłumaczenie? Poprawki od native speakerów są bardzo mile widziane —
zobacz [docs/TRANSLATING.md](docs/TRANSLATING.md); nie trzeba niczego budować.

## Lekki dla komputera

Zmierzone na Windows 11 z kartą NVIDIA GeForce RTX 2070, MacWin 1.0.0, 30 sekund bezczynności:

| Proces | Procesor w spoczynku | Pamięć |
| --- | --- | --- |
| `MacWin.exe` (zasobnik, renderer) | 0 ms | ok. 52 MB (głównie sterownik karty) |
| `MacWinHelper32.exe` (programy 32-bitowe) | 0 ms | ok. 9 MB |
| `MacWinHook64.dll` w każdym programie | — | ok. 340 KB |

Między animacjami nic nie działa: żadnego odpytywania ani timerów. Jedna klatka dżina to
ok. 0,3 ms pracy procesora.

## Prywatność

MacWin niczego nie zbiera. Jedyne połączenie sieciowe to sprawdzenie aktualizacji: raz
dziennie pyta API GitHuba, czy jest nowe wydanie, i pobiera je z GitHuba, jeśli zdecydujesz
się zaktualizować. Można to wyłączyć w **Ustawienia → Ogólne → Aktualizacje**.

## Pytania i odpowiedzi

<details>
<summary><b>Czy to bezpieczne dla gier z anty-cheatem?</b></summary>

MacWin nigdy nie wchodzi do gier zainstalowanych w folderach gier Steam, Epic, Riot, Xbox,
EA, Ubisoft i GOG, do gier z EasyAntiCheat albo BattlEye ani do listy znanych gier
z anty-cheatem. Dowolny program lub folder możesz dodać w **Ustawienia → Wykluczenia →
Nigdy nie wczytuj MacWin**.
</details>

<details>
<summary><b>Coś wygląda źle. Jak to natychmiast wyłączyć?</b></summary>

Naciśnij **Ctrl+Alt+Shift+F12**. MacWin się zamyka, a okna działają jak zwykle. Potem
[zgłoś problem](https://github.com/IMoggsYou/MacWin/issues/new/choose) i dołącz dziennik
(`%LOCALAPPDATA%\MacWin\host.log`).
</details>

<details>
<summary><b>Dlaczego instalator prosi o uprawnienia administratora?</b></summary>

Instaluje MacWin w Program Files i rejestruje zadanie w Harmonogramie zadań, żeby MacWin
mógł startować z Windows i animować programy uruchomione jako administrator — bez okna UAC
przy każdym logowaniu.
</details>

<details>
<summary><b>Dlaczego Eksplorator plików i Notatnik zostają przy rogach Windows 11?</b></summary>

Okna z systemowym paskiem tytułu psują się po zmianie kształtu. MacWin daje squircle
dżinowi, swojemu oknu i programom, które same rysują pasek tytułu.
</details>

## Budowanie ze źródeł

```powershell
cmake --preset x64
cmake --build --preset x64
ctest --test-dir build/x64
cmake --build --preset x64 --target MacWinSetup   # instalator
```

Potrzebne: llvm-mingw, CMake, Ninja, .NET 10 SDK i Inno Setup 6 — szczegóły w
[docs/BUILDING.md](docs/BUILDING.md) i [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Współtworzenie

Zgłoszenia błędów, pomysły, tłumaczenia i kod są mile widziane. Przeczytaj
[CONTRIBUTING.md](CONTRIBUTING.md) (oraz [AGENTS.md](AGENTS.md), jeśli pracujesz z asystentem
AI). Problemy z bezpieczeństwem: [SECURITY.md](SECURITY.md).

Jeśli MacWin Ci się podoba, ⭐ pomaga innym go znaleźć.

## Podziękowania

- Logika animacji dżina pochodzi z moda do Windhawka
  [MacOS Minimize Animation](https://github.com/Abdullah-Masood-05) autorstwa Abdullah Masood,
  z silnikiem dżina od [Potassiumunchera](https://github.com/Potassiumuncher) (MIT).
- [MinHook](https://github.com/TsudaKageyu/minhook) — Tsuda Kageyu (BSD 2-Clause).

Pełne informacje: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Licencja

Autor: **[KosaWLeb](https://github.com/IMoggsYou)**. Copyright © 2026 KosaWLeb.

MacWin to wolne oprogramowanie na licencji [GNU GPL w wersji 3.0 lub nowszej](LICENSE),
z dodatkowymi warunkami z pliku [NOTICE](NOTICE): zachowaj informację o autorze, oznacz
zmienione wersje i nadaj im inną nazwę oraz ikonę ([TRADEMARKS.md](TRADEMARKS.md)).

macOS jest znakiem towarowym Apple Inc., a Windows — Microsoft Corporation. MacWin nie jest
powiązany z żadną z tych firm.
