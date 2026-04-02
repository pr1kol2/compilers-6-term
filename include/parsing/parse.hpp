#pragma once

#include <vector>

#include "parsing/ast.hpp"
#include "tokenization/token.hpp"
#include "util/position.hpp"

namespace parsing {

struct ParsedProgram {
  ast::Program ast;
  std::vector<util::Position> positions;

  // TODO : multiple exceptions
};

ParsedProgram parse(const std::vector<tokenization::Token>& tokens);

[[nodiscard]] inline const util::Position& positionOf(
    const ParsedProgram& parsed, ast::NodeId node_id) {
  return parsed.positions.at(node_id);
}

template <typename Node>
[[nodiscard]] inline const util::Position& positionOf(
    const ParsedProgram& parsed, const Node& node) {
  return positionOf(parsed, node.id);
}

}  // namespace parsing
