// Unit tests for the pure parts of MacWin (no framework: CTest runs this exe, a
// non-zero exit code is a failure). Build: cmake --build --preset x64; run: ctest --test-dir build/x64

#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "common/strutil.h"
#include "host/i18n.h"
#include "host/json.h"
#include "host/updater.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

void TestVersions() {
    CHECK(CompareVersions(L"1.0.0", L"1.0.0") == 0);
    CHECK(CompareVersions(L"v1.0.0", L"1.0.0") == 0);
    CHECK(CompareVersions(L"1.0.1", L"1.0.0") > 0);
    CHECK(CompareVersions(L"1.10.0", L"1.9.9") > 0);
    CHECK(CompareVersions(L"2.0", L"1.99.99") > 0);
    CHECK(CompareVersions(L"1.2.0-beta", L"1.2.0") < 0);
    CHECK(CompareVersions(L"1.2.0", L"1.2.0-rc.1") > 0);
    CHECK(CompareVersions(L"0.9.9", L"1.0.0") < 0);
}

const wchar_t* kRelease = LR"({
  "tag_name": "v1.2.0", "html_url": "https://github.com/KosaWLeb/MacWin/releases/tag/v1.2.0",
  "draft": false, "prerelease": false, "body": "notes",
  "assets": [
    {"name": "MacWinSetup-1.2.0.exe",
     "browser_download_url": "https://github.com/KosaWLeb/MacWin/releases/download/v1.2.0/MacWinSetup-1.2.0.exe"},
    {"name": "SHA256SUMS.txt",
     "browser_download_url": "https://github.com/KosaWLeb/MacWin/releases/download/v1.2.0/SHA256SUMS.txt"}
  ]
})";

void TestRelease() {
    Release r;
    CHECK(ParseLatestRelease(kRelease, &r));
    CHECK(r.version == L"1.2.0");
    CHECK(r.installerName == L"MacWinSetup-1.2.0.exe");
    CHECK(r.notesUrl == L"https://github.com/KosaWLeb/MacWin/releases/tag/v1.2.0");
    CHECK(r.sumsUrl.find(L"SHA256SUMS.txt") != std::wstring::npos);

    std::wstring draft = kRelease;
    draft.replace(draft.find(L"\"draft\": false"), 14, L"\"draft\": true ");
    CHECK(!ParseLatestRelease(draft, &r));

    // Assets from anywhere else are never used.
    std::wstring foreign = kRelease;
    size_t at = foreign.find(L"https://github.com/KosaWLeb/MacWin/releases/download/v1.2.0/MacWinSetup");
    foreign.replace(at, 19, L"https://evil.example");
    CHECK(!ParseLatestRelease(foreign, &r));

    std::wstring badName = kRelease;
    badName.replace(badName.find(L"\"MacWinSetup-1.2.0.exe\""), 23, L"\"..\\\\MacWinSetup.exe\"");
    CHECK(!ParseLatestRelease(badName, &r));

    CHECK(!ParseLatestRelease(L"{\"message\": \"Not Found\"}", &r));
    CHECK(!ParseLatestRelease(L"not json", &r));
}

void TestSums() {
    std::string sums =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  MacWinSetup-1.2.0.exe\r\n"
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff *Other.zip\n";
    CHECK(FindSha256(sums, L"MacWinSetup-1.2.0.exe") == L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    CHECK(FindSha256(sums, L"other.zip") == L"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    CHECK(FindSha256(sums, L"Missing.exe").empty());
    CHECK(FindSha256("zz  MacWinSetup-1.2.0.exe\n", L"MacWinSetup-1.2.0.exe").empty());
}

void TestHosts() {
    CHECK(IsTrustedDownloadHost(L"github.com"));
    CHECK(IsTrustedDownloadHost(L"api.github.com"));
    CHECK(IsTrustedDownloadHost(L"objects.githubusercontent.com"));
    CHECK(IsTrustedDownloadHost(L"release-assets.githubusercontent.com"));
    CHECK(!IsTrustedDownloadHost(L"githubusercontent.com.evil.com"));
    CHECK(!IsTrustedDownloadHost(L"evilgithub.com"));
    CHECK(!IsTrustedDownloadHost(L"example.com"));
}

void TestLanguages() {
    using i18n::Resolve;
    CHECK(Resolve(L"auto", {L"pl-PL"}) == L"pl");
    CHECK(Resolve(L"auto", {L"de-DE", L"fr-FR"}) == L"fr");
    CHECK(Resolve(L"auto", {L"de-DE", L"it-IT"}) == L"en");
    CHECK(Resolve(L"auto", {L"zh-TW"}) == L"zh-Hans");
    CHECK(Resolve(L"auto", {L"zh-Hans-CN"}) == L"zh-Hans");
    CHECK(Resolve(L"auto", {L"pt-PT"}) == L"pt-BR");
    CHECK(Resolve(L"auto", {L"ar-SA"}) == L"ar");
    CHECK(Resolve(L"auto", {L"bn-IN"}) == L"bn");
    CHECK(Resolve(L"auto", {L"hi-IN"}) == L"hi");
    CHECK(Resolve(L"auto", {L"ja-JP"}) == L"ja");
    CHECK(Resolve(L"auto", {L"ru-RU"}) == L"ru");
    CHECK(Resolve(L"auto", {L"es-MX"}) == L"es");
    CHECK(Resolve(L"auto", {L"en-GB"}) == L"en");
    CHECK(Resolve(L"auto", {}) == L"en");
    CHECK(Resolve(L"ja", {L"pl-PL"}) == L"ja");
    CHECK(Resolve(L"PT-br", {L"pl-PL"}) == L"pt-BR");
    CHECK(Resolve(L"xx", {L"es-ES"}) == L"es");
    CHECK(Resolve(L"", {L"ru-RU"}) == L"ru");
}

bool Matches(std::vector<std::wstring> entries, const wchar_t* path) {
    std::wstring list;
    for (const std::wstring& e : entries) list += e + L'\0';
    list += L'\0';
    list += L'\0';
    mw::ImageNames names;
    mw::MakeImageNames(path, &names);
    return mw::ListMatches(list.c_str(), names);
}

void TestProgramLists() {
    CHECK(Matches({L"notepad.exe"}, L"C:\\Windows\\notepad.exe"));
    CHECK(Matches({L"notepad"}, L"C:\\Windows\\NOTEPAD.EXE"));
    CHECK(Matches({L"c:\\windows\\notepad.exe"}, L"C:\\Windows\\notepad.exe"));
    CHECK(!Matches({L"note"}, L"C:\\Windows\\notepad.exe"));
    CHECK(Matches({L"\\steamapps\\common\\"}, L"D:\\Steam\\steamapps\\common\\Game\\game.exe"));
    CHECK(!Matches({L"\\steamapps\\common\\"}, L"C:\\Program Files\\Steam\\steam.exe"));
    CHECK(Matches({L"\\epic games\\"}, L"C:\\Program Files\\Epic Games\\Fortnite\\x.exe"));
    // A full path to an exe is an exact entry, not a folder pattern.
    CHECK(!Matches({L"c:\\games\\a.exe"}, L"C:\\Games\\a.exe.backup\\b.exe"));
}

void TestJson() {
    json::Value root;
    CHECK(json::Parse(L"{\"future\": {\"x\": [1, 2.5, \"s\"]}, \"enabled\": true, \"name\": \"a\\\"b\\\\c\\u00e9\"}", &root));
    json::Set(&root, L"enabled", json::Bool(false));
    json::Set(&root, L"language", json::String(L"pl"));
    std::wstring text = json::Serialize(root);
    json::Value again;
    CHECK(json::Parse(text, &again));
    CHECK(again.GetBool(L"enabled", true) == false);
    const json::Value* future = again.Get(L"future");
    CHECK(future && future->type == json::Value::Object && future->Get(L"x") && future->Get(L"x")->items.size() == 3);
    CHECK(again.Get(L"name") && again.Get(L"name")->str == L"a\"b\\c\u00e9");
    CHECK(again.Get(L"language") && again.Get(L"language")->str == L"pl");
    CHECK(again.members.front().first == L"future");   // order kept
}

}  // namespace

int wmain() {
    TestVersions();
    TestRelease();
    TestSums();
    TestHosts();
    TestLanguages();
    TestProgramLists();
    TestJson();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all unit tests passed\n");
    return 0;
}
