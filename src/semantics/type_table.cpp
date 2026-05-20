#include "semantics/type_table.hpp"

#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "semantics/symbol_table.hpp"
#include "util/overloaded.hpp"

namespace semantics {

namespace {

using TypeId = std::size_t;
inline constexpr TypeId kInvalidTypeId = std::numeric_limits<TypeId>::max();

struct InternalIntType {};

struct InternalDataType {
  SymbolId symbol = kInvalidSymbolId;
};

struct InternalFunctionType {
  TypeId parameter = kInvalidTypeId;
  TypeId result = kInvalidTypeId;
};

struct InternalTypeVariable {
  std::optional<TypeId> binding;
};

using InternalType = std::variant<InternalIntType, InternalDataType,
                                  InternalFunctionType, InternalTypeVariable>;

struct InternalConstructorSignature {
  SymbolId data_type = kInvalidSymbolId;
  std::vector<TypeId> fields;
};

struct InternalFunctionSignature {
  std::vector<TypeId> parameters;
  TypeId result = kInvalidTypeId;
};

}  // namespace

Type intType() { return Type{IntType{}}; }

Type dataType(SymbolId symbol) { return Type{DataType{.symbol = symbol}}; }

namespace {

Type functionType(Type parameter, Type result) {
  return Type{Box<FunctionType>(FunctionType{
      .parameter = Box<Type>(std::move(parameter)),
      .result = Box<Type>(std::move(result)),
  })};
}

bool containsFunctionType(const Type& type) {
  return std::visit(util::overloaded{
                        [](const IntType&) { return false; },
                        [](const DataType&) { return false; },
                        [](const Box<FunctionType>&) { return true; },
                    },
                    type);
}

}  // namespace

const ConstructorSignature* TypeTable::constructorSignature(
    SymbolId symbol_id) const {
  if (auto it = constructor_signatures_.find(symbol_id);
      it != constructor_signatures_.end()) {
    return &it->second;
  }
  return nullptr;
}

const FunctionSignature* TypeTable::functionSignature(
    SymbolId symbol_id) const {
  if (auto it = function_signatures_.find(symbol_id);
      it != function_signatures_.end()) {
    return &it->second;
  }
  return nullptr;
}

class TypeAnalyzer {
 public:
  TypeAnalyzer(const parsing::ParsedProgram& parsed,
               const AnalysisResult& analysis)
      : parsed_(&parsed), analysis_(&analysis) {}

  TypeAnalysisResult analyze() {
    if (!analysis_->ok()) {
      result_.diagnostics = analysis_->diagnostics;
      return std::move(result_);
    }

    registerBuiltins();
    registerConstructors();
    prepareFunctionSignatures();
    inferFunctionBodies();
    finalizeTypes();
    checkFunctionRestrictions();
    return std::move(result_);
  }

 private:
  const parsing::ParsedProgram* parsed_;
  const AnalysisResult* analysis_;
  TypeAnalysisResult result_;

  std::vector<InternalType> types_;
  std::unordered_map<SymbolId, TypeId> symbol_types_;
  std::unordered_map<SymbolId, InternalConstructorSignature>
      constructor_signatures_;
  std::unordered_map<SymbolId, InternalFunctionSignature> function_signatures_;

  [[nodiscard]] const ScopeTree& scopes() const { return analysis_->scopes; }

  [[nodiscard]] TypeId makeType(InternalType type) {
    const auto id = types_.size();
    types_.push_back(type);
    return id;
  }

  [[nodiscard]] TypeId makeVariable() {
    return makeType(InternalTypeVariable{});
  }

  [[nodiscard]] TypeId makeInt() { return makeType(InternalIntType{}); }

  [[nodiscard]] TypeId makeData(SymbolId symbol) {
    return makeType(InternalDataType{.symbol = symbol});
  }

  [[nodiscard]] TypeId makeFunction(TypeId parameter, TypeId result) {
    return makeType(InternalFunctionType{
        .parameter = parameter,
        .result = result,
    });
  }

  [[nodiscard]] TypeId makeFunction(std::vector<TypeId> parameters,
                                    TypeId result) {
    auto type = result;
    for (const auto parameter : parameters | std::views::reverse) {
      type = makeFunction(parameter, type);
    }
    return type;
  }

  [[nodiscard]] TypeId resolve(TypeId id) {
    auto* variable = std::get_if<InternalTypeVariable>(&types_.at(id));
    if (variable == nullptr || !variable->binding.has_value()) {
      return id;
    }

    const auto resolved = resolve(*variable->binding);
    variable->binding = resolved;
    return resolved;
  }

  [[nodiscard]] TypeId peek(TypeId id) const {
    while (true) {
      const auto* variable = std::get_if<InternalTypeVariable>(&types_.at(id));
      if (variable == nullptr || !variable->binding.has_value()) {
        return id;
      }
      id = *variable->binding;
    }
  }

  [[nodiscard]] bool occurs(TypeId variable, TypeId type) {
    type = resolve(type);
    if (variable == type) {
      return true;
    }

    const auto* function = std::get_if<InternalFunctionType>(&types_.at(type));
    return function != nullptr && (occurs(variable, function->parameter) ||
                                   occurs(variable, function->result));
  }

  [[nodiscard]] bool bindVariable(TypeId variable, TypeId type) {
    if (occurs(variable, type)) {
      return false;
    }
    std::get<InternalTypeVariable>(types_.at(variable)).binding = type;
    return true;
  }

  [[nodiscard]] bool tryUnify(TypeId actual, TypeId expected) {
    actual = resolve(actual);
    expected = resolve(expected);
    if (actual == expected) {
      return true;
    }

    if (std::holds_alternative<InternalTypeVariable>(types_.at(actual))) {
      return bindVariable(actual, expected);
    }
    if (std::holds_alternative<InternalTypeVariable>(types_.at(expected))) {
      return bindVariable(expected, actual);
    }

    if (std::holds_alternative<InternalIntType>(types_.at(actual)) &&
        std::holds_alternative<InternalIntType>(types_.at(expected))) {
      return true;
    }

    const auto* actual_data = std::get_if<InternalDataType>(&types_.at(actual));
    const auto* expected_data =
        std::get_if<InternalDataType>(&types_.at(expected));
    if (actual_data != nullptr && expected_data != nullptr) {
      return actual_data->symbol == expected_data->symbol;
    }

    const auto* actual_function =
        std::get_if<InternalFunctionType>(&types_.at(actual));
    const auto* expected_function =
        std::get_if<InternalFunctionType>(&types_.at(expected));
    if (actual_function != nullptr && expected_function != nullptr) {
      return tryUnify(actual_function->parameter,
                      expected_function->parameter) &&
             tryUnify(actual_function->result, expected_function->result);
    }

    return false;
  }

  bool unify(TypeId actual, TypeId expected, ast::NodeId node_id,
             std::string_view context) {
    if (tryUnify(actual, expected)) {
      return true;
    }

    addError(node_id,
             std::format("Type mismatch in {}: expected {}, got {}", context,
                         describe(expected), describe(actual)));
    return false;
  }

  [[nodiscard]] std::string describe(TypeId id) const {
    id = peek(id);
    return std::visit(
        util::overloaded{
            [](const InternalIntType&) -> std::string { return "Int"; },
            [&](const InternalDataType& data_type) -> std::string {
              return scopes().symbol(data_type.symbol).name;
            },
            [&](const InternalFunctionType& function) -> std::string {
              auto parameter = describe(function.parameter);
              if (std::holds_alternative<InternalFunctionType>(
                      types_.at(peek(function.parameter)))) {
                parameter = std::format("({})", parameter);
              }
              return std::format("{} -> {}", parameter,
                                 describe(function.result));
            },
            [&](const InternalTypeVariable&) -> std::string {
              return std::format("'t{}", id);
            },
        },
        types_.at(id));
  }

  [[nodiscard]] std::optional<Type> materialize(TypeId id) {
    id = resolve(id);
    return std::visit(
        util::overloaded{
            [](const InternalIntType&) -> std::optional<Type> {
              return intType();
            },
            [](const InternalDataType& data_type) -> std::optional<Type> {
              return semantics::dataType(data_type.symbol);
            },
            [&](const InternalFunctionType& function) -> std::optional<Type> {
              auto parameter = materialize(function.parameter);
              auto result = materialize(function.result);
              if (!parameter.has_value() || !result.has_value()) {
                return std::nullopt;
              }
              return functionType(std::move(*parameter), std::move(*result));
            },
            [](const InternalTypeVariable&) -> std::optional<Type> {
              return std::nullopt;
            },
        },
        types_.at(id));
  }

  void registerBuiltins() {
    const auto* int_symbol =
        scopes().localSymbol(ScopeTree::rootScopeId(), "Int");
    if (int_symbol != nullptr) {
      symbol_types_[int_symbol->id] = makeInt();
    }
  }

  void registerConstructors() {
    for (const auto& definition : parsed_->ast.definitions) {
      const auto* data_definition =
          std::get_if<ast::DataTypeDefinition>(&definition);
      if (data_definition == nullptr) {
        continue;
      }

      const auto* data_symbol =
          scopes().declaredSymbol(data_definition->id, SymbolKind::DataType);
      if (data_symbol == nullptr) {
        continue;
      }
      symbol_types_[data_symbol->id] = makeData(data_symbol->id);

      for (const auto& constructor : data_definition->constructors) {
        registerConstructor(constructor, *data_symbol);
      }
    }
  }

  void registerConstructor(const ast::Constructor& constructor,
                           const Symbol& data_symbol) {
    const auto* constructor_symbol =
        scopes().declaredSymbol(constructor.id, SymbolKind::Constructor);
    if (constructor_symbol == nullptr) {
      return;
    }

    std::vector<TypeId> fields;
    fields.reserve(constructor.fields.size());
    for (const auto& field : constructor.fields) {
      const auto* type_symbol =
          scopes().localSymbol(ScopeTree::rootScopeId(), field);
      if (type_symbol == nullptr || !isTypeSymbol(type_symbol->kind)) {
        addError(constructor.id, std::format("Unknown type '{}'", field));
        return;
      }
      fields.push_back(typeOfTypeSymbol(*type_symbol));
    }

    const auto result = makeData(data_symbol.id);
    const auto callable_type = makeFunction(fields, result);
    symbol_types_[constructor_symbol->id] = callable_type;
    constructor_signatures_.emplace(constructor_symbol->id,
                                    InternalConstructorSignature{
                                        .data_type = data_symbol.id,
                                        .fields = std::move(fields),
                                    });
  }

  [[nodiscard]] TypeId typeOfTypeSymbol(const Symbol& symbol) {
    if (symbol.kind == SymbolKind::BuiltinType) {
      return makeInt();
    }
    return makeData(symbol.id);
  }

  void prepareFunctionSignatures() {
    for (const auto& definition : parsed_->ast.definitions) {
      const auto* function = std::get_if<ast::FunctionDefinition>(&definition);
      if (function == nullptr) {
        continue;
      }
      prepareFunctionSignature(*function);
    }
  }

  void prepareFunctionSignature(const ast::FunctionDefinition& function) {
    const auto* function_symbol =
        scopes().declaredSymbol(function.id, SymbolKind::Function);
    const auto function_scope = scopes().scopeOf(function.id);
    if (function_symbol == nullptr || !function_scope.has_value()) {
      return;
    }

    std::vector<TypeId> parameters;
    parameters.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
      const auto* parameter_symbol =
          scopes().localSymbol(*function_scope, parameter);
      if (parameter_symbol == nullptr) {
        continue;
      }

      const auto parameter_type = makeVariable();
      parameters.push_back(parameter_type);
      symbol_types_[parameter_symbol->id] = parameter_type;
    }

    const auto result = makeVariable();
    const auto callable_type = makeFunction(parameters, result);
    symbol_types_[function_symbol->id] = callable_type;
    function_signatures_.emplace(function_symbol->id,
                                 InternalFunctionSignature{
                                     .parameters = std::move(parameters),
                                     .result = result,
                                 });
  }

  void inferFunctionBodies() {
    for (const auto& definition : parsed_->ast.definitions) {
      const auto* function = std::get_if<ast::FunctionDefinition>(&definition);
      if (function == nullptr) {
        continue;
      }

      const auto* function_symbol =
          scopes().declaredSymbol(function->id, SymbolKind::Function);
      if (function_symbol == nullptr) {
        continue;
      }

      const auto signature = function_signatures_.find(function_symbol->id);
      if (signature == function_signatures_.end()) {
        continue;
      }

      const auto body_type = inferExpression(*function->body);
      unify(body_type, signature->second.result, expressionId(*function->body),
            std::format("function '{}'", function->name));
    }
  }

  [[nodiscard]] TypeId inferExpression(const ast::Expression& expression) {
    const auto type =
        std::visit(util::overloaded{
                       [&](const ast::IntLiteral& literal) {
                         return inferIntLiteral(literal);
                       },
                       [&](const ast::Variable& variable) {
                         return inferVariable(variable);
                       },
                       [&]<ast::IsBinaryOperator Op>(const Op& op) {
                         return inferBinaryOperator(op);
                       },
                       [&](const ast::Application& application) {
                         return inferApplication(application);
                       },
                       [&](const ast::CaseExpression& case_expression) {
                         return inferCaseExpression(case_expression);
                       },
                   },
                   expression);
    return type;
  }

  [[nodiscard]] TypeId inferIntLiteral(
      [[maybe_unused]] const ast::IntLiteral& literal) {
    return makeInt();
  }

  [[nodiscard]] TypeId inferVariable(const ast::Variable& variable) {
    const auto* symbol = scopes().resolvedSymbol(variable.id);
    if (symbol == nullptr) {
      return makeVariable();
    }

    if (auto it = symbol_types_.find(symbol->id); it != symbol_types_.end()) {
      return it->second;
    }

    return makeVariable();
  }

  template <ast::IsBinaryOperator Op>
  [[nodiscard]] TypeId inferBinaryOperator(const Op& op) {
    const auto left = inferExpression(*op.left_operand);
    const auto right = inferExpression(*op.right_operand);
    const auto integer = makeInt();
    unify(left, integer, expressionId(*op.left_operand),
          "arithmetic left operand");
    unify(right, integer, expressionId(*op.right_operand),
          "arithmetic right operand");

    const auto result = makeInt();
    return result;
  }

  [[nodiscard]] TypeId inferApplication(const ast::Application& application) {
    const auto function = inferExpression(*application.function);
    const auto argument = inferExpression(*application.argument);
    const auto result = makeVariable();
    const auto expected_function = makeFunction(argument, result);
    unify(function, expected_function, application.id, "function application");

    return result;
  }

  [[nodiscard]] TypeId inferCaseExpression(
      const ast::CaseExpression& case_expression) {
    const auto scrutinee = inferExpression(*case_expression.scrutinee);
    const auto result = makeVariable();

    for (const auto& branch : case_expression.branches) {
      inferPattern(branch.pattern, scrutinee);
      const auto body = inferExpression(*branch.body);
      unify(body, result, expressionId(*branch.body), "case branch result");
    }

    return result;
  }

  void inferPattern(const ast::Pattern& pattern, TypeId expected) {
    std::visit(util::overloaded{
                   [&](const ast::VariablePattern& variable_pattern) {
                     inferVariablePattern(variable_pattern, expected);
                   },
                   [&](const ast::ConstructorPattern& constructor_pattern) {
                     inferConstructorPattern(constructor_pattern, expected);
                   },
               },
               pattern);
  }

  void inferVariablePattern(const ast::VariablePattern& pattern,
                            TypeId expected) {
    const auto* symbol =
        scopes().declaredSymbol(pattern.id, SymbolKind::PatternVariable);
    if (symbol != nullptr) {
      symbol_types_[symbol->id] = expected;
    }
  }

  void inferConstructorPattern(const ast::ConstructorPattern& pattern,
                               TypeId expected) {
    const auto* constructor = scopes().resolvedSymbol(pattern.id);
    if (constructor == nullptr) {
      return;
    }

    const auto signature = constructor_signatures_.find(constructor->id);
    if (signature == constructor_signatures_.end()) {
      return;
    }

    unify(expected, makeData(signature->second.data_type), pattern.id,
          "constructor pattern");

    if (pattern.arguments.size() != signature->second.fields.size()) {
      addError(pattern.id,
               std::format("Constructor '{}' expects {} arguments, got {}",
                           pattern.name, signature->second.fields.size(),
                           pattern.arguments.size()));
      return;
    }

    const auto* bindings = scopes().declaredSymbolIds(pattern.id);
    if (bindings == nullptr ||
        bindings->size() != signature->second.fields.size()) {
      return;
    }

    for (const auto [binding, field] :
         std::views::zip(*bindings, signature->second.fields)) {
      symbol_types_[binding] = field;
    }
  }

  [[nodiscard]] static ast::NodeId expressionId(
      const ast::Expression& expression) {
    return std::visit([](const auto& node) { return node.id; }, expression);
  }

  void finalizeTypes() {
    finalizeSymbolTypes();
    finalizeConstructorSignatures();
    finalizeFunctionSignatures();
  }

  void finalizeSymbolTypes() {
    for (const auto& [symbol_id, type_id] : symbol_types_) {
      if (materialize(type_id).has_value()) {
        continue;
      }

      const auto& symbol = scopes().symbol(symbol_id);
      addError(
          symbol.declaration,
          std::format("Could not infer concrete type of '{}'", symbol.name));
    }
  }

  void finalizeConstructorSignatures() {
    for (const auto& [symbol_id, signature] : constructor_signatures_) {
      auto fields = materializeAll(signature.fields);
      if (!fields.has_value()) {
        continue;
      }

      result_.types.constructor_signatures_.emplace(
          symbol_id, ConstructorSignature{
                         .data_type = signature.data_type,
                         .fields = std::move(*fields),
                     });
    }
  }

  void finalizeFunctionSignatures() {
    for (const auto& [symbol_id, signature] : function_signatures_) {
      auto parameters = materializeAll(signature.parameters);
      auto result = materialize(signature.result);
      if (!parameters.has_value() || !result.has_value()) {
        continue;
      }

      result_.types.function_signatures_.emplace(
          symbol_id, FunctionSignature{
                         .parameters = std::move(*parameters),
                         .result = std::move(*result),
                     });
    }
  }

  [[nodiscard]] std::optional<std::vector<Type>> materializeAll(
      const std::vector<TypeId>& type_ids) {
    std::vector<Type> types;
    types.reserve(type_ids.size());
    for (const auto type_id : type_ids) {
      auto type = materialize(type_id);
      if (!type.has_value()) {
        return std::nullopt;
      }
      types.push_back(std::move(*type));
    }
    return types;
  }

  void checkFunctionRestrictions() {
    for (const auto& [symbol_id, signature] :
         result_.types.function_signatures_) {
      const auto& symbol = scopes().symbol(symbol_id);

      if (symbol.name == "main" && !signature.parameters.empty()) {
        addError(symbol.declaration,
                 "Function 'main' must not have parameters");
      }

      for (const auto& parameter : signature.parameters) {
        if (containsFunctionType(parameter)) {
          addError(symbol.declaration,
                   std::format("Function '{}' has function-typed parameter; "
                               "higher-order functions are not supported",
                               symbol.name));
        }
      }

      if (containsFunctionType(signature.result)) {
        addError(symbol.declaration,
                 std::format("Function '{}' returns a function; partial "
                             "application is not supported",
                             symbol.name));
      }
    }
  }

  void addError(ast::NodeId node_id, std::string message) {
    result_.diagnostics.push_back(
        makeDiagnostic(*parsed_, node_id, std::move(message)));
  }
};

TypeAnalysisResult analyzeTypes(const parsing::ParsedProgram& parsed,
                                const AnalysisResult& analysis) {
  return TypeAnalyzer{parsed, analysis}.analyze();
}

}  // namespace semantics
