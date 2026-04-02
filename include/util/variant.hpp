#pragma once

#include <format>
#include <stdexcept>
#include <string_view>
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

template <typename Expected, typename Node>
[[nodiscard]] const Expected* tryGet(const Node& node) {
  return std::get_if<Expected>(&variantOf(node));
}

template <typename Expected, typename Node>
[[nodiscard]] const Expected& get(const Node& node,
                                  std::string_view context = "variant node") {
  if (const auto* value = tryGet<Expected>(node)) {
    return *value;
  }
  throw std::logic_error(std::format("Unexpected alternative in {}", context));
}

}  // namespace util
