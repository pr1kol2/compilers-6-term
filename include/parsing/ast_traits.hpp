#pragma once

namespace ast {

// TODO maybe delete / move to parse_tests.cpp

template <typename Node>
concept IsBinaryOperator = requires(const Node& node) {
  node.left_operand;
  node.right_operand;
};

}  // namespace ast
