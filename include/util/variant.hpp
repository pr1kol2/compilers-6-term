#pragma once

#include <variant>

namespace util {

template <typename Node>
[[nodiscard]] const typename Node::Variant& variantOf(const Node& node) {
  return node;
}

template <typename Node, typename Visitor>
decltype(auto) visitNode(const Node& node, Visitor&& visitor) {
  return std::visit(std::forward<Visitor>(visitor), variantOf(node));
}

}  // namespace util
