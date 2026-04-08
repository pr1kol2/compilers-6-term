#pragma once

#include <concepts>
#include <string_view>

#include "parsing/ast.hpp"

namespace ast {

template <typename Node>
concept IsBinaryOperator = requires(const Node& node) {
  node.left_operand;
  node.right_operand;
};

template <IsBinaryOperator Op>
constexpr std::string_view symbolOf() {
  if constexpr (std::same_as<Op, Addition>) {
    return "+";
  } else if constexpr (std::same_as<Op, Subtraction>) {
    return "-";
  } else if constexpr (std::same_as<Op, Multiplication>) {
    return "*";
  } else if constexpr (std::same_as<Op, Division>) {
    return "/";
  }
}

}  // namespace ast
