; MacWin installer texts (Spanish). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=Se instalará [name/ver]: animaciones de ventanas al estilo de macOS. Las ventanas caen en la barra de tareas como un genio y vuelven a salir volando.%n%nMacWin se ejecuta en segundo plano, con un icono en el área de notificación.

[CustomMessages]
AppComments=Animaciones de ventanas al estilo de macOS para Windows 11
GroupStartup=Inicio:
TaskAutostart=Iniciar MacWin al iniciar sesión
RegisteringTask=Registrando la tarea de inicio de MacWin…
LaunchMacWin=Iniciar MacWin
OpenSettings=Abrir la configuración
ShortcutComment=Configuración de las animaciones de ventanas
DotNetDownloading=Descargando .NET 10 Desktop Runtime de Microsoft…
DotNetInstalling=Instalando .NET 10 Desktop Runtime…
DotNetFailed=No se pudo instalar .NET 10 Desktop Runtime (%1). Las animaciones funcionan sin él, pero la ventana de configuración lo necesita. Puedes descargarlo más tarde desde:%n%n%2
Arm64NotSupported=MacWin necesita un procesador Intel o AMD de 64 bits (x64). Windows en ARM todavía no es compatible.
RemoveSettings=¿Quitar también la configuración y los registros de MacWin?
UpdatesTitle=Actualizaciones automáticas
UpdatesSubtitle=¿Cómo debe mantenerse al día MacWin?
UpdatesText=Una vez al día MacWin pregunta a GitHub si hay una versión nueva. Puede descargarla e instalarla solo en segundo plano, o mostrártela primero en la ventana de configuración.%n%nPuedes cambiarlo más tarde en la configuración, en General.
UpdatesAuto=Instalar actualizaciones automáticamente (recomendado)
