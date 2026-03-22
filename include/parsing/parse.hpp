#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace parsing {

struct ParseResult {
  bool success;
  std::size_t token_count;
  std::string root;
};

ParseResult Parse(const std::vector<std::string>& tokens);

}  // namespace parsing
