//-------------------------------------------------
//
//          Json.h
//
//  glTF を読むための最小限の JSON パーサ。
//-------------------------------------------------

//
// glTF ローダーからの使い方:
//
//   JsonValue root;
//   ParseJson(text, length, root);
//   int count = root["meshes"].Size();
//   std::string name = root["meshes"][0]["name"].AsString();
//
// 存在しないキーを引いても落ちず、Null 値が返る(IsNull() で判定できる)

#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

class JsonValue
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type        type = Type::Null;
    bool        boolValue = false;
    double      numberValue = 0.0;
    std::string stringValue;

    std::vector<JsonValue>          arrayValue;
    std::map<std::string, JsonValue> objectValue;

    bool IsNull()   const { return type == Type::Null; }
    bool IsArray()  const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    bool Has(const std::string& key) const;

    // 存在しない場合は Null 値への参照を返す(落ちない)
    const JsonValue& operator[](const std::string& key) const;
    const JsonValue& operator[](size_t index) const;

    size_t Size() const;

    int         AsInt(int defaultValue = 0) const;
    float       AsFloat(float defaultValue = 0.0f) const;
    bool        AsBool(bool defaultValue = false) const;
    std::string AsString(const std::string& defaultValue = "") const;
};

bool ParseJson(const char* text, size_t length, JsonValue& out);
