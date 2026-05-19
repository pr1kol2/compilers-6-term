#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "parsing/ast.hpp"
#include "parsing/parse.hpp"

namespace semantics {

struct StringHash {
  using is_transparent = void;

  [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
};

using ScopeId = std::size_t;
inline constexpr ScopeId kInvalidScopeId = std::numeric_limits<ScopeId>::max();

using SymbolId = std::size_t;
inline constexpr SymbolId kInvalidSymbolId =
    std::numeric_limits<SymbolId>::max();

enum class SymbolKind : std::uint8_t {
  BuiltinType,
  DataType,
  Constructor,
  Function,
  Parameter,
  PatternVariable,
};

[[nodiscard]] std::string_view toString(SymbolKind kind);
[[nodiscard]] bool isTypeSymbol(SymbolKind kind);
[[nodiscard]] bool isValueSymbol(SymbolKind kind);

struct Symbol {
  SymbolId id = kInvalidSymbolId;
  ScopeId scope = kInvalidScopeId;
  std::string name;
  SymbolKind kind = SymbolKind::BuiltinType;
  ast::NodeId declaration = ast::kInvalidNodeId;
  std::size_t arity = 0;

  friend bool operator==(const Symbol&, const Symbol&) = default;
};

struct Scope {
  ScopeId id = kInvalidScopeId;
  ScopeId parent = kInvalidScopeId;
  ast::NodeId owner = ast::kInvalidNodeId;
  std::unordered_map<std::string, SymbolId, StringHash, std::equal_to<>>
      symbols;
  std::vector<ScopeId> children;
};

class ScopeTree {
 public:
  ScopeTree();

  [[nodiscard]] static ScopeId rootScopeId() { return 0; }
  [[nodiscard]] const Scope& scope(ScopeId id) const;
  [[nodiscard]] const Symbol& symbol(SymbolId id) const;

  [[nodiscard]] std::optional<ScopeId> scopeOf(ast::NodeId node_id) const;
  [[nodiscard]] std::optional<SymbolId> localSymbolId(
      ScopeId scope_id, std::string_view name) const;
  [[nodiscard]] const Symbol* localSymbol(ScopeId scope_id,
                                          std::string_view name) const;
  [[nodiscard]] std::optional<SymbolId> resolveId(ScopeId scope_id,
                                                  std::string_view name) const;
  [[nodiscard]] std::optional<SymbolId> resolvedSymbolId(
      ast::NodeId node_id) const;
  [[nodiscard]] const Symbol* resolvedSymbol(ast::NodeId node_id) const;
  [[nodiscard]] const std::vector<SymbolId>* declaredSymbolIds(
      ast::NodeId node_id) const;
  [[nodiscard]] const Symbol* declaredSymbol(ast::NodeId node_id,
                                             SymbolKind kind) const;

 private:
  [[nodiscard]] ScopeId createScope(ScopeId parent, ast::NodeId owner);
  void bindNodeToScope(ast::NodeId node_id, ScopeId scope_id);
  void bindNodeToSymbol(ast::NodeId node_id, SymbolId symbol_id);
  [[nodiscard]] std::optional<SymbolId> addLocalSymbol(ScopeId scope_id,
                                                       Symbol symbol);

  std::vector<Scope> scopes_;
  std::vector<Symbol> symbols_;
  std::unordered_map<ast::NodeId, ScopeId> scope_by_node_;
  std::unordered_map<ast::NodeId, SymbolId> resolved_symbols_;
  std::unordered_map<ast::NodeId, std::vector<SymbolId>> declared_symbols_;

  friend class Analyzer;
};

struct Diagnostic {
  ast::NodeId node_id = ast::kInvalidNodeId;
  std::string message;
};

[[nodiscard]] std::string positionOf(const parsing::ParsedProgram& parsed,
                                     ast::NodeId node_id);
[[nodiscard]] Diagnostic makeDiagnostic(const parsing::ParsedProgram& parsed,
                                        ast::NodeId node_id,
                                        std::string message);

struct AnalysisResult {
  ScopeTree scopes;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

[[nodiscard]] AnalysisResult analyze(const parsing::ParsedProgram& parsed);

}  // namespace semantics
