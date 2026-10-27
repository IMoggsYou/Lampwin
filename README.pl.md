<div align="center">

<img src="docs/media/icon.png" width="128" height="128" alt="Ikona Lampwin">

# Lampwin

**Wygląd macOS dla Windows 11: efekt dżina, rogi squircle, widżety na pulpicie i ikony aplikacji.**

Okna wpadają do paska zadań jak dżin i wylatują z niego z powrotem, a do tego dostają miękkie
rogi jak w macOS. Pulpit dostaje widżety z macOS, programy — wybrane przez Ciebie ikony, a
Windows wysyła mniej danych o Tobie. Lekki, darmowy i open source.

[![Najnowsza wersja](https://img.shields.io/github/v/release/IMoggsYou/Lampwin?style=for-the-badge&logo=github&label=wersja&color=0A84FF)](https://github.com/IMoggsYou/Lampwin/releases/latest)
[![Pobrania](https://img.shields.io/github/downloads/IMoggsYou/Lampwin/total?style=for-the-badge&label=pobrania&color=34C759)](https://github.com/IMoggsYou/Lampwin/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/IMoggsYou/Lampwin/build.yml?branch=main&style=for-the-badge&label=build)](https://github.com/IMoggsYou/Lampwin/actions/workflows/build.yml)
[![Windows 11](https://img.shields.io/badge/Windows-11-0078D4?style=for-the-badge&logo=windows11&logoColor=white)](#wymagania)
[![Języki](https://img.shields.io/badge/j%C4%99zyki-11-AF52DE?style=for-the-badge)](#języki)
[![Licencja](https://img.shields.io/badge/licencja-GPL--3.0-orange?style=for-the-badge)](LICENSE)

### [⬇&nbsp; Pobierz Lampwin dla Windows 11](https://github.com/IMoggsYou/Lampwin/releases/latest)

<img src="docs/media/demo.gif" width="860" alt="Okno wpada do swojej ikony na pasku zadań efektem dżina i wylatuje z powrotem">

[English](README.md) · **Polski**

<sub>Lampwin nazywał się MacWin do wersji 1.4.0; stare linki prowadzą tutaj.</sub>

</div>

---

## Co potrafi

- 🧞 **Efekt dżina** — minimalizowane okno wlewa się do swojej ikony na pasku zadań i wypływa
  z niej przy przywracaniu; rysowane na karcie graficznej płynną krzywą „lampy” (albo
  klasycznym lejkiem), a cień okna leci razem z nim. Wolisz coś spokojniejszego? Efekt
  **Skalowanie** zmniejsza okno do ikony jednym miękkim ruchem.
- 🪟 **Otwieranie i zamykanie** okien efektem dżina, z ikony programu i do niej, albo
  efektem **Zoom** (z miejsca, w które kliknięto), **Rozpad** lub **Odłamki** (okno
  pęka na kawałki szkła, które odlatują w 3D).
- ↩️ **Odwracanie w locie** — kliknij jeszcze raz w trakcie, a dżin zawróci dokładnie z tego
  miejsca. Win+D animuje wszystkie okna naraz. Gdy w Windows wyłączysz *Efekty animacji*,
  Lampwin też przestaje animować.
- ⬛ **Rogi squircle** — kształt z macOS (wygładzenie 100%) albo zaokrąglenie Windows 11,
  dla animacji, okna ustawień i programów, które same rysują pasek tytułu (Chrome, Edge,
  Brave, Discord, VS Code, Spotify…), z dopasowanym cieniem.
- 🗓️ **Widżety na pulpicie** — zegar, kalendarz, pogoda, wskaźniki systemu, notatka,
  zdjęcie, odliczanie do wybranego dnia i to, co teraz gra, jasne albo ciemne. Leżą na
  pulpicie pod oknami i układają się w siatce jak w macOS, aż do paska zadań; przeciągnij je
  w dowolne miejsce, prawy przycisk zmienia rozmiar i wygląd.
- 🎨 **Ikony aplikacji** — daj dowolnemu programowi własną ikonę: na jego oknach, w Alt+Tab,
  na pasku zadań i w menu Start. Dodaj ikony z macOS (`.icns`), PNG, JPEG, WebP, HEIC, ICO
  albo weź ikonę z innego programu; Lampwin przycina ją do wielkości ikon Windows i sam
  znajduje program po nazwie pliku. Zestaw ikon w stylu macOS jest w komplecie. Zapisz
  swoje ustawienie jako **zestaw ikon**, wróć do **ikon Windows** jednym kliknięciem albo
  usuń wszystko.
- 🔒 **Przełączniki prywatności** — wyłącz telemetrię Windows, identyfikator reklamowy,
  reklamy i porady w menu Start i na ekranie blokady, wyniki z internetu w wyszukiwaniu,
  Copilota, Recall i więcej. Każda wartość jest najpierw zapisywana, a **Przywróć** cofa
  wszystko; Windows Update, Defender, SmartScreen i zapora nie są nigdy ruszane.
- ⚙️ **Okno ustawień jak Ustawienia systemowe macOS** — podgląd na żywo, wyszukiwarka, jasny
  i ciemny motyw, wykluczenia programów i ustawienia w pliku do przeniesienia na inny
  komputer.
- 🌍 **11 języków**, wybieranych automatycznie z Windows (zobacz [Języki](#języki)).
- 🔄 **Automatyczne aktualizacje** z GitHuba, sprawdzane sumą SHA-256 — instalowane w tle
  albo proponowane z krótkim „co nowego” przy otwarciu ustawień.
- 🚀 **Startuje razem z Windows**, siedzi w obszarze powiadomień, w spoczynku nie używa
  procesora.
- 🛡️ **Bezpieczny z założenia** — nie wchodzi do procesów systemowych ani gier z anty-cheatem,
  przy każdym problemie wraca do zwykłej animacji Windows, a **Ctrl+Alt+Shift+F12**
  natychmiast go wyłącza.
- 🖥️ **Każdy komputer z Windows 11** — karty AMD, Intel i NVIDIA albo żadna (wtedy rysuje na
  procesorze). Animuje też programy 32-bitowe i uruchomione jako administrator.

## Zrzuty ekranu

<p align="center">
  <img src="docs/media/settings-light.png" width="49%" alt="Animacje okien: podgląd dżina na żywo i trzy style">
  <img src="docs/media/settings-dark.png" width="49%" alt="Rogi okien w ciemnym motywie: squircle z macOS albo rogi Windows 11">
</p>

<p align="center">
  <img src="docs/media/app-icons.png" width="49%" alt="Ikony aplikacji: ikony z macOS dla Signala, Spotify, Steama, Telegrama i VS Code">
  <img src="docs/media/privacy.png" width="49%" alt="Prywatność: przełączniki danych diagnostycznych Windows, z kopią i przywracaniem">
</p>

<p align="center">
  <img src="docs/media/widgets.png" width="860" alt="Widżety na pulpicie: zegary, kalendarze, pogoda, wskaźniki systemu i notatka">
</p>

## Instalacja

1. Pobierz **`MacWinSetup-x.y.z.exe`** z
   [najnowszego wydania](https://github.com/IMoggsYou/Lampwin/releases/latest).
2. Uruchom. Jeśli brakuje .NET 10 Desktop Runtime (potrzebnego oknu ustawień), instalator
   sam pobierze go od Microsoftu.
3. Gotowe — zminimalizuj dowolne okno. Lampwin startuje razem z Windows, jego ikona jest
   w obszarze powiadomień (kliknij ją, żeby otworzyć ustawienia).

> [!IMPORTANT]
> **Masz MacWin 1.3.1 lub starszy?** Te wersje nie zaktualizują się same od zmiany nazwy
> projektu. Zainstaluj najnowsze wydanie raz ręcznie (ustawienia zostaną); potem znów
> aktualizuje się samo.

> [!NOTE]
> Lampwin nie ma jeszcze podpisu cyfrowego, więc Windows SmartScreen może pokazać
> *„System Windows ochronił ten komputer”*. Kliknij **Więcej informacji → Uruchom mimo to**.
> Każde wydanie buduje GitHub Actions z tego repozytorium, z opublikowaną sumą SHA-256
> (`SHA256SUMS.txt`) i [poświadczeniem pochodzenia](https://github.com/IMoggsYou/Lampwin/attestations).

### Wymagania

| | |
| --- | --- |
| Windows | Windows 11 (zalecany 22H2 lub nowszy), 64-bitowy **x64** |
| Grafika | Dowolna: AMD, Intel, NVIDIA albo żadna (Direct3D 11 / WARP) |
| Dysk | ok. 7 MB (+ .NET 10 Desktop Runtime dla okna ustawień) |
| Jeszcze nieobsługiwane | Windows na ARM (komputery ze Snapdragonem) |

### Odinstalowanie

**Ustawienia → Aplikacje → Zainstalowane aplikacje → Lampwin → Odinstaluj.** Instalator
zapyta, czy usunąć też ustawienia.

## Języki

Lampwin mówi w Twoim języku: bierze język wyświetlania Windows, a gdy go nie zna — angielski.
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

Widżety nic nie kosztują, dopóki żadnego nie dodasz. Widżet jest przerysowywany tylko wtedy,
gdy coś się na nim zmienia — raz na minutę, co dwie sekundy dla wskaźników systemu, co
sekundę dla zegara ze wskazówką sekundową (ok. 0,4 ms procesora na tyknięcie, rysowane są
tylko wskazówki) — i wcale, gdy ekran jest wyłączony, komputer zablokowany albo działa gra
na pełnym ekranie.

## Prywatność

Lampwin niczego nie zbiera. Łączy się z internetem tylko w dwóch sprawach:

- **Aktualizacje:** raz dziennie pyta API GitHuba, czy jest nowe wydanie, i pobiera je z
  GitHuba, jeśli zdecydujesz się zaktualizować. Można to wyłączyć w
  **Ustawienia → Ogólne → Aktualizacje**.
- **Pogoda**, tylko gdy na pulpicie jest widżet Pogoda: co 30 minut Lampwin pyta
  [Open-Meteo](https://open-meteo.com) o prognozę. Z opcją **Użyj mojej lokalizacji**
  (domyślnie włączoną) miejsce pochodzi z usług lokalizacji Windows, a
  [OpenStreetMap](https://nominatim.openstreetmap.org) podaje nazwę miejscowości, pytany tylko
  po zmianie miejsca. Oba dostają współrzędne zaokrąglone do około kilometra, nigdy
  dokładniejsze. Ręczne wyszukiwanie miejsca wysyła do Open-Meteo wpisaną nazwę. Nic więcej
  nie jest wysyłane i nie trzeba żadnego konta ani klucza. Część z lokalizacją wyłącza
  przełącznik w Windows (**Ustawienia → Prywatność i zabezpieczenia → Lokalizacja**).

Ikony aplikacji, strona Prywatność i pozostałe widżety działają w całości na Twoim komputerze.
Widżet Teraz gra pokazuje to samo co przyciski multimediów Windows (utwór, wykonawcę,
aplikację); odczytuje to na Twoim komputerze i tylko wtedy, gdy widżet jest na pulpicie.

## Pytania i odpowiedzi

<details>
<summary><b>Czy to bezpieczne dla gier z anty-cheatem?</b></summary>

Lampwin nigdy nie wchodzi do gier zainstalowanych w folderach gier Steam, Epic, Riot, Xbox,
EA, Ubisoft i GOG, do gier z EasyAntiCheat albo BattlEye ani do listy znanych gier
z anty-cheatem. Dowolny program lub folder możesz dodać w **Ustawienia → Wykluczenia →
Nigdy nie wczytuj Lampwin**.
</details>

<details>
<summary><b>Coś wygląda źle. Jak to natychmiast wyłączyć?</b></summary>

Naciśnij **Ctrl+Alt+Shift+F12**. Lampwin się zamyka, a okna działają jak zwykle. Potem
[zgłoś problem](https://github.com/IMoggsYou/Lampwin/issues/new/choose) i dołącz dziennik
(`%LOCALAPPDATA%\MacWin\host.log`).
</details>

<details>
<summary><b>Dlaczego instalator prosi o uprawnienia administratora?</b></summary>

Instaluje Lampwin w Program Files i rejestruje zadanie w Harmonogramie zadań, żeby Lampwin
mógł startować z Windows i animować programy uruchomione jako administrator — bez okna UAC
przy każdym logowaniu.
</details>

<details>
<summary><b>Dlaczego Eksplorator plików i Notatnik zostają przy rogach Windows 11?</b></summary>

Okna z systemowym paskiem tytułu psują się po zmianie kształtu. Lampwin daje squircle
dżinowi, swojemu oknu i programom, które same rysują pasek tytułu.
</details>

<details>
<summary><b>Jak przywrócić programowi jego własną ikonę?</b></summary>

W **Ustawienia → Ikony aplikacji** wybierz **Brak programu** obok ikony albo naciśnij
**Użyj ikon Windows**, żeby wszystkie programy dostały z powrotem swoje ikony (Twoje ikony
zostają na liście). Przypięte ikony na pasku zadań odświeżają się po **Uruchom ponownie
Eksplorator**.
</details>

<details>
<summary><b>Czy strona Prywatność zepsuje Windows Update albo antywirusa?</b></summary>

Nie. Ma tylko przełączniki telemetrii, reklam i podobnych funkcji, nigdy Windows Update,
Microsoft Defendera, SmartScreena, zapory ani UAC. Każda wartość jest zapisywana przed
zmianą, a **Przywróć** odtwarza dokładnie to, co było.
</details>

<details>
<summary><b>Czy działa z kilkoma monitorami i automatycznie ukrywanym paskiem zadań?</b></summary>

Tak. Ukryty pasek zadań wysuwa się, gdy okno się minimalizuje; przy kilku monitorach dżin
może celować w pasek zadań na monitorze okna (Ustawienia → Animacje okien).
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

Jeśli Lampwin Ci się podoba, ⭐ pomaga innym go znaleźć.

## Podziękowania

- Logika animacji dżina pochodzi z moda do Windhawka
  [MacOS Minimize Animation](https://github.com/Abdullah-Masood-05) autorstwa Abdullah Masood,
  z silnikiem dżina od [Potassiumunchera](https://github.com/Potassiumuncher) (MIT).
- [MinHook](https://github.com/TsudaKageyu/minhook) — Tsuda Kageyu (BSD 2-Clause).

Pełne informacje: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Licencja

Autor: **[IMoggsYou](https://github.com/IMoggsYou)**. Copyright © 2026 IMoggsYou.

Lampwin to wolne oprogramowanie na licencji [GNU GPL w wersji 3.0 lub nowszej](LICENSE),
z dodatkowymi warunkami z pliku [NOTICE](NOTICE): zachowaj informację o autorze, oznacz
zmienione wersje i nadaj im inną nazwę oraz ikonę ([TRADEMARKS.md](TRADEMARKS.md)).

macOS jest znakiem towarowym Apple Inc., a Windows — Microsoft Corporation. Lampwin nie jest
powiązany z żadną z tych firm.
