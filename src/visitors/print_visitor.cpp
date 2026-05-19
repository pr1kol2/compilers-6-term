#include "visitors/print_visitor.hpp"

#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "util/overloaded.hpp"

namespace visitors {

namespace {

template <std::ranges::input_range Range, typename Formatter>
std::string joinFormatted(const Range& range, std::string_view separator,
                          Formatter formatter) {
  std::string result;
  for (const auto& item : range) {
    if (!result.empty()) {
      result += separator;
    }
    result += formatter(item);
  }
  return result;
}

std::string asString(std::string_view value) { return std::string(value); }

template <std::ranges::input_range Range>
std::string printHeadWithArguments(std::string_view head, const Range& args) {
  auto tail = joinFormatted(args, " ", asString);
  if (tail.empty()) {
    return std::string(head);
  }
  return std::format("{} {}", head, tail);
}

std::string printPattern(const ast::Pattern& pattern) {
  return std::visit(
      util::overloaded{
          [](const ast::VariablePattern& vp) -> std::string { return vp.name; },
          [](const ast::ConstructorPattern& cp) -> std::string {
            return printHeadWithArguments(cp.name, cp.arguments);
          },
      },
      pattern);
}

std::string printConstructor(const ast::Constructor& ctor) {
  return printHeadWithArguments(ctor.name, ctor.fields);
}

}  // namespace

std::string print(const ast::Expression& expr) {
  return std::visit(
      util::overloaded{
          [](const ast::IntLiteral& lit) -> std::string {
            return std::to_string(lit.value);
          },
          [](const ast::Variable& var) -> std::string { return var.name; },
          []<ast::IsBinaryOperator Op>(const Op& op) -> std::string {
            return std::format("({} {} {})", print(*op.left_operand),
                               ast::symbolOf<Op>(), print(*op.right_operand));
          },
          [](const ast::Application& app) -> std::string {
            return std::format("({} {})", print(*app.function),
                               print(*app.argument));
          },
          [](const ast::CaseExpression& ce) -> std::string {
            auto branches =
                joinFormatted(ce.branches, " ", [](const auto& branch) {
                  return std::format("{} -> {{ {} }}",
                                     printPattern(branch.pattern),
                                     print(*branch.body));
                });
            return std::format("case {} of {{ {} }}", print(*ce.scrutinee),
                               branches);
          },
      },
      expr);
}

std::string print(const ast::Definition& definition) {
  return std::visit(
      util::overloaded{
          [](const ast::FunctionDefinition& fd) -> std::string {
            auto params = joinFormatted(fd.parameters, " ", asString);
            if (!params.empty()) {
              params = std::format(" {}", params);
            }
            return std::format("defn {}{} = {{ {} }}", fd.name, params,
                               print(*fd.body));
          },
          [](const ast::DataTypeDefinition& dt) -> std::string {
            auto constructors =
                joinFormatted(dt.constructors, ", ", printConstructor);
            return std::format("data {} = {{ {} }}", dt.name, constructors);
          },
      },
      definition);
}

std::string print(const ast::Program& program) {
  return joinFormatted(program.definitions, "\n", [](const auto& definition) {
    return print(definition);
  });
}

}  // namespace visitors
