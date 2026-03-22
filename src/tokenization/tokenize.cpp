#include "tokenization/tokenize.hpp"

#include <sstream>

namespace tokenization {

std::vector<std::string> Tokenize(std::string_view source) {
  std::istringstream input{std::string(source)};
  std::vector<std::string> tokens;
  std::string token;

  while (input >> token) {
    tokens.push_back(token);
  }

  return tokens;
}

}  // namespace tokenization
