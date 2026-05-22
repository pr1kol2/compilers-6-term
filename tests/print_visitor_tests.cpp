#include <gtest/gtest.h>
#include <string>
#include <string_view>

#include "test_utils.hpp"
#include "visitors/print_visitor.hpp"

// NOLINTBEGIN

using namespace ast;
using test_utils::parseProgram;

namespace {

std::string printBody(std::string_view source) {
  auto program = parseProgram(source);
  const auto& fd = std::get<FunctionDefinition>(program.definitions.front());
  return visitors::print(*fd.body);
}

std::string printDef(std::string_view source) {
  return visitors::print(parseProgram(source).definitions.front());
}

}  // namespace

TEST(PrintVisitor, IntLiteral) {
  EXPECT_EQ(printBody("defn f = { 42 }"), "42");
}

TEST(PrintVisitor, Variable) { EXPECT_EQ(printBody("defn f x = { x }"), "x"); }

TEST(PrintVisitor, BinaryAddition) {
  EXPECT_EQ(printBody("defn f = { 1 + 2 }"), "(1 + 2)");
}

TEST(PrintVisitor, NestedArithmetic) {
  EXPECT_EQ(printBody("defn f = { 1 + 2 * 3 }"), "(1 + (2 * 3))");
}

TEST(PrintVisitor, LeftAssociativity) {
  EXPECT_EQ(printBody("defn f = { 1 - 2 - 3 }"), "((1 - 2) - 3)");
}

TEST(PrintVisitor, Application) {
  EXPECT_EQ(printBody("defn f = { g x }"), "(g x)");
}

TEST(PrintVisitor, NestedApplication) {
  EXPECT_EQ(printBody("defn f = { g x y }"), "((g x) y)");
}

TEST(PrintVisitor, Division) {
  EXPECT_EQ(printBody("defn f = { 1 / 2 }"), "(1 / 2)");
}

TEST(PrintVisitor, CaseExpression) {
  EXPECT_EQ(
      printBody("defn f x = { case x of { Nil -> { 0 } Cons y ys -> { y } } }"),
      "case x of { Nil -> { 0 } Cons y ys -> { y } }");
}

TEST(PrintVisitor, CaseWithVariablePattern) {
  EXPECT_EQ(printBody("defn f x = { case x of { y -> { y } } }"),
            "case x of { y -> { y } }");
}

TEST(PrintVisitor, FunctionDefinition) {
  EXPECT_EQ(printDef("defn f x y = { x + y }"), "defn f x y = { (x + y) }");
}

TEST(PrintVisitor, FunctionWithoutParameters) {
  EXPECT_EQ(printDef("defn f = { 42 }"), "defn f = { 42 }");
}

TEST(PrintVisitor, DataTypeDefinition) {
  EXPECT_EQ(printDef("data List = { Nil, Cons Int List }"),
            "data List = { Nil, Cons Int List }");
}

TEST(PrintVisitor, FullProgram) {
  EXPECT_EQ(visitors::print(parseProgram("defn f x = { x + 1 } "
                                         "data Bool = { True, False }")),
            "defn f x = { (x + 1) }\ndata Bool = { True, False }");
}

// NOLINTEND
