#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "parsing/parse.hpp"
#include "tokenization/tokenize.hpp"
#include "visitors/interpreter.hpp"

// NOLINTBEGIN

using visitors::ConstructedValue;
using visitors::Value;

namespace {

parsing::ParsedProgram parseSource(std::string_view source) {
  return parsing::parse(tokenization::tokenize(source));
}

Value run(std::string_view source) {
  return visitors::interpret(parseSource(source).ast);
}

Value constructed(std::string name, std::vector<Value> fields = {}) {
  return ConstructedValue{std::move(name), std::move(fields)};
}

}  // namespace

TEST(Interpreter, IntLiteral) { EXPECT_EQ(run("defn main = { 42 }"), 42); }

TEST(Interpreter, Addition) { EXPECT_EQ(run("defn main = { 1 + 2 }"), 3); }

TEST(Interpreter, Subtraction) { EXPECT_EQ(run("defn main = { 10 - 3 }"), 7); }

TEST(Interpreter, Multiplication) {
  EXPECT_EQ(run("defn main = { 4 * 5 }"), 20);
}

TEST(Interpreter, Division) { EXPECT_EQ(run("defn main = { 10 / 3 }"), 3); }

TEST(Interpreter, ArithmeticPrecedence) {
  EXPECT_EQ(run("defn main = { 1 + 2 * 3 }"), 7);
}

TEST(Interpreter, LeftAssociativity) {
  EXPECT_EQ(run("defn main = { 10 - 3 - 2 }"), 5);
}

TEST(Interpreter, Parentheses) {
  EXPECT_EQ(run("defn main = { (1 + 2) * 3 }"), 9);
}

TEST(Interpreter, DivisionByZero) {
  EXPECT_THROW(run("defn main = { 1 / 0 }"), std::runtime_error);
}

TEST(Interpreter, NullaryConstructor) {
  EXPECT_EQ(run("data Bool = { True, False } defn main = { True }"),
            constructed("True"));
}

TEST(Interpreter, ConstructorWithFields) {
  EXPECT_EQ(run("data Wrap = { W Int } defn main = { W 42 }"),
            constructed("W", {Value(42)}));
}

TEST(Interpreter, NestedConstructors) {
  EXPECT_EQ(
      run("data List = { Nil, Cons Int List } defn main = { Cons 1 Nil }"),
      constructed("Cons", {Value(1), constructed("Nil")}));
}

TEST(Interpreter, CaseOnNullaryConstructor) {
  EXPECT_EQ(
      run("data Bool = { True, False } "
          "defn main = { case True of { True -> { 1 } False -> { 0 } } }"),
      Value(1));
}

TEST(Interpreter, CaseSecondBranch) {
  EXPECT_EQ(
      run("data Bool = { True, False } "
          "defn main = { case False of { True -> { 1 } False -> { 0 } } }"),
      Value(0));
}

TEST(Interpreter, CaseWithVariableBinding) {
  EXPECT_EQ(run("data Wrap = { W Int } "
                "defn main = { case W 42 of { W x -> { x } } }"),
            Value(42));
}

TEST(Interpreter, CaseWithArithmeticInBody) {
  EXPECT_EQ(run("data Wrap = { W Int } "
                "defn main = { case W 10 of { W x -> { x + 1 } } }"),
            Value(11));
}

TEST(Interpreter, CaseDestructureNested) {
  EXPECT_EQ(run("data List = { Nil, Cons Int List } "
                "defn main = { case Cons 7 Nil of "
                "{ Nil -> { 0 } Cons x xs -> { x } } }"),
            Value(7));
}

TEST(Interpreter, NoMainFunction) {
  EXPECT_THROW(run("defn f = { 42 }"), std::runtime_error);
}

TEST(Interpreter, UndefinedVariable) {
  EXPECT_THROW(run("defn main = { x }"), std::runtime_error);
}

TEST(Interpreter, NoMatchingBranch) {
  EXPECT_THROW(run("data AB = { A, B } "
                   "defn main = { case B of { A -> { 1 } } }"),
               std::runtime_error);
}
