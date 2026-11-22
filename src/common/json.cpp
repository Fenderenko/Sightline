#include "common/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace sl::json {

namespace {

const Value kNull;
const std::string kEmpty;

void AppendUtf8(unsigned cp, std::string& out) {
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

struct Parser {
  const char* p;
  const char* e;
  std::string err;

  bool Fail(const char* what) {
    if (err.empty()) err = what;
    return false;
  }

  void Ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
  }

  bool Literal(const char* word, size_t n) {
    if (static_cast<size_t>(e - p) < n || std::string_view(p, n) != std::string_view(word, n)) return Fail("bad literal");
    p += n;
    return true;
  }

  bool Hex4(unsigned& v) {
    if (e - p < 4) return Fail("bad \\u escape");
    v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      else return Fail("bad hex digit");
    }
    return true;
  }

  bool String(std::string& out) {
    if (p >= e || *p != '"') return Fail("expected string");
    ++p;
    while (p < e) {
      const char c = *p++;
      if (c == '"') return true;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (p >= e) break;
      const char x = *p++;
      switch (x) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = 0;
          if (!Hex4(cp)) return false;
          // Combine UTF-16 surrogate pairs.
          if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            unsigned lo = 0;
            if (!Hex4(lo)) return false;
            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          AppendUtf8(cp, out);
          break;
        }
        default: return Fail("bad escape");
      }
    }
    return Fail("unterminated string");
  }

  bool Parse(Value& v, int depth) {
    if (depth > 64) return Fail("nesting too deep");
    Ws();
    if (p >= e) return Fail("unexpected end");
    const char c = *p;
    if (c == '{') {
      ++p;
      v = Value::Object();
      Ws();
      if (p < e && *p == '}') {
        ++p;
        return true;
      }
      for (;;) {
        Ws();
        std::string key;
        if (!String(key)) return false;
        Ws();
        if (p >= e || *p != ':') return Fail("expected ':'");
        ++p;
        Value item;
        if (!Parse(item, depth + 1)) return false;
        v.Set(std::move(key), std::move(item));
        Ws();
        if (p < e && *p == ',') {
          ++p;
          continue;
        }
        if (p < e && *p == '}') {
          ++p;
          return true;
        }
        return Fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++p;
      v = Value::Array();
      Ws();
      if (p < e && *p == ']') {
        ++p;
        return true;
      }
      for (;;) {
        Value item;
        if (!Parse(item, depth + 1)) return false;
        v.Push(std::move(item));
        Ws();
        if (p < e && *p == ',') {
          ++p;
          continue;
        }
        if (p < e && *p == ']') {
          ++p;
          return true;
        }
        return Fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      std::string s;
      if (!String(s)) return false;
      v = Value(std::move(s));
      return true;
    }
    if (c == 't') {
      if (!Literal("true", 4)) return false;
      v = Value(true);
      return true;
    }
    if (c == 'f') {
      if (!Literal("false", 5)) return false;
      v = Value(false);
      return true;
    }
    if (c == 'n') {
      if (!Literal("null", 4)) return false;
      v = Value();
      return true;
    }
    double d = 0;
    const auto res = std::from_chars(p, e, d);
    if (res.ec != std::errc()) return Fail("bad number");
    p = res.ptr;
    v = Value(d);
    return true;
  }
};

void Escape(const std::string& s, std::string& out) {
  out += '"';
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += ch;
        }
    }
  }
  out += '"';
}

void Write(const Value& v, std::string& out, bool pretty, int indent) {
  const auto newline = [&](int level) {
    if (!pretty) return;
    out += '\n';
    out.append(static_cast<size_t>(level) * 2, ' ');
  };
  switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.Bool() ? "true" : "false"; break;
    case Value::Type::Number: {
      const double d = v.Num();
      char buf[32];
      if (!std::isfinite(d)) {
        out += "0";
      } else if (d == std::floor(d) && std::fabs(d) < 1e15) {
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        out += buf;
      } else {
        snprintf(buf, sizeof(buf), "%.6g", d);
        out += buf;
      }
      break;
    }
    case Value::Type::String: Escape(v.Str(), out); break;
    case Value::Type::Array: {
      out += '[';
      for (size_t i = 0; i < v.Size(); ++i) {
        if (i) out += ',';
        newline(indent + 1);
        Write(v.At(i), out, pretty, indent + 1);
      }
      if (v.Size()) newline(indent);
      out += ']';
      break;
    }
    case Value::Type::Object: {
      out += '{';
      for (size_t i = 0; i < v.Size(); ++i) {
        if (i) out += ',';
        newline(indent + 1);
        Escape(v.KeyAt(i), out);
        out += pretty ? ": " : ":";
        Write(v.At(i), out, pretty, indent + 1);
      }
      if (v.Size()) newline(indent);
      out += '}';
      break;
    }
  }
}

}  // namespace

Value Value::Array() {
  Value v;
  v.type_ = Type::Array;
  return v;
}

Value Value::Object() {
  Value v;
  v.type_ = Type::Object;
  return v;
}

const std::string& Value::Str() const { return type_ == Type::String ? str_ : kEmpty; }

const Value& Value::operator[](std::string_view key) const {
  if (type_ != Type::Object) return kNull;
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) return vals_[i];
  }
  return kNull;
}

bool Value::Has(std::string_view key) const {
  if (type_ != Type::Object) return false;
  for (const auto& k : keys_) {
    if (k == key) return true;
  }
  return false;
}

Value& Value::Set(std::string key, Value v) {
  if (type_ != Type::Object) *this = Object();
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      vals_[i] = std::move(v);
      return vals_[i];
    }
  }
  keys_.push_back(std::move(key));
  vals_.push_back(std::move(v));
  return vals_.back();
}

const Value& Value::At(size_t i) const { return i < vals_.size() ? vals_[i] : kNull; }

const std::string& Value::KeyAt(size_t i) const { return i < keys_.size() ? keys_[i] : kEmpty; }

Value& Value::Push(Value v) {
  if (type_ != Type::Array) *this = Array();
  vals_.push_back(std::move(v));
  return vals_.back();
}

bool Parse(std::string_view text, Value& out, std::string* error) {
  Parser ps{text.data(), text.data() + text.size(), {}};
  Value v;
  bool ok = ps.Parse(v, 0);
  if (ok) {
    ps.Ws();
    if (ps.p != ps.e) ok = ps.Fail("trailing characters");
  }
  if (ok) out = std::move(v);
  else if (error) *error = ps.err;
  return ok;
}

std::string Serialize(const Value& v, bool pretty) {
  std::string out;
  Write(v, out, pretty, 0);
  if (pretty) out += '\n';
  return out;
}

}  // namespace sl::json
