#include <algorithm>
#include <concepts>
#include <cstddef>
#include <gtest/gtest.h>
#include <initializer_list>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "parsing/parse.hpp"
#include "tokenization/tokenize.hpp"
#include "util/overloaded.hpp"
#include "util/variant.hpp"

// NOLINTBEGIN

using namespace ast;
using namespace std::string_view_literals;
using util::overloaded;
using util::visitNode;

template <typename Expected, typename Node>
[[nodiscard]] const Expected& asNode(const Node& node,
                                     std::string_view context = "AST node") {
  return util::get<Expected>(node, context);
}

parsing::ParsedProgram parseSource(std::string_view source) {
  return parsing::parse(tokenization::tokenize(source));
}

[[nodiscard]] Expression literal(int value) { return IntLiteral{{}, value}; }

[[nodiscard]] Expression variable(std::string_view name) {
  return Variable{{}, std::string(name)};
}

template <typename Operator>
[[nodiscard]] Expression binary(Expression left, Expression right) {
  return Operator{
      {},
      std::move(left),
      std::move(right),
  };
}

[[nodiscard]] Expression apply(Expression function, Expression argument) {
  return Application{
      {},
      std::move(function),
      std::move(argument),
  };
}

[[nodiscard]] Pattern constructorPattern(
    std::string_view name,
    std::initializer_list<std::string_view> arguments = {}) {
  return ConstructorPattern{
      {},
      std::string(name),
      {arguments.begin(), arguments.end()},
  };
}

[[nodiscard]] Expression caseOf(Expression scrutinee,
                                std::initializer_list<Branch> branches) {
  return CaseExpression{
      {},
      std::move(scrutinee),
      std::vector<Branch>(branches),
  };
}

parsing::ParsedProgram expectProgram(std::string_view source,
                                     Program expected) {
  auto parsed = parseSource(source);
  EXPECT_EQ(parsed.ast, expected);
  return parsed;
}

template <typename Node>
void expectPosition(const parsing::ParsedProgram& parsed, const Node& node,
                    util::Position expected) {
  ASSERT_NE(node.id, ast::kInvalidNodeId);
  EXPECT_LT(node.id, parsed.positions.size());
  EXPECT_EQ(parsing::positionOf(parsed, node), expected);
}

[[nodiscard]] const Expression& firstFunctionBody(
    const parsing::ParsedProgram& parsed) {
  const auto& definition = asNode<FunctionDefinition>(
      parsed.ast.definitions.front(), "top-level function definition");
  return *definition.body;
}

[[nodiscard]] const CaseExpression& firstFunctionCaseBody(
    const parsing::ParsedProgram& parsed) {
  return asNode<CaseExpression>(firstFunctionBody(parsed), "function body");
}

void collectNodeIds(const Pattern& pattern, std::vector<NodeId>& node_ids) {
  visitNode(pattern, [&](const auto& node) { node_ids.push_back(node.id); });
}

void collectNodeIds(const Expression& expression,
                    std::vector<NodeId>& node_ids) {
  visitNode(expression, [&](const auto& node) {
    using NodeType = std::remove_cvref_t<decltype(node)>;
    node_ids.push_back(node.id);
    if constexpr (ast::IsBinaryOperator<NodeType>) {
      collectNodeIds(*node.left_operand, node_ids);
      collectNodeIds(*node.right_operand, node_ids);
    } else if constexpr (std::same_as<NodeType, Application>) {
      collectNodeIds(*node.function, node_ids);
      collectNodeIds(*node.argument, node_ids);
    } else if constexpr (std::same_as<NodeType, CaseExpression>) {
      collectNodeIds(*node.scrutinee, node_ids);
      for (const auto& branch : node.branches) {
        node_ids.push_back(branch.id);
        collectNodeIds(branch.pattern, node_ids);
        collectNodeIds(*branch.body, node_ids);
      }
    }
  });
}

void collectNodeIds(const Definition& definition,
                    std::vector<NodeId>& node_ids) {
  visitNode(definition, [&](const auto& node) {
    using NodeType = std::remove_cvref_t<decltype(node)>;
    node_ids.push_back(node.id);
    if constexpr (std::same_as<NodeType, FunctionDefinition>) {
      collectNodeIds(*node.body, node_ids);
    } else if constexpr (std::same_as<NodeType, DataTypeDefinition>) {
      for (const auto& constructor : node.constructors) {
        node_ids.push_back(constructor.id);
      }
    }
  });
}

[[nodiscard]] std::vector<NodeId> collectNodeIds(const Program& program) {
  std::vector<NodeId> node_ids;
  for (const auto& definition : program.definitions) {
    collectNodeIds(definition, node_ids);
  }
  return node_ids;
}

TEST(Parse, NumericLiteral) {
  auto parsed = expectProgram(
      "defn f = { 42 }",
      Program{{Definition{FunctionDefinition{{}, "f", {}, literal(42)}}}});
  const auto& node =
      asNode<IntLiteral>(firstFunctionBody(parsed), "function body");
  expectPosition(parsed, node, {1, 12, 1, 14});
}

TEST(Parse, Variable) {
  auto parsed = expectProgram(
      "defn f x = { x }",
      Program{{Definition{FunctionDefinition{{}, "f", {"x"}, variable("x")}}}});
  const auto& node =
      asNode<Variable>(firstFunctionBody(parsed), "function body");
  expectPosition(parsed, node, {1, 14, 1, 15});
}

TEST(Parse, Addition) {
  auto parsed = expectProgram(
      "defn f = { 1 + 2 }",
      Program{{Definition{FunctionDefinition{
          {}, "f", {}, binary<Addition>(literal(1), literal(2))}}}});
  const auto& node =
      asNode<Addition>(firstFunctionBody(parsed), "function body");
  expectPosition(parsed, node, {1, 14, 1, 15});
}

TEST(Parse, ArithmeticPrecedence) {
  EXPECT_EQ(parseSource("defn f = { 1 + 2 * 3 }").ast,
            (Program{{Definition{FunctionDefinition{
                {},
                "f",
                {},
                binary<Addition>(literal(1), binary<Multiplication>(
                                                 literal(2), literal(3)))}}}}));
}

TEST(Parse, LeftAssociativity) {
  EXPECT_EQ(parseSource("defn f = { 1 - 2 - 3 }").ast,
            (Program{{Definition{FunctionDefinition{
                {},
                "f",
                {},
                binary<Subtraction>(binary<Subtraction>(literal(1), literal(2)),
                                    literal(3))}}}}));
}

TEST(Parse, ParenthesizedExpression) {
  EXPECT_EQ(parseSource("defn f = { (1 + 2) * 3 }").ast,
            (Program{{Definition{FunctionDefinition{
                {},
                "f",
                {},
                binary<Multiplication>(binary<Addition>(literal(1), literal(2)),
                                       literal(3))}}}}));
}

TEST(Parse, Application) {
  EXPECT_EQ(parseSource("defn g = { f x }").ast,
            (Program{{Definition{FunctionDefinition{
                {}, "g", {}, apply(variable("f"), variable("x"))}}}}));
}

TEST(Parse, NestedApplication) {
  EXPECT_EQ(parseSource("defn g = { f x y }").ast,
            (Program{{Definition{FunctionDefinition{
                {},
                "g",
                {},
                apply(apply(variable("f"), variable("x")), variable("y"))}}}}));
}

TEST(Parse, ApplicationBindsTighterThanArithmetic) {
  EXPECT_EQ(parseSource("defn h = { f x + g y }").ast,
            (Program{{Definition{FunctionDefinition{
                {},
                "h",
                {},
                binary<Addition>(apply(variable("f"), variable("x")),
                                 apply(variable("g"), variable("y")))}}}}));
}

TEST(Parse, FunctionWithParameters) {
  auto parsed =
      expectProgram("defn f x y = { x + y }",
                    Program{{Definition{FunctionDefinition{
                        {},
                        "f",
                        {"x", "y"},
                        binary<Addition>(variable("x"), variable("y"))}}}});
  const auto& function = asNode<FunctionDefinition>(
      parsed.ast.definitions.front(), "top-level function definition");
  expectPosition(parsed, function, {1, 1, 1, 5});
}

TEST(Parse, DataType) {
  auto parsed = expectProgram(
      "data Bool = { True, False }",
      Program{{Definition{DataTypeDefinition{
          {},
          "Bool",
          {Constructor{{}, "True", {}}, Constructor{{}, "False", {}}}}}}});
  const auto& data_type = asNode<DataTypeDefinition>(
      parsed.ast.definitions.front(), "top-level data definition");
  expectPosition(parsed, data_type, {1, 1, 1, 5});
  expectPosition(parsed, data_type.constructors.front(), {1, 15, 1, 19});
}

TEST(Parse, DataTypeWithFields) {
  EXPECT_EQ(parseSource("data List = { Nil, Cons Int List }").ast,
            (Program{{Definition{DataTypeDefinition{
                {},
                "List",
                {Constructor{{}, "Nil", {}},
                 Constructor{{}, "Cons", {"Int", "List"}}}}}}}));
}

TEST(Parse, CaseExpression) {
  auto parsed = expectProgram(
      "defn f x = { case x of { Nil -> { 0 } Cons y ys -> { y } } }",
      Program{{Definition{FunctionDefinition{
          {},
          "f",
          {"x"},
          caseOf(variable("x"),
                 {Branch{{}, constructorPattern("Nil"), literal(0)},
                  Branch{{},
                         constructorPattern("Cons", {"y", "ys"}),
                         variable("y")}})}}}});
  const auto& case_expression = firstFunctionCaseBody(parsed);
  const auto& first_branch = case_expression.branches.front();
  const auto& second_branch = case_expression.branches.back();
  const auto& first_pattern =
      asNode<ConstructorPattern>(first_branch.pattern, "case branch pattern");
  const auto& second_pattern =
      asNode<ConstructorPattern>(second_branch.pattern, "case branch pattern");

  expectPosition(parsed, first_pattern, {1, 26, 1, 29});
  expectPosition(parsed, first_branch, {1, 26, 1, 29});
  expectPosition(parsed, second_pattern, {1, 39, 1, 43});
  expectPosition(parsed, second_branch, {1, 39, 1, 43});
}

TEST(Parse, FullExample) {
  EXPECT_EQ(
      parseSource(
          "defn f x = { x + x } "
          "data List = { Nil, Cons Int List } "
          "defn g l = { case l of { Nil -> { 0 } Cons x xs -> { x } } }")
          .ast,
      (Program{
          {Definition{FunctionDefinition{
               {}, "f", {"x"}, binary<Addition>(variable("x"), variable("x"))}},
           Definition{
               DataTypeDefinition{{},
                                  "List",
                                  {Constructor{{}, "Nil", {}},
                                   Constructor{{}, "Cons", {"Int", "List"}}}}},
           Definition{FunctionDefinition{
               {},
               "g",
               {"l"},
               caseOf(variable("l"),
                      {Branch{{}, constructorPattern("Nil"), literal(0)},
                       Branch{{},
                              constructorPattern("Cons", {"x", "xs"}),
                              variable("x")}})}}}}));
}

TEST(Parse, PositionsAreStoredByDenseNodeId) {
  auto parsed = parseSource(
      "defn f x = { case x of { Cons y ys -> { y } } } data List = { Nil, Cons "
      "Int List }");
  auto node_ids = collectNodeIds(parsed.ast);

  std::ranges::sort(node_ids);
  const auto expected_ids =
      std::views::iota(ast::NodeId{0}, parsed.positions.size());

  EXPECT_TRUE(std::ranges::equal(node_ids, expected_ids));
}

TEST(Parse, RepresentativeNodesIndexIntoPositions) {
  auto parsed = expectProgram(
      "defn f x = { case x of { Cons y ys -> { y } } } data List = { Nil, Cons "
      "Int List }",
      Program{{Definition{FunctionDefinition{
                   {},
                   "f",
                   {"x"},
                   caseOf(variable("x"),
                          {Branch{{},
                                  constructorPattern("Cons", {"y", "ys"}),
                                  variable("y")}})}},
               Definition{DataTypeDefinition{
                   {},
                   "List",
                   {Constructor{{}, "Nil", {}},
                    Constructor{{}, "Cons", {"Int", "List"}}}}}}});
  const auto& function = asNode<FunctionDefinition>(
      parsed.ast.definitions.front(), "top-level function definition");
  const auto& case_expression =
      asNode<CaseExpression>(*function.body, "function body");
  const auto& branch = case_expression.branches.front();
  const auto& pattern =
      asNode<ConstructorPattern>(branch.pattern, "case branch pattern");
  const auto& data_type = asNode<DataTypeDefinition>(
      parsed.ast.definitions.back(), "top-level data definition");

  expectPosition(parsed, function, {1, 1, 1, 5});
  expectPosition(parsed, case_expression, {1, 14, 1, 18});
  expectPosition(parsed, branch, {1, 26, 1, 30});
  expectPosition(parsed, pattern, {1, 26, 1, 30});
  expectPosition(parsed, data_type, {1, 49, 1, 53});
}

TEST(Parse, StructuralEqualityIgnoresNodeId) {
  EXPECT_EQ((IntLiteral{1, 42}), (IntLiteral{2, 42}));

  EXPECT_EQ((ConstructorPattern{1, "Cons", {"x"}}),
            (ConstructorPattern{2, "Cons", {"x"}}));

  EXPECT_EQ((FunctionDefinition{1, "f", {"x"}, variable("x")}),
            (FunctionDefinition{2, "f", {"x"}, variable("x")}));
}

TEST(Parse, ThrowsOnUnexpectedToken) {
  EXPECT_THROW(parseSource("42"), std::runtime_error);
}

TEST(Parse, ThrowsOnMissingBrace) {
  EXPECT_THROW(parseSource("defn f = { 42"), std::runtime_error);
}

// NOLINTEND
