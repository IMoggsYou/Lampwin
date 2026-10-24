; MacWin installer texts (Russian). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=Будет установлен [name/ver] — анимация окон в стиле macOS: окна втягиваются в панель задач, как джинн, и вылетают обратно.%n%nMacWin работает в фоне, его значок находится в области уведомлений.

[CustomMessages]
AppComments=Анимация окон в стиле macOS для Windows 11
GroupStartup=Автозапуск:
TaskAutostart=Запускать MacWin при входе в систему
RegisteringTask=Регистрация задачи запуска MacWin…
LaunchMacWin=Запустить MacWin
OpenSettings=Открыть параметры
ShortcutComment=Параметры анимации окон
DotNetDownloading=Скачивание .NET 10 Desktop Runtime с сайта Microsoft…
DotNetInstalling=Установка .NET 10 Desktop Runtime…
DotNetFailed=Не удалось установить .NET 10 Desktop Runtime (%1). Анимация работает и без него, но окну параметров он нужен. Его можно установить позже отсюда:%n%n%2
Arm64NotSupported=Для MacWin нужен 64-разрядный процессор Intel или AMD (x64). Windows на ARM пока не поддерживается.
RemoveSettings=Удалить также параметры и журналы MacWin?
UpdatesTitle=Автоматические обновления
UpdatesSubtitle=Как MacWin будет обновляться?
UpdatesText=Раз в день MacWin спрашивает GitHub, не вышла ли новая версия. Он может сам скачать и установить её в фоне или сначала показать её в окне параметров.%n%nЭто можно изменить позже в параметрах, в разделе «Общие».
UpdatesAuto=Устанавливать обновления автоматически (рекомендуется)
