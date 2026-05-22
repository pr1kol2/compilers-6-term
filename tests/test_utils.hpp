#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/parse.hpp"
#include "semantics/symbol_table.hpp"
#include "semantics/type_table.hpp"
#include "tokenization/tokenize.hpp"
#include "visitors/interpreter.hpp"

namespace test_utils {

[[nodiscard]] inline parsing::ParsedProgram parseSource(
    std::string_view source) {
  return parsing::parse(tokenization::tokenize(source));
}

[[nodiscard]] inline ast::Program parseProgram(std::string_view source) {
  return parseSource(source).ast;
}

[[nodiscard]] inline semantics::AnalysisResult analyzeSource(
    std::string_view source) {
  return semantics::analyze(parseSource(source));
}

[[nodiscard]] inline semantics::TypeAnalysisResult analyzeTypesSource(
    std::string_view source) {
  auto parsed = parseSource(source);
  return semantics::analyzeTypes(parsed, semantics::analyze(parsed));
}

[[nodiscard]] inline const ast::FunctionDefinition& functionAt(
    const ast::Program& program, std::size_t index) {
  return std::get<ast::FunctionDefinition>(program.definitions.at(index));
}

[[nodiscard]] inline const ast::Expression& functionBody(
    const ast::Program& program, std::size_t index = 0) {
  return *functionAt(program, index).body;
}

[[nodiscard]] inline const ast::Variable& asVariable(
    const ast::Expression& expression) {
  return std::get<ast::Variable>(expression);
}

[[nodiscard]] inline const ast::Application& asApplication(
    const ast::Expression& expression) {
  return std::get<ast::Application>(expression);
}

[[nodiscard]] inline const ast::CaseExpression& asCase(
    const ast::Expression& expression) {
  return std::get<ast::CaseExpression>(expression);
}

[[nodiscard]] inline const ast::ConstructorPattern& asConstructorPattern(
    const ast::Pattern& pattern) {
  return std::get<ast::ConstructorPattern>(pattern);
}

[[nodiscard]] inline bool hasDiagnosticContaining(const auto& result,
                                                  std::string_view text) {
  return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
    return diagnostic.message.contains(text);
  });
}

[[nodiscard]] inline visitors::Value constructed(
    std::string name, std::vector<visitors::Value> fields = {}) {
  return visitors::ConstructedValue{std::move(name), std::move(fields)};
}

}  // namespace test_utils
