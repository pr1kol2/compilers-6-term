#pragma once

#include <string_view>
#include <vector>

#include "tokenization/token.hpp"

namespace tokenization {

std::vector<Token> tokenize(std::string_view source);

}  // namespace tokenization
