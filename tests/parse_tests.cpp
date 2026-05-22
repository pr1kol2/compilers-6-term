#include <gtest/gtest.h>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/parse.hpp"
#include "test_utils.hpp"

// NOLINTBEGIN

using namespace ast;
using test_utils::functionBody;
using test_utils::parseSource;

namespace {

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

}  // namespace

TEST(Parse, NumericLiteral) {
  auto parsed = expectProgram(
      "defn f = { 42 }",
      Program{{Definition{FunctionDefinition{{}, "f", {}, literal(42)}}}});
  const auto& node = std::get<IntLiteral>(functionBody(parsed.ast));
  expectPosition(parsed, node, {1, 12, 1, 14});
}

TEST(Parse, Variable) {
  auto parsed = expectProgram(
      "defn f x = { x }",
      Program{{Definition{FunctionDefinition{{}, "f", {"x"}, variable("x")}}}});
  const auto& node = std::get<Variable>(functionBody(parsed.ast));
  expectPosition(parsed, node, {1, 14, 1, 15});
}

TEST(Parse, Addition) {
  auto parsed = expectProgram(
      "defn f = { 1 + 2 }",
      Program{{Definition{FunctionDefinition{
          {}, "f", {}, binary<Addition>(literal(1), literal(2))}}}});
  const auto& node = std::get<Addition>(functionBody(parsed.ast));
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
  const auto& function =
      std::get<FunctionDefinition>(parsed.ast.definitions.front());
  expectPosition(parsed, function, {1, 1, 1, 5});
}

TEST(Parse, DataType) {
  auto parsed = expectProgram(
      "data Bool = { True, False }",
      Program{{Definition{DataTypeDefinition{
          {},
          "Bool",
          {Constructor{{}, "True", {}}, Constructor{{}, "False", {}}}}}}});
  const auto& data_type =
      std::get<DataTypeDefinition>(parsed.ast.definitions.front());
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
  const auto& case_expression =
      std::get<CaseExpression>(functionBody(parsed.ast));
  const auto& first_branch = case_expression.branches.front();
  const auto& second_branch = case_expression.branches.back();
  const auto& first_pattern =
      std::get<ConstructorPattern>(first_branch.pattern);
  const auto& second_pattern =
      std::get<ConstructorPattern>(second_branch.pattern);

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
  const auto& function =
      std::get<FunctionDefinition>(parsed.ast.definitions.front());
  const auto& case_expression = std::get<CaseExpression>(*function.body);
  const auto& branch = case_expression.branches.front();
  const auto& pattern = std::get<ConstructorPattern>(branch.pattern);
  const auto& data_type =
      std::get<DataTypeDefinition>(parsed.ast.definitions.back());

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
  EXPECT_THROW(static_cast<void>(parseSource("42")), std::runtime_error);
}

TEST(Parse, ThrowsOnMissingBrace) {
  EXPECT_THROW(static_cast<void>(parseSource("defn f = { 42")),
               std::runtime_error);
}

// NOLINTEND
