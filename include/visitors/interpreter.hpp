#pragma once

#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "parsing/ast.hpp"
#include "util/box.hpp"

namespace visitors {

struct ConstructedValue;

using ValueVariant = std::variant<int, Box<ConstructedValue>>;

struct Value : ValueVariant {
  using Variant = ValueVariant;
  using ValueVariant::ValueVariant;
  using ValueVariant::operator=;

  // NOLINTNEXTLINE
  Value(ConstructedValue cv);

  friend bool operator==(const Value&, const Value&) = default;
};

struct ConstructedValue {
  std::string name;
  std::vector<Value> fields;

  bool operator==(const ConstructedValue&) const = default;
};

inline Value::Value(ConstructedValue cv)
    : ValueVariant(Box<ConstructedValue>(std::move(cv))) {}

std::ostream& operator<<(std::ostream& os, const Value& value);

Value interpret(const ast::Program& program);

}  // namespace visitors
