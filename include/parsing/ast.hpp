#pragma once

#include <limits>
#include <string>
#include <variant>
#include <vector>

#include "util/box.hpp"

namespace ast {

namespace detail {

// we do not want to compare id's
template <typename T, typename... Members>
constexpr bool equalBy(const T& left, const T& right, Members... members) {
  return ((left.*members == right.*members) && ...);
}

}  // namespace detail

// NOLINTBEGIN
#define AST_NODE_ID NodeId id = kInvalidNodeId;

#define DECLARE_OPERATOR_EQUAL(Type, ...)                       \
  friend bool operator==(const Type& left, const Type& right) { \
    return detail::equalBy(left, right, __VA_ARGS__);           \
  }

#define DECLARE_BINARY_EXPRESSION(Type)                                     \
  struct Type {                                                             \
    AST_NODE_ID                                                             \
    ExpressionPtr left_operand;                                             \
    ExpressionPtr right_operand;                                            \
    DECLARE_OPERATOR_EQUAL(Type, &Type::left_operand, &Type::right_operand) \
  };

#define DECLARE_VARIANT_WRAPPER(Type, VariantType)              \
  struct Type : VariantType {                                   \
    using Variant = VariantType;                                \
    using VariantType::VariantType;                             \
    using VariantType::operator=;                               \
    friend bool operator==(const Type&, const Type&) = default; \
  };
// NOLINTEND

using NodeId = std::size_t;
inline constexpr NodeId kInvalidNodeId = std::numeric_limits<NodeId>::max();

struct Expression;
using ExpressionPtr = Box<Expression>;

struct IntLiteral {
  AST_NODE_ID
  int value;
  DECLARE_OPERATOR_EQUAL(IntLiteral, &IntLiteral::value)
};

struct Variable {
  AST_NODE_ID
  std::string name;
  DECLARE_OPERATOR_EQUAL(Variable, &Variable::name)
};

DECLARE_BINARY_EXPRESSION(Addition)
DECLARE_BINARY_EXPRESSION(Subtraction)
DECLARE_BINARY_EXPRESSION(Multiplication)
DECLARE_BINARY_EXPRESSION(Division)

struct Application {
  AST_NODE_ID
  ExpressionPtr function;
  ExpressionPtr argument;
  DECLARE_OPERATOR_EQUAL(Application, &Application::function,
                         &Application::argument)
};

struct VariablePattern {
  AST_NODE_ID
  std::string name;
  DECLARE_OPERATOR_EQUAL(VariablePattern, &VariablePattern::name)
};

struct ConstructorPattern {
  AST_NODE_ID
  std::string name;
  std::vector<std::string> arguments;
  DECLARE_OPERATOR_EQUAL(ConstructorPattern, &ConstructorPattern::name,
                         &ConstructorPattern::arguments)
};

using PatternVariant = std::variant<VariablePattern, ConstructorPattern>;
DECLARE_VARIANT_WRAPPER(Pattern, PatternVariant)

struct Branch {
  AST_NODE_ID
  Pattern pattern;
  ExpressionPtr body;
  DECLARE_OPERATOR_EQUAL(Branch, &Branch::pattern, &Branch::body)
};

struct CaseExpression {
  AST_NODE_ID
  ExpressionPtr scrutinee;
  std::vector<Branch> branches;
  DECLARE_OPERATOR_EQUAL(CaseExpression, &CaseExpression::scrutinee,
                         &CaseExpression::branches)
};

using ExpressionVariant =
    std::variant<IntLiteral, Variable, Addition, Subtraction, Multiplication,
                 Division, Application, CaseExpression>;
DECLARE_VARIANT_WRAPPER(Expression, ExpressionVariant)

struct FunctionDefinition {
  AST_NODE_ID
  std::string name;
  std::vector<std::string> parameters;
  ExpressionPtr body;
  DECLARE_OPERATOR_EQUAL(FunctionDefinition, &FunctionDefinition::name,
                         &FunctionDefinition::parameters,
                         &FunctionDefinition::body)
};

struct Constructor {
  AST_NODE_ID
  std::string name;
  std::vector<std::string> fields;
  DECLARE_OPERATOR_EQUAL(Constructor, &Constructor::name, &Constructor::fields)
};

struct DataTypeDefinition {
  AST_NODE_ID
  std::string name;
  std::vector<Constructor> constructors;
  DECLARE_OPERATOR_EQUAL(DataTypeDefinition, &DataTypeDefinition::name,
                         &DataTypeDefinition::constructors)
};

using DefinitionVariant = std::variant<FunctionDefinition, DataTypeDefinition>;
DECLARE_VARIANT_WRAPPER(Definition, DefinitionVariant)

struct Program {
  std::vector<Definition> definitions;

  friend bool operator==(const Program&, const Program&) = default;
};

#undef AST_NODE_ID
#undef DECLARE_OPERATOR_EQUAL
#undef DECLARE_BINARY_EXPRESSION
#undef DECLARE_VARIANT_WRAPPER

}  // namespace ast
