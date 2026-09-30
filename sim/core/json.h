// Minimal JSON reader (objects, arrays, strings, numbers, true/false/null) for parameter and
// scenario files. No external dependencies; throws nothing, reports errors via ok().
#pragma once

#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sim {

struct Json {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;  // insertion order preserved

    bool is_obj() const { return type == OBJ; }
    bool is_num() const { return type == NUM; }
    bool is_str() const { return type == STR; }
    bool is_arr() const { return type == ARR; }
    const Json* get(const std::string& key) const {
        for (const auto& kv : o) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    double num(const std::string& key, double def) const {
        const Json* v = get(key);
        if (!v) return def;
        if (v->type == NUM) return v->n;
        if (v->type == BOOL) return v->b ? 1 : 0;
        return def;
    }
    std::string str(const std::string& key, const std::string& def) const {
        const Json* v = get(key);
        return v && v->type == STR ? v->s : def;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : t_(text) {}
    bool parse(Json& out) {
        skip();
        if (!value(out)) return false;
        skip();
        return pos_ == t_.size();
    }
    const std::string& error() const { return err_; }

private:
    const std::string& t_;
    size_t pos_ = 0;
    std::string err_;

    bool fail(const char* msg) {
        err_ = std::string(msg) + " at offset " + std::to_string(pos_);
        return false;
    }
    void skip() {
        while (pos_ < t_.size()) {
            if (std::isspace(static_cast<unsigned char>(t_[pos_]))) { ++pos_; continue; }
            if (t_.compare(pos_, 2, "//") == 0) { while (pos_ < t_.size() && t_[pos_] != '\n') ++pos_; continue; }
            break;
        }
    }
    bool value(Json& v) {
        if (pos_ >= t_.size()) return fail("unexpected end");
        const char c = t_[pos_];
        if (c == '{') return object(v);
        if (c == '[') return array(v);
        if (c == '"') { v.type = Json::STR; return string(v.s); }
        if (t_.compare(pos_, 4, "true") == 0) { v.type = Json::BOOL; v.b = true; pos_ += 4; return true; }
        if (t_.compare(pos_, 5, "false") == 0) { v.type = Json::BOOL; v.b = false; pos_ += 5; return true; }
        if (t_.compare(pos_, 4, "null") == 0) { v.type = Json::NUL; pos_ += 4; return true; }
        char* end = nullptr;
        v.n = std::strtod(t_.c_str() + pos_, &end);
        if (end == t_.c_str() + pos_) return fail("bad value");
        pos_ = static_cast<size_t>(end - t_.c_str());
        v.type = Json::NUM;
        return true;
    }
    bool string(std::string& out) {
        ++pos_;
        out.clear();
        while (pos_ < t_.size() && t_[pos_] != '"') {
            char c = t_[pos_++];
            if (c == '\\' && pos_ < t_.size()) {
                const char e = t_[pos_++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'u': pos_ += 4; c = '?'; break;
                    default: c = e; break;
                }
            }
            out.push_back(c);
        }
        if (pos_ >= t_.size()) return fail("unterminated string");
        ++pos_;
        return true;
    }
    bool array(Json& v) {
        v.type = Json::ARR;
        ++pos_;
        skip();
        if (pos_ < t_.size() && t_[pos_] == ']') { ++pos_; return true; }
        for (;;) {
            Json item;
            skip();
            if (!value(item)) return false;
            v.a.push_back(std::move(item));
            skip();
            if (pos_ < t_.size() && t_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < t_.size() && t_[pos_] == ']') { ++pos_; return true; }
            return fail("expected , or ]");
        }
    }
    bool object(Json& v) {
        v.type = Json::OBJ;
        ++pos_;
        skip();
        if (pos_ < t_.size() && t_[pos_] == '}') { ++pos_; return true; }
        for (;;) {
            skip();
            if (pos_ >= t_.size() || t_[pos_] != '"') return fail("expected key");
            std::string key;
            if (!string(key)) return false;
            skip();
            if (pos_ >= t_.size() || t_[pos_] != ':') return fail("expected :");
            ++pos_;
            skip();
            Json item;
            if (!value(item)) return false;
            v.o.emplace_back(std::move(key), std::move(item));
            skip();
            if (pos_ < t_.size() && t_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < t_.size() && t_[pos_] == '}') { ++pos_; return true; }
            return fail("expected , or }");
        }
    }
};

inline bool json_parse(const std::string& text, Json& out, std::string* error = nullptr) {
    JsonParser p(text);
    const bool ok = p.parse(out);
    if (!ok && error) *error = p.error();
    return ok;
}

}  // namespace sim
