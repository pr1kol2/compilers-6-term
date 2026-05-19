#include "semantics/symbol_table.hpp"

#include <cctype>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "util/overloaded.hpp"

namespace semantics {

namespace {

[[nodiscard]] bool startsWithUppercase(std::string_view name) {
  return !name.empty() && static_cast<bool>(std::isupper(
                              static_cast<unsigned char>(*name.begin())));
}

}  // namespace

std::string_view toString(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::BuiltinType:
      return "builtin type";
    case SymbolKind::DataType:
      return "data type";
    case SymbolKind::Constructor:
      return "constructor";
    case SymbolKind::Function:
      return "function";
    case SymbolKind::Parameter:
      return "parameter";
    case SymbolKind::PatternVariable:
      return "pattern variable";
  }
  std::unreachable();
}

bool isTypeSymbol(SymbolKind kind) {
  return kind == SymbolKind::BuiltinType || kind == SymbolKind::DataType;
}

bool isValueSymbol(SymbolKind kind) {
  return kind == SymbolKind::Constructor || kind == SymbolKind::Function ||
         kind == SymbolKind::Parameter || kind == SymbolKind::PatternVariable;
}

std::string positionOf(const parsing::ParsedProgram& parsed,
                       ast::NodeId node_id) {
  if (node_id == ast::kInvalidNodeId || node_id >= parsed.positions.size()) {
    return "<builtin>";
  }
  return parsed.positions.at(node_id).toString();
}

Diagnostic makeDiagnostic(const parsing::ParsedProgram& parsed,
                          ast::NodeId node_id, std::string message) {
  return Diagnostic{
      .node_id = node_id,
      .message = std::format("{} at {}", std::move(message),
                             positionOf(parsed, node_id)),
  };
}

std::size_t StringHash::operator()(std::string_view value) const noexcept {
  return std::hash<std::string_view>{}(value);
}

ScopeTree::ScopeTree() {
  scopes_.push_back(Scope{
      .id = 0,
      .parent = kInvalidScopeId,
      .owner = ast::kInvalidNodeId,
      .symbols = {},
      .children = {},
  });
}

const Scope& ScopeTree::scope(ScopeId id) const { return scopes_.at(id); }

const Symbol& ScopeTree::symbol(SymbolId id) const { return symbols_.at(id); }

std::optional<ScopeId> ScopeTree::scopeOf(ast::NodeId node_id) const {
  if (auto it = scope_by_node_.find(node_id); it != scope_by_node_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<SymbolId> ScopeTree::localSymbolId(ScopeId scope_id,
                                                 std::string_view name) const {
  const auto& symbols = scope(scope_id).symbols;
  if (auto it = symbols.find(name); it != symbols.end()) {
    return it->second;
  }
  return std::nullopt;
}

const Symbol* ScopeTree::localSymbol(ScopeId scope_id,
                                     std::string_view name) const {
  const auto id = localSymbolId(scope_id, name);
  return id.has_value() ? &symbol(*id) : nullptr;
}

std::optional<SymbolId> ScopeTree::resolveId(ScopeId scope_id,
                                             std::string_view name) const {
  auto current = scope_id;
  while (current != kInvalidScopeId) {
    if (const auto id = localSymbolId(current, name); id.has_value()) {
      return id;
    }
    current = scope(current).parent;
  }
  return std::nullopt;
}

std::optional<SymbolId> ScopeTree::resolvedSymbolId(ast::NodeId node_id) const {
  if (auto it = resolved_symbols_.find(node_id);
      it != resolved_symbols_.end()) {
    return it->second;
  }
  return std::nullopt;
}

const Symbol* ScopeTree::resolvedSymbol(ast::NodeId node_id) const {
  const auto id = resolvedSymbolId(node_id);
  return id.has_value() ? &symbol(*id) : nullptr;
}

const std::vector<SymbolId>* ScopeTree::declaredSymbolIds(
    ast::NodeId node_id) const {
  if (auto it = declared_symbols_.find(node_id);
      it != declared_symbols_.end()) {
    return &it->second;
  }
  return nullptr;
}

const Symbol* ScopeTree::declaredSymbol(ast::NodeId node_id,
                                        SymbolKind kind) const {
  const auto* ids = declaredSymbolIds(node_id);
  if (ids == nullptr) {
    return nullptr;
  }

  for (const auto id : *ids) {
    const auto& candidate = symbol(id);
    if (candidate.kind == kind) {
      return &candidate;
    }
  }
  return nullptr;
}

ScopeId ScopeTree::createScope(ScopeId parent, ast::NodeId owner) {
  const auto id = scopes_.size();
  scopes_.push_back(Scope{
      .id = id,
      .parent = parent,
      .owner = owner,
      .symbols = {},
      .children = {},
  });
  scopes_.at(parent).children.push_back(id);
  return id;
}

void ScopeTree::bindNodeToScope(ast::NodeId node_id, ScopeId scope_id) {
  if (node_id != ast::kInvalidNodeId) {
    scope_by_node_[node_id] = scope_id;
  }
}

void ScopeTree::bindNodeToSymbol(ast::NodeId node_id, SymbolId symbol_id) {
  if (node_id != ast::kInvalidNodeId) {
    resolved_symbols_[node_id] = symbol_id;
  }
}

std::optional<SymbolId> ScopeTree::addLocalSymbol(ScopeId scope_id,
                                                  Symbol symbol) {
  auto& symbols = scopes_.at(scope_id).symbols;
  const auto name = symbol.name;
  if (symbols.contains(name)) {
    return std::nullopt;
  }

  const auto id = symbols_.size();
  symbol.id = id;
  symbol.scope = scope_id;
  symbols_.push_back(std::move(symbol));
  symbols.emplace(name, id);

  const auto declaration = symbols_.back().declaration;
  if (declaration != ast::kInvalidNodeId) {
    declared_symbols_[declaration].push_back(id);
  }

  return id;
}

class Analyzer {
 public:
  explicit Analyzer(const parsing::ParsedProgram& parsed) : parsed_(&parsed) {}

  AnalysisResult analyze() {
    registerBuiltins();
    registerGlobalDefinitions();
    checkGlobalDefinitions();
    bindFunctionBodies();
    return std::move(result_);
  }

 private:
  const parsing::ParsedProgram* parsed_;
  AnalysisResult result_;

  void registerBuiltins() {
    addSymbol(ScopeTree::rootScopeId(), Symbol{
                                            .name = "Int",
                                            .kind = SymbolKind::BuiltinType,
                                        });
  }

  void registerGlobalDefinitions() {
    for (const auto& definition : parsed_->ast.definitions) {
      std::visit(util::overloaded{
                     [&](const ast::FunctionDefinition& fd) {
                       addSymbol(ScopeTree::rootScopeId(),
                                 Symbol{
                                     .name = fd.name,
                                     .kind = SymbolKind::Function,
                                     .declaration = fd.id,
                                     .arity = fd.parameters.size(),
                                 });
                     },
                     [&](const ast::DataTypeDefinition& dt) {
                       addSymbol(ScopeTree::rootScopeId(),
                                 Symbol{
                                     .name = dt.name,
                                     .kind = SymbolKind::DataType,
                                     .declaration = dt.id,
                                     .arity = dt.constructors.size(),
                                 });
                       for (const auto& ctor : dt.constructors) {
                         addSymbol(ScopeTree::rootScopeId(),
                                   Symbol{
                                       .name = ctor.name,
                                       .kind = SymbolKind::Constructor,
                                       .declaration = ctor.id,
                                       .arity = ctor.fields.size(),
                                   });
                       }
                     },
                 },
                 definition);
    }
  }

  void checkGlobalDefinitions() {
    for (const auto& definition : parsed_->ast.definitions) {
      std::visit(
          util::overloaded{
              [&](const ast::FunctionDefinition& fd) {
                result_.scopes.bindNodeToScope(fd.id, ScopeTree::rootScopeId());
              },
              [&](const ast::DataTypeDefinition& dt) {
                result_.scopes.bindNodeToScope(dt.id, ScopeTree::rootScopeId());
                for (const auto& ctor : dt.constructors) {
                  result_.scopes.bindNodeToScope(ctor.id,
                                                 ScopeTree::rootScopeId());
                  checkConstructorFields(ctor);
                }
              },
          },
          definition);
    }
  }

  void bindFunctionBodies() {
    for (const auto& definition : parsed_->ast.definitions) {
      if (const auto* fd = std::get_if<ast::FunctionDefinition>(&definition)) {
        bindFunction(*fd);
      }
    }
  }

  void bindFunction(const ast::FunctionDefinition& fd) {
    const auto function_scope =
        result_.scopes.createScope(ScopeTree::rootScopeId(), fd.id);
    result_.scopes.bindNodeToScope(fd.id, function_scope);

    for (const auto& parameter : fd.parameters) {
      addSymbol(function_scope, Symbol{
                                    .name = parameter,
                                    .kind = SymbolKind::Parameter,
                                    .declaration = fd.id,
                                });
    }

    bindExpression(*fd.body, function_scope);
  }

  void checkConstructorFields(const ast::Constructor& ctor) {
    for (const auto& field : ctor.fields) {
      const auto* symbol =
          result_.scopes.localSymbol(ScopeTree::rootScopeId(), field);
      if (symbol == nullptr || !isTypeSymbol(symbol->kind)) {
        addError(ctor.id, std::format("Unknown type '{}'", field));
      }
    }
  }

  void bindExpression(const ast::Expression& expression, ScopeId scope_id) {
    std::visit(util::overloaded{
                   [&](const ast::IntLiteral& literal) {
                     result_.scopes.bindNodeToScope(literal.id, scope_id);
                   },
                   [&](const ast::Variable& variable) {
                     bindVariable(variable, scope_id);
                   },
                   [&]<ast::IsBinaryOperator Op>(const Op& op) {
                     result_.scopes.bindNodeToScope(op.id, scope_id);
                     bindExpression(*op.left_operand, scope_id);
                     bindExpression(*op.right_operand, scope_id);
                   },
                   [&](const ast::Application& application) {
                     result_.scopes.bindNodeToScope(application.id, scope_id);
                     bindExpression(*application.function, scope_id);
                     bindExpression(*application.argument, scope_id);
                   },
                   [&](const ast::CaseExpression& case_expression) {
                     bindCaseExpression(case_expression, scope_id);
                   },
               },
               expression);
  }

  void bindVariable(const ast::Variable& variable, ScopeId scope_id) {
    result_.scopes.bindNodeToScope(variable.id, scope_id);

    const auto symbol_id = result_.scopes.resolveId(scope_id, variable.name);
    if (!symbol_id.has_value()) {
      addError(variable.id,
               std::format("Undefined symbol '{}'", variable.name));
      return;
    }
    const auto& symbol = result_.scopes.symbol(*symbol_id);

    if (startsWithUppercase(variable.name) &&
        symbol.kind != SymbolKind::Constructor) {
      addError(variable.id, std::format("'{}' is {}, not constructor",
                                        variable.name, toString(symbol.kind)));
      return;
    }

    if (!startsWithUppercase(variable.name) && !isValueSymbol(symbol.kind)) {
      addError(variable.id, std::format("'{}' is {}, not value", variable.name,
                                        toString(symbol.kind)));
      return;
    }

    result_.scopes.bindNodeToSymbol(variable.id, *symbol_id);
  }

  void bindCaseExpression(const ast::CaseExpression& case_expression,
                          ScopeId scope_id) {
    result_.scopes.bindNodeToScope(case_expression.id, scope_id);
    bindExpression(*case_expression.scrutinee, scope_id);

    for (const auto& branch : case_expression.branches) {
      const auto branch_scope = result_.scopes.createScope(scope_id, branch.id);
      result_.scopes.bindNodeToScope(branch.id, branch_scope);
      bindPattern(branch.pattern, branch_scope);
      bindExpression(*branch.body, branch_scope);
    }
  }

  void bindPattern(const ast::Pattern& pattern, ScopeId scope_id) {
    std::visit(
        util::overloaded{
            [&](const ast::VariablePattern& variable_pattern) {
              result_.scopes.bindNodeToScope(variable_pattern.id, scope_id);
              addSymbol(scope_id, Symbol{
                                      .name = variable_pattern.name,
                                      .kind = SymbolKind::PatternVariable,
                                      .declaration = variable_pattern.id,
                                  });
            },
            [&](const ast::ConstructorPattern& constructor_pattern) {
              bindConstructorPattern(constructor_pattern, scope_id);
            },
        },
        pattern);
  }

  void bindConstructorPattern(const ast::ConstructorPattern& pattern,
                              ScopeId scope_id) {
    result_.scopes.bindNodeToScope(pattern.id, scope_id);

    const auto symbol_id =
        result_.scopes.localSymbolId(ScopeTree::rootScopeId(), pattern.name);
    const auto* symbol =
        symbol_id.has_value() ? &result_.scopes.symbol(*symbol_id) : nullptr;
    if (symbol == nullptr || symbol->kind != SymbolKind::Constructor) {
      addError(pattern.id,
               std::format("Undefined constructor '{}'", pattern.name));
    } else if (symbol->arity != pattern.arguments.size()) {
      addError(
          pattern.id,
          std::format("Constructor '{}' expects {} arguments, got {}",
                      pattern.name, symbol->arity, pattern.arguments.size()));
    } else {
      result_.scopes.bindNodeToSymbol(pattern.id, symbol->id);
    }

    for (const auto& argument : pattern.arguments) {
      addSymbol(scope_id, Symbol{
                              .name = argument,
                              .kind = SymbolKind::PatternVariable,
                              .declaration = pattern.id,
                          });
    }
  }

  std::optional<SymbolId> addSymbol(ScopeId scope_id, Symbol symbol) {
    const auto name = symbol.name;
    const auto declaration = symbol.declaration;
    if (const auto id =
            result_.scopes.addLocalSymbol(scope_id, std::move(symbol));
        id.has_value()) {
      return id;
    }

    const auto* previous = result_.scopes.localSymbol(scope_id, name);
    addError(declaration,
             std::format("Redefinition of '{}'; previous {} declared at {}",
                         name, toString(previous->kind),
                         positionOf(*parsed_, previous->declaration)));
    return std::nullopt;
  }

  void addError(ast::NodeId node_id, std::string message) {
    result_.diagnostics.push_back(
        makeDiagnostic(*parsed_, node_id, std::move(message)));
  }
};

AnalysisResult analyze(const parsing::ParsedProgram& parsed) {
  return Analyzer{parsed}.analyze();
}

}  // namespace semantics
