; MacWin installer texts (BrazilianPortuguese). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=Será instalado o [name/ver]: animações de janelas no estilo do macOS. As janelas caem na barra de tarefas como um gênio e saem voando de volta.%n%nO Lampwin é executado em segundo plano, com um ícone na área de notificação.

[CustomMessages]
AppComments=Animações de janelas no estilo do macOS para o Windows 11
GroupStartup=Inicialização:
TaskAutostart=Iniciar o Lampwin ao entrar
RegisteringTask=Registrando a tarefa de inicialização do Lampwin…
LaunchMacWin=Iniciar o Lampwin
OpenSettings=Abrir as configurações
ShortcutComment=Configurações das animações de janelas
DotNetDownloading=Baixando o .NET 10 Desktop Runtime da Microsoft…
DotNetInstalling=Instalando o .NET 10 Desktop Runtime…
DotNetFailed=Não foi possível instalar o .NET 10 Desktop Runtime (%1). As animações funcionam sem ele, mas a janela de configurações precisa dele. Você pode baixá-lo depois em:%n%n%2
Arm64NotSupported=O Lampwin precisa de um processador Intel ou AMD de 64 bits (x64). O Windows em ARM ainda não é compatível.
RemoveSettings=Remover também as configurações e os logs do Lampwin?
UpdatesTitle=Atualizações automáticas
UpdatesSubtitle=Como o Lampwin deve se manter atualizado?
UpdatesText=Uma vez por dia o Lampwin pergunta ao GitHub se há uma nova versão. Ele pode baixá-la e instalá-la sozinho em segundo plano, ou mostrá-la primeiro na janela de configurações.%n%nVocê pode mudar isso depois nas configurações, em Geral.
UpdatesAuto=Instalar atualizações automaticamente (recomendado)
