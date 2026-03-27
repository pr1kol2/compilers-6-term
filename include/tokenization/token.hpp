#pragma once

#include <string>
#include <variant>

#include "util/position.hpp"

namespace tokenization {

// NOLINTBEGIN
#define ADD_EMPTY_TOKEN(Type)                                           \
  struct Type {                                                         \
    friend bool operator==(const Type& lhs, const Type& rhs) = default; \
  };

#define ADD_OPERATOR_EQUAL(Type) \
  friend bool operator==(const Type& lhs, const Type& rhs) = default;
// NOLINTEND

// Literals
struct IntLiteral {
  int value;
  ADD_OPERATOR_EQUAL(IntLiteral)
};

// Identifiers
struct LowerVariable {
  std::string name;
  ADD_OPERATOR_EQUAL(LowerVariable)
};

struct UpperVariable {
  std::string name;
  ADD_OPERATOR_EQUAL(UpperVariable)
};

// Keywords
ADD_EMPTY_TOKEN(Definition)
ADD_EMPTY_TOKEN(Data)
ADD_EMPTY_TOKEN(Case)
ADD_EMPTY_TOKEN(Of)

// Operators
ADD_EMPTY_TOKEN(Equal)
ADD_EMPTY_TOKEN(Arrow)
ADD_EMPTY_TOKEN(Plus)
ADD_EMPTY_TOKEN(Minus)
ADD_EMPTY_TOKEN(Star)
ADD_EMPTY_TOKEN(Slash)

// Delimiters
ADD_EMPTY_TOKEN(LeftBrace)
ADD_EMPTY_TOKEN(RightBrace)
ADD_EMPTY_TOKEN(LeftParentheses)
ADD_EMPTY_TOKEN(RightParentheses)
ADD_EMPTY_TOKEN(Comma)

using TokenVariant =
    std::variant<IntLiteral, LowerVariable, UpperVariable, Definition, Data,
                 Case, Of, Equal, Arrow, Plus, Minus, Star, Slash, LeftBrace,
                 RightBrace, LeftParentheses, RightParentheses, Comma>;

struct Token {
  TokenVariant load;
  util::Position position;

  ADD_OPERATOR_EQUAL(Token)
};

#undef ADD_EMPTY_TOKEN
#undef ADD_OPERATOR_EQUAL

}  // namespace tokenization
