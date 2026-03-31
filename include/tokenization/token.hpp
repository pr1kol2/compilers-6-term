#pragma once

#include <string>
#include <variant>

#include "util/position.hpp"

namespace tokenization {

// NOLINTBEGIN
#define DECLARE_EMPTY_TOKEN(Type)                                       \
  struct Type {                                                         \
    friend bool operator==(const Type& lhs, const Type& rhs) = default; \
  };

#define DECLARE_OPERATOR_EQUAL(Type) \
  friend bool operator==(const Type& lhs, const Type& rhs) = default;
// NOLINTEND

// Literals
struct IntLiteral {
  int value;
  DECLARE_OPERATOR_EQUAL(IntLiteral)
};

// Identifiers
struct LowerVariable {
  std::string name;
  DECLARE_OPERATOR_EQUAL(LowerVariable)
};

struct UpperVariable {
  std::string name;
  DECLARE_OPERATOR_EQUAL(UpperVariable)
};

// Keywords
DECLARE_EMPTY_TOKEN(Function)
DECLARE_EMPTY_TOKEN(Data)
DECLARE_EMPTY_TOKEN(Case)
DECLARE_EMPTY_TOKEN(Of)

// Operators
DECLARE_EMPTY_TOKEN(Equal)
DECLARE_EMPTY_TOKEN(Arrow)
DECLARE_EMPTY_TOKEN(Plus)
DECLARE_EMPTY_TOKEN(Minus)
DECLARE_EMPTY_TOKEN(Star)
DECLARE_EMPTY_TOKEN(Slash)

// Delimiters
DECLARE_EMPTY_TOKEN(LeftBrace)
DECLARE_EMPTY_TOKEN(RightBrace)
DECLARE_EMPTY_TOKEN(LeftParentheses)
DECLARE_EMPTY_TOKEN(RightParentheses)
DECLARE_EMPTY_TOKEN(Comma)

using TokenVariant =
    std::variant<IntLiteral, LowerVariable, UpperVariable, Function, Data, Case,
                 Of, Equal, Arrow, Plus, Minus, Star, Slash, LeftBrace,
                 RightBrace, LeftParentheses, RightParentheses, Comma>;

struct Token {
  TokenVariant load;
  util::Position position;

  DECLARE_OPERATOR_EQUAL(Token)
};

#undef DECLARE_EMPTY_TOKEN
#undef DECLARE_OPERATOR_EQUAL

}  // namespace tokenization
