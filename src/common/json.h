#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sl::json {

// Minimal JSON DOM. Objects keep insertion order; lookups are linear,
// which is fine for config-sized documents.
class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : type_(Type::Bool), bool_(b) {}
  Value(int n) : type_(Type::Number), num_(n) {}
  Value(unsigned n) : type_(Type::Number), num_(n) {}
  Value(float n) : type_(Type::Number), num_(n) {}
  Value(double n) : type_(Type::Number), num_(n) {}
  Value(const char* s) : type_(Type::String), str_(s) {}
  Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
  Value(std::string_view s) : type_(Type::String), str_(s) {}

  static Value Array();
  static Value Object();

  Type type() const { return type_; }
  bool IsNull() const { return type_ == Type::Null; }
  bool IsObject() const { return type_ == Type::Object; }
  bool IsArray() const { return type_ == Type::Array; }
  bool IsString() const { return type_ == Type::String; }
  bool IsNumber() const { return type_ == Type::Number; }
  bool IsBool() const { return type_ == Type::Bool; }

  bool Bool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
  double Num(double def = 0) const { return type_ == Type::Number ? num_ : def; }
  float Float(float def = 0) const { return type_ == Type::Number ? static_cast<float>(num_) : def; }
  int Int(int def = 0) const { return type_ == Type::Number ? static_cast<int>(num_) : def; }
  const std::string& Str() const;
  std::string Str(std::string_view def) const { return type_ == Type::String ? str_ : std::string(def); }

  // Object access. Missing keys yield a shared null value.
  const Value& operator[](std::string_view key) const;
  bool Has(std::string_view key) const;
  Value& Set(std::string key, Value v);

  // Array / object element access.
  size_t Size() const { return vals_.size(); }
  const Value& At(size_t i) const;
  const std::string& KeyAt(size_t i) const;
  Value& Push(Value v);

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  std::vector<std::string> keys_;
  std::vector<Value> vals_;
};

bool Parse(std::string_view text, Value& out, std::string* error = nullptr);
std::string Serialize(const Value& v, bool pretty = true);

}  // namespace sl::json
