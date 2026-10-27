; MacWin installer texts (Hindi). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=यह [name/ver] इंस्टॉल करेगा – macOS शैली के विंडो एनिमेशन: विंडो जिनी की तरह टास्कबार में जाती हैं और वापस उड़कर आती हैं।%n%nLampwin पृष्ठभूमि में चलता है, और इसका आइकन सूचना क्षेत्र में रहता है।
; Inno Setup 6 messages the older base translation (languages/base) does not have
; yet - the ones this installer can show.
DiskSpaceGBLabel=कम से कम [gb] GB खाली डिस्क स्थान आवश्यक है।
DownloadingLabel2=फ़ाइलें डाउनलोड हो रही हैं...
ButtonStopDownload=डाउनलोड &रोकें
StopDownload=क्या आप वाकई डाउनलोड रोकना चाहते हैं?
ErrorDownloadAborted=डाउनलोड रद्द किया गया
ErrorDownloadFailed=डाउनलोड विफल रहा: %1 %2
ErrorDownloadSizeFailed=आकार प्राप्त नहीं हो सका: %1 %2
ErrorProgress=अमान्य प्रगति: %2 में से %1
ErrorFileSize=अमान्य फ़ाइल आकार: अपेक्षित %1, मिला %2
ErrorDownloading=फ़ाइल डाउनलोड करते समय एक त्रुटि हुई:
StatusDownloadFiles=फ़ाइलें डाउनलोड हो रही हैं...
PrepareToInstallNeedsRestart=सेटअप को आपका कंप्यूटर पुनः आरंभ करना होगा। कंप्यूटर पुनः आरंभ होने के बाद [name] की स्थापना पूरी करने के लिए सेटअप फिर से चलाएँ।%n%nक्या आप अभी पुनः आरंभ करना चाहते हैं?
AbortRetryIgnoreSelectAction=कार्रवाई चुनें
AbortRetryIgnoreRetry=&फिर से प्रयास करें
AbortRetryIgnoreIgnore=त्रुटि को &अनदेखा करें और जारी रखें
AbortRetryIgnoreCancel=स्थापना रद्द करें
RetryCancelSelectAction=कार्रवाई चुनें
RetryCancelRetry=&फिर से प्रयास करें
RetryCancelCancel=रद्द करें
FileAbortRetryIgnoreSkipNotRecommended=यह फ़ाइल &छोड़ें (अनुशंसित नहीं)
FileAbortRetryIgnoreIgnoreNotRecommended=त्रुटि को &अनदेखा करें और जारी रखें (अनुशंसित नहीं)
ExistingFileReadOnly2=मौजूदा फ़ाइल को बदला नहीं जा सका, क्योंकि वह केवल-पढ़ने योग्य चिह्नित है।
ExistingFileReadOnlyRetry=केवल-पढ़ने योग्य विशेषता &हटाएँ और फिर से प्रयास करें
ExistingFileReadOnlyKeepExisting=मौजूदा फ़ाइल &रखें
FileExistsSelectAction=कार्रवाई चुनें
FileExists2=फ़ाइल पहले से मौजूद है।
FileExistsOverwriteExisting=मौजूदा फ़ाइल को &अधिलेखित करें
FileExistsKeepExisting=मौजूदा फ़ाइल &रखें
FileExistsOverwriteOrKeepAll=अगले टकरावों के लिए भी &यही करें
ExistingFileNewerSelectAction=कार्रवाई चुनें
ExistingFileNewer2=मौजूदा फ़ाइल उस फ़ाइल से नई है जिसे सेटअप इंस्टॉल करने का प्रयास कर रहा है।
ExistingFileNewerOverwriteExisting=मौजूदा फ़ाइल को &अधिलेखित करें
ExistingFileNewerKeepExisting=मौजूदा फ़ाइल &रखें (अनुशंसित)
ExistingFileNewerOverwriteOrKeepAll=अगले टकरावों के लिए भी &यही करें

[CustomMessages]
AppComments=Windows 11 के लिए macOS शैली के विंडो एनिमेशन
GroupStartup=स्टार्टअप:
TaskAutostart=साइन इन करने पर Lampwin शुरू करें
RegisteringTask=Lampwin का स्टार्टअप कार्य पंजीकृत किया जा रहा है…
LaunchMacWin=Lampwin शुरू करें
OpenSettings=सेटिंग्स खोलें
ShortcutComment=विंडो एनिमेशन सेटिंग्स
DotNetDownloading=Microsoft से .NET 10 डेस्कटॉप रनटाइम डाउनलोड किया जा रहा है…
DotNetInstalling=.NET 10 डेस्कटॉप रनटाइम इंस्टॉल किया जा रहा है…
DotNetFailed=.NET 10 डेस्कटॉप रनटाइम इंस्टॉल नहीं हो सका (%1)। एनिमेशन इसके बिना भी काम करते हैं, लेकिन सेटिंग्स विंडो को इसकी ज़रूरत है। आप इसे बाद में यहाँ से प्राप्त कर सकते हैं:%n%n%2
Arm64NotSupported=Lampwin को 64-बिट Intel या AMD (x64) प्रोसेसर चाहिए। ARM पर Windows अभी समर्थित नहीं है।
RemoveSettings=क्या Lampwin की सेटिंग्स और लॉग भी हटाने हैं?
UpdatesTitle=अपने-आप अपडेट
UpdatesSubtitle=Lampwin को अप-टू-डेट कैसे रखा जाए?
UpdatesText=दिन में एक बार Lampwin GitHub से पूछता है कि क्या कोई नया संस्करण आया है। यह उसे पृष्ठभूमि में अपने-आप डाउनलोड और इंस्टॉल कर सकता है, या पहले सेटिंग्स विंडो में आपको दिखा सकता है।%n%nआप इसे बाद में सेटिंग्स में, सामान्य के अंतर्गत बदल सकते हैं।
UpdatesAuto=अपडेट अपने-आप इंस्टॉल करें (अनुशंसित)
