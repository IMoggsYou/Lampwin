; MacWin installer texts (Arabic). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=سيتم تثبيت [name/ver]، وهو حركات نوافذ بأسلوب macOS: تسقط النوافذ في شريط المهام كالمارد ثم تخرج منه طائرةً.%n%nيعمل Lampwin في الخلفية، وتظهر أيقونته في منطقة الإعلامات.

[CustomMessages]
AppComments=حركات نوافذ بأسلوب macOS لنظام Windows ‎11
GroupStartup=بدء التشغيل:
TaskAutostart=تشغيل Lampwin عند تسجيل الدخول
RegisteringTask=جارٍ تسجيل مهمة بدء تشغيل Lampwin…
LaunchMacWin=تشغيل Lampwin
OpenSettings=فتح الإعدادات
ShortcutComment=إعدادات حركات النوافذ
DotNetDownloading=جارٍ تنزيل .NET 10 Desktop Runtime من Microsoft…
DotNetInstalling=جارٍ تثبيت .NET 10 Desktop Runtime…
DotNetFailed=تعذّر تثبيت .NET 10 Desktop Runtime (%1). تعمل الحركات من دونه، لكن نافذة الإعدادات تحتاج إليه. يمكنك الحصول عليه لاحقًا من:%n%n%2
Arm64NotSupported=يحتاج Lampwin إلى معالج Intel أو AMD بنظام 64 بت (x64). نظام Windows على ARM غير مدعوم بعد.
RemoveSettings=هل تريد أيضًا إزالة إعدادات Lampwin وسجلاته؟
UpdatesTitle=التحديثات التلقائية
UpdatesSubtitle=كيف يبقى Lampwin محدّثًا؟
UpdatesText=يسأل Lampwin موقع GitHub مرة واحدة يوميًا عن وجود إصدار جديد. يمكنه تنزيله وتثبيته بنفسه في الخلفية، أو عرضه عليك أولًا في نافذة الإعدادات.%n%nيمكنك تغيير ذلك لاحقًا في الإعدادات، ضمن عام.
UpdatesAuto=تثبيت التحديثات تلقائيًا (مستحسن)
