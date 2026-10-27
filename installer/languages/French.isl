; MacWin installer texts (French). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=[name/ver] va être installé : des animations de fenêtres façon macOS. Les fenêtres tombent dans la barre des tâches comme un génie et en ressortent en s’envolant.%n%nLampwin fonctionne en arrière-plan, avec une icône dans la zone de notification.

[CustomMessages]
AppComments=Des animations de fenêtres façon macOS pour Windows 11
GroupStartup=Démarrage :
TaskAutostart=Lancer Lampwin à l’ouverture de session
RegisteringTask=Enregistrement de la tâche de démarrage de Lampwin…
LaunchMacWin=Lancer Lampwin
OpenSettings=Ouvrir les paramètres
ShortcutComment=Paramètres des animations de fenêtres
DotNetDownloading=Téléchargement de .NET 10 Desktop Runtime depuis Microsoft…
DotNetInstalling=Installation de .NET 10 Desktop Runtime…
DotNetFailed=Impossible d’installer .NET 10 Desktop Runtime (%1). Les animations fonctionnent sans lui, mais la fenêtre des paramètres en a besoin. Vous pourrez l’obtenir plus tard ici :%n%n%2
Arm64NotSupported=Lampwin nécessite un processeur Intel ou AMD 64 bits (x64). Windows sur ARM n’est pas encore pris en charge.
RemoveSettings=Supprimer aussi les paramètres et les journaux de Lampwin ?
UpdatesTitle=Mises à jour automatiques
UpdatesSubtitle=Comment Lampwin doit-il rester à jour ?
UpdatesText=Une fois par jour, Lampwin demande à GitHub si une nouvelle version est sortie. Il peut la télécharger et l’installer tout seul en arrière-plan, ou d’abord vous la montrer dans la fenêtre des paramètres.%n%nVous pourrez changer cela plus tard dans les paramètres, sous Général.
UpdatesAuto=Installer automatiquement les mises à jour (recommandé)
