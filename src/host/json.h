#pragma once
#include <string>
#include <utility>
#include <vector>

// Just enough JSON for settings.json: objects, arrays, strings, numbers, booleans.
namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool boolean = false;
    double number = 0;
    std::wstring str;
    std::vector<Value> items;
    std::vector<std::pair<std::wstring, Value>> members;

    const Value* Get(const wchar_t* key) const;
    bool GetBool(const wchar_t* key, bool fallback) const;
    int GetInt(const wchar_t* key, int fallback) const;
    std::vector<std::wstring> GetStrings(const wchar_t* key, const std::vector<std::wstring>& fallback) const;
};

bool Parse(const std::wstring& text, Value* out);
std::wstring Quote(const std::wstring& s);

}  // namespace json
