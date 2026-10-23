#include "host/i18n.h"
#include <wctype.h>
#include <unordered_map>
#include "host/json.h"
#include "host/log.h"

namespace i18n {

const wchar_t* const kLanguages[] = {L"en", L"pl", L"zh-Hans", L"hi", L"es", L"fr",
                                     L"ar", L"bn", L"ru", L"pt-BR", L"ja"};
const int kLanguageCount = ARRAYSIZE(kLanguages);

namespace {

typedef std::unordered_map<std::wstring, std::wstring> Strings;

std::wstring g_language;
Strings g_english;
Strings g_strings;
const std::wstring g_empty;

int IndexOf(const std::wstring& code) {
    for (int i = 0; i < kLanguageCount; ++i) {
        if (_wcsicmp(code.c_str(), kLanguages[i]) == 0) return i;
    }
    return -1;
}

// The host's own keys ("host.*") of one embedded language file.
Strings LoadLanguage(int index) {
    Strings out;
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(kFirstResource + index), RT_RCDATA);
    HGLOBAL data = res ? LoadResource(self, res) : nullptr;
    const char* bytes = data ? (const char*)LockResource(data) : nullptr;
    DWORD size = res ? SizeofResource(self, res) : 0;
    if (!bytes || !size) {
        Log(L"i18n: no strings for %ls", kLanguages[index]);
        return out;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes, (int)size, nullptr, 0);
    std::wstring text((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes, (int)size, &text[0], n);
    json::Value root;
    if (!json::Parse(text, &root) || root.type != json::Value::Object) {
        Log(L"i18n: %ls.json is not valid JSON", kLanguages[index]);
        return out;
    }
    for (const auto& m : root.members) {
        if (m.first.compare(0, 5, L"host.") == 0 && m.second.type == json::Value::String) out[m.first] = m.second.str;
    }
    return out;
}

}  // namespace

std::wstring Resolve(const std::wstring& setting, const std::vector<std::wstring>& preferred) {
    if (!setting.empty() && _wcsicmp(setting.c_str(), L"auto") != 0) {
        int i = IndexOf(setting);
        if (i >= 0) return kLanguages[i];
    }
    for (const std::wstring& tag : preferred) {
        std::wstring primary;
        for (wchar_t c : tag) {
            if (c == L'-' || c == L'_') break;
            primary.push_back((wchar_t)towlower(c));
        }
        if (primary == L"zh") return L"zh-Hans";
        if (primary == L"pt") return L"pt-BR";
        int i = IndexOf(primary);
        if (i >= 0) return kLanguages[i];
    }
    return L"en";
}

std::vector<std::wstring> PreferredUILanguages() {
    std::vector<std::wstring> out;
    ULONG count = 0, size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0) {
        std::wstring buffer(size, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, &buffer[0], &size)) {
            for (const wchar_t* p = buffer.c_str(); *p; p += wcslen(p) + 1) out.emplace_back(p);
        }
    }
    if (out.empty()) {
        wchar_t name[LOCALE_NAME_MAX_LENGTH];
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) out.emplace_back(name);
    }
    return out;
}

void Init(const std::wstring& setting) {
    std::wstring language = Resolve(setting, PreferredUILanguages());
    if (language == g_language) return;
    if (g_english.empty()) g_english = LoadLanguage(0);
    g_language = language;
    int index = IndexOf(language);
    g_strings = index > 0 ? LoadLanguage(index) : Strings();
    Log(L"i18n: %ls (setting \"%ls\")", g_language.c_str(), setting.c_str());
}

const std::wstring& Language() {
    return g_language;
}

bool IsRightToLeft() {
    return g_language == L"ar";
}

const std::wstring& T(const wchar_t* key) {
    auto it = g_strings.find(key);
    if (it != g_strings.end()) return it->second;
    it = g_english.find(key);
    return it != g_english.end() ? it->second : g_empty;
}

std::wstring F(const wchar_t* key, const std::wstring& arg0) {
    std::wstring text = T(key);
    size_t at = text.find(L"{0}");
    if (at != std::wstring::npos) text.replace(at, 3, arg0);
    return text;
}

int Message(HWND owner, const std::wstring& text, UINT type) {
    if (IsRightToLeft()) type |= MB_RTLREADING | MB_RIGHT;
    return MessageBoxW(owner, text.c_str(), L"MacWin", type);
}

}  // namespace i18n
