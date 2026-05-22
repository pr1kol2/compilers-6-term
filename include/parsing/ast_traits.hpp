#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "parsing/ast.hpp"

namespace ast {

template <typename Node>
concept IsBinaryOperator = requires(const Node& node) {
  node.left_operand;
  node.right_operand;
};

enum class BinaryOperatorKind : std::uint8_t {
  Addition,
  Subtraction,
  Multiplication,
  Division,
};

template <typename Op>
struct BinaryOperatorTraits;

template <>
struct BinaryOperatorTraits<Addition> {
  static constexpr BinaryOperatorKind kind = BinaryOperatorKind::Addition;
  static constexpr std::string_view symbol = "+";
};

template <>
struct BinaryOperatorTraits<Subtraction> {
  static constexpr BinaryOperatorKind kind = BinaryOperatorKind::Subtraction;
  static constexpr std::string_view symbol = "-";
};

template <>
struct BinaryOperatorTraits<Multiplication> {
  static constexpr BinaryOperatorKind kind = BinaryOperatorKind::Multiplication;
  static constexpr std::string_view symbol = "*";
};

template <>
struct BinaryOperatorTraits<Division> {
  static constexpr BinaryOperatorKind kind = BinaryOperatorKind::Division;
  static constexpr std::string_view symbol = "/";
};

template <IsBinaryOperator Op>
constexpr BinaryOperatorKind kindOf() {
  return BinaryOperatorTraits<std::remove_cvref_t<Op>>::kind;
}

template <IsBinaryOperator Op>
constexpr std::string_view symbolOf() {
  return BinaryOperatorTraits<std::remove_cvref_t<Op>>::symbol;
}

}  // namespace ast
