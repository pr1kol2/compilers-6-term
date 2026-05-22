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
  const semantics::TypeTable* types = nullptr;
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

[[nodiscard]] std::size_t constructorArity(const semantics::Symbol& symbol,
                                           const semantics::TypeTable& types) {
  if (symbol.kind != semantics::SymbolKind::Constructor) {
    throw std::runtime_error("Expected constructor symbol");
  }
  const auto* signature = types.getConstructorSignature(symbol.id);
  if (signature == nullptr) {
    throw std::runtime_error("Constructor has no type signature");
  }
  return signature->fields.size();
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
            const auto scope_id = context.scopes->getScopeId(vp.id);
            const auto* symbol =
                scope_id.has_value()
                    ? context.scopes->getLocalSymbol(*scope_id, vp.name)
                    : nullptr;
            if (symbol == nullptr) {
              throw std::runtime_error(
                  "Pattern variable has no semantic binding");
            }
            if (symbol->kind != semantics::SymbolKind::PatternVariable) {
              throw std::runtime_error(
                  "Pattern name is not a variable binding");
            }
            env[symbol->id] = value;
            return true;
          },
          [&](const ast::ConstructorPattern& cp) -> bool {
            const auto* constructor = context.scopes->getResolvedSymbol(cp.id);
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
            const auto scope_id = context.scopes->getScopeId(cp.id);
            if (!scope_id.has_value()) {
              throw std::runtime_error(
                  "Constructor pattern has invalid bindings");
            }
            for (const auto& [argument, field] :
                 std::views::zip(cp.arguments, fields)) {
              const auto* binding =
                  context.scopes->getLocalSymbol(*scope_id, argument);
              if (binding == nullptr ||
                  binding->kind != semantics::SymbolKind::PatternVariable) {
                throw std::runtime_error(
                    "Constructor pattern has invalid bindings");
              }
              env[binding->id] = field;
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
            const auto* symbol = context.scopes->getResolvedSymbol(var.id);
            if (symbol == nullptr) {
              throw std::runtime_error("Undefined variable: " + var.name);
            }
            if (symbol->kind == semantics::SymbolKind::Constructor) {
              const auto arity = constructorArity(*symbol, *context.types);
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

Value interpret(const parsing::ParsedProgram& parsed) {
  auto analysis = semantics::analyze(parsed);
  return interpret(parsed, analysis);
}

Value interpret(const parsing::ParsedProgram& parsed,
                const semantics::AnalysisResult& analysis) {
  if (!analysis.ok()) {
    throw std::runtime_error(
        semantics::formatDiagnostic(parsed, analysis.diagnostics.front()));
  }

  const auto type_analysis = semantics::analyzeTypes(parsed, analysis);
  if (!type_analysis.ok()) {
    throw std::runtime_error(
        semantics::formatDiagnostic(parsed, type_analysis.diagnostics.front()));
  }

  const auto* main_symbol = analysis.scopes.getLocalSymbol(
      semantics::ScopeTree::getRootScopeId(), "main");
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
                              .types = &type_analysis.types,
                          }));
}

}  // namespace visitors
