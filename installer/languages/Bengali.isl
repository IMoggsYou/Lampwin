; MacWin installer texts (Bengali). Loaded after Inno Setup's own messages for this
; language (see installer/MacWin.iss). Keep the [CustomMessages] keys the same in
; every file: tools/check_i18n.cmake checks them.

[Messages]
WelcomeLabel2=এটি [name/ver] ইনস্টল করবে – macOS-ধাঁচের উইন্ডো অ্যানিমেশন: উইন্ডো জিনির মতো টাস্কবারে ঢুকে যায় এবং আবার উড়ে বেরিয়ে আসে।%n%nLampwin পটভূমিতে চলে, আর এর আইকন নোটিফিকেশন এলাকায় থাকে।
; Inno Setup 6 messages the older base translation (languages/base) does not have
; yet - the ones this installer can show.
DiskSpaceGBLabel=কমপক্ষে [gb] GB খালি ডিস্ক জায়গা প্রয়োজন।
DownloadingLabel2=ফাইল ডাউনলোড হচ্ছে...
ButtonStopDownload=ডাউনলোড &থামান
StopDownload=আপনি কি নিশ্চিত যে ডাউনলোড থামাতে চান?
ErrorDownloadAborted=ডাউনলোড বাতিল করা হয়েছে
ErrorDownloadFailed=ডাউনলোড ব্যর্থ হয়েছে: %1 %2
ErrorDownloadSizeFailed=আকার জানা যায়নি: %1 %2
ErrorProgress=অবৈধ অগ্রগতি: %2-এর মধ্যে %1
ErrorFileSize=অবৈধ ফাইলের আকার: প্রত্যাশিত %1, পাওয়া গেছে %2
ErrorDownloading=একটি ফাইল ডাউনলোড করার সময় একটি ত্রুটি হয়েছে:
StatusDownloadFiles=ফাইল ডাউনলোড হচ্ছে...
PrepareToInstallNeedsRestart=সেটআপকে আপনার কম্পিউটার আবার চালু করতে হবে। কম্পিউটার আবার চালু হওয়ার পর [name]-এর ইনস্টলেশন শেষ করতে সেটআপ আবার চালান।%n%nআপনি কি এখনই আবার চালু করতে চান?
AbortRetryIgnoreSelectAction=কাজ বেছে নিন
AbortRetryIgnoreRetry=&আবার চেষ্টা করুন
AbortRetryIgnoreIgnore=ত্রুটি &উপেক্ষা করে চালিয়ে যান
AbortRetryIgnoreCancel=ইনস্টলেশন বাতিল করুন
RetryCancelSelectAction=কাজ বেছে নিন
RetryCancelRetry=&আবার চেষ্টা করুন
RetryCancelCancel=বাতিল
FileAbortRetryIgnoreSkipNotRecommended=এই ফাইলটি &এড়িয়ে যান (প্রস্তাবিত নয়)
FileAbortRetryIgnoreIgnoreNotRecommended=ত্রুটি &উপেক্ষা করে চালিয়ে যান (প্রস্তাবিত নয়)
ExistingFileReadOnly2=বিদ্যমান ফাইলটি প্রতিস্থাপন করা যায়নি, কারণ এটি শুধু-পঠনযোগ্য হিসেবে চিহ্নিত।
ExistingFileReadOnlyRetry=শুধু-পঠনযোগ্য বৈশিষ্ট্য &সরিয়ে আবার চেষ্টা করুন
ExistingFileReadOnlyKeepExisting=বিদ্যমান ফাইলটি &রাখুন
FileExistsSelectAction=কাজ বেছে নিন
FileExists2=ফাইলটি আগে থেকেই আছে।
FileExistsOverwriteExisting=বিদ্যমান ফাইলটি &প্রতিস্থাপন করুন
FileExistsKeepExisting=বিদ্যমান ফাইলটি &রাখুন
FileExistsOverwriteOrKeepAll=পরবর্তী দ্বন্দ্বগুলোর জন্যও &এটাই করুন
ExistingFileNewerSelectAction=কাজ বেছে নিন
ExistingFileNewer2=সেটআপ যে ফাইলটি ইনস্টল করার চেষ্টা করছে, বিদ্যমান ফাইলটি তার চেয়ে নতুন।
ExistingFileNewerOverwriteExisting=বিদ্যমান ফাইলটি &প্রতিস্থাপন করুন
ExistingFileNewerKeepExisting=বিদ্যমান ফাইলটি &রাখুন (প্রস্তাবিত)
ExistingFileNewerOverwriteOrKeepAll=পরবর্তী দ্বন্দ্বগুলোর জন্যও &এটাই করুন

[CustomMessages]
AppComments=Windows 11-এর জন্য macOS-ধাঁচের উইন্ডো অ্যানিমেশন
GroupStartup=স্টার্টআপ:
TaskAutostart=সাইন ইন করলে Lampwin চালু করুন
RegisteringTask=Lampwin-এর স্টার্টআপ টাস্ক নিবন্ধন করা হচ্ছে…
LaunchMacWin=Lampwin চালু করুন
OpenSettings=সেটিংস খুলুন
ShortcutComment=উইন্ডো অ্যানিমেশন সেটিংস
DotNetDownloading=Microsoft থেকে .NET 10 ডেস্কটপ রানটাইম ডাউনলোড করা হচ্ছে…
DotNetInstalling=.NET 10 ডেস্কটপ রানটাইম ইনস্টল করা হচ্ছে…
DotNetFailed=.NET 10 ডেস্কটপ রানটাইম ইনস্টল করা যায়নি (%1)। অ্যানিমেশন এটি ছাড়াই কাজ করে, কিন্তু সেটিংস উইন্ডোর এটি দরকার। আপনি পরে এখান থেকে এটি পেতে পারেন:%n%n%2
Arm64NotSupported=Lampwin-এর জন্য 64-বিট Intel বা AMD (x64) প্রসেসর দরকার। ARM-এ Windows এখনো সমর্থিত নয়।
RemoveSettings=Lampwin-এর সেটিংস ও লগও কি মুছে ফেলবেন?
UpdatesTitle=স্বয়ংক্রিয় আপডেট
UpdatesSubtitle=Lampwin কীভাবে হালনাগাদ থাকবে?
UpdatesText=দিনে একবার Lampwin GitHub-কে জিজ্ঞাসা করে নতুন সংস্করণ এসেছে কি না। এটি নিজেই পটভূমিতে সেটি ডাউনলোড ও ইনস্টল করতে পারে, অথবা প্রথমে সেটিংস উইন্ডোতে আপনাকে দেখাতে পারে।%n%nপরে সেটিংসে, সাধারণ অংশে এটি বদলাতে পারবেন।
UpdatesAuto=স্বয়ংক্রিয়ভাবে আপডেট ইনস্টল করুন (প্রস্তাবিত)
