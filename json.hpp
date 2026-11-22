#ifndef MINI_JSON_HPP
#define MINI_JSON_HPP

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <istream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace nlohmann {

class json {
 private:
  enum Type { Null, Bool, Number, String, Array, Object, Discarded };
  Type type_ = Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  std::vector<json> items_;
  std::vector<std::string> keys_;

 public:
  json() = default;
  json(std::nullptr_t) {}
  json(bool b) : type_(Bool), bool_(b) {}
  json(const char *s) : type_(String), str_(s) {}
  json(const std::string &s) : type_(String), str_(s) {}
  template <class T, class = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
  json(T n) : type_(Number), num_((double)n) {}

  json(std::initializer_list<json> init) {
    bool pairs = std::all_of(init.begin(), init.end(), [](const json &e) {
      return e.type_ == Array && e.items_.size() == 2 && e.items_[0].type_ == String;
    });
    if (pairs) {
      type_ = Object;
      for (const json &e : init) (*this)[e.items_[0].str_] = e.items_[1];
    } else {
      type_ = Array;
      items_.assign(init.begin(), init.end());
    }
  }

  static json array() { json j; j.type_ = Array; return j; }
  static json object() { json j; j.type_ = Object; return j; }

  void push_back(json v) {
    if (type_ == Null) type_ = Array;
    items_.push_back(std::move(v));
  }

  json &operator[](const std::string &key) {
    if (type_ == Null) type_ = Object;
    for (size_t i = 0; i < keys_.size(); i++)
      if (keys_[i] == key) return items_[i];
    keys_.push_back(key);
    items_.emplace_back();
    return items_.back();
  }
  const json &operator[](const std::string &key) const {
    static const json missing;
    const json *p = find(key);
    return p ? *p : missing;
  }

  bool is_null() const { return type_ == Null; }
  bool is_array() const { return type_ == Array; }
  bool is_object() const { return type_ == Object; }
  bool is_string() const { return type_ == String; }
  bool is_discarded() const { return type_ == Discarded; }
  bool contains(const std::string &key) const { return find(key) != nullptr; }
  size_t size() const { return items_.size(); }
  std::vector<json>::const_iterator begin() const { return items_.begin(); }
  std::vector<json>::const_iterator end() const { return items_.end(); }

  std::string value(const std::string &k, const char *d) const {
    const json *p = find(k);
    return p && p->type_ == String ? p->str_ : std::string(d);
  }
  std::string value(const std::string &k, const std::string &d) const {
    const json *p = find(k);
    return p && p->type_ == String ? p->str_ : d;
  }
  bool value(const std::string &k, bool d) const {
    const json *p = find(k);
    return p && p->type_ == Bool ? p->bool_ : d;
  }
  int value(const std::string &k, int d) const {
    const json *p = find(k);
    if (!p || p->type_ != Number || !(p->num_ >= INT_MIN && p->num_ <= INT_MAX)) return d;
    return (int)p->num_;
  }

  template <class T> T get() const {
    if constexpr (std::is_same_v<T, std::string>) return str_;
    else if constexpr (std::is_same_v<T, bool>) return bool_;
    else return (T)num_;
  }

  std::string dump(int indent = -1) const {
    std::string out;
    write(out, indent, 0);
    return out;
  }

  static json parse(const std::string &text, std::nullptr_t = nullptr, bool allow_exceptions = true) {
    try {
      Parser p{text};
      if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) p.pos = 3;
      json v = p.value(0);
      p.skip_ws();
      if (p.pos != text.size()) throw ParseError{};
      return v;
    } catch (const ParseError &) {
      if (allow_exceptions) throw std::runtime_error("invalid JSON");
      json bad;
      bad.type_ = Discarded;
      return bad;
    }
  }
  static json parse(std::istream &in, std::nullptr_t = nullptr, bool allow_exceptions = true) {
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(text, nullptr, allow_exceptions);
  }

 private:
  const json *find(const std::string &key) const {
    if (type_ != Object) return nullptr;
    for (size_t i = 0; i < keys_.size(); i++)
      if (keys_[i] == key) return &items_[i];
    return nullptr;
  }

  void write(std::string &out, int indent, int depth) const {
    auto newline = [&](int d) {
      if (indent < 0) return;
      out += '\n';
      out.append((size_t)(indent * d), ' ');
    };
    switch (type_) {
      case Bool: out += bool_ ? "true" : "false"; break;
      case Number: write_number(out); break;
      case String: write_string(out, str_); break;
      case Array:
      case Object: {
        const bool obj = type_ == Object;
        if (items_.empty()) { out += obj ? "{}" : "[]"; break; }
        out += obj ? '{' : '[';
        for (size_t i = 0; i < items_.size(); i++) {
          if (i) out += ',';
          newline(depth + 1);
          if (obj) {
            write_string(out, keys_[i]);
            out += indent < 0 ? ":" : ": ";
          }
          items_[i].write(out, indent, depth + 1);
        }
        newline(depth);
        out += obj ? '}' : ']';
        break;
      }
      default: out += "null";
    }
  }

  void write_number(std::string &out) const {
    char buf[40];
    if (!std::isfinite(num_)) out += "null";
    else if (num_ == std::floor(num_) && std::fabs(num_) < 1e15) {
      std::snprintf(buf, sizeof buf, "%lld", (long long)num_);
      out += buf;
    } else {
      std::snprintf(buf, sizeof buf, "%.17g", num_);
      out += buf;
    }
  }

  static void write_string(std::string &out, const std::string &s) {
    out += '"';
    for (unsigned char c : s) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
          } else {
            out += (char)c;
          }
      }
    }
    out += '"';
  }

  struct ParseError {};

  struct Parser {
    static constexpr int MAX_DEPTH = 100;
    static constexpr size_t MAX_KEYS = 1000;

    const std::string &s;
    size_t pos = 0;

    static bool digit(char c) { return c >= '0' && c <= '9'; }
    char peek() const { return pos < s.size() ? s[pos] : '\0'; }
    void skip_ws() {
      while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') pos++;
    }
    void expect(const char *word) {
      size_t n = std::strlen(word);
      if (s.compare(pos, n, word) != 0) throw ParseError{};
      pos += n;
    }

    json value(int depth) {
      if (depth > MAX_DEPTH) throw ParseError{};
      skip_ws();
      switch (peek()) {
        case '{': return object(depth);
        case '[': return array(depth);
        case '"': return json(string());
        case 't': expect("true"); return json(true);
        case 'f': expect("false"); return json(false);
        case 'n': expect("null"); return json();
        default: return number();
      }
    }

    json array(int depth) {
      json a = json::array();
      pos++;
      skip_ws();
      if (peek() == ']') { pos++; return a; }
      for (;;) {
        a.items_.push_back(value(depth + 1));
        skip_ws();
        char c = peek();
        pos++;
        if (c == ']') return a;
        if (c != ',') throw ParseError{};
      }
    }

    json object(int depth) {
      json o = json::object();
      pos++;
      skip_ws();
      if (peek() == '}') { pos++; return o; }
      for (;;) {
        skip_ws();
        if (peek() != '"') throw ParseError{};
        std::string key = string();
        skip_ws();
        if (peek() != ':') throw ParseError{};
        pos++;
        if (o.keys_.size() >= MAX_KEYS) throw ParseError{};
        o[key] = value(depth + 1);
        skip_ws();
        char c = peek();
        pos++;
        if (c == '}') return o;
        if (c != ',') throw ParseError{};
      }
    }

    json number() {
      size_t start = pos;
      if (peek() == '-') pos++;
      if (peek() == '0') pos++;
      else if (peek() >= '1' && peek() <= '9') while (digit(peek())) pos++;
      else throw ParseError{};
      if (peek() == '.') {
        pos++;
        if (!digit(peek())) throw ParseError{};
        while (digit(peek())) pos++;
      }
      if (peek() == 'e' || peek() == 'E') {
        pos++;
        if (peek() == '+' || peek() == '-') pos++;
        if (!digit(peek())) throw ParseError{};
        while (digit(peek())) pos++;
      }
      double v = std::strtod(s.substr(start, pos - start).c_str(), nullptr);
      if (!std::isfinite(v)) throw ParseError{};
      return json(v);
    }

    unsigned hex4() {
      if (pos + 4 > s.size()) throw ParseError{};
      unsigned v = 0;
      for (int i = 0; i < 4; i++) {
        char h = s[pos++];
        v <<= 4;
        if (digit(h)) v |= (unsigned)(h - '0');
        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
        else throw ParseError{};
      }
      return v;
    }

    static void append_utf8(std::string &out, unsigned cp) {
      if (cp < 0x80) out += (char)cp;
      else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) {
        out += (char)(0xE0 | cp >> 12);
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
      } else {
        out += (char)(0xF0 | cp >> 18);
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
      }
    }

    std::string string() {
      std::string out;
      pos++;
      for (;;) {
        if (pos >= s.size()) throw ParseError{};
        unsigned char c = (unsigned char)s[pos++];
        if (c == '"') return out;
        if (c < 0x20) throw ParseError{};
        if (c == '\\') {
          if (pos >= s.size()) throw ParseError{};
          char e = s[pos++];
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
              unsigned cp = hex4();
              if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (s.compare(pos, 2, "\\u") != 0) throw ParseError{};
                pos += 2;
                unsigned low = hex4();
                if (low < 0xDC00 || low > 0xDFFF) throw ParseError{};
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
              } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                throw ParseError{};
              }
              append_utf8(out, cp);
              break;
            }
            default: throw ParseError{};
          }
        } else if (c < 0x80) {
          out += (char)c;
        } else {
          unsigned char lo = 0x80, hi = 0xBF;
          size_t extra;
          if (c >= 0xC2 && c <= 0xDF) extra = 1;
          else if (c >= 0xE0 && c <= 0xEF) { extra = 2; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
          else if (c >= 0xF0 && c <= 0xF4) { extra = 3; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
          else throw ParseError{};
          if (pos + extra > s.size()) throw ParseError{};
          unsigned char second = (unsigned char)s[pos];
          if (second < lo || second > hi) throw ParseError{};
          for (size_t k = 1; k < extra; k++)
            if (((unsigned char)s[pos + k] & 0xC0) != 0x80) throw ParseError{};
          out.append(s, pos - 1, extra + 1);
          pos += extra;
        }
      }
    }
  };
};

}
#endif

