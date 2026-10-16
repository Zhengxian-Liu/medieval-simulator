#include "simcore/json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sim {

namespace {
const Json kNull;
}

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    Json parseRoot() {
        Json v = parseValue();
        skipWs();
        if (pos_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& what) {
        size_t line = 1;
        for (size_t i = 0; i < pos_ && i < s_.size(); ++i)
            if (s_[i] == '\n') ++line;
        throw JsonError("JSON parse error at line " + std::to_string(line) + ": " + what);
    }

    void skipWs() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/') {
                // Allow // comments in data files.
                while (pos_ < s_.size() && s_[pos_] != '\n') ++pos_;
            } else {
                break;
            }
        }
    }

    char peek() {
        skipWs();
        if (pos_ >= s_.size()) fail("unexpected end");
        return s_[pos_];
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    Json parseValue() {
        char c = peek();
        Json v;
        if (c == '{') {
            ++pos_;
            v.type_ = Json::Type::Object;
            if (peek() == '}') { ++pos_; return v; }
            while (true) {
                if (peek() != '"') fail("expected key");
                std::string key = parseString();
                expect(':');
                v.object_[key] = parseValue();
                char n = peek();
                ++pos_;
                if (n == '}') break;
                if (n != ',') fail("expected ',' or '}'");
                if (peek() == '}') { ++pos_; break; }  // trailing comma
            }
        } else if (c == '[') {
            ++pos_;
            v.type_ = Json::Type::Array;
            if (peek() == ']') { ++pos_; return v; }
            while (true) {
                v.array_.push_back(parseValue());
                char n = peek();
                ++pos_;
                if (n == ']') break;
                if (n != ',') fail("expected ',' or ']'");
                if (peek() == ']') { ++pos_; break; }
            }
        } else if (c == '"') {
            v.type_ = Json::Type::String;
            v.string_ = parseString();
        } else if (s_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            v.type_ = Json::Type::Bool;
            v.bool_ = true;
        } else if (s_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            v.type_ = Json::Type::Bool;
        } else if (s_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
        } else {
            const char* start = s_.c_str() + pos_;
            char* end = nullptr;
            double d = std::strtod(start, &end);
            if (end == start) fail("unexpected character");
            pos_ += static_cast<size_t>(end - start);
            v.type_ = Json::Type::Number;
            v.number_ = d;
        }
        return v;
    }

    void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) fail("unterminated string");
            char c = s_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= s_.size()) fail("bad escape");
                char e = s_[pos_++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (pos_ + 4 > s_.size()) fail("bad unicode escape");
                        unsigned cp = static_cast<unsigned>(std::stoul(s_.substr(pos_, 4), nullptr, 16));
                        pos_ += 4;
                        appendUtf8(out, cp);
                        break;
                    }
                    default: out += e;
                }
            } else {
                out += c;
            }
        }
        return out;
    }
};

Json Json::parse(const std::string& text) { return JsonParser(text).parseRoot(); }

Json Json::parseFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw JsonError("cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        return parse(ss.str());
    } catch (const JsonError& e) {
        throw JsonError(path + ": " + e.what());
    }
}

bool Json::asBool() const {
    if (type_ != Type::Bool) throw JsonError("not a bool");
    return bool_;
}
double Json::asNumber() const {
    if (type_ != Type::Number) throw JsonError("not a number");
    return number_;
}
const std::string& Json::asString() const {
    if (type_ != Type::String) throw JsonError("not a string");
    return string_;
}
const std::vector<Json>& Json::asArray() const {
    static const std::vector<Json> kEmpty;
    if (type_ == Type::Null) return kEmpty;  // a missing list reads as empty
    if (type_ != Type::Array) throw JsonError("not an array");
    return array_;
}
const std::map<std::string, Json>& Json::asObject() const {
    if (type_ != Type::Object) throw JsonError("not an object");
    return object_;
}
bool Json::has(const std::string& key) const {
    return type_ == Type::Object && object_.count(key) > 0;
}
const Json& Json::operator[](const std::string& key) const {
    if (type_ != Type::Object) return kNull;
    auto it = object_.find(key);
    return it == object_.end() ? kNull : it->second;
}
size_t Json::size() const {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}
double Json::numberOr(const std::string& key, double fallback) const {
    const Json& v = (*this)[key];
    return v.isNumber() ? v.number_ : fallback;
}
std::string Json::stringOr(const std::string& key, const std::string& fallback) const {
    const Json& v = (*this)[key];
    return v.isString() ? v.string_ : fallback;
}

}  // namespace sim
