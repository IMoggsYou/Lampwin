; MacWin installer texts (Japanese). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=[name/ver] をインストールします。macOS 風のウィンドウ アニメーションで、ウィンドウがジニーのようにタスク バーへ吸い込まれ、また飛び出してきます。%n%nLampwin はバックグラウンドで動作し、アイコンは通知領域に表示されます。

[CustomMessages]
AppComments=Windows 11 のための macOS 風ウィンドウ アニメーション
GroupStartup=スタートアップ:
TaskAutostart=サインイン時に Lampwin を起動する
RegisteringTask=Lampwin の起動タスクを登録しています…
LaunchMacWin=Lampwin を起動する
OpenSettings=設定を開く
ShortcutComment=ウィンドウ アニメーションの設定
DotNetDownloading=Microsoft から .NET 10 Desktop Runtime をダウンロードしています…
DotNetInstalling=.NET 10 Desktop Runtime をインストールしています…
DotNetFailed=.NET 10 Desktop Runtime をインストールできませんでした (%1)。アニメーションはなくても動作しますが、設定ウィンドウには必要です。後で次の場所から入手できます:%n%n%2
Arm64NotSupported=Lampwin には 64 ビットの Intel または AMD (x64) プロセッサが必要です。ARM 版 Windows にはまだ対応していません。
RemoveSettings=Lampwin の設定とログも削除しますか?
UpdatesTitle=自動アップデート
UpdatesSubtitle=Lampwin を最新の状態に保つ方法を選んでください。
UpdatesText=Lampwin は 1 日に 1 回、新しいバージョンがあるかを GitHub に問い合わせます。バックグラウンドで自動的にダウンロードしてインストールするか、先に設定ウィンドウで知らせるかを選べます。%n%nこれは後で設定の [一般] で変更できます。
UpdatesAuto=アップデートを自動的にインストールする (推奨)
