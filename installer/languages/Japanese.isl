; MacWin installer texts (Japanese). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=[name/ver] をインストールします。macOS 風のウィンドウ アニメーションで、ウィンドウがジニーのようにタスク バーへ吸い込まれ、また飛び出してきます。%n%nMacWin はバックグラウンドで動作し、アイコンは通知領域に表示されます。

[CustomMessages]
AppComments=Windows 11 のための macOS 風ウィンドウ アニメーション
GroupStartup=スタートアップ:
TaskAutostart=サインイン時に MacWin を起動する
RegisteringTask=MacWin の起動タスクを登録しています…
LaunchMacWin=MacWin を起動する
OpenSettings=設定を開く
ShortcutComment=ウィンドウ アニメーションの設定
DotNetDownloading=Microsoft から .NET 10 Desktop Runtime をダウンロードしています…
DotNetInstalling=.NET 10 Desktop Runtime をインストールしています…
DotNetFailed=.NET 10 Desktop Runtime をインストールできませんでした (%1)。アニメーションはなくても動作しますが、設定ウィンドウには必要です。後で次の場所から入手できます:%n%n%2
Arm64NotSupported=MacWin には 64 ビットの Intel または AMD (x64) プロセッサが必要です。ARM 版 Windows にはまだ対応していません。
RemoveSettings=MacWin の設定とログも削除しますか?
