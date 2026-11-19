// =============================================================================
//  mcp/Json — petite bibliothèque JSON (lecture / écriture) pour le serveur MCP.
//  Valeurs : null, booléen, nombre, texte (UTF-8), tableau, objet (ordre gardé).
// =============================================================================
#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace drapeo::mcp {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(int v) : type_(Type::Number), num_(v) {}
    Json(long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(long long v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(std::size_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(float v) : type_(Type::Number), num_(v) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(const char* s) : type_(Type::String), str_(s) {}
    Json(std::string s) : type_(Type::String), str_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool isNull() const { return type_ == Type::Null; }
    [[nodiscard]] bool isBool() const { return type_ == Type::Bool; }
    [[nodiscard]] bool isNumber() const { return type_ == Type::Number; }
    [[nodiscard]] bool isString() const { return type_ == Type::String; }
    [[nodiscard]] bool isArray() const { return type_ == Type::Array; }
    [[nodiscard]] bool isObject() const { return type_ == Type::Object; }

    [[nodiscard]] bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    [[nodiscard]] double asNumber(double def = 0.0) const { return type_ == Type::Number ? num_ : def; }
    [[nodiscard]] const std::string& asString() const { return str_; }

    // Tableau
    void push(Json v) { type_ = Type::Array; arr_.push_back(std::move(v)); }
    [[nodiscard]] std::size_t size() const { return type_ == Type::Array ? arr_.size() : type_ == Type::Object ? obj_.size() : 0; }
    [[nodiscard]] const Json& at(std::size_t i) const { return i < arr_.size() ? arr_[i] : nullValue(); }
    [[nodiscard]] const std::vector<Json>& items() const { return arr_; }

    // Objet
    Json& operator[](const std::string& key)
    {
        type_ = Type::Object;
        for (auto& kv : obj_)
            if (kv.first == key) return kv.second;
        obj_.emplace_back(key, Json());
        return obj_.back().second;
    }
    [[nodiscard]] const Json& get(const std::string& key) const
    {
        for (const auto& kv : obj_)
            if (kv.first == key) return kv.second;
        return nullValue();
    }
    [[nodiscard]] bool has(const std::string& key) const
    {
        for (const auto& kv : obj_)
            if (kv.first == key) return !kv.second.isNull();
        return false;
    }
    [[nodiscard]] const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

    [[nodiscard]] std::string dump() const
    {
        std::string out;
        write(out);
        return out;
    }

    /// Lecture d'un texte JSON. Renvoie false (et `error`) si le texte est invalide.
    static bool parse(const std::string& text, Json& out, std::string& error)
    {
        std::size_t pos = 0;
        if (!parseValue(text, pos, out, error, 0)) return false;
        skipSpace(text, pos);
        if (pos != text.size()) {
            error = "caractères en trop après le JSON";
            return false;
        }
        return true;
    }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;

    static const Json& nullValue()
    {
        static const Json n;
        return n;
    }

    static void writeString(std::string& out, const std::string& s)
    {
        out += '"';
        for (const unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
            }
        }
        out += '"';
    }

    void write(std::string& out) const
    {
        switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: {
            if (!std::isfinite(num_)) {
                out += "null";
                break;
            }
            char buf[40];
            if (num_ == std::floor(num_) && std::abs(num_) < 1e15)
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(num_));
            else
                std::snprintf(buf, sizeof(buf), "%.6g", num_);
            out += buf;
            break;
        }
        case Type::String: writeString(out, str_); break;
        case Type::Array:
            out += '[';
            for (std::size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += ',';
                arr_[i].write(out);
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (std::size_t i = 0; i < obj_.size(); ++i) {
                if (i) out += ',';
                writeString(out, obj_[i].first);
                out += ':';
                obj_[i].second.write(out);
            }
            out += '}';
            break;
        }
    }

    static void skipSpace(const std::string& t, std::size_t& p)
    {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\n' || t[p] == '\r')) ++p;
    }

    static void appendUtf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    static bool parseHex4(const std::string& t, std::size_t& p, uint32_t& v)
    {
        if (p + 4 > t.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    static bool parseString(const std::string& t, std::size_t& p, std::string& out, std::string& error)
    {
        ++p;   // guillemet ouvrant
        while (p < t.size()) {
            const char c = t[p++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= t.size()) break;
            const char e = t[p++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!parseHex4(t, p, cp)) {
                    error = "séquence \\u invalide";
                    return false;
                }
                if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= t.size() && t[p] == '\\' && t[p + 1] == 'u') {
                    p += 2;
                    uint32_t lo = 0;
                    if (!parseHex4(t, p, lo)) {
                        error = "séquence \\u invalide";
                        return false;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: error = "échappement invalide"; return false;
            }
        }
        error = "texte non terminé";
        return false;
    }

    static bool parseValue(const std::string& t, std::size_t& p, Json& out, std::string& error, int depth)
    {
        if (depth > 200) {
            error = "JSON trop imbriqué";
            return false;
        }
        skipSpace(t, p);
        if (p >= t.size()) {
            error = "JSON incomplet";
            return false;
        }
        const char c = t[p];
        if (c == '{') {
            out = object();
            ++p;
            skipSpace(t, p);
            if (p < t.size() && t[p] == '}') {
                ++p;
                return true;
            }
            while (true) {
                skipSpace(t, p);
                if (p >= t.size() || t[p] != '"') {
                    error = "clé attendue";
                    return false;
                }
                std::string key;
                if (!parseString(t, p, key, error)) return false;
                skipSpace(t, p);
                if (p >= t.size() || t[p] != ':') {
                    error = "« : » attendu";
                    return false;
                }
                ++p;
                Json v;
                if (!parseValue(t, p, v, error, depth + 1)) return false;
                out.obj_.emplace_back(std::move(key), std::move(v));
                skipSpace(t, p);
                if (p < t.size() && t[p] == ',') {
                    ++p;
                    continue;
                }
                if (p < t.size() && t[p] == '}') {
                    ++p;
                    return true;
                }
                error = "« , » ou « } » attendu";
                return false;
            }
        }
        if (c == '[') {
            out = array();
            ++p;
            skipSpace(t, p);
            if (p < t.size() && t[p] == ']') {
                ++p;
                return true;
            }
            while (true) {
                Json v;
                if (!parseValue(t, p, v, error, depth + 1)) return false;
                out.arr_.push_back(std::move(v));
                skipSpace(t, p);
                if (p < t.size() && t[p] == ',') {
                    ++p;
                    continue;
                }
                if (p < t.size() && t[p] == ']') {
                    ++p;
                    return true;
                }
                error = "« , » ou « ] » attendu";
                return false;
            }
        }
        if (c == '"') {
            out = Json(std::string());
            return parseString(t, p, out.str_, error);
        }
        if (t.compare(p, 4, "true") == 0) {
            out = Json(true);
            p += 4;
            return true;
        }
        if (t.compare(p, 5, "false") == 0) {
            out = Json(false);
            p += 5;
            return true;
        }
        if (t.compare(p, 4, "null") == 0) {
            out = Json();
            p += 4;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = p;
            ++p;
            while (p < t.size() && (std::isdigit(static_cast<unsigned char>(t[p])) || t[p] == '.' || t[p] == 'e' || t[p] == 'E' ||
                                    t[p] == '+' || t[p] == '-'))
                ++p;
            const std::string num = t.substr(start, p - start);
            char* end = nullptr;
            const double v = std::strtod(num.c_str(), &end);
            if (end == num.c_str()) {
                error = "nombre invalide";
                return false;
            }
            out = Json(v);
            return true;
        }
        error = "valeur JSON invalide";
        return false;
    }
};

} // namespace drapeo::mcp
