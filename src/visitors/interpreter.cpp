#include "visitors/interpreter.hpp"

#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "util/box.hpp"
#include "util/overloaded.hpp"

namespace visitors {

namespace {

struct PartialConstructor {
  std::string name;
  std::size_t arity;
  std::vector<Value> applied;
};

using EvalResult = std::variant<Value, PartialConstructor>;

using Environment = std::unordered_map<std::string, Value>;

struct ConstructorRegistry {
  std::unordered_map<std::string, std::size_t> arities;

  void registerDataType(const ast::DataTypeDefinition& dt) {
    for (const auto& ctor : dt.constructors) {
      arities[ctor.name] = ctor.fields.size();
    }
  }

  [[nodiscard]] bool isConstructor(const std::string& name) const {
    return arities.contains(name);
  }

  [[nodiscard]] std::size_t arity(const std::string& name) const {
    return arities.at(name);
  }
};

Value asValue(EvalResult result) {
  if (auto* v = std::get_if<Value>(&result)) {
    return std::move(*v);
  }
  throw std::runtime_error("Unexpected partial constructor in final result");
}

int asInt(const Value& value) {
  const auto* n = std::get_if<int>(&value);
  if (n == nullptr) {
    throw std::runtime_error("Expected integer value");
  }
  return *n;
}

EvalResult evaluate(const ast::Expression& expression, const Environment& env,
                    const ConstructorRegistry& registry);

bool matchPattern(const ast::Pattern& pattern, const Value& value,
                  Environment& env) {
  return std::visit(
      util::overloaded{
          [&](const ast::VariablePattern& vp) -> bool {
            env[vp.name] = value;
            return true;
          },
          [&](const ast::ConstructorPattern& cp) -> bool {
            const auto* boxed = std::get_if<Box<ConstructedValue>>(&value);
            if (boxed == nullptr || (*boxed)->name != cp.name) {
              return false;
            }
            const auto& fields = (*boxed)->fields;
            if (fields.size() != cp.arguments.size()) {
              return false;
            }
            for (std::size_t i = 0; i < cp.arguments.size(); ++i) {
              env[cp.arguments[i]] = fields[i];
            }
            return true;
          },
      },
      pattern);
}

template <typename Op>
int applyBinaryOp(int left, int right) {
  if constexpr (std::same_as<Op, ast::Addition>) {
    return left + right;
  } else if constexpr (std::same_as<Op, ast::Subtraction>) {
    return left - right;
  } else if constexpr (std::same_as<Op, ast::Multiplication>) {
    return left * right;
  } else if constexpr (std::same_as<Op, ast::Division>) {
    if (right == 0) {
      throw std::runtime_error("Division by zero");
    }
    return left / right;
  }
}

EvalResult evaluate(const ast::Expression& expression, const Environment& env,
                    const ConstructorRegistry& registry) {
  return std::visit(
      util::overloaded{
          [](const ast::IntLiteral& lit) -> EvalResult {
            return Value{lit.value};
          },
          [&](const ast::Variable& var) -> EvalResult {
            if (registry.isConstructor(var.name)) {
              auto arity = registry.arity(var.name);
              if (arity == 0) {
                return Value{ConstructedValue{var.name, {}}};
              }
              return PartialConstructor{var.name, arity, {}};
            }
            auto it = env.find(var.name);
            if (it == env.end()) {
              throw std::runtime_error("Undefined variable: " + var.name);
            }
            return Value{it->second};
          },
          [&]<ast::IsBinaryOperator Op>(const Op& op) -> EvalResult {
            int left =
                asInt(asValue(evaluate(*op.left_operand, env, registry)));
            int right =
                asInt(asValue(evaluate(*op.right_operand, env, registry)));
            return Value{applyBinaryOp<Op>(left, right)};
          },
          [&](const ast::Application& app) -> EvalResult {
            auto func = evaluate(*app.function, env, registry);
            auto arg = asValue(evaluate(*app.argument, env, registry));

            auto* partial = std::get_if<PartialConstructor>(&func);
            if (partial == nullptr) {
              throw std::runtime_error("Application of non-constructor value");
            }

            partial->applied.push_back(std::move(arg));

            if (partial->applied.size() == partial->arity) {
              return Value{ConstructedValue{std::move(partial->name),
                                            std::move(partial->applied)}};
            }
            return std::move(*partial);
          },
          [&](const ast::CaseExpression& ce) -> EvalResult {
            auto scrutinee = asValue(evaluate(*ce.scrutinee, env, registry));
            for (const auto& branch : ce.branches) {
              Environment branch_env = env;
              if (matchPattern(branch.pattern, scrutinee, branch_env)) {
                return evaluate(*branch.body, branch_env, registry);
              }
            }
            throw std::runtime_error("No matching branch in case expression");
          },
      },
      expression);
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const Value& value) {
  std::visit(util::overloaded{
                 [&](int n) { os << n; },
                 [&](const Box<ConstructedValue>& cv) {
                   if (cv->fields.empty()) {
                     os << cv->name;
                   } else {
                     os << '(' << cv->name;
                     for (const auto& field : cv->fields) {
                       os << ' ' << field;
                     }
                     os << ')';
                   }
                 },
             },
             value);
  return os;
}

Value interpret(const ast::Program& program) {
  ConstructorRegistry registry;
  const ast::FunctionDefinition* main_fn = nullptr;

  for (const auto& def : program.definitions) {
    std::visit(util::overloaded{
                   [&](const ast::FunctionDefinition& fd) {
                     if (fd.name == "main") {
                       main_fn = &fd;
                     }
                   },
                   [&](const ast::DataTypeDefinition& dt) {
                     registry.registerDataType(dt);
                   },
               },
               def);
  }

  if (main_fn == nullptr) {
    throw std::runtime_error("No 'main' function defined");
  }

  Environment env;
  return asValue(evaluate(*main_fn->body, env, registry));
}

}  // namespace visitors
