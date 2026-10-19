// Minimal JSON reader for SimCore data files. No external dependencies.
#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sim {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json parse(const std::string& text);
    static Json parseFile(const std::string& path);
    static Json allOf(const Json& a, const Json& b);  // both must hold (an array of conditions); a null side drops out

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const;
    double asNumber() const;
    int asInt() const { return static_cast<int>(asNumber()); }
    const std::string& asString() const;
    const std::vector<Json>& asArray() const;
    const std::map<std::string, Json>& asObject() const;

    bool has(const std::string& key) const;
    // Returns a Null value when the key is missing.
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t index) const { return asArray().at(index); }
    size_t size() const;

    double numberOr(const std::string& key, double fallback) const;
    std::string stringOr(const std::string& key, const std::string& fallback) const;

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;
};

struct JsonError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

}  // namespace sim
