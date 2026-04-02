#pragma once

namespace ast {

template <typename Node>
concept IsBinaryOperator = requires(const Node& node) {
  node.left_operand;
  node.right_operand;
};

}  // namespace ast
