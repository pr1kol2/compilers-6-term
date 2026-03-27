#include <algorithm>
#include <gtest/gtest.h>
#include <vector>

#include "tokenization/tokenize.hpp"

// NOLINTBEGIN

using namespace tokenization;

TEST(Tokenize, EmptyInput) { EXPECT_TRUE(tokenize("").empty()); }

TEST(Tokenize, Whitespaces) { EXPECT_TRUE(tokenize("   \n\t  ").empty()); }

TEST(Tokenize, Keywords) {
  auto tokens = tokenize("defn data case of");
  std::vector<Token> expected = {
      {Definition{}, {1, 1, 1, 5}},
      {Data{}, {1, 6, 1, 10}},
      {Case{}, {1, 11, 1, 15}},
      {Of{}, {1, 16, 1, 18}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, LowerVariable) {
  auto tokens = tokenize("foo bar");
  std::vector<Token> expected = {
      {LowerVariable{"foo"}, {1, 1, 1, 4}},
      {LowerVariable{"bar"}, {1, 5, 1, 8}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, UpperVariable) {
  auto tokens = tokenize("Nil Cons");
  std::vector<Token> expected = {
      {UpperVariable{"Nil"}, {1, 1, 1, 4}},
      {UpperVariable{"Cons"}, {1, 5, 1, 9}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, IntegerLiteral) {
  auto tokens = tokenize("42 0 123");
  std::vector<Token> expected = {
      {IntLiteral{42}, {1, 1, 1, 3}},
      {IntLiteral{0}, {1, 4, 1, 5}},
      {IntLiteral{123}, {1, 6, 1, 9}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, ArrowOperator) {
  auto tokens = tokenize("->+-");
  std::vector<Token> expected = {
      {Arrow{}, {1, 1, 1, 3}},
      {Plus{}, {1, 3, 1, 4}},
      {Minus{}, {1, 4, 1, 5}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, AllOperatorsAndDelimiters) {
  auto tokens = tokenize("= + - * / { } ( ) ,");
  std::vector<Token> expected = {
      {Equal{}, {1, 1, 1, 2}},
      {Plus{}, {1, 3, 1, 4}},
      {Minus{}, {1, 5, 1, 6}},
      {Star{}, {1, 7, 1, 8}},
      {Slash{}, {1, 9, 1, 10}},
      {LeftBrace{}, {1, 11, 1, 12}},
      {RightBrace{}, {1, 13, 1, 14}},
      {LeftParentheses{}, {1, 15, 1, 16}},
      {RightParentheses{}, {1, 17, 1, 18}},
      {Comma{}, {1, 19, 1, 20}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, TracksPosition) {
  auto tokens = tokenize("defn\nfoo");
  std::vector<Token> expected = {
      {Definition{}, {1, 1, 1, 5}},
      {LowerVariable{"foo"}, {2, 1, 2, 4}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, ThrowsOnUnexpectedCharacter) {
  EXPECT_THROW(tokenize("@"), std::runtime_error);
}

TEST(Tokenize, FunctionDeclaration) {
  auto tokens = tokenize("defn f x = { x + x }");
  std::vector<Token> expected = {
      {Definition{}, {1, 1, 1, 5}},       {LowerVariable{"f"}, {1, 6, 1, 7}},
      {LowerVariable{"x"}, {1, 8, 1, 9}}, {Equal{}, {1, 10, 1, 11}},
      {LeftBrace{}, {1, 12, 1, 13}},      {LowerVariable{"x"}, {1, 14, 1, 15}},
      {Plus{}, {1, 16, 1, 17}},           {LowerVariable{"x"}, {1, 18, 1, 19}},
      {RightBrace{}, {1, 20, 1, 21}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

TEST(Tokenize, MinusVsArrow) {
  auto tokens = tokenize("x -> y - z");
  std::vector<Token> expected = {
      {LowerVariable{"x"}, {1, 1, 1, 2}},   {Arrow{}, {1, 3, 1, 5}},
      {LowerVariable{"y"}, {1, 6, 1, 7}},   {Minus{}, {1, 8, 1, 9}},
      {LowerVariable{"z"}, {1, 10, 1, 11}},
  };
  EXPECT_TRUE(std::ranges::equal(tokens, expected));
}

// NOLINTEND
