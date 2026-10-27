; MacWin installer texts (English). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=This will install [name/ver] – macOS-style window animations: windows fall into the taskbar like a genie and fly back out.%n%nLampwin runs in the background with an icon in the notification area.

[CustomMessages]
AppComments=macOS-style window animations for Windows 11
GroupStartup=Startup:
TaskAutostart=Start Lampwin when I sign in
RegisteringTask=Registering Lampwin's startup task…
LaunchMacWin=Start Lampwin
OpenSettings=Open the settings
ShortcutComment=Window animation settings
DotNetDownloading=Downloading the .NET 10 Desktop Runtime from Microsoft…
DotNetInstalling=Installing the .NET 10 Desktop Runtime…
DotNetFailed=The .NET 10 Desktop Runtime could not be installed (%1). The animations work without it, but the settings window needs it. You can get it later from:%n%n%2
Arm64NotSupported=Lampwin needs a 64-bit Intel or AMD (x64) processor. Windows on ARM is not supported yet.
RemoveSettings=Also remove your Lampwin settings and logs?
UpdatesTitle=Automatic updates
UpdatesSubtitle=How should Lampwin stay up to date?
UpdatesText=Once a day Lampwin asks GitHub whether a new version is out. It can download and install it by itself in the background, or show it to you in the settings window first.%n%nYou can change this later in the settings, under General.
UpdatesAuto=Install updates automatically (recommended)
