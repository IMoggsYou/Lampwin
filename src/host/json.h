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
// Pretty-printed (2 spaces), members in their order.
std::wstring Serialize(const Value& v, int indent = 0);

Value Bool(bool b);
Value Number(double n);
Value String(const std::wstring& s);
Value Strings(const std::vector<std::wstring>& items);
Value Object();
// Replaces the member `key` of an object (keeping its place) or appends it.
void Set(Value* object, const wchar_t* key, Value v);

}  // namespace json
