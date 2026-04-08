#include "visitors/print_visitor.hpp"

#include <format>
#include <string>
#include <variant>

#include "parsing/ast.hpp"
#include "parsing/ast_traits.hpp"
#include "util/overloaded.hpp"

namespace visitors {

namespace {

std::string printPattern(const ast::Pattern& pattern) {
  return std::visit(
      util::overloaded{
          [](const ast::VariablePattern& vp) -> std::string { return vp.name; },
          [](const ast::ConstructorPattern& cp) -> std::string {
            std::string result = cp.name;
            for (const auto& arg : cp.arguments) {
              result += ' ';
              result += arg;
            }
            return result;
          },
      },
      pattern);
}

std::string printConstructor(const ast::Constructor& ctor) {
  std::string result = ctor.name;
  for (const auto& field : ctor.fields) {
    result += ' ';
    result += field;
  }
  return result;
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
            std::string branches;
            for (const auto& branch : ce.branches) {
              if (!branches.empty()) {
                branches += ' ';
              }
              branches +=
                  std::format("{} -> {{ {} }}", printPattern(branch.pattern),
                              print(*branch.body));
            }
            return std::format("case {} of {{ {} }}", print(*ce.scrutinee),
                               branches);
          },
      },
      expr);
}

std::string print(const ast::Definition& definition) {
  return std::visit(util::overloaded{
                        [](const ast::FunctionDefinition& fd) -> std::string {
                          std::string params;
                          for (const auto& p : fd.parameters) {
                            params += ' ';
                            params += p;
                          }
                          return std::format("defn {}{} = {{ {} }}", fd.name,
                                             params, print(*fd.body));
                        },
                        [](const ast::DataTypeDefinition& dt) -> std::string {
                          std::string constructors;
                          for (const auto& ctor : dt.constructors) {
                            if (!constructors.empty()) {
                              constructors += ", ";
                            }
                            constructors += printConstructor(ctor);
                          }
                          return std::format("data {} = {{ {} }}", dt.name,
                                             constructors);
                        },
                    },
                    definition);
}

std::string print(const ast::Program& program) {
  std::string result;
  for (const auto& def : program.definitions) {
    if (!result.empty()) {
      result += '\n';
    }
    result += print(def);
  }
  return result;
}

}  // namespace visitors
