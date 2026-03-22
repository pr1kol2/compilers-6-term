#include "parsing/parse.hpp"

namespace parsing {

ParseResult Parse(const std::vector<std::string>& tokens) {
  return ParseResult{
      .success = !tokens.empty(),
      .token_count = tokens.size(),
      .root = tokens.empty() ? "empty" : "program",
  };
}

}  // namespace parsing
