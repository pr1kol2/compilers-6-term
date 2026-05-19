#include <gtest/gtest.h>

#include "parsing/ast.hpp"
#include "semantics/symbol_table.hpp"
#include "test_utils.hpp"

// NOLINTBEGIN

using namespace ast;
using test_utils::analyzeSource;
using test_utils::analyzeTypesSource;
using test_utils::asApplication;
using test_utils::asCase;
using test_utils::asConstructorPattern;
using test_utils::asVariable;
using test_utils::functionAt;
using test_utils::functionBody;
using test_utils::hasDiagnosticContaining;
using test_utils::parseSource;

TEST(Semantics, RegistersGlobalSymbolsAndBuiltins) {
  auto parsed = parseSource(
      "data List = { Nil, Cons Int List } "
      "defn main = { Nil }");
  auto result = semantics::analyze(parsed);
  ASSERT_TRUE(result.ok());

  const auto root = result.scopes.rootScopeId();
  const auto* int_type = result.scopes.localSymbol(root, "Int");
  const auto* list_type = result.scopes.localSymbol(root, "List");
  const auto* nil = result.scopes.localSymbol(root, "Nil");
  const auto* cons = result.scopes.localSymbol(root, "Cons");
  const auto* main = result.scopes.localSymbol(root, "main");

  ASSERT_NE(int_type, nullptr);
  EXPECT_EQ(int_type->kind, semantics::SymbolKind::BuiltinType);
  ASSERT_NE(list_type, nullptr);
  EXPECT_EQ(list_type->kind, semantics::SymbolKind::DataType);
  EXPECT_EQ(list_type->arity, 2);
  ASSERT_NE(nil, nullptr);
  EXPECT_EQ(nil->kind, semantics::SymbolKind::Constructor);
  EXPECT_EQ(nil->arity, 0);
  ASSERT_NE(cons, nullptr);
  EXPECT_EQ(cons->kind, semantics::SymbolKind::Constructor);
  EXPECT_EQ(cons->arity, 2);
  ASSERT_NE(main, nullptr);
  EXPECT_EQ(main->kind, semantics::SymbolKind::Function);
  EXPECT_EQ(main->arity, 0);
}

TEST(Semantics, BindsFunctionParametersInFunctionScope) {
  auto parsed = parseSource("defn id x = { x }");
  auto result = semantics::analyze(parsed);
  ASSERT_TRUE(result.ok());

  const auto& function = functionAt(parsed.ast, 0);
  const auto function_scope = result.scopes.scopeOf(function.id);
  ASSERT_TRUE(function_scope.has_value());

  const auto* parameter = result.scopes.localSymbol(*function_scope, "x");
  ASSERT_NE(parameter, nullptr);
  EXPECT_EQ(parameter->kind, semantics::SymbolKind::Parameter);
  EXPECT_EQ(parameter->declaration, function.id);

  const auto& variable = asVariable(functionBody(parsed.ast));
  const auto* resolved = result.scopes.resolvedSymbol(variable.id);
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->kind, semantics::SymbolKind::Parameter);
  EXPECT_EQ(resolved->declaration, function.id);
  const auto resolved_id = result.scopes.resolvedSymbolId(variable.id);
  ASSERT_TRUE(resolved_id.has_value());
  EXPECT_EQ(*resolved_id, parameter->id);
}

TEST(Semantics, AllowsForwardFunctionReferences) {
  auto parsed = parseSource("defn main = { id 42 } defn id x = { x }");
  auto result = semantics::analyze(parsed);
  ASSERT_TRUE(result.ok());

  const auto& application = asApplication(functionBody(parsed.ast));
  const auto& callee = asVariable(*application.function);
  const auto& id_function = functionAt(parsed.ast, 1);

  const auto* resolved = result.scopes.resolvedSymbol(callee.id);
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->kind, semantics::SymbolKind::Function);
  EXPECT_EQ(resolved->declaration, id_function.id);
}

TEST(Semantics, ReportsUndefinedVariable) {
  auto result = analyzeSource("defn main = { missing }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(result, "Undefined symbol 'missing'"));
}

TEST(Semantics, ReportsGlobalRedefinition) {
  auto result = analyzeSource("defn f = { 1 } defn f = { 2 }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(result, "Redefinition of 'f'"));
}

TEST(Semantics, ReportsDuplicateFunctionParameter) {
  auto result = analyzeSource("defn f x x = { x }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(result, "Redefinition of 'x'"));
}

TEST(Semantics, PatternVariableShadowsFunctionParameter) {
  auto parsed = parseSource(
      "data Wrap = { W Int } "
      "defn f x = { case W 1 of { W x -> { x } } }");
  auto result = semantics::analyze(parsed);
  ASSERT_TRUE(result.ok());

  const auto& case_expression = asCase(functionBody(parsed.ast, 1));
  const auto& branch = case_expression.branches.front();
  const auto& pattern = asConstructorPattern(branch.pattern);
  const auto& branch_body = asVariable(*branch.body);

  const auto* resolved = result.scopes.resolvedSymbol(branch_body.id);
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->kind, semantics::SymbolKind::PatternVariable);
  EXPECT_EQ(resolved->declaration, pattern.id);
}

TEST(Semantics, CaseBranchPatternBindingsAreIsolated) {
  auto result = analyzeSource(
      "data Wrap = { W Int } "
      "defn main = { case W 1 of { W x -> { x } W y -> { x } } }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(result, "Undefined symbol 'x'"));
}

TEST(Semantics, ReportsConstructorPatternArityMismatch) {
  auto result = analyzeSource(
      "data Wrap = { W Int } "
      "defn main = { case W 1 of { W x y -> { x } } }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(
      result, "Constructor 'W' expects 1 arguments, got 2"));
}

TEST(Semantics, ReportsUnknownConstructorFieldType) {
  auto result = analyzeSource("data Bad = { B Missing } defn main = { 0 }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(result, "Unknown type 'Missing'"));
}

TEST(Semantics, ReportsDuplicatePatternBinding) {
  auto result = analyzeSource(
      "data Pair = { P Int Int } "
      "defn main = { case P 1 2 of { P x x -> { x } } }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(result, "Redefinition of 'x'"));
}

TEST(Semantics, InfersFunctionAndConstructorTypes) {
  auto parsed = parseSource(
      "data List = { Nil, Cons Int List } "
      "defn head xs = { case xs of { Cons x rest -> { x } Nil -> { 0 } } } "
      "defn main = { head (Cons 1 Nil) }");
  auto analysis = semantics::analyze(parsed);
  auto result = semantics::analyzeTypes(parsed, analysis);
  ASSERT_TRUE(result.ok());

  const auto root = semantics::ScopeTree::rootScopeId();
  const auto* list = analysis.scopes.localSymbol(root, "List");
  const auto* cons = analysis.scopes.localSymbol(root, "Cons");
  const auto* head = analysis.scopes.localSymbol(root, "head");
  const auto* main = analysis.scopes.localSymbol(root, "main");
  ASSERT_NE(list, nullptr);
  ASSERT_NE(cons, nullptr);
  ASSERT_NE(head, nullptr);
  ASSERT_NE(main, nullptr);

  const auto* cons_signature = result.types.constructorSignature(cons->id);
  ASSERT_NE(cons_signature, nullptr);
  EXPECT_EQ(cons_signature->data_type, list->id);
  ASSERT_EQ(cons_signature->fields.size(), 2);
  EXPECT_EQ(cons_signature->fields.front(), semantics::intType());
  EXPECT_EQ(cons_signature->fields.back(), semantics::dataType(list->id));
  EXPECT_EQ(cons_signature->result, semantics::dataType(list->id));

  const auto* head_signature = result.types.functionSignature(head->id);
  ASSERT_NE(head_signature, nullptr);
  ASSERT_EQ(head_signature->parameters.size(), 1);
  EXPECT_EQ(head_signature->parameters.front(), semantics::dataType(list->id));
  EXPECT_EQ(head_signature->result, semantics::intType());

  const auto* main_signature = result.types.functionSignature(main->id);
  ASSERT_NE(main_signature, nullptr);
  EXPECT_TRUE(main_signature->parameters.empty());
  EXPECT_EQ(main_signature->result, semantics::intType());
}

TEST(Semantics, ReportsConstructorPatternFromDifferentDataType) {
  auto result = analyzeTypesSource(
      "data TA = { A } data TB = { B } "
      "defn main = { case A of { B -> { 1 } } }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(
      hasDiagnosticContaining(result, "Type mismatch in constructor pattern"));
}

TEST(Semantics, ReportsDifferentCaseBranchTypes) {
  auto result = analyzeTypesSource(
      "data Bool = { True, False } data TUnit = { Unit } "
      "defn main = { case True of { "
      "True -> { 1 } False -> { Unit } } }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(
      hasDiagnosticContaining(result, "Type mismatch in case branch result"));
}

TEST(Semantics, ReportsConstructorArgumentTypeMismatch) {
  auto result = analyzeTypesSource(
      "data Bool = { True } data List = { Nil, Cons Int List } "
      "defn main = { Cons True Nil }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(
      hasDiagnosticContaining(result, "Type mismatch in function application"));
}

TEST(Semantics, ReportsArithmeticOverNonInt) {
  auto result =
      analyzeTypesSource("data Bool = { True } defn main = { True + 1 }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(
      result, "Type mismatch in arithmetic left operand"));
}

TEST(Semantics, ReportsPartialApplicationAsFunctionResult) {
  auto result = analyzeTypesSource(
      "data List = { Nil, Cons Int List } defn main = { Cons 1 }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(
      hasDiagnosticContaining(result, "partial application is not supported"));
}

TEST(Semantics, ReportsUnresolvedMonomorphicType) {
  auto result = analyzeTypesSource("defn id x = { x }");
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(hasDiagnosticContaining(result, "Could not infer concrete type"));
}

// NOLINTEND
