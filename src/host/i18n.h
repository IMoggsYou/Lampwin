// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/Lampwin

#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>
#include <vector>

// The host's texts (tray menu, notifications, messages) from i18n/<code>.json, which
// are embedded in MacWin.exe (host.rc.in). The language rule is the settings app's
// (settings-app/Services/Loc.cs): the "language" setting, or with "auto" the first
// Windows display language MacWin has (any zh-* = Simplified Chinese, any pt-* =
// Brazilian Portuguese), otherwise English.
namespace i18n {

// Every shipped language, in the order of their RCDATA ids (kFirstResource + index).
extern const wchar_t* const kLanguages[];
extern const int kLanguageCount;
const int kFirstResource = 100;

// Pure: the language for a setting and a list of preferred UI languages (BCP 47).
std::wstring Resolve(const std::wstring& setting, const std::vector<std::wstring>& preferred);
// The user's Windows display languages, most preferred first.
std::vector<std::wstring> PreferredUILanguages();

// Loads the strings for a "language" setting (cheap to call again: same language = no-op).
void Init(const std::wstring& setting);
const std::wstring& Language();
bool IsRightToLeft();

// The text for a key ("host.menu.settings"); English if this language lacks it.
const std::wstring& T(const wchar_t* key);
// T with {0} replaced.
std::wstring F(const wchar_t* key, const std::wstring& arg0);
// Every text whose key starts with `prefix`, in this language (English where it has none).
std::unordered_map<std::wstring, std::wstring> All(const wchar_t* prefix);
// The Windows locale that writes dates in this language ("pl-PL").
std::wstring LocaleName();

// MessageBox in the UI language: right-to-left reading for Arabic.
int Message(HWND owner, const std::wstring& text, UINT type);

}  // namespace i18n
