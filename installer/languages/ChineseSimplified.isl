; MacWin installer texts (ChineseSimplified). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=即将安装 [name/ver]——macOS 风格的窗口动画：窗口会像神灯一样落入任务栏，再从中飞出。%n%nMacWin 在后台运行，图标位于通知区域。

[CustomMessages]
AppComments=为 Windows 11 带来 macOS 风格的窗口动画
GroupStartup=启动：
TaskAutostart=登录时启动 MacWin
RegisteringTask=正在注册 MacWin 启动任务…
LaunchMacWin=启动 MacWin
OpenSettings=打开设置
ShortcutComment=窗口动画设置
DotNetDownloading=正在从 Microsoft 下载 .NET 10 桌面运行时…
DotNetInstalling=正在安装 .NET 10 桌面运行时…
DotNetFailed=无法安装 .NET 10 桌面运行时（%1）。动画无需它也能正常工作，但设置窗口需要它。你可以稍后从以下网址获取：%n%n%2
Arm64NotSupported=MacWin 需要 64 位 Intel 或 AMD (x64) 处理器。暂不支持 ARM 版 Windows。
RemoveSettings=是否同时删除 MacWin 的设置和日志？
UpdatesTitle=自动更新
UpdatesSubtitle=MacWin 应如何保持最新？
UpdatesText=MacWin 每天会询问一次 GitHub 是否有新版本。它可以在后台自动下载并安装新版本，也可以先在设置窗口中向你显示。%n%n你之后可以在设置的“通用”中更改此项。
UpdatesAuto=自动安装更新（推荐）
