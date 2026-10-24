; MacWin installer texts (Polish). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=Zostanie zainstalowany [name/ver] – animacje okien w stylu macOS: okna wpadają do paska zadań jak dżin i wylatują z niego z powrotem.%n%nMacWin działa w tle, z ikoną w obszarze powiadomień.

[CustomMessages]
AppComments=Animacje okien w stylu macOS dla Windows 11
GroupStartup=Uruchamianie:
TaskAutostart=Uruchamiaj MacWin po zalogowaniu
RegisteringTask=Rejestrowanie zadania uruchamiania MacWin…
LaunchMacWin=Uruchom MacWin
OpenSettings=Otwórz ustawienia
ShortcutComment=Ustawienia animacji okien
DotNetDownloading=Pobieranie .NET 10 Desktop Runtime od Microsoftu…
DotNetInstalling=Instalowanie .NET 10 Desktop Runtime…
DotNetFailed=Nie udało się zainstalować .NET 10 Desktop Runtime (%1). Animacje działają i bez niego, ale okno ustawień go potrzebuje. Możesz go pobrać później ze strony:%n%n%2
Arm64NotSupported=MacWin wymaga 64-bitowego procesora Intel lub AMD (x64). Windows na ARM nie jest jeszcze obsługiwany.
RemoveSettings=Usunąć też ustawienia i dzienniki MacWin?
UpdatesTitle=Automatyczne aktualizacje
UpdatesSubtitle=Jak MacWin ma być aktualizowany?
UpdatesText=Raz dziennie MacWin pyta GitHub, czy jest nowa wersja. Może ją sam pobrać i zainstalować w tle albo najpierw pokazać ją w oknie ustawień.%n%nMożesz to później zmienić w ustawieniach, w sekcji Ogólne.
UpdatesAuto=Instaluj aktualizacje automatycznie (zalecane)
