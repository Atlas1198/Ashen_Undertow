//----------------------------------------------------------------------------
// Json.cpp
// 再帰下降による JSON パーサ。glTF を読むのに必要な範囲をカバーする。
//----------------------------------------------------------------------------
#include "Json.h"

#include <cstdlib>
#include <cstring>

namespace
{
    // 存在しないキーを引いたときに返す共有の Null 値
    const JsonValue g_nullValue;

    struct Parser
    {
        const char* p   = nullptr;
        const char* end = nullptr;

        void SkipWhitespace()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                ++p;
        }

        bool ParseValue(JsonValue& out);
        bool ParseString(std::string& out);
        bool ParseNumber(JsonValue& out);
        bool ParseObject(JsonValue& out);
        bool ParseArray(JsonValue& out);
    };

    // コードポイントを UTF-8 として追記する
    void AppendUtf8(std::string& s, uint32_t cp)
    {
        if (cp < 0x80)
        {
            s.push_back((char)cp);
        }
        else if (cp < 0x800)
        {
            s.push_back((char)(0xC0 | (cp >> 6)));
            s.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            s.push_back((char)(0xE0 | (cp >> 12)));
            s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else
        {
            s.push_back((char)(0xF0 | (cp >> 18)));
            s.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    bool ReadHex4(const char*& p, const char* end, uint32_t& out)
    {
        if (end - p < 4) return false;
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = p[i];
            uint32_t d;
            if      (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
            else return false;
            out = (out << 4) | d;
        }
        p += 4;
        return true;
    }

    bool Parser::ParseString(std::string& out)
    {
        if (p >= end || *p != '"') return false;
        ++p;

        out.clear();
        while (p < end)
        {
            const char c = *p++;
            if (c == '"') return true;

            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }

            if (p >= end) return false;
            const char esc = *p++;
            switch (esc)
            {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u':
            {
                uint32_t cp = 0;
                if (!ReadHex4(p, end, cp)) return false;
                // サロゲートペア
                if (cp >= 0xD800 && cp <= 0xDBFF && (end - p) >= 6 &&
                    p[0] == '\\' && p[1] == 'u')
                {
                    const char* save = p;
                    p += 2;
                    uint32_t low = 0;
                    if (ReadHex4(p, end, low) && low >= 0xDC00 && low <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    else
                        p = save;
                }
                AppendUtf8(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool Parser::ParseNumber(JsonValue& out)
    {
        char* endPtr = nullptr;
        const double v = strtod(p, &endPtr);
        if (endPtr == p) return false;
        p = endPtr;

        out.type = JsonValue::Type::Number;
        out.numberValue = v;
        return true;
    }

    bool Parser::ParseArray(JsonValue& out)
    {
        if (p >= end || *p != '[') return false;
        ++p;

        out.type = JsonValue::Type::Array;
        out.arrayValue.clear();

        SkipWhitespace();
        if (p < end && *p == ']') { ++p; return true; }

        for (;;)
        {
            JsonValue element;
            if (!ParseValue(element)) return false;
            out.arrayValue.push_back(std::move(element));

            SkipWhitespace();
            if (p >= end) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return true; }
            return false;
        }
    }

    bool Parser::ParseObject(JsonValue& out)
    {
        if (p >= end || *p != '{') return false;
        ++p;

        out.type = JsonValue::Type::Object;
        out.objectValue.clear();

        SkipWhitespace();
        if (p < end && *p == '}') { ++p; return true; }

        for (;;)
        {
            SkipWhitespace();

            std::string key;
            if (!ParseString(key)) return false;

            SkipWhitespace();
            if (p >= end || *p != ':') return false;
            ++p;

            JsonValue value;
            if (!ParseValue(value)) return false;
            out.objectValue[key] = std::move(value);

            SkipWhitespace();
            if (p >= end) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return false;
        }
    }

    bool Parser::ParseValue(JsonValue& out)
    {
        SkipWhitespace();
        if (p >= end) return false;

        switch (*p)
        {
        case '{': return ParseObject(out);
        case '[': return ParseArray(out);
        case '"':
            out.type = JsonValue::Type::String;
            return ParseString(out.stringValue);
        case 't':
            if (end - p >= 4 && strncmp(p, "true", 4) == 0)
            {
                p += 4;
                out.type = JsonValue::Type::Bool;
                out.boolValue = true;
                return true;
            }
            return false;
        case 'f':
            if (end - p >= 5 && strncmp(p, "false", 5) == 0)
            {
                p += 5;
                out.type = JsonValue::Type::Bool;
                out.boolValue = false;
                return true;
            }
            return false;
        case 'n':
            if (end - p >= 4 && strncmp(p, "null", 4) == 0)
            {
                p += 4;
                out.type = JsonValue::Type::Null;
                return true;
            }
            return false;
        default:
            return ParseNumber(out);
        }
    }
}

//----------------------------------------------------------------------------
// JsonValue のアクセサ
//----------------------------------------------------------------------------
bool JsonValue::Has(const std::string& key) const
{
    return type == Type::Object && objectValue.find(key) != objectValue.end();
}

const JsonValue& JsonValue::operator[](const std::string& key) const
{
    if (type != Type::Object) return g_nullValue;
    auto it = objectValue.find(key);
    return (it == objectValue.end()) ? g_nullValue : it->second;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    if (type != Type::Array || index >= arrayValue.size()) return g_nullValue;
    return arrayValue[index];
}

size_t JsonValue::Size() const
{
    if (type == Type::Array)  return arrayValue.size();
    if (type == Type::Object) return objectValue.size();
    return 0;
}

int JsonValue::AsInt(int defaultValue) const
{
    return (type == Type::Number) ? (int)numberValue : defaultValue;
}

float JsonValue::AsFloat(float defaultValue) const
{
    return (type == Type::Number) ? (float)numberValue : defaultValue;
}

bool JsonValue::AsBool(bool defaultValue) const
{
    return (type == Type::Bool) ? boolValue : defaultValue;
}

std::string JsonValue::AsString(const std::string& defaultValue) const
{
    return (type == Type::String) ? stringValue : defaultValue;
}

//----------------------------------------------------------------------------
bool ParseJson(const char* text, size_t length, JsonValue& out)
{
    Parser parser;
    parser.p   = text;
    parser.end = text + length;

    // UTF-8 BOM を読み飛ばす
    if (length >= 3 &&
        (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF)
    {
        parser.p += 3;
    }

    if (!parser.ParseValue(out)) return false;
    parser.SkipWhitespace();
    return true;
}
