#include "visitors/interpreter.hpp"

#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "parsing/parse.hpp"
#include "semantics/symbol_table.hpp"
#include "semantics/type_table.hpp"
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

using Environment = std::unordered_map<semantics::SymbolId, Value>;

struct EvaluationContext {
  const semantics::ScopeTree* scopes = nullptr;
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

[[nodiscard]] std::size_t constructorArity(const semantics::Symbol& symbol) {
  if (symbol.kind != semantics::SymbolKind::Constructor) {
    throw std::runtime_error("Expected constructor symbol");
  }
  return symbol.arity;
}

[[nodiscard]] const ast::FunctionDefinition* findFunctionById(
    const ast::Program& program, ast::NodeId id) {
  for (const auto& definition : program.definitions) {
    if (const auto* function =
            std::get_if<ast::FunctionDefinition>(&definition);
        function != nullptr && function->id == id) {
      return function;
    }
  }
  return nullptr;
}

EvalResult evaluate(const ast::Expression& expression, const Environment& env,
                    const EvaluationContext& context);

bool matchPattern(const ast::Pattern& pattern, const Value& value,
                  Environment& env, const EvaluationContext& context) {
  return std::visit(
      util::overloaded{
          [&](const ast::VariablePattern& vp) -> bool {
            const auto* symbol = context.scopes->declaredSymbol(
                vp.id, semantics::SymbolKind::PatternVariable);
            if (symbol == nullptr) {
              throw std::runtime_error(
                  "Pattern variable has no semantic binding");
            }
            env[symbol->id] = value;
            return true;
          },
          [&](const ast::ConstructorPattern& cp) -> bool {
            const auto* constructor = context.scopes->resolvedSymbol(cp.id);
            if (constructor == nullptr ||
                constructor->kind != semantics::SymbolKind::Constructor) {
              throw std::runtime_error(
                  "Constructor pattern has no semantic binding");
            }
            const auto* boxed = std::get_if<Box<ConstructedValue>>(&value);
            if (boxed == nullptr || (*boxed)->name != constructor->name) {
              return false;
            }
            const auto& fields = (*boxed)->fields;
            if (fields.size() != cp.arguments.size()) {
              return false;
            }
            if (fields.empty()) {
              return true;
            }
            const auto* bindings = context.scopes->declaredSymbolIds(cp.id);
            if (bindings == nullptr || bindings->size() != fields.size()) {
              throw std::runtime_error(
                  "Constructor pattern has invalid bindings");
            }
            for (const auto& [binding, field] :
                 std::views::zip(*bindings, fields)) {
              env[binding] = field;
            }
            return true;
          },
      },
      pattern);
}

template <typename Op>
int applyBinaryOp(int left, int right) {
  switch (ast::kindOf<Op>()) {
    case ast::BinaryOperatorKind::Addition:
      return left + right;
    case ast::BinaryOperatorKind::Subtraction:
      return left - right;
    case ast::BinaryOperatorKind::Multiplication:
      return left * right;
    case ast::BinaryOperatorKind::Division:
      if (right == 0) {
        throw std::runtime_error("Division by zero");
      }
      return left / right;
  }
  std::unreachable();
}

EvalResult evaluate(const ast::Expression& expression, const Environment& env,
                    const EvaluationContext& context) {
  return std::visit(
      util::overloaded{
          [](const ast::IntLiteral& lit) -> EvalResult {
            return Value{lit.value};
          },
          [&](const ast::Variable& var) -> EvalResult {
            const auto* symbol = context.scopes->resolvedSymbol(var.id);
            if (symbol == nullptr) {
              throw std::runtime_error("Undefined variable: " + var.name);
            }
            if (symbol->kind == semantics::SymbolKind::Constructor) {
              const auto arity = constructorArity(*symbol);
              if (arity == 0) {
                return Value{ConstructedValue{symbol->name, {}}};
              }
              return PartialConstructor{symbol->name, arity, {}};
            }
            auto it = env.find(symbol->id);
            if (it == env.end()) {
              throw std::runtime_error("Unbound local variable: " + var.name);
            }
            return Value{it->second};
          },
          [&]<ast::IsBinaryOperator Op>(const Op& op) -> EvalResult {
            int left = asInt(asValue(evaluate(*op.left_operand, env, context)));
            int right =
                asInt(asValue(evaluate(*op.right_operand, env, context)));
            return Value{applyBinaryOp<Op>(left, right)};
          },
          [&](const ast::Application& app) -> EvalResult {
            auto func = evaluate(*app.function, env, context);
            auto arg = asValue(evaluate(*app.argument, env, context));

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
            auto scrutinee = asValue(evaluate(*ce.scrutinee, env, context));
            for (const auto& branch : ce.branches) {
              Environment branch_env = env;
              if (matchPattern(branch.pattern, scrutinee, branch_env,
                               context)) {
                return evaluate(*branch.body, branch_env, context);
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
  return interpret(parsing::ParsedProgram{
      .ast = program,
      .positions = {},
  });
}

Value interpret(const parsing::ParsedProgram& parsed) {
  auto analysis = semantics::analyze(parsed);
  return interpret(parsed, analysis);
}

Value interpret(const parsing::ParsedProgram& parsed,
                const semantics::AnalysisResult& analysis) {
  if (!analysis.ok()) {
    throw std::runtime_error(analysis.diagnostics.front().message);
  }

  const auto type_analysis = semantics::analyzeTypes(parsed, analysis);
  if (!type_analysis.ok()) {
    throw std::runtime_error(type_analysis.diagnostics.front().message);
  }

  const auto* main_symbol =
      analysis.scopes.localSymbol(semantics::ScopeTree::rootScopeId(), "main");
  if (main_symbol == nullptr ||
      main_symbol->kind != semantics::SymbolKind::Function) {
    throw std::runtime_error("No 'main' function defined");
  }

  const auto* main_fn = findFunctionById(parsed.ast, main_symbol->declaration);
  if (main_fn == nullptr) {
    throw std::runtime_error("Main function declaration is missing");
  }

  Environment env;
  return asValue(evaluate(*main_fn->body, env,
                          EvaluationContext{
                              .scopes = &analysis.scopes,
                          }));
}

}  // namespace visitors
