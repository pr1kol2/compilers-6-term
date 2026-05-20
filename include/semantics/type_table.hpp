#pragma once

#include <unordered_map>
#include <variant>
#include <vector>

#include "parsing/parse.hpp"
#include "semantics/symbol_table.hpp"
#include "util/box.hpp"

namespace semantics {

struct IntType {
  friend bool operator==(const IntType&, const IntType&) = default;
};

struct DataType {
  SymbolId symbol = kInvalidSymbolId;

  friend bool operator==(const DataType&, const DataType&) = default;
};

struct FunctionType;

using TypeVariant = std::variant<IntType, DataType, Box<FunctionType>>;

struct Type : TypeVariant {
  using Variant = TypeVariant;
  using TypeVariant::TypeVariant;
  using TypeVariant::operator=;

  friend bool operator==(const Type&, const Type&) = default;
};

struct FunctionType {
  Box<Type> parameter;
  Box<Type> result;

  friend bool operator==(const FunctionType&, const FunctionType&) = default;
};

[[nodiscard]] Type intType();
[[nodiscard]] Type dataType(SymbolId symbol);

struct ConstructorSignature {
  SymbolId data_type = kInvalidSymbolId;
  std::vector<Type> fields;

  friend bool operator==(const ConstructorSignature&,
                         const ConstructorSignature&) = default;
};

struct FunctionSignature {
  std::vector<Type> parameters;
  Type result = intType();

  friend bool operator==(const FunctionSignature&,
                         const FunctionSignature&) = default;
};

class TypeTable {
 public:
  [[nodiscard]] const ConstructorSignature* constructorSignature(
      SymbolId symbol_id) const;
  [[nodiscard]] const FunctionSignature* functionSignature(
      SymbolId symbol_id) const;

 private:
  std::unordered_map<SymbolId, ConstructorSignature> constructor_signatures_;
  std::unordered_map<SymbolId, FunctionSignature> function_signatures_;

  friend class TypeAnalyzer;
};

struct TypeAnalysisResult {
  TypeTable types;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

[[nodiscard]] TypeAnalysisResult analyzeTypes(
    const parsing::ParsedProgram& parsed, const AnalysisResult& analysis);

}  // namespace semantics
