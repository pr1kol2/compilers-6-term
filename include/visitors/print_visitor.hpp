#pragma once

#include <string>

#include "parsing/ast.hpp"

namespace visitors {

std::string print(const ast::Program& program);
std::string print(const ast::Definition& definition);
std::string print(const ast::Expression& expression);

}  // namespace visitors
