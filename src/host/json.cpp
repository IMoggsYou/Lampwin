#include "host/json.h"
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>

namespace json {

namespace {

struct Parser {
    const wchar_t* p;
    const wchar_t* end;

    void SkipWs() {
        while (p < end && iswspace(*p)) ++p;
    }

    bool Literal(const wchar_t* word) {
        size_t n = wcslen(word);
        if ((size_t)(end - p) < n || wcsncmp(p, word, n) != 0) return false;
        p += n;
        return true;
    }

    bool ParseString(std::wstring* out) {
        if (p >= end || *p != L'"') return false;
        ++p;
        out->clear();
        while (p < end && *p != L'"') {
            wchar_t c = *p++;
            if (c != L'\\') {
                out->push_back(c);
                continue;
            }
            if (p >= end) return false;
            wchar_t e = *p++;
            switch (e) {
                case L'"': out->push_back(L'"'); break;
                case L'\\': out->push_back(L'\\'); break;
                case L'/': out->push_back(L'/'); break;
                case L'b': out->push_back(L'\b'); break;
                case L'f': out->push_back(L'\f'); break;
                case L'n': out->push_back(L'\n'); break;
                case L'r': out->push_back(L'\r'); break;
                case L't': out->push_back(L'\t'); break;
                case L'u': {
                    if (end - p < 4) return false;
                    wchar_t hex[5] = {p[0], p[1], p[2], p[3], 0};
                    out->push_back((wchar_t)wcstoul(hex, nullptr, 16));
                    p += 4;
                    break;
                }
                default: return false;
            }
        }
        if (p >= end) return false;
        ++p;
        return true;
    }

    bool ParseValue(Value* v, int depth) {
        if (depth > 32) return false;
        SkipWs();
        if (p >= end) return false;
        wchar_t c = *p;
        if (c == L'{') {
            ++p;
            v->type = Value::Object;
            SkipWs();
            if (p < end && *p == L'}') {
                ++p;
                return true;
            }
            for (;;) {
                SkipWs();
                std::wstring key;
                if (!ParseString(&key)) return false;
                SkipWs();
                if (p >= end || *p != L':') return false;
                ++p;
                Value member;
                if (!ParseValue(&member, depth + 1)) return false;
                v->members.emplace_back(std::move(key), std::move(member));
                SkipWs();
                if (p < end && *p == L',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == L'}') {
                    ++p;
                    return true;
                }
                return false;
            }
        }
        if (c == L'[') {
            ++p;
            v->type = Value::Array;
            SkipWs();
            if (p < end && *p == L']') {
                ++p;
                return true;
            }
            for (;;) {
                Value item;
                if (!ParseValue(&item, depth + 1)) return false;
                v->items.push_back(std::move(item));
                SkipWs();
                if (p < end && *p == L',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == L']') {
                    ++p;
                    return true;
                }
                return false;
            }
        }
        if (c == L'"') {
            v->type = Value::String;
            return ParseString(&v->str);
        }
        if (Literal(L"true")) {
            v->type = Value::Bool;
            v->boolean = true;
            return true;
        }
        if (Literal(L"false")) {
            v->type = Value::Bool;
            v->boolean = false;
            return true;
        }
        if (Literal(L"null")) {
            v->type = Value::Null;
            return true;
        }
        wchar_t* stop = nullptr;
        std::wstring num;
        while (p < end && (iswdigit(*p) || *p == L'-' || *p == L'+' || *p == L'.' || *p == L'e' || *p == L'E')) {
            num.push_back(*p++);
        }
        if (num.empty()) return false;
        v->type = Value::Number;
        v->number = wcstod(num.c_str(), &stop);
        return stop && *stop == 0;
    }
};

}  // namespace

const Value* Value::Get(const wchar_t* key) const {
    for (const auto& m : members) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

bool Value::GetBool(const wchar_t* key, bool fallback) const {
    const Value* v = Get(key);
    return (v && v->type == Bool) ? v->boolean : fallback;
}

int Value::GetInt(const wchar_t* key, int fallback) const {
    const Value* v = Get(key);
    return (v && v->type == Number) ? (int)v->number : fallback;
}

std::vector<std::wstring> Value::GetStrings(const wchar_t* key, const std::vector<std::wstring>& fallback) const {
    const Value* v = Get(key);
    if (!v || v->type != Array) return fallback;
    std::vector<std::wstring> out;
    for (const Value& item : v->items) {
        if (item.type == String) out.push_back(item.str);
    }
    return out;
}

bool Parse(const std::wstring& text, Value* out) {
    Parser parser{text.c_str(), text.c_str() + text.size()};
    if (!parser.ParseValue(out, 0)) return false;
    parser.SkipWs();
    return parser.p == parser.end;
}

std::wstring Quote(const std::wstring& s) {
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        switch (c) {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf(buf, 8, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out += L"\"";
    return out;
}

}  // namespace json
